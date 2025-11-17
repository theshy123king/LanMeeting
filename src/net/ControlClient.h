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

private slots:
    void onConnected();
    void onReadyRead();

private:
    QTcpSocket *m_socket;
};

#endif // CONTROLCLIENT_H

