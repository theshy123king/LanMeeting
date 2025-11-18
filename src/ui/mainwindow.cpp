#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QInputDialog>
#include <QVBoxLayout>

#include "common/Config.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , server(nullptr)
    , client(nullptr)
    , media(nullptr)
    , audio(nullptr)
    , audioNet(nullptr)
    , videoNet(nullptr)
{
    ui->setupUi(this);

    statusLabel = new QLabel(this);
    statusLabel->setText("未连接");
    statusBar()->addPermanentWidget(statusLabel);

    media = new MediaEngine(this);
    QWidget *preview = media->createPreviewWidget();

    if (ui->videoContainer && preview) {
        auto *layout = qobject_cast<QVBoxLayout *>(ui->videoContainer->layout());
        if (!layout) {
            layout = new QVBoxLayout(ui->videoContainer);
            ui->videoContainer->setLayout(layout);
        }
        layout->addWidget(preview);
    }

    if (!media->startCamera()) {
        QMessageBox::warning(this, "视频错误", "无法启动摄像头，请检查设备权限或占用情况。");
    }

    audio = new AudioEngine(this);
    if (!audio->startCapture()) {
        QMessageBox::warning(this, "音频错误", "无法启动麦克风采集，请检查设备权限或占用情况。");
    }
    if (!audio->startPlayback()) {
        QMessageBox::warning(this, "音频错误", "无法启动扬声器播放，请检查音频设备配置。");
    }

    audioNet = new AudioTransport(audio, this);

    videoNet = new MediaTransport(media, this);
    QWidget *remoteView = videoNet->getRemoteVideoWidget();
    if (ui->remoteVideoContainer && remoteView) {
        auto *remoteLayout = qobject_cast<QVBoxLayout *>(ui->remoteVideoContainer->layout());
        if (!remoteLayout) {
            remoteLayout = new QVBoxLayout(ui->remoteVideoContainer);
            ui->remoteVideoContainer->setLayout(remoteLayout);
        }
        remoteLayout->addWidget(remoteView);
    }
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

void MainWindow::on_btnCreateRoom_clicked()
{
    if (!server) {
        server = new ControlServer(this);

        connect(server, &ControlServer::clientJoined, this, [this](const QString &ip) {
            if (audioNet && !audioNet->startTransport(Config::AUDIO_PORT_SEND, ip, Config::AUDIO_PORT_RECV)) {
                QMessageBox::critical(this, "音频错误", "无法建立音频网络通道（端口可能被占用）。");
            }
            if (videoNet && !videoNet->startTransport(Config::VIDEO_PORT_SEND, ip, Config::VIDEO_PORT_RECV)) {
                QMessageBox::critical(this, "视频错误", "无法建立视频网络通道（端口可能被占用）。");
            }
        });
    }

    if (server->startServer()) {
        QMessageBox::information(this, "创建会议", "会议已创建，等待其他客户端加入...");
        statusLabel->setText("已创建会议，等待对端加入…");
    } else {
        QMessageBox::critical(this, "创建会议", "无法创建会议服务器（端口可能被占用）。");
        statusLabel->setText("连接已断开");
    }
}

void MainWindow::on_btnJoinRoom_clicked()
{
    bool ok = false;
    const QString ip = QInputDialog::getText(this,
                                             "加入会议",
                                             "请输入主机 IP 地址:",
                                             QLineEdit::Normal,
                                             "127.0.0.1",
                                             &ok);
    if (!ok || ip.isEmpty()) {
        return;
    }

    if (!client) {
        client = new ControlClient(this);

        connect(client, &ControlClient::errorOccurred, this, [this](const QString &msg) {
            QMessageBox::critical(this, "加入会议失败", "控制连接出现错误：\n" + msg);
            statusLabel->setText("连接已断开");
        });

        connect(client, &ControlClient::joined, this, [this]() {
            statusBar()->showMessage("已成功加入会议。", 3000);
            statusLabel->setText("已加入会议，正在通话");
        });
    }

    client->connectToHost(ip, Config::CONTROL_PORT);

    if (audioNet && !audioNet->startTransport(Config::AUDIO_PORT_RECV, ip, Config::AUDIO_PORT_SEND)) {
        QMessageBox::critical(this, "音频错误", "无法建立音频网络通道（端口可能被占用）。");
    }

    if (videoNet && !videoNet->startTransport(Config::VIDEO_PORT_RECV, ip, Config::VIDEO_PORT_SEND)) {
        QMessageBox::critical(this, "视频错误", "无法建立视频网络通道（端口可能被占用）。");
    }

    statusBar()->showMessage("正在加入会议...", 3000);
    statusLabel->setText("正在加入会议...");
}
