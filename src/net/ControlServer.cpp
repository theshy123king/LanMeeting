#include "ControlServer.h"

#include <QDebug>

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
        return true;
    }

    const bool ok = m_server->listen(QHostAddress::Any, port);
    if (!ok) {
        qDebug() << "ControlServer failed to listen on port" << port
                 << ":" << m_server->errorString();
    } else {
        qDebug() << "ControlServer listening on port" << port;
    }
    return ok;
}

void ControlServer::stopServer()
{
    for (QTcpSocket *socket : std::as_const(m_clients)) {
        if (socket) {
            socket->disconnectFromHost();
            socket->deleteLater();
        }
    }
    m_clients.clear();

    if (m_server->isListening()) {
        m_server->close();
    }
}

void ControlServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            continue;

        qDebug() << "ControlServer: client connected from"
                 << socket->peerAddress().toString()
                 << ":" << socket->peerPort();

        m_clients.append(socket);
        connect(socket, &QTcpSocket::readyRead,
                this, &ControlServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void ControlServer::onReadyRead()
{
    for (QTcpSocket *socket : std::as_const(m_clients)) {
        if (!socket || socket->bytesAvailable() <= 0)
            continue;

        const QByteArray data = socket->readAll().trimmed();
        qDebug() << "ControlServer received:" << data;

        if (data == "JOIN") {
            socket->write("OK");
            socket->flush();
        }
    }
}

