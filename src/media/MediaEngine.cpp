#include "MediaEngine.h"

MediaEngine::MediaEngine(QObject *parent)
    : QObject(parent)
    , camera(nullptr)
    , videoWidget(nullptr)
    , videoSink(new QVideoSink(this))
{
    connect(videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        lastFrame = frame;
    });
}

MediaEngine::~MediaEngine()
{
    stopCamera();
    delete camera;
    delete videoWidget;
}

QWidget *MediaEngine::createPreviewWidget()
{
    if (!videoWidget) {
        videoWidget = new QVideoWidget;
    }
    return videoWidget;
}

bool MediaEngine::startCamera()
{
    if (!camera) {
        camera = new QCamera(this);
    }

    if (!videoWidget) {
        videoWidget = new QVideoWidget;
    }

    captureSession.setCamera(camera);
    captureSession.setVideoOutput(videoSink);

    camera->start();
    return camera->isActive();
}

void MediaEngine::stopCamera()
{
    if (camera && camera->isActive()) {
        camera->stop();
    }
}

QImage MediaEngine::convertFrame(const QVideoFrame &frame)
{
    if (!frame.isValid()) {
        return QImage();
    }

    QVideoFrame copyFrame(frame);
    QImage image = copyFrame.toImage();
    if (image.isNull()) {
        return QImage();
    }

    return image;
}

QImage MediaEngine::getCurrentFrame()
{
    return convertFrame(lastFrame);
}
