// ffmpeg_pusher.cpp
#include "ffmpeg_pusher.hh"
#include "ffmpeg_metwork_init.hh"

// #include <thread>
// #include <chrono>

#define MAX_RETRIES 10

FFmpegPusher::FFmpegPusher(const std::string &url, int w, int h, int fr, const std::string &prot)
    : rtmpUrl(url), width(w), height(h), frameRate(fr), protocol(prot)
{
}

FFmpegPusher::~FFmpegPusher()
{
    close();
}

bool FFmpegPusher::init()
{
    // 初始化FFmpeg库
    FFmpegNetworkInitializer::init();
    // 确定输出格式
    std::string formatName = "flv"; // RTMP默认使用flv格式
    if (protocol == "rtsp")
    {
        formatName = "rtsp";
    }

    // 分配输出格式上下文
    if (avformat_alloc_output_context2(&formatContext, nullptr,
                                       formatName.c_str(), rtmpUrl.c_str()) < 0)
    {
        std::cerr << "无法创建输出上下文 (协议: " << protocol << ")" << std::endl;
        return false;
    }

    // 查找编码器  硬解可以改用H265推流
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    // codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
    if (!codec)
    {
        std::cerr << "未找到H.264编码器" << std::endl;
        return false;
    }

    // 创建编码器上下文
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext)
    {
        std::cerr << "无法分配编码器上下文" << std::endl;
        return false;
    }

    // 设置编码器参数 硬解可以改用H265推流
    codecContext->codec_id = AV_CODEC_ID_H264;
    // codecContext->codec_id = AV_CODEC_ID_HEVC;
    codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    codecContext->width = width;
    codecContext->height = height;
    codecContext->time_base = {1, frameRate};
    // codecContext->framerate = {frameRate, 1};
    // 设置目标码率
    codecContext->bit_rate = 1024000; // 平均要求 1024 kbps
    // 设置最大码率和缓冲区大小
    codecContext->rc_max_rate = 2048000;    // 最大限制 2048 kbps
    codecContext->rc_buffer_size = 4096000; // 缓冲区大小：最大码率缓存池[低延迟优先，缓冲区大小与目标码率相同, 平常最大码率的2-5倍]
    codecContext->gop_size = 50;
    codecContext->max_b_frames = 1;
    // 强制第一帧为关键帧
    codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 设置编码器选项
    AVDictionary *options = nullptr;
    // av_dict_set(&options, "preset", "ultrafast", 0); // 使用快速压缩[带宽占用多]
    av_dict_set(&options, "preset", "medium", 0); // 使用普通压缩[性能占用多]
    av_dict_set(&options, "crf", "23", 0);        // 追求画质优先 设置 CRF 值 (H264 CRF 值差不多是23)

    // 打开编码器
    if (avcodec_open2(codecContext, codec, &options) < 0)
    {
        std::cerr << "无法打开编码器" << std::endl;
        // av_dict_free(&options);
        return false;
    }
    av_dict_free(&options);

    // 创建输出流
    stream = avformat_new_stream(formatContext, codec);
    if (!stream)
    {
        std::cerr << "无法创建输出流" << std::endl;
        return false;
    }
    stream->time_base = codecContext->time_base;

    // 复制编码器参数到输出流
    if (avcodec_parameters_from_context(stream->codecpar, codecContext) < 0)
    {
        std::cerr << "无法复制编码器参数到输出流" << std::endl;
        return false;
    }

    AVDictionary *format_options = nullptr;
    // RTSP特殊设置
    if (protocol == "rtsp")
    {
        av_dict_set(&format_options, "rtsp_transport", "tcp", 0); // 使用TCP传输
    }
    else if (protocol == "rtmp")
    {
        // 对于RTMP，不关心文件大小和时长
        // formatContext->oformat->flags |= AVFMT_NOTIMESTAMPS;
        formatContext->flags |= AVFMT_NOTIMESTAMPS; // 正确设置标志的方法
        // 设置flvflags
        av_dict_set(&format_options, "flvflags", "no_duration_filesize", 0);
    }

    av_dict_set(&format_options, "tune", "zerolatency", 0);
    av_dict_set(&format_options, "fflags", "nobuffer", 0);

    // 打开输出URL
    if (!(formatContext->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open2(&formatContext->pb, rtmpUrl.c_str(), AVIO_FLAG_WRITE, nullptr, &format_options) < 0)
        {
            std::cerr << "无法打开输出URL (协议: " << protocol << ")" << std::endl;
            return false;
        }
    }

    av_dict_free(&format_options);

    // 写入文件头
    if (avformat_write_header(formatContext, nullptr) < 0)
    {
        std::cerr << "写入头信息失败" << std::endl;
        return false;
    }

    // 分配帧和包
    frame = av_frame_alloc();
    if (!frame)
    {
        std::cerr << "无法分配视频帧" << std::endl;
        return false;
    }
    frame->format = codecContext->pix_fmt;
    frame->width = codecContext->width;
    frame->height = codecContext->height;
    if (av_frame_get_buffer(frame, 0) < 0)
    {
        std::cerr << "无法分配视频帧数据" << std::endl;
        return false;
    }

    packet = av_packet_alloc();
    if (!packet)
    {
        std::cerr << "无法分配数据包" << std::endl;
        return false;
    }

    // 初始化 SWS 上下文用于 RGB 到 YUV420P 的转换
    swsContext = sws_getContext(
        width, height, AV_PIX_FMT_RGB24,
        width, height, codecContext->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsContext)
    {
        std::cerr << "无法初始化 SWS 上下文" << std::endl;
        return false;
    }

    std::cout << "推流器初始化成功: "
              << "协议=" << protocol << ", 尺寸=" << width << "x" << height
              << ", 帧率=" << frameRate << std::endl;

    initialized = true;
    return true;
}

bool FFmpegPusher::pushFrame(cv::Mat &inFrame)
{
    if (!initialized || inFrame.empty())
        return false;

    if (inFrame.cols != width || inFrame.rows != height)
    {
        std::cerr << "帧尺寸与编码器尺寸不匹配" << std::endl;
        return false;
    }

    // static auto lastFrameTime = std::chrono::steady_clock::now();
    // auto currentTime = std::chrono::steady_clock::now();
    // auto frameDuration = std::chrono::milliseconds((int)(1000 / frameRate)); // For 25 FPS

    // if (currentTime - lastFrameTime < frameDuration)
    // {
    //     std::this_thread::sleep_for(frameDuration - (currentTime - lastFrameTime));
    // }

    // lastFrameTime = std::chrono::steady_clock::now();

    uint8_t *src_data[4];
    int src_linesize[4];
    src_data[0] = inFrame.data;
    src_linesize[0] = inFrame.step;

    // 将 RGB 数据转换为 YUV420P 并存储在 frame 中
    sws_scale(swsContext, src_data, src_linesize, 0,
              inFrame.rows, frame->data, frame->linesize);
    // sws_freeContext(sws_ctx);

    int retries = 0;
    bool frame_sent = false;
    int ret;

    // 从编码器接收数据包并写入输出流
    do
    {
        frame->pts = frameCount++;

        // 发送帧到编码器
        ret = avcodec_send_frame(codecContext, frame);
        if (ret < 0)
        {
            std::cerr << "发送帧到编码器失败: " << ret << std::endl;
            return false;
        }

        ret = avcodec_receive_packet(codecContext, packet);
        if (ret == AVERROR(EAGAIN))
        {
            if (++retries > MAX_RETRIES)
            {
                std::cerr << "超过接收数据包的最大重试次数. " << std::endl;
                return false;
            }

            return true; // 需要更多帧, 也属于是成功
        }
        else if (ret == AVERROR_EOF)
        {
            // 编码器完成了所有输入数据的处理
            frame_sent = false;
            break;
        }
        else if (ret < 0)
        {
            std::cerr << "从编码器接收数据包失败" << std::endl;
            return false;
        }

        frame_sent = true;
    } while (!frame_sent);

    // 转换时间基
    av_packet_rescale_ts(packet, codecContext->time_base, stream->time_base);
    packet->stream_index = stream->index;
    // 确保 packet->pts 和 packet->dts 已正确设置
    if (packet->pts == AV_NOPTS_VALUE || packet->dts == AV_NOPTS_VALUE)
    {
        std::cerr << "PTS 或 DTS 设置不正确!" << std::endl;
    }

    // 写入数据包
    ret = av_interleaved_write_frame(formatContext, packet);
    if (ret < 0)
    {
        std::cerr << "写入数据包失败" << std::endl;
        return false;
    }

    return true;
}

void FFmpegPusher::close()
{
    if (!initialized)
        return;

    // 写入文件尾
    av_write_trailer(formatContext);

    // 释放资源
    if (packet)
    {
        av_packet_free(&packet);
        packet = nullptr;
    }

    if (frame)
    {
        av_frame_free(&frame);
        frame = nullptr;
    }
    if (swsContext)
    {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }

    if (codecContext)
    {
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }

    if (formatContext)
    {
        if (!(formatContext->oformat->flags & AVFMT_NOFILE))
        {
            avio_closep(&formatContext->pb);
        }
        avformat_free_context(formatContext);
        formatContext = nullptr;
    }

    initialized = false;
}
