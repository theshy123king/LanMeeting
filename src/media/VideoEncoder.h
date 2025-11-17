#pragma once

#ifdef USE_FFMPEG_H264

#include <QByteArray>
#include <QList>

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
    bool encodeFrame(const AVFrame *frame, QByteArray &outPacket);
    void flush(QList<QByteArray> &outPackets);

private:
    const AVCodec *codec;
    AVCodecContext *ctx;
    AVPacket *pkt;
    int width;
    int height;
    AVPixelFormat pixFmt;
};

#endif // USE_FFMPEG_H264
