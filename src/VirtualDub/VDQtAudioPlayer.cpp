#include "VDQtAudioPlayer.h"
#include "VDQtVideoDecoder.h"
#include <QDebug>
#include <QFile>
#include <algorithm>
#include <cstdint>
#include <QProcess>
#include <QTemporaryFile>

AVSAudioDevice::AVSAudioDevice(AVS_Clip *clip, const AVS_VideoInfo *vi, QObject *parent)
    : QIODevice(parent)
    , m_clip(clip)
    , m_vi(vi)
    , m_currentSample(0)
{
    open(QIODevice::ReadOnly);
}

void AVSAudioDevice::setClip(AVS_Clip *clip, const AVS_VideoInfo *vi) {
    m_clip = clip;
    m_vi = vi;
    m_currentSample = 0;
}

void AVSAudioDevice::seekToSample(int64_t sample) {
    m_currentSample = sample;
}

qint64 AVSAudioDevice::bytesAvailable() const {
    if (!m_clip || !m_vi || !avs_has_audio(m_vi)) return 0;
    int64_t remainingSamples = m_vi->num_audio_samples - m_currentSample;
    if (remainingSamples <= 0) return 0;
    int bytesPerSample = m_vi->nchannels * 2;
    return (qint64)remainingSamples * bytesPerSample + QIODevice::bytesAvailable();
}

qint64 AVSAudioDevice::size() const {
    if (!m_clip || !m_vi || !avs_has_audio(m_vi)) return 0;
    int bytesPerSample = m_vi->nchannels * 2;
    return (qint64)m_vi->num_audio_samples * bytesPerSample;
}

qint64 AVSAudioDevice::readData(char *data, qint64 maxlen) {
    if (!m_clip || !m_vi || !avs_has_audio(m_vi)) return 0;

    int channels = m_vi->nchannels;
    int bytesPerSample = channels * 2; // Outputting 16-bit Int16
    int64_t samplesRequested = maxlen / bytesPerSample;

    if (samplesRequested <= 0) return 0;

    int64_t remainingSamples = m_vi->num_audio_samples - m_currentSample;
    if (remainingSamples <= 0) return 0;

    int64_t samplesToRead = std::min(samplesRequested, remainingSamples);

    if (m_vi->sample_type == AVS_SAMPLE_INT16) {
        int err = avs_get_audio(m_clip, data, m_currentSample, samplesToRead);
        if (err != 0) return 0;
    } else if (m_vi->sample_type == AVS_SAMPLE_FLOAT) {
        std::vector<float> floatBuffer(samplesToRead * channels);
        int err = avs_get_audio(m_clip, floatBuffer.data(), m_currentSample, samplesToRead);
        if (err != 0) return 0;

        int16_t *outPtr = reinterpret_cast<int16_t *>(data);
        for (size_t i = 0; i < floatBuffer.size(); ++i) {
            float val = floatBuffer[i];
            int s = static_cast<int>(val * 32767.0f);
            outPtr[i] = static_cast<int16_t>(std::clamp(s, -32768, 32767));
        }
    } else if (m_vi->sample_type == AVS_SAMPLE_INT32) {
        std::vector<int32_t> int32Buffer(samplesToRead * channels);
        int err = avs_get_audio(m_clip, int32Buffer.data(), m_currentSample, samplesToRead);
        if (err != 0) return 0;

        int16_t *outPtr = reinterpret_cast<int16_t *>(data);
        for (size_t i = 0; i < int32Buffer.size(); ++i) {
            outPtr[i] = static_cast<int16_t>(int32Buffer[i] >> 16);
        }
    } else if (m_vi->sample_type == AVS_SAMPLE_INT24) {
        std::vector<uint8_t> int24Buffer(samplesToRead * channels * 3);
        int err = avs_get_audio(m_clip, int24Buffer.data(), m_currentSample, samplesToRead);
        if (err != 0) return 0;

        int16_t *outPtr = reinterpret_cast<int16_t *>(data);
        for (int64_t i = 0; i < samplesToRead * channels; ++i) {
            int32_t val = (int32_t(int24Buffer[i * 3 + 0]) << 8) |
                          (int32_t(int24Buffer[i * 3 + 1]) << 16) |
                          (int32_t((int8_t)int24Buffer[i * 3 + 2]) << 24);
            outPtr[i] = static_cast<int16_t>(val >> 16);
        }
    } else if (m_vi->sample_type == AVS_SAMPLE_INT8) {
        std::vector<uint8_t> int8Buffer(samplesToRead * channels);
        int err = avs_get_audio(m_clip, int8Buffer.data(), m_currentSample, samplesToRead);
        if (err != 0) return 0;

        int16_t *outPtr = reinterpret_cast<int16_t *>(data);
        for (size_t i = 0; i < int8Buffer.size(); ++i) {
            outPtr[i] = static_cast<int16_t>((int(int8Buffer[i]) - 128) << 8);
        }
    } else {
        int bytesPerChan = avs_bytes_per_audio_sample(m_vi) / channels;
        std::vector<char> rawBuf(samplesToRead * channels * bytesPerChan);
        int err = avs_get_audio(m_clip, rawBuf.data(), m_currentSample, samplesToRead);
        if (err != 0) return 0;
        memcpy(data, rawBuf.data(), std::min(maxlen, (qint64)rawBuf.size()));
    }

    m_currentSample += samplesToRead;
    return samplesToRead * bytesPerSample;
}

qint64 AVSAudioDevice::writeData(const char *data, qint64 len) {
    Q_UNUSED(data);
    Q_UNUSED(len);
    return 0;
}

VDQtAudioPlayer::VDQtAudioPlayer()
    : mIsOpen(false),
      mHasAudio(false),
      mIsPlaying(false),
      mIsAvsAudio(false),
      mSampleRate(48000),
      mChannels(2),
      mBitsPerSample(16),
      mTotalSamples(0),
      mAudioStreamIndex(-1),
      mFormatCtx(nullptr),
      mCodecCtx(nullptr),
      mSwrCtx(nullptr),
      mAudioSink(nullptr),
      mAvsAudioDevice(nullptr),
      mClip(nullptr),
      mVi(nullptr) {
}

VDQtAudioPlayer::~VDQtAudioPlayer() {
    close();
}

bool VDQtAudioPlayer::openAvsClip(AVS_Clip *clip, const AVS_VideoInfo *vi) {
    close();
    if (!clip || !vi || !avs_has_audio(vi)) return false;

    mIsAvsAudio = true;
    mIsOpen = true;
    mHasAudio = true;
    mClip = clip;
    mVi = vi;
    mSampleRate = vi->audio_samples_per_second;
    mChannels = vi->nchannels;
    mBitsPerSample = 16;
    mTotalSamples = vi->num_audio_samples;

    QAudioFormat format;
    format.setSampleRate(mSampleRate);
    format.setChannelCount(vi->nchannels);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    if (!defaultDevice.isNull()) {
        mAvsAudioDevice = new AVSAudioDevice(clip, vi);
        mAudioSink = new QAudioSink(defaultDevice, format);
    } else {
        qWarning() << "[VDQtAudioPlayer] No audio output device available for live playback, but audio stream is active for processing & export.";
    }

    qDebug() << "[VDQtAudioPlayer] Loaded native AviSynth audio stream:"
             << mSampleRate << "Hz,"
             << vi->nchannels << "channels,"
             << vi->num_audio_samples << "total samples.";
    return true;
}

bool VDQtAudioPlayer::openFile(const QString& filePath) {
    close();

    mFilePath = filePath;
    std::string pathStr = filePath.toStdString();

    bool isScript = filePath.endsWith(".avs", Qt::CaseInsensitive) || filePath.endsWith(".vpy", Qt::CaseInsensitive);
    AVInputFormat *inputFmt = nullptr;

    if (isScript) {
        inputFmt = const_cast<AVInputFormat*>(av_find_input_format(filePath.endsWith(".avs", Qt::CaseInsensitive) ? "avisynth" : "vapoursynth"));
        if (!inputFmt) {
            QString resolvedMedia = VDQtVideoDecoder::parseScriptSource(filePath);
            if (!resolvedMedia.isEmpty() && QFile::exists(resolvedMedia)) {
                return openFile(resolvedMedia);
            }
            mIsOpen = true;
            mHasAudio = false;
            return true;
        }
    }

    if (avformat_open_input(&mFormatCtx, pathStr.c_str(), inputFmt, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(mFormatCtx, nullptr) < 0) {
        close();
        return false;
    }

    for (unsigned int i = 0; i < mFormatCtx->nb_streams; i++) {
        if (mFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            mAudioStreamIndex = i;
            break;
        }
    }

    if (mAudioStreamIndex == -1) {
        qDebug() << "[VDQtAudioPlayer] No audio stream found in:" << filePath;
        mIsOpen = true;
        mHasAudio = false;
        return true;
    }

    AVStream *audioStream = mFormatCtx->streams[mAudioStreamIndex];
    const AVCodec *codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
    if (!codec) {
        mIsOpen = true;
        mHasAudio = false;
        return false;
    }

    mCodecCtx = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(mCodecCtx, audioStream->codecpar) < 0) {
        close();
        return false;
    }

    if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
        close();
        return false;
    }

    int targetSampleRate = (mCodecCtx->sample_rate > 0) ? mCodecCtx->sample_rate : 48000;
    mSampleRate = targetSampleRate;
    mChannels = 2;
    mBitsPerSample = 16;

    mSwrCtx = swr_alloc();

    AVChannelLayout outChannelLayout;
    av_channel_layout_default(&outChannelLayout, 2);

    AVChannelLayout inChannelLayout = mCodecCtx->ch_layout;
    if (inChannelLayout.nb_channels <= 0 || inChannelLayout.order == AV_CHANNEL_ORDER_UNSPEC) {
        av_channel_layout_default(&inChannelLayout, mCodecCtx->ch_layout.nb_channels > 0 ? mCodecCtx->ch_layout.nb_channels : 2);
    }

    swr_alloc_set_opts2(
        &mSwrCtx,
        &outChannelLayout,
        AV_SAMPLE_FMT_S16,
        targetSampleRate,
        &inChannelLayout,
        mCodecCtx->sample_fmt,
        mCodecCtx->sample_rate,
        0,
        nullptr
    );

    swr_init(mSwrCtx);

    AVPacket packet;
    AVFrame *frame = av_frame_alloc();

    mPcmData.clear();

    while (av_read_frame(mFormatCtx, &packet) >= 0) {
        if (packet.stream_index == mAudioStreamIndex) {
            if (avcodec_send_packet(mCodecCtx, &packet) >= 0) {
                while (avcodec_receive_frame(mCodecCtx, frame) >= 0) {
                    int maxDstSamples = av_rescale_rnd(
                        swr_get_delay(mSwrCtx, mCodecCtx->sample_rate) + frame->nb_samples,
                        targetSampleRate,
                        mCodecCtx->sample_rate,
                        AV_ROUND_UP
                    ) + 256;

                    uint8_t *outBuffer = nullptr;
                    int linesize = 0;
                    if (av_samples_alloc(&outBuffer, &linesize, 2, maxDstSamples, AV_SAMPLE_FMT_S16, 0) >= 0) {
                        int outSamples = swr_convert(
                            mSwrCtx,
                            &outBuffer,
                            maxDstSamples,
                            (const uint8_t **)frame->data,
                            frame->nb_samples
                        );

                        if (outSamples > 0) {
                            int bytes = outSamples * 4;
                            mPcmData.append(reinterpret_cast<const char *>(outBuffer), bytes);
                        }
                        av_freep(&outBuffer);
                    }
                }
            }
        }
        av_packet_unref(&packet);
    }

    uint8_t *flushBuf = nullptr;
    int flushLinesize = 0;
    if (av_samples_alloc(&flushBuf, &flushLinesize, 2, 4096, AV_SAMPLE_FMT_S16, 0) >= 0) {
        int outSamples = swr_convert(mSwrCtx, &flushBuf, 4096, nullptr, 0);
        if (outSamples > 0) {
            mPcmData.append(reinterpret_cast<const char *>(flushBuf), outSamples * 4);
        }
        av_freep(&flushBuf);
    }

    av_frame_free(&frame);

    QAudioFormat format;
    format.setSampleRate(targetSampleRate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice info = QMediaDevices::defaultAudioOutput();
    if (!info.isNull()) {
        mAudioSink = new QAudioSink(info, format);
    }

    mAudioBuffer.setData(mPcmData);
    mAudioBuffer.open(QIODevice::ReadOnly);

    mIsOpen = true;
    mHasAudio = (!mPcmData.isEmpty() || mAudioStreamIndex != -1);
    mTotalSamples = mPcmData.size() / 4;

    qDebug() << "[VDQtAudioPlayer] Loaded audio stream:" << mPcmData.size() << "bytes decoded," << mChannels << "ch @" << mSampleRate << "Hz.";
    return true;
}

void VDQtAudioPlayer::close() {
    mIsPlaying = false;

    if (mAudioSink) {
        mAudioSink->stop();
        mAudioSink->reset();
        delete mAudioSink;
        mAudioSink = nullptr;
    }
    if (mAvsAudioDevice) {
        mAvsAudioDevice->close();
        delete mAvsAudioDevice;
        mAvsAudioDevice = nullptr;
    }
    if (mAudioBuffer.isOpen()) {
        mAudioBuffer.close();
    }
    mPcmData.clear();

    if (mSwrCtx) {
        swr_free(&mSwrCtx);
        mSwrCtx = nullptr;
    }
    if (mCodecCtx) {
        avcodec_free_context(&mCodecCtx);
        mCodecCtx = nullptr;
    }
    if (mFormatCtx) {
        avformat_close_input(&mFormatCtx);
        mFormatCtx = nullptr;
    }

    mIsOpen = false;
    mHasAudio = false;
    mIsAvsAudio = false;
    mClip = nullptr;
    mVi = nullptr;
    mAudioStreamIndex = -1;
}

void VDQtAudioPlayer::play() {
    if (!mHasAudio || !mAudioSink) return;

    if (mAudioSink->state() == QAudio::SuspendedState) {
        mAudioSink->resume();
    } else if (mAudioSink->state() == QAudio::StoppedState) {
        if (mIsAvsAudio && mAvsAudioDevice) {
            mAudioSink->start(mAvsAudioDevice);
        } else if (mAudioBuffer.isOpen()) {
            mAudioSink->start(&mAudioBuffer);
        }
    }
    mIsPlaying = true;
}

void VDQtAudioPlayer::pause() {
    if (!mHasAudio || !mAudioSink) return;

    mAudioSink->suspend();
    mIsPlaying = false;
}

void VDQtAudioPlayer::stop() {
    if (mAudioSink) {
        mAudioSink->stop();
    }
    if (mIsAvsAudio && mAvsAudioDevice) {
        mAvsAudioDevice->seekToSample(0);
    } else if (mAudioBuffer.isOpen()) {
        mAudioBuffer.seek(0);
    }
    mIsPlaying = false;
}

void VDQtAudioPlayer::seekToFrame(int frameIndex, double fps) {
    if (!mHasAudio || fps <= 0 || mSampleRate <= 0) return;

    if (mAudioSink && (mAudioSink->state() == QAudio::ActiveState || mAudioSink->state() == QAudio::SuspendedState)) {
        mAudioSink->stop();
        mAudioSink->reset();
    }

    if (mIsAvsAudio && mAvsAudioDevice) {
        double timeSeconds = frameIndex / fps;
        int64_t sample = static_cast<int64_t>(std::round(timeSeconds * mSampleRate));
        mAvsAudioDevice->seekToSample(sample);
    } else if (mAudioBuffer.isOpen()) {
        double timeSeconds = frameIndex / fps;
        int64_t sampleIndex = static_cast<int64_t>(std::round(timeSeconds * mSampleRate));
        int64_t byteOffset = sampleIndex * 4; 

        int64_t maxBytes = (mPcmData.size() / 4) * 4;
        byteOffset = std::clamp(byteOffset, (int64_t)0, maxBytes);
        mAudioBuffer.seek(byteOffset);
    }
}

double VDQtAudioPlayer::getCurrentAudioTimeSeconds() const {
    if (!mHasAudio || !mAudioSink || mSampleRate <= 0) return -1.0;

    if (mIsAvsAudio && mAvsAudioDevice) {
        int64_t currentSample = mAvsAudioDevice->getCurrentSample();
        return static_cast<double>(currentSample) / mSampleRate;
    } else {
        qint64 byteOffset = mAudioBuffer.pos();
        qint64 samples = byteOffset / 4; // 16-bit stereo = 4 bytes per sample frame
        return static_cast<double>(samples) / mSampleRate;
    }
    return -1.0;
}

QString VDQtAudioPlayer::getAudioLayoutString() const {
    if (!mHasAudio) return "No audio";
    return QString("%1 Hz %2-bit %3 ch")
        .arg(mSampleRate)
        .arg(mBitsPerSample)
        .arg(mChannels);
}

QString VDQtAudioPlayer::getAudioCompressionString() const {
    if (!mHasAudio) return "No audio";
    if (mIsAvsAudio) return "No compression (PCM)";
    if (mCodecCtx && mCodecCtx->codec) {
        const char *name = avcodec_get_name(mCodecCtx->codec_id);
        if (name) {
            QString codecStr = QString::fromUtf8(name).toUpper();
            if (codecStr.startsWith("PCM")) return "No compression (PCM)";
            return codecStr;
        }
    }
    return "No compression (PCM)";
}

bool VDQtAudioPlayer::exportAudioToFile(const QString &outputPath, int64_t startSample, int64_t sampleCount, std::function<bool(int progress, int total)> progressCallback) {
    if (!mHasAudio && mPcmData.isEmpty() && (!mIsAvsAudio || !mClip || !mVi)) {
        return false;
    }

    // 1. If we have native AviSynth+ audio clip
    if (mIsAvsAudio && mClip && mVi) {
        int64_t totalSamples = mVi->num_audio_samples;
        if (totalSamples <= 0) return false;

        startSample = std::clamp(startSample, (int64_t)0, totalSamples);
        int64_t maxAvailable = totalSamples - startSample;
        int64_t samplesToExport = (sampleCount > 0) ? std::min(sampleCount, maxAvailable) : maxAvailable;
        if (samplesToExport <= 0) return false;

        int channels = mVi->nchannels;
        int sampleRate = mVi->audio_samples_per_second;
        int bitsPerSample = 16;
        int bytesPerSample = channels * (bitsPerSample / 8);

        QString tempWavPath = outputPath;
        bool needsTranscode = !outputPath.endsWith(".wav", Qt::CaseInsensitive);
        QTemporaryFile tempFile;
        if (needsTranscode) {
            tempFile.open();
            tempWavPath = tempFile.fileName() + ".wav";
        }

        QFile wavFile(tempWavPath);
        if (!wavFile.open(QIODevice::WriteOnly)) {
            return false;
        }

        // Write placeholder RIFF WAV header (44 bytes)
        QByteArray header(44, 0);
        wavFile.write(header);

        int64_t currentSample = startSample;
        int64_t endSample = startSample + samplesToExport;
        const int chunkSize = 65536;
        int64_t totalDataBytes = 0;

        while (currentSample < endSample) {
            if (progressCallback && !progressCallback((int)(((currentSample - startSample) * 100) / samplesToExport), 100)) {
                wavFile.close();
                wavFile.remove();
                return false;
            }

            int64_t toRead = std::min((int64_t)chunkSize, endSample - currentSample);
            QByteArray pcmChunk(toRead * bytesPerSample, 0);

            if (mVi->sample_type == AVS_SAMPLE_INT16) {
                int err = avs_get_audio(mClip, pcmChunk.data(), currentSample, toRead);
                if (err != 0) break;
            } else if (mVi->sample_type == AVS_SAMPLE_FLOAT) {
                QByteArray floatBuffer(toRead * channels * sizeof(float), 0);
                int err = avs_get_audio(mClip, floatBuffer.data(), currentSample, toRead);
                if (err != 0) break;

                const float *fPtr = reinterpret_cast<const float *>(floatBuffer.constData());
                int16_t *outPtr = reinterpret_cast<int16_t *>(pcmChunk.data());
                int totalValues = toRead * channels;

                for (int i = 0; i < totalValues; ++i) {
                    float val = fPtr[i];
                    int s = (int)(val * 32767.0f);
                    outPtr[i] = (int16_t)std::clamp(s, -32768, 32767);
                }
            } else {
                QByteArray rawBuf(toRead * avs_bytes_per_audio_sample(mVi), 0);
                int err = avs_get_audio(mClip, rawBuf.data(), currentSample, toRead);
                if (err != 0) break;
                memcpy(pcmChunk.data(), rawBuf.constData(), std::min((qint64)pcmChunk.size(), (qint64)rawBuf.size()));
            }

            wavFile.write(pcmChunk);
            totalDataBytes += pcmChunk.size();
            currentSample += toRead;
        }

        // Finalize RIFF WAV Header
        wavFile.seek(0);
        uint32_t riffChunkSize = totalDataBytes + 36;
        uint32_t fmtSubchunkSize = 16;
        uint16_t audioFormat = 1; // PCM
        uint16_t numChannels = channels;
        uint32_t sRate = sampleRate;
        uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
        uint16_t blockAlign = channels * (bitsPerSample / 8);
        uint16_t bps = bitsPerSample;
        uint32_t dataSubchunkSize = totalDataBytes;

        char hbuf[44];
        memcpy(hbuf + 0, "RIFF", 4);
        memcpy(hbuf + 4, &riffChunkSize, 4);
        memcpy(hbuf + 8, "WAVE", 4);
        memcpy(hbuf + 12, "fmt ", 4);
        memcpy(hbuf + 16, &fmtSubchunkSize, 4);
        memcpy(hbuf + 20, &audioFormat, 2);
        memcpy(hbuf + 22, &numChannels, 2);
        memcpy(hbuf + 24, &sRate, 4);
        memcpy(hbuf + 28, &byteRate, 4);
        memcpy(hbuf + 32, &blockAlign, 2);
        memcpy(hbuf + 34, &bps, 2);
        memcpy(hbuf + 36, "data", 4);
        memcpy(hbuf + 40, &dataSubchunkSize, 4);

        wavFile.write(hbuf, 44);
        wavFile.close();

        if (needsTranscode) {
            QStringList args;
            args << "-y" << "-i" << tempWavPath;
            if (outputPath.endsWith(".m4a", Qt::CaseInsensitive)) {
                args << "-c:a" << "aac" << "-b:a" << "256k";
            } else if (outputPath.endsWith(".mka", Qt::CaseInsensitive)) {
                args << "-c:a" << "flac";
            } else if (outputPath.endsWith(".aiff", Qt::CaseInsensitive) || outputPath.endsWith(".aif", Qt::CaseInsensitive)) {
                args << "-c:a" << "pcm_s16be";
            } else {
                args << "-c:a" << "copy";
            }
            args << outputPath;

            QProcess proc;
            proc.start("ffmpeg", args);
            proc.waitForFinished(-1);
            QFile::remove(tempWavPath);
            return proc.exitCode() == 0 && QFile::exists(outputPath) && QFile(outputPath).size() > 0;
        }

        if (progressCallback) progressCallback(100, 100);
        return QFile::exists(outputPath) && QFile(outputPath).size() > 0;
    }

    // 2. If we have decoded in-memory PCM data (44100Hz 16-bit 2ch)
    if (!mPcmData.isEmpty()) {
        QString tempWavPath = outputPath;
        bool needsTranscode = !outputPath.endsWith(".wav", Qt::CaseInsensitive);
        QTemporaryFile tempFile;
        if (needsTranscode) {
            tempFile.open();
            tempWavPath = tempFile.fileName() + ".wav";
        }

        QFile wavFile(tempWavPath);
        if (wavFile.open(QIODevice::WriteOnly)) {
            int64_t totalBytes = mPcmData.size();
            int64_t byteOffset = startSample * 4;
            int64_t bytesToWrite = (sampleCount > 0) ? std::min((int64_t)sampleCount * 4, totalBytes - byteOffset) : (totalBytes - byteOffset);
            if (bytesToWrite <= 0) return false;

            uint32_t totalDataBytes = bytesToWrite;
            uint32_t riffChunkSize = totalDataBytes + 36;
            uint32_t fmtSubchunkSize = 16;
            uint16_t audioFormat = 1; // PCM
            uint16_t numChannels = 2;
            uint32_t sRate = (mSampleRate > 0) ? mSampleRate : 44100;
            uint32_t byteRate = sRate * 2 * 2;
            uint16_t blockAlign = 4;
            uint16_t bps = 16;
            uint32_t dataSubchunkSize = totalDataBytes;

            char hbuf[44];
            memcpy(hbuf + 0, "RIFF", 4);
            memcpy(hbuf + 4, &riffChunkSize, 4);
            memcpy(hbuf + 8, "WAVE", 4);
            memcpy(hbuf + 12, "fmt ", 4);
            memcpy(hbuf + 16, &fmtSubchunkSize, 4);
            memcpy(hbuf + 20, &audioFormat, 2);
            memcpy(hbuf + 22, &numChannels, 2);
            memcpy(hbuf + 24, &sRate, 4);
            memcpy(hbuf + 28, &byteRate, 4);
            memcpy(hbuf + 32, &blockAlign, 2);
            memcpy(hbuf + 34, &bps, 2);
            memcpy(hbuf + 36, "data", 4);
            memcpy(hbuf + 40, &dataSubchunkSize, 4);

            wavFile.write(hbuf, 44);

            int64_t currentOffset = byteOffset;
            int64_t endOffset = byteOffset + bytesToWrite;
            const int chunkSize = 65536;

            while (currentOffset < endOffset) {
                if (progressCallback && !progressCallback((int)(((currentOffset - byteOffset) * 100) / bytesToWrite), 100)) {
                    wavFile.close();
                    wavFile.remove();
                    return false;
                }
                int64_t toWrite = std::min((int64_t)chunkSize, endOffset - currentOffset);
                wavFile.write(mPcmData.constData() + currentOffset, toWrite);
                currentOffset += toWrite;
            }
            wavFile.close();

            if (needsTranscode) {
                QStringList args;
                args << "-y" << "-i" << tempWavPath;
                if (outputPath.endsWith(".m4a", Qt::CaseInsensitive)) {
                    args << "-c:a" << "aac" << "-b:a" << "256k";
                } else if (outputPath.endsWith(".mka", Qt::CaseInsensitive)) {
                    args << "-c:a" << "flac";
                } else if (outputPath.endsWith(".aiff", Qt::CaseInsensitive) || outputPath.endsWith(".aif", Qt::CaseInsensitive)) {
                    args << "-c:a" << "pcm_s16be";
                } else {
                    args << "-c:a" << "copy";
                }
                args << outputPath;

                QProcess proc;
                proc.start("ffmpeg", args);
                proc.waitForFinished(-1);
                QFile::remove(tempWavPath);
                return proc.exitCode() == 0 && QFile::exists(outputPath) && QFile(outputPath).size() > 0;
            }

            if (progressCallback) progressCallback(100, 100);
            return QFile::exists(outputPath) && QFile(outputPath).size() > 0;
        }
    }

    // 3. Fallback via FFmpeg CLI from source file
    if (!mFilePath.isEmpty() && QFile::exists(mFilePath)) {
        QStringList args;
        args << "-y" << "-i" << mFilePath << "-vn";
        if (outputPath.endsWith(".wav", Qt::CaseInsensitive)) {
            args << "-c:a" << "pcm_s16le";
        } else if (outputPath.endsWith(".w64", Qt::CaseInsensitive)) {
            args << "-c:a" << "pcm_s16le";
        } else if (outputPath.endsWith(".m4a", Qt::CaseInsensitive)) {
            args << "-c:a" << "aac" << "-b:a" << "256k";
        } else if (outputPath.endsWith(".mka", Qt::CaseInsensitive)) {
            args << "-c:a" << "copy";
        } else if (outputPath.endsWith(".aiff", Qt::CaseInsensitive)) {
            args << "-c:a" << "pcm_s16be";
        } else {
            args << "-c:a" << "copy";
        }
        args << outputPath;

        QProcess proc;
        proc.start("ffmpeg", args);
        proc.waitForFinished(-1);
        if (progressCallback) progressCallback(100, 100);
        return proc.exitCode() == 0 && QFile::exists(outputPath) && QFile(outputPath).size() > 0;
    }

    return false;
}
