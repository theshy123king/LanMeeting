#ifndef CONTROLSERVER_H
#define CONTROLSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class ControlServer : public QObject
{
    Q_OBJECT

public:
    explicit ControlServer(QObject *parent = nullptr);

    bool startServer(quint16 port = 5000);
    void stopServer();

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QTcpServer *m_server;
    QList<QTcpSocket *> m_clients;
};

#endif // CONTROLSERVER_H

