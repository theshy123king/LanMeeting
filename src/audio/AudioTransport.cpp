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
    , muted(false)
{
    sendTimer->setInterval(20);
    connect(sendTimer, &QTimer::timeout, this, &AudioTransport::onSendTimer);
}

AudioTransport::~AudioTransport()
{
    stopTransport();
}

bool AudioTransport::startTransport(quint16 localPortValue,
                                    const QString &remoteIpValue,
                                    quint16 remotePortValue)
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

bool AudioTransport::startSendOnly(const QString &remoteIpValue,
                                   quint16 remotePortValue)
{
    // Only (re)configure the sending side; do not bind the receive socket.
    stopTransport();

    localPort = 0;
    remoteIp = remoteIpValue;
    remotePort = remotePortValue;

    sendTimer->start();

    return true;
}

void AudioTransport::stopTransport()
{
    if (sendTimer->isActive()) {
        sendTimer->stop();
    }

    udpSendSocket->disconnect(this);
    if (udpRecvSocket->isOpen()) {
        udpRecvSocket->close();
    }

    udpRecvSocket->disconnect(this);

    // Reset state so a future reconnection does not reuse stale address/port.
    localPort = 0;
    remotePort = 0;
    remoteIp.clear();
}

void AudioTransport::setMuted(bool mutedValue)
{
    muted = mutedValue;
    LOG_INFO(QStringLiteral("AudioTransport: mute state changed to %1")
                 .arg(muted ? QStringLiteral("ON") : QStringLiteral("OFF")));
}

void AudioTransport::onReadyRead()
{
    if (!audio) {
        return;
    }

    while (udpRecvSocket->hasPendingDatagrams()) {
        QByteArray buffer;
        const qint64 pendingSize = udpRecvSocket->pendingDatagramSize();
        if (pendingSize <= 0) {
            LOG_WARN(QStringLiteral("AudioTransport: pendingDatagramSize returned %1")
                         .arg(pendingSize));
            // Underlying buffer looks inconsistent; exit the loop to avoid spinning.
            break;
        }

        buffer.resize(int(pendingSize));
        const qint64 read = udpRecvSocket->readDatagram(buffer.data(), buffer.size());
        if (read <= 0) {
            LOG_WARN(QStringLiteral("AudioTransport: failed to read UDP datagram - %1")
                         .arg(udpRecvSocket->errorString()));
            continue;
        }

        if (read < buffer.size()) {
            buffer.resize(int(read));
        }

        if (buffer.isEmpty()) {
            LOG_WARN(QStringLiteral("AudioTransport: received empty UDP datagram"));
            continue;
        }

        audio->playAudio(buffer);
    }
}

void AudioTransport::onSendTimer()
{
    if (!audio || remoteIp.isEmpty() || remotePort == 0 || muted) {
        return;
    }

    const QByteArray data = audio->readCapturedAudio();
    if (data.isEmpty()) {
        return;
    }

    const qint64 written =
        udpSendSocket->writeDatagram(data, QHostAddress(remoteIp), remotePort);
    if (written < 0) {
        LOG_WARN(QStringLiteral("AudioTransport: failed to send UDP datagram to %1:%2 - %3")
                     .arg(remoteIp)
                     .arg(remotePort)
                     .arg(udpSendSocket->errorString()));
    }
}
