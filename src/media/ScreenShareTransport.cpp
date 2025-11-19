#include "ScreenShareTransport.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QLabel>
#include <QPixmap>
#include <QScreen>

#include "common/Logger.h"

ScreenShareTransport::ScreenShareTransport(QObject *parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
    , m_sendTimer(new QTimer(this))
    , m_remotePort(0)
    , m_sending(false)
    , m_receiving(false)
    , m_renderLabel(nullptr)
{
    m_sendTimer->setInterval(80); // ~12.5 fps, enough for screen sharing
    connect(m_sendTimer, &QTimer::timeout, this, &ScreenShareTransport::onSendTimer);
    connect(m_socket, &QUdpSocket::readyRead, this, &ScreenShareTransport::onReadyRead);
}

ScreenShareTransport::~ScreenShareTransport()
{
    stopSender();
    stopReceiver();
}

void ScreenShareTransport::setDestinations(const QSet<QString> &ips)
{
    m_destIps = ips;
}

bool ScreenShareTransport::startSender(quint16 remotePort)
{
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

    // Downscale to a reasonable size for LAN transport.
    const QSize targetSize(1280, 720);
    const QImage image = pixmap.toImage().scaled(targetSize,
                                                 Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
    if (image.isNull()) {
        return;
    }

    QByteArray buffer;
    QBuffer qBuffer(&buffer);
    qBuffer.open(QIODevice::WriteOnly);
    if (!image.save(&qBuffer, "JPG", 70)) { // moderate quality
        return;
    }

    if (buffer.isEmpty()) {
        return;
    }

    for (const QString &ip : std::as_const(m_destIps)) {
        if (ip.isEmpty()) {
            continue;
        }
        const qint64 written =
            m_socket->writeDatagram(buffer, QHostAddress(ip), m_remotePort);
        if (written < 0) {
            LOG_WARN(QStringLiteral("ScreenShareTransport: failed to send screen frame to %1:%2 - %3")
                         .arg(ip)
                         .arg(m_remotePort)
                         .arg(m_socket->errorString()));
        }
    }
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

        if (datagram.isEmpty()) {
            continue;
        }

        if (!m_renderLabel) {
            continue;
        }

        QImage image;
        if (!image.loadFromData(datagram, "JPG")) {
            LOG_WARN(QStringLiteral("ScreenShareTransport: failed to decode JPEG screen frame (size=%1)")
                         .arg(datagram.size()));
            continue;
        }

        if (!image.isNull()) {
            m_renderLabel->setPixmap(QPixmap::fromImage(image).scaled(m_renderLabel->size(),
                                                                      Qt::KeepAspectRatio,
                                                                      Qt::SmoothTransformation));
            m_renderLabel->setText(QString());
        }
    }
}
