#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QInputDialog>
#include <QVBoxLayout>

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

    media->startCamera();

    audio = new AudioEngine(this);
    audio->startCapture();
    audio->startPlayback();

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
    if (videoNet) {
        videoNet->startTransport(7000, QStringLiteral("<client-ip-placeholder>"), 7001);
    }

    if (audioNet) {
        audioNet->startTransport(6000, QStringLiteral("<client-ip-placeholder>"), 6001);
    }

    if (!server) {
        server = new ControlServer(this);
    }

    if (server->startServer()) {
        QMessageBox::information(this, "创建会议", "会议已创建，等待其他客户端加入...");
    } else {
        QMessageBox::warning(this, "创建会议", "无法创建会议服务器。");
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
    }

    client->connectToHost(ip);

    if (audioNet) {
        audioNet->startTransport(6001, ip, 6000);
    }

    if (videoNet) {
        videoNet->startTransport(7001, ip, 7000);
    }

    QMessageBox::information(this, "加入会议", "正在加入会议...");
}
