#ifndef CONTROLSERVER_H
#define CONTROLSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

#include "common/Config.h"

class ControlServer : public QObject
{
    Q_OBJECT

public:
    explicit ControlServer(QObject *parent = nullptr);

    bool startServer(quint16 port = Config::CONTROL_PORT);
    void stopServer();
    void sendChatToAll(const QString &message);
    void broadcastMediaState(const QString &ip, bool micMuted, bool cameraEnabled);
    void broadcastScreenShareState(const QString &ip, bool sharing);

signals:
    void clientJoined(const QString &ip);
    void clientLeft(const QString &ip);
    void chatReceived(const QString &ip, const QString &message);
    void mediaStateChanged(const QString &ip, bool micMuted, bool cameraEnabled);
    void screenShareStateChanged(const QString &ip, bool sharing);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QTcpServer *m_server;
    QList<QTcpSocket *> m_clients;
};

#endif // CONTROLSERVER_H
