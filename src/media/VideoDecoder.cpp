#include "VideoDecoder.h"

#ifdef USE_FFMPEG_H264

VideoDecoder::VideoDecoder()
    : codec(nullptr)
    , ctx(nullptr)
    , pkt(nullptr)
{
#if LIBAVCODEC_VERSION_MAJOR < 58
    avcodec_register_all();
#endif
}

VideoDecoder::~VideoDecoder()
{
    if (ctx) {
        avcodec_free_context(&ctx);
        ctx = nullptr;
    }
    if (pkt) {
        av_packet_free(&pkt);
        pkt = nullptr;
    }
}

bool VideoDecoder::init()
{
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        return false;
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        return false;
    }

    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        avcodec_free_context(&ctx);
        return false;
    }

    return true;
}

bool VideoDecoder::decodePacket(const QByteArray &packet, AVFrame *outFrame)
{
    if (!ctx || !pkt || !outFrame) {
        return false;
    }

    av_packet_unref(pkt);
    pkt->data = reinterpret_cast<uint8_t *>(const_cast<char *>(packet.constData()));
    pkt->size = packet.size();

    int ret = avcodec_send_packet(ctx, pkt);
    if (ret < 0) {
        return false;
    }

    ret = avcodec_receive_frame(ctx, outFrame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return false;
    }

    return ret >= 0;
}

#endif // USE_FFMPEG_H264

