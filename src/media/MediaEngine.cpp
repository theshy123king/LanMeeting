#include "MediaEngine.h"

MediaEngine::MediaEngine(QObject *parent)
    : QObject(parent)
    , camera(nullptr)
    , videoWidget(nullptr)
{
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
    captureSession.setVideoOutput(videoWidget);

    camera->start();
    return camera->isActive();
}

void MediaEngine::stopCamera()
{
    if (camera && camera->isActive()) {
        camera->stop();
    }
}

