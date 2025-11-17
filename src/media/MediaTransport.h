#ifndef MEDIATRANSPORT_H
#define MEDIATRANSPORT_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QString>
#include <QLabel>

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
};

#endif // MEDIATRANSPORT_H

