#include "ControlServer.h"

#include <QHostAddress>
#include <QStringList>
#include "common/Logger.h"

ControlServer::ControlServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection,
            this, &ControlServer::onNewConnection);
}

bool ControlServer::startServer(quint16 port)
{
    if (m_server->isListening()) {
        LOG_INFO(QStringLiteral("ControlServer already listening on port %1").arg(port));
        return true;
    }

    const bool ok = m_server->listen(QHostAddress::Any, port);
    if (!ok) {
        LOG_ERROR(QStringLiteral("ControlServer failed to listen on port %1: %2")
                      .arg(port)
                      .arg(m_server->errorString()));
    } else {
        LOG_INFO(QStringLiteral("ControlServer listening on port %1").arg(port));
    }
    return ok;
}

void ControlServer::stopServer()
{
    for (QTcpSocket *socket : std::as_const(m_clients)) {
        if (!socket) {
            continue;
        }
        socket->disconnect(this);
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    m_clients.clear();

    if (m_server->isListening()) {
        m_server->close();
        LOG_INFO(QStringLiteral("ControlServer stopped listening"));
    }
}

void ControlServer::sendChatToAll(const QString &message)
{
    if (m_clients.isEmpty()) {
        LOG_WARN(QStringLiteral("ControlServer::sendChatToAll called with no connected clients"));
        return;
    }

    const QByteArray data = QByteArrayLiteral("CHAT:") + message.toUtf8() + '\n';

    for (QTcpSocket *socket : std::as_const(m_clients)) {
        if (!socket)
            continue;
        if (socket->state() != QAbstractSocket::ConnectedState)
            continue;

        const qint64 written = socket->write(data);
        if (written < 0) {
            LOG_WARN(QStringLiteral("ControlServer: failed to send chat to %1 - %2")
                         .arg(socket->peerAddress().toString())
                         .arg(socket->errorString()));
        }
    }
}

void ControlServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            continue;

        LOG_INFO(QStringLiteral("ControlServer: client connected from %1:%2")
                     .arg(socket->peerAddress().toString())
                     .arg(socket->peerPort()));

        m_clients.append(socket);
        connect(socket, &QTcpSocket::readyRead,
                this, &ControlServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            const QString ip = socket->peerAddress().toString();
            LOG_INFO(QStringLiteral("ControlServer: client disconnected from %1").arg(ip));
            m_clients.removeAll(socket);
            removeClientFromRoom(socket);
            emit clientLeft(ip);
            socket->deleteLater();
        });
    }
}

void ControlServer::onReadyRead()
{
    for (QTcpSocket *socket : std::as_const(m_clients)) {
        if (!socket || socket->bytesAvailable() <= 0)
            continue;

        const QByteArray data = socket->readAll();
        const QList<QByteArray> lines = data.split('\n');

        for (QByteArray line : lines) {
            line = line.trimmed();
            if (line.isEmpty())
                continue;

            LOG_INFO(QStringLiteral("ControlServer received: %1")
                         .arg(QString::fromUtf8(line)));

            const QString clientIp = socket->peerAddress().toString();

            if (line == QByteArrayLiteral("JOIN")) {
                socket->write("OK\n");
                socket->flush();

                LOG_INFO(QStringLiteral("ControlServer: JOIN confirmed for %1").arg(clientIp));
                emit clientJoined(clientIp);
            } else if (line == QByteArrayLiteral("LEAVE")) {
                LOG_INFO(QStringLiteral("ControlServer: LEAVE received from %1").arg(clientIp));
                socket->disconnectFromHost();
            } else if (line.startsWith(QByteArrayLiteral("CHAT:"))) {
                const QString msg = QString::fromUtf8(line.mid(5));
                LOG_INFO(QStringLiteral("ControlServer: chat from %1 - %2").arg(clientIp, msg));
                emit chatReceived(clientIp, msg);
            } else if (line.startsWith(QByteArrayLiteral("CREATE_ROOM:"))) {
                const QByteArray payload = line.mid(QByteArrayLiteral("CREATE_ROOM:").size());
                const QString roomId = QString::fromUtf8(payload).trimmed();

                LOG_INFO(QStringLiteral("ControlServer: CREATE_ROOM from %1 - \"%2\"")
                             .arg(clientIp, roomId));

                if (roomId.isEmpty()) {
                    sendError(socket, QStringLiteral("房间 ID 不能为空"));
                    continue;
                }

                if (m_rooms.contains(roomId)) {
                    sendError(socket,
                              QStringLiteral("房间 \"%1\" 已存在").arg(roomId));
                    continue;
                }

                m_rooms.insert(roomId, QList<QTcpSocket *>{});
                LOG_INFO(QStringLiteral("ControlServer: room \"%1\" created").arg(roomId));

                const QByteArray response =
                    QByteArrayLiteral("ROOM_CREATED:") + roomId.toUtf8() + '\n';
                const qint64 written = socket->write(response);
                if (written < 0) {
                    LOG_WARN(QStringLiteral("ControlServer: failed to send ROOM_CREATED to %1 - %2")
                                 .arg(clientIp)
                                 .arg(socket->errorString()));
                }
            } else if (line.startsWith(QByteArrayLiteral("JOIN_ROOM:"))) {
                const QByteArray payload = line.mid(QByteArrayLiteral("JOIN_ROOM:").size());
                const QString roomId = QString::fromUtf8(payload).trimmed();

                LOG_INFO(QStringLiteral("ControlServer: JOIN_ROOM from %1 - \"%2\"")
                             .arg(clientIp, roomId));

                if (roomId.isEmpty()) {
                    sendError(socket, QStringLiteral("房间 ID 不能为空"));
                    continue;
                }

                if (!m_rooms.contains(roomId)) {
                    sendError(socket,
                              QStringLiteral("房间 \"%1\" 不存在").arg(roomId));
                    continue;
                }

                // Remove from previous room if necessary.
                if (m_clientRooms.contains(socket)) {
                    const QString previousRoomId = m_clientRooms.value(socket);
                    if (previousRoomId == roomId) {
                        sendError(socket,
                                  QStringLiteral("已在房间 \"%1\" 中").arg(roomId));
                        continue;
                    }

                    auto it = m_rooms.find(previousRoomId);
                    if (it != m_rooms.end()) {
                        it->removeAll(socket);
                        if (it->isEmpty()) {
                            LOG_INFO(QStringLiteral("ControlServer: room \"%1\" removed (empty after client switch)")
                                         .arg(previousRoomId));
                            m_rooms.erase(it);
                        }
                    }
                }

                QList<QTcpSocket *> &members = m_rooms[roomId];
                if (!members.contains(socket)) {
                    members.append(socket);
                }
                m_clientRooms.insert(socket, roomId);

                LOG_INFO(QStringLiteral("ControlServer: client %1 joined room \"%2\"")
                             .arg(clientIp, roomId));

                const QByteArray response =
                    QByteArrayLiteral("ROOM_JOINED:") + roomId.toUtf8() + '\n';
                const qint64 written = socket->write(response);
                if (written < 0) {
                    LOG_WARN(QStringLiteral("ControlServer: failed to send ROOM_JOINED to %1 - %2")
                                 .arg(clientIp)
                                 .arg(socket->errorString()));
                }
            } else if (line == QByteArrayLiteral("ROOM_LIST")) {
                const QStringList roomIds = m_rooms.keys();
                const QString joined = roomIds.join(',');
                const QByteArray response =
                    QByteArrayLiteral("ROOM_LIST_RESULT:") + joined.toUtf8() + '\n';

                const qint64 written = socket->write(response);
                if (written < 0) {
                    LOG_WARN(QStringLiteral("ControlServer: failed to send ROOM_LIST_RESULT to %1 - %2")
                                 .arg(clientIp)
                                 .arg(socket->errorString()));
                } else {
                    LOG_INFO(QStringLiteral("ControlServer: sent ROOM_LIST_RESULT to %1 (%2 rooms)")
                                 .arg(clientIp)
                                 .arg(roomIds.size()));
                }
            }
        }
    }
}

void ControlServer::removeClientFromRoom(QTcpSocket *socket)
{
    if (!socket) {
        return;
    }

    const QString roomId = m_clientRooms.take(socket);
    if (roomId.isEmpty()) {
        return;
    }

    auto it = m_rooms.find(roomId);
    if (it == m_rooms.end()) {
        return;
    }

    it->removeAll(socket);
    if (it->isEmpty()) {
        LOG_INFO(QStringLiteral("ControlServer: room \"%1\" removed (last client disconnected)")
                     .arg(roomId));
        m_rooms.erase(it);
    }
}

void ControlServer::sendError(QTcpSocket *socket, const QString &message)
{
    if (!socket) {
        return;
    }

    const QByteArray data =
        QByteArrayLiteral("ERROR:") + message.toUtf8() + '\n';
    const qint64 written = socket->write(data);
    if (written < 0) {
        LOG_WARN(QStringLiteral("ControlServer: failed to send ERROR to %1 - %2")
                     .arg(socket->peerAddress().toString())
                     .arg(socket->errorString()));
    } else {
        LOG_WARN(QStringLiteral("ControlServer: sent ERROR to %1 - %2")
                     .arg(socket->peerAddress().toString(), message));
    }
}
