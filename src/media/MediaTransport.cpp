#include "MediaTransport.h"

#include <QBuffer>
#include <QHostAddress>
#include <QImage>
#include <QPixmap>
#include <QVBoxLayout>
#include <cstring>

#ifdef USE_FFMPEG_H264
extern "C" {
#include <libswscale/swscale.h>
}
#endif

#include "MediaEngine.h"
#include "common/Logger.h"

MediaTransport::MediaTransport(MediaEngine *engine, QObject *parent)
    : QObject(parent)
    , udpSendSocket(new QUdpSocket(this))
    , udpRecvSocket(new QUdpSocket(this))
    , sendTimer(new QTimer(this))
    , remoteIp()
    , localPort(0)
    , remotePort(0)
    , remoteVideoLabel(new QLabel)
    , remoteVideoWidget(new QWidget)
    , media(engine)
#ifdef USE_FFMPEG_H264
    , encoder(nullptr)
    , decoder(nullptr)
    , videoWidth(0)
    , videoHeight(0)
#endif
{
    sendTimer->setInterval(40);
    connect(sendTimer, &QTimer::timeout, this, &MediaTransport::onSendTimer);

    remoteVideoLabel->setAlignment(Qt::AlignCenter);
    remoteVideoLabel->setMinimumSize(320, 240);

    auto *layout = new QVBoxLayout(remoteVideoWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(remoteVideoLabel);
}

bool MediaTransport::startTransport(quint16 localPortValue, const QString &remoteIpValue, quint16 remotePortValue)
{
    stopTransport();

    localPort = localPortValue;
    remoteIp = remoteIpValue;
    remotePort = remotePortValue;

    if (!udpRecvSocket->bind(QHostAddress::AnyIPv4, localPort)) {
        LOG_WARN(QStringLiteral("MediaTransport: failed to bind UDP port %1: %2")
                     .arg(localPort)
                     .arg(udpRecvSocket->errorString()));
        return false;
    }

    connect(udpRecvSocket, &QUdpSocket::readyRead, this, &MediaTransport::onReadyRead);

#ifdef USE_FFMPEG_H264
    if (media) {
        const QImage frame = media->getCurrentFrame();
        if (!frame.isNull()) {
            videoWidth = frame.width();
            videoHeight = frame.height();
        } else {
            videoWidth = 640;
            videoHeight = 480;
        }

        if (!encoder) {
            encoder = new VideoEncoder();
            if (!encoder->init(videoWidth, videoHeight, AV_PIX_FMT_YUV420P)) {
                LOG_WARN(QStringLiteral("MediaTransport: failed to initialize H.264 encoder, falling back to JPEG transport"));
                delete encoder;
                encoder = nullptr;
            }
        }

        if (!decoder) {
            decoder = new VideoDecoder();
            if (!decoder->init()) {
                LOG_WARN(QStringLiteral("MediaTransport: failed to initialize H.264 decoder, falling back to JPEG transport"));
                delete decoder;
                decoder = nullptr;
            }
        }
    }
#endif

    sendTimer->start();

    return true;
}

void MediaTransport::stopTransport()
{
    if (sendTimer->isActive()) {
        sendTimer->stop();
    }

    if (udpRecvSocket->isOpen()) {
        udpRecvSocket->close();
    }

    udpRecvSocket->disconnect(this);

#ifdef USE_FFMPEG_H264
    delete encoder;
    encoder = nullptr;
    delete decoder;
    decoder = nullptr;
    videoWidth = 0;
    videoHeight = 0;
#endif
}

QWidget *MediaTransport::getRemoteVideoWidget()
{
    return remoteVideoWidget;
}

void MediaTransport::onSendTimer()
{
    if (!media || remoteIp.isEmpty() || remotePort == 0) {
        return;
    }

    const QImage frame = media->getCurrentFrame();
    if (frame.isNull()) {
        return;
    }

#ifdef USE_FFMPEG_H264
    if (encoder && videoWidth > 0 && videoHeight > 0) {
        QImage rgbFrame = frame.convertToFormat(QImage::Format_RGBA8888);
        if (rgbFrame.isNull()) {
            return;
        }

        AVFrame *srcFrame = av_frame_alloc();
        if (!srcFrame) {
            return;
        }

        srcFrame->format = AV_PIX_FMT_RGBA;
        srcFrame->width = videoWidth;
        srcFrame->height = videoHeight;

        if (av_frame_get_buffer(srcFrame, 32) < 0) {
            av_frame_free(&srcFrame);
            return;
        }

        for (int y = 0; y < videoHeight; ++y) {
            uint8_t *dstLine = srcFrame->data[0] + y * srcFrame->linesize[0];
            const uint8_t *srcLine = rgbFrame.constScanLine(y);
            std::memcpy(dstLine, srcLine, size_t(videoWidth) * 4);
        }

        AVFrame *yuvFrame = av_frame_alloc();
        if (!yuvFrame) {
            av_frame_free(&srcFrame);
            return;
        }

        yuvFrame->format = AV_PIX_FMT_YUV420P;
        yuvFrame->width = videoWidth;
        yuvFrame->height = videoHeight;

        if (av_frame_get_buffer(yuvFrame, 32) < 0) {
            av_frame_free(&srcFrame);
            av_frame_free(&yuvFrame);
            return;
        }

        SwsContext *swsCtx = sws_getContext(videoWidth,
                                            videoHeight,
                                            AV_PIX_FMT_RGBA,
                                            videoWidth,
                                            videoHeight,
                                            AV_PIX_FMT_YUV420P,
                                            SWS_BILINEAR,
                                            nullptr,
                                            nullptr,
                                            nullptr);
        if (!swsCtx) {
            av_frame_free(&srcFrame);
            av_frame_free(&yuvFrame);
            return;
        }

        const uint8_t *srcSlice[4] = { srcFrame->data[0], nullptr, nullptr, nullptr };
        int srcStride[4] = { srcFrame->linesize[0], 0, 0, 0 };

        sws_scale(swsCtx,
                  srcSlice,
                  srcStride,
                  0,
                  videoHeight,
                  yuvFrame->data,
                  yuvFrame->linesize);

        sws_freeContext(swsCtx);
        av_frame_free(&srcFrame);

        QByteArray packet;
        if (encoder->encodeFrame(yuvFrame, packet) && !packet.isEmpty()) {
            const qint64 written = udpSendSocket->writeDatagram(packet, QHostAddress(remoteIp), remotePort);
            if (written < 0) {
                LOG_WARN(QStringLiteral("MediaTransport: failed to send H.264 packet to %1:%2 - %3")
                             .arg(remoteIp)
                             .arg(remotePort)
                             .arg(udpSendSocket->errorString()));
            }
        } else {
            LOG_WARN(QStringLiteral("MediaTransport: encoder produced empty packet"));
        }

        av_frame_free(&yuvFrame);

        return;
    }
#endif

    QByteArray buffer;
    QBuffer qBuffer(&buffer);
    qBuffer.open(QIODevice::WriteOnly);
    if (!frame.save(&qBuffer, "JPG")) {
        return;
    }

    if (!buffer.isEmpty()) {
        const qint64 written = udpSendSocket->writeDatagram(buffer, QHostAddress(remoteIp), remotePort);
        if (written < 0) {
            LOG_WARN(QStringLiteral("MediaTransport: failed to send JPEG frame to %1:%2 - %3")
                         .arg(remoteIp)
                         .arg(remotePort)
                         .arg(udpSendSocket->errorString()));
        }
    }
}

void MediaTransport::onReadyRead()
{
    while (udpRecvSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(udpRecvSocket->pendingDatagramSize()));
        const qint64 read = udpRecvSocket->readDatagram(datagram.data(), datagram.size());
        if (read <= 0) {
            LOG_WARN(QStringLiteral("MediaTransport: failed to read UDP datagram - %1")
                         .arg(udpRecvSocket->errorString()));
            continue;
        }

#ifdef USE_FFMPEG_H264
        if (decoder && !datagram.isEmpty()) {
            AVFrame *frame = av_frame_alloc();
            if (!frame) {
                continue;
            }

            if (decoder->decodePacket(datagram, frame)) {
                SwsContext *swsCtx = sws_getContext(frame->width,
                                                    frame->height,
                                                    static_cast<AVPixelFormat>(frame->format),
                                                    frame->width,
                                                    frame->height,
                                                    AV_PIX_FMT_RGBA,
                                                    SWS_BILINEAR,
                                                    nullptr,
                                                    nullptr,
                                                    nullptr);
                if (swsCtx) {
                    QImage image(frame->width, frame->height, QImage::Format_RGBA8888);

                    uint8_t *dstData[4] = { image.bits(), nullptr, nullptr, nullptr };
                    int dstLinesize[4] = { frame->width * 4, 0, 0, 0 };

                    sws_scale(swsCtx,
                              frame->data,
                              frame->linesize,
                              0,
                              frame->height,
                              dstData,
                              dstLinesize);

                    sws_freeContext(swsCtx);

                    if (!image.isNull()) {
                        remoteVideoLabel->setPixmap(QPixmap::fromImage(image).scaled(remoteVideoLabel->size(),
                                                                                     Qt::KeepAspectRatio,
                                                                                     Qt::SmoothTransformation));
                    }
                }
            }

            av_frame_free(&frame);
            continue;
        }
#endif

        QImage image;
        image.loadFromData(datagram, "JPG");
        if (!image.isNull()) {
            remoteVideoLabel->setPixmap(QPixmap::fromImage(image).scaled(remoteVideoLabel->size(),
                                                                         Qt::KeepAspectRatio,
                                                                         Qt::SmoothTransformation));
        }
    }
}
