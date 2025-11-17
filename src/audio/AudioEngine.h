#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QAudioFormat>
#include <QAudioSource>
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>

class AudioEngine : public QObject
{
    Q_OBJECT

public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    bool startCapture();
    void stopCapture();

    bool startPlayback();
    void stopPlayback();

    QByteArray readCapturedAudio();
    void playAudio(const QByteArray &data);

private:
    QAudioSource *audioSource;
    QAudioSink *audioSink;
    QIODevice *inputDevice;
    QIODevice *outputDevice;
    QAudioFormat format;
};

#endif // AUDIOENGINE_H

