#ifndef MEDIAENGINE_H
#define MEDIAENGINE_H

#include <QObject>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoWidget>

class MediaEngine : public QObject
{
    Q_OBJECT
public:
    explicit MediaEngine(QObject *parent = nullptr);
    ~MediaEngine();

    QWidget *createPreviewWidget();
    bool startCamera();
    void stopCamera();

private:
    QCamera *camera;
    QMediaCaptureSession captureSession;
    QVideoWidget *videoWidget;
};

#endif // MEDIAENGINE_H

