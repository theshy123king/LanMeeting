#include "AudioTransport.h"

#include <QHostAddress>

#include "AudioEngine.h"
#include "common/Logger.h"

// Worker object that lives in a dedicated high-priority thread and
// performs the actual UDP send for audio frames so that the main/UI
// thread (where AudioEngine typically lives) is not blocked by
// network I/O or kernel socket back-pressure.
class AudioSendWorker : public QObject
{
    Q_OBJECT

public:
    explicit AudioSendWorker(QObject *parent = nullptr)
        : QObject(parent)
        , socket(nullptr)
    {
    }

public slots:
    void sendAudioFrame(const QByteArray &data, const QString &ip, quint16 port)
    {
        if (data.isEmpty() || ip.isEmpty() || port == 0) {
            return;
        }

        if (!socket) {
            socket = new QUdpSocket(this);
        }

        const qint64 written = socket->writeDatagram(data, QHostAddress(ip), port);
        if (written < 0) {
            LOG_WARN(QStringLiteral("AudioSendWorker: failed to send UDP datagram to %1:%2 - %3")
                         .arg(ip)
                         .arg(port)
                         .arg(socket->errorString()));
        }
    }

private:
    QUdpSocket *socket;
};

AudioTransport::AudioTransport(AudioEngine *engine, QObject *parent)
    : QObject(parent)
    , udpRecvSocket(new QUdpSocket(this))
    , localPort(0)
    , remoteIp()
    , remotePort(0)
    , audio(engine)
    , sendTimer(new QTimer(this))
    , muted(false)
    , sendThread()
    , sendWorker(new AudioSendWorker)
{
    sendTimer->setInterval(20);
    connect(sendTimer, &QTimer::timeout, this, &AudioTransport::onSendTimer);

    // Configure dedicated audio send thread.
    sendWorker->moveToThread(&sendThread);
    sendThread.setObjectName(QStringLiteral("AudioSendThread"));
    connect(&sendThread, &QThread::finished, sendWorker, &QObject::deleteLater);

    // Audio frames captured on the main/GUI thread are forwarded to
    // the worker via a queued connection so that network I/O happens
    // independently of screen sharing or video encoding work.
    connect(this,
            &AudioTransport::audioFrameCaptured,
            sendWorker,
            &AudioSendWorker::sendAudioFrame,
            Qt::QueuedConnection);

    // Give the audio sending thread a higher scheduling priority so
    // that 20 ms audio frames are transmitted on time even under load.
    sendThread.start(QThread::TimeCriticalPriority);
}

AudioTransport::~AudioTransport()
{
    if (sendThread.isRunning()) {
        sendThread.quit();
        sendThread.wait();
    }

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

    // Offload the actual UDP send to the dedicated audio send thread.
    emit audioFrameCaptured(data, remoteIp, remotePort);
}

#include "AudioTransport.moc"
