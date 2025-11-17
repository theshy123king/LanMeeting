#include "ControlClient.h"

#include <QDebug>

ControlClient::ControlClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected,
            this, &ControlClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &ControlClient::onReadyRead);
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
    qDebug() << "ControlClient connected, sending JOIN";
    m_socket->write("JOIN");
    m_socket->flush();
}

void ControlClient::onReadyRead()
{
    const QByteArray data = m_socket->readAll().trimmed();
    qDebug() << "ControlClient received:" << data;

    if (data == "OK") {
        qDebug() << "ControlClient: join confirmed by server.";
    }
}

