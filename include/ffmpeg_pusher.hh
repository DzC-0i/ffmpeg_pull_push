// ffmpeg_pusher.hh
#ifndef FFMPEG_PUSHER_H
#define FFMPEG_PUSHER_H

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <string>
#include <iostream>

#include <opencv2/opencv.hpp>

class FFmpegPusher
{
private:
    AVFormatContext *formatContext = nullptr;
    AVCodecContext *codecContext = nullptr;
    AVStream *stream = nullptr;
    const AVCodec *codec;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *swsContext = nullptr;
    std::string rtmpUrl;
    bool initialized = false;
    int64_t frameCount = 0;
    int width, height, frameRate;
    std::string protocol;

public:
    FFmpegPusher(const std::string &url, int w, int h, int fr, const std::string &prot = "rtmp");
    ~FFmpegPusher();

    bool init();
    bool pushFrame(cv::Mat &inFrame);
    void close();
};

#endif // FFMPEG_PUSHER_H
