#include "VDQtAudioPlayer.h"
#include "VDQtVideoDecoder.h"

#include <QAudioDevice>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QTemporaryFile>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace {

constexpr qint64 kLiveAudioPrebufferUsecs = 750000;
constexpr qint64 kAudioSinkBufferUsecs = 200000;

qint64 alignedAudioBufferSize(const QAudioFormat& format,
                              qint64 durationUsecs,
                              qint64 minimumBytes,
                              qint64 maximumBytes)
{
    const qint64 bytesPerFrame = std::max(1, format.bytesPerFrame());
    qint64 bytes = std::clamp<qint64>(
        format.bytesForDuration(durationUsecs), minimumBytes, maximumBytes);
    bytes -= bytes % bytesPerFrame;
    return std::max(bytesPerFrame, bytes);
}

void configureLiveAudioSink(QAudioSink *sink, const QAudioFormat& format)
{
    if (!sink) return;

    const qint64 requestedBytes = alignedAudioBufferSize(
        format, kAudioSinkBufferUsecs, 32 * 1024, 2 * 1024 * 1024);
    sink->setBufferSize(static_cast<int>(std::min<qint64>(
        requestedBytes, std::numeric_limits<int>::max())));
    QObject::connect(sink, &QAudioSink::stateChanged, sink,
                     [sink](QAudio::State state) {
        if (state == QAudio::IdleState && sink->error() != QAudio::NoError) {
            qWarning() << "[VDQtAudioPlayer] Audio output became idle with error"
                       << static_cast<int>(sink->error())
                       << "; requested sink buffer:" << sink->bufferSize() << "bytes.";
        }
    });
}

QString avErrorString(int errorCode)
{
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, errorBuffer, sizeof(errorBuffer));
    return QString::fromUtf8(errorBuffer);
}

bool copyOrCreateLayout(AVChannelLayout *destination,
                        const AVChannelLayout *source,
                        int fallbackChannels)
{
    if (!destination) return false;

    if (source && source->nb_channels > 0 && source->order != AV_CHANNEL_ORDER_UNSPEC) {
        return av_channel_layout_copy(destination, source) >= 0;
    }

    av_channel_layout_default(destination, std::max(1, fallbackChannels));
    return destination->nb_channels > 0;
}

QString describeLayout(const AVChannelLayout *layout, int channels)
{
    AVChannelLayout fallback = {};
    const AVChannelLayout *layoutToDescribe = layout;
    if (!layoutToDescribe || layoutToDescribe->nb_channels <= 0 ||
        layoutToDescribe->order == AV_CHANNEL_ORDER_UNSPEC) {
        av_channel_layout_default(&fallback, std::max(1, channels));
        layoutToDescribe = &fallback;
    }

    char description[128] = {};
    const int result = av_channel_layout_describe(layoutToDescribe,
                                                   description,
                                                   sizeof(description));
    const QString value = result >= 0 ? QString::fromUtf8(description) : QString();
    av_channel_layout_uninit(&fallback);
    return value;
}

AVSampleFormat qtSampleFormatToAV(QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8: return AV_SAMPLE_FMT_U8;
    case QAudioFormat::Int16: return AV_SAMPLE_FMT_S16;
    case QAudioFormat::Int32: return AV_SAMPLE_FMT_S32;
    case QAudioFormat::Float: return AV_SAMPLE_FMT_FLT;
    default: return AV_SAMPLE_FMT_NONE;
    }
}

bool audioOutputEnabled()
{
    // Device discovery may block in headless/container sessions when host audio
    // sockets are visible but inaccessible. Keep decode/export usable in those
    // environments without changing normal desktop behavior.
    return qEnvironmentVariableIntValue("VD_DISABLE_AUDIO_OUTPUT") == 0;
}

QString stagedOutputTemplate(const QString &outputPath)
{
    const QFileInfo target(outputPath);
    QString baseName = target.completeBaseName();
    if (baseName.isEmpty()) baseName = QStringLiteral("audio");
    QString pattern = target.dir().filePath(
        QStringLiteral(".%1.XXXXXX").arg(baseName));
    const QString suffix = target.completeSuffix();
    if (!suffix.isEmpty()) pattern += QLatin1Char('.') + suffix;
    return pattern;
}

int64_t timelineOriginForAudio(const AVFormatContext *formatContext,
                               const AVStream *audioStream)
{
    if (!formatContext || !audioStream) return 0;

    // The editor's time zero is the first video stream timestamp. Use that same
    // origin for audio so an intentional stream-start offset is preserved. For
    // audio-only media, fall back to the container and then audio stream origin.
    int videoStreamIndex = av_find_best_stream(
        const_cast<AVFormatContext *>(formatContext),
        AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex >= 0 &&
        videoStreamIndex < static_cast<int>(formatContext->nb_streams)) {
        const AVStream *videoStream = formatContext->streams[videoStreamIndex];
        if (videoStream->start_time != AV_NOPTS_VALUE) {
            return av_rescale_q(videoStream->start_time,
                                videoStream->time_base,
                                audioStream->time_base);
        }
    }

    if (formatContext->start_time != AV_NOPTS_VALUE) {
        return av_rescale_q(formatContext->start_time,
                            AV_TIME_BASE_Q,
                            audioStream->time_base);
    }
    if (audioStream->start_time != AV_NOPTS_VALUE) return audioStream->start_time;
    return 0;
}

int64_t timelineEndForAudio(const AVFormatContext *formatContext,
                            const AVStream *audioStream,
                            int64_t timelineOrigin)
{
    if (!formatContext || !audioStream) return AV_NOPTS_VALUE;

    if (audioStream->duration != AV_NOPTS_VALUE && audioStream->duration > 0) {
        const int64_t streamStart = audioStream->start_time != AV_NOPTS_VALUE
            ? audioStream->start_time
            : timelineOrigin;
        if (streamStart <= std::numeric_limits<int64_t>::max() - audioStream->duration)
            return streamStart + audioStream->duration;
    }

    const bool hasVideoTimeline = av_find_best_stream(
        const_cast<AVFormatContext *>(formatContext),
        AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0) >= 0;
    if (!hasVideoTimeline &&
        formatContext->duration != AV_NOPTS_VALUE && formatContext->duration > 0) {
        const int64_t containerStart = formatContext->start_time != AV_NOPTS_VALUE
            ? av_rescale_q(formatContext->start_time,
                           AV_TIME_BASE_Q,
                           audioStream->time_base)
            : timelineOrigin;
        const int64_t containerDuration = av_rescale_q(formatContext->duration,
                                                        AV_TIME_BASE_Q,
                                                        audioStream->time_base);
        if (containerDuration > 0 &&
            containerStart <= std::numeric_limits<int64_t>::max() - containerDuration) {
            return containerStart + containerDuration;
        }
    }
    return AV_NOPTS_VALUE;
}

bool replaceWithStagedFile(const QString &stagedPath, const QString &outputPath)
{
    if (stagedPath.isEmpty() || outputPath.isEmpty() || QFileInfo(stagedPath).size() <= 0)
        return false;
    const QFileInfo existing(outputPath);
    const QFile::Permissions permissions = existing.exists()
        ? existing.permissions()
        : QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
    QFile::setPermissions(stagedPath, permissions);
    const QByteArray stagedName = QFile::encodeName(stagedPath);
    const QByteArray outputName = QFile::encodeName(outputPath);
    return std::rename(stagedName.constData(), outputName.constData()) == 0;
}

int bitsForSampleFormat(AVSampleFormat format)
{
    const int bytes = av_get_bytes_per_sample(format);
    return bytes > 0 ? bytes * 8 : 0;
}

bool checkedBufferSize(int64_t samples, int channels, int bytesPerChannel, qsizetype *size)
{
    if (!size || samples < 0 || channels <= 0 || bytesPerChannel <= 0) return false;
    const int64_t bytesPerFrame = static_cast<int64_t>(channels) * bytesPerChannel;
    if (samples > std::numeric_limits<qsizetype>::max() / bytesPerFrame) return false;
    *size = static_cast<qsizetype>(samples * bytesPerFrame);
    return true;
}

bool readAvsAsInt16(AVS_Clip *clip,
                    const AVS_VideoInfo *vi,
                    int64_t startSample,
                    int64_t sampleCount,
                    char *output)
{
    if (!clip || !vi || !output || sampleCount <= 0 || vi->nchannels <= 0) return false;

    const int64_t valueCount64 = sampleCount * static_cast<int64_t>(vi->nchannels);
    if (valueCount64 <= 0 ||
        valueCount64 > static_cast<int64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    const int valueCount = static_cast<int>(valueCount64);
    int16_t *destination = reinterpret_cast<int16_t *>(output);

    switch (vi->sample_type) {
    case AVS_SAMPLE_INT16:
        return avs_get_audio(clip, output, startSample, sampleCount) == 0;

    case AVS_SAMPLE_FLOAT: {
        std::vector<float> input(static_cast<size_t>(valueCount));
        if (avs_get_audio(clip, input.data(), startSample, sampleCount) != 0) return false;
        for (int i = 0; i < valueCount; ++i) {
            const float value = std::isfinite(input[i]) ? std::clamp(input[i], -1.0f, 1.0f) : 0.0f;
            destination[i] = static_cast<int16_t>(
                std::clamp<long>(std::lrint(value * 32767.0f), -32768L, 32767L));
        }
        return true;
    }

    case AVS_SAMPLE_INT32: {
        std::vector<int32_t> input(static_cast<size_t>(valueCount));
        if (avs_get_audio(clip, input.data(), startSample, sampleCount) != 0) return false;
        for (int i = 0; i < valueCount; ++i) {
            destination[i] = static_cast<int16_t>(input[i] >> 16);
        }
        return true;
    }

    case AVS_SAMPLE_INT24: {
        std::vector<uint8_t> input(static_cast<size_t>(valueCount) * 3U);
        if (avs_get_audio(clip, input.data(), startSample, sampleCount) != 0) return false;
        for (int i = 0; i < valueCount; ++i) {
            const size_t offset = static_cast<size_t>(i) * 3U;
            const uint16_t highWord = static_cast<uint16_t>(input[offset + 1]) |
                                      (static_cast<uint16_t>(input[offset + 2]) << 8);
            destination[i] = static_cast<int16_t>(highWord);
        }
        return true;
    }

    case AVS_SAMPLE_INT8: {
        std::vector<uint8_t> input(static_cast<size_t>(valueCount));
        if (avs_get_audio(clip, input.data(), startSample, sampleCount) != 0) return false;
        for (int i = 0; i < valueCount; ++i) {
            destination[i] = static_cast<int16_t>((static_cast<int>(input[i]) - 128) << 8);
        }
        return true;
    }

    default:
        return false;
    }
}

class FFmpegAudioDecoder final {
public:
    FFmpegAudioDecoder() = default;
    ~FFmpegAudioDecoder()
    {
        if (mSwrContext) swr_free(&mSwrContext);
        if (mPacket) av_packet_free(&mPacket);
        if (mDeferredFrame) av_frame_free(&mDeferredFrame);
        if (mFrame) av_frame_free(&mFrame);
        if (mCodecContext) avcodec_free_context(&mCodecContext);
        if (mFormatContext) avformat_close_input(&mFormatContext);
        av_channel_layout_uninit(&mSourceLayout);
        av_channel_layout_uninit(&mOutputLayout);
    }

    FFmpegAudioDecoder(const FFmpegAudioDecoder &) = delete;
    FFmpegAudioDecoder &operator=(const FFmpegAudioDecoder &) = delete;

    bool open(const QString &filePath, int preferredStreamIndex)
    {
        const QByteArray encodedPath = QFile::encodeName(filePath);
        const AVInputFormat *inputFormat = nullptr;
        if (filePath.endsWith(".avs", Qt::CaseInsensitive)) {
            inputFormat = av_find_input_format("avisynth");
        } else if (filePath.endsWith(".vpy", Qt::CaseInsensitive)) {
            inputFormat = av_find_input_format("vapoursynth");
        }

        int result = avformat_open_input(&mFormatContext,
                                         encodedPath.constData(),
                                         inputFormat,
                                         nullptr);
        if (result < 0) return fail("Could not open audio source", result);

        result = avformat_find_stream_info(mFormatContext, nullptr);
        if (result < 0) return fail("Could not read audio stream information", result);

        if (preferredStreamIndex >= 0 &&
            preferredStreamIndex < static_cast<int>(mFormatContext->nb_streams) &&
            mFormatContext->streams[preferredStreamIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            mStreamIndex = preferredStreamIndex;
        } else {
            mStreamIndex = av_find_best_stream(mFormatContext,
                                               AVMEDIA_TYPE_AUDIO,
                                               -1,
                                               -1,
                                               nullptr,
                                               0);
        }
        if (mStreamIndex < 0) return fail("No decodable audio stream was found", mStreamIndex);

        mStream = mFormatContext->streams[mStreamIndex];
        mTimelineOriginTimestamp = timelineOriginForAudio(mFormatContext, mStream);
        const AVCodec *codec = avcodec_find_decoder(mStream->codecpar->codec_id);
        if (!codec) return fail("No decoder is available for the audio stream", AVERROR_DECODER_NOT_FOUND);

        mCodecContext = avcodec_alloc_context3(codec);
        if (!mCodecContext) return fail("Could not allocate the audio decoder", AVERROR(ENOMEM));

        result = avcodec_parameters_to_context(mCodecContext, mStream->codecpar);
        if (result < 0) return fail("Could not initialize the audio decoder", result);

        result = avcodec_open2(mCodecContext, codec, nullptr);
        if (result < 0) return fail("Could not open the audio decoder", result);

        mInputRate = mCodecContext->sample_rate > 0
            ? mCodecContext->sample_rate
            : mStream->codecpar->sample_rate;
        if (mInputRate <= 0 || mCodecContext->sample_fmt == AV_SAMPLE_FMT_NONE) {
            return fail("The audio stream has an invalid sample format", AVERROR_INVALIDDATA);
        }

        const int fallbackChannels = mCodecContext->ch_layout.nb_channels > 0
            ? mCodecContext->ch_layout.nb_channels
            : std::max(1, mStream->codecpar->ch_layout.nb_channels);
        if (!copyOrCreateLayout(&mSourceLayout,
                                &mCodecContext->ch_layout,
                                fallbackChannels)) {
            return fail("The audio stream has an invalid channel layout", AVERROR_INVALIDDATA);
        }

        mFrame = av_frame_alloc();
        mDeferredFrame = av_frame_alloc();
        mPacket = av_packet_alloc();
        if (!mFrame || !mDeferredFrame || !mPacket) {
            return fail("Could not allocate audio decode buffers", AVERROR(ENOMEM));
        }

        return true;
    }

    bool configureOutput(AVSampleFormat outputFormat,
                         int outputRate,
                         const AVChannelLayout *outputLayout)
    {
        if (!mCodecContext || outputFormat == AV_SAMPLE_FMT_NONE || outputRate <= 0 || !outputLayout) {
            return fail("Invalid audio output format", AVERROR(EINVAL));
        }

        av_channel_layout_uninit(&mOutputLayout);
        if (!copyOrCreateLayout(&mOutputLayout,
                                outputLayout,
                                outputLayout->nb_channels)) {
            return fail("Invalid audio output channel layout", AVERROR(EINVAL));
        }

        if (mSwrContext) swr_free(&mSwrContext);
        int result = swr_alloc_set_opts2(&mSwrContext,
                                         &mOutputLayout,
                                         outputFormat,
                                         outputRate,
                                         &mSourceLayout,
                                         mCodecContext->sample_fmt,
                                         mInputRate,
                                         0,
                                         nullptr);
        if (result < 0 || !mSwrContext) {
            return fail("Could not allocate the audio converter",
                        result < 0 ? result : AVERROR(ENOMEM));
        }

        result = swr_init(mSwrContext);
        if (result < 0) return fail("Could not initialize the audio converter", result);

        mOutputFormat = outputFormat;
        mOutputRate = outputRate;
        mBytesPerFrame = av_get_bytes_per_sample(outputFormat) * mOutputLayout.nb_channels;
        if (mBytesPerFrame <= 0) return fail("Invalid converted audio frame size", AVERROR(EINVAL));
        return true;
    }

    bool nextChunk(QByteArray *pcm, int *sampleCount, int64_t *startSample)
    {
        if (!pcm || !sampleCount || !startSample || !mSwrContext || mFailed || mResamplerDrained) {
            return false;
        }

        pcm->clear();
        *sampleCount = 0;
        *startSample = AV_NOPTS_VALUE;

        for (;;) {
            int receiveResult = 0;
            if (mDeferredFramePending) {
                av_frame_move_ref(mFrame, mDeferredFrame);
                mDeferredFramePending = false;
            } else {
                receiveResult = avcodec_receive_frame(mCodecContext, mFrame);
            }
            if (receiveResult == 0) {
                const int64_t frameStart = frameStartSample(mFrame);
                if (frameStart != AV_NOPTS_VALUE &&
                    mLastPresentationEndSample != AV_NOPTS_VALUE &&
                    !samplePositionsAreNear(frameStart,
                                            mLastPresentationEndSample,
                                            timestampJitterToleranceSamples())) {
                    // libswresample can retain samples from the preceding frame.
                    // Drain those at the old timeline position before beginning a
                    // discontinuous frame, otherwise a gap/overlap is shifted by
                    // the converter's filter delay.
                    av_frame_move_ref(mDeferredFrame, mFrame);
                    mDeferredFramePending = true;
                    const bool produced = drainResampler(pcm, sampleCount, startSample, false);
                    if (!resetResampler()) return false;
                    mLastPresentationEndSample = AV_NOPTS_VALUE;
                    mLastOutputEndSample = AV_NOPTS_VALUE;
                    if (produced) return true;
                    continue;
                }

                const bool converted = convertFrame(pcm, sampleCount, startSample);
                av_frame_unref(mFrame);
                if (!converted) return false;
                if (*sampleCount > 0) return true;
                continue;
            }

            if (receiveResult == AVERROR_EOF) {
                mDecoderEof = true;
                return flushResampler(pcm, sampleCount, startSample);
            }
            if (receiveResult != AVERROR(EAGAIN)) {
                fail("Audio decoding failed", receiveResult);
                return false;
            }

            if (mPacketPending) {
                const int sendResult = avcodec_send_packet(mCodecContext, mPacket);
                if (sendResult == 0) {
                    av_packet_unref(mPacket);
                    mPacketPending = false;
                    continue;
                }
                if (sendResult == AVERROR(EAGAIN)) continue;
                av_packet_unref(mPacket);
                mPacketPending = false;
                if (sendResult != AVERROR_EOF) {
                    fail("Could not submit an audio packet to the decoder", sendResult);
                    return false;
                }
                mDecoderEof = true;
                return flushResampler(pcm, sampleCount, startSample);
            }

            if (!mInputEof) {
                for (;;) {
                    const int readResult = av_read_frame(mFormatContext, mPacket);
                    if (readResult >= 0) {
                        if (mPacket->stream_index != mStreamIndex) {
                            av_packet_unref(mPacket);
                            continue;
                        }
                        mPacketPending = true;
                        break;
                    }

                    if (readResult != AVERROR_EOF) {
                        fail("Could not read the next audio packet", readResult);
                        return false;
                    }
                    mInputEof = true;
                    break;
                }
                if (mPacketPending) continue;
            }

            if (!mFlushPacketSent) {
                const int sendResult = avcodec_send_packet(mCodecContext, nullptr);
                if (sendResult == 0 || sendResult == AVERROR_EOF) {
                    mFlushPacketSent = true;
                    if (sendResult == AVERROR_EOF) {
                        mDecoderEof = true;
                        return flushResampler(pcm, sampleCount, startSample);
                    }
                    continue;
                }
                if (sendResult == AVERROR(EAGAIN)) continue;
                fail("Could not drain the audio decoder", sendResult);
                return false;
            }

            mDecoderEof = true;
            return flushResampler(pcm, sampleCount, startSample);
        }
    }

    bool seekToSample(int64_t sample)
    {
        if (!mFormatContext || !mCodecContext || !mStream || mOutputRate <= 0) return false;

        sample = std::max<int64_t>(0, sample);
        const int64_t targetTimestamp = mTimelineOriginTimestamp +
            av_rescale_q(sample, AVRational{1, mOutputRate}, mStream->time_base);
        const int64_t seekTimestamp = mStream->start_time != AV_NOPTS_VALUE
            ? std::max(targetTimestamp, mStream->start_time)
            : targetTimestamp;

        int result = avformat_seek_file(mFormatContext,
                                        mStreamIndex,
                                        std::numeric_limits<int64_t>::min(),
                                        seekTimestamp,
                                        seekTimestamp,
                                        AVSEEK_FLAG_BACKWARD);
        if (result < 0) {
            result = av_seek_frame(mFormatContext,
                                   mStreamIndex,
                                   seekTimestamp,
                                   AVSEEK_FLAG_BACKWARD);
        }
        if (result < 0) {
            qWarning() << "[VDQtAudioPlayer] Audio seek failed:" << avErrorString(result);
            return false;
        }

        avcodec_flush_buffers(mCodecContext);
        av_packet_unref(mPacket);
        av_frame_unref(mFrame);
        av_frame_unref(mDeferredFrame);
        if (!resetResampler()) return false;

        mPacketPending = false;
        mInputEof = false;
        mFlushPacketSent = false;
        mDecoderEof = false;
        mResamplerDrained = false;
        mDeferredFramePending = false;
        mLastPresentationEndSample = AV_NOPTS_VALUE;
        mLastOutputEndSample = AV_NOPTS_VALUE;
        mFailed = false;
        mError.clear();
        return true;
    }

    bool failed() const { return mFailed; }
    bool atEnd() const { return mResamplerDrained; }
    QString error() const { return mError; }
    int inputRate() const { return mInputRate; }
    int outputRate() const { return mOutputRate; }
    int bytesPerFrame() const { return mBytesPerFrame; }
    AVSampleFormat sourceSampleFormat() const
    {
        return mCodecContext ? mCodecContext->sample_fmt : AV_SAMPLE_FMT_NONE;
    }
    int sourceBitsPerSample() const
    {
        if (!mCodecContext || !mStream) return 0;
        if (mCodecContext->bits_per_raw_sample > 0) return mCodecContext->bits_per_raw_sample;
        if (mStream->codecpar->bits_per_raw_sample > 0) return mStream->codecpar->bits_per_raw_sample;
        const int decodedBits = bitsForSampleFormat(mCodecContext->sample_fmt);
        if (decodedBits > 0) return decodedBits;
        return mStream->codecpar->bits_per_coded_sample;
    }
    const AVChannelLayout *sourceLayout() const { return &mSourceLayout; }

private:
    bool fail(const QString &message, int errorCode)
    {
        mFailed = true;
        mError = errorCode < 0
            ? QString("%1: %2").arg(message, avErrorString(errorCode))
            : message;
        return false;
    }

    bool allocateOutput(int sampleCapacity, QByteArray *pcm)
    {
        if (!pcm || sampleCapacity <= 0) return false;
        const int bufferSize = av_samples_get_buffer_size(nullptr,
                                                          mOutputLayout.nb_channels,
                                                          sampleCapacity,
                                                          mOutputFormat,
                                                          1);
        if (bufferSize < 0) return fail("Could not size an audio conversion buffer", bufferSize);
        pcm->resize(bufferSize);
        return true;
    }

    int64_t outputSampleForTimestamp(int64_t timestamp) const
    {
        if (timestamp == AV_NOPTS_VALUE || !mStream || mOutputRate <= 0) {
            return AV_NOPTS_VALUE;
        }
        if ((mTimelineOriginTimestamp > 0 &&
             timestamp < std::numeric_limits<int64_t>::min() + mTimelineOriginTimestamp) ||
            (mTimelineOriginTimestamp < 0 &&
             timestamp > std::numeric_limits<int64_t>::max() + mTimelineOriginTimestamp)) {
            return AV_NOPTS_VALUE;
        }
        return av_rescale_q(timestamp - mTimelineOriginTimestamp,
                            mStream->time_base,
                            AVRational{1, mOutputRate});
    }

    int64_t frameStartSample(const AVFrame *frame) const
    {
        return frame ? outputSampleForTimestamp(frame->best_effort_timestamp)
                     : AV_NOPTS_VALUE;
    }

    int64_t timestampJitterToleranceSamples() const
    {
        // Packet timestamps are often expressed in a time base that cannot
        // represent every audio sample exactly. Some MP4 muxers also vary AAC
        // packet durations by a few dozen samples while every decoded frame
        // still contains 1024 continuous samples. Follow decoded PCM
        // across discrepancies of at most two milliseconds; larger differences
        // remain real gaps/overlaps and are preserved on the media timeline.
        constexpr int divisor = 500;
        return std::max<int64_t>(1,
            (static_cast<int64_t>(mOutputRate) + divisor - 1) / divisor);
    }

    static bool samplePositionsAreNear(int64_t first,
                                       int64_t second,
                                       int64_t tolerance)
    {
        if (first >= second) {
            return second > std::numeric_limits<int64_t>::max() - tolerance ||
                   first <= second + tolerance;
        }
        return first > std::numeric_limits<int64_t>::max() - tolerance ||
               second <= first + tolerance;
    }

    bool resetResampler()
    {
        swr_close(mSwrContext);
        const int result = swr_init(mSwrContext);
        return result >= 0 || fail("Could not reset the audio converter", result);
    }

    bool convertFrame(QByteArray *pcm, int *sampleCount, int64_t *startSample)
    {
        const int64_t delayBeforeConversion = swr_get_delay(mSwrContext, mInputRate);
        const int64_t maximumSamples64 = av_rescale_rnd(
            delayBeforeConversion + mFrame->nb_samples,
            mOutputRate,
            mInputRate,
            AV_ROUND_UP);
        if (maximumSamples64 <= 0 || maximumSamples64 > std::numeric_limits<int>::max()) {
            return fail("The decoded audio frame is too large", AVERROR(EINVAL));
        }
        const int maximumSamples = static_cast<int>(maximumSamples64);
        if (!allocateOutput(maximumSamples, pcm)) return false;

        uint8_t *outputData[] = { reinterpret_cast<uint8_t *>(pcm->data()) };
        const uint8_t **inputData = const_cast<const uint8_t **>(mFrame->extended_data);
        const int convertedSamples = swr_convert(mSwrContext,
                                                 outputData,
                                                 maximumSamples,
                                                 inputData,
                                                 mFrame->nb_samples);
        if (convertedSamples < 0) return fail("Audio sample conversion failed", convertedSamples);

        int validSamples = convertedSamples;
        int64_t frameStart = frameStartSample(mFrame);
        if (frameStart != AV_NOPTS_VALUE &&
            mLastPresentationEndSample != AV_NOPTS_VALUE &&
            frameStart != mLastPresentationEndSample &&
            samplePositionsAreNear(frameStart,
                                   mLastPresentationEndSample,
                                   timestampJitterToleranceSamples())) {
            // Keep the resampler and PCM stream continuous across harmless
            // timestamp quantization.
            frameStart = mLastPresentationEndSample;
        }
        int64_t chunkStart = AV_NOPTS_VALUE;
        if (frameStart != AV_NOPTS_VALUE) {
            const int64_t delayedOutput = av_rescale_rnd(delayBeforeConversion,
                                                         mOutputRate,
                                                         mInputRate,
                                                         AV_ROUND_NEAR_INF);
            chunkStart = frameStart - delayedOutput;
            *startSample = chunkStart;

            // AVFrame::duration is packet presentation metadata, not a safe
            // per-frame PCM edit point. Some MP4/AAC files vary it while every
            // decoded frame still contains 1024 continuous samples. Follow the
            // decoded sample count; FFmpeg already applies codec delay/padding,
            // and callers clip explicit selections at the final range boundary.
            const int64_t decodedDuration = av_rescale_rnd(
                mFrame->nb_samples,
                mOutputRate,
                mInputRate,
                AV_ROUND_NEAR_INF);
            mLastPresentationEndSample = decodedDuration >= 0 &&
                frameStart <= std::numeric_limits<int64_t>::max() - decodedDuration
                ? frameStart + decodedDuration
                : AV_NOPTS_VALUE;

            if (chunkStart <= std::numeric_limits<int64_t>::max() - validSamples) {
                mLastOutputEndSample = chunkStart + validSamples;
            } else {
                mLastOutputEndSample = AV_NOPTS_VALUE;
            }
        }

        pcm->resize(validSamples * mBytesPerFrame);
        *sampleCount = validSamples;
        return true;
    }

    bool drainResampler(QByteArray *pcm,
                        int *sampleCount,
                        int64_t *startSample,
                        bool markDrained)
    {
        const int64_t pending64 = av_rescale_rnd(swr_get_delay(mSwrContext, mInputRate),
                                                 mOutputRate,
                                                 mInputRate,
                                                 AV_ROUND_UP);
        if (pending64 <= 0) {
            if (markDrained) mResamplerDrained = true;
            return false;
        }
        if (pending64 > std::numeric_limits<int>::max()) {
            return fail("The pending resampler output is too large", AVERROR(EINVAL));
        }

        const int pending = static_cast<int>(pending64);
        if (!allocateOutput(pending, pcm)) return false;
        uint8_t *outputData[] = { reinterpret_cast<uint8_t *>(pcm->data()) };
        const int convertedSamples = swr_convert(mSwrContext,
                                                 outputData,
                                                 pending,
                                                 nullptr,
                                                 0);
        if (convertedSamples < 0) {
            return fail("Could not drain the audio resampler", convertedSamples);
        }

        int validSamples = convertedSamples;
        if (mLastOutputEndSample != AV_NOPTS_VALUE) {
            *startSample = mLastOutputEndSample;
            if (mLastPresentationEndSample != AV_NOPTS_VALUE) {
                const int64_t maximumValid = std::max<int64_t>(
                    0, mLastPresentationEndSample - mLastOutputEndSample);
                validSamples = static_cast<int>(std::min<int64_t>(validSamples, maximumValid));
            }
            if (mLastOutputEndSample <=
                std::numeric_limits<int64_t>::max() - validSamples) {
                mLastOutputEndSample += validSamples;
            } else {
                mLastOutputEndSample = AV_NOPTS_VALUE;
            }
        }

        pcm->resize(validSamples * mBytesPerFrame);
        *sampleCount = validSamples;
        if (validSamples == 0 && markDrained) mResamplerDrained = true;
        return validSamples > 0;
    }

    bool flushResampler(QByteArray *pcm, int *sampleCount, int64_t *startSample)
    {
        if (!mDecoderEof || mResamplerDrained) return false;
        return drainResampler(pcm, sampleCount, startSample, true);
    }

    AVFormatContext *mFormatContext = nullptr;
    AVCodecContext *mCodecContext = nullptr;
    SwrContext *mSwrContext = nullptr;
    AVFrame *mFrame = nullptr;
    AVFrame *mDeferredFrame = nullptr;
    AVPacket *mPacket = nullptr;
    AVStream *mStream = nullptr;
    AVChannelLayout mSourceLayout = {};
    AVChannelLayout mOutputLayout = {};
    AVSampleFormat mOutputFormat = AV_SAMPLE_FMT_NONE;
    int mStreamIndex = -1;
    int mInputRate = 0;
    int mOutputRate = 0;
    int mBytesPerFrame = 0;
    int64_t mTimelineOriginTimestamp = 0;
    int64_t mLastPresentationEndSample = AV_NOPTS_VALUE;
    int64_t mLastOutputEndSample = AV_NOPTS_VALUE;
    bool mDeferredFramePending = false;
    bool mPacketPending = false;
    bool mInputEof = false;
    bool mFlushPacketSent = false;
    bool mDecoderEof = false;
    bool mResamplerDrained = false;
    bool mFailed = false;
    QString mError;
};

struct PcmOutputSpec {
    AVSampleFormat sampleFormat = AV_SAMPLE_FMT_S16;
    int containerBits = 16;
    int validBits = 16;
    bool floatingPoint = false;
};

PcmOutputSpec choosePcmOutput(AVSampleFormat sourceFormat, int sourceBits)
{
    PcmOutputSpec result;
    const AVSampleFormat packed = av_get_packed_sample_fmt(sourceFormat);
    if (packed == AV_SAMPLE_FMT_FLT || packed == AV_SAMPLE_FMT_DBL) {
        result.sampleFormat = AV_SAMPLE_FMT_FLT;
        result.containerBits = 32;
        result.validBits = 32;
        result.floatingPoint = true;
    } else if (sourceBits > 16 || packed == AV_SAMPLE_FMT_S32 || packed == AV_SAMPLE_FMT_S64) {
        result.sampleFormat = AV_SAMPLE_FMT_S32;
        result.containerBits = 32;
        result.validBits = std::clamp(sourceBits > 0 ? sourceBits : 32, 17, 32);
    } else if (sourceBits > 0 && sourceBits <= 8 && packed == AV_SAMPLE_FMT_U8) {
        result.sampleFormat = AV_SAMPLE_FMT_U8;
        result.containerBits = 8;
        result.validBits = 8;
    }
    return result;
}

class WavWriter final {
public:
    bool open(const QString &path,
              int channels,
              int sampleRate,
              int containerBits,
              int validBits,
              bool floatingPoint,
              uint32_t channelMask,
              int64_t expectedDataBytes)
    {
        if (channels <= 0 || sampleRate <= 0 || containerBits <= 0 || containerBits % 8 != 0) {
            return false;
        }

        mChannels = channels;
        mSampleRate = sampleRate;
        mContainerBits = containerBits;
        mValidBits = validBits;
        mFloatingPoint = floatingPoint;
        mChannelMask = channelMask;
        const int64_t blockAlign = static_cast<int64_t>(channels) * (containerBits / 8);
        if (channels > std::numeric_limits<quint16>::max() ||
            blockAlign <= 0 || blockAlign > std::numeric_limits<quint16>::max() ||
            static_cast<int64_t>(sampleRate) * blockAlign >
                std::numeric_limits<quint32>::max()) {
            return false;
        }
        mBlockAlign = static_cast<int>(blockAlign);
        mExtensible = channels > 2 || validBits != containerBits;
        mRf64 = expectedDataBytes > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) - 256;
        mReserveRf64 = mRf64 || expectedDataBytes < 0;
        mDataBytes = 0;

        mFile.setFileName(path);
        if (!mFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

        const QByteArray header = makeHeader(0);
        if (header.isEmpty() || mFile.write(header) != header.size()) {
            abort();
            return false;
        }
        return true;
    }

    bool write(const char *data, qint64 size)
    {
        if (!mFile.isOpen() || !data || size < 0) return false;
        if (size > std::numeric_limits<int64_t>::max() - mDataBytes) return false;
        const int64_t newDataBytes = mDataBytes + size;
        if (!mRf64 && newDataBytes >
            static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) - 256) {
            if (!mReserveRf64) return false;
            mRf64 = true;
        }
        if (size > 0 && mFile.write(data, size) != size) return false;
        mDataBytes = newDataBytes;
        return true;
    }

    bool finalize()
    {
        if (!mFile.isOpen()) return false;
        const QByteArray header = makeHeader(mDataBytes);
        if (header.isEmpty() || !mFile.seek(0) || mFile.write(header) != header.size()) {
            abort();
            return false;
        }
        const bool ok = mFile.flush();
        mFile.close();
        return ok;
    }

    void abort()
    {
        const QString path = mFile.fileName();
        if (mFile.isOpen()) mFile.close();
        if (!path.isEmpty()) QFile::remove(path);
    }

private:
    QByteArray makeHeader(int64_t dataBytes) const
    {
        QByteArray header;
        QDataStream stream(&header, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);

        const quint32 fmtSize = mExtensible ? 40U : 16U;
        const quint64 headerSize = ((mRf64 || mReserveRf64) ? 48ULL : 12ULL) +
                                   8ULL + fmtSize + 8ULL;
        const quint64 riffSize = static_cast<quint64>(std::max<int64_t>(0, dataBytes)) + headerSize - 8ULL;

        stream.writeRawData(mRf64 ? "RF64" : "RIFF", 4);
        stream << (mRf64 ? 0xffffffffU : static_cast<quint32>(riffSize));
        stream.writeRawData("WAVE", 4);

        if (mRf64) {
            stream.writeRawData("ds64", 4);
            stream << quint32(28);
            stream << riffSize;
            stream << static_cast<quint64>(std::max<int64_t>(0, dataBytes));
            stream << static_cast<quint64>(mBlockAlign > 0 ? dataBytes / mBlockAlign : 0);
            stream << quint32(0);
        } else if (mReserveRf64) {
            // Keep a same-sized placeholder so an unknown-length stream can be
            // upgraded to RF64 at finalize time without moving audio data.
            stream.writeRawData("JUNK", 4);
            stream << quint32(28);
            const char padding[28] = {};
            stream.writeRawData(padding, sizeof(padding));
        }

        stream.writeRawData("fmt ", 4);
        stream << fmtSize;
        stream << static_cast<quint16>(mExtensible ? 0xfffe : (mFloatingPoint ? 3 : 1));
        stream << static_cast<quint16>(mChannels);
        stream << static_cast<quint32>(mSampleRate);
        stream << static_cast<quint32>(mSampleRate * mBlockAlign);
        stream << static_cast<quint16>(mBlockAlign);
        stream << static_cast<quint16>(mContainerBits);

        if (mExtensible) {
            stream << quint16(22);
            stream << static_cast<quint16>(mValidBits);
            stream << mChannelMask;
            const unsigned char subFormat[16] = {
                static_cast<unsigned char>(mFloatingPoint ? 3 : 1), 0, 0, 0,
                0, 0, 0x10, 0,
                0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71
            };
            stream.writeRawData(reinterpret_cast<const char *>(subFormat), 16);
        }

        stream.writeRawData("data", 4);
        stream << (mRf64 ? 0xffffffffU : static_cast<quint32>(dataBytes));
        return stream.status() == QDataStream::Ok ? header : QByteArray();
    }

    QFile mFile;
    int mChannels = 0;
    int mSampleRate = 0;
    int mContainerBits = 0;
    int mValidBits = 0;
    int mBlockAlign = 0;
    bool mFloatingPoint = false;
    bool mExtensible = false;
    bool mRf64 = false;
    bool mReserveRf64 = false;
    uint32_t mChannelMask = 0;
    int64_t mDataBytes = 0;
};

uint32_t wavChannelMask(const AVChannelLayout *layout)
{
    if (!layout || layout->order != AV_CHANNEL_ORDER_NATIVE) return 0;
    return static_cast<uint32_t>(layout->u.mask & 0xffffffffULL);
}

bool reportProgress(const std::function<bool(int, int)> &callback,
                    int64_t completed,
                    int64_t total,
                    int *lastPercent,
                    int maximumPercent = 100)
{
    if (!callback) return true;
    int percent = 0;
    if (total > 0) {
        const long double ratio = static_cast<long double>(completed) /
                                  static_cast<long double>(total);
        percent = static_cast<int>(std::clamp<long double>(ratio * maximumPercent,
                                                           0.0L,
                                                           maximumPercent));
    }
    if (lastPercent && percent == *lastPercent) return true;
    if (lastPercent) *lastPercent = percent;
    return callback(percent, 100);
}

bool transcodeTemporaryWav(const QString &wavPath,
                           const QString &outputPath,
                           int sourceBits,
                           const std::function<bool(int, int)> &progressCallback)
{
    QStringList arguments;
    arguments << "-nostdin" << "-y" << "-i" << wavPath;

    if (outputPath.endsWith(".m4a", Qt::CaseInsensitive)) {
        arguments << "-c:a" << "aac" << "-b:a" << "256k";
    } else if (outputPath.endsWith(".mka", Qt::CaseInsensitive)) {
        arguments << "-c:a" << "flac";
    } else if (outputPath.endsWith(".aiff", Qt::CaseInsensitive) ||
               outputPath.endsWith(".aif", Qt::CaseInsensitive)) {
        arguments << "-c:a" << (sourceBits > 16 ? "pcm_s24be" : "pcm_s16be");
    } else {
        arguments << "-c:a" << "copy";
    }
    arguments << outputPath;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("ffmpeg", arguments);
    if (!process.waitForStarted(5000)) {
        QFile::remove(outputPath);
        return false;
    }

    QByteArray diagnosticTail;
    bool cancelled = false;
    while (!process.waitForFinished(50)) {
        diagnosticTail += process.readAll();
        if (diagnosticTail.size() > 64 * 1024) {
            diagnosticTail.remove(0, diagnosticTail.size() - 64 * 1024);
        }
        if (progressCallback && !progressCallback(95, 100)) {
            cancelled = true;
            process.terminate();
            if (!process.waitForFinished(1500)) {
                process.kill();
                process.waitForFinished(3000);
            }
            break;
        }
    }
    diagnosticTail += process.readAll();

    const bool success = !cancelled &&
                         process.exitStatus() == QProcess::NormalExit &&
                         process.exitCode() == 0 &&
                         QFileInfo(outputPath).size() > 0;
    if (!success) {
        if (!diagnosticTail.isEmpty()) {
            qWarning().noquote() << "[VDQtAudioPlayer] ffmpeg audio transcode failed:"
                                 << QString::fromLocal8Bit(diagnosticTail.right(4096));
        }
        QFile::remove(outputPath);
        return false;
    }

    if (progressCallback && !progressCallback(100, 100)) {
        QFile::remove(outputPath);
        return false;
    }
    return true;
}

} // namespace

class VDQtFFmpegAudioDevice final : public QIODevice {
public:
    VDQtFFmpegAudioDevice(const QString &filePath,
                         int streamIndex,
                         const QAudioFormat &outputFormat,
                         int64_t totalSamples,
                         int testDecodeDelayMs = 0,
                         QObject *parent = nullptr)
        : QIODevice(parent)
        , mFilePath(filePath)
        , mStreamIndex(streamIndex)
        , mOutputFormat(outputFormat)
        , mTotalSamples(totalSamples)
        , mTestDecodeDelayMs(std::max(0, testDecodeDelayMs))
    {
    }

    ~VDQtFFmpegAudioDevice() override
    {
        close();
    }

    bool initialize()
    {
        auto decoder = std::make_unique<FFmpegAudioDecoder>();
        if (!decoder->open(mFilePath, mStreamIndex)) {
            mError = decoder->error();
            return false;
        }

        const AVSampleFormat sampleFormat = qtSampleFormatToAV(mOutputFormat.sampleFormat());
        AVChannelLayout outputLayout = {};
        if (mOutputFormat.channelCount() == decoder->sourceLayout()->nb_channels) {
            copyOrCreateLayout(&outputLayout,
                               decoder->sourceLayout(),
                               mOutputFormat.channelCount());
        } else {
            av_channel_layout_default(&outputLayout, mOutputFormat.channelCount());
        }

        const bool configured = decoder->configureOutput(sampleFormat,
                                                         mOutputFormat.sampleRate(),
                                                         &outputLayout);
        av_channel_layout_uninit(&outputLayout);
        if (!configured) {
            mError = decoder->error();
            return false;
        }

        mBytesPerFrame = decoder->bytesPerFrame();
        mSourceRate = decoder->inputRate();
        mOutputRate = decoder->outputRate();
        mTotalOutputSamples = (mTotalSamples > 0 && mSourceRate > 0 && mOutputRate > 0)
            ? av_rescale_rnd(mTotalSamples, mOutputRate, mSourceRate, AV_ROUND_UP)
            : 0;
        mSilenceByte = mOutputFormat.sampleFormat() == QAudioFormat::UInt8
            ? static_cast<char>(0x80)
            : '\0';
        mBufferTargetBytes = alignedAudioBufferSize(
            mOutputFormat, kLiveAudioPrebufferUsecs, 64 * 1024, 4 * 1024 * 1024);
        mDecoder = std::move(decoder);
        if (!open(QIODevice::ReadOnly)) return false;
        return startDecodeThreadAndPrime();
    }

    bool isSequential() const override { return true; }

    bool atEnd() const override
    {
        QMutexLocker locker(&mMutex);
        return mProducerEof && bufferedBytesUnlocked() == 0;
    }

    void close() override
    {
        stopDecodeThread();
        QIODevice::close();
    }

    qint64 bytesAvailable() const override
    {
        QMutexLocker locker(&mMutex);
        qint64 buffered = bufferedBytesUnlocked();
        const qint64 baseAvailable = QIODevice::bytesAvailable();
        if (baseAvailable > 0 &&
            baseAvailable <= std::numeric_limits<qint64>::max() - buffered) {
            buffered += baseAvailable;
        }
        return buffered;
    }

    qint64 size() const override
    {
        if (mTotalSamples <= 0 || mBytesPerFrame <= 0 || mSourceRate <= 0 || mOutputRate <= 0) {
            return 0;
        }
        if (mTotalOutputSamples > std::numeric_limits<qint64>::max() / mBytesPerFrame) {
            return std::numeric_limits<qint64>::max();
        }
        return mTotalOutputSamples * mBytesPerFrame;
    }

    bool seekToSample(int64_t sample)
    {
        stopDecodeThread();
        if (!mDecoder) return false;
        sample = std::max<int64_t>(0, sample);
        if (mTotalSamples > 0) sample = std::min(sample, mTotalSamples);
        const int64_t outputSample = (mSourceRate > 0 && mOutputRate > 0)
            ? av_rescale_rnd(sample, mOutputRate, mSourceRate, AV_ROUND_NEAR_INF)
            : sample;
        if (!mDecoder->seekToSample(outputSample)) {
            QMutexLocker locker(&mMutex);
            mError = mDecoder->error();
            return false;
        }

        // QIODevice may buffer more bytes than the caller requested. Reopening
        // discards that private read-ahead so data from before the decoder seek
        // cannot leak into the new logical position.
        QIODevice::close();
        if (!QIODevice::open(QIODevice::ReadOnly)) {
            QMutexLocker locker(&mMutex);
            mError = QStringLiteral("Could not reopen the audio playback stream after seeking.");
            return false;
        }
        {
            QMutexLocker locker(&mMutex);
            mBuffer.clear();
            mBufferOffset = 0;
            mBaseOutputSample = outputSample;
            mBaseSample = sample;
            mProducedOutputSample = outputSample;
            mBytesDelivered = 0;
            mProducerEof = false;
            mProducerFailed = false;
            mError.clear();
        }
        return startDecodeThreadAndPrime();
    }

    int64_t currentSample() const
    {
        QMutexLocker locker(&mMutex);
        const int64_t deliveredOutputSamples = mBytesPerFrame > 0
            ? mBytesDelivered / mBytesPerFrame
            : 0;
        const int64_t deliveredSourceSamples = (mSourceRate > 0 && mOutputRate > 0)
            ? av_rescale_rnd(deliveredOutputSamples,
                             mSourceRate,
                             mOutputRate,
                             AV_ROUND_NEAR_INF)
            : deliveredOutputSamples;
        return mBaseSample + deliveredSourceSamples;
    }

    QString error() const
    {
        QMutexLocker locker(&mMutex);
        return mError;
    }

protected:
    qint64 readData(char *data, qint64 maximumLength) override
    {
        if (!data || maximumLength <= 0) return 0;
        QMutexLocker locker(&mMutex);
        if (!mDecoder || mBytesPerFrame <= 0) return 0;

        maximumLength -= maximumLength % mBytesPerFrame;
        if (maximumLength <= 0) return 0;

        qint64 copied = 0;
        bool waitedForProducer = false;
        while (copied < maximumLength) {
            const qint64 available = bufferedBytesUnlocked();
            if (available <= 0) {
                if (mProducerEof || mProducerFailed || waitedForProducer) break;
                // A normally primed producer never reaches this wait. Give it
                // one short scheduling opportunity without doing codec work on
                // Qt's real-time audio callback.
                waitedForProducer = true;
                mBufferChanged.wait(&mMutex, 20);
                continue;
            }

            qint64 amount = std::min(available, maximumLength - copied);
            amount -= amount % mBytesPerFrame;
            if (amount <= 0) break;
            std::memcpy(data + copied, mBuffer.constData() + mBufferOffset, amount);
            copied += amount;
            mBufferOffset += static_cast<qsizetype>(amount);
            mBytesDelivered += amount;
            compactBufferUnlocked();
            mBufferChanged.wakeAll();
        }

        return copied;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    static bool samplePositionsAreNear(int64_t first,
                                       int64_t second,
                                       int64_t tolerance)
    {
        if (first >= second) {
            return second > std::numeric_limits<int64_t>::max() - tolerance ||
                   first <= second + tolerance;
        }
        return first > std::numeric_limits<int64_t>::max() - tolerance ||
               second <= first + tolerance;
    }

    qint64 bufferedBytesUnlocked() const
    {
        return std::max<qint64>(0, mBuffer.size() - mBufferOffset);
    }

    void compactBufferUnlocked()
    {
        if (mBufferOffset >= mBuffer.size()) {
            mBuffer.clear();
            mBufferOffset = 0;
        } else if (mBufferOffset > 0 && mBufferOffset >= mBufferTargetBytes / 2) {
            mBuffer.remove(0, mBufferOffset);
            mBufferOffset = 0;
        }
    }

    bool appendSilence(int64_t *gapSamples)
    {
        if (!gapSamples || *gapSamples <= 0) return false;
        bool notify = false;
        {
            QMutexLocker locker(&mMutex);
            while (!mStopProducer && bufferedBytesUnlocked() >= mBufferTargetBytes)
                mBufferChanged.wait(&mMutex);
            if (mStopProducer) return false;

            compactBufferUnlocked();
            const qint64 freeBytes = std::max<qint64>(
                0, mBufferTargetBytes - bufferedBytesUnlocked());
            const int64_t frames = std::min<int64_t>(*gapSamples,
                                                      freeBytes / mBytesPerFrame);
            if (frames <= 0) return true;
            const qsizetype bytes = static_cast<qsizetype>(frames * mBytesPerFrame);
            const qsizetype oldSize = mBuffer.size();
            mBuffer.resize(oldSize + bytes);
            std::memset(mBuffer.data() + oldSize,
                        static_cast<unsigned char>(mSilenceByte),
                        static_cast<size_t>(bytes));
            *gapSamples -= frames;
            mProducedOutputSample += frames;
            notify = true;
            mBufferChanged.wakeAll();
        }
        if (notify) Q_EMIT readyRead();
        return true;
    }

    bool appendPending(QByteArray *pending, qsizetype *pendingOffset)
    {
        if (!pending || !pendingOffset || *pendingOffset >= pending->size()) return false;
        bool notify = false;
        {
            QMutexLocker locker(&mMutex);
            while (!mStopProducer && bufferedBytesUnlocked() >= mBufferTargetBytes)
                mBufferChanged.wait(&mMutex);
            if (mStopProducer) return false;

            compactBufferUnlocked();
            qint64 amount = std::min<qint64>(
                pending->size() - *pendingOffset,
                mBufferTargetBytes - bufferedBytesUnlocked());
            amount -= amount % mBytesPerFrame;
            if (amount <= 0) return true;
            mBuffer.append(pending->constData() + *pendingOffset,
                           static_cast<qsizetype>(amount));
            *pendingOffset += static_cast<qsizetype>(amount);
            mProducedOutputSample += amount / mBytesPerFrame;
            notify = true;
            mBufferChanged.wakeAll();
        }
        if (notify) Q_EMIT readyRead();
        if (*pendingOffset >= pending->size()) {
            pending->clear();
            *pendingOffset = 0;
        }
        return true;
    }

    void decodeLoop()
    {
        QByteArray pending;
        qsizetype pendingOffset = 0;
        int64_t gapSamples = 0;
        bool decoderFinished = false;

        for (;;) {
            {
                QMutexLocker locker(&mMutex);
                if (mStopProducer) return;
            }

            if (gapSamples > 0) {
                if (!appendSilence(&gapSamples)) return;
                continue;
            }
            if (pendingOffset < pending.size()) {
                if (!appendPending(&pending, &pendingOffset)) return;
                continue;
            }
            if (decoderFinished) {
                {
                    QMutexLocker locker(&mMutex);
                    mProducerEof = true;
                    mBufferChanged.wakeAll();
                }
                Q_EMIT readyRead();
                return;
            }

            if (mTestDecodeDelayMs > 0)
                QThread::msleep(static_cast<unsigned long>(mTestDecodeDelayMs));

            QByteArray decoded;
            int samples = 0;
            int64_t chunkStart = AV_NOPTS_VALUE;
            if (!mDecoder->nextChunk(&decoded, &samples, &chunkStart)) {
                if (mDecoder->failed()) {
                    {
                        QMutexLocker locker(&mMutex);
                        mError = mDecoder->error();
                        mProducerFailed = true;
                        mProducerEof = true;
                        mBufferChanged.wakeAll();
                    }
                    Q_EMIT readyRead();
                    return;
                }

                int64_t cursor = 0;
                {
                    QMutexLocker locker(&mMutex);
                    cursor = mProducedOutputSample;
                }
                if (mTotalOutputSamples > cursor)
                    gapSamples = mTotalOutputSamples - cursor;
                decoderFinished = true;
                continue;
            }

            int64_t cursor = 0;
            {
                QMutexLocker locker(&mMutex);
                cursor = mProducedOutputSample;
            }
            if (chunkStart == AV_NOPTS_VALUE) chunkStart = cursor;
            const int64_t jitterTolerance = std::max<int64_t>(
                1, (static_cast<int64_t>(mOutputRate) + 999) / 1000);
            if (chunkStart != cursor &&
                samplePositionsAreNear(chunkStart, cursor, jitterTolerance)) {
                chunkStart = cursor;
            }

            if (samples < 0 || chunkStart > std::numeric_limits<int64_t>::max() - samples ||
                samples > std::numeric_limits<qsizetype>::max() / mBytesPerFrame ||
                decoded.size() < samples * static_cast<qsizetype>(mBytesPerFrame)) {
                {
                    QMutexLocker locker(&mMutex);
                    mError = QStringLiteral("The decoded audio timestamp or buffer is invalid.");
                    mProducerFailed = true;
                    mProducerEof = true;
                    mBufferChanged.wakeAll();
                }
                Q_EMIT readyRead();
                return;
            }

            const int64_t chunkEnd = chunkStart + samples;
            if (chunkEnd <= cursor) continue;

            qsizetype discardBytes = 0;
            if (chunkStart < cursor) {
                const int64_t discardSamples = cursor - chunkStart;
                if (discardSamples >= samples) continue;
                discardBytes = static_cast<qsizetype>(discardSamples * mBytesPerFrame);
            } else if (chunkStart > cursor) {
                gapSamples = chunkStart - cursor;
            }
            pending = std::move(decoded);
            pendingOffset = discardBytes;
        }
    }

    bool startDecodeThreadAndPrime()
    {
        {
            QMutexLocker locker(&mMutex);
            if (mDecodeThread || !mDecoder) return false;
            mStopProducer = false;
            mProducerEof = false;
            mProducerFailed = false;
            mDecodeThread = QThread::create([this]() { decodeLoop(); });
            if (!mDecodeThread) {
                mError = QStringLiteral("Could not create the audio decode-ahead thread.");
                return false;
            }
        }

        mDecodeThread->start(QThread::HighPriority);

        QElapsedTimer timer;
        timer.start();
        QMutexLocker locker(&mMutex);
        while (bufferedBytesUnlocked() < mBufferTargetBytes &&
               !mProducerEof && !mProducerFailed && timer.elapsed() < 5000) {
            mBufferChanged.wait(&mMutex, 50);
        }
        if (mProducerFailed) return false;
        if (bufferedBytesUnlocked() == 0 && !mProducerEof) {
            mError = QStringLiteral("Timed out while priming audio playback.");
            return false;
        }
        return true;
    }

    void stopDecodeThread()
    {
        QThread *thread = nullptr;
        {
            QMutexLocker locker(&mMutex);
            thread = mDecodeThread;
            if (!thread) return;
            mStopProducer = true;
            mBufferChanged.wakeAll();
        }
        thread->wait();
        delete thread;
        {
            QMutexLocker locker(&mMutex);
            if (mDecodeThread == thread) mDecodeThread = nullptr;
            mStopProducer = false;
        }
    }

    QString mFilePath;
    int mStreamIndex = -1;
    QAudioFormat mOutputFormat;
    int64_t mTotalSamples = 0;
    std::unique_ptr<FFmpegAudioDecoder> mDecoder;
    QByteArray mBuffer;
    qsizetype mBufferOffset = 0;
    int mBytesPerFrame = 0;
    int mSourceRate = 0;
    int mOutputRate = 0;
    int64_t mTotalOutputSamples = 0;
    int64_t mBaseOutputSample = 0;
    int64_t mBaseSample = 0;
    int64_t mProducedOutputSample = 0;
    int64_t mBytesDelivered = 0;
    qint64 mBufferTargetBytes = 0;
    char mSilenceByte = '\0';
    int mTestDecodeDelayMs = 0;
    QThread *mDecodeThread = nullptr;
    bool mStopProducer = false;
    bool mProducerEof = false;
    bool mProducerFailed = false;
    QString mError;
    QWaitCondition mBufferChanged;
    mutable QMutex mMutex;
};

#ifdef VDQT_AUDIO_TESTING
bool VDQtRunAudioBufferRegression(const QString& filePath, QString *errorMessage)
{
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);

    VDQtFFmpegAudioDevice device(filePath, -1, format, 0);
    if (!device.initialize()) {
        if (errorMessage) *errorMessage = device.error();
        return false;
    }

    const qint64 halfSecond = format.bytesForDuration(500000);
    if (device.bytesAvailable() < halfSecond) {
        if (errorMessage) *errorMessage = QStringLiteral("The playback device was not primed.");
        return false;
    }

    const qint64 pullSize = format.bytesForDuration(50000);
    QByteArray buffer(static_cast<qsizetype>(pullSize), Qt::Uninitialized);
    const auto readExact = [&device](char *destination, qint64 byteCount) {
        qint64 totalRead = 0;
        QElapsedTimer timer;
        timer.start();
        while (totalRead < byteCount && timer.elapsed() < 1000) {
            const qint64 amount = device.read(destination + totalRead,
                                              byteCount - totalRead);
            if (amount > 0)
                totalRead += amount;
            else
                QThread::msleep(1);
        }
        return totalRead;
    };
    std::array<int16_t, 2> previousSamples{};
    bool havePreviousSamples = false;
    int maximumSampleStep = 0;
    for (int pull = 0; pull < 20; ++pull) {
        if (readExact(buffer.data(), pullSize) != pullSize) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Buffered audio pull %1 returned early: %2")
                                    .arg(pull)
                                    .arg(QStringLiteral("%1 (available=%2, current=%3, atEnd=%4)")
                                             .arg(device.error())
                                             .arg(device.bytesAvailable())
                                             .arg(device.currentSample())
                                             .arg(device.atEnd()));
            }
            return false;
        }
        const auto *samples = reinterpret_cast<const int16_t *>(buffer.constData());
        const qsizetype frameCount = buffer.size() / (2 * static_cast<qsizetype>(sizeof(int16_t)));
        for (qsizetype frame = 0; frame < frameCount; ++frame) {
            for (int channel = 0; channel < 2; ++channel) {
                const int16_t sample = samples[frame * 2 + channel];
                if (havePreviousSamples) {
                    maximumSampleStep = std::max(
                        maximumSampleStep,
                        std::abs(static_cast<int>(sample) - previousSamples[channel]));
                }
                previousSamples[channel] = sample;
            }
            havePreviousSamples = true;
        }
    }
    if (maximumSampleStep > 1500) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Playback PCM contains a discontinuity of %1 levels.")
                                .arg(maximumSampleStep);
        }
        return false;
    }
    const int64_t oneSecondSample = device.currentSample();
    if (oneSecondSample < 48000 || oneSecondSample > 48000 + 4096) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Playback read-ahead was outside its bounded window: %1.")
                                .arg(oneSecondSample);
        }
        return false;
    }

    if (!device.seekToSample(24000)) {
        if (errorMessage) *errorMessage = device.error();
        return false;
    }
    if (device.bytesAvailable() < halfSecond || device.currentSample() != 24000) {
        if (errorMessage) *errorMessage = QStringLiteral("Seek did not rebuild the decode-ahead window.");
        return false;
    }
    if (readExact(buffer.data(), pullSize) != pullSize ||
        device.currentSample() < 26400 || device.currentSample() > 26400 + 4096) {
        if (errorMessage) *errorMessage = QStringLiteral("Playback did not resume continuously after seek.");
        return false;
    }
    return true;
}

bool VDQtRunAudioDecodeAheadDeadlineRegression(const QString& filePath, QString *errorMessage)
{
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);

    // Artificially make each codec pull expensive while leaving it faster than
    // real time overall. A synchronous QIODevice blocks for 30-45 ms per 50 ms
    // audio request; the rolling decode-ahead path should only copy buffered PCM.
    VDQtFFmpegAudioDevice device(filePath, -1, format, 0, 15);
    if (!device.initialize()) {
        if (errorMessage) *errorMessage = device.error();
        return false;
    }

    const qint64 pullSize = format.bytesForDuration(50000);
    QByteArray buffer(static_cast<qsizetype>(pullSize), Qt::Uninitialized);
    qint64 maximumReadTimeMs = 0;
    for (int pull = 0; pull < 20; ++pull) {
        QElapsedTimer readTimer;
        readTimer.start();
        const qint64 bytesRead = device.read(buffer.data(), pullSize);
        maximumReadTimeMs = std::max(maximumReadTimeMs, readTimer.elapsed());
        if (bytesRead != pullSize) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Decode-ahead pull %1 returned early: %2")
                                    .arg(pull)
                                    .arg(device.error());
            }
            return false;
        }
        QThread::msleep(50);
    }

    if (maximumReadTimeMs > 20) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("An audio callback blocked for %1 ms on codec work.")
                                .arg(maximumReadTimeMs);
        }
        return false;
    }
    return true;
}

bool VDQtRunAudioGapRegression(const QString& filePath,
                               int64_t gapStartSample,
                               int64_t gapLengthSamples,
                               QString *errorMessage)
{
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);

    VDQtFFmpegAudioDevice device(filePath, -1, format, 0);
    if (!device.initialize()) {
        if (errorMessage) *errorMessage = device.error();
        return false;
    }

    const int64_t endSample = gapStartSample + gapLengthSamples;
    const int64_t samplesToRead = endSample + 512;
    const qint64 bytesToRead = samplesToRead * format.bytesPerFrame();
    if (bytesToRead <= 0 || bytesToRead > std::numeric_limits<qsizetype>::max()) {
        if (errorMessage) *errorMessage = QStringLiteral("The gap fixture range is invalid.");
        return false;
    }

    QByteArray buffer(static_cast<qsizetype>(bytesToRead), Qt::Uninitialized);
    if (device.read(buffer.data(), bytesToRead) != bytesToRead) {
        if (errorMessage) *errorMessage = QStringLiteral("The gap fixture ended unexpectedly.");
        return false;
    }

    const auto *samples = reinterpret_cast<const int16_t *>(buffer.constData());
    for (int64_t frame = gapStartSample; frame < endSample; ++frame) {
        if (samples[frame * 2] != 0 || samples[frame * 2 + 1] != 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("A real %1-sample timeline gap was not preserved.")
                                    .arg(gapLengthSamples);
            }
            return false;
        }
    }

    int peakBefore = 0;
    int peakAfter = 0;
    for (int64_t frame = std::max<int64_t>(0, gapStartSample - 256);
         frame < gapStartSample;
         ++frame) {
        peakBefore = std::max(peakBefore, std::abs(static_cast<int>(samples[frame * 2])));
    }
    for (int64_t frame = endSample; frame < samplesToRead; ++frame) {
        peakAfter = std::max(peakAfter, std::abs(static_cast<int>(samples[frame * 2])));
    }
    if (peakBefore < 100 || peakAfter < 100) {
        if (errorMessage) *errorMessage = QStringLiteral("The gap fixture lacks audio around its gap.");
        return false;
    }
    return true;
}

bool VDQtRunAvsAudioDecodeAheadDeadlineRegression(AVS_Clip *clip,
                                                  const AVS_VideoInfo *vi,
                                                  QString *errorMessage)
{
    if (!clip || !vi || !avs_has_audio(vi) || vi->audio_samples_per_second <= 0 ||
        vi->nchannels <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("The AviSynth test clip has no audio.");
        return false;
    }

    // Artificial graph latency is paid by the producer thread during priming
    // and refill. Pulling from the QIODevice must remain a bounded buffer copy.
    AVSAudioDevice device(clip, vi, 15);
    if (!device.initialize()) {
        if (errorMessage) *errorMessage = device.error();
        return false;
    }

    const int64_t pullSamples = std::max<int64_t>(
        1, vi->audio_samples_per_second / 20);
    const qint64 pullBytes = pullSamples * vi->nchannels *
                             static_cast<qint64>(sizeof(int16_t));
    if (pullBytes <= 0 || pullBytes > std::numeric_limits<qsizetype>::max()) {
        if (errorMessage) *errorMessage = QStringLiteral("The AviSynth pull size is invalid.");
        return false;
    }

    QByteArray buffer(static_cast<qsizetype>(pullBytes), Qt::Uninitialized);
    qint64 maximumReadTimeMs = 0;
    for (int pull = 0; pull < 20; ++pull) {
        QElapsedTimer readTimer;
        readTimer.start();
        const qint64 bytesRead = device.read(buffer.data(), pullBytes);
        maximumReadTimeMs = std::max(maximumReadTimeMs, readTimer.elapsed());
        if (bytesRead != pullBytes) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Buffered AviSynth pull %1 returned early: %2")
                                    .arg(pull)
                                    .arg(device.error());
            }
            return false;
        }
        QThread::msleep(50);
    }

    if (maximumReadTimeMs > 20) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "An AviSynth audio callback blocked for %1 ms on graph evaluation.")
                                .arg(maximumReadTimeMs);
        }
        return false;
    }

    const int64_t seekSample = std::min<int64_t>(
        vi->num_audio_samples, vi->audio_samples_per_second / 2);
    if (!device.seekToSample(seekSample) || device.getCurrentSample() != seekSample ||
        device.read(buffer.data(), pullBytes) != pullBytes) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("AviSynth seek did not rebuild its decode-ahead window: %1")
                                .arg(device.error());
        }
        return false;
    }
    return true;
}
#endif

AVSAudioDevice::AVSAudioDevice(AVS_Clip *clip,
                               const AVS_VideoInfo *vi,
                               int testDecodeDelayMs,
                               QObject *parent)
    : QIODevice(parent)
    , m_clip(clip)
    , m_vi(vi)
    , m_testDecodeDelayMs(std::max(0, testDecodeDelayMs))
{
}

AVSAudioDevice::~AVSAudioDevice()
{
    close();
}

bool AVSAudioDevice::initialize()
{
    if (!m_clip || !m_vi || !avs_has_audio(m_vi) || m_vi->nchannels <= 0 ||
        m_vi->audio_samples_per_second <= 0 || m_vi->num_audio_samples <= 0) {
        QMutexLocker locker(&m_mutex);
        m_error = QStringLiteral("The AviSynth clip has no playable audio stream.");
        return false;
    }

    m_bytesPerFrame = m_vi->nchannels * static_cast<int>(sizeof(int16_t));
    QAudioFormat format;
    format.setSampleRate(m_vi->audio_samples_per_second);
    format.setChannelCount(m_vi->nchannels);
    format.setSampleFormat(QAudioFormat::Int16);
    m_bufferTargetBytes = alignedAudioBufferSize(
        format, kLiveAudioPrebufferUsecs, 64 * 1024, 4 * 1024 * 1024);

    if (!QIODevice::open(QIODevice::ReadOnly)) {
        QMutexLocker locker(&m_mutex);
        m_error = QStringLiteral("Could not open the AviSynth audio playback stream.");
        return false;
    }
    return startDecodeThreadAndPrime();
}

bool AVSAudioDevice::seekToSample(int64_t sample)
{
    stopDecodeThread();
    if (!m_vi || !avs_has_audio(m_vi)) return false;

    const int64_t maximum = std::max<int64_t>(0, m_vi->num_audio_samples);
    sample = std::clamp<int64_t>(sample, 0, maximum);

    // Discard any QIODevice read-ahead as well as the producer's decoded PCM.
    QIODevice::close();
    if (!QIODevice::open(QIODevice::ReadOnly)) {
        QMutexLocker locker(&m_mutex);
        m_error = QStringLiteral("Could not reopen the AviSynth audio stream after seeking.");
        return false;
    }
    {
        QMutexLocker locker(&m_mutex);
        m_buffer.clear();
        m_bufferOffset = 0;
        m_baseSample = sample;
        m_producerSample = sample;
        m_bytesDelivered = 0;
        m_producerEof = sample >= maximum;
        m_producerFailed = false;
        m_error.clear();
    }
    return startDecodeThreadAndPrime();
}

int64_t AVSAudioDevice::getCurrentSample() const
{
    QMutexLocker locker(&m_mutex);
    const int64_t deliveredSamples = m_bytesPerFrame > 0
        ? m_bytesDelivered / m_bytesPerFrame
        : 0;
    return m_baseSample + deliveredSamples;
}

QString AVSAudioDevice::error() const
{
    QMutexLocker locker(&m_mutex);
    return m_error;
}

bool AVSAudioDevice::atEnd() const
{
    QMutexLocker locker(&m_mutex);
    return m_producerEof && bufferedBytesUnlocked() == 0;
}

void AVSAudioDevice::close()
{
    stopDecodeThread();
    QIODevice::close();
}

qint64 AVSAudioDevice::bytesAvailable() const
{
    QMutexLocker locker(&m_mutex);
    qint64 buffered = bufferedBytesUnlocked();
    const qint64 baseAvailable = QIODevice::bytesAvailable();
    if (baseAvailable > 0 &&
        baseAvailable <= std::numeric_limits<qint64>::max() - buffered) {
        buffered += baseAvailable;
    }
    return buffered;
}

qint64 AVSAudioDevice::size() const
{
    if (!m_clip || !m_vi || !avs_has_audio(m_vi) || m_vi->nchannels <= 0) return 0;
    const int64_t bytesPerFrame = static_cast<int64_t>(m_vi->nchannels) * sizeof(int16_t);
    if (m_vi->num_audio_samples > std::numeric_limits<qint64>::max() / bytesPerFrame) {
        return std::numeric_limits<qint64>::max();
    }
    return m_vi->num_audio_samples * bytesPerFrame;
}

qint64 AVSAudioDevice::readData(char *data, qint64 maximumLength)
{
    if (!m_clip || !m_vi || !avs_has_audio(m_vi) || !data ||
        maximumLength <= 0 || m_bytesPerFrame <= 0) {
        return 0;
    }

    maximumLength -= maximumLength % m_bytesPerFrame;
    if (maximumLength <= 0) return 0;

    QMutexLocker locker(&m_mutex);
    qint64 copied = 0;
    bool waitedForProducer = false;
    while (copied < maximumLength) {
        const qint64 available = bufferedBytesUnlocked();
        if (available <= 0) {
            if (m_producerEof || m_producerFailed || waitedForProducer) break;
            // The callback may briefly yield to the producer, but it never
            // evaluates the AviSynth graph itself.
            waitedForProducer = true;
            m_bufferChanged.wait(&m_mutex, 20);
            continue;
        }

        qint64 amount = std::min(available, maximumLength - copied);
        amount -= amount % m_bytesPerFrame;
        if (amount <= 0) break;
        std::memcpy(data + copied, m_buffer.constData() + m_bufferOffset, amount);
        copied += amount;
        m_bufferOffset += static_cast<qsizetype>(amount);
        m_bytesDelivered += amount;
        compactBufferUnlocked();
        m_bufferChanged.wakeAll();
    }
    return copied;
}

qint64 AVSAudioDevice::bufferedBytesUnlocked() const
{
    return std::max<qint64>(0, m_buffer.size() - m_bufferOffset);
}

void AVSAudioDevice::compactBufferUnlocked()
{
    if (m_bufferOffset >= m_buffer.size()) {
        m_buffer.clear();
        m_bufferOffset = 0;
    } else if (m_bufferOffset > 0 && m_bufferOffset >= m_bufferTargetBytes / 2) {
        m_buffer.remove(0, m_bufferOffset);
        m_bufferOffset = 0;
    }
}

void AVSAudioDevice::decodeLoop()
{
    constexpr int64_t kDecodeChunkSamples = 32768;

    for (;;) {
        int64_t startSample = 0;
        int64_t samplesToRead = 0;
        {
            QMutexLocker locker(&m_mutex);
            while (!m_stopProducer && bufferedBytesUnlocked() >= m_bufferTargetBytes)
                m_bufferChanged.wait(&m_mutex);
            if (m_stopProducer) return;

            const int64_t remaining = m_vi->num_audio_samples - m_producerSample;
            if (remaining <= 0) {
                m_producerEof = true;
                m_bufferChanged.wakeAll();
                locker.unlock();
                Q_EMIT readyRead();
                return;
            }

            const qint64 freeBytes = std::max<qint64>(
                m_bytesPerFrame, m_bufferTargetBytes - bufferedBytesUnlocked());
            startSample = m_producerSample;
            samplesToRead = std::min({remaining,
                                      kDecodeChunkSamples,
                                      static_cast<int64_t>(freeBytes / m_bytesPerFrame)});
        }

        if (m_testDecodeDelayMs > 0)
            QThread::msleep(static_cast<unsigned long>(m_testDecodeDelayMs));

        qsizetype byteCount = 0;
        if (samplesToRead <= 0 ||
            !checkedBufferSize(samplesToRead, m_vi->nchannels,
                               static_cast<int>(sizeof(int16_t)), &byteCount)) {
            QMutexLocker locker(&m_mutex);
            m_error = QStringLiteral("The AviSynth playback buffer size is invalid.");
            m_producerFailed = true;
            m_producerEof = true;
            m_bufferChanged.wakeAll();
            locker.unlock();
            Q_EMIT readyRead();
            return;
        }

        QByteArray decoded(byteCount, Qt::Uninitialized);
        if (!readAvsAsInt16(m_clip, m_vi, startSample, samplesToRead, decoded.data())) {
            QMutexLocker locker(&m_mutex);
            m_error = QStringLiteral("AviSynth failed while decoding playback audio.");
            m_producerFailed = true;
            m_producerEof = true;
            m_bufferChanged.wakeAll();
            locker.unlock();
            Q_EMIT readyRead();
            return;
        }

        {
            QMutexLocker locker(&m_mutex);
            if (m_stopProducer) return;
            compactBufferUnlocked();
            m_buffer.append(decoded);
            m_producerSample += samplesToRead;
            if (m_producerSample >= m_vi->num_audio_samples)
                m_producerEof = true;
            m_bufferChanged.wakeAll();
        }
        Q_EMIT readyRead();
    }
}

bool AVSAudioDevice::startDecodeThreadAndPrime()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_decodeThread || !m_clip || !m_vi || m_bytesPerFrame <= 0) return false;
        if (m_producerEof) return true;
        m_stopProducer = false;
        m_producerFailed = false;
        m_decodeThread = QThread::create([this]() { decodeLoop(); });
        if (!m_decodeThread) {
            m_error = QStringLiteral("Could not create the AviSynth audio decode-ahead thread.");
            return false;
        }
    }

    m_decodeThread->start(QThread::HighPriority);

    QElapsedTimer timer;
    timer.start();
    QMutexLocker locker(&m_mutex);
    while (bufferedBytesUnlocked() < m_bufferTargetBytes &&
           !m_producerEof && !m_producerFailed && timer.elapsed() < 5000) {
        m_bufferChanged.wait(&m_mutex, 50);
    }
    if (m_producerFailed) return false;
    if (bufferedBytesUnlocked() == 0 && !m_producerEof) {
        m_error = QStringLiteral("Timed out while priming AviSynth audio playback.");
        return false;
    }
    return true;
}

void AVSAudioDevice::stopDecodeThread()
{
    QThread *thread = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        thread = m_decodeThread;
        if (!thread) return;
        m_stopProducer = true;
        m_bufferChanged.wakeAll();
    }
    thread->wait();
    delete thread;
    {
        QMutexLocker locker(&m_mutex);
        if (m_decodeThread == thread) m_decodeThread = nullptr;
        m_stopProducer = false;
    }
}

qint64 AVSAudioDevice::writeData(const char *, qint64)
{
    return -1;
}

VDQtAudioPlayer::VDQtAudioPlayer()
    : mIsOpen(false)
    , mHasAudio(false)
    , mIsPlaying(false)
    , mIsAvsAudio(false)
    , mSampleRate(48000)
    , mChannels(2)
    , mBitsPerSample(16)
    , mTotalSamples(0)
    , mTotalSamplesExact(false)
    , mAudioStreamIndex(-1)
    , mFormatCtx(nullptr)
    , mCodecCtx(nullptr)
    , mAudioSink(nullptr)
    , mFFmpegAudioDevice(nullptr)
    , mAvsAudioDevice(nullptr)
    , mClip(nullptr)
    , mVi(nullptr)
{
}

VDQtAudioPlayer::~VDQtAudioPlayer()
{
    close();
}

bool VDQtAudioPlayer::openAvsClip(AVS_Clip *clip, const AVS_VideoInfo *vi)
{
    close();
    if (!clip || !vi || !avs_has_audio(vi) || vi->audio_samples_per_second <= 0 ||
        vi->nchannels <= 0 || vi->num_audio_samples <= 0 ||
        avs_bytes_per_channel_sample(vi) <= 0) {
        return false;
    }

    mIsAvsAudio = true;
    mIsOpen = true;
    mHasAudio = true;
    mClip = clip;
    mVi = vi;
    mSampleRate = vi->audio_samples_per_second;
    mChannels = vi->nchannels;
    mBitsPerSample = avs_bytes_per_channel_sample(vi) * 8;
    mTotalSamples = vi->num_audio_samples;
    mTotalSamplesExact = true;

    AVChannelLayout layout = {};
    av_channel_layout_default(&layout, mChannels);
    mChannelLayoutName = describeLayout(&layout, mChannels);
    av_channel_layout_uninit(&layout);

    QAudioFormat format;
    format.setSampleRate(mSampleRate);
    format.setChannelCount(mChannels);
    format.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice defaultDevice = audioOutputEnabled()
        ? QMediaDevices::defaultAudioOutput()
        : QAudioDevice();
    if (!defaultDevice.isNull() && defaultDevice.isFormatSupported(format)) {
        mAvsAudioDevice = new AVSAudioDevice(clip, vi);
        if (mAvsAudioDevice->initialize()) {
            mAudioSink = new QAudioSink(defaultDevice, format);
            configureLiveAudioSink(mAudioSink, format);
        } else {
            qWarning() << "[VDQtAudioPlayer] AviSynth playback initialization failed:"
                       << mAvsAudioDevice->error();
            delete mAvsAudioDevice;
            mAvsAudioDevice = nullptr;
        }
    } else if (!defaultDevice.isNull()) {
        qWarning() << "[VDQtAudioPlayer] The audio device does not support"
                   << mSampleRate << "Hz" << mChannels
                   << "channel Int16 AviSynth playback; export remains available.";
    } else {
        qWarning() << "[VDQtAudioPlayer] No audio output device is available; export remains available.";
    }

    qDebug() << "[VDQtAudioPlayer] Opened AviSynth audio stream:"
             << mSampleRate << "Hz," << mChannels << "channels,"
             << mBitsPerSample << "bits," << mTotalSamples << "samples.";
    return true;
}

bool VDQtAudioPlayer::openFile(const QString &filePath)
{
    close();
    mFilePath = filePath;

    const bool isScript = filePath.endsWith(".avs", Qt::CaseInsensitive) ||
                          filePath.endsWith(".vpy", Qt::CaseInsensitive);
    const AVInputFormat *inputFormat = nullptr;
    if (isScript) {
        inputFormat = av_find_input_format(filePath.endsWith(".avs", Qt::CaseInsensitive)
                                               ? "avisynth"
                                               : "vapoursynth");
        if (!inputFormat) {
            const QString resolvedMedia = VDQtVideoDecoder::parseScriptSource(filePath);
            if (!resolvedMedia.isEmpty() && QFile::exists(resolvedMedia)) {
                return openFile(resolvedMedia);
            }
            mIsOpen = true;
            mHasAudio = false;
            return true;
        }
    }

    const QByteArray encodedPath = QFile::encodeName(filePath);
    int result = avformat_open_input(&mFormatCtx,
                                     encodedPath.constData(),
                                     inputFormat,
                                     nullptr);
    if (result < 0) {
        qWarning() << "[VDQtAudioPlayer] Could not open audio source:"
                   << avErrorString(result);
        close();
        return false;
    }

    result = avformat_find_stream_info(mFormatCtx, nullptr);
    if (result < 0) {
        qWarning() << "[VDQtAudioPlayer] Could not read stream information:"
                   << avErrorString(result);
        close();
        return false;
    }

    mAudioStreamIndex = av_find_best_stream(mFormatCtx,
                                            AVMEDIA_TYPE_AUDIO,
                                            -1,
                                            -1,
                                            nullptr,
                                            0);
    if (mAudioStreamIndex < 0) {
        qDebug() << "[VDQtAudioPlayer] No audio stream found in:" << filePath;
        mAudioStreamIndex = -1;
        mIsOpen = true;
        mHasAudio = false;
        return true;
    }

    AVStream *audioStream = mFormatCtx->streams[mAudioStreamIndex];
    const AVCodec *codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
    if (!codec) {
        qWarning() << "[VDQtAudioPlayer] No decoder is available for codec"
                   << avcodec_get_name(audioStream->codecpar->codec_id);
        close();
        return false;
    }

    mCodecCtx = avcodec_alloc_context3(codec);
    if (!mCodecCtx) {
        close();
        return false;
    }

    result = avcodec_parameters_to_context(mCodecCtx, audioStream->codecpar);
    if (result < 0) {
        qWarning() << "[VDQtAudioPlayer] Could not initialize audio decoder:"
                   << avErrorString(result);
        close();
        return false;
    }
    result = avcodec_open2(mCodecCtx, codec, nullptr);
    if (result < 0) {
        qWarning() << "[VDQtAudioPlayer] Could not open audio decoder:"
                   << avErrorString(result);
        close();
        return false;
    }

    mSampleRate = mCodecCtx->sample_rate > 0
        ? mCodecCtx->sample_rate
        : audioStream->codecpar->sample_rate;
    mChannels = mCodecCtx->ch_layout.nb_channels > 0
        ? mCodecCtx->ch_layout.nb_channels
        : audioStream->codecpar->ch_layout.nb_channels;
    if (mSampleRate <= 0 || mChannels <= 0) {
        qWarning() << "[VDQtAudioPlayer] Audio stream has invalid rate or channel metadata.";
        close();
        return false;
    }

    mBitsPerSample = mCodecCtx->bits_per_raw_sample;
    if (mBitsPerSample <= 0) mBitsPerSample = audioStream->codecpar->bits_per_raw_sample;
    if (mBitsPerSample <= 0) mBitsPerSample = bitsForSampleFormat(mCodecCtx->sample_fmt);
    if (mBitsPerSample <= 0) mBitsPerSample = audioStream->codecpar->bits_per_coded_sample;
    if (mBitsPerSample <= 0) mBitsPerSample = 16;

    mChannelLayoutName = describeLayout(&mCodecCtx->ch_layout, mChannels);
    const int64_t timelineOrigin = timelineOriginForAudio(mFormatCtx, audioStream);
    const int64_t timelineEnd = timelineEndForAudio(mFormatCtx,
                                                     audioStream,
                                                     timelineOrigin);
    mTotalSamples = timelineEnd != AV_NOPTS_VALUE && timelineEnd > timelineOrigin
        ? av_rescale_q(timelineEnd - timelineOrigin,
                       audioStream->time_base,
                       AVRational{1, mSampleRate})
        : 0;
    // Demuxer durations are useful for display/progress but are often estimates,
    // and can be shorter than the final decoded frame. Only AviSynth exposes an
    // authoritative sample count; regular media must run to decoder EOF.
    mTotalSamplesExact = false;

    mIsOpen = true;
    mHasAudio = true;

    const QAudioDevice outputDevice = audioOutputEnabled()
        ? QMediaDevices::defaultAudioOutput()
        : QAudioDevice();
    if (!outputDevice.isNull()) {
        QAudioFormat playbackFormat;
        playbackFormat.setSampleRate(mSampleRate);
        playbackFormat.setChannelCount(mChannels);
        playbackFormat.setSampleFormat(QAudioFormat::Int16);
        if (!outputDevice.isFormatSupported(playbackFormat)) {
            playbackFormat = outputDevice.preferredFormat();
        }

        if (playbackFormat.sampleRate() > 0 && playbackFormat.channelCount() > 0 &&
            qtSampleFormatToAV(playbackFormat.sampleFormat()) != AV_SAMPLE_FMT_NONE) {
            mFFmpegAudioDevice = new VDQtFFmpegAudioDevice(filePath,
                                                           mAudioStreamIndex,
                                                           playbackFormat,
                                                           mTotalSamplesExact ? mTotalSamples : 0);
            if (mFFmpegAudioDevice->initialize()) {
                mAudioSink = new QAudioSink(outputDevice, playbackFormat);
                configureLiveAudioSink(mAudioSink, playbackFormat);
            } else {
                qWarning() << "[VDQtAudioPlayer] Live playback initialization failed:"
                           << mFFmpegAudioDevice->error();
                delete mFFmpegAudioDevice;
                mFFmpegAudioDevice = nullptr;
            }
        }
    } else {
        qWarning() << "[VDQtAudioPlayer] No audio output device is available; export remains available.";
    }

    qDebug() << "[VDQtAudioPlayer] Opened audio stream without eager decoding:"
             << mSampleRate << "Hz," << mChannels << "channels,"
             << mBitsPerSample << "bits," << mChannelLayoutName;
    return true;
}

void VDQtAudioPlayer::close()
{
    mIsPlaying = false;

    if (mAudioSink) {
        mAudioSink->stop();
        mAudioSink->reset();
        delete mAudioSink;
        mAudioSink = nullptr;
    }
    if (mFFmpegAudioDevice) {
        mFFmpegAudioDevice->close();
        delete mFFmpegAudioDevice;
        mFFmpegAudioDevice = nullptr;
    }
    if (mAvsAudioDevice) {
        mAvsAudioDevice->close();
        delete mAvsAudioDevice;
        mAvsAudioDevice = nullptr;
    }
    if (mCodecCtx) avcodec_free_context(&mCodecCtx);
    if (mFormatCtx) avformat_close_input(&mFormatCtx);

    mIsOpen = false;
    mHasAudio = false;
    mIsAvsAudio = false;
    mSampleRate = 48000;
    mChannels = 2;
    mBitsPerSample = 16;
    mTotalSamples = 0;
    mTotalSamplesExact = false;
    mFilePath.clear();
    mAudioStreamIndex = -1;
    mChannelLayoutName.clear();
    mClip = nullptr;
    mVi = nullptr;
}

void VDQtAudioPlayer::play()
{
    if (!mHasAudio || !mAudioSink) return;

    if (mAudioSink->state() == QAudio::SuspendedState) {
        mAudioSink->resume();
    } else if (mAudioSink->state() != QAudio::ActiveState) {
        if (mIsAvsAudio && mAvsAudioDevice) {
            mAudioSink->start(mAvsAudioDevice);
        } else if (mFFmpegAudioDevice) {
            mAudioSink->start(mFFmpegAudioDevice);
        } else {
            return;
        }
    }
    mIsPlaying = true;
}

void VDQtAudioPlayer::pause()
{
    if (!mHasAudio || !mAudioSink) return;
    mAudioSink->suspend();
    mIsPlaying = false;
}

void VDQtAudioPlayer::stop()
{
    if (mAudioSink) {
        mAudioSink->stop();
        mAudioSink->reset();
    }
    if (mIsAvsAudio && mAvsAudioDevice) {
        mAvsAudioDevice->seekToSample(0);
    } else if (mFFmpegAudioDevice) {
        mFFmpegAudioDevice->seekToSample(0);
    }
    mIsPlaying = false;
}

void VDQtAudioPlayer::seekToFrame(int frameIndex, double fps)
{
    if (fps <= 0.0) return;
    seekToTimeSeconds(std::max(0, frameIndex) / fps);
}

void VDQtAudioPlayer::seekToTimeSeconds(double timeSeconds)
{
    if (!mHasAudio || !std::isfinite(timeSeconds) || mSampleRate <= 0) return;

    if (mAudioSink && mAudioSink->state() != QAudio::StoppedState) {
        mAudioSink->stop();
        mAudioSink->reset();
    }

    timeSeconds = std::max(0.0, timeSeconds);
    int64_t sample = static_cast<int64_t>(std::llround(timeSeconds * mSampleRate));
    if (mTotalSamplesExact && mTotalSamples > 0) {
        sample = std::min(sample, mTotalSamples);
    }

    if (mIsAvsAudio && mAvsAudioDevice) {
        mAvsAudioDevice->seekToSample(sample);
    } else if (mFFmpegAudioDevice) {
        mFFmpegAudioDevice->seekToSample(sample);
    }
    mIsPlaying = false;
}

double VDQtAudioPlayer::getCurrentAudioTimeSeconds() const
{
    if (!mHasAudio || mSampleRate <= 0) return -1.0;
    if (mIsAvsAudio && mAvsAudioDevice) {
        return static_cast<double>(mAvsAudioDevice->getCurrentSample()) / mSampleRate;
    }
    if (mFFmpegAudioDevice) {
        return static_cast<double>(mFFmpegAudioDevice->currentSample()) / mSampleRate;
    }
    return -1.0;
}

QString VDQtAudioPlayer::getAudioLayoutString() const
{
    if (!mHasAudio) return "No audio";
    const QString layout = mChannelLayoutName.isEmpty()
        ? QString("%1 ch").arg(mChannels)
        : mChannelLayoutName;
    return QString("%1 Hz %2-bit %3").arg(mSampleRate).arg(mBitsPerSample).arg(layout);
}

QString VDQtAudioPlayer::getAudioCompressionString() const
{
    if (!mHasAudio) return "No audio";
    if (mIsAvsAudio) return "No compression (PCM)";
    if (mCodecCtx) {
        const char *name = avcodec_get_name(mCodecCtx->codec_id);
        if (name) {
            const QString codecName = QString::fromUtf8(name).toUpper();
            if (codecName.startsWith("PCM")) return "No compression (PCM)";
            return codecName;
        }
    }
    return "Unknown";
}

bool VDQtAudioPlayer::exportAudioToFile(
    const QString &outputPath,
    int64_t startSample,
    int64_t sampleCount,
    std::function<bool(int progress, int total)> progressCallback)
{
#ifdef VDQT_AUDIO_TESTING
    mLastExportUsedSeek = false;
    mLastExportDecodedSamples = 0;
#endif
    if (!mHasAudio || outputPath.isEmpty()) return false;
    if (!mFilePath.isEmpty() &&
        QFileInfo(outputPath).absoluteFilePath() == QFileInfo(mFilePath).absoluteFilePath()) {
        qWarning() << "[VDQtAudioPlayer] Refusing to overwrite the active source file.";
        return false;
    }

    QTemporaryFile stagedOutput(stagedOutputTemplate(outputPath));
    stagedOutput.setAutoRemove(true);
    if (!stagedOutput.open()) {
        qWarning() << "[VDQtAudioPlayer] Could not create a staged output in the destination directory.";
        return false;
    }
    const QString stagedOutputPath = stagedOutput.fileName();
    stagedOutput.close();

    startSample = std::max<int64_t>(0, startSample);
    if (mTotalSamplesExact && mTotalSamples > 0) {
        startSample = std::min(startSample, mTotalSamples);
    }
    const int64_t maximumAvailable = mTotalSamplesExact && mTotalSamples > 0
        ? std::max<int64_t>(0, mTotalSamples - startSample)
        : -1;
    if (sampleCount > 0 && maximumAvailable >= 0) {
        sampleCount = std::min(sampleCount, maximumAvailable);
    } else if (sampleCount <= 0 && maximumAvailable >= 0) {
        sampleCount = maximumAvailable;
    }
    if (sampleCount == 0 || maximumAvailable == 0) return false;

    const bool needsTranscode = !outputPath.endsWith(".wav", Qt::CaseInsensitive);
    QTemporaryFile temporaryWav(QDir::tempPath() + "/virtualdub2-audio-XXXXXX.wav");
    temporaryWav.setAutoRemove(true);
    QString wavPath = stagedOutputPath;
    if (needsTranscode) {
        if (!temporaryWav.open()) return false;
        wavPath = temporaryWav.fileName();
        temporaryWav.close();
    }

    bool extractionSucceeded = false;
    int exportedBits = mBitsPerSample;
    const int extractionProgressMaximum = needsTranscode ? 90 : 100;

    if (mIsAvsAudio && mClip && mVi) {
        const int bytesPerChannel = avs_bytes_per_channel_sample(mVi);
        if (bytesPerChannel <= 0) return false;
        const int containerBits = bytesPerChannel * 8;
        const bool floatingPoint = mVi->sample_type == AVS_SAMPLE_FLOAT;
        exportedBits = containerBits;
        AVChannelLayout layout = {};
        av_channel_layout_default(&layout, mChannels);

        const int64_t samplesToExport = sampleCount > 0
            ? sampleCount
            : mVi->num_audio_samples - startSample;
        const int64_t bytesPerFrame = static_cast<int64_t>(mChannels) * bytesPerChannel;
        if (samplesToExport <= 0 ||
            samplesToExport > std::numeric_limits<int64_t>::max() / bytesPerFrame) {
            av_channel_layout_uninit(&layout);
            return false;
        }
        const int64_t expectedBytes = samplesToExport * bytesPerFrame;

        WavWriter writer;
        if (!writer.open(wavPath,
                         mChannels,
                         mSampleRate,
                         containerBits,
                         containerBits,
                         floatingPoint,
                         wavChannelMask(&layout),
                         expectedBytes)) {
            av_channel_layout_uninit(&layout);
            return false;
        }
        av_channel_layout_uninit(&layout);

        const int64_t endSample = startSample + samplesToExport;
        int64_t currentSample = startSample;
        int lastPercent = -1;
        bool ok = true;
        constexpr int64_t chunkSamples = 32768;
        while (currentSample < endSample) {
            if (!reportProgress(progressCallback,
                                currentSample - startSample,
                                samplesToExport,
                                &lastPercent,
                                extractionProgressMaximum)) {
                ok = false;
                break;
            }

            const int64_t count = std::min(chunkSamples, endSample - currentSample);
            qsizetype byteCount = 0;
            if (!checkedBufferSize(count, mChannels, bytesPerChannel, &byteCount) ||
                byteCount > std::numeric_limits<int>::max()) {
                ok = false;
                break;
            }
            QByteArray buffer(static_cast<int>(byteCount), Qt::Uninitialized);
            if (avs_get_audio(mClip, buffer.data(), currentSample, count) != 0 ||
                !writer.write(buffer.constData(), buffer.size())) {
                ok = false;
                break;
            }
            currentSample += count;
        }

        if (ok && (!progressCallback ||
                   progressCallback(extractionProgressMaximum, 100))) {
            extractionSucceeded = writer.finalize();
        } else {
            writer.abort();
        }
    } else if (!mFilePath.isEmpty()) {
        FFmpegAudioDecoder decoder;
        if (!decoder.open(mFilePath, mAudioStreamIndex)) {
            qWarning() << "[VDQtAudioPlayer] Audio export decoder initialization failed:"
                       << decoder.error();
            return false;
        }

        const PcmOutputSpec spec = choosePcmOutput(decoder.sourceSampleFormat(),
                                                   decoder.sourceBitsPerSample());
        exportedBits = spec.validBits;
        if (!decoder.configureOutput(spec.sampleFormat,
                                     decoder.inputRate(),
                                     decoder.sourceLayout())) {
            qWarning() << "[VDQtAudioPlayer] Audio export converter initialization failed:"
                       << decoder.error();
            return false;
        }

        const int bytesPerFrame = decoder.bytesPerFrame();
        const int64_t expectedSamples = sampleCount > 0 ? sampleCount : -1;
        const int64_t expectedBytes = expectedSamples > 0 &&
                                      expectedSamples <= std::numeric_limits<int64_t>::max() / bytesPerFrame
            ? expectedSamples * bytesPerFrame
            : -1;

        WavWriter writer;
        if (!writer.open(wavPath,
                         decoder.sourceLayout()->nb_channels,
                         decoder.inputRate(),
                         spec.containerBits,
                         spec.validBits,
                         spec.floatingPoint,
                         wavChannelMask(decoder.sourceLayout()),
                         expectedBytes)) {
            return false;
        }

        const int64_t wantedEnd = sampleCount > 0 &&
                                  startSample <= std::numeric_limits<int64_t>::max() - sampleCount
            ? startSample + sampleCount
            : std::numeric_limits<int64_t>::max();
        const int64_t progressTotal = sampleCount > 0
            ? sampleCount
            : std::max<int64_t>(mTotalSamples > startSample
                                    ? mTotalSamples - startSample
                                    : 0,
                                1);
        const bool hasExactProgressEnd = wantedEnd != std::numeric_limits<int64_t>::max();
        int64_t decodedCursor = 0;
        int64_t writtenSamples = 0;
        int lastPercent = -1;
        bool ok = true;

        // Seeking with a short decode preroll retains codec/resampler history
        // while avoiding a decode of the entire file for a late selection.
        // Timestamp-based trimming below still determines the exact first
        // output sample, including intentional gaps and stream offsets.
        constexpr int64_t kSeekPrerollSeconds = 2;
        constexpr int64_t kMinimumSeekSeconds = 4;
        if (startSample > static_cast<int64_t>(decoder.outputRate()) * kMinimumSeekSeconds) {
            const int64_t prerollSamples = static_cast<int64_t>(decoder.outputRate())
                                         * kSeekPrerollSeconds;
            const int64_t seekSample = std::max<int64_t>(0, startSample - prerollSamples);
            if (decoder.seekToSample(seekSample)) {
                decodedCursor = seekSample;
#ifdef VDQT_AUDIO_TESTING
                mLastExportUsedSeek = true;
#endif
            }
        }

        const auto progressAtTimelineSample = [startSample, progressTotal](int64_t sample) {
            const int64_t relative = sample > startSample ? sample - startSample : 0;
            return std::min(relative, progressTotal);
        };

        auto writeSilence = [&](int64_t timelineStart, int64_t samples) {
            constexpr int64_t kSilenceChunkSamples = 32768;
            const char silenceByte = spec.sampleFormat == AV_SAMPLE_FMT_U8
                ? static_cast<char>(0x80)
                : '\0';
            int64_t completed = 0;
            while (samples > 0) {
                if (!reportProgress(progressCallback,
                                    progressAtTimelineSample(timelineStart + completed),
                                    progressTotal,
                                    hasExactProgressEnd ? &lastPercent : nullptr,
                                    extractionProgressMaximum)) {
                    return false;
                }
                const int64_t count = std::min(samples, kSilenceChunkSamples);
                if (count > std::numeric_limits<int>::max() / bytesPerFrame)
                    return false;
                QByteArray silence(static_cast<int>(count * bytesPerFrame), silenceByte);
                if (!writer.write(silence.constData(), silence.size())) return false;
                writtenSamples += count;
                completed += count;
                samples -= count;
            }
            return true;
        };

        QByteArray pcm;
        int decodedSamples = 0;
        int64_t decodedTimestamp = AV_NOPTS_VALUE;
        while (decoder.nextChunk(&pcm, &decodedSamples, &decodedTimestamp)) {
#ifdef VDQT_AUDIO_TESTING
            mLastExportDecodedSamples += decodedSamples;
#endif
            const int64_t chunkStart = decodedTimestamp != AV_NOPTS_VALUE
                ? decodedTimestamp
                : decodedCursor;
            if (decodedSamples < 0 || chunkStart > std::numeric_limits<int64_t>::max() - decodedSamples) {
                ok = false;
                break;
            }
            const int64_t chunkEnd = chunkStart + decodedSamples;

            if (chunkStart > decodedCursor) {
                const int64_t silenceStart = std::max(startSample, decodedCursor);
                const int64_t silenceEnd = std::min(wantedEnd, chunkStart);
                if (silenceStart < silenceEnd &&
                    !writeSilence(silenceStart, silenceEnd - silenceStart)) {
                    ok = false;
                    break;
                }
                if (silenceEnd >= wantedEnd) {
                    decodedCursor = chunkStart;
                    break;
                }
            }

            if (!reportProgress(progressCallback,
                                progressAtTimelineSample(std::max(decodedCursor, chunkEnd)),
                                progressTotal,
                                hasExactProgressEnd ? &lastPercent : nullptr,
                                extractionProgressMaximum)) {
                ok = false;
                break;
            }

            const int64_t writeStart = std::max({startSample, chunkStart, decodedCursor});
            const int64_t writeEnd = std::min(wantedEnd, chunkEnd);
            if (writeStart < writeEnd) {
                const int64_t skipSamples = writeStart - chunkStart;
                const int64_t samplesFromChunk = writeEnd - writeStart;
                const int64_t byteOffset = skipSamples * bytesPerFrame;
                const int64_t byteCount = samplesFromChunk * bytesPerFrame;
                if (byteOffset < 0 || byteCount < 0 ||
                    byteOffset + byteCount > pcm.size() ||
                    !writer.write(pcm.constData() + byteOffset, byteCount)) {
                    ok = false;
                    break;
                }
                writtenSamples += samplesFromChunk;
            }

            decodedCursor = std::max(decodedCursor, chunkEnd);

            if (decodedCursor >= wantedEnd) break;
        }

        if (ok && !decoder.failed() && wantedEnd != std::numeric_limits<int64_t>::max() &&
            decodedCursor < wantedEnd) {
            const int64_t silenceStart = std::max(startSample, decodedCursor);
            if (silenceStart < wantedEnd &&
                !writeSilence(silenceStart, wantedEnd - silenceStart)) {
                ok = false;
            }
        }

        if (decoder.failed()) {
            qWarning() << "[VDQtAudioPlayer] Audio export decoding failed:"
                       << decoder.error();
            ok = false;
        }
        if (ok && sampleCount > 0 && writtenSamples != sampleCount) {
            qWarning() << "[VDQtAudioPlayer] Audio source ended before the requested sample range:"
                       << writtenSamples << "of" << sampleCount << "samples were available.";
            ok = false;
        }
        if (writtenSamples <= 0) ok = false;

        if (ok && (!progressCallback ||
                   progressCallback(extractionProgressMaximum, 100))) {
            extractionSucceeded = writer.finalize();
        } else {
            writer.abort();
        }
    }

    if (!extractionSucceeded) {
        if (!needsTranscode) QFile::remove(stagedOutputPath);
        return false;
    }

    bool outputSucceeded = true;
    if (needsTranscode) {
        outputSucceeded = transcodeTemporaryWav(wavPath,
                                                stagedOutputPath,
                                                exportedBits,
                                                progressCallback);
    } else if (progressCallback && !progressCallback(100, 100)) {
        outputSucceeded = false;
    }

    if (!outputSucceeded || !replaceWithStagedFile(stagedOutputPath, outputPath)) {
        QFile::remove(stagedOutputPath);
        return false;
    }
    return true;
}
