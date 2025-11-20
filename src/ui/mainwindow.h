#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>
#include <QLabel>
#include <QPoint>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QImage>

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
class QGridLayout;
class QUdpSocket;
class QLabel;
class ScreenShareWidget;
class QCheckBox;
class QTimer;

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
    void onScreenShareFrameReceived(const QImage &image);
    void updateScreenSharePixmap();

    Ui::MainWindow *ui;
    QLabel *statusLabel;
    ControlServer *server;
    ControlClient *client;
    MediaEngine *media;
    AudioEngine *audio;
    AudioTransport *audioNet;
    MediaTransport *videoNet;
    ScreenShareTransport *screenShare;
    QLabel *screenShareOverlayLabel;
    ScreenShareWidget *screenShareWidget;

    // Host-side container that arranges multiple remote videos
    // in a simple grid layout depending on participant count.
    QWidget *remoteParticipantsContainer;
    QGridLayout *remoteParticipantsLayout;

    // Local self-view overlay: display name and media state icons.
    QLabel *localNameLabel;
    QLabel *localMicIconLabel;
    QLabel *localCameraIconLabel;

    // Guest-side remote host info bar (nickname + media state).
    QLabel *remoteHostNameLabel;
    QLabel *remoteHostMicIconLabel;
    QLabel *remoteHostCameraIconLabel;

    QWidget *controlBar;
    QToolButton *btnToggleSidePanel;
    QToolButton *btnCreateRoom;
    QToolButton *btnJoinRoom;
    QToolButton *btnLeaveRoom;
    QToolButton *btnMute;
    QToolButton *btnCamera;
    QToolButton *btnScreenShare;
    QTimer *controlBarHideTimer;
    QWidget *controlsContainer;
    QTimer *screenShareHideTimer;
    QCheckBox *screenFitCheckBox;

    bool isDraggingPreview;
    QPoint previewDragStartPos;
    QPoint previewStartPos;

      // Host-side multi-remote video receiving
      QUdpSocket *hostVideoRecvSocket;
      QHash<QString, QLabel *> hostVideoLabels;
      QHash<QString, QLabel *> hostVideoMicIconLabels;
      QHash<QString, QLabel *> hostVideoCameraIconLabels;
    // Host-side multi-remote audio receiving & mixing
    QUdpSocket *hostAudioRecvSocket;
      QSet<QString> activeClientIps;

    MeetingRole meetingRole;
    MeetingState meetingState;
      QString currentRemoteIp;
      bool audioTransportActive;
      bool videoTransportActive;
      bool audioMuted;
      bool cameraEnabled;
      int connectedClientCount;
      QStringList participantNames;
      QImage lastScreenShareFrame;
      QString activeSpeakerIp;

      // Guest 侧简单自动重连
      QTimer *guestReconnectTimer;
      int guestReconnectAttempts;
      bool guestManualLeave;

      // Layout helpers
      void rebuildRemoteParticipantGrid();
      QWidget *createParticipantVideoTile(const QString &displayName,
                                          QLabel **outVideoLabel,
                                          QLabel **outMicIconLabel,
                                          QLabel **outCameraIconLabel);
      void updateLocalMediaStateIcons();
  };

#endif // MAINWINDOW_H
