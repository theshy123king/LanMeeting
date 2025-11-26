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
#include <QThread>
#include <QRect>
#include <QVector>

// Internal assembly state for a single screen-share frame on the receiver side.
struct ScreenShareFrameAssembly
{
    quint16 totalPackets = 0;
    QHash<quint16, QByteArray> packets;
    qint64 firstSeenMs = 0;
};

class QLabel;
class ScreenShareSenderWorker;
class ScreenShareCaptureWorker;

// Lightweight screen sharing transport:
// - On host side: captures the primary screen periodically and sends JPEG
//   frames via UDP to all registered client IPs.
// - On client side: receives JPEG frames on a given UDP port and renders
//   them into a provided QLabel.
class ScreenShareTransport : public QObject
{
    Q_OBJECT

public:
    struct CaptureSettings
    {
        int maxWidth = 0;
        int maxHeight = 0;
        int jpegQuality = 0;
        QRect captureRect;
    };

    explicit ScreenShareTransport(QObject *parent = nullptr);
    ~ScreenShareTransport() override;

    // Host-side API
    void setDestinations(const QSet<QString> &ips);
    // Capture the entire primary screen (default behaviour).
    void setCaptureFullScreen();
    // Capture a specific region (in global screen coordinates).
    void setCaptureRegion(const QRect &rect);
    bool startSender(quint16 remotePort);
    void stopSender();
    bool isSending() const { return m_sending; }

    // Client-side API
    bool startReceiver(quint16 localPort);
    void stopReceiver();
    bool isReceiving() const { return m_receiving; }
    void setRenderLabel(QLabel *label);
    // Control how the incoming frames are scaled into the
    // render label on the client side.
    void setRenderFitToWindow(bool fit);
    void logDiagnostics() const;

signals:
    // Emitted on the client side whenever a new screen frame
    // has been decoded while in receiving mode.
    void screenFrameReceived(const QImage &image);

    // Emitted on the host side after the GUI thread has captured
    // and downscaled a screen image and encoded it to JPEG. The
    // heavy UDP fragmentation and sending work is performed in a
    // dedicated background thread to avoid blocking audio/video.
    void encodedFrameReady(const QByteArray &encodedFrame,
                           const QSet<QString> &destIps,
                           quint16 remotePort,
                           quint32 frameId);
    void requestCapture();
    void statusTextChanged(const QString &text);

private slots:
    void onSendTimer();
    void onReadyRead();
    void onBandwidthSample(qint64 bytesPerSec);
    void applyQualityPreset();
    void onFrameReady(const QByteArray &jpeg, int width, int height, double diffScore);

private:
    void updateStatusText(const QString &tier, const QString &reason = QString());

    QUdpSocket *m_socket;
    QTimer *m_sendTimer;

    // Dedicated worker thread for sending screen-share UDP packets.
    QThread m_sendThread;
    ScreenShareSenderWorker *m_senderWorker;
    // Dedicated worker thread for capture/scale/encode.
    QThread *m_captureThread = nullptr;
    ScreenShareCaptureWorker *m_captureWorker = nullptr;

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

    // Optional capture region in screen coordinates; if null,
    // the full screen is captured.
    QRect m_captureRect;
    // Client-side scaling mode for m_renderLabel rendering.
    bool m_renderFitToWindow = true;

    int m_qualityLevel = 2; // 0=low,1=medium,2=high
    qint64 m_lastAdjustMs = 0;
    QVector<qint64> m_sendHistory;
    qint64 m_lastBandwidthSample = 0;
    int m_currentTargetFps = 0;
    int m_currentMaxWidth = 0;
    int m_currentMaxHeight = 0;
    int m_currentJpegQuality = 0;
    int m_baseMaxWidth = 0;
    int m_baseMaxHeight = 0;
    int m_baseJpegQuality = 0;
    qint64 m_lastFrameSentMs = 0;
    double m_lastDiffScore = 1.0;
    QString m_currentTierLabel;
    QString m_statusText;
    int m_effectiveFps = 0;
    CaptureSettings m_captureSettings;
};

#endif // SCREENSHARETRANSPORT_H
