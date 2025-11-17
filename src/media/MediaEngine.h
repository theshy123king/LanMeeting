#ifndef MEDIAENGINE_H
#define MEDIAENGINE_H

#include <QObject>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoWidget>
#include <QVideoSink>
#include <QVideoFrame>
#include <QImage>

class MediaEngine : public QObject
{
    Q_OBJECT
public:
    explicit MediaEngine(QObject *parent = nullptr);
    ~MediaEngine();

    QWidget *createPreviewWidget();
    bool startCamera();
    void stopCamera();

    QImage convertFrame(const QVideoFrame &frame);
    QImage getCurrentFrame();

private:
    QCamera *camera;
    QMediaCaptureSession captureSession;
    QVideoWidget *videoWidget;
    QVideoSink *videoSink;
    QVideoFrame lastFrame;
};

#endif // MEDIAENGINE_H
