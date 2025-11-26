#ifndef CONTROLSERVER_H
#define CONTROLSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>

#include "common/Config.h"

class ControlServer : public QObject
{
    Q_OBJECT

public:
    explicit ControlServer(QObject *parent = nullptr);

    bool startServer(quint16 port = Config::CONTROL_PORT);
    void stopServer();
    void setRoomId(const QString &roomId);
    QString roomId() const;
    QString defaultRoomId() const;
    void sendChatToAll(const QString &message, const QString &roomId = QString());
    void broadcastMediaState(const QString &ip, bool micMuted, bool cameraEnabled, const QString &roomId = QString());
    void broadcastScreenShareState(const QString &ip, bool sharing, const QString &roomId = QString());

signals:
    void clientJoined(const QString &ip, const QString &roomId);
    void clientLeft(const QString &ip, const QString &roomId);
    void chatReceived(const QString &ip, const QString &roomId, const QString &message);
    void mediaStateChanged(const QString &ip, const QString &roomId, bool micMuted, bool cameraEnabled);
    void screenShareStateChanged(const QString &ip, const QString &roomId, bool sharing);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QString normalizedRoomId(const QString &roomId) const;
    QList<QTcpSocket *> clientsForRoom(const QString &roomId) const;
    void removeClientFromRoom(QTcpSocket *socket);

    QTcpServer *m_server;
    QList<QTcpSocket *> m_clients;
    QHash<QString, QList<QTcpSocket *>> m_roomClients;
    QHash<QTcpSocket *, QString> m_clientRooms;
    QTimer *m_pingTimer;
    QElapsedTimer m_elapsed;
    qint64 m_lastPongMs;
    QString m_roomId;
};

#endif // CONTROLSERVER_H
