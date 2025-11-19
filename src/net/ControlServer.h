#ifndef CONTROLSERVER_H
#define CONTROLSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>

#include "common/Config.h"

class ControlServer : public QObject
{
    Q_OBJECT

public:
    explicit ControlServer(QObject *parent = nullptr);

    bool startServer(quint16 port = Config::CONTROL_PORT);
    void stopServer();
    void sendChatToAll(const QString &message);

signals:
    void clientJoined(const QString &ip);
    void clientLeft(const QString &ip);
    void chatReceived(const QString &ip, const QString &message);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QTcpServer *m_server;
    QList<QTcpSocket *> m_clients;

    // Room ID -> sockets in the room
    QHash<QString, QList<QTcpSocket *>> m_rooms;
    // Socket -> current room ID (empty if not in any room)
    QHash<QTcpSocket *, QString> m_clientRooms;

    void removeClientFromRoom(QTcpSocket *socket);
    void sendError(QTcpSocket *socket, const QString &message);
};

#endif // CONTROLSERVER_H
