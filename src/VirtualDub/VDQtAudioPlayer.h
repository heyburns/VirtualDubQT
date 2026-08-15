#ifndef VDQTAUDIOPLAYER_H
#define VDQTAUDIOPLAYER_H

#include <QString>
#include <QByteArray>
#include <QBuffer>
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>

#include <QIODevice>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <avisynth/avisynth_c.h>
}

class AVSAudioDevice : public QIODevice {
    Q_OBJECT
public:
    AVSAudioDevice(AVS_Clip *clip, const AVS_VideoInfo *vi, QObject *parent = nullptr);
    void setClip(AVS_Clip *clip, const AVS_VideoInfo *vi);
    void seekToSample(int64_t sample);
    int64_t getCurrentSample() const { return m_currentSample; }

    bool isSequential() const override { return false; }
    qint64 bytesAvailable() const override;
    qint64 size() const override;

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    AVS_Clip *m_clip;
    const AVS_VideoInfo *m_vi;
    int64_t m_currentSample;
};

#include <functional>

class VDQtAudioPlayer {
public:
    VDQtAudioPlayer();
    ~VDQtAudioPlayer();

    bool openFile(const QString& filePath);
    bool openAvsClip(AVS_Clip *clip, const AVS_VideoInfo *vi);
    void close();

    void play();
    void pause();
    void stop();
    void seekToFrame(int frameIndex, double fps);
    double getCurrentAudioTimeSeconds() const;

    bool isPlaying() const { return mIsPlaying; }
    bool hasAudio() const { return mHasAudio; }
    int getSampleRate() const { return mSampleRate; }
    int getChannels() const { return mChannels; }
    int getBitsPerSample() const { return mBitsPerSample; }
    int64_t getTotalSamples() const { return mTotalSamples; }

    QString getAudioLayoutString() const;
    QString getAudioCompressionString() const;
    bool exportAudioToFile(const QString &outputPath, int64_t startSample = 0, int64_t sampleCount = -1, std::function<bool(int progress, int total)> progressCallback = nullptr);

private:
    bool mIsOpen;
    bool mHasAudio;
    bool mIsPlaying;
    bool mIsAvsAudio;
    int mSampleRate;
    int mChannels;
    int mBitsPerSample;
    int64_t mTotalSamples;

    QString mFilePath;
    int mAudioStreamIndex;

    AVFormatContext *mFormatCtx;
    AVCodecContext *mCodecCtx;
    SwrContext *mSwrCtx;

    QAudioSink *mAudioSink;
    QBuffer mAudioBuffer;
    QByteArray mPcmData;
    AVSAudioDevice *mAvsAudioDevice;

    AVS_Clip *mClip = nullptr;
    const AVS_VideoInfo *mVi = nullptr;
};

#endif // VDQTAUDIOPLAYER_H
