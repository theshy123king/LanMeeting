#ifndef AUDIOTRANSPORT_H
#define AUDIOTRANSPORT_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QString>
#include <QThread>

class AudioEngine;
class AudioSendWorker;

class AudioTransport : public QObject
{
    Q_OBJECT

public:
    explicit AudioTransport(AudioEngine *engine, QObject *parent = nullptr);
    ~AudioTransport();

    bool startTransport(quint16 localPort, const QString &remoteIp, quint16 remotePort);
    // Send-only mode: only transmit local audio to the given
    // remote endpoint without binding a local receive port.
    bool startSendOnly(const QString &remoteIp, quint16 remotePort);
    void stopTransport();
    void setMuted(bool muted);

private slots:
    void onReadyRead();
    void onSendTimer();

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

    // Dedicated worker thread and helper object that own
    // the UDP send socket for audio frames.
    QThread sendThread;
    AudioSendWorker *sendWorker;
};

#endif // AUDIOTRANSPORT_H
