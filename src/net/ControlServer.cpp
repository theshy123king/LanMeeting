#include "ControlServer.h"

#include <QHostAddress>
#include "common/Logger.h"

ControlServer::ControlServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_pingTimer(new QTimer(this))
    , m_elapsed()
    , m_lastPongMs(0)
    , m_roomId(QString::fromUtf8(Config::DEFAULT_ROOM_ID))
{
    connect(m_server, &QTcpServer::newConnection,
            this, &ControlServer::onNewConnection);

    m_pingTimer->setInterval(5000);
    connect(m_pingTimer, &QTimer::timeout, this, [this]() {
        if (!m_elapsed.isValid()) {
            m_elapsed.start();
            m_lastPongMs = m_elapsed.elapsed();
            return;
        }
        const qint64 now = m_elapsed.elapsed();
        if (m_lastPongMs > 0 && now - m_lastPongMs > 30000) {
            LOG_INFO(QStringLiteral("ControlServer: no client ping seen in >30s"));
        }
    });
    m_pingTimer->start();
}

void ControlServer::setRoomId(const QString &roomId)
{
    m_roomId = normalizedRoomId(roomId);
}

QString ControlServer::roomId() const
{
    return normalizedRoomId(m_roomId);
}

QString ControlServer::defaultRoomId() const
{
    return QString::fromUtf8(Config::DEFAULT_ROOM_ID);
}

bool ControlServer::startServer(quint16 port)
{
    if (m_server->isListening()) {
        LOG_INFO(QStringLiteral("ControlServer already listening on port %1 (default room %2)")
                     .arg(port)
                     .arg(roomId()));
        return true;
    }

    const bool ok = m_server->listen(QHostAddress::Any, port);
    if (!ok) {
        LOG_ERROR(QStringLiteral("ControlServer failed to listen on port %1: %2")
                      .arg(port)
                      .arg(m_server->errorString()));
    } else {
        LOG_INFO(QStringLiteral("ControlServer listening on port %1 (default room %2)")
                     .arg(port)
                     .arg(roomId()));
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
    m_roomClients.clear();
    m_clientRooms.clear();

    if (m_server->isListening()) {
        m_server->close();
        LOG_INFO(QStringLiteral("ControlServer stopped listening"));
    }
}

void ControlServer::sendChatToAll(const QString &message, const QString &roomId)
{
    const QString targetRoom = normalizedRoomId(roomId.isEmpty() ? m_roomId : roomId);
    const QList<QTcpSocket *> roomClients = clientsForRoom(targetRoom);

    if (roomClients.isEmpty()) {
        LOG_WARN(QStringLiteral("ControlServer::sendChatToAll called with no connected clients in room %1")
                     .arg(targetRoom));
        return;
    }

    const QByteArray data = QByteArrayLiteral("CHAT:") + message.toUtf8() + '\n';

    for (QTcpSocket *socket : roomClients) {
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

void ControlServer::broadcastMediaState(const QString &ip,
                                        bool micMuted,
                                        bool cameraEnabled,
                                        const QString &roomId)
{
    const QString targetRoom = normalizedRoomId(roomId.isEmpty() ? m_roomId : roomId);
    const QList<QTcpSocket *> roomClients = clientsForRoom(targetRoom);
    if (roomClients.isEmpty()) {
        return;
    }

    const QByteArray line = QByteArrayLiteral("STATE:MEDIA;room=") + targetRoom.toUtf8()
                            + QByteArrayLiteral(";ip=") + ip.toUtf8()
                            + QByteArrayLiteral(";mic=") + (micMuted ? "1" : "0")
                            + QByteArrayLiteral(";cam=") + (cameraEnabled ? "1" : "0")
                            + '\n';

    for (QTcpSocket *socket : roomClients) {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        socket->write(line);
    }
}

void ControlServer::broadcastScreenShareState(const QString &ip, bool sharing, const QString &roomId)
{
    const QString targetRoom = normalizedRoomId(roomId.isEmpty() ? m_roomId : roomId);
    const QList<QTcpSocket *> roomClients = clientsForRoom(targetRoom);
    if (roomClients.isEmpty()) {
        return;
    }

    const QByteArray line = QByteArrayLiteral("STATE:SCREEN;room=") + targetRoom.toUtf8()
                            + QByteArrayLiteral(";ip=") + ip.toUtf8()
                            + QByteArrayLiteral(";on=") + (sharing ? "1" : "0")
                            + '\n';

    for (QTcpSocket *socket : roomClients) {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        socket->write(line);
    }
}

QString ControlServer::normalizedRoomId(const QString &roomId) const
{
    if (!roomId.trimmed().isEmpty()) {
        return roomId.trimmed();
    }
    return QString::fromUtf8(Config::DEFAULT_ROOM_ID);
}

QList<QTcpSocket *> ControlServer::clientsForRoom(const QString &roomId) const
{
    return m_roomClients.value(normalizedRoomId(roomId));
}

void ControlServer::removeClientFromRoom(QTcpSocket *socket)
{
    const QString roomKey = m_clientRooms.take(socket);
    if (roomKey.isEmpty()) {
        return;
    }

    auto it = m_roomClients.find(roomKey);
    if (it != m_roomClients.end()) {
        it->removeAll(socket);
        if (it->isEmpty()) {
            m_roomClients.erase(it);
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
            const QString roomId = m_clientRooms.value(socket, this->roomId());
            LOG_INFO(QStringLiteral("ControlServer: client disconnected from %1 (room %2)").arg(ip, roomId));
            m_clients.removeAll(socket);
            removeClientFromRoom(socket);
            emit clientLeft(ip, roomId);
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

            if (line.startsWith(QByteArrayLiteral("JOIN"))) {
                const QString previousRoom = m_clientRooms.value(socket);
                const bool alreadyJoined = m_clientRooms.contains(socket);
                QString requestedRoom = previousRoom;
                if (line.contains(';')) {
                    const QList<QByteArray> fields = line.split(';');
                    for (const QByteArray &field : fields) {
                        if (field.startsWith(QByteArrayLiteral("room="))) {
                            requestedRoom = QString::fromUtf8(field.mid(5));
                        }
                    }
                }
                const QString roomId = normalizedRoomId(requestedRoom);
                m_clientRooms.insert(socket, roomId);
                auto &list = m_roomClients[roomId];
                if (!list.contains(socket)) {
                    list.append(socket);
                }

                socket->write("OK\n");
                socket->flush();

                if (!alreadyJoined || previousRoom != roomId) {
                    LOG_INFO(QStringLiteral("ControlServer: JOIN confirmed for %1 in room %2")
                                 .arg(clientIp, roomId));
                    emit clientJoined(clientIp, roomId);
                } else {
                    LOG_INFO(QStringLiteral("ControlServer: duplicate JOIN from %1 in room %2 ignored")
                                 .arg(clientIp, roomId));
                }
            } else if (line == QByteArrayLiteral("LEAVE")) {
                const QString roomId = m_clientRooms.value(socket, this->roomId());
                LOG_INFO(QStringLiteral("ControlServer: LEAVE received from %1 (room %2)").arg(clientIp, roomId));
                removeClientFromRoom(socket);
                socket->disconnectFromHost();
            } else if (line == QByteArrayLiteral("PING")) {
                socket->write("PONG\n");
                socket->flush();
                if (!m_elapsed.isValid()) {
                    m_elapsed.start();
                }
                m_lastPongMs = m_elapsed.elapsed();
            } else if (line.startsWith(QByteArrayLiteral("CHAT:"))) {
                const QString msg = QString::fromUtf8(line.mid(5));
                const QString roomId = m_clientRooms.value(socket, this->roomId());
                LOG_INFO(QStringLiteral("ControlServer: chat from %1 (room %2) - %3")
                             .arg(clientIp, roomId, msg));
                emit chatReceived(clientIp, roomId, msg);
            } else if (line.startsWith(QByteArrayLiteral("MEDIA:"))) {
                // Format: MEDIA:mic=0/1;cam=0/1
                bool micMuted = false;
                bool cameraEnabled = true;
                QString roomId = m_clientRooms.value(socket);

                const QList<QByteArray> parts = line.mid(6).split(';');
                for (const QByteArray &part : parts) {
                    if (part.startsWith(QByteArrayLiteral("mic="))) {
                        const QByteArray v = part.mid(4).trimmed();
                        micMuted = (v == "1");
                    } else if (part.startsWith(QByteArrayLiteral("cam="))) {
                        const QByteArray v = part.mid(4).trimmed();
                        cameraEnabled = (v != "0");
                    } else if (part.startsWith(QByteArrayLiteral("room="))) {
                        roomId = QString::fromUtf8(part.mid(5));
                    }
                }

                roomId = normalizedRoomId(roomId.isEmpty() ? m_clientRooms.value(socket) : roomId);
                if (!roomId.isEmpty() && (!m_clientRooms.contains(socket) || m_clientRooms.value(socket) != roomId)) {
                    m_clientRooms.insert(socket, roomId);
                    auto &list = m_roomClients[roomId];
                    if (!list.contains(socket)) {
                        list.append(socket);
                    }
                }

                emit mediaStateChanged(clientIp, roomId, micMuted, cameraEnabled);
                broadcastMediaState(clientIp, micMuted, cameraEnabled, roomId);
            } else if (line.startsWith(QByteArrayLiteral("SCREEN:"))) {
                // Format: SCREEN:on=0/1
                bool sharing = false;
                QString roomId = m_clientRooms.value(socket);
                const QList<QByteArray> parts = line.mid(7).split(';');
                for (const QByteArray &part : parts) {
                    if (part.startsWith(QByteArrayLiteral("on="))) {
                        const QByteArray v = part.mid(3).trimmed();
                        sharing = (v == "1");
                    } else if (part.startsWith(QByteArrayLiteral("room="))) {
                        roomId = QString::fromUtf8(part.mid(5));
                    }
                }

                roomId = normalizedRoomId(roomId.isEmpty() ? m_clientRooms.value(socket) : roomId);
                if (!roomId.isEmpty() && (!m_clientRooms.contains(socket) || m_clientRooms.value(socket) != roomId)) {
                    m_clientRooms.insert(socket, roomId);
                    auto &list = m_roomClients[roomId];
                    if (!list.contains(socket)) {
                        list.append(socket);
                    }
                }

                emit screenShareStateChanged(clientIp, roomId, sharing);
                broadcastScreenShareState(clientIp, sharing, roomId);
            }
        }
    }
}
