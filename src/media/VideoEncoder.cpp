#include "VideoEncoder.h"

#ifdef USE_FFMPEG_H264

VideoEncoder::VideoEncoder()
    : codec(nullptr)
    , ctx(nullptr)
    , pkt(nullptr)
    , width(0)
    , height(0)
    , pixFmt(AV_PIX_FMT_YUV420P)
    , ptsCounter(0)
{
#if LIBAVCODEC_VERSION_MAJOR < 58
    avcodec_register_all();
#endif
}

VideoEncoder::~VideoEncoder()
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

bool VideoEncoder::init(int w, int h, AVPixelFormat fmt)
{
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        return false;
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        return false;
    }

    width = w;
    height = h;
    pixFmt = fmt;
    ptsCounter = 0;

    ctx->width = width;
    ctx->height = height;
    ctx->pix_fmt = pixFmt;
    ctx->time_base = AVRational{1, 25};
    ctx->framerate = AVRational{25, 1};
    ctx->bit_rate = 400000;
    ctx->gop_size = 10;
    ctx->max_b_frames = 0;
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    av_opt_set(ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);

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

bool VideoEncoder::encodeFrame(AVFrame *frame, QByteArray &outPacket)
{
    if (!ctx || !pkt) {
        return false;
    }

    outPacket.clear();

    if (frame) {
        frame->pts = ptsCounter++;
    }

    if (avcodec_send_frame(ctx, frame) < 0) {
        return false;
    }

    while (true) {
        int ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            return false;
        }

        outPacket.append(reinterpret_cast<const char *>(pkt->data),
                         static_cast<int>(pkt->size));
        av_packet_unref(pkt);
    }

    return !outPacket.isEmpty();
}

void VideoEncoder::flush(QList<QByteArray> &outPackets)
{
    if (!ctx || !pkt) {
        return;
    }

    avcodec_send_frame(ctx, nullptr);

    while (true) {
        int ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            break;
        }

        QByteArray packet;
        packet.append(reinterpret_cast<const char *>(pkt->data),
                      static_cast<int>(pkt->size));
        outPackets.append(packet);

        av_packet_unref(pkt);
    }
}

#endif // USE_FFMPEG_H264
