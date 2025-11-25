#ifndef CONTROLCLIENT_H
#define CONTROLCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QElapsedTimer>

class QHostAddress;
class QTimer;

class ControlClient : public QObject
{
    Q_OBJECT

public:
    explicit ControlClient(QObject *parent = nullptr);

    void connectToHost(const QString &ip, quint16 port = 5000);
    void disconnectFromHost();
    void sendChatMessage(const QString &message);
    void sendMediaState(bool micMuted, bool cameraEnabled);
    void sendScreenShareState(bool sharing);

signals:
    void joined();
    void errorOccurred(const QString &message);
    void disconnected();
    void chatReceived(const QString &message);
    void mediaStateUpdated(const QString &ip, bool micMuted, bool cameraEnabled);
    void screenShareStateUpdated(const QString &ip, bool sharing);

private slots:
    void onConnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError socketError);
    void onDisconnected();
    void onPingTimer();

private:
    QTcpSocket *m_socket;
    QByteArray m_buffer;
    bool m_joined;
    QTimer *m_pingTimer;
    QElapsedTimer m_elapsed;
    qint64 m_lastPongMs;
};

#endif // CONTROLCLIENT_H
