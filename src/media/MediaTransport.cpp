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
            encoder->init(videoWidth, videoHeight, AV_PIX_FMT_YUV420P);
        }

        if (!decoder) {
            decoder = new VideoDecoder();
            decoder->init();
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
        QImage rgbImage = frame.convertToFormat(QImage::Format_RGBA8888);

        AVFrame *srcFrame = av_frame_alloc();
        AVFrame *yuvFrame = av_frame_alloc();
        if (!srcFrame || !yuvFrame) {
            if (srcFrame) {
                av_frame_free(&srcFrame);
            }
            if (yuvFrame) {
                av_frame_free(&yuvFrame);
            }
            return;
        }

        srcFrame->format = AV_PIX_FMT_RGBA;
        srcFrame->width = videoWidth;
        srcFrame->height = videoHeight;

        if (av_frame_get_buffer(srcFrame, 0) < 0) {
            av_frame_free(&srcFrame);
            av_frame_free(&yuvFrame);
            return;
        }

        for (int y = 0; y < videoHeight; ++y) {
            std::memcpy(srcFrame->data[0] + y * srcFrame->linesize[0],
                        rgbImage.constScanLine(y),
                        static_cast<size_t>(videoWidth * 4));
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
            udpSendSocket->writeDatagram(packet, QHostAddress(remoteIp), remotePort);
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
        udpSendSocket->writeDatagram(buffer, QHostAddress(remoteIp), remotePort);
    }
}

void MediaTransport::onReadyRead()
{
    while (udpRecvSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(udpRecvSocket->pendingDatagramSize()));
        udpRecvSocket->readDatagram(datagram.data(), datagram.size());

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

