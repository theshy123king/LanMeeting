#include "MediaTransport.h"

#include <QBuffer>
#include <QHostAddress>
#include <QImage>
#include <QPixmap>
#include <QVBoxLayout>

#ifdef USE_FFMPEG_H264
extern "C" {
#include <libswscale/swscale.h>
}
#endif

#include "MediaEngine.h"
#include "common/Logger.h"

namespace {
constexpr double kTargetFrameIntervalMs = 1000.0 / 24.0;
constexpr int kFrameIntervalCeilMs = 42; // rounding up 41.67 for QTimer.

#ifdef USE_FFMPEG_H264
QSize ensureEvenSize(const QSize &size)
{
    QSize even = size;
    if (even.width() % 2 != 0) {
        even.rwidth() -= 1;
    }
    if (even.height() % 2 != 0) {
        even.rheight() -= 1;
    }
    if (even.width() <= 0 || even.height() <= 0) {
        return QSize();
    }
    return even;
}

QSize calculateEncodeSize(const QSize &source, const QSize &bound)
{
    if (!source.isValid()) {
        return ensureEvenSize(bound);
    }

    QSize scaled = source;
    if (bound.isValid()) {
        scaled = source.scaled(bound, Qt::KeepAspectRatio);
    }

    scaled.setWidth(qMin(scaled.width(), source.width()));
    scaled.setHeight(qMin(scaled.height(), source.height()));
    return ensureEvenSize(scaled);
}
#endif
} // namespace

MediaTransport::MediaTransport(MediaEngine *engine, QObject *parent)
    : QObject(parent)
    , udpSendSocket(new QUdpSocket(this))
    , udpRecvSocket(new QUdpSocket(this))
    , sendTimer(new QTimer(this))
    , sendClock()
    , lastSendMs(0)
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
    , activeEncodeBound(960, 540)
    , fallbackEncodeBound(720, 404)
    , fallbackActive(false)
#endif
{
    // Enforce ~24 FPS pacing for outgoing video.
    sendTimer->setInterval(kFrameIntervalCeilMs);
    connect(sendTimer, &QTimer::timeout, this, &MediaTransport::onSendTimer);

    remoteVideoLabel->setAlignment(Qt::AlignCenter);
    remoteVideoLabel->setMinimumSize(320, 240);
    remoteVideoLabel->setObjectName(QStringLiteral("remoteVideoLabel"));
    remoteVideoLabel->setText(QStringLiteral("Waiting for remote participant to join or start sending video..."));

    auto *layout = new QVBoxLayout(remoteVideoWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(remoteVideoLabel);
}

MediaTransport::~MediaTransport()
{
    // 确保在销毁时停止定时器和关闭套接字，避免残留网络活动。
    stopTransport();
}

bool MediaTransport::startTransport(quint16 localPortValue, const QString &remoteIpValue, quint16 remotePortValue)
{
    stopTransport();

    localPort = localPortValue;
    remoteIp = remoteIpValue;
    remotePort = remotePortValue;
    lastSendMs = 0;
    sendClock.invalidate();
    sendClock.start();

    if (!udpRecvSocket->bind(QHostAddress::AnyIPv4, localPort)) {
        LOG_WARN(QStringLiteral("MediaTransport: failed to bind UDP port %1: %2")
                     .arg(localPort)
                     .arg(udpRecvSocket->errorString()));
        return false;
    }

    connect(udpRecvSocket, &QUdpSocket::readyRead, this, &MediaTransport::onReadyRead);

#ifdef USE_FFMPEG_H264
    fallbackActive = false;
    if (media) {
        const QImage frame = media->getCurrentFrame();
        const QSize sourceSize = frame.isNull() ? QSize(640, 480) : frame.size();
        const QSize encodeSize = calculateEncodeSize(sourceSize, activeEncodeBound);
        videoWidth = encodeSize.width();
        videoHeight = encodeSize.height();

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

    // 进入“通道已建立，等待对端视频”的占位状态。
    if (remoteVideoLabel) {
        remoteVideoLabel->setPixmap(QPixmap());
        remoteVideoLabel->setText(QStringLiteral("Channel established, waiting for remote video..."));
    }

    sendTimer->start();

    return true;
}

bool MediaTransport::startSendOnly(const QString &remoteIpValue, quint16 remotePortValue)
{
    // Send-only mode: we do not bind the receive socket,
    // so this transport instance will only push local video
    // frames to the given remote endpoint.
    stopTransport();

    localPort = 0;
    remoteIp = remoteIpValue;
    remotePort = remotePortValue;
    lastSendMs = 0;
    sendClock.invalidate();
    sendClock.start();

#ifdef USE_FFMPEG_H264
    fallbackActive = false;
    if (media) {
        const QImage frame = media->getCurrentFrame();
        const QSize sourceSize = frame.isNull() ? QSize(640, 480) : frame.size();
        const QSize encodeSize = calculateEncodeSize(sourceSize, activeEncodeBound);
        videoWidth = encodeSize.width();
        videoHeight = encodeSize.height();

        if (!encoder) {
            encoder = new VideoEncoder();
            if (!encoder->init(videoWidth, videoHeight, AV_PIX_FMT_YUV420P)) {
                LOG_WARN(QStringLiteral("MediaTransport: failed to initialize H.264 encoder for send-only mode, falling back to JPEG transport"));
                delete encoder;
                encoder = nullptr;
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
    lastSendMs = 0;
    sendClock.invalidate();

    udpSendSocket->disconnect(this);
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
    activeEncodeBound = QSize(960, 540);
    fallbackActive = false;
#endif

    // 重置端口与地址，避免下次启动时误用旧状态。
    localPort = 0;
    remotePort = 0;
    remoteIp.clear();

    // 恢复远端视频区域的占位画面，避免停会/断线后停留在最后一帧。
    if (remoteVideoLabel) {
        remoteVideoLabel->setPixmap(QPixmap());
        remoteVideoLabel->setText(QStringLiteral("Waiting for remote participant to join or start sending video..."));
    }
}

QWidget *MediaTransport::getRemoteVideoWidget()
{
    return remoteVideoWidget;
}

void MediaTransport::logDiagnostics() const
{
    LOG_INFO(QStringLiteral("VideoNet diag: localPort=%1 remote=%2:%3 recvOpen=%4 sendOpen=%5")
                 .arg(localPort)
                 .arg(remoteIp)
                 .arg(remotePort)
                 .arg(udpRecvSocket && udpRecvSocket->isOpen())
                 .arg(udpSendSocket && udpSendSocket->isOpen()));
}

void MediaTransport::onSendTimer()
{
    if (!media || remoteIp.isEmpty() || remotePort == 0) {
        return;
    }

    if (!sendClock.isValid()) {
        sendClock.start();
    }

    const qint64 nowMs = sendClock.elapsed();
    const qint64 minIntervalMs = static_cast<qint64>(kTargetFrameIntervalMs);
    const qint64 lateIntervalMs = static_cast<qint64>(kTargetFrameIntervalMs * 1.5);
    if (lastSendMs > 0) {
        const qint64 delta = nowMs - lastSendMs;
        if (delta < minIntervalMs) {
            return;
        }
        if (delta > lateIntervalMs) {
            lastSendMs = nowMs;
            return;
        }
    }
    lastSendMs = nowMs;

    const QImage frame = media->getCurrentFrame();
    if (frame.isNull()) {
        return;
    }

#ifdef USE_FFMPEG_H264
    if (encoder && videoWidth > 0 && videoHeight > 0) {
        const QSize sourceSize = frame.size().isValid() ? frame.size() : QSize(videoWidth, videoHeight);
        const QSize bound = fallbackActive ? fallbackEncodeBound : activeEncodeBound;
        const QSize encodeSize = calculateEncodeSize(sourceSize, bound);
        AVFrame *yuvFrame = nullptr;
        if (!media->prepareFrameForEncode(encodeSize.width(), encodeSize.height(), AV_PIX_FMT_YUV420P, yuvFrame) || !yuvFrame) {
            return;
        }

        if (yuvFrame->width != videoWidth || yuvFrame->height != videoHeight) {
            videoWidth = yuvFrame->width;
            videoHeight = yuvFrame->height;
            encoder->reinit(videoWidth, videoHeight, AV_PIX_FMT_YUV420P);
        }

        QByteArray packet;
        const bool encoded = encoder->encodeFrame(yuvFrame, packet);
        av_frame_free(&yuvFrame);

        if (encoded && !packet.isEmpty()) {
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

        if (encoder->fallbackRequested()) {
            fallbackActive = true;
            encoder->clearFallbackRequest();
            LOG_WARN(QStringLiteral("MediaTransport: switching to 720p fallback encode bound (%1x%2) after sustained load")
                         .arg(fallbackEncodeBound.width())
                         .arg(fallbackEncodeBound.height()));
        }

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
        const qint64 pendingSize = udpRecvSocket->pendingDatagramSize();
        if (pendingSize <= 0) {
            LOG_WARN(QStringLiteral("MediaTransport: pendingDatagramSize returned %1").arg(pendingSize));
            // 出现此情况说明底层缓冲区状态异常，跳出读取循环避免死循环。
            break;
        }

        datagram.resize(int(pendingSize));
        const qint64 read = udpRecvSocket->readDatagram(datagram.data(), datagram.size());
        if (read <= 0) {
            LOG_WARN(QStringLiteral("MediaTransport: failed to read UDP datagram - %1")
                         .arg(udpRecvSocket->errorString()));
            continue;
        }

        if (read < datagram.size()) {
            datagram.resize(int(read));
        }

        if (datagram.isEmpty()) {
            LOG_WARN(QStringLiteral("MediaTransport: received empty UDP datagram"));
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
                        const QSize target = remoteVideoLabel->size();
                        const QSize native = image.size();
                        QSize scaledSize = native;
                        if (!target.isEmpty() && native.isValid()) {
                            QSize fit = native.scaled(target, Qt::KeepAspectRatio);
                            const double scaleFactor = qMin(1.0,
                                                            qMin(double(fit.width()) / double(native.width()),
                                                                 double(fit.height()) / double(native.height())));
                            scaledSize = QSize(int(native.width() * scaleFactor),
                                               int(native.height() * scaleFactor));
                        }
                        QPixmap pix = QPixmap::fromImage(image).scaled(scaledSize,
                                                                       Qt::KeepAspectRatio,
                                                                       Qt::SmoothTransformation);
                        remoteVideoLabel->setPixmap(pix);
                        emit remoteFrameReceived();
                    } else {
                        LOG_WARN(QStringLiteral("MediaTransport: decoded frame produced null image"));
                    }
                } else {
                    LOG_WARN(QStringLiteral("MediaTransport: failed to create sws context for decoded frame"));
                }
            } else {
                LOG_WARN(QStringLiteral("MediaTransport: failed to decode H.264 packet (size=%1)").arg(datagram.size()));
            }

            av_frame_free(&frame);
            continue;
        }
#endif

        QImage image;
        if (!image.loadFromData(datagram, "JPG")) {
            LOG_WARN(QStringLiteral("MediaTransport: failed to decode JPEG frame (size=%1)").arg(datagram.size()));
            continue;
        }

        if (!image.isNull()) {
            const QSize target = remoteVideoLabel->size();
            const QSize native = image.size();
            QSize scaledSize = native;
            if (!target.isEmpty() && native.isValid()) {
                QSize fit = native.scaled(target, Qt::KeepAspectRatio);
                const double scaleFactor = qMin(1.0,
                                                qMin(double(fit.width()) / double(native.width()),
                                                     double(fit.height()) / double(native.height())));
                scaledSize = QSize(int(native.width() * scaleFactor),
                                   int(native.height() * scaleFactor));
            }
            QPixmap pix = QPixmap::fromImage(image).scaled(scaledSize,
                                                           Qt::KeepAspectRatio,
                                                           Qt::SmoothTransformation);
            remoteVideoLabel->setPixmap(pix);
            emit remoteFrameReceived();
        }
    }
}
