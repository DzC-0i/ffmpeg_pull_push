#pragma once

#include <mutex>
extern "C"
{
#include <libavformat/avformat.h>
}

class FFmpegNetworkInitializer
{
private:
    static std::once_flag initFlag; // 仅声明，不定义

public:
    static void init();
};
