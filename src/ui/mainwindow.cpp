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
    , controlBarHideTimer(nullptr)
    , controlsContainer(nullptr)
    , isDraggingPreview(false)
    , previewDragStartPos()
    , previewStartPos()
{
    ui->setupUi(this);

    initLayout();
    initSidePanel();
    initPreviewWindow();
    initFloatingControls();

    statusLabel = new QLabel(this);
    statusLabel->setText(QStringLiteral("未连接"));
    statusBar()->addPermanentWidget(statusLabel);

    // 本地预览
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

    // 音频初始化
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

    // 远端视频
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
}

MainWindow::~MainWindow()
{
    delete media;
    delete videoNet;
    delete audioNet;
    delete audio;
    delete server;
    delete client;
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

    // 侧边栏中文标签
    if (ui->sideTabWidget) {
        const int chatIndex = ui->sideTabWidget->indexOf(ui->chatTab);
        if (chatIndex >= 0) {
            ui->sideTabWidget->setTabText(chatIndex, QStringLiteral("聊天"));
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
    controlBar->setObjectName("floatingControlBar");
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

    auto *btnCreate = new QToolButton(controlsContainer);
    btnCreate->setText(QStringLiteral("创建"));
    controlsLayout->addWidget(btnCreate);
    connect(btnCreate, &QAbstractButton::clicked, this, &MainWindow::on_btnCreateRoom_clicked);

    auto *btnJoin = new QToolButton(controlsContainer);
    btnJoin->setText(QStringLiteral("加入"));
    controlsLayout->addWidget(btnJoin);
    connect(btnJoin, &QAbstractButton::clicked, this, &MainWindow::on_btnJoinRoom_clicked);

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
    if (!server) {
        server = new ControlServer(this);

        connect(server, &ControlServer::clientJoined, this, [this](const QString &ip) {
            if (audioNet && !audioNet->startTransport(Config::AUDIO_PORT_SEND, ip, Config::AUDIO_PORT_RECV)) {
                QMessageBox::critical(this,
                                      QStringLiteral("音频错误"),
                                      QStringLiteral("无法建立音频网络通道（端口可能被占用）。"));
            }
            if (videoNet && !videoNet->startTransport(Config::VIDEO_PORT_SEND, ip, Config::VIDEO_PORT_RECV)) {
                QMessageBox::critical(this,
                                      QStringLiteral("视频错误"),
                                      QStringLiteral("无法建立视频网络通道（端口可能被占用）。"));
            }
        });
    }

    if (server->startServer()) {
        QMessageBox::information(this,
                                 QStringLiteral("创建会议"),
                                 QStringLiteral("会议已创建，等待其他客户端加入..."));
        if (statusLabel) {
            statusLabel->setText(QStringLiteral("已创建会议，等待对端加入…"));
        }
    } else {
        QMessageBox::critical(this,
                              QStringLiteral("创建会议"),
                              QStringLiteral("无法创建会议服务器（端口可能被占用）。"));
        if (statusLabel) {
            statusLabel->setText(QStringLiteral("连接已断开"));
        }
    }
}

void MainWindow::on_btnJoinRoom_clicked()
{
    bool ok = false;
    const QString ip = QInputDialog::getText(this,
                                             QStringLiteral("加入会议"),
                                             QStringLiteral("请输入主机 IP 地址:"),
                                             QLineEdit::Normal,
                                             QStringLiteral("127.0.0.1"),
                                             &ok);
    if (!ok || ip.isEmpty()) {
        return;
    }

    if (!client) {
        client = new ControlClient(this);

        connect(client, &ControlClient::errorOccurred, this, [this](const QString &msg) {
            QMessageBox::critical(this,
                                  QStringLiteral("加入会议失败"),
                                  QStringLiteral("控制连接出现错误：\n%1").arg(msg));
            if (statusLabel) {
                statusLabel->setText(QStringLiteral("连接已断开"));
            }
        });

        connect(client, &ControlClient::joined, this, [this]() {
            statusBar()->showMessage(QStringLiteral("已成功加入会议。"), 3000);
            if (statusLabel) {
                statusLabel->setText(QStringLiteral("已加入会议，正在通话"));
            }
        });
    }

    client->connectToHost(ip, Config::CONTROL_PORT);

    if (audioNet && !audioNet->startTransport(Config::AUDIO_PORT_RECV, ip, Config::AUDIO_PORT_SEND)) {
        QMessageBox::critical(this,
                              QStringLiteral("音频错误"),
                              QStringLiteral("无法建立音频网络通道（端口可能被占用）。"));
    }

    if (videoNet && !videoNet->startTransport(Config::VIDEO_PORT_RECV, ip, Config::VIDEO_PORT_SEND)) {
        QMessageBox::critical(this,
                              QStringLiteral("视频错误"),
                              QStringLiteral("无法建立视频网络通道（端口可能被占用）。"));
    }

    statusBar()->showMessage(QStringLiteral("正在加入会议..."), 3000);
    if (statusLabel) {
        statusLabel->setText(QStringLiteral("正在加入会议..."));
    }
}

