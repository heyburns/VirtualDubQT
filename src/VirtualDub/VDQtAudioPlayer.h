#ifndef VDQTAUDIOPLAYER_H
#define VDQTAUDIOPLAYER_H

#include <QString>
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>

#include <QByteArray>
#include <QIODevice>
#include <QMutex>
#include <QWaitCondition>

class QThread;

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <avisynth/avisynth_c.h>
}

class VDQtFFmpegAudioDevice;

class AVSAudioDevice : public QIODevice {
    Q_OBJECT
public:
    AVSAudioDevice(AVS_Clip *clip,
                   const AVS_VideoInfo *vi,
                   int testDecodeDelayMs = 0,
                   QObject *parent = nullptr);
    ~AVSAudioDevice() override;

    bool initialize();
    bool seekToSample(int64_t sample);
    int64_t getCurrentSample() const;
    QString error() const;

    bool isSequential() const override { return true; }
    bool atEnd() const override;
    void close() override;
    qint64 bytesAvailable() const override;
    qint64 size() const override;

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    qint64 bufferedBytesUnlocked() const;
    void compactBufferUnlocked();
    void decodeLoop();
    bool startDecodeThreadAndPrime();
    void stopDecodeThread();

    AVS_Clip *m_clip;
    const AVS_VideoInfo *m_vi;
    QByteArray m_buffer;
    qsizetype m_bufferOffset = 0;
    int64_t m_baseSample = 0;
    int64_t m_producerSample = 0;
    int64_t m_bytesDelivered = 0;
    qint64 m_bufferTargetBytes = 0;
    int m_bytesPerFrame = 0;
    int m_testDecodeDelayMs = 0;
    QThread *m_decodeThread = nullptr;
    bool m_stopProducer = false;
    bool m_producerEof = false;
    bool m_producerFailed = false;
    QString m_error;
    QWaitCondition m_bufferChanged;
    mutable QMutex m_mutex;
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
    void seekToTimeSeconds(double timeSeconds);
    double getCurrentAudioTimeSeconds() const;

    bool isPlaying() const { return mIsPlaying; }
    bool hasAudio() const { return mHasAudio; }
    int getSampleRate() const { return mSampleRate; }
    int getChannels() const { return mChannels; }
    int getBitsPerSample() const { return mBitsPerSample; }
    int64_t getTotalSamples() const { return mTotalSamples; }
    QString getSourcePath() const { return mFilePath; }

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
    bool mTotalSamplesExact;

    QString mFilePath;
    int mAudioStreamIndex;

    AVFormatContext *mFormatCtx;
    AVCodecContext *mCodecCtx;

    QAudioSink *mAudioSink;
    VDQtFFmpegAudioDevice *mFFmpegAudioDevice;
    AVSAudioDevice *mAvsAudioDevice;

    QString mChannelLayoutName;

    AVS_Clip *mClip = nullptr;
    const AVS_VideoInfo *mVi = nullptr;
};

#ifdef VDQT_AUDIO_TESTING
bool VDQtRunAudioBufferRegression(const QString& filePath, QString *errorMessage);
bool VDQtRunAudioDecodeAheadDeadlineRegression(const QString& filePath, QString *errorMessage);
bool VDQtRunAudioGapRegression(const QString& filePath,
                               int64_t gapStartSample,
                               int64_t gapLengthSamples,
                               QString *errorMessage);
bool VDQtRunAvsAudioDecodeAheadDeadlineRegression(AVS_Clip *clip,
                                                  const AVS_VideoInfo *vi,
                                                  QString *errorMessage);
#endif

#endif // VDQTAUDIOPLAYER_H
