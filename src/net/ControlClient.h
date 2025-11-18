#ifndef CONTROLCLIENT_H
#define CONTROLCLIENT_H

#include <QObject>
#include <QTcpSocket>

class ControlClient : public QObject
{
    Q_OBJECT

public:
    explicit ControlClient(QObject *parent = nullptr);

    void connectToHost(const QString &ip, quint16 port = 5000);

signals:
    void joined();
    void errorOccurred(const QString &message);

private slots:
    void onConnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError socketError);

private:
    QTcpSocket *m_socket;
    QByteArray m_buffer;
};

#endif // CONTROLCLIENT_H
