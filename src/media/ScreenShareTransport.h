#ifndef SCREENSHARETRANSPORT_H
#define SCREENSHARETRANSPORT_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QSet>

class QLabel;

// Lightweight screen sharing transport:
// - On host side: captures the primary screen periodically and sends JPEG
//   frames via UDP to all registered client IPs.
// - On client side: receives JPEG frames on a given UDP port and renders
//   them into a provided QLabel.
class ScreenShareTransport : public QObject
{
    Q_OBJECT

public:
    explicit ScreenShareTransport(QObject *parent = nullptr);
    ~ScreenShareTransport() override;

    // Host-side API
    void setDestinations(const QSet<QString> &ips);
    bool startSender(quint16 remotePort);
    void stopSender();
    bool isSending() const { return m_sending; }

    // Client-side API
    bool startReceiver(quint16 localPort);
    void stopReceiver();
    bool isReceiving() const { return m_receiving; }
    void setRenderLabel(QLabel *label);

private slots:
    void onSendTimer();
    void onReadyRead();

private:
    QUdpSocket *m_socket;
    QTimer *m_sendTimer;

    QSet<QString> m_destIps;
    quint16 m_remotePort;

    bool m_sending;
    bool m_receiving;

    QLabel *m_renderLabel;
};

#endif // SCREENSHARETRANSPORT_H

