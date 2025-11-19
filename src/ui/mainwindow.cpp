#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QInputDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
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
#include <QTextBrowser>
#include <QPushButton>
#include <QListWidget>
#include <QTabWidget>

#include "common/Config.h"
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
    , controlBar(nullptr)
    , btnToggleSidePanel(nullptr)
    , btnCreateRoom(nullptr)
    , btnJoinRoom(nullptr)
    , btnLeaveRoom(nullptr)
    , btnMute(nullptr)
    , controlBarHideTimer(nullptr)
    , controlsContainer(nullptr)
    , roomTab(nullptr)
    , roomListWidget(nullptr)
    , btnRefreshRooms(nullptr)
    , btnCreateRoomInTab(nullptr)
    , btnJoinRoomInTab(nullptr)
    , isDraggingPreview(false)
    , previewDragStartPos()
    , previewStartPos()
    , meetingRole(MeetingRole::None)
    , meetingState(MeetingState::Idle)
    , currentRemoteIp()
    , currentRoomId()
    , audioTransportActive(false)
    , videoTransportActive(false)
    , audioMuted(false)
    , connectedClientCount(0)
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
    }

    if (!media->startCamera()) {
        QMessageBox::warning(this,
                             QStringLiteral("视频错误"),
                             QStringLiteral("无法启动摄像头，请检查设备权限或占用情况。"));
    }

    // 本地音频采集和播放
    audio = new AudioEngine(this);
    if (!audio->startCapture()) {
        QMessageBox::warning(this,
                             QStringLiteral("音频错误"),
                             QStringLiteral("无法启动麦克风采集，请检查设备权限或占用情况。"));
    }
    if (!audio->startPlayback()) {
        QMessageBox::warning(this,
                             QStringLiteral("音频错误"),
                             QStringLiteral("无法启动扬声器播放，请检查音频设备配置。"));
    }

    audioNet = new AudioTransport(audio, this);

    // 远端视频显示区域
    videoNet = new MediaTransport(media, this);
    QWidget *remoteView = videoNet->getRemoteVideoWidget();
    if (ui->remoteVideoContainer && remoteView) {
        auto *remoteLayout = qobject_cast<QVBoxLayout *>(ui->remoteVideoContainer->layout());
        if (!remoteLayout) {
            remoteLayout = new QVBoxLayout(ui->remoteVideoContainer);
            ui->remoteVideoContainer->setLayout(remoteLayout);
        }
        remoteLayout->setContentsMargins(0, 0, 0, 0);
        remoteLayout->setSpacing(0);
        remoteView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        remoteLayout->addWidget(remoteView);
    }

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
            ui->sideTabWidget->setTabText(chatIndex, QStringLiteral("聊天"));
        }
        const int participantsIndex = ui->sideTabWidget->indexOf(ui->participantsTab);
        if (participantsIndex >= 0) {
            ui->sideTabWidget->setTabText(participantsIndex, QStringLiteral("参会者"));
        }
        const int logIndex = ui->sideTabWidget->indexOf(ui->logTab);
        if (logIndex >= 0) {
            ui->sideTabWidget->setTabText(logIndex, QStringLiteral("日志"));
        }
        const int settingsIndex = ui->sideTabWidget->indexOf(ui->settingsTab);
        if (settingsIndex >= 0) {
            ui->sideTabWidget->setTabText(settingsIndex, QStringLiteral("设置"));
        }
    }

    if (ui->labelSettings) {
        ui->labelSettings->setText(QStringLiteral("通用设置"));
    }
    if (ui->chatLineEdit) {
        ui->chatLineEdit->setPlaceholderText(QStringLiteral("输入消息..."));
    }
    if (ui->btnSendChat) {
        ui->btnSendChat->setText(QStringLiteral("发送"));
    }
    if (ui->labelParticipants) {
        ui->labelParticipants->setText(QStringLiteral("当前参会者"));
    }

    if (ui->chkHideSelfView) {
        ui->chkHideSelfView->setText(QStringLiteral("隐藏本地预览"));
        ui->chkHideSelfView->setChecked(false);
        connect(ui->chkHideSelfView, &QCheckBox::toggled, this, [this](bool checked) {
            if (ui->videoContainer) {
                ui->videoContainer->setVisible(!checked);
                updateOverlayGeometry();
            }
        });
    }

    if (ui->chkMuteOnJoin) {
        ui->chkMuteOnJoin->setText(QStringLiteral("加入时静音"));
    }

    // 动态添加“房间”选项卡，用于在加入会议前选择房间。
    if (ui->sideTabWidget && !roomTab) {
        roomTab = new QWidget(ui->sideTabWidget);
        roomTab->setObjectName(QStringLiteral("roomTab"));

        auto *layout = new QVBoxLayout(roomTab);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(4);

        auto *label = new QLabel(QStringLiteral("可用房间"), roomTab);
        roomListWidget = new QListWidget(roomTab);
        roomListWidget->setObjectName(QStringLiteral("roomListWidget"));

        btnCreateRoomInTab = new QPushButton(QStringLiteral("创建房间"), roomTab);
        btnJoinRoomInTab = new QPushButton(QStringLiteral("加入房间"), roomTab);
        btnRefreshRooms = new QPushButton(QStringLiteral("刷新列表"), roomTab);

        auto *buttonsLayout = new QHBoxLayout;
        buttonsLayout->setContentsMargins(0, 0, 0, 0);
        buttonsLayout->setSpacing(4);
        buttonsLayout->addWidget(btnCreateRoomInTab);
        buttonsLayout->addWidget(btnJoinRoomInTab);

        layout->addWidget(label);
        layout->addWidget(roomListWidget, 1);
        layout->addLayout(buttonsLayout);
        layout->addWidget(btnRefreshRooms);

        ui->sideTabWidget->addTab(roomTab, QStringLiteral("房间"));

        // 创建房间：输入房间 ID 后发送 CREATE_ROOM
        connect(btnCreateRoomInTab, &QPushButton::clicked, this, [this]() {
            if (!client) {
                QMessageBox::information(this,
                                         QStringLiteral("房间"),
                                         QStringLiteral("尚未连接到会议服务器，请先点击“加入”连接主机。"));
                return;
            }

            bool ok = false;
            const QString roomId = QInputDialog::getText(this,
                                                         QStringLiteral("创建房间"),
                                                         QStringLiteral("请输入房间 ID:"),
                                                         QLineEdit::Normal,
                                                         QString(),
                                                         &ok)
                                   .trimmed();
            if (!ok || roomId.isEmpty()) {
                appendLogMessage(QStringLiteral("创建房间被取消（未输入有效房间 ID）"));
                return;
            }

            appendLogMessage(QStringLiteral("请求创建房间：%1").arg(roomId));
            client->createRoom(roomId);
        });

        // 加入房间：优先使用列表中当前选中项，否则让用户输入房间 ID
        connect(btnJoinRoomInTab, &QPushButton::clicked, this, [this]() {
            if (!client) {
                QMessageBox::information(this,
                                         QStringLiteral("房间"),
                                         QStringLiteral("尚未连接到会议服务器，请先点击“加入”连接主机。"));
                return;
            }

            QString roomId;
            if (roomListWidget && roomListWidget->currentItem()) {
                roomId = roomListWidget->currentItem()->text().trimmed();
            }

            if (roomId.isEmpty()) {
                bool ok = false;
                roomId = QInputDialog::getText(this,
                                               QStringLiteral("加入房间"),
                                               QStringLiteral("请输入房间 ID:"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok)
                             .trimmed();
                if (!ok || roomId.isEmpty()) {
                    appendLogMessage(QStringLiteral("加入房间被取消（未选择或输入房间 ID）"));
                    return;
                }
            }

            appendLogMessage(QStringLiteral("请求加入房间：%1").arg(roomId));
            client->joinRoom(roomId);
        });

        // 刷新房间列表
        connect(btnRefreshRooms, &QPushButton::clicked, this, [this]() {
            if (!client) {
                QMessageBox::information(this,
                                         QStringLiteral("房间"),
                                         QStringLiteral("尚未连接到会议服务器，请先点击“加入”连接主机。"));
                return;
            }

            appendLogMessage(QStringLiteral("请求刷新房间列表"));
            client->requestRoomList();
        });
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

    // 主控制按钮：控制展开/收起其他功能按钮
    auto *btnMenu = new QToolButton(controlBar);
    btnMenu->setText(QStringLiteral("控制"));
    btnMenu->setCheckable(true);
    layout->addWidget(btnMenu);

    controlsContainer = new QWidget(controlBar);
    auto *controlsLayout = new QHBoxLayout(controlsContainer);
    controlsLayout->setContentsMargins(8, 0, 0, 0);
    controlsLayout->setSpacing(6);

    btnCreateRoom = new QToolButton(controlsContainer);
    btnCreateRoom->setText(QStringLiteral("创建"));
    controlsLayout->addWidget(btnCreateRoom);
    connect(btnCreateRoom, &QAbstractButton::clicked, this, &MainWindow::on_btnCreateRoom_clicked);

    btnJoinRoom = new QToolButton(controlsContainer);
    btnJoinRoom->setText(QStringLiteral("加入"));
    controlsLayout->addWidget(btnJoinRoom);
    connect(btnJoinRoom, &QAbstractButton::clicked, this, &MainWindow::on_btnJoinRoom_clicked);

    btnLeaveRoom = new QToolButton(controlsContainer);
    btnLeaveRoom->setText(QStringLiteral("离开"));
    controlsLayout->addWidget(btnLeaveRoom);
    connect(btnLeaveRoom, &QAbstractButton::clicked, this, &MainWindow::on_btnLeaveRoom_clicked);

    btnMute = new QToolButton(controlsContainer);
    btnMute->setText(QStringLiteral("静音"));
    btnMute->setCheckable(true);
    controlsLayout->addWidget(btnMute);
    connect(btnMute, &QAbstractButton::toggled, this, [this](bool checked) {
        audioMuted = checked;
        if (audioNet) {
            audioNet->setMuted(audioMuted);
        }
        appendLogMessage(checked ? QStringLiteral("已开启静音（本地音频不再发送）")
                                 : QStringLiteral("已关闭静音，恢复发送音频"));
    });

    btnToggleSidePanel = new QToolButton(controlsContainer);
    btnToggleSidePanel->setText(QStringLiteral("侧边栏"));
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

    meetingRole = MeetingRole::None;
    meetingState = MeetingState::Idle;
    currentRemoteIp.clear();
    currentRoomId.clear();

    participantNames.clear();
    refreshParticipantListView();

    if (roomListWidget) {
        roomListWidget->clear();
    }
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
                              QStringLiteral("音频错误"),
                              QStringLiteral("无法建立音频网络通道（端口可能被占用）。"));
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
                              QStringLiteral("视频错误"),
                              QStringLiteral("无法建立视频网络通道（端口可能被占用）。"));
    }

    updateMeetingStatusLabel();
    updateControlsForMeetingState();
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

    // 房间选择相关控件：仅在作为参会方且已开始连接服务器时可用（包括“连接中”和“已在会议中”）。
    const bool roomUiEnabled = (meetingRole == MeetingRole::Guest) && !idle;
    if (roomListWidget) {
        roomListWidget->setEnabled(roomUiEnabled);
    }
    if (btnCreateRoomInTab) {
        btnCreateRoomInTab->setEnabled(roomUiEnabled);
    }
    if (btnJoinRoomInTab) {
        btnJoinRoomInTab->setEnabled(roomUiEnabled && !inMeeting);
    }
    if (btnRefreshRooms) {
        btnRefreshRooms->setEnabled(roomUiEnabled);
    }
}

void MainWindow::updateMeetingStatusLabel()
{
    if (!statusLabel) {
        return;
    }

    // 计算除自己外的其他参会者数量
    if (meetingRole == MeetingRole::Host) {
        connectedClientCount = qMax(0, participantNames.size() - 1);
    } else if (meetingRole == MeetingRole::Guest) {
        connectedClientCount = qMax(0, participantNames.size() - 1);
    } else {
        connectedClientCount = 0;
    }

    QString statusText;

    switch (meetingState) {
    case MeetingState::Idle:
        statusText = QStringLiteral("未连接");
        break;
    case MeetingState::WaitingPeer: {
        const int participants = 1 + connectedClientCount;
        statusText = QStringLiteral("会议已创建，等待对端加入（当前 %1 人）").arg(participants);
        break;
    }
    case MeetingState::Connecting:
        statusText = QStringLiteral("正在加入会议...");
        break;
    case MeetingState::InMeeting: {
        const int participants = 1 + connectedClientCount;
        if (currentRoomId.isEmpty()) {
            statusText = QStringLiteral("会议进行中（%1 人）").arg(participants);
        } else {
            statusText = QStringLiteral("会议进行中 - 房间 %1（%2 人）")
                             .arg(currentRoomId)
                             .arg(participants);
        }
        break;
    }
    }

    statusLabel->setText(statusText);
    setWindowTitle(QStringLiteral("局域网会议 - %1").arg(statusText));
}

void MainWindow::appendChatMessage(const QString &sender, const QString &message, bool isLocal)
{
    if (ui && ui->chatView) {
        ui->chatView->append(QStringLiteral("%1: %2").arg(sender, message));
    }

    if (isLocal) {
        appendLogMessage(QStringLiteral("发送聊天消息：%1").arg(message));
    } else {
        appendLogMessage(QStringLiteral("收到聊天消息（%1）：%2").arg(sender, message));
    }
}

void MainWindow::refreshParticipantListView()
{
    if (ui && ui->participantList) {
        ui->participantList->clear();
        for (const QString &name : participantNames) {
            ui->participantList->addItem(name);
        }
    }

    updateMeetingStatusLabel();
    updateControlsForMeetingState();
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
}

void MainWindow::on_btnCreateRoom_clicked()
{
    appendLogMessage(QStringLiteral("用户点击创建会议"));

    if (meetingRole == MeetingRole::Guest) {
        QMessageBox::information(this,
                                 QStringLiteral("创建会议"),
                                 QStringLiteral("当前已作为参会方加入会议，请先离开当前会议再创建新会议。"));
        appendLogMessage(QStringLiteral("创建会议被拒绝：当前处于参会方状态"));
        return;
    }

    if (!server) {
        server = new ControlServer(this);

        connect(server, &ControlServer::clientJoined, this, [this](const QString &ip) {
            appendLogMessage(QStringLiteral("控制连接收到客户端加入：%1").arg(ip));

            const QString displayName = QStringLiteral("参会者（%1）").arg(ip);
            if (!participantNames.contains(displayName)) {
                participantNames.append(displayName);
                refreshParticipantListView();
            }

            appendChatMessage(QStringLiteral("系统"),
                              QStringLiteral("%1 加入会议").arg(displayName),
                              false);

            if (audioNet && !audioNet->startTransport(Config::AUDIO_PORT_SEND, ip, Config::AUDIO_PORT_RECV)) {
                QMessageBox::critical(this,
                                      QStringLiteral("音频错误"),
                                      QStringLiteral("无法建立音频网络通道（端口可能被占用）。"));
                audioTransportActive = false;
            } else if (audioNet) {
                audioNet->setMuted(audioMuted);
                audioTransportActive = true;
            }

            if (videoNet && !videoNet->startTransport(Config::VIDEO_PORT_SEND, ip, Config::VIDEO_PORT_RECV)) {
                QMessageBox::critical(this,
                                      QStringLiteral("视频错误"),
                                      QStringLiteral("无法建立视频网络通道（端口可能被占用）。"));
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
                        label->setText(QStringLiteral("对端已加入，等待视频画面..."));
                    }
                }
            }

            updateMeetingStatusLabel();
            updateControlsForMeetingState();

            appendLogMessage(QStringLiteral("客户端 %1 已加入会议，音视频传输已启动").arg(ip));
        });

        connect(server, &ControlServer::clientLeft, this, [this](const QString &ip) {
            const QString displayName = QStringLiteral("参会者（%1）").arg(ip);
            appendLogMessage(QStringLiteral("服务器检测到客户端离开：%1").arg(ip));

            participantNames.removeAll(displayName);
            refreshParticipantListView();

            appendChatMessage(QStringLiteral("系统"),
                              QStringLiteral("%1 离开会议").arg(displayName),
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
            appendChatMessage(QStringLiteral("对方"), msg, false);
        });
    }

    if (server->startServer()) {
        QMessageBox::information(this,
                                 QStringLiteral("创建会议"),
                                 QStringLiteral("会议已创建，等待其他客户端加入..."));
        meetingRole = MeetingRole::Host;
        meetingState = MeetingState::WaitingPeer;

        participantNames.clear();
        participantNames << QStringLiteral("我（主持人）");
        refreshParticipantListView();

        appendChatMessage(QStringLiteral("系统"),
                          QStringLiteral("你创建了会议，等待参会者加入。"),
                          false);

        updateMeetingStatusLabel();
        updateControlsForMeetingState();
        appendLogMessage(QStringLiteral("会议服务器已启动，等待客户端连接"));
    } else {
        QMessageBox::critical(this,
                              QStringLiteral("创建会议"),
                              QStringLiteral("无法创建会议服务器（端口可能被占用）。"));
        resetMeetingState();
        appendLogMessage(QStringLiteral("会议服务器启动失败，可能端口被占用"));
    }
}

void MainWindow::on_btnJoinRoom_clicked()
{
    appendLogMessage(QStringLiteral("用户点击加入会议"));

    if (meetingRole == MeetingRole::Host) {
        QMessageBox::information(this,
                                 QStringLiteral("加入会议"),
                                 QStringLiteral("当前已作为主持人创建会议，请先离开当前会议再加入其他会议。"));
        appendLogMessage(QStringLiteral("加入会议被拒绝：当前处于主持人状态"));
        return;
    }

    if (meetingState == MeetingState::Connecting || meetingState == MeetingState::InMeeting) {
        QMessageBox::information(this,
                                 QStringLiteral("加入会议"),
                                 QStringLiteral("当前已经在加入或通话中，无法重复加入。"));
        appendLogMessage(QStringLiteral("加入会议被拒绝：当前正在连接或已在会议中"));
        return;
    }

    bool ok = false;
    const QString ip = QInputDialog::getText(this,
                                             QStringLiteral("加入会议"),
                                             QStringLiteral("请输入主机 IP 地址:"),
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
    }

    if (!client) {
        client = new ControlClient(this);

        connect(client, &ControlClient::errorOccurred, this, [this](const QString &msg) {
            QMessageBox::critical(this,
                                  QStringLiteral("加入会议失败"),
                                  QStringLiteral("控制连接出现错误：\n%1").arg(msg));
            appendLogMessage(QStringLiteral("控制连接错误：%1").arg(msg));
            resetMeetingState();
        });

        // 与服务器握手成功（JOIN/OK 完成），进入“房间选择”阶段
        connect(client, &ControlClient::joined, this, [this]() {
            statusBar()->showMessage(QStringLiteral("已连接到会议服务器，可以选择房间"), 3000);
            appendLogMessage(QStringLiteral("控制连接握手成功，进入房间选择阶段"));

            // 自动显示侧边栏并切换到“房间”选项卡
            if (btnToggleSidePanel && ui->sidePanel) {
                btnToggleSidePanel->setChecked(true);
                ui->sidePanel->setVisible(true);
            }
            if (ui->sideTabWidget && roomTab) {
                const int index = ui->sideTabWidget->indexOf(roomTab);
                if (index >= 0) {
                    ui->sideTabWidget->setCurrentIndex(index);
                }
            }

            // 自动请求一次房间列表，方便用户直接选择
            if (client) {
                client->requestRoomList();
            }

            updateMeetingStatusLabel();
            updateControlsForMeetingState();
        });

        // 成功创建房间：提示并默认自动加入该房间
        connect(client, &ControlClient::roomCreated, this, [this](const QString &roomId) {
            appendLogMessage(QStringLiteral("房间创建成功：%1").arg(roomId));
            QMessageBox::information(this,
                                     QStringLiteral("房间"),
                                     QStringLiteral("房间 \"%1\" 创建成功，将自动加入该房间。").arg(roomId));

            if (client) {
                client->joinRoom(roomId);
            }
        });

        // 成功加入房间：切换到已有的一对一会议逻辑
        connect(client, &ControlClient::roomJoined, this, [this](const QString &roomId) {
            currentRoomId = roomId;

            statusBar()->showMessage(QStringLiteral("已加入房间：%1").arg(roomId), 3000);

            meetingRole = MeetingRole::Guest;
            meetingState = MeetingState::InMeeting;

            participantNames.clear();
            participantNames << QStringLiteral("我") << QStringLiteral("主持人");
            refreshParticipantListView();

            appendChatMessage(QStringLiteral("系统"),
                              QStringLiteral("你已加入房间 %1").arg(roomId),
                              false);

            appendLogMessage(QStringLiteral("房间加入成功：%1，启动音视频通道").arg(roomId));
            startClientMediaTransports();
        });

        // 收到房间列表结果：刷新房间列表界面
        connect(client,
                &ControlClient::roomListReceived,
                this,
                [this](const QStringList &rooms) {
                    if (roomListWidget) {
                        roomListWidget->clear();
                        for (const QString &roomId : rooms) {
                            roomListWidget->addItem(roomId);
                        }
                    }

                    appendLogMessage(QStringLiteral("收到房间列表，共 %1 个房间").arg(rooms.size()));
                });

        // 协议级错误（如房间不存在、已存在等）
        connect(client,
                &ControlClient::protocolErrorReceived,
                this,
                [this](const QString &message) {
                    QMessageBox::warning(this,
                                         QStringLiteral("房间错误"),
                                         message);
                    appendLogMessage(QStringLiteral("房间协议错误：%1").arg(message));
                });

        connect(client, &ControlClient::disconnected, this, [this]() {
            appendChatMessage(QStringLiteral("系统"),
                              QStringLiteral("与主持人的连接已断开"),
                              false);
            appendLogMessage(QStringLiteral("检测到控制连接断开，重置会议状态"));
            resetMeetingState();
        });

        connect(client, &ControlClient::chatReceived, this, [this](const QString &msg) {
            appendChatMessage(QStringLiteral("对方"), msg, false);
        });
    }

    client->connectToHost(ip, Config::CONTROL_PORT);
    meetingRole = MeetingRole::Guest;
    meetingState = MeetingState::Connecting;

    statusBar()->showMessage(QStringLiteral("正在加入会议..."), 3000);
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
                              QStringLiteral("离开会议"),
                              QStringLiteral("确定要离开当前会议吗？"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::Yes);
    if (reply != QMessageBox::Yes) {
        appendLogMessage(QStringLiteral("离开会议操作被用户取消"));
        return;
    }

    appendChatMessage(QStringLiteral("系统"),
                      QStringLiteral("你已离开会议"),
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
                                 QStringLiteral("聊天"),
                                 QStringLiteral("当前不在会议中，无法发送聊天消息。"));
        appendLogMessage(QStringLiteral("尝试在非会议状态发送聊天消息：%1").arg(text));
        return;
    }

    ui->chatLineEdit->clear();

    appendChatMessage(QStringLiteral("我"), text, true);

    if (meetingRole == MeetingRole::Host && server) {
        server->sendChatToAll(text);
    } else if (meetingRole == MeetingRole::Guest && client) {
        client->sendChatMessage(text);
    }
}
