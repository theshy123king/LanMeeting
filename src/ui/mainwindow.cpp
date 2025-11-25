#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QApplication>
#include <QApplication>
#include <QInputDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QToolButton>
#include <QTimer>
#include <QMouseEvent>
#include <QEvent>
#include <QResizeEvent>
#include <QCheckBox>
#include <QSizePolicy>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QUdpSocket>
#include <QHostAddress>
#include <QStyle>
#include <QVector>

#include "common/Config.h"
#include "ScreenShareWidget.h"
#include "ChatMessageWidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , statusLabel(nullptr)
    , server(nullptr)
    , client(nullptr)
    , media(nullptr)
    , audio(nullptr)
    , audioNet(nullptr)
    , videoNet(nullptr)
    , screenShare(nullptr)
    , screenShareOverlayLabel(nullptr)
    , screenShareWidget(nullptr)
    , remoteParticipantsContainer(nullptr)
    , remoteParticipantsLayout(nullptr)
    , localNameLabel(nullptr)
    , localMicIconLabel(nullptr)
    , localCameraIconLabel(nullptr)
    , remoteHostNameLabel(nullptr)
    , remoteHostMicIconLabel(nullptr)
    , remoteHostCameraIconLabel(nullptr)
    , controlBar(nullptr)
    , btnToggleSidePanel(nullptr)
    , btnCreateRoom(nullptr)
    , btnJoinRoom(nullptr)
    , btnLeaveRoom(nullptr)
    , btnMute(nullptr)
    , btnCamera(nullptr)
    , btnScreenShare(nullptr)
    , controlBarHideTimer(nullptr)
    , controlsContainer(nullptr)
    , screenShareHideTimer(nullptr)
    , screenFitCheckBox(nullptr)
    , diagTimer(nullptr)
    , hostVideoRecvSocket(nullptr)
    , hostAudioRecvSocket(nullptr)
    , isDraggingPreview(false)
    , previewDragStartPos()
    , previewStartPos()
    , meetingRole(MeetingRole::None)
    , meetingState(MeetingState::Idle)
    , currentRemoteIp()
    , audioTransportActive(false)
    , videoTransportActive(false)
    , audioMuted(false)
    , cameraEnabled(true)
    , connectedClientCount(0)
    , guestReconnectTimer(nullptr)
    , guestReconnectAttempts(0)
    , guestManualLeave(false)
{
    ui->setupUi(this);

    initLayout();
    initSidePanel();
    initPreviewWindow();
    initFloatingControls();

    statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(statusLabel);
    updateMeetingStatusLabel();

    appendLogMessage(QStringLiteral("应用启动，主窗口初始化完成"));

    // 本地视频预览
    media = new MediaEngine(this);
    QWidget *preview = media->createPreviewWidget();

    if (ui->videoContainer && preview) {
        auto *layout = qobject_cast<QVBoxLayout *>(ui->videoContainer->layout());
        if (!layout) {
            layout = new QVBoxLayout(ui->videoContainer);
            ui->videoContainer->setLayout(layout);
        }
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(preview);

        // Local self-view overlay: show display name and
        // basic mic/camera state icons under the preview.
        QWidget *infoBar = new QWidget(ui->videoContainer);
        auto *infoLayout = new QHBoxLayout(infoBar);
        infoLayout->setContentsMargins(6, 2, 6, 2);
        infoLayout->setSpacing(4);

        localNameLabel = new QLabel(QStringLiteral("Me"), infoBar);
        localNameLabel->setStyleSheet(QStringLiteral("color: white;"));
        infoLayout->addWidget(localNameLabel);

        infoLayout->addStretch();

        localMicIconLabel = new QLabel(infoBar);
        localCameraIconLabel = new QLabel(infoBar);
        infoLayout->addWidget(localMicIconLabel);
        infoLayout->addWidget(localCameraIconLabel);

        layout->addWidget(infoBar);
    }

    cameraEnabled = media->startCamera();
    if (!cameraEnabled) {
        QMessageBox::warning(this,
                             QStringLiteral("Video error"),
                             QStringLiteral("Unable to start camera. Please check device permissions or whether it is in use."));
    }
    updateLocalMediaStateIcons();

    // 本地音频采集和播放
    audio = new AudioEngine(this);
    if (!audio->startCapture()) {
        QMessageBox::warning(this,
                             QStringLiteral("Audio error"),
                             QStringLiteral("Unable to start microphone. Please check device permissions or whether it is in use."));
    }
    if (!audio->startPlayback()) {
        QMessageBox::warning(this,
                             QStringLiteral("Audio error"),
                             QStringLiteral("Unable to start speaker playback. Please check audio device configuration."));
    }

    audioNet = new AudioTransport(audio, this);

    // 远端摄像头视频显示区域
    videoNet = new MediaTransport(media, this);
    if (QWidget *remoteContainer = ui->remoteVideoContainer) {
        QWidget *remoteView = videoNet->getRemoteVideoWidget();
        if (remoteView) {
            auto *remoteLayout = qobject_cast<QVBoxLayout *>(remoteContainer->layout());
            if (!remoteLayout) {
                remoteLayout = new QVBoxLayout(remoteContainer);
                remoteContainer->setLayout(remoteLayout);
            }
            remoteLayout->setContentsMargins(0, 0, 0, 0);
            remoteLayout->setSpacing(0);
            remoteView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            remoteLayout->addWidget(remoteView);

            // 屏幕共享显示控件：与摄像头视频控件共用容器，但互斥显示。
            screenShareWidget = new ScreenShareWidget(remoteContainer);
            screenShareWidget->setObjectName(QStringLiteral("screenShareWidget"));
            screenShareWidget->setSizePolicy(QSizePolicy::Expanding,
                                             QSizePolicy::Expanding);
            screenShareWidget->hide();
            remoteLayout->addWidget(screenShareWidget);

            // Host-side multi-participant grid container lives under the
            // main remote video area and is shown when there are multiple
            // remote video streams to arrange.
            remoteParticipantsContainer = new QWidget(remoteContainer);
            remoteParticipantsContainer->setObjectName(QStringLiteral("remoteParticipantsContainer"));
            remoteParticipantsContainer->setSizePolicy(QSizePolicy::Expanding,
                                                       QSizePolicy::Expanding);
            remoteParticipantsLayout = new QGridLayout(remoteParticipantsContainer);
            remoteParticipantsLayout->setContentsMargins(4, 4, 4, 4);
            remoteParticipantsLayout->setSpacing(4);
            remoteParticipantsContainer->hide();
            remoteLayout->addWidget(remoteParticipantsContainer);
        }
    }

    // 屏幕共享传输（主持人发送 / 客户端接收）
    screenShare = new ScreenShareTransport(this);
    if (screenFitCheckBox) {
        screenShare->setRenderFitToWindow(screenFitCheckBox->isChecked());
    }
    connect(screenShare,
            &ScreenShareTransport::screenFrameReceived,
            this,
            &MainWindow::onScreenShareFrameReceived);

    // Guest 侧音视频 / 屏幕共享超时与自动重连相关定时器
    guestReconnectTimer = new QTimer(this);
    guestReconnectTimer->setInterval(2000);
    guestReconnectTimer->setSingleShot(false);

    diagTimer = new QTimer(this);
    diagTimer->setInterval(12000);
    connect(diagTimer, &QTimer::timeout, this, [this]() {
        if (audioNet) {
            audioNet->logDiagnostics();
        }
        if (videoNet) {
            videoNet->logDiagnostics();
        }
        if (screenShare) {
            screenShare->logDiagnostics();
        }
        if (client) {
            LOG_INFO(QStringLiteral("ControlClient diag tick (heartbeat lastPong tracking active)"));
        }
    });
    diagTimer->start();

    updateOverlayGeometry();
    updateControlsForMeetingState();
}

MainWindow::~MainWindow()
{
    resetMeetingState();

    if (server) {
        server->stopServer();
    }

    delete server;
    delete client;
    delete videoNet;
    delete audioNet;
    delete audio;
    delete media;
    delete ui;
}

void MainWindow::initLayout()
{
    if (ui->mainLayout) {
        ui->mainLayout->setContentsMargins(0, 0, 0, 0);
        ui->mainLayout->setSpacing(0);
        ui->mainLayout->setStretch(0, 1); // 视频区域
        ui->mainLayout->setStretch(1, 0); // 侧边栏
    }

    if (ui->videoAreaLayout) {
        ui->videoAreaLayout->setContentsMargins(0, 0, 0, 0);
        ui->videoAreaLayout->setSpacing(0);
    }

    if (ui->centralwidget) {
        ui->centralwidget->setMouseTracking(true);
    }
    if (ui->videoArea) {
        ui->videoArea->setMouseTracking(true);
        ui->videoArea->installEventFilter(this);
    }
}

  void MainWindow::initSidePanel()
  {
    if (ui->sidePanel) {
        ui->sidePanel->setVisible(false);
    }

    if (ui->sideTabWidget) {
        const int chatIndex = ui->sideTabWidget->indexOf(ui->chatTab);
        if (chatIndex >= 0) {
            ui->sideTabWidget->setTabText(chatIndex, QStringLiteral("Chat"));
        }
        const int participantsIndex = ui->sideTabWidget->indexOf(ui->participantsTab);
        if (participantsIndex >= 0) {
            ui->sideTabWidget->setTabText(participantsIndex, QStringLiteral("Participants"));
        }
        const int logIndex = ui->sideTabWidget->indexOf(ui->logTab);
        if (logIndex >= 0) {
            ui->sideTabWidget->setTabText(logIndex, QStringLiteral("Log"));
        }
        const int settingsIndex = ui->sideTabWidget->indexOf(ui->settingsTab);
        if (settingsIndex >= 0) {
            ui->sideTabWidget->setTabText(settingsIndex, QStringLiteral("Settings"));
        }
    }

    if (ui->labelSettings) {
        ui->labelSettings->setText(QStringLiteral("General"));
    }
    if (ui->chatLineEdit) {
        ui->chatLineEdit->setPlaceholderText(QStringLiteral("Type a message..."));
        connect(ui->chatLineEdit,
                &QLineEdit::returnPressed,
                this,
                &MainWindow::on_btnSendChat_clicked);
    }
    if (ui->btnSendChat) {
        ui->btnSendChat->setText(QStringLiteral("Send"));
    }
      if (ui->labelParticipants) {
          ui->labelParticipants->setText(QStringLiteral("Participants"));
      }

    if (ui->chatList) {
        ui->chatList->setFrameShape(QFrame::NoFrame);
        ui->chatList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->chatList->setSelectionMode(QAbstractItemView::NoSelection);
        ui->chatList->setFocusPolicy(Qt::NoFocus);
        ui->chatList->setResizeMode(QListView::Adjust);
        ui->chatList->setUniformItemSizes(false);
        ui->chatList->setSpacing(2);
    }

    if (ui->chkHideSelfView) {
        ui->chkHideSelfView->setText(QStringLiteral("Hide self view"));
        ui->chkHideSelfView->setChecked(false);
        connect(ui->chkHideSelfView, &QCheckBox::toggled, this, [this](bool checked) {
            if (ui->videoContainer) {
                ui->videoContainer->setVisible(!checked);
                updateOverlayGeometry();
            }
        });
    }

      if (ui->chkMuteOnJoin) {
          ui->chkMuteOnJoin->setText(QStringLiteral("Mute on join"));
      }

      // Screen tab: add a simple scaling mode toggle for screen sharing.
      if (ui->screenShareLabel) {
          QWidget *parent = ui->screenShareLabel->parentWidget();
          if (parent && !screenFitCheckBox) {
              if (auto *layout = qobject_cast<QVBoxLayout *>(parent->layout())) {
                  screenFitCheckBox = new QCheckBox(QStringLiteral("Fit to window"), parent);
                  screenFitCheckBox->setChecked(true);
                  layout->addWidget(screenFitCheckBox);
                  connect(screenFitCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
                      if (screenShare) {
                          screenShare->setRenderFitToWindow(checked);
                      }
                  });
              }
          }
      }
  }

void MainWindow::initPreviewWindow()
{
    if (!ui->videoArea || !ui->videoContainer) {
        return;
    }

    ui->videoContainer->setMinimumSize(200, 150);
    ui->videoContainer->setMaximumSize(400, 300);
      ui->videoContainer->setStyleSheet(QStringLiteral("background-color: black; border-radius: 4px;"));
      ui->videoContainer->installEventFilter(this);
  }

void MainWindow::updateLocalMediaStateIcons()
{
    QStyle *s = style();
    if (localMicIconLabel && s) {
        const QStyle::StandardPixmap micPixmap =
            audioMuted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume;
        localMicIconLabel->setPixmap(s->standardIcon(micPixmap).pixmap(16, 16));
        localMicIconLabel->setToolTip(audioMuted ? QStringLiteral("Microphone muted")
                                                 : QStringLiteral("Microphone on"));
    }
    if (localCameraIconLabel && s) {
        const QStyle::StandardPixmap camPixmap =
            cameraEnabled ? QStyle::SP_DesktopIcon : QStyle::SP_DialogCloseButton;
        localCameraIconLabel->setPixmap(s->standardIcon(camPixmap).pixmap(16, 16));
        localCameraIconLabel->setToolTip(cameraEnabled ? QStringLiteral("Camera on")
                                                       : QStringLiteral("Camera off"));
    }

}

void MainWindow::initFloatingControls()
{
    if (!ui->videoArea) {
        return;
    }

    controlBar = new QWidget(ui->videoArea);
    controlBar->setObjectName(QStringLiteral("floatingControlBar"));
    controlBar->setAutoFillBackground(true);
    controlBar->setStyleSheet(
        "#floatingControlBar { background-color: rgba(0, 0, 0, 160); border-radius: 8px; }"
        "#floatingControlBar QToolButton { color: white; padding: 4px 8px; }");

    auto *layout = new QHBoxLayout(controlBar);
    layout->setContentsMargins(12, 4, 12, 4);
    layout->setSpacing(6);

    // Main control button: toggle other controls
    auto *btnMenu = new QToolButton(controlBar);
    btnMenu->setText(QStringLiteral("Controls"));
    btnMenu->setCheckable(true);
    layout->addWidget(btnMenu);

    controlsContainer = new QWidget(controlBar);
    auto *controlsLayout = new QHBoxLayout(controlsContainer);
    controlsLayout->setContentsMargins(8, 0, 0, 0);
    controlsLayout->setSpacing(6);

    btnCreateRoom = new QToolButton(controlsContainer);
    btnCreateRoom->setText(QStringLiteral("Host"));
    controlsLayout->addWidget(btnCreateRoom);
    connect(btnCreateRoom, &QAbstractButton::clicked, this, &MainWindow::on_btnCreateRoom_clicked);

    btnJoinRoom = new QToolButton(controlsContainer);
    btnJoinRoom->setText(QStringLiteral("Join"));
    controlsLayout->addWidget(btnJoinRoom);
    connect(btnJoinRoom, &QAbstractButton::clicked, this, &MainWindow::on_btnJoinRoom_clicked);

      btnLeaveRoom = new QToolButton(controlsContainer);
      btnLeaveRoom->setText(QStringLiteral("Leave"));
      controlsLayout->addWidget(btnLeaveRoom);
      connect(btnLeaveRoom, &QAbstractButton::clicked, this, &MainWindow::on_btnLeaveRoom_clicked);

      btnMute = new QToolButton(controlsContainer);
      btnMute->setText(QStringLiteral("Mute"));
      btnMute->setCheckable(true);
      controlsLayout->addWidget(btnMute);
      connect(btnMute, &QAbstractButton::toggled, this, [this](bool checked) {
          audioMuted = checked;
          if (audioNet) {
              audioNet->setMuted(audioMuted);
          }
          appendLogMessage(checked ? QStringLiteral("Mute enabled (local audio will not be sent)")
                                   : QStringLiteral("Mute disabled, local audio will be sent"));
          updateLocalMediaStateIcons();
          broadcastLocalMediaState();
      });

      btnCamera = new QToolButton(controlsContainer);
      btnCamera->setText(QStringLiteral("Camera"));
      btnCamera->setCheckable(true);
      controlsLayout->addWidget(btnCamera);
      connect(btnCamera, &QAbstractButton::toggled, this, [this](bool checked) {
          if (!media) {
              return;
          }
          if (checked) {
              media->stopCamera();
              cameraEnabled = false;
              appendLogMessage(QStringLiteral("Camera disabled; local video will not be sent"));
          } else {
              cameraEnabled = media->startCamera();
              if (!cameraEnabled) {
                  QMessageBox::warning(this,
                                       QStringLiteral("Video error"),
                                       QStringLiteral("Unable to start camera. Please check device permissions or whether it is in use."));
              }
          }
          updateLocalMediaStateIcons();
          broadcastLocalMediaState();
      });

      btnScreenShare = new QToolButton(controlsContainer);
    btnScreenShare->setText(QStringLiteral("Share Screen"));
    btnScreenShare->setCheckable(true);
    controlsLayout->addWidget(btnScreenShare);
    connect(btnScreenShare, &QAbstractButton::toggled, this, [this](bool checked) {
        if (!screenShare) {
            return;
        }
        if (meetingRole != MeetingRole::Host) {
            btnScreenShare->setChecked(false);
            QMessageBox::information(this,
                                     QStringLiteral("Screen sharing"),
                                     QStringLiteral("Only the host can start screen sharing."));
            return;
        }

        if (checked) {
            screenShare->setDestinations(activeClientIps);
            if (!screenShare->startSender(Config::SCREEN_PORT_RECV)) {
                QMessageBox::warning(this,
                                     QStringLiteral("Screen sharing"),
                                     QStringLiteral("Unable to start screen sharing. The UDP port may be in use."));
                btnScreenShare->setChecked(false);
                return;
            }
            statusBar()->showMessage(QStringLiteral("Screen sharing is ON"), 3000);
            appendLogMessage(QStringLiteral("主持人开启屏幕共享"));
        } else {
            screenShare->stopSender();
            statusBar()->showMessage(QStringLiteral("Screen sharing is OFF"), 3000);
            appendLogMessage(QStringLiteral("主持人关闭屏幕共享"));
        }
    });

    btnToggleSidePanel = new QToolButton(controlsContainer);
    btnToggleSidePanel->setText(QStringLiteral("Side panel"));
    btnToggleSidePanel->setCheckable(true);
    controlsLayout->addWidget(btnToggleSidePanel);
    connect(btnToggleSidePanel, &QAbstractButton::toggled, this, [this](bool checked) {
        if (ui->sidePanel) {
            ui->sidePanel->setVisible(checked);
        }
        updateOverlayGeometry();
    });

    controlsContainer->setVisible(false);
    layout->addWidget(controlsContainer);
    layout->addStretch();

    connect(btnMenu, &QAbstractButton::toggled, this, [this](bool checked) {
        Q_UNUSED(checked);
        if (controlsContainer) {
            controlsContainer->setVisible(checked);
        }
        showControlBarTemporarily();
        updateOverlayGeometry();
    });

    controlBar->hide();
    controlBar->installEventFilter(this);

    controlBarHideTimer = new QTimer(this);
      controlBarHideTimer->setInterval(2000);
      controlBarHideTimer->setSingleShot(true);
      connect(controlBarHideTimer, &QTimer::timeout, this, [this]() {
          if (controlBar) {
              controlBar->hide();
          }
      });
}

void MainWindow::updateOverlayGeometry()
{
    if (!ui->videoArea) {
        return;
    }

    const int margin = 12;
    const int areaWidth = ui->videoArea->width();
    const int areaHeight = ui->videoArea->height();

    if (controlBar) {
        const QSize hint = controlBar->sizeHint();
        int width = hint.width();
        int height = hint.height();
        if (width > areaWidth - 2 * margin) {
            width = areaWidth - 2 * margin;
        }
        if (width < 0) {
            width = 0;
        }
        controlBar->resize(width, height);
        int x = (areaWidth - width) / 2;
        int y = areaHeight - height - margin;
        if (y < margin) {
            y = margin;
        }
        controlBar->move(x, y);
        controlBar->raise();
    }

    if (ui->videoContainer && ui->videoContainer->isVisible()) {
        QSize currentSize = ui->videoContainer->size();
        int width = currentSize.width();
        int height = currentSize.height();
        if (width == 0 || height == 0) {
            width = areaWidth / 4;
            height = areaHeight / 4;
            if (width < 160) {
                width = 160;
            }
            if (height < 120) {
                height = 120;
            }
            ui->videoContainer->resize(width, height);
        }

        int overlap = (controlBar && controlBar->isVisible()) ? (controlBar->height() + margin) : 0;
        int x = areaWidth - width - margin;
        int y = areaHeight - height - margin - overlap;
        if (x < margin) {
            x = margin;
        }
        if (y < margin) {
            y = margin;
        }
        ui->videoContainer->move(x, y);
        ui->videoContainer->raise();
    }
}

void MainWindow::showControlBarTemporarily()
{
    if (!controlBar) {
        return;
    }
    controlBar->show();
    controlBar->raise();
    if (controlBarHideTimer) {
        controlBarHideTimer->start();
    }
}

void MainWindow::onScreenShareFrameReceived(const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    // 只有参会者端才显示远端屏幕共享大屏，主持人本地不渲染自己的共享画面，避免递归采集。
    if (meetingRole != MeetingRole::Guest) {
        return;
    }

    lastScreenShareFrame = image;

    // 屏幕共享显示时隐藏摄像头视频控件，保证两者互斥显示、不会叠加绘制。
    if (videoNet) {
        if (QWidget *remoteWidget = videoNet->getRemoteVideoWidget()) {
            remoteWidget->setVisible(false);
        }
    }

    if (!screenShareOverlayLabel) {
        if (!ui->remoteVideoContainer) {
            return;
        }
        auto *layout = qobject_cast<QVBoxLayout *>(ui->remoteVideoContainer->layout());
        if (!layout) {
            layout = new QVBoxLayout(ui->remoteVideoContainer);
            ui->remoteVideoContainer->setLayout(layout);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
        }

        screenShareOverlayLabel = new QLabel(ui->remoteVideoContainer);
        screenShareOverlayLabel->setObjectName(QStringLiteral("screenShareOverlayLabel"));
        screenShareOverlayLabel->setAlignment(Qt::AlignCenter);
        screenShareOverlayLabel->setStyleSheet(
            QStringLiteral("background-color: black; color: white;"));
        screenShareOverlayLabel->setSizePolicy(QSizePolicy::Expanding,
                                               QSizePolicy::Expanding);
        layout->addWidget(screenShareOverlayLabel);
    }

    if (!screenShareHideTimer) {
        screenShareHideTimer = new QTimer(this);
        screenShareHideTimer->setInterval(500);
        screenShareHideTimer->setSingleShot(true);
        connect(screenShareHideTimer, &QTimer::timeout, this, [this]() {
            // 一段时间未收到新帧，认为屏幕共享结束，恢复摄像头视频。
            if (screenShareOverlayLabel) {
                screenShareOverlayLabel->hide();
                screenShareOverlayLabel->setPixmap(QPixmap());
                screenShareOverlayLabel->setText(QString());
            }
            if (videoNet) {
                if (QWidget *remoteWidget = videoNet->getRemoteVideoWidget()) {
                    remoteWidget->setVisible(true);
                }
            }
        });
    }

    screenShareOverlayLabel->show();
    screenShareOverlayLabel->raise();

    updateScreenSharePixmap();

    screenShareHideTimer->start();
}

void MainWindow::updateScreenSharePixmap()
{
    if (!screenShareOverlayLabel || !screenShareOverlayLabel->isVisible()) {
        return;
    }
    if (lastScreenShareFrame.isNull()) {
        return;
    }

    const QSize size = screenShareOverlayLabel->size();
    if (size.isEmpty()) {
        return;
    }

    // 每次绘制前完全用当前帧覆盖整个控件区域，采用“裁剪式”缩放避免拉伸错位或残影。
    const QPixmap pixmap =
        QPixmap::fromImage(lastScreenShareFrame).scaled(size,
                                                        Qt::KeepAspectRatioByExpanding,
                                                        Qt::SmoothTransformation);
    screenShareOverlayLabel->setPixmap(pixmap);
    screenShareOverlayLabel->setText(QString());
}

void MainWindow::appendLogMessage(const QString &message)
{
    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz "));
    const QString line = timestamp + message;

    if (ui && ui->logView) {
        ui->logView->appendPlainText(line);
    }
}

  void MainWindow::resetMeetingState()
  {
    if (audioNet && audioTransportActive) {
        appendLogMessage(QStringLiteral("停止音频传输"));
        audioNet->stopTransport();
    }
    audioTransportActive = false;

    if (videoNet && videoTransportActive) {
        appendLogMessage(QStringLiteral("停止视频传输"));
        videoNet->stopTransport();
    }
    videoTransportActive = false;

      audioMuted = false;
      if (audioNet) {
          audioNet->setMuted(false);
      }
      updateLocalMediaStateIcons();

    meetingRole = MeetingRole::None;
    meetingState = MeetingState::Idle;
    currentRemoteIp.clear();

    participantInfos.clear();
    participantOrder.clear();
    refreshParticipantListView();
    activeClientIps.clear();

    // 清理主持人端的多路远端视频/音频接收资源
    if (hostVideoRecvSocket) {
        hostVideoRecvSocket->close();
        hostVideoRecvSocket->deleteLater();
        hostVideoRecvSocket = nullptr;
    }
    hostVideoLabels.clear();

    if (hostAudioRecvSocket) {
        hostAudioRecvSocket->close();
        hostAudioRecvSocket->deleteLater();
        hostAudioRecvSocket = nullptr;
    }

      if (ui->remoteVideoContainer) {
          if (auto *layout = ui->remoteVideoContainer->layout()) {
              while (QLayoutItem *item = layout->takeAt(0)) {
                  if (QWidget *w = item->widget()) {
                      w->deleteLater();
                  }
                  delete item;
              }
          }
      }
      remoteParticipantsContainer = nullptr;
      remoteParticipantsLayout = nullptr;
      hostVideoLabels.clear();
      hostVideoMicIconLabels.clear();
      hostVideoCameraIconLabels.clear();
      activeSpeakerIp.clear();

    if (screenShare && screenShare->isSending()) {
        screenShare->stopSender();
    }

    if (screenShare && screenShare->isReceiving()) {
        screenShare->stopReceiver();
    }

      if (screenShareHideTimer) {
          screenShareHideTimer->stop();
      }
      if (screenShareOverlayLabel) {
          screenShareOverlayLabel->hide();
          screenShareOverlayLabel->setPixmap(QPixmap());
          screenShareOverlayLabel->setText(QString());
      }
      screenShareOverlayLabel = nullptr;
    lastScreenShareFrame = QImage();
}

void MainWindow::startClientMediaTransports()
{
    if (!audioNet || !videoNet || currentRemoteIp.isEmpty()) {
        return;
    }

    appendLogMessage(QStringLiteral("控制连接已建立，开始启动音视频传输"));

    if (audioNet->startTransport(Config::AUDIO_PORT_RECV, currentRemoteIp, Config::AUDIO_PORT_SEND)) {
        audioNet->setMuted(audioMuted);
        audioTransportActive = true;
        appendLogMessage(QStringLiteral("音频传输通道已建立（本地端口 %1 -> 远端 %2:%3）")
                             .arg(Config::AUDIO_PORT_RECV)
                             .arg(currentRemoteIp)
                             .arg(Config::AUDIO_PORT_SEND));
    } else {
        audioTransportActive = false;
        QMessageBox::critical(this,
                              QStringLiteral("Audio error"),
                              QStringLiteral("Failed to create audio network channel (port may be in use)."));
    }

    if (videoNet->startTransport(Config::VIDEO_PORT_RECV, currentRemoteIp, Config::VIDEO_PORT_SEND)) {
        videoTransportActive = true;
        appendLogMessage(QStringLiteral("视频传输通道已建立（本地端口 %1 -> 远端 %2:%3）")
                             .arg(Config::VIDEO_PORT_RECV)
                             .arg(currentRemoteIp)
                             .arg(Config::VIDEO_PORT_SEND));
    } else {
        videoTransportActive = false;
        QMessageBox::critical(this,
                              QStringLiteral("Video error"),
                              QStringLiteral("Failed to create video network channel (port may be in use)."));
    }

    updateMeetingStatusLabel();
    updateControlsForMeetingState();
}

// Host-side: lazily create and bind the UDP socket used to receive
// video frames from all connected participants.
static constexpr int kHostVideoThumbWidth  = 160;
static constexpr int kHostVideoThumbHeight = 120;

QWidget *MainWindow::createParticipantVideoTile(const QString &displayName,
                                                QLabel **outVideoLabel,
                                                QLabel **outMicIconLabel,
                                                QLabel **outCameraIconLabel)
{
    if (!remoteParticipantsContainer) {
        return nullptr;
    }

    QWidget *tile = new QWidget(remoteParticipantsContainer);
    tile->setObjectName(QStringLiteral("remoteParticipantTile"));
    tile->setStyleSheet(QStringLiteral("background-color: #202020; border-radius: 4px;"));

    auto *tileLayout = new QVBoxLayout(tile);
    tileLayout->setContentsMargins(0, 0, 0, 0);
    tileLayout->setSpacing(0);

    QLabel *videoLabel = new QLabel(tile);
    videoLabel->setMinimumSize(kHostVideoThumbWidth, kHostVideoThumbHeight);
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    tileLayout->addWidget(videoLabel);

    QWidget *infoBar = new QWidget(tile);
    auto *infoLayout = new QHBoxLayout(infoBar);
    infoLayout->setContentsMargins(6, 2, 6, 2);
    infoLayout->setSpacing(4);

    QLabel *nameLabel = new QLabel(displayName, infoBar);
    nameLabel->setStyleSheet(QStringLiteral("color: white;"));
    infoLayout->addWidget(nameLabel);

    infoLayout->addStretch();

    QLabel *micIconLabel = new QLabel(infoBar);
    QLabel *cameraIconLabel = new QLabel(infoBar);
    QStyle *s = style();
    micIconLabel->setPixmap(s->standardIcon(QStyle::SP_MediaVolume).pixmap(16, 16));
    micIconLabel->setToolTip(QStringLiteral("Microphone status (remote)"));
    cameraIconLabel->setPixmap(s->standardIcon(QStyle::SP_DesktopIcon).pixmap(16, 16));
    cameraIconLabel->setToolTip(QStringLiteral("Camera status (remote)"));
    infoLayout->addWidget(micIconLabel);
    infoLayout->addWidget(cameraIconLabel);

    tileLayout->addWidget(infoBar);

    if (outVideoLabel) {
        *outVideoLabel = videoLabel;
    }
    if (outMicIconLabel) {
        *outMicIconLabel = micIconLabel;
    }
    if (outCameraIconLabel) {
        *outCameraIconLabel = cameraIconLabel;
    }

    return tile;
}

void MainWindow::rebuildRemoteParticipantGrid()
{
    if (!remoteParticipantsContainer || !remoteParticipantsLayout) {
        return;
    }

    // Clear layout items but keep tiles alive.
    while (QLayoutItem *item = remoteParticipantsLayout->takeAt(0)) {
        delete item;
    }

    const QList<QString> keys = hostVideoLabels.keys();
    const int count = keys.size();
    if (count == 0) {
        remoteParticipantsContainer->hide();
        return;
    }

    remoteParticipantsContainer->show();

    int columns = 1;
    if (count == 1) {
        columns = 1;
    } else if (count == 2) {
        columns = 2;
    } else if (count <= 4) {
        columns = 2;
    } else {
        columns = 3;
    }
    int row = 0;
    int column = 0;

    for (int i = 0; i < keys.size(); ++i) {
        const QString &ip = keys.at(i);
        QLabel *label = hostVideoLabels.value(ip, nullptr);
        if (!label) {
            continue;
        }
        QWidget *tile = label->parentWidget();
        if (!tile) {
            continue;
        }

        // 根据当前发言者设置高亮边框
        if (!activeSpeakerIp.isEmpty() && ip == activeSpeakerIp) {
            tile->setStyleSheet(QStringLiteral(
                "background-color: #202020; border-radius: 4px; border: 2px solid #3daee9;"));
        } else {
            tile->setStyleSheet(QStringLiteral(
                "background-color: #202020; border-radius: 4px;"));
        }

        remoteParticipantsLayout->addWidget(tile, row, column);
        remoteParticipantsLayout->setAlignment(tile, Qt::AlignCenter);

        ++column;
        if (column >= columns) {
            column = 0;
            ++row;
        }
    }
}

void MainWindow::initHostVideoReceiver()
{
    if (hostVideoRecvSocket) {
        return;
    }

    hostVideoRecvSocket = new QUdpSocket(this);
    if (!hostVideoRecvSocket->bind(QHostAddress::AnyIPv4, Config::VIDEO_PORT_SEND)) {
        appendLogMessage(QStringLiteral("主持人端视频接收端口绑定失败，无法接收远端视频"));
        hostVideoRecvSocket->deleteLater();
        hostVideoRecvSocket = nullptr;
        return;
    }

    connect(hostVideoRecvSocket, &QUdpSocket::readyRead, this, [this]() {
        while (hostVideoRecvSocket && hostVideoRecvSocket->hasPendingDatagrams()) {
            QByteArray datagram;
            datagram.resize(int(hostVideoRecvSocket->pendingDatagramSize()));

            QHostAddress senderAddr;
            quint16 senderPort = 0;
            const qint64 read =
                hostVideoRecvSocket->readDatagram(datagram.data(), datagram.size(), &senderAddr, &senderPort);
            if (read <= 0) {
                appendLogMessage(QStringLiteral("读取远端视频数据失败：%1")
                                     .arg(hostVideoRecvSocket->errorString()));
                continue;
            }

            if (read < datagram.size()) {
                datagram.resize(int(read));
            }

            if (datagram.isEmpty()) {
                continue;
            }

            const QString senderIp = senderAddr.toString();

            if (!ui->remoteVideoContainer) {
                continue;
            }

            QLabel *label = hostVideoLabels.value(senderIp, nullptr);
              if (!label) {
                  if (!remoteParticipantsContainer || !remoteParticipantsLayout) {
                      if (QWidget *remoteContainer = ui->remoteVideoContainer) {
                          auto *outerLayout = qobject_cast<QVBoxLayout *>(remoteContainer->layout());
                          if (!outerLayout) {
                              outerLayout = new QVBoxLayout(remoteContainer);
                              remoteContainer->setLayout(outerLayout);
                              outerLayout->setContentsMargins(0, 0, 0, 0);
                              outerLayout->setSpacing(0);
                          }

                          remoteParticipantsContainer = new QWidget(remoteContainer);
                          remoteParticipantsContainer->setObjectName(
                              QStringLiteral("remoteParticipantsContainer"));
                          remoteParticipantsContainer->setSizePolicy(QSizePolicy::Expanding,
                                                                     QSizePolicy::Expanding);
                          remoteParticipantsLayout = new QGridLayout(remoteParticipantsContainer);
                          remoteParticipantsLayout->setContentsMargins(4, 4, 4, 4);
                          remoteParticipantsLayout->setSpacing(4);
                          remoteParticipantsContainer->show();
                          outerLayout->addWidget(remoteParticipantsContainer);
                      }
                  }

                  QLabel *videoLabel = nullptr;
                  QLabel *micIcon = nullptr;
                  QLabel *cameraIcon = nullptr;
                  QWidget *tile = createParticipantVideoTile(senderIp,
                                                             &videoLabel,
                                                             &micIcon,
                                                             &cameraIcon);
                  label = videoLabel;
                  hostVideoLabels.insert(senderIp, label);
                  if (!senderIp.isEmpty()) {
                      if (micIcon) {
                          hostVideoMicIconLabels.insert(senderIp, micIcon);
                      }
                      if (cameraIcon) {
                          hostVideoCameraIconLabels.insert(senderIp, cameraIcon);
                      }
                  }

                  if (remoteParticipantsLayout) {
                      remoteParticipantsLayout->addWidget(tile);
                      rebuildRemoteParticipantGrid();
                  }
              }

            QImage image;
            if (!image.loadFromData(datagram, "JPG")) {
                appendLogMessage(QStringLiteral("解码远端 JPEG 视频帧失败（大小=%1）").arg(datagram.size()));
                continue;
            }

            if (!image.isNull()) {
                label->setPixmap(QPixmap::fromImage(image).scaled(label->size(),
                                                                  Qt::KeepAspectRatio,
                                                                  Qt::SmoothTransformation));
                label->setToolTip(QStringLiteral("From: %1").arg(senderIp));
            }
        }
    });
}

// Host-side: lazily create and bind the UDP socket used to receive
// audio frames from all connected participants and perform a simple
// mixing before sending to AudioEngine for playback.
  void MainWindow::initHostAudioMixer()
  {
    if (hostAudioRecvSocket) {
        return;
    }

    hostAudioRecvSocket = new QUdpSocket(this);
    if (!hostAudioRecvSocket->bind(QHostAddress::AnyIPv4, Config::AUDIO_PORT_SEND)) {
        appendLogMessage(QStringLiteral("主持人端音频接收端口绑定失败，无法接收远端音频"));
        hostAudioRecvSocket->deleteLater();
        hostAudioRecvSocket = nullptr;
        return;
    }

    connect(hostAudioRecvSocket, &QUdpSocket::readyRead, this, [this]() {
        if (!audio) {
            // 没有音频引擎就无法播放，直接丢弃。
            while (hostAudioRecvSocket && hostAudioRecvSocket->hasPendingDatagrams()) {
                QByteArray tmp;
                tmp.resize(int(hostAudioRecvSocket->pendingDatagramSize()));
                hostAudioRecvSocket->readDatagram(tmp.data(), tmp.size());
            }
            return;
        }

        // 一次 readyRead 内将当前所有待处理的数据包进行简单叠加混音。
          QVector<QByteArray> packets;
          QHash<QString, double> levels;
        qint64 maxSize = 0;

        while (hostAudioRecvSocket && hostAudioRecvSocket->hasPendingDatagrams()) {
              QByteArray datagram;
              datagram.resize(int(hostAudioRecvSocket->pendingDatagramSize()));
            QHostAddress senderAddr;
            quint16 senderPort = 0;
            const qint64 read =
                hostAudioRecvSocket->readDatagram(datagram.data(), datagram.size(), &senderAddr, &senderPort);
            if (read <= 0) {
                appendLogMessage(QStringLiteral("读取远端音频数据失败：%1")
                                     .arg(hostAudioRecvSocket->errorString()));
                continue;
            }
            if (read < datagram.size()) {
                datagram.resize(int(read));
            }
              if (datagram.isEmpty()) {
                  continue;
              }
              packets.push_back(datagram);
              if (datagram.size() > maxSize) {
                  maxSize = datagram.size();
              }

              // 简单的说话人检测：计算每个 IP 的音频能量（平均绝对值）
              if (!senderAddr.isNull()) {
                  const QString senderIp = senderAddr.toString();
                  const int sampleCount = datagram.size() / 2;
                  if (sampleCount > 0) {
                      const auto *samples =
                          reinterpret_cast<const qint16 *>(datagram.constData());
                      double sum = 0.0;
                      for (int i = 0; i < sampleCount; ++i) {
                          sum += std::abs(samples[i]);
                      }
                      const double level = sum / double(sampleCount);
                      levels[senderIp] = qMax(levels.value(senderIp, 0.0), level);
                  }
              }
          }

        if (packets.isEmpty() || maxSize <= 0) {
            return;
        }

        // 混音：将所有 16bit 单声道 PCM 流简单求和并截断到 int16 范围。
        QByteArray mixed;
        mixed.resize(int(maxSize));
        mixed.fill(0);

        const int sampleCount = mixed.size() / 2;
        auto *mixedSamples = reinterpret_cast<qint16 *>(mixed.data());

        for (const QByteArray &packet : packets) {
            const int count = qMin(sampleCount, packet.size() / 2);
            const auto *srcSamples = reinterpret_cast<const qint16 *>(packet.constData());
            for (int i = 0; i < count; ++i) {
                int sum = int(mixedSamples[i]) + int(srcSamples[i]);
                if (sum > 32767) {
                    sum = 32767;
                } else if (sum < -32768) {
                    sum = -32768;
                }
                mixedSamples[i] = qint16(sum);
            }
        }

          audio->playAudio(mixed);

          // 选择当前能量最大的一个作为简单的“当前发言者”
          QString newActiveSpeaker;
          double maxLevel = 0.0;
          const double threshold = 500.0; // 简单阈值，避免环境噪声触发
          for (auto it = levels.constBegin(); it != levels.constEnd(); ++it) {
              if (it.value() > maxLevel) {
                  maxLevel = it.value();
                  newActiveSpeaker = it.key();
              }
          }
          if (!newActiveSpeaker.isEmpty() && maxLevel > threshold) {
              if (newActiveSpeaker != activeSpeakerIp) {
                  activeSpeakerIp = newActiveSpeaker;
                  rebuildRemoteParticipantGrid();
              }
          } else if (!levels.isEmpty() && maxLevel <= threshold) {
              // 没有明显发言者时清除高亮
              if (!activeSpeakerIp.isEmpty()) {
                  activeSpeakerIp.clear();
                  rebuildRemoteParticipantGrid();
              }
          }

        // 将混音后的会议音频广播给所有已知的客户端（每个客户端在本地使用 AudioTransport 播放）。
        for (const QString &ip : std::as_const(activeClientIps)) {
            if (ip.isEmpty()) {
                continue;
            }
            hostAudioRecvSocket->writeDatagram(mixed, QHostAddress(ip), Config::AUDIO_PORT_RECV);
        }
    });
}

  void MainWindow::updateControlsForMeetingState()
  {
      const bool inMeeting = (meetingState == MeetingState::InMeeting);
      const bool idle = (meetingState == MeetingState::Idle);

    if (btnCreateRoom) {
        btnCreateRoom->setEnabled(idle);
    }
    if (btnJoinRoom) {
        btnJoinRoom->setEnabled(idle);
    }
      if (btnLeaveRoom) {
          btnLeaveRoom->setEnabled(!idle);
      }
      if (btnMute) {
          btnMute->setEnabled(inMeeting);
          btnMute->setChecked(inMeeting && audioMuted);
      }
      if (btnCamera) {
          btnCamera->setEnabled(inMeeting);
          btnCamera->setChecked(inMeeting && !cameraEnabled);
      }
      if (btnScreenShare) {
          const bool canShare = inMeeting && meetingRole == MeetingRole::Host;
          btnScreenShare->setEnabled(canShare);
          if (!canShare) {
              btnScreenShare->setChecked(false);
          }
      }

    const bool chatEnabled = inMeeting;
    if (ui->chatLineEdit) {
        ui->chatLineEdit->setEnabled(chatEnabled);
    }
    if (ui->btnSendChat) {
        ui->btnSendChat->setEnabled(chatEnabled);
    }

      if (btnToggleSidePanel && ui->sidePanel) {
          if (inMeeting) {
              btnToggleSidePanel->setChecked(true);
              ui->sidePanel->setVisible(true);
          }
      }

      updateLocalMediaStateIcons();
  }

void MainWindow::updateMeetingStatusLabel()

{

    if (!statusLabel) {

        return;

    }



    const bool hasLocalParticipant = participantInfos.contains(localParticipantKey());

    connectedClientCount = qMax(0, participantInfos.size() - (hasLocalParticipant ? 1 : 0));



    QString statusText;



    switch (meetingState) {

    case MeetingState::Idle:

        statusText = QStringLiteral("Not connected");

        break;

    case MeetingState::WaitingPeer: {

        const int participants = connectedClientCount + (hasLocalParticipant ? 1 : 0);

        statusText = QStringLiteral("Meeting created, waiting for participants (currently %1)").arg(participants);

        break;

    }

    case MeetingState::Connecting:

        statusText = QStringLiteral("Joining meeting...");

        break;

    case MeetingState::InMeeting: {

        const int participants = connectedClientCount + (hasLocalParticipant ? 1 : 0);

        statusText = QStringLiteral("In meeting (%1 participants)").arg(participants);

        break;

    }

    }



    statusLabel->setText(statusText);
    setWindowTitle(QStringLiteral("LAN Meeting - %1").arg(statusText));
}


void MainWindow::appendChatMessage(const QString &sender, const QString &message, bool isLocal)
{
    if (ui && ui->chatList) {
        QListWidget *list = ui->chatList;
        const bool isSystem =
            sender.compare(QStringLiteral("System"), Qt::CaseInsensitive) == 0;

        ChatMessageWidget::MessageKind kind = ChatMessageWidget::MessageKind::Remote;
        if (isSystem) {
            kind = ChatMessageWidget::MessageKind::System;
        } else if (isLocal) {
            kind = ChatMessageWidget::MessageKind::Local;
        }

        auto *widget = new ChatMessageWidget(sender, message, kind, list);
        auto *item = new QListWidgetItem(list);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
        item->setSizeHint(widget->sizeHint());
        list->setItemWidget(item, widget);
        list->scrollToBottom();
    }

    if (isLocal) {
        appendLogMessage(QStringLiteral("发送聊天消息：%1").arg(message));
    } else {
        appendLogMessage(QStringLiteral("收到聊天消息（%1）：%2").arg(sender, message));
    }
}

void MainWindow::refreshParticipantListView()
{
    if (!ui || !ui->participantList) {
        return;
    }

    QListWidget *list = ui->participantList;
    list->clear();

    QStyle *s = style();
    const QIcon micOn = s ? s->standardIcon(QStyle::SP_MediaVolume) : QIcon();
    const QIcon micOff = s ? s->standardIcon(QStyle::SP_MediaVolumeMuted) : QIcon();
    const QIcon camOn = s ? s->standardIcon(QStyle::SP_DesktopIcon) : QIcon();
    const QIcon camOff = s ? s->standardIcon(QStyle::SP_DialogCancelButton) : QIcon();

    for (const QString &key : participantOrder) {
        if (!participantInfos.contains(key)) {
            continue;
        }
        const ParticipantInfo info = participantInfos.value(key);

        auto *item = new QListWidgetItem(list);
        QWidget *row = new QWidget(list);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(8, 4, 8, 4);
        layout->setSpacing(6);

        QLabel *nameLabel = new QLabel(info.displayName, row);
        QFont f = nameLabel->font();
        if (info.isLocal) {
            f.setBold(true);
            nameLabel->setFont(f);
        }
        layout->addWidget(nameLabel, 1);

        QLabel *micLabel = new QLabel(row);
        micLabel->setPixmap((info.micMuted ? micOff : micOn).pixmap(16, 16));
        micLabel->setToolTip(info.micMuted ? QStringLiteral("Microphone muted")
                                           : QStringLiteral("Microphone on"));
        layout->addWidget(micLabel, 0, Qt::AlignRight);

        QLabel *camLabel = new QLabel(row);
        camLabel->setPixmap((info.cameraEnabled ? camOn : camOff).pixmap(16, 16));
        camLabel->setToolTip(info.cameraEnabled ? QStringLiteral("Camera on")
                                                : QStringLiteral("Camera off"));
        layout->addWidget(camLabel, 0, Qt::AlignRight);

        row->setLayout(layout);
        item->setSizeHint(row->sizeHint());
        list->addItem(item);
        list->setItemWidget(item, row);
    }

    updateMeetingStatusLabel();
    updateControlsForMeetingState();
}

QString MainWindow::localParticipantKey() const
{
    return QStringLiteral("local");
}

QString MainWindow::hostParticipantKey() const
{
    return QStringLiteral("host");
}

void MainWindow::upsertParticipant(const QString &key,
                                   const QString &displayName,
                                   const QString &ip,
                                   bool micMuted,
                                   bool cameraEnabled,
                                   bool isLocal)
{
    if (key.isEmpty()) {
        return;
    }

    ParticipantInfo info;
    info.key = key;
    info.displayName = displayName;
    info.ip = ip.isEmpty() ? key : ip;
    info.micMuted = micMuted;
    info.cameraEnabled = cameraEnabled;
    info.isLocal = isLocal;

    participantInfos.insert(key, info);
    if (!participantOrder.contains(key)) {
        participantOrder.append(key);
    }

    refreshParticipantListView();
}

void MainWindow::removeParticipant(const QString &key)
{
    if (key.isEmpty()) {
        return;
    }

    participantInfos.remove(key);
    participantOrder.removeAll(key);
    refreshParticipantListView();
}

void MainWindow::updateParticipantMediaStateByKey(const QString &key,
                                                  bool micMuted,
                                                  bool cameraEnabled)
{
    if (key.isEmpty() || !participantInfos.contains(key)) {
        return;
    }

    ParticipantInfo info = participantInfos.value(key);
    if (info.micMuted == micMuted && info.cameraEnabled == cameraEnabled) {
        return;
    }

    info.micMuted = micMuted;
    info.cameraEnabled = cameraEnabled;
    participantInfos.insert(key, info);
    refreshParticipantListView();
}

void MainWindow::updateParticipantMediaStateByIp(const QString &ip,
                                                 bool micMuted,
                                                 bool cameraEnabled)
{
    if (ip.isEmpty()) {
        return;
    }

    for (const QString &key : participantInfos.keys()) {
        ParticipantInfo info = participantInfos.value(key);
        const QString effectiveIp = info.ip.isEmpty() ? key : info.ip;
        if (effectiveIp == ip) {
            if (info.micMuted == micMuted && info.cameraEnabled == cameraEnabled) {
                return;
            }
            info.micMuted = micMuted;
            info.cameraEnabled = cameraEnabled;
            participantInfos.insert(key, info);
            refreshParticipantListView();
            return;
        }
    }

    // If we reach here, add a placeholder participant for this IP.
    const QString display = QStringLiteral("Participant (%1)").arg(ip);
    upsertParticipant(ip, display, ip, micMuted, cameraEnabled, false);
}

void MainWindow::broadcastLocalMediaState()
{
    updateParticipantMediaStateByKey(localParticipantKey(), audioMuted, cameraEnabled);

    if (meetingState != MeetingState::InMeeting) {
        return;
    }

    if (meetingRole == MeetingRole::Host && server) {
        server->broadcastMediaState(hostParticipantKey(), audioMuted, cameraEnabled);
    } else if (meetingRole == MeetingRole::Guest && client) {
        client->sendMediaState(audioMuted, cameraEnabled);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->videoArea || watched == controlBar) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove) {
            showControlBarTemporarily();
        }
    }

    if (watched == ui->videoContainer) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                isDraggingPreview = true;
                previewDragStartPos = mouseEvent->globalPosition().toPoint();
                previewStartPos = ui->videoContainer->pos();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            if (isDraggingPreview) {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                const QPoint delta = mouseEvent->globalPosition().toPoint() - previewDragStartPos;
                QPoint newPos = previewStartPos + delta;

                if (ui->videoArea) {
                    int maxX = ui->videoArea->width() - ui->videoContainer->width();
                    int maxY = ui->videoArea->height() - ui->videoContainer->height();
                    if (maxX < 0) {
                        maxX = 0;
                    }
                    if (maxY < 0) {
                        maxY = 0;
                    }

                    if (newPos.x() < 0) {
                        newPos.setX(0);
                    } else if (newPos.x() > maxX) {
                        newPos.setX(maxX);
                    }

                    if (newPos.y() < 0) {
                        newPos.setY(0);
                    } else if (newPos.y() > maxY) {
                        newPos.setY(maxY);
                    }
                }

                ui->videoContainer->move(newPos);
                ui->videoContainer->raise();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && isDraggingPreview) {
                isDraggingPreview = false;
                return true;
            }
            break;
        }
        default:
            break;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateOverlayGeometry();
    updateScreenSharePixmap();
}

void MainWindow::on_btnCreateRoom_clicked()
{
    appendLogMessage(QStringLiteral("用户点击创建会议"));

    if (meetingRole == MeetingRole::Guest) {
        QMessageBox::information(this,
                                 QStringLiteral("Host meeting"),
                                 QStringLiteral("You are already a participant. Leave the current meeting before hosting a new one."));
        appendLogMessage(QStringLiteral("创建会议被拒绝：当前处于参会方状态"));
        return;
    }

    if (!server) {
        server = new ControlServer(this);

        connect(server, &ControlServer::clientJoined, this, [this](const QString &ip) {
            appendLogMessage(QStringLiteral("控制连接收到客户端加入：%1").arg(ip));

            const QString displayName = QStringLiteral("Participant (%1)").arg(ip);
            upsertParticipant(ip, displayName, ip, false, true, false);
            activeClientIps.insert(ip);

            appendChatMessage(QStringLiteral("System"),
                              QStringLiteral("%1 joined the meeting").arg(displayName),
                              false);

            if (audioNet && !audioNet->startSendOnly(ip, Config::AUDIO_PORT_RECV)) {
                QMessageBox::critical(this,
                                      QStringLiteral("Audio error"),
                                      QStringLiteral("Failed to create audio network channel (port may be in use)."));
                audioTransportActive = false;
            } else if (audioNet) {
                audioNet->setMuted(audioMuted);
                audioTransportActive = true;
            }

            if (videoNet && !videoNet->startSendOnly(ip, Config::VIDEO_PORT_RECV)) {
                QMessageBox::critical(this,
                                      QStringLiteral("Video error"),
                                      QStringLiteral("Failed to create video network channel (port may be in use)."));
                videoTransportActive = false;
            } else if (videoNet) {
                videoTransportActive = true;
            }

            meetingRole = MeetingRole::Host;
            meetingState = MeetingState::InMeeting;

            // 更新远端视频区域占位文本
            if (videoNet) {
                QWidget *remoteWidget = videoNet->getRemoteVideoWidget();
                if (remoteWidget) {
                    if (auto *label =
                            remoteWidget->findChild<QLabel *>(QStringLiteral("remoteVideoLabel"))) {
                        label->setText(QStringLiteral("Remote participant joined, waiting for video..."));
                    }
                }
            }

            updateMeetingStatusLabel();
            updateControlsForMeetingState();

            appendLogMessage(QStringLiteral("客户端 %1 已加入会议，音视频传输已启动").arg(ip));
        });

        connect(server, &ControlServer::clientLeft, this, [this](const QString &ip) {
            const QString displayName = QStringLiteral("Participant (%1)").arg(ip);
            appendLogMessage(QStringLiteral("服务器检测到客户端离开：%1").arg(ip));

            removeParticipant(ip);
            activeClientIps.remove(ip);

            appendChatMessage(QStringLiteral("System"),
                              QStringLiteral("%1 left the meeting").arg(displayName),
                              false);

            // 停止当前音视频通道，保持会议继续处于“等待对端加入”状态
            if (audioNet && audioTransportActive) {
                audioNet->stopTransport();
                audioTransportActive = false;
            }
            if (videoNet && videoTransportActive) {
                videoNet->stopTransport();
                videoTransportActive = false;
            }

            meetingState = MeetingState::WaitingPeer;
            updateMeetingStatusLabel();
            updateControlsForMeetingState();
        });

        connect(server, &ControlServer::chatReceived, this, [this](const QString &ip, const QString &msg) {
            Q_UNUSED(ip);
            appendChatMessage(QStringLiteral("Remote"), msg, false);
        });

        connect(server,
                &ControlServer::mediaStateChanged,
                this,
                [this](const QString &ip, bool micMuted, bool cameraEnabled) {
                    updateParticipantMediaStateByIp(ip, micMuted, cameraEnabled);
                });
    }

    if (server->startServer()) {
        QMessageBox::information(this,
                                 QStringLiteral("Host meeting"),
                                 QStringLiteral("Meeting created. Waiting for participants..."));
        meetingRole = MeetingRole::Host;
        meetingState = MeetingState::WaitingPeer;

        participantInfos.clear();
        participantOrder.clear();
        upsertParticipant(localParticipantKey(),
                          QStringLiteral("Me (Host)"),
                          hostParticipantKey(),
                          audioMuted,
                          cameraEnabled,
                          true);

        appendChatMessage(QStringLiteral("System"),
                          QStringLiteral("You created a meeting and are waiting for participants."),
                          false);

        updateMeetingStatusLabel();
        updateControlsForMeetingState();
        appendLogMessage(QStringLiteral("会议服务器已启动，等待客户端连接"));
        initHostVideoReceiver();
        initHostAudioMixer();
    } else {
        QMessageBox::critical(this,
                              QStringLiteral("Host meeting"),
                              QStringLiteral("Failed to start meeting server (port may be in use)."));
        resetMeetingState();
        appendLogMessage(QStringLiteral("会议服务器启动失败，可能端口被占用"));
    }
}

void MainWindow::on_btnJoinRoom_clicked()
{
    appendLogMessage(QStringLiteral("用户点击加入会议"));

    if (meetingRole == MeetingRole::Host) {
        QMessageBox::information(this,
                                 QStringLiteral("Join meeting"),
                                 QStringLiteral("You are hosting a meeting already. Leave the current meeting before joining another."));
        appendLogMessage(QStringLiteral("加入会议被拒绝：当前处于主持人状态"));
        return;
    }

    if (meetingState == MeetingState::Connecting || meetingState == MeetingState::InMeeting) {
        QMessageBox::information(this,
                                 QStringLiteral("Join meeting"),
                                 QStringLiteral("You are already joining or in a meeting."));
        appendLogMessage(QStringLiteral("加入会议被拒绝：当前正在连接或已在会议中"));
        return;
    }

    bool ok = false;
    const QString ip = QInputDialog::getText(this,
                                             QStringLiteral("Join meeting"),
                                             QStringLiteral("Enter host IP address:"),
                                             QLineEdit::Normal,
                                             QStringLiteral("127.0.0.1"),
                                             &ok);
    if (!ok || ip.isEmpty()) {
        appendLogMessage(QStringLiteral("加入会议被取消（未输入有效 IP）"));
        return;
    }

    currentRemoteIp = ip;

    if (ui->chkMuteOnJoin && ui->chkMuteOnJoin->isChecked()) {
        audioMuted = true;
        if (audioNet) {
            audioNet->setMuted(true);
        }
        updateLocalMediaStateIcons();
    }

    if (!client) {
        client = new ControlClient(this);

        connect(client, &ControlClient::errorOccurred, this, [this](const QString &msg) {
            QMessageBox::critical(this,
                                  QStringLiteral("Join failed"),
                                  QStringLiteral("Control connection error:\n%1").arg(msg));
            appendLogMessage(QStringLiteral("控制连接错误：%1").arg(msg));
            resetMeetingState();
        });

        connect(client, &ControlClient::joined, this, [this]() {
            statusBar()->showMessage(QStringLiteral("Successfully joined meeting."), 3000);

            meetingRole = MeetingRole::Guest;
            meetingState = MeetingState::InMeeting;

            participantInfos.clear();
            participantOrder.clear();
            upsertParticipant(localParticipantKey(),
                              QStringLiteral("Me"),
                              localParticipantKey(),
                              audioMuted,
                              cameraEnabled,
                              true);
            const QString hostLabel = currentRemoteIp.isEmpty()
                                           ? QStringLiteral("Host")
                                           : QStringLiteral("Host (%1)").arg(currentRemoteIp);
            upsertParticipant(hostParticipantKey(),
                              hostLabel,
                              hostParticipantKey(),
                              false,
                              true,
                              false);

            appendChatMessage(QStringLiteral("System"),
                              QStringLiteral("You joined the meeting"),
                              false);

            appendLogMessage(QStringLiteral("控制连接握手成功，已加入会议"));
            startClientMediaTransports();
            broadcastLocalMediaState();

            // 客户端端：开始接收主持人屏幕共享画面
            if (screenShare && ui->screenShareLabel) {
                screenShare->setRenderLabel(ui->screenShareLabel);
                screenShare->startReceiver(Config::SCREEN_PORT_RECV);
            }
        });

        connect(client, &ControlClient::disconnected, this, [this]() {
            appendChatMessage(QStringLiteral("System"),
                              QStringLiteral("Disconnected from host"),
                              false);
            appendLogMessage(QStringLiteral("检测到控制连接断开，重置会议状态"));
            resetMeetingState();
        });

        connect(client, &ControlClient::chatReceived, this, [this](const QString &msg) {
            appendChatMessage(QStringLiteral("Remote"), msg, false);
        });

        connect(client,
                &ControlClient::mediaStateUpdated,
                this,
                [this](const QString &ip, bool micMuted, bool cameraEnabled) {
                    updateParticipantMediaStateByIp(ip, micMuted, cameraEnabled);
                });
    }

    client->connectToHost(ip, Config::CONTROL_PORT);
    meetingRole = MeetingRole::Guest;
    meetingState = MeetingState::Connecting;

    statusBar()->showMessage(QStringLiteral("Joining meeting..."), 3000);
    updateMeetingStatusLabel();
    updateControlsForMeetingState();
}

void MainWindow::on_btnLeaveRoom_clicked()
{
    appendLogMessage(QStringLiteral("用户点击离开会议"));

    if (meetingRole == MeetingRole::None && meetingState == MeetingState::Idle) {
        appendLogMessage(QStringLiteral("离开会议请求被忽略：当前不在会议中"));
        return;
    }

    const QMessageBox::StandardButton reply =
        QMessageBox::question(this,
                              QStringLiteral("Leave meeting"),
                              QStringLiteral("Are you sure you want to leave this meeting?"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::Yes);
    if (reply != QMessageBox::Yes) {
        appendLogMessage(QStringLiteral("离开会议操作被用户取消"));
        return;
    }

    appendChatMessage(QStringLiteral("System"),
                      QStringLiteral("You left the meeting"),
                      false);

    if (meetingRole == MeetingRole::Host && server) {
        appendLogMessage(QStringLiteral("停止会议服务器，断开所有客户端"));
        server->stopServer();
    }

    if (meetingRole == MeetingRole::Guest && client) {
        appendLogMessage(QStringLiteral("通知主机离开会议并关闭控制连接"));
        client->disconnectFromHost();
    }

    resetMeetingState();
}

void MainWindow::on_btnSendChat_clicked()
{
    if (!ui || !ui->chatLineEdit) {
        return;
    }

    const QString text = ui->chatLineEdit->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    if (meetingState != MeetingState::InMeeting) {
        QMessageBox::information(this,
                                 QStringLiteral("Chat"),
                                 QStringLiteral("You are not in a meeting; cannot send messages."));
        appendLogMessage(QStringLiteral("尝试在非会议状态发送聊天消息：%1").arg(text));
        return;
    }

    ui->chatLineEdit->clear();

    appendChatMessage(QStringLiteral("Me"), text, true);

    if (meetingRole == MeetingRole::Host && server) {
        server->sendChatToAll(text);
    } else if (meetingRole == MeetingRole::Guest && client) {
        client->sendChatMessage(text);
    }
}
