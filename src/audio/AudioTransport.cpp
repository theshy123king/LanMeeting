#include "AudioTransport.h"

#include <QHostAddress>

#include "AudioEngine.h"
#include "common/Logger.h"

AudioTransport::AudioTransport(AudioEngine *engine, QObject *parent)
    : QObject(parent)
    , udpSendSocket(new QUdpSocket(this))
    , udpRecvSocket(new QUdpSocket(this))
    , localPort(0)
    , remoteIp()
    , remotePort(0)
    , audio(engine)
    , sendTimer(new QTimer(this))
{
    sendTimer->setInterval(20);
    connect(sendTimer, &QTimer::timeout, this, &AudioTransport::onSendTimer);
}

AudioTransport::~AudioTransport()
{
    stopTransport();
}

bool AudioTransport::startTransport(quint16 localPortValue, const QString &remoteIpValue, quint16 remotePortValue)
{
    stopTransport();

    localPort = localPortValue;
    remoteIp = remoteIpValue;
    remotePort = remotePortValue;

    if (!udpRecvSocket->bind(QHostAddress::AnyIPv4, localPort)) {
        LOG_WARN(QStringLiteral("AudioTransport: failed to bind UDP port %1: %2")
                     .arg(localPort)
                     .arg(udpRecvSocket->errorString()));
        return false;
    }

    connect(udpRecvSocket, &QUdpSocket::readyRead, this, &AudioTransport::onReadyRead);

    sendTimer->start();

    return true;
}

void AudioTransport::stopTransport()
{
    if (sendTimer->isActive()) {
        sendTimer->stop();
    }

    if (udpRecvSocket->isOpen()) {
        udpRecvSocket->close();
    }

    udpRecvSocket->disconnect(this);
}

void AudioTransport::onReadyRead()
{
    if (!audio) {
        return;
    }

    while (udpRecvSocket->hasPendingDatagrams()) {
        QByteArray buffer;
        buffer.resize(int(udpRecvSocket->pendingDatagramSize()));
        const qint64 read = udpRecvSocket->readDatagram(buffer.data(), buffer.size());
        if (read <= 0) {
            LOG_WARN(QStringLiteral("AudioTransport: failed to read UDP datagram - %1")
                         .arg(udpRecvSocket->errorString()));
            continue;
        }
        if (!buffer.isEmpty()) {
            audio->playAudio(buffer);
        }
    }
}

void AudioTransport::onSendTimer()
{
    if (!audio || remoteIp.isEmpty() || remotePort == 0) {
        return;
    }

    const QByteArray data = audio->readCapturedAudio();
    if (data.isEmpty()) {
        return;
    }

    const qint64 written = udpSendSocket->writeDatagram(data, QHostAddress(remoteIp), remotePort);
    if (written < 0) {
        LOG_WARN(QStringLiteral("AudioTransport: failed to send UDP datagram to %1:%2 - %3")
                     .arg(remoteIp)
                     .arg(remotePort)
                     .arg(udpSendSocket->errorString()));
    }
}
