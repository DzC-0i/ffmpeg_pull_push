// ffmpeg_capture.cpp
#include "ffmpeg_capture.hpp"

#include "ffmpeg_metwork_init.hpp"

FFmpegCapture::FFmpegCapture(const std::string &url) : rtspUrl(url), width(0), height(0)
{
    FFmpegNetworkInitializer::init();
}

FFmpegCapture::~FFmpegCapture()
{
    close();
}

bool FFmpegCapture::open()
{
    AVDictionary *options = NULL;

    av_dict_set(&options, "buffer_size", "4096000", 0); // 设置缓存大小,1080p可将值跳到最大
    av_dict_set(&options, "rtsp_transport", "tcp", 0);  // 以tcp的方式打开
    av_dict_set(&options, "stimeout", "10000000", 0);   // 设置超时断开链接时间，单位us
    av_dict_set(&options, "max_delay", "500000", 0);    // 设置最大时延

    // 打开RTSP流
    if (avformat_open_input(&formatContext, rtspUrl.c_str(), nullptr, &options) < 0)
    {
        std::cerr << "无法打开RTSP流" << std::endl;
        return false;
    }

    // 读取流信息
    if (avformat_find_stream_info(formatContext, nullptr) < 0)
    {
        std::cerr << "无法读取流信息" << std::endl;
        return false;
    }

    // 查找视频流
    videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0)
    {
        std::cerr << "未找到视频流" << std::endl;
        return false;
    }
    videoStream = formatContext->streams[videoStreamIndex];

    // 查找解码器
    AVCodec *codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec)
    {
        std::cerr << "未找到合适的解码器" << std::endl;
        return false;
    }

    // 创建解码器上下文
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext)
    {
        std::cerr << "无法分配解码器上下文" << std::endl;
        return false;
    }

    // 复制流参数到解码器上下文
    if (avcodec_parameters_to_context(codecContext, videoStream->codecpar) < 0)
    {
        std::cerr << "无法复制流参数到解码器上下文" << std::endl;
        return false;
    }

    // 打开解码器
    if (avcodec_open2(codecContext, codec, nullptr) < 0)
    {
        std::cerr << "无法打开解码器" << std::endl;
        return false;
    }

    // 分配帧和包
    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !packet)
    {
        std::cerr << "无法分配帧或包" << std::endl;
        return false;
    }

    // 初始化SWS上下文（如果需要格式转换）
    width = codecContext->width;
    height = codecContext->height;
    swsContext = sws_getContext(
        width, height, AV_PIX_FMT_YUV420P,
        width, height, AV_PIX_FMT_RGB24, // 转换为RGB888
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsContext)
    {
        std::cerr << "无法初始化SWS上下文" << std::endl;
        return false;
    }

    std::cout << "SWS上下文创建成功: "
              << "输入=" << av_get_pix_fmt_name(codecContext->pix_fmt)
              << ", 输出=" << av_get_pix_fmt_name(AV_PIX_FMT_RGB24)
              << ", 尺寸=" << width << "x" << height << std::endl;

    std::cout << "拉流初始化成功: " << width << "x" << height << std::endl;
    isOpened = true;
    return true;
}

bool FFmpegCapture::readFrame(cv::Mat &outFrame)
{
    if (!isOpened)
    {
        std::cerr << "视频流没有打开" << std::endl;
        return false;
    }

    // 读取数据包
    int ret = av_read_frame(formatContext, packet);
    if (ret < 0)
    {
        std::cerr << "读取数据包失败" << std::endl;
        return false;
    }

    // 只处理视频流数据包
    if (packet->stream_index == videoStreamIndex)
    {
        // 发送数据包到解码器
        ret = avcodec_send_packet(codecContext, packet);
        if (ret < 0)
        {
            std::cerr << "发送数据包到解码器失败" << std::endl;
            av_packet_unref(packet);
            return false;
        }

        // 接收解码后的帧
        ret = avcodec_receive_frame(codecContext, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            std::cerr << "接收到结束帧" << std::endl;
            av_packet_unref(packet);
            return false;
        }
        else if (ret < 0)
        {
            std::cerr << "接收解码帧失败" << std::endl;
            av_packet_unref(packet);
            return false;
        }

        // 创建OpenCV Mat并分配内存
        if (outFrame.empty())
        {
            outFrame.create(height, width, CV_8UC3); // BGR格式
        }

        // 准备RGB数据缓冲区
        uint8_t *rgbData[4] = {outFrame.data, nullptr, nullptr, nullptr};
        int rgbLinesize[4] = {outFrame.step, 0, 0, 0};

        // 格式转换
        sws_scale(swsContext, frame->data, frame->linesize, 0, height,
                  rgbData, rgbLinesize);

        // delete[] rgbData;
        av_packet_unref(packet);
        return true;
    }
    else
    {
        std::cerr << "未接收到视频流数据包" << std::endl;
        av_packet_unref(packet);
        return false;
    }
}

void FFmpegCapture::close()
{
    if (!isOpened)
        return;

    // 释放资源
    if (swsContext)
    {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }

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

    if (codecContext)
    {
        avcodec_close(codecContext);
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }

    if (formatContext)
    {
        avformat_close_input(&formatContext);
        formatContext = nullptr;
    }

    isOpened = false;
}
