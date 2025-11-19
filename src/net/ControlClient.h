#ifndef CONTROLCLIENT_H
#define CONTROLCLIENT_H

#include <QObject>
#include <QTcpSocket>

class QHostAddress;

class ControlClient : public QObject
{
    Q_OBJECT

public:
    explicit ControlClient(QObject *parent = nullptr);

    void connectToHost(const QString &ip, quint16 port = 5000);
    void disconnectFromHost();
    void sendChatMessage(const QString &message);

signals:
    void joined();
    void errorOccurred(const QString &message);
    void disconnected();
    void chatReceived(const QString &message);

private slots:
    void onConnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError socketError);
    void onDisconnected();

private:
    QTcpSocket *m_socket;
    QByteArray m_buffer;
    bool m_joined;
};

#endif // CONTROLCLIENT_H
