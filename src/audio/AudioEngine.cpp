#include "AudioEngine.h"

#include "common/Logger.h"

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , audioSource(nullptr)
    , audioSink(nullptr)
    , inputDevice(nullptr)
    , outputDevice(nullptr)
{
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
}

AudioEngine::~AudioEngine()
{
    stopCapture();
    stopPlayback();
}

bool AudioEngine::startCapture()
{
    if (audioSource) {
        stopCapture();
    }

    audioSource = new QAudioSource(format, this);
    inputDevice = audioSource->start();

    if (!inputDevice) {
        LOG_WARN(QStringLiteral("AudioEngine: failed to start audio capture"));
        return false;
    }

    return true;
}

void AudioEngine::stopCapture()
{
    if (audioSource) {
        audioSource->stop();
        audioSource->deleteLater();
        audioSource = nullptr;
    }
    inputDevice = nullptr;
}

bool AudioEngine::startPlayback()
{
    if (audioSink) {
        stopPlayback();
    }

    audioSink = new QAudioSink(format, this);
    outputDevice = audioSink->start();

    if (!outputDevice) {
        LOG_WARN(QStringLiteral("AudioEngine: failed to start audio playback"));
        return false;
    }

    return true;
}

void AudioEngine::stopPlayback()
{
    if (audioSink) {
        audioSink->stop();
        audioSink->deleteLater();
        audioSink = nullptr;
    }
    outputDevice = nullptr;
}

QByteArray AudioEngine::readCapturedAudio()
{
    if (!inputDevice) {
        return QByteArray();
    }

    return inputDevice->readAll();
}

void AudioEngine::playAudio(const QByteArray &data)
{
    if (!outputDevice || data.isEmpty()) {
        return;
    }

    outputDevice->write(data);
}
