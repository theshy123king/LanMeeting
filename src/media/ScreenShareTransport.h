#ifndef SCREENSHARETRANSPORT_H
#define SCREENSHARETRANSPORT_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QSet>
#include <QImage>
#include <QtGlobal>
#include <QByteArray>
#include <QHash>

// Internal assembly state for a single screen-share frame on the receiver side.
struct ScreenShareFrameAssembly
{
    quint16 totalPackets = 0;
    QHash<quint16, QByteArray> packets;
    qint64 firstSeenMs = 0;
};

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

signals:
    // Emitted on the client side whenever a new screen frame
    // has been decoded while in receiving mode.
    void screenFrameReceived(const QImage &image);

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

    // Sender-side incremental frame id.
    quint32 m_nextFrameId = 0;

    // Receiver-side in-flight frame assemblies.
    QHash<quint32, ScreenShareFrameAssembly> m_pendingFrames;
    qint64 m_lastCleanupMs = 0;
};

#endif // SCREENSHARETRANSPORT_H
