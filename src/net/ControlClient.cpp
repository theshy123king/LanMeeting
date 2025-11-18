#include "ControlClient.h"

#include "common/Logger.h"

ControlClient::ControlClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected,
            this, &ControlClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &ControlClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &ControlClient::onError);
}

void ControlClient::connectToHost(const QString &ip, quint16 port)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState ||
        m_socket->state() == QAbstractSocket::ConnectingState) {
        return;
    }

    m_socket->connectToHost(ip, port);
}

void ControlClient::onConnected()
{
    LOG_INFO(QStringLiteral("ControlClient connected, sending JOIN"));
    m_socket->write("JOIN");
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

    if (m_buffer.contains("OK")) {
        LOG_INFO(QStringLiteral("ControlClient: join confirmed by server."));
        m_buffer.clear();
        emit joined();
    }
}

void ControlClient::onError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    const QString message = m_socket->errorString();
    LOG_WARN(QStringLiteral("ControlClient: socket error - %1").arg(message));
    emit errorOccurred(message);
}
