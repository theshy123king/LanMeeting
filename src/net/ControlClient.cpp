#include "ControlClient.h"

#include "common/Logger.h"

ControlClient::ControlClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_buffer()
    , m_joined(false)
{
    connect(m_socket, &QTcpSocket::connected,
            this, &ControlClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &ControlClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &ControlClient::onError);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &ControlClient::onDisconnected);
}

void ControlClient::connectToHost(const QString &ip, quint16 port)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState ||
        m_socket->state() == QAbstractSocket::ConnectingState) {
        LOG_WARN(QStringLiteral("ControlClient::connectToHost called while socket is already connected or connecting"));
        return;
    }

    m_buffer.clear();
    m_joined = false;

    LOG_INFO(QStringLiteral("ControlClient connecting to %1:%2").arg(ip).arg(port));
    m_socket->connectToHost(ip, port);
}

void ControlClient::disconnectFromHost()
{
    if (!m_socket) {
        return;
    }

    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        LOG_INFO(QStringLiteral("ControlClient: sending LEAVE and closing connection"));
        m_socket->write("LEAVE\n");
        m_socket->flush();
    } else {
        LOG_INFO(QStringLiteral("ControlClient: disconnectFromHost called while not connected (state=%1)")
                     .arg(m_socket->state()));
    }

    m_socket->disconnectFromHost();
}

void ControlClient::onConnected()
{
    LOG_INFO(QStringLiteral("ControlClient connected, sending JOIN"));
    m_socket->write("JOIN\n");
    m_socket->flush();
}

void ControlClient::onReadyRead()
{
    const QByteArray data = m_socket->readAll();
    if (data.isEmpty())
        return;

    m_buffer.append(data);
    LOG_INFO(QStringLiteral("ControlClient received buffer: %1")
                 .arg(QString::fromUtf8(m_buffer)));

    if (!m_joined && m_buffer.contains("OK")) {
        m_joined = true;
        LOG_INFO(QStringLiteral("ControlClient: join confirmed by server."));

        const int index = m_buffer.indexOf("OK");
        if (index >= 0) {
            m_buffer.remove(index, 2);
        }

        emit joined();
    }

    while (true) {
        const int newlineIndex = m_buffer.indexOf('\n');
        if (newlineIndex < 0)
            break;

        QByteArray line = m_buffer.left(newlineIndex);
        m_buffer.remove(0, newlineIndex + 1);
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QByteArrayLiteral("CHAT:"))) {
            const QString msg = QString::fromUtf8(line.mid(5));
            LOG_INFO(QStringLiteral("ControlClient: chat received - %1").arg(msg));
            emit chatReceived(msg);
        }
    }
}

void ControlClient::onError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    const QString message = m_socket->errorString();
    LOG_WARN(QStringLiteral("ControlClient: socket error - %1").arg(message));
    emit errorOccurred(message);
}

void ControlClient::onDisconnected()
{
    LOG_INFO(QStringLiteral("ControlClient: disconnected from host"));
    m_joined = false;
    m_buffer.clear();
    emit disconnected();
}

void ControlClient::sendChatMessage(const QString &message)
{
    if (!m_socket) {
        return;
    }

    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        LOG_WARN(QStringLiteral("ControlClient::sendChatMessage called while socket is not connected"));
        return;
    }

    const QByteArray data = QByteArrayLiteral("CHAT:") + message.toUtf8() + '\n';
    const qint64 written = m_socket->write(data);
    if (written < 0) {
        LOG_WARN(QStringLiteral("ControlClient: failed to send chat message - %1").arg(m_socket->errorString()));
    }
}
