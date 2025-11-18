#include "MediaEngine.h"

#include <QPixmap>

MediaEngine::MediaEngine(QObject *parent)
    : QObject(parent)
    , camera(nullptr)
    , previewLabel(nullptr)
    , videoSink(new QVideoSink(this))
{
    connect(videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        lastFrame = frame;

        if (previewLabel) {
            const QImage image = convertFrame(frame);
            if (!image.isNull()) {
                previewLabel->setPixmap(QPixmap::fromImage(image).scaled(previewLabel->size(),
                                                                         Qt::KeepAspectRatio,
                                                                         Qt::SmoothTransformation));
            }
        }
    });
}

MediaEngine::~MediaEngine()
{
    stopCamera();
    delete camera;
    delete previewLabel;
}

QWidget *MediaEngine::createPreviewWidget()
{
    if (!previewLabel) {
        previewLabel = new QLabel;
        previewLabel->setAlignment(Qt::AlignCenter);
        previewLabel->setMinimumSize(320, 240);
    }
    return previewLabel;
}

bool MediaEngine::startCamera()
{
    if (!camera) {
        camera = new QCamera(this);
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
