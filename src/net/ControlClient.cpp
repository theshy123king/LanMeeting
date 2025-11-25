#include "ControlClient.h"

#include "common/Logger.h"
#include <QTimer>

ControlClient::ControlClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_buffer()
    , m_joined(false)
    , m_pingTimer(new QTimer(this))
    , m_elapsed()
    , m_lastPongMs(0)
{
    connect(m_socket, &QTcpSocket::connected,
            this, &ControlClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &ControlClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &ControlClient::onError);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &ControlClient::onDisconnected);

    m_pingTimer->setInterval(5000);
    connect(m_pingTimer, &QTimer::timeout, this, &ControlClient::onPingTimer);
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
    m_lastPongMs = 0;
    m_elapsed.invalidate();

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
    m_elapsed.start();
    m_lastPongMs = m_elapsed.elapsed();
    m_pingTimer->start();
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
        } else if (line == QByteArrayLiteral("PONG")) {
            if (m_elapsed.isValid()) {
                const qint64 now = m_elapsed.elapsed();
                const qint64 age = (m_lastPongMs > 0) ? (now - m_lastPongMs) : 0;
                m_lastPongMs = now;
                LOG_INFO(QStringLiteral("ControlClient heartbeat: pong age=%1ms health=ok")
                             .arg(age));
            }
        } else if (line.startsWith(QByteArrayLiteral("STATE:"))) {
            // Examples:
            // STATE:MEDIA;ip=1.2.3.4;mic=1;cam=0
            // STATE:SCREEN;ip=1.2.3.4;on=1
            const QByteArray payload = line.mid(6);
            const QList<QByteArray> fields = payload.split(';');
            if (fields.isEmpty())
                continue;

            const QByteArray kind = fields.at(0);
            QString ip;
            bool micMuted = false;
            bool cameraEnabled = true;
            bool sharing = false;

            for (int i = 1; i < fields.size(); ++i) {
                const QByteArray part = fields.at(i);
                if (part.startsWith(QByteArrayLiteral("ip="))) {
                    ip = QString::fromUtf8(part.mid(3));
                } else if (part.startsWith(QByteArrayLiteral("mic="))) {
                    const QByteArray v = part.mid(4).trimmed();
                    micMuted = (v == "1");
                } else if (part.startsWith(QByteArrayLiteral("cam="))) {
                    const QByteArray v = part.mid(4).trimmed();
                    cameraEnabled = (v != "0");
                } else if (part.startsWith(QByteArrayLiteral("on="))) {
                    const QByteArray v = part.mid(3).trimmed();
                    sharing = (v == "1");
                }
            }

            if (kind == QByteArrayLiteral("MEDIA")) {
                if (!ip.isEmpty()) {
                    emit mediaStateUpdated(ip, micMuted, cameraEnabled);
                }
            } else if (kind == QByteArrayLiteral("SCREEN")) {
                if (!ip.isEmpty()) {
                    emit screenShareStateUpdated(ip, sharing);
                }
            }
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
    m_pingTimer->stop();
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

void ControlClient::sendMediaState(bool micMuted, bool cameraEnabled)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const QByteArray line = QByteArrayLiteral("MEDIA:mic=")
                            + (micMuted ? "1" : "0")
                            + QByteArrayLiteral(";cam=")
                            + (cameraEnabled ? "1" : "0")
                            + '\n';
    const qint64 written = m_socket->write(line);
    if (written < 0) {
        LOG_WARN(QStringLiteral("ControlClient: failed to send MEDIA state - %1").arg(m_socket->errorString()));
    }
}

void ControlClient::sendScreenShareState(bool sharing)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const QByteArray line = QByteArrayLiteral("SCREEN:on=")
                            + (sharing ? "1" : "0")
                            + '\n';
    const qint64 written = m_socket->write(line);
    if (written < 0) {
        LOG_WARN(QStringLiteral("ControlClient: failed to send SCREEN state - %1").arg(m_socket->errorString()));
    }
}

void ControlClient::onPingTimer()
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    if (m_elapsed.isValid()) {
        const qint64 now = m_elapsed.elapsed();
        if (m_lastPongMs > 0 && now - m_lastPongMs > 15000) {
            LOG_WARN(QStringLiteral("ControlClient: heartbeat timeout age=%1ms, closing socket")
                         .arg(now - m_lastPongMs));
            m_socket->disconnectFromHost();
            return;
        }
    }

    m_socket->write("PING\n");
    m_socket->flush();
}
