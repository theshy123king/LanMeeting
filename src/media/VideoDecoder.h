#pragma once

#ifdef USE_FFMPEG_H264

#include <QByteArray>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

class VideoDecoder
{
public:
    VideoDecoder();
    ~VideoDecoder();

    bool init();
    bool decodePacket(const QByteArray &packet, AVFrame *outFrame);

private:
    AVCodec *codec;
    AVCodecContext *ctx;
    AVPacket *pkt;
};

#endif // USE_FFMPEG_H264

