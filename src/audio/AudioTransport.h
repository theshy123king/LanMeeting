#ifndef AUDIOTRANSPORT_H
#define AUDIOTRANSPORT_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QString>

class AudioEngine;

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

private:
    QUdpSocket *udpSendSocket;
    QUdpSocket *udpRecvSocket;
    quint16 localPort;
    QString remoteIp;
    quint16 remotePort;
    AudioEngine *audio;
    QTimer *sendTimer;
    bool muted;
};

#endif // AUDIOTRANSPORT_H
