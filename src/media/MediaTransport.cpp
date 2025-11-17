#include "MediaTransport.h"

#include <QBuffer>
#include <QHostAddress>
#include <QImage>
#include <QPixmap>
#include <QVBoxLayout>

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

        QImage image;
        image.loadFromData(datagram, "JPG");
        if (!image.isNull()) {
            remoteVideoLabel->setPixmap(QPixmap::fromImage(image).scaled(remoteVideoLabel->size(),
                                                                         Qt::KeepAspectRatio,
                                                                         Qt::SmoothTransformation));
        }
    }
}

