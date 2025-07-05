#include "ffmpeg_metwork_init.hpp"

std::once_flag FFmpegNetworkInitializer::initFlag; // 唯一的定义

void FFmpegNetworkInitializer::init()
{
    std::call_once(initFlag, []()
                   {
                       avformat_network_init();
                       // std::cout << "FFmpeg网络组件已初始化（call_once方式）" << std::endl;
                   });
}
