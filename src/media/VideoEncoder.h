#pragma once

#ifdef USE_FFMPEG_H264

#include <QByteArray>
#include <QList>
#include <QSize>
#include <deque>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

class VideoEncoder
{
public:
    VideoEncoder();
    ~VideoEncoder();

    bool init(int width, int height, AVPixelFormat pixFmt = AV_PIX_FMT_YUV420P);
    bool reinit(int width, int height, AVPixelFormat pixFmt = AV_PIX_FMT_YUV420P);
    bool encodeFrame(AVFrame *frame, QByteArray &outPacket);
    void flush(QList<QByteArray> &outPackets);

    QSize encodeSize() const;
    bool fallbackRequested() const;
    QSize fallbackSize() const;
    double averageEncodeTimeMs() const;
    void clearFallbackRequest();

private:
    bool openContext(const std::string &presetOverride, int crfOverride);
    void updateQualityController(double encodeMs);

    const AVCodec *codec;
    AVCodecContext *ctx;
    AVPacket *pkt;
    int width;
    int height;
    AVPixelFormat pixFmt;
    int64_t ptsCounter;

    std::string preset;
    int crfValue;
    double targetFrameIntervalMs;
    std::deque<double> encodeDurations;
    size_t maxSamples;
    double smoothedEncodeMs;
    bool requestFallback;
    int overBudgetStreak;
    int fallbackWidth;
    int fallbackHeight;
};

#endif // USE_FFMPEG_H264
