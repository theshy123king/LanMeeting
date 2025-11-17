#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>

#include "net/ControlServer.h"
#include "net/ControlClient.h"
#include "media/MediaEngine.h"
#include "audio/AudioEngine.h"
#include "audio/AudioTransport.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_btnCreateRoom_clicked();
    void on_btnJoinRoom_clicked();

private:
    Ui::MainWindow *ui;
    ControlServer *server;
    ControlClient *client;
    MediaEngine *media;
    AudioEngine *audio;
    AudioTransport *audioNet;
};

#endif // MAINWINDOW_H
