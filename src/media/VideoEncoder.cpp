#include "VideoEncoder.h"

#ifdef USE_FFMPEG_H264

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>

VideoEncoder::VideoEncoder()
    : codec(nullptr)
    , ctx(nullptr)
    , pkt(nullptr)
    , width(0)
    , height(0)
    , pixFmt(AV_PIX_FMT_YUV420P)
    , ptsCounter(0)
    , preset("medium")
    , crfValue(22)
    , targetFrameIntervalMs(1000.0 / 24.0)
    , maxSamples(30)
    , smoothedEncodeMs(0.0)
    , requestFallback(false)
    , overBudgetStreak(0)
    , fallbackWidth(720)
    , fallbackHeight(404)
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
    width = w;
    height = h;
    pixFmt = fmt;
    ptsCounter = 0;
    encodeDurations.clear();
    smoothedEncodeMs = 0.0;
    requestFallback = false;
    overBudgetStreak = 0;
    preset = "medium";
    crfValue = 22;

    return openContext(preset, crfValue);
}

bool VideoEncoder::encodeFrame(AVFrame *frame, QByteArray &outPacket)
{
    if (!ctx || !pkt) {
        return false;
    }

    if (frame && (frame->width != width || frame->height != height || frame->format != pixFmt)) {
        reinit(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format));
    }

    outPacket.clear();

    if (frame) {
        frame->pts = ptsCounter++;
    }

    QElapsedTimer encodeTimer;
    encodeTimer.start();

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

    const double encodeMs = encodeTimer.nsecsElapsed() / 1000000.0;
    updateQualityController(encodeMs);

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

bool VideoEncoder::reinit(int w, int h, AVPixelFormat fmt)
{
    width = w;
    height = h;
    pixFmt = fmt;
    encodeDurations.clear();
    smoothedEncodeMs = 0.0;
    overBudgetStreak = 0;
    requestFallback = false;
    return openContext(preset, crfValue);
}

QSize VideoEncoder::encodeSize() const
{
    return QSize(width, height);
}

bool VideoEncoder::fallbackRequested() const
{
    return requestFallback;
}

QSize VideoEncoder::fallbackSize() const
{
    return QSize(fallbackWidth, fallbackHeight);
}

double VideoEncoder::averageEncodeTimeMs() const
{
    return smoothedEncodeMs;
}

void VideoEncoder::clearFallbackRequest()
{
    requestFallback = false;
}

bool VideoEncoder::openContext(const std::string &presetOverride, int crfOverride)
{
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            return false;
        }
    }

    AVCodecContext *newCtx = avcodec_alloc_context3(codec);
    if (!newCtx) {
        return false;
    }

    newCtx->width = width;
    newCtx->height = height;
    newCtx->pix_fmt = pixFmt;
    newCtx->time_base = AVRational{1, 24};
    newCtx->framerate = AVRational{24, 1};
    newCtx->bit_rate = 400000;
    newCtx->gop_size = 10;
    newCtx->max_b_frames = 0;
    newCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    const std::string presetToUse = presetOverride.empty() ? preset : presetOverride;
    const int crfToUse = (crfOverride > 0) ? crfOverride : crfValue;

    av_opt_set(newCtx->priv_data, "preset", presetToUse.c_str(), 0);
    av_opt_set(newCtx->priv_data, "tune", "zerolatency", 0);
    av_opt_set_int(newCtx->priv_data, "crf", crfToUse, 0);

    if (avcodec_open2(newCtx, codec, nullptr) < 0) {
        avcodec_free_context(&newCtx);
        return false;
    }

    if (ctx) {
        avcodec_free_context(&ctx);
    }
    ctx = newCtx;
    preset = presetToUse;
    crfValue = crfToUse;

    if (!pkt) {
        pkt = av_packet_alloc();
        if (!pkt) {
            avcodec_free_context(&ctx);
            ctx = nullptr;
            return false;
        }
    }

    return true;
}

void VideoEncoder::updateQualityController(double encodeMs)
{
    encodeDurations.push_back(encodeMs);
    if (encodeDurations.size() > maxSamples) {
        encodeDurations.pop_front();
    }

    double sum = 0.0;
    for (double t : encodeDurations) {
        sum += t;
    }
    smoothedEncodeMs = encodeDurations.empty() ? 0.0 : (sum / static_cast<double>(encodeDurations.size()));

    const double halfBudget = targetFrameIntervalMs * 0.5;
    const double fastThreshold = targetFrameIntervalMs * 0.85;
    const double fallbackThreshold = targetFrameIntervalMs;
    const double recoverThreshold = targetFrameIntervalMs * 0.6;

    int desiredCrf = crfValue;
    if (smoothedEncodeMs <= halfBudget) {
        desiredCrf = 22;
    } else {
        const double span = targetFrameIntervalMs - halfBudget;
        const double offset = std::max(0.0, smoothedEncodeMs - halfBudget);
        const double ratio = std::min(1.0, offset / span);
        desiredCrf = 22 + static_cast<int>(std::round(ratio * 2.0));
        desiredCrf = std::max(22, std::min(24, desiredCrf));
    }

    if (ctx && desiredCrf != crfValue) {
        av_opt_set_int(ctx->priv_data, "crf", desiredCrf, 0);
        crfValue = desiredCrf;
    }

    std::string desiredPreset = preset;
    if (smoothedEncodeMs < halfBudget) {
        desiredPreset = "medium";
    } else if (smoothedEncodeMs >= fastThreshold) {
        desiredPreset = "fast";
    }

    if (desiredPreset != preset) {
        openContext(desiredPreset, crfValue);
    }

    if (smoothedEncodeMs > fallbackThreshold) {
        ++overBudgetStreak;
    } else if (smoothedEncodeMs < recoverThreshold && overBudgetStreak > 0) {
        --overBudgetStreak;
    }

    if (overBudgetStreak >= 5) {
        requestFallback = true;
        overBudgetStreak = 0;
    }
}

#endif // USE_FFMPEG_H264
