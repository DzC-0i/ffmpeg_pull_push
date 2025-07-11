#include <iostream>
#include <csignal>
#include <string>
#include <chrono>
#include <thread>
#include "ffmpeg_capture.hh"
#include "ffmpeg_pusher.hh"

bool running = true;

// Signal handler to gracefully close the connection
void signalHandler(int signum)
{
    std::cerr << "捕获到信号 " << signum << "，正在关闭连接..." << std::endl;
    running = false;

    // std::exit(signum);
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        std::cerr << "用法: " << argv[0] << " <RTSP_URL> <CHOICE: rtsp/rtmp> <RTSP_URL/RTMP_URL>" << std::endl;
        return -1;
    }

    // 注册信号捕获 (如 Ctrl+C)
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string rtspUrl = argv[1];
    std::string streamType = argv[2];
    std::string streamUrl = argv[3];
    int frameRate = 25;

    std::cout << "正在初始化视频流客户端..." << std::endl;

    // 初始化FFmpeg拉流模块
    FFmpegCapture capturer(rtspUrl);
    if (!capturer.open())
    {
        std::cerr << "拉流模块初始化失败" << std::endl;
        return -1;
    }

    int width = capturer.getWidth();
    int height = capturer.getHeight();
    std::cout << "视频尺寸: " << width << "x" << height << ", 帧率: " << frameRate << std::endl;

    // 初始化FFmpeg推流模块（使用RTMP协议）
    FFmpegPusher pusher(streamUrl, width, height, frameRate, streamType);
    if (!pusher.init())
    {
        std::cerr << "推流模块初始化失败" << std::endl;
        capturer.close();
        return -1;
    }

    cv::Mat captureFrame;

    // 主循环
    int64_t frameCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    std::cout << "开始视频流处理..." << std::endl;
    std::cout << "按Ctrl+C退出..." << std::endl;

    try
    {
        while (running)
        {
            // 读取帧
            if (!capturer.readFrame(captureFrame))
            {
                std::cerr << "读取帧失败，尝试重新连接..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(2));

                capturer.close();
                if (!capturer.open())
                {
                    std::cerr << "重新连接失败" << std::endl;
                    running = false;
                }
                continue;
            }

            // 推流
            if (!pusher.pushFrame(captureFrame))
            {
                std::cerr << "推流失败" << std::endl;
                running = false;
                break;
            }

            // 控制帧率
            frameCount++;
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
            int expectedTime = (1000 / frameRate) * frameCount;

            if (elapsed < expectedTime)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(expectedTime - elapsed));
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "发生异常: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "发生未知错误" << std::endl;
    }

    // 清理资源
    std::cout << "正在释放资源..." << std::endl;
    capturer.close();
    pusher.close();

    std::cout << "视频流客户端已退出" << std::endl;
    return 0;
}
