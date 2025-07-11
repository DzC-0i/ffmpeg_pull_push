// ffmpeg_capture.cpp
#include "ffmpeg_capture.hh"

#include "ffmpeg_metwork_init.hh"

FFmpegCapture::FFmpegCapture(const std::string &url) : rtspUrl(url), width(0), height(0)
{
}

FFmpegCapture::~FFmpegCapture()
{
    close();
}

bool FFmpegCapture::open()
{
    FFmpegNetworkInitializer::init();
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
    const AVCodec *codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
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

inline std::string avErrorString(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

bool FFmpegCapture::readFrame(cv::Mat &outFrame)
{
    if (!isOpened)
        return false;

    while (true)
    {
        // 1. 尝试从解码器获取帧
        int ret = avcodec_receive_frame(codecContext, frame);
        if (ret == 0)
        {
            break; // 成功获取帧
        }
        else if (ret != AVERROR(EAGAIN))
        {
            if (ret == AVERROR_EOF)
                std::cerr << "流结束" << std::endl;
            else
                std::cerr << "解码错误: " << avErrorString(ret) << std::endl;
            return false;
        }

        // 2. 需要新数据包
        while (true)
        {
            av_packet_unref(packet); // 清理前一个包

            // 读取网络数据包
            ret = av_read_frame(formatContext, packet);
            if (ret < 0)
            {
                if (ret == AVERROR(EAGAIN))
                    continue;
                std::cerr << "读取失败: " << avErrorString(ret) << std::endl;
                return false;
            }

            // 3. 只处理视频流
            if (packet->stream_index == videoStreamIndex)
            {
                ret = avcodec_send_packet(codecContext, packet);
                av_packet_unref(packet); // 立即释放
                if (ret < 0)
                {
                    std::cerr << "发送包失败: " << avErrorString(ret) << std::endl;
                    return false;
                }
                break; // 退出内层循环
            }
            av_packet_unref(packet); // 释放非视频包
        }
    }

    // 4. 准备输出缓冲区 (优化内存复用)
    if (outFrame.empty() ||
        outFrame.cols != width ||
        outFrame.rows != height ||
        outFrame.type() != CV_8UC3)
    {
        outFrame.create(height, width, CV_8UC3);
    }

    // 5. 转换YUV->RGB
    uint8_t *dstData[1] = {outFrame.data};
    int dstLinesize[1] = {static_cast<int>(outFrame.step)};
    sws_scale(swsContext, frame->data, frame->linesize,
              0, height, dstData, dstLinesize);

    return true;
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

    videoStream = nullptr;
    videoStreamIndex = -1;

    isOpened = false;
}
