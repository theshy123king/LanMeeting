#include "ScreenShareTransport.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QHostAddress>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QScreen>
#include <QDataStream>
#include <QDateTime>

#include "common/Logger.h"
#include "common/Config.h"

namespace {
// Magic value to identify LanMeeting screen-share packets.
constexpr quint32 kScreenShareMagic = 0x53534852u; // 'S','S','H','R'
// Header layout: magic (4) + frameId (4) + packetIndex (2) + totalPackets (2) + payloadSize (2)
constexpr int kScreenShareHeaderSize = 4 + 4 + 2 + 2 + 2;
// Max payload per UDP packet to stay well below typical MTU.
constexpr int kScreenShareMaxPayloadSize = 1200;
// Max time to wait for all fragments of a frame before dropping it (ms).
constexpr int kFrameAssemblyTimeoutMs = 500;
} // namespace

// Background worker that owns the UDP socket used for sending
// screen-sharing packets. It runs in its own thread so that the
// GUI/audio thread is not blocked by heavy send loops or by the
// kernel's UDP buffer back-pressure. It also enforces a simple
// bandwidth cap using a sliding time window.
class ScreenShareSenderWorker : public QObject
{
    Q_OBJECT

public:
    explicit ScreenShareSenderWorker(QObject *parent = nullptr)
        : QObject(parent)
        , m_socket(nullptr)
        , m_bytesSentInWindow(0)
        , m_windowStartMs(0)
        , m_maxBytesPerSecond(Config::SCREEN_SHARE_MAX_BYTES_PER_SEC)
    {
    }

public slots:
    void sendFrame(const QByteArray &encodedFrame,
                   const QSet<QString> &destIps,
                   quint16 remotePort,
                   quint32 frameId)
    {
        if (encodedFrame.isEmpty() || destIps.isEmpty() || remotePort == 0) {
            return;
        }

        if (!m_socket) {
            m_socket = new QUdpSocket(this);
        }

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 windowMs = Config::SCREEN_SHARE_BW_WINDOW_MS;

        if (m_windowStartMs == 0 || nowMs - m_windowStartMs >= windowMs) {
            m_windowStartMs = nowMs;
            m_bytesSentInWindow = 0;
        }

        const int maxPayload = kScreenShareMaxPayloadSize;
        const quint16 totalPackets =
            static_cast<quint16>((encodedFrame.size() + maxPayload - 1) / maxPayload);
        if (totalPackets == 0) {
            return;
        }

        // Estimate how many bytes this frame would consume on the wire,
        // including UDP/IP headers margin, to decide whether to drop it
        // when the bandwidth budget is exhausted.
        const qint64 perPacketBytes =
            static_cast<qint64>(kScreenShareHeaderSize + maxPayload);
        const qint64 estimatedTotalBytes =
            static_cast<qint64>(totalPackets) * perPacketBytes * destIps.size();

        if (m_bytesSentInWindow + estimatedTotalBytes > m_maxBytesPerSecond) {
            // Drop this frame to keep bandwidth under control; audio and
            // camera video will thus retain headroom on the link.
            LOG_INFO(QStringLiteral("ScreenShareSender: dropping frame to respect bandwidth cap"));
            return;
        }

        // Send the frame, fragmenting into UDP packets. Track the real
        // number of bytes we put on the wire to keep the sliding window
        // accounting reasonably accurate.
        for (quint16 packetIndex = 0; packetIndex < totalPackets; ++packetIndex) {
            const int offset = int(packetIndex) * maxPayload;
            const int len = qMin(maxPayload, encodedFrame.size() - offset);
            if (len <= 0) {
                break;
            }

            QByteArray datagram;
            datagram.reserve(kScreenShareHeaderSize + len);

            QDataStream out(&datagram, QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::BigEndian);
            out << kScreenShareMagic;
            out << frameId;
            out << packetIndex;
            out << totalPackets;
            out << static_cast<quint16>(len);

            datagram.append(encodedFrame.constData() + offset, len);

            for (const QString &ip : destIps) {
                if (ip.isEmpty()) {
                    continue;
                }

                const qint64 written =
                    m_socket->writeDatagram(datagram, QHostAddress(ip), remotePort);
                if (written < 0) {
                    LOG_WARN(QStringLiteral("ScreenShareSender: failed to send to %1:%2 - %3")
                                 .arg(ip)
                                 .arg(remotePort)
                                 .arg(m_socket->errorString()));
                    continue;
                }

                m_bytesSentInWindow += written;

                if (m_bytesSentInWindow >= m_maxBytesPerSecond) {
                    // Bandwidth budget exhausted in the middle of a frame;
                    // stop sending remaining packets for this frame.
                    return;
                }
            }
        }
    }

private:
    QUdpSocket *m_socket;
    qint64 m_bytesSentInWindow;
    qint64 m_windowStartMs;
    qint64 m_maxBytesPerSecond;
};

ScreenShareTransport::ScreenShareTransport(QObject *parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
    , m_sendTimer(new QTimer(this))
    , m_sendThread()
    , m_senderWorker(new ScreenShareSenderWorker)
    , m_remotePort(0)
    , m_sending(false)
    , m_receiving(false)
    , m_renderLabel(nullptr)
{
    m_nextFrameId = 1;
    m_lastCleanupMs = 0;

    // Limit screen-share frame rate to a modest level so that CPU
    // and bandwidth usage remain bounded even when the desktop has
    // large resolution or frequent updates.
    const int intervalMs = (Config::SCREEN_SHARE_FPS > 0)
                               ? (1000 / Config::SCREEN_SHARE_FPS)
                               : 200; // fallback
    m_sendTimer->setInterval(intervalMs);
    connect(m_sendTimer, &QTimer::timeout, this, &ScreenShareTransport::onSendTimer);
    connect(m_socket, &QUdpSocket::readyRead, this, &ScreenShareTransport::onReadyRead);

    // Configure dedicated sender thread.
    m_senderWorker->moveToThread(&m_sendThread);
    m_sendThread.setObjectName(QStringLiteral("ScreenShareSendThread"));
    connect(&m_sendThread, &QThread::finished, m_senderWorker, &QObject::deleteLater);
    connect(this,
            &ScreenShareTransport::encodedFrameReady,
            m_senderWorker,
            &ScreenShareSenderWorker::sendFrame,
            Qt::QueuedConnection);
    m_sendThread.start(QThread::NormalPriority);
}

ScreenShareTransport::~ScreenShareTransport()
{
    stopSender();
    stopReceiver();

    if (m_sendThread.isRunning()) {
        m_sendThread.quit();
        m_sendThread.wait();
    }
}

void ScreenShareTransport::setDestinations(const QSet<QString> &ips)
{
    m_destIps = ips;
    if (m_renderLabel) {
        m_renderLabel->setPixmap(QPixmap());
        m_renderLabel->setText(QStringLiteral("Host has not started screen sharing"));
    }
}

void ScreenShareTransport::setCaptureFullScreen()
{
    m_captureRect = QRect();
}

void ScreenShareTransport::setCaptureRegion(const QRect &rect)
{
    m_captureRect = rect;
}

bool ScreenShareTransport::startSender(quint16 remotePort)
{
    // Default to full-screen capture if the region has not been
    // explicitly set by the UI layer.
    if (!m_sending && m_captureRect.isNull()) {
        setCaptureFullScreen();
    }
    if (m_sending && m_remotePort == remotePort) {
        return true;
    }

    m_remotePort = remotePort;
    m_sending = true;
    m_sendTimer->start();
    return true;
}

void ScreenShareTransport::stopSender()
{
    if (m_sendTimer->isActive()) {
        m_sendTimer->stop();
    }
    m_sending = false;
}

bool ScreenShareTransport::startReceiver(quint16 localPort)
{
    stopReceiver();

    if (!m_socket->bind(QHostAddress::AnyIPv4, localPort)) {
        LOG_WARN(QStringLiteral("ScreenShareTransport: failed to bind UDP port %1: %2")
                     .arg(localPort)
                     .arg(m_socket->errorString()));
        return false;
    }

    m_receiving = true;
    return true;
}

void ScreenShareTransport::stopReceiver()
{
    if (m_receiving) {
        if (m_socket->isOpen()) {
            m_socket->close();
        }
        m_receiving = false;
    }

    m_pendingFrames.clear();
    m_lastCleanupMs = 0;

    if (m_renderLabel) {
        m_renderLabel->setPixmap(QPixmap());
        m_renderLabel->setText(QStringLiteral("Host has not started screen sharing"));
    }
}

void ScreenShareTransport::setRenderLabel(QLabel *label)
{
    m_renderLabel = label;
    if (m_renderLabel && m_renderLabel->pixmap(Qt::ReturnByValue).isNull()) {
        m_renderLabel->setText(QStringLiteral("Host has not started screen sharing"));
    }
}

void ScreenShareTransport::setRenderFitToWindow(bool fit)
{
    m_renderFitToWindow = fit;
}

void ScreenShareTransport::onSendTimer()
{
    if (!m_sending || m_destIps.isEmpty() || m_remotePort == 0) {
        return;
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return;
    }

    const QPixmap pixmap = screen->grabWindow(0);
    if (pixmap.isNull()) {
        return;
    }

    QImage image = pixmap.toImage();
    if (!m_captureRect.isNull()) {
        // Map requested capture region to the grabbed image; the
        // grabWindow(0) pixmap uses screen coordinates starting at 0,0.
        const QRect imageRect = QRect(QPoint(0, 0), image.size());
        const QRect clipped = m_captureRect.intersected(imageRect);
        if (!clipped.isEmpty()) {
            image = image.copy(clipped);
        }
    }

    // Downscale to a reasonable size for LAN transport.
    const QSize targetSize(Config::SCREEN_SHARE_MAX_WIDTH,
                           Config::SCREEN_SHARE_MAX_HEIGHT);
    image = image.scaled(targetSize,
                         Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
    if (image.isNull()) {
        return;
    }

    QByteArray buffer;
    QBuffer qBuffer(&buffer);
    qBuffer.open(QIODevice::WriteOnly);
    if (!image.save(&qBuffer, "JPG", Config::SCREEN_SHARE_JPEG_QUALITY)) {
        return;
    }

    if (buffer.isEmpty()) {
        return;
    }

    const quint32 frameId = m_nextFrameId++;

    // Offload fragmentation and UDP sending to the dedicated worker
    // thread, which also enforces a simple bandwidth cap.
    emit encodedFrameReady(buffer, m_destIps, m_remotePort, frameId);
}

void ScreenShareTransport::onReadyRead()
{
    if (!m_receiving) {
        // Ignore packets when not in receiving mode.
        while (m_socket->hasPendingDatagrams()) {
            QByteArray tmp;
            tmp.resize(int(m_socket->pendingDatagramSize()));
            m_socket->readDatagram(tmp.data(), tmp.size());
        }
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        const qint64 read =
            m_socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        if (read <= 0) {
            LOG_WARN(QStringLiteral("ScreenShareTransport: failed to read UDP datagram - %1")
                         .arg(m_socket->errorString()));
            continue;
        }

        if (read < datagram.size()) {
            datagram.resize(int(read));
        }

        if (datagram.size() < kScreenShareHeaderSize) {
            LOG_WARN(QStringLiteral("ScreenShareTransport: received too small datagram (%1 bytes)")
                         .arg(datagram.size()));
            continue;
        }

        QDataStream in(datagram);
        in.setByteOrder(QDataStream::BigEndian);

        quint32 magic = 0;
        quint32 frameId = 0;
        quint16 packetIndex = 0;
        quint16 totalPackets = 0;
        quint16 payloadSize = 0;

        in >> magic >> frameId >> packetIndex >> totalPackets >> payloadSize;
        if (in.status() != QDataStream::Ok) {
            LOG_WARN(QStringLiteral("ScreenShareTransport: failed to parse screen-share header"));
            continue;
        }

        if (magic != kScreenShareMagic) {
            // Not a screen-share packet; ignore silently.
            continue;
        }

        if (totalPackets == 0) {
            continue;
        }

        if (packetIndex >= totalPackets) {
            continue;
        }

        const int headerSize = kScreenShareHeaderSize;
        if (payloadSize == 0 || headerSize + payloadSize > datagram.size()) {
            LOG_WARN(QStringLiteral("ScreenShareTransport: invalid payload size %1 for datagram size %2")
                         .arg(payloadSize)
                         .arg(datagram.size()));
            continue;
        }

        const QByteArray payload = datagram.mid(headerSize, payloadSize);

        ScreenShareFrameAssembly &assembly = m_pendingFrames[frameId];
        if (assembly.packets.isEmpty()) {
            assembly.totalPackets = totalPackets;
            assembly.firstSeenMs = nowMs;
        } else if (assembly.totalPackets != totalPackets) {
            // Inconsistent metadata; reset this frame.
            assembly.packets.clear();
            assembly.totalPackets = totalPackets;
            assembly.firstSeenMs = nowMs;
        }

        if (!assembly.packets.contains(packetIndex)) {
            assembly.packets.insert(packetIndex, payload);
        }

        if (assembly.packets.size() == assembly.totalPackets) {
            // We have all fragments for this frame; assemble in order.
            QByteArray frameData;
            frameData.reserve(int(assembly.totalPackets) * kScreenShareMaxPayloadSize);

            bool missing = false;
            for (quint16 i = 0; i < assembly.totalPackets; ++i) {
                auto it = assembly.packets.constFind(i);
                if (it == assembly.packets.cend()) {
                    missing = true;
                    break;
                }
                frameData.append(it.value());
            }

            m_pendingFrames.remove(frameId);

            if (missing || frameData.isEmpty()) {
                continue;
            }

            QImage image;
            if (!image.loadFromData(frameData, "JPG")) {
                LOG_WARN(QStringLiteral("ScreenShareTransport: failed to decode reassembled JPEG screen frame (size=%1)")
                             .arg(frameData.size()));
                continue;
            }

            if (!image.isNull()) {
                emit screenFrameReceived(image);

                if (m_renderLabel) {
                    const QSize labelSize = m_renderLabel->size();
                    if (!labelSize.isEmpty()) {
                        const Qt::AspectRatioMode mode =
                            m_renderFitToWindow ? Qt::KeepAspectRatio
                                                : Qt::KeepAspectRatioByExpanding;
                        const QPixmap pixmap =
                            QPixmap::fromImage(image).scaled(labelSize,
                                                             mode,
                                                             Qt::SmoothTransformation);
                        m_renderLabel->setPixmap(pixmap);
                        m_renderLabel->setText(QString());
                    }
                }
            }
        }
    }

    // Drop timed-out incomplete frames.
    const qint64 nowAfterMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastCleanupMs == 0 || nowAfterMs - m_lastCleanupMs > kFrameAssemblyTimeoutMs) {
        m_lastCleanupMs = nowAfterMs;

        auto it = m_pendingFrames.begin();
        while (it != m_pendingFrames.end()) {
            if (nowAfterMs - it->firstSeenMs > kFrameAssemblyTimeoutMs) {
                it = m_pendingFrames.erase(it);
            } else {
                ++it;
            }
        }
    }
}

#include "ScreenShareTransport.moc"
