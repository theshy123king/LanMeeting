#include "AudioTransport.h"

#include <QHostAddress>
#include <QtEndian>
#include <algorithm>
#include <limits>
#include <utility>

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
    , m_jitterQueue()
    , m_lastSeq(0)
    , m_sendSeq(0)
    , m_expectedSeq(1)
    , m_reorderBuf()
    , m_lastPcm()
    , m_jitterTimer(nullptr)
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

    if (!m_jitterTimer) {
        m_jitterTimer = new QTimer(this);
        m_jitterTimer->setInterval(20);
        connect(m_jitterTimer, &QTimer::timeout, this, &AudioTransport::onJitterTimer);
    }

    m_jitterQueue.clear();
    m_lastSeq = 0;
    m_sendSeq = 0;
    m_expectedSeq = 1;
    m_reorderBuf.clear();
    m_jitterTarget = m_jitterMin + 1;
    m_interArrivalTimes.clear();
    m_arrivalTimer.invalidate();
    m_diagTimer.restart();
    m_plcCount = 0;
    m_lossEvents = 0;
    m_lastPcm.clear();
    m_jitterTimer->start();

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
    m_sendSeq = 0;
    m_interArrivalTimes.clear();
    m_arrivalTimer.invalidate();
    m_diagTimer.restart();
    m_plcCount = 0;
    m_lossEvents = 0;

    sendTimer->start();

    return true;
}

void AudioTransport::stopTransport()
{
    if (sendTimer->isActive()) {
        sendTimer->stop();
    }

    if (m_jitterTimer) {
        m_jitterTimer->stop();
        m_jitterTimer->deleteLater();
        m_jitterTimer = nullptr;
    }

    m_jitterQueue.clear();
    m_lastSeq = 0;
    m_sendSeq = 0;
    m_expectedSeq = 1;
    m_reorderBuf.clear();
    m_jitterTarget = m_jitterMin + 1;
    m_interArrivalTimes.clear();
    m_arrivalTimer.invalidate();
    m_diagTimer.invalidate();
    m_plcCount = 0;
    m_lossEvents = 0;
    m_lastPcm.clear();

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

void AudioTransport::logDiagnostics() const
{
    qint64 jitterSpread = 0;
    if (!m_interArrivalTimes.isEmpty()) {
        qint64 minIa = std::numeric_limits<qint64>::max();
        qint64 maxIa = 0;
        for (qint64 ia : std::as_const(m_interArrivalTimes)) {
            minIa = std::min(minIa, ia);
            maxIa = std::max(maxIa, ia);
        }
        if (maxIa > minIa) {
            jitterSpread = maxIa - minIa;
        }
    }
    LOG_INFO(QStringLiteral("AudioNet diag: jitterQ=%1 target=%2 reorderBuf=%3 plcCount=%4 lossEvents=%5 iaJitter=%6ms")
                 .arg(m_jitterQueue.size())
                 .arg(m_jitterTarget)
                 .arg(m_reorderBuf.size())
                 .arg(static_cast<qulonglong>(m_plcCount))
                 .arg(static_cast<qulonglong>(m_lossEvents))
                 .arg(jitterSpread));
}

QByteArray AudioTransport::generatePLC(const QByteArray &lastFrame) const
{
    if (lastFrame.isEmpty() || (lastFrame.size() % int(sizeof(qint16)) != 0)) {
        return QByteArray();
    }

    const int sampleCount = lastFrame.size() / int(sizeof(qint16));
    if (sampleCount <= 0) {
        return QByteArray();
    }

    QByteArray plcFrame(lastFrame.size(), Qt::Uninitialized);
    const qint16 *in = reinterpret_cast<const qint16 *>(lastFrame.constData());
    qint16 *out = reinterpret_cast<qint16 *>(plcFrame.data());

    const int firstHalf = sampleCount / 2;
    const int secondHalf = sampleCount - firstHalf;

    auto clampSample = [](double v) -> qint16 {
        if (v > 32767.0) {
            return 32767;
        }
        if (v < -32768.0) {
            return -32768;
        }
        return static_cast<qint16>(v);
    };

    for (int i = 0; i < firstHalf; ++i) {
        const double t = firstHalf > 1 ? static_cast<double>(i) / static_cast<double>(firstHalf - 1) : 0.0;
        const double gain = 1.0 - 0.5 * t; // from 1.0 down to 0.5
        out[i] = clampSample(static_cast<double>(in[i]) * gain);
    }

    for (int i = 0; i < secondHalf; ++i) {
        const double t = secondHalf > 1 ? static_cast<double>(i) / static_cast<double>(secondHalf - 1) : 0.0;
        const double gain = 0.5 * (1.0 - t); // from 0.5 down to 0.0
        const int idx = firstHalf + i;
        const qint16 sample = (idx < sampleCount) ? in[idx] : 0;
        out[idx] = clampSample(static_cast<double>(sample) * gain);
    }

    return plcFrame;
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

        uint32_t seq = 0;
        QByteArray pcm;
        if (buffer.size() >= int(sizeof(uint32_t))) {
            seq = qFromBigEndian<uint32_t>(reinterpret_cast<const uchar *>(buffer.constData()));
            pcm = buffer.mid(int(sizeof(uint32_t)));
        } else {
            seq = ++m_lastSeq;
            pcm = buffer;
        }

        if (pcm.isEmpty()) {
            LOG_WARN(QStringLiteral("AudioTransport: received UDP audio packet with no PCM payload"));
            continue;
        }

        if (!m_arrivalTimer.isValid()) {
            m_arrivalTimer.start();
        } else {
            const qint64 delta = m_arrivalTimer.restart();
            if (delta > 0) {
                m_interArrivalTimes.append(delta);
                if (m_interArrivalTimes.size() > 20) {
                    m_interArrivalTimes.remove(0);
                }

                qint64 minIa = std::numeric_limits<qint64>::max();
                qint64 maxIa = 0;
                for (qint64 ia : std::as_const(m_interArrivalTimes)) {
                    minIa = std::min(minIa, ia);
                    maxIa = std::max(maxIa, ia);
                }
                const qint64 jitter = maxIa > minIa ? (maxIa - minIa) : 0;
                const qint64 highJitter = 20; // ms window mapped to max depth
                const double ratio = qBound(0.0, double(jitter) / double(highJitter), 1.0);
                const int target = int(m_jitterMin + ratio * (m_jitterMax - m_jitterMin));
                m_jitterTarget = qBound(m_jitterMin, target, m_jitterMax);
            }
        }

        m_reorderBuf.insert(seq, pcm);

        while (m_reorderBuf.contains(m_expectedSeq)) {
            const QByteArray orderedPcm = m_reorderBuf.take(m_expectedSeq);
            m_jitterQueue.enqueue({orderedPcm, m_expectedSeq});
            if (m_jitterQueue.size() > 5) {
                m_jitterQueue.dequeue();
            }
            m_lastPcm = orderedPcm;
            ++m_expectedSeq;
            emit audioFrameReceived();
        }

        while (m_reorderBuf.size() > 20) {
            const auto it = m_reorderBuf.begin();
            const uint32_t droppedSeq = it.key();
            m_reorderBuf.erase(it);
            if (droppedSeq >= m_expectedSeq) {
                m_expectedSeq = droppedSeq + 1;
            }
        }
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

    QByteArray packet;
    packet.resize(int(sizeof(uint32_t)));
    qToBigEndian<uint32_t>(++m_sendSeq, reinterpret_cast<uchar *>(packet.data()));
    packet.append(data);

    // Offload the actual UDP send to the dedicated audio send thread.
    emit audioFrameCaptured(packet, remoteIp, remotePort);
}

void AudioTransport::onJitterTimer()
{
    if (!audio) {
        return;
    }

    static uint32_t lastPlayedSeq = 0;
    bool plcApplied = false;

    if (m_jitterQueue.isEmpty()) {
        lastPlayedSeq = 0;
        return;
    }

    if (m_jitterQueue.size() > m_jitterMax + 2) {
        m_jitterQueue.dequeue();
    }

    if (m_jitterQueue.size() < m_jitterTarget) {
        return;
    }

    const JitterFrame frame = m_jitterQueue.dequeue();

    if (lastPlayedSeq != 0 && frame.seq > lastPlayedSeq + 1 && !m_lastPcm.isEmpty()) {
        ++m_lossEvents;
        const QByteArray plc = generatePLC(m_lastPcm);
        if (!plc.isEmpty()) {
            audio->playAudio(plc);
            plcApplied = true;
            ++m_plcCount;
        } else {
            audio->playAudio(m_lastPcm);
        }
    }

    audio->playAudio(frame.pcm);
    lastPlayedSeq = frame.seq;

    if (!m_diagTimer.isValid()) {
        m_diagTimer.start();
    }
    if (m_diagTimer.hasExpired(12000)) {
        qint64 jitterSpread = 0;
        if (!m_interArrivalTimes.isEmpty()) {
            qint64 minIa = std::numeric_limits<qint64>::max();
            qint64 maxIa = 0;
            for (qint64 ia : std::as_const(m_interArrivalTimes)) {
                minIa = std::min(minIa, ia);
                maxIa = std::max(maxIa, ia);
            }
            if (maxIa > minIa) {
                jitterSpread = maxIa - minIa;
            }
        }
        LOG_INFO(QStringLiteral("AudioNet jitter: q=%1 target=%2 reorder=%3 plcApplied=%4 iaJitter=%5ms")
                     .arg(m_jitterQueue.size())
                     .arg(m_jitterTarget)
                     .arg(m_reorderBuf.size())
                     .arg(plcApplied)
                     .arg(jitterSpread));
        logDiagnostics();
        m_diagTimer.restart();
    }
}

#include "AudioTransport.moc"
