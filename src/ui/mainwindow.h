#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>
#include <QLabel>
#include <QPoint>
#include <QStringList>
#include <QHash>
#include <QSet>

#include "common/Logger.h"
#include "net/ControlServer.h"
#include "net/ControlClient.h"
#include "media/MediaEngine.h"
#include "audio/AudioEngine.h"
#include "audio/AudioTransport.h"
#include "media/MediaTransport.h"
#include "media/ScreenShareTransport.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QTimer;
class QToolButton;
class QWidget;
class QEvent;
class QResizeEvent;
class QHBoxLayout;
class QUdpSocket;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    enum class MeetingRole {
        None,
        Host,
        Guest
    };

    enum class MeetingState {
        Idle,
        WaitingPeer,
        Connecting,
        InMeeting
    };

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void on_btnCreateRoom_clicked();
    void on_btnJoinRoom_clicked();
    void on_btnLeaveRoom_clicked();
    void on_btnSendChat_clicked();

private:
    void initLayout();
    void initSidePanel();
    void initFloatingControls();
    void initPreviewWindow();
    void updateOverlayGeometry();
    void showControlBarTemporarily();
    void appendLogMessage(const QString &message);
    void resetMeetingState();
    void startClientMediaTransports();
    void updateControlsForMeetingState();
    void updateMeetingStatusLabel();
    void appendChatMessage(const QString &sender, const QString &message, bool isLocal);
    void refreshParticipantListView();
    void initHostVideoReceiver();
    void initHostAudioMixer();

    Ui::MainWindow *ui;
    QLabel *statusLabel;
    ControlServer *server;
    ControlClient *client;
    MediaEngine *media;
    AudioEngine *audio;
    AudioTransport *audioNet;
    MediaTransport *videoNet;
    ScreenShareTransport *screenShare;

    QWidget *controlBar;
    QToolButton *btnToggleSidePanel;
    QToolButton *btnCreateRoom;
    QToolButton *btnJoinRoom;
    QToolButton *btnLeaveRoom;
    QToolButton *btnMute;
    QToolButton *btnScreenShare;
    QTimer *controlBarHideTimer;
    QWidget *controlsContainer;

    bool isDraggingPreview;
    QPoint previewDragStartPos;
    QPoint previewStartPos;

    // Host-side multi-remote video receiving
    QUdpSocket *hostVideoRecvSocket;
    QHash<QString, QLabel *> hostVideoLabels;
    // Host-side multi-remote audio receiving & mixing
    QUdpSocket *hostAudioRecvSocket;
    QSet<QString> activeClientIps;

    MeetingRole meetingRole;
    MeetingState meetingState;
    QString currentRemoteIp;
    bool audioTransportActive;
    bool videoTransportActive;
    bool audioMuted;
    int connectedClientCount;
    QStringList participantNames;
};

#endif // MAINWINDOW_H
