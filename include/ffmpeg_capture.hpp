// ffmpeg_capture.hpp
#ifndef FFMPEG_CAPTURE_H
#define FFMPEG_CAPTURE_H

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <string>
#include <iostream>

#include <opencv2/opencv.hpp>

class FFmpegCapture
{
private:
    AVFormatContext *formatContext = nullptr;
    AVCodecContext *codecContext = nullptr;
    AVStream *videoStream = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *swsContext = nullptr;
    int videoStreamIndex = -1;
    std::string rtspUrl;
    bool isOpened = false;
    int width, height;

public:
    FFmpegCapture(const std::string &url);
    ~FFmpegCapture();

    bool open();
    bool readFrame(cv::Mat &outFrame);
    void close();
    int getWidth() const { return width; }
    int getHeight() const { return height; }
};

#endif // FFMPEG_CAPTURE_H
