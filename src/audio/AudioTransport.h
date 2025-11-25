#ifndef AUDIOTRANSPORT_H
#define AUDIOTRANSPORT_H

#include <QObject>
#include <QUdpSocket>
#include <QQueue>
#include <QMap>
#include <QVector>
#include <QTimer>
#include <QString>
#include <QThread>
#include <QElapsedTimer>
#include <cstdint>

class AudioEngine;
class AudioSendWorker;

class AudioTransport : public QObject
{
    Q_OBJECT

    struct JitterFrame
    {
        QByteArray pcm;
        uint32_t seq;
    };

public:
    explicit AudioTransport(AudioEngine *engine, QObject *parent = nullptr);
    ~AudioTransport();

    bool startTransport(quint16 localPort, const QString &remoteIp, quint16 remotePort);
    // Send-only mode: only transmit local audio to the given
    // remote endpoint without binding a local receive port.
    bool startSendOnly(const QString &remoteIp, quint16 remotePort);
    void stopTransport();
    void setMuted(bool muted);
    void logDiagnostics() const;

private slots:
    void onReadyRead();
    void onSendTimer();
    void onJitterTimer();

    QByteArray generatePLC(const QByteArray &lastFrame) const;

signals:
    // Emitted on the main/GUI thread whenever a new chunk
    // of captured audio should be sent over UDP. The actual
    // network I/O is performed on a dedicated worker thread
    // to avoid being blocked by heavy screen sharing or video.
    void audioFrameCaptured(const QByteArray &data,
                            const QString &remoteIp,
                            quint16 remotePort);

    // Emitted on the receive side whenever a remote audio
    // packet is successfully read and queued for playback.
    void audioFrameReceived();

private:
    QUdpSocket *udpRecvSocket;
    quint16 localPort;
    QString remoteIp;
    quint16 remotePort;
    AudioEngine *audio;
    QTimer *sendTimer;
    bool muted;
    QQueue<JitterFrame> m_jitterQueue;
    uint32_t m_lastSeq = 0;
    uint32_t m_sendSeq = 0;
    uint32_t m_expectedSeq = 1;
    QMap<uint32_t, QByteArray> m_reorderBuf;
    int m_jitterMin = 2;
    int m_jitterMax = 8;
    int m_jitterTarget = 3;
    QVector<qint64> m_interArrivalTimes;
    QElapsedTimer m_arrivalTimer;
    mutable QElapsedTimer m_diagTimer;
    uint64_t m_plcCount = 0;
    uint64_t m_lossEvents = 0;
    QByteArray m_lastPcm;
    QTimer *m_jitterTimer = nullptr;

    // Dedicated worker thread and helper object that own
    // the UDP send socket for audio frames.
    QThread sendThread;
    AudioSendWorker *sendWorker;
};

#endif // AUDIOTRANSPORT_H
