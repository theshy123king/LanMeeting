#ifndef MEDIAENGINE_H
#define MEDIAENGINE_H

#include <QObject>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QLabel>
#include <QVideoSink>
#include <QVideoFrame>
#include <QImage>
#include <QSize>

#ifdef USE_FFMPEG_H264
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#endif

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

#ifdef USE_FFMPEG_H264
    bool prepareFrameForEncode(int targetWidth, int targetHeight, AVPixelFormat pixelFormat, AVFrame *&outFrame);
#endif

private:
    QCamera *camera;
    QMediaCaptureSession captureSession;
    QLabel *previewLabel;
    QVideoSink *videoSink;
    QVideoFrame lastFrame;
};

#endif // MEDIAENGINE_H
