#ifndef MEDIATRANSPORT_H
#define MEDIATRANSPORT_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QString>
#include <QLabel>

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

    bool startTransport(quint16 localPort, const QString &remoteIp, quint16 remotePort);
    void stopTransport();

    QWidget *getRemoteVideoWidget();

private slots:
    void onSendTimer();
    void onReadyRead();

private:
    QUdpSocket *udpSendSocket;
    QUdpSocket *udpRecvSocket;
    QTimer *sendTimer;

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
#endif
};

#endif // MEDIATRANSPORT_H

