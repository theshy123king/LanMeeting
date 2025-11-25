#ifndef MEDIATRANSPORT_H
#define MEDIATRANSPORT_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QString>
#include <QLabel>
#include <QElapsedTimer>
#include <QSize>

#ifdef USE_FFMPEG_H264
#include "media/VideoEncoder.h"
#include "media/VideoDecoder.h"
#endif

class MediaEngine;

class MediaTransport : public QObject
{
    Q_OBJECT

public:
    explicit MediaTransport(MediaEngine *engine, QObject *parent = nullptr);
    ~MediaTransport() override;

    bool startTransport(quint16 localPort, const QString &remoteIp, quint16 remotePort);
    // Send-only mode: used when we only need to push local video to a remote
    // endpoint without receiving remote video on this transport instance.
    bool startSendOnly(const QString &remoteIp, quint16 remotePort);
    void stopTransport();
    void logDiagnostics() const;

    QWidget *getRemoteVideoWidget();

signals:
    // Emitted whenever a remote video frame has been
    // successfully decoded and rendered to the label.
    void remoteFrameReceived();

private slots:
    void onSendTimer();
    void onReadyRead();

private:
    QUdpSocket *udpSendSocket;
    QUdpSocket *udpRecvSocket;
    QTimer *sendTimer;
    QElapsedTimer sendClock;
    qint64 lastSendMs;

    QString remoteIp;
    quint16 localPort;
    quint16 remotePort;

    QLabel *remoteVideoLabel;
    QWidget *remoteVideoWidget;

    MediaEngine *media;

#ifdef USE_FFMPEG_H264
    VideoEncoder *encoder;
    VideoDecoder *decoder;
    int videoWidth;
    int videoHeight;
    QSize activeEncodeBound;
    QSize fallbackEncodeBound;
    bool fallbackActive;
#endif
};

#endif // MEDIATRANSPORT_H
