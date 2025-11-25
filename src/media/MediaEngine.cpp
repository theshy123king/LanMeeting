#include "MediaEngine.h"

#include <QCameraFormat>
#include <QPixmap>
#include <QList>
#include <QSize>

#ifdef USE_FFMPEG_H264
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}
#endif

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

    const QList<QCameraFormat> formats = camera->cameraDevice().videoFormats();
    QCameraFormat bestFormat;
    int bestScore = -1;
    QCameraFormat bestAbove720p;
    int bestAbove720pScore = -1;
    for (const QCameraFormat &fmt : formats) {
        const QSize res = fmt.resolution();
        if (res.width() <= 0 || res.height() <= 0) {
            continue;
        }
        const qint64 score = qint64(res.width()) * qint64(res.height());
        if (res.width() >= 1280 && res.height() >= 720) {
            if (score > bestAbove720pScore) {
                bestAbove720pScore = int(score);
                bestAbove720p = fmt;
            }
        }
        if (score > bestScore) {
            bestScore = int(score);
            bestFormat = fmt;
        }
    }
    const QCameraFormat chosen = (bestAbove720pScore > 0) ? bestAbove720p : bestFormat;
    if (chosen.resolution().isValid()) {
        camera->setCameraFormat(chosen);
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

#ifdef USE_FFMPEG_H264
bool MediaEngine::prepareFrameForEncode(int targetWidth, int targetHeight, AVPixelFormat pixelFormat, AVFrame *&outFrame)
{
    outFrame = nullptr;
    if (targetWidth <= 0 || targetHeight <= 0) {
        return false;
    }

    const QImage image = convertFrame(lastFrame);
    if (image.isNull()) {
        return false;
    }

    QImage rgbaFrame = image.convertToFormat(QImage::Format_RGBA8888);
    if (rgbaFrame.isNull()) {
        return false;
    }

    const QSize sourceSize = rgbaFrame.size();
    QSize targetSize = sourceSize;
    const QSize requestedSize(targetWidth, targetHeight);
    if (sourceSize.isValid() && requestedSize.isValid()) {
        targetSize = sourceSize.scaled(requestedSize, Qt::KeepAspectRatio);
    }
    targetSize.setWidth(qMin(targetSize.width(), sourceSize.width()));
    targetSize.setHeight(qMin(targetSize.height(), sourceSize.height()));

    // Ensure dimensions are even for YUV420P.
    if (targetSize.width() % 2 != 0) {
        targetSize.rwidth() -= 1;
    }
    if (targetSize.height() % 2 != 0) {
        targetSize.rheight() -= 1;
    }

    if (targetSize.width() <= 0 || targetSize.height() <= 0) {
        return false;
    }

    AVFrame *srcFrame = av_frame_alloc();
    if (!srcFrame) {
        return false;
    }

    srcFrame->format = AV_PIX_FMT_RGBA;
    srcFrame->width = sourceSize.width();
    srcFrame->height = sourceSize.height();
    srcFrame->data[0] = const_cast<uint8_t *>(rgbaFrame.constBits());
    srcFrame->linesize[0] = rgbaFrame.bytesPerLine();

    AVFrame *dstFrame = av_frame_alloc();
    if (!dstFrame) {
        av_frame_free(&srcFrame);
        return false;
    }

    dstFrame->format = pixelFormat;
    dstFrame->width = targetSize.width();
    dstFrame->height = targetSize.height();

    if (av_frame_get_buffer(dstFrame, 32) < 0) {
        av_frame_free(&srcFrame);
        av_frame_free(&dstFrame);
        return false;
    }

    SwsContext *swsCtx = sws_getContext(srcFrame->width,
                                        srcFrame->height,
                                        AV_PIX_FMT_RGBA,
                                        dstFrame->width,
                                        dstFrame->height,
                                        pixelFormat,
                                        SWS_BILINEAR,
                                        nullptr,
                                        nullptr,
                                        nullptr);
    if (!swsCtx) {
        av_frame_free(&srcFrame);
        av_frame_free(&dstFrame);
        return false;
    }

    sws_scale(swsCtx,
              srcFrame->data,
              srcFrame->linesize,
              0,
              srcFrame->height,
              dstFrame->data,
              dstFrame->linesize);

    sws_freeContext(swsCtx);
    av_frame_free(&srcFrame);

    outFrame = dstFrame;
    return true;
}
#endif
