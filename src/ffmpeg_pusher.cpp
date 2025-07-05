// ffmpeg_pusher.cpp
#include "ffmpeg_pusher.hpp"

#include "ffmpeg_metwork_init.hpp"

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

    // 查找编码器
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
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

    // 设置编码器参数
    codecContext->codec_id = AV_CODEC_ID_H264;
    codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    codecContext->width = width;
    codecContext->height = height;
    codecContext->time_base = {1, frameRate};
    codecContext->framerate = {frameRate, 1};
    codecContext->bit_rate = 4000000; // 4 Mbps
    codecContext->gop_size = 25;
    codecContext->max_b_frames = 0;
    // 强制第一帧为关键帧
    codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 设置H.264编码器选项
    AVDictionary *options = nullptr;
    av_dict_set(&options, "preset", "ultrafast", 0);
    av_dict_set(&options, "tune", "zerolatency", 0);
    // 显式设置profile和level
    av_dict_set(&options, "profile", "main", 0);
    av_dict_set(&options, "level", "3.1", 0);

    // 禁用B帧
    // av_dict_set(&options, "bframes", "0", 0);

    // RTSP特殊设置
    if (protocol == "rtsp")
    {
        av_dict_set(&options, "rtsp_transport", "tcp", 0); // 使用TCP传输
    }
    else if (protocol == "rtmp")
    {
        // 对于RTMP，不关心文件大小和时长
        formatContext->oformat->flags |= AVFMT_NOTIMESTAMPS;
        // 设置flvflags
        av_dict_set(&options, "flvflags", "no_duration_filesize", 0);
    }

    // 打开编码器
    if (avcodec_open2(codecContext, codec, &options) < 0)
    {
        std::cerr << "无法打开编码器" << std::endl;
        av_dict_free(&options);
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

    // 打开输出URL
    if (!(formatContext->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&formatContext->pb, rtmpUrl.c_str(), AVIO_FLAG_WRITE) < 0)
        {
            std::cerr << "无法打开输出URL (协议: " << protocol << ")" << std::endl;
            return false;
        }
    }

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

    // // 关键修复：正确的时间戳计算
    // static int64_t start_time = av_gettime_relative(); // 静态变量保持时间基准
    // int64_t now = av_gettime_relative();
    // int64_t elapsed = now - start_time;
    // frame->pts = av_rescale_q(elapsed, {1, 1000000}, codecContext->time_base);

    // // 设置帧时间戳
    frame->pts = frameCount++;
    frame->pkt_dts = frame->pts;
    frame->pkt_duration = 1;

    // 关键帧检测（根据实际情况设置）
    if (frameCount % codecContext->gop_size == 0)
    { // 每25帧设为关键帧
        // 设置关键帧标记（让编码器决定具体类型）
        frame->key_frame = 1;
        // frame->pict_type = AV_PICTURE_TYPE_I;
    }
    else
    {
        frame->key_frame = 0;
        // frame->pict_type = AV_PICTURE_TYPE_P;
    }

    // 转换 OpenCV 的 BGR 图像到 YUV420P 格式
    const int stride[] = {static_cast<int>(inFrame.step)};
    sws_scale(swsContext, &inFrame.data, stride, 0, inFrame.rows,
              frame->data, frame->linesize);

    // 发送帧到编码器
    int ret = avcodec_send_frame(codecContext, frame);
    if (ret < 0)
    {
        std::cerr << "发送帧到编码器失败: " << ret << std::endl;
        return false;
    }

    // 从编码器接收数据包并写入输出流
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(codecContext, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }
        else if (ret < 0)
        {
            std::cerr << "从编码器接收数据包失败" << std::endl;
            return false;
        }

        // 转换时间基
        av_packet_rescale_ts(packet, codecContext->time_base, stream->time_base);
        packet->stream_index = stream->index;

        // 写入数据包
        ret = av_interleaved_write_frame(formatContext, packet);
        av_packet_unref(packet);
        if (ret < 0)
        {
            std::cerr << "写入数据包失败" << std::endl;
            return false;
        }
    }

    // frameCount++;
    return true;
}

void FFmpegPusher::close()
{
    if (!initialized)
        return;

    // 刷新编码器
    avcodec_send_frame(codecContext, nullptr);
    while (true)
    {
        int ret = avcodec_receive_packet(codecContext, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }
        if (ret < 0)
        {
            break;
        }

        av_packet_rescale_ts(packet, codecContext->time_base, stream->time_base);
        packet->stream_index = stream->index;

        av_interleaved_write_frame(formatContext, packet);
        av_packet_unref(packet);
    }

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
