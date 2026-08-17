#include "VDQtAudioPlayer.h"
#include "VDQtVideoDecoder.h"

#include <QAudioDevice>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QTemporaryFile>

#include <algorithm>
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
                    frameStart != mLastPresentationEndSample) {
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

    int64_t framePresentationEndSample(const AVFrame *frame) const
    {
        if (!frame || frame->best_effort_timestamp == AV_NOPTS_VALUE || frame->duration <= 0 ||
            frame->best_effort_timestamp >
                std::numeric_limits<int64_t>::max() - frame->duration) {
            return AV_NOPTS_VALUE;
        }
        return outputSampleForTimestamp(frame->best_effort_timestamp + frame->duration);
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
        const int64_t frameStart = frameStartSample(mFrame);
        const int64_t presentationEnd = framePresentationEndSample(mFrame);
        int64_t chunkStart = AV_NOPTS_VALUE;
        if (frameStart != AV_NOPTS_VALUE) {
            const int64_t delayedOutput = av_rescale_rnd(delayBeforeConversion,
                                                         mOutputRate,
                                                         mInputRate,
                                                         AV_ROUND_NEAR_INF);
            chunkStart = frameStart - delayedOutput;
            *startSample = chunkStart;

            // A final compressed frame can contain codec padding beyond its
            // declared presentation duration. Keep the decoder draining to EOF,
            // but never expose samples beyond that timestamp boundary.
            if (presentationEnd != AV_NOPTS_VALUE) {
                const int64_t maximumValid = std::max<int64_t>(0, presentationEnd - chunkStart);
                validSamples = static_cast<int>(std::min<int64_t>(validSamples, maximumValid));
                mLastPresentationEndSample = presentationEnd;
            } else {
                mLastPresentationEndSample = AV_NOPTS_VALUE;
            }

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
                         QObject *parent = nullptr)
        : QIODevice(parent)
        , mFilePath(filePath)
        , mStreamIndex(streamIndex)
        , mOutputFormat(outputFormat)
        , mTotalSamples(totalSamples)
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
        mDecoder = std::move(decoder);
        return open(QIODevice::ReadOnly);
    }

    bool isSequential() const override { return true; }

    qint64 bytesAvailable() const override
    {
        QMutexLocker locker(&mMutex);
        const qint64 buffered = std::max<qint64>(0, mPending.size() - mPendingOffset);
        const bool trailingTimelineData = mTotalOutputSamples > 0 &&
            currentOutputSampleUnlocked() < mTotalOutputSamples;
        const qint64 prospective = (!mDecoder ||
            (mDecoder->atEnd() && mGapBytesRemaining <= 0 && !trailingTimelineData))
            ? 0
            : 4096;
        qint64 available = buffered;
        const auto addSaturated = [&available](qint64 amount) {
            if (amount <= 0) return;
            if (amount > std::numeric_limits<qint64>::max() - available)
                available = std::numeric_limits<qint64>::max();
            else
                available += amount;
        };
        addSaturated(mGapBytesRemaining);
        addSaturated(prospective);
        addSaturated(QIODevice::bytesAvailable());
        return available;
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
        QMutexLocker locker(&mMutex);
        if (!mDecoder) return false;
        sample = std::max<int64_t>(0, sample);
        if (mTotalSamples > 0) sample = std::min(sample, mTotalSamples);
        const int64_t outputSample = (mSourceRate > 0 && mOutputRate > 0)
            ? av_rescale_rnd(sample, mOutputRate, mSourceRate, AV_ROUND_NEAR_INF)
            : sample;
        if (!mDecoder->seekToSample(outputSample)) {
            mError = mDecoder->error();
            return false;
        }

        // QIODevice may buffer more bytes than the caller requested. Reopening
        // discards that private read-ahead so data from before the decoder seek
        // cannot leak into the new logical position.
        QIODevice::close();
        if (!QIODevice::open(QIODevice::ReadOnly)) {
            mError = QStringLiteral("Could not reopen the audio playback stream after seeking.");
            return false;
        }
        mPending.clear();
        mPendingOffset = 0;
        mGapBytesRemaining = 0;
        mBaseOutputSample = outputSample;
        mBaseSample = sample;
        mBytesDelivered = 0;
        mTailSilenceScheduled = false;
        return true;
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

        qint64 copied = 0;
        while (copied < maximumLength) {
            if (mGapBytesRemaining > 0) {
                const qint64 amount = std::min(mGapBytesRemaining, maximumLength - copied);
                std::memset(data + copied, static_cast<unsigned char>(mSilenceByte), amount);
                copied += amount;
                mGapBytesRemaining -= amount;
                mBytesDelivered += amount;
                continue;
            }

            if (mPendingOffset < mPending.size()) {
                const qint64 available = mPending.size() - mPendingOffset;
                const qint64 amount = std::min(available, maximumLength - copied);
                std::memcpy(data + copied, mPending.constData() + mPendingOffset, amount);
                copied += amount;
                mPendingOffset += amount;
                mBytesDelivered += amount;
                if (mPendingOffset == mPending.size()) {
                    mPending.clear();
                    mPendingOffset = 0;
                }
                continue;
            }

            int samples = 0;
            int64_t chunkStart = AV_NOPTS_VALUE;
            if (!mDecoder->nextChunk(&mPending, &samples, &chunkStart)) {
                if (mDecoder->failed()) mError = mDecoder->error();
                const int64_t cursor = currentOutputSampleUnlocked();
                if (!mDecoder->failed() && !mTailSilenceScheduled &&
                    mTotalOutputSamples > cursor) {
                    const int64_t remainingSamples = mTotalOutputSamples - cursor;
                    if (remainingSamples <= std::numeric_limits<qint64>::max() / mBytesPerFrame) {
                        mGapBytesRemaining = remainingSamples * mBytesPerFrame;
                        mTailSilenceScheduled = true;
                        continue;
                    }
                    mError = QStringLiteral("The trailing audio gap is too large.");
                }
                break;
            }
            mPendingOffset = 0;

            const int64_t cursor = currentOutputSampleUnlocked();
            if (chunkStart == AV_NOPTS_VALUE) chunkStart = cursor;
            if (samples < 0 || chunkStart > std::numeric_limits<int64_t>::max() - samples) {
                mPending.clear();
                mError = QStringLiteral("The decoded audio timestamp is invalid.");
                break;
            }
            const int64_t chunkEnd = chunkStart + samples;
            if (chunkEnd <= cursor) {
                mPending.clear();
                continue;
            }
            if (chunkStart < cursor) {
                const int64_t samplesToDiscard = cursor - chunkStart;
                const int64_t bytesToDiscard = samplesToDiscard * mBytesPerFrame;
                if (bytesToDiscard >= mPending.size()) {
                    mPending.clear();
                    continue;
                }
                mPendingOffset = static_cast<qsizetype>(bytesToDiscard);
            } else if (chunkStart > cursor) {
                const int64_t gapSamples = chunkStart - cursor;
                if (gapSamples > std::numeric_limits<qint64>::max() / mBytesPerFrame) {
                    mPending.clear();
                    mError = QStringLiteral("The decoded audio gap is too large.");
                    break;
                }
                mGapBytesRemaining = gapSamples * mBytesPerFrame;
            }
        }

        return copied;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    int64_t currentOutputSampleUnlocked() const
    {
        return mBaseOutputSample +
            (mBytesPerFrame > 0 ? mBytesDelivered / mBytesPerFrame : 0);
    }

    QString mFilePath;
    int mStreamIndex = -1;
    QAudioFormat mOutputFormat;
    int64_t mTotalSamples = 0;
    std::unique_ptr<FFmpegAudioDecoder> mDecoder;
    QByteArray mPending;
    qsizetype mPendingOffset = 0;
    int mBytesPerFrame = 0;
    int mSourceRate = 0;
    int mOutputRate = 0;
    int64_t mTotalOutputSamples = 0;
    int64_t mBaseOutputSample = 0;
    int64_t mBaseSample = 0;
    int64_t mBytesDelivered = 0;
    qint64 mGapBytesRemaining = 0;
    char mSilenceByte = '\0';
    bool mTailSilenceScheduled = false;
    QString mError;
    mutable QMutex mMutex;
};

AVSAudioDevice::AVSAudioDevice(AVS_Clip *clip, const AVS_VideoInfo *vi, QObject *parent)
    : QIODevice(parent)
    , m_clip(clip)
    , m_vi(vi)
    , m_currentSample(0)
{
    open(QIODevice::ReadOnly);
}

void AVSAudioDevice::setClip(AVS_Clip *clip, const AVS_VideoInfo *vi)
{
    QIODevice::close();
    m_clip = clip;
    m_vi = vi;
    m_currentSample = 0;
    QIODevice::open(QIODevice::ReadOnly);
}

void AVSAudioDevice::seekToSample(int64_t sample)
{
    const int64_t maximum = (m_vi && avs_has_audio(m_vi)) ? m_vi->num_audio_samples : 0;
    m_currentSample = std::clamp<int64_t>(sample, 0, std::max<int64_t>(0, maximum));
    // Clear QIODevice's read-ahead buffer, as the device is intentionally
    // sequential and its public byte position is unrelated to sample seeking.
    QIODevice::close();
    QIODevice::open(QIODevice::ReadOnly);
}

qint64 AVSAudioDevice::bytesAvailable() const
{
    if (!m_clip || !m_vi || !avs_has_audio(m_vi) || m_vi->nchannels <= 0) return 0;
    const int64_t remainingSamples = m_vi->num_audio_samples - m_currentSample;
    if (remainingSamples <= 0) return QIODevice::bytesAvailable();
    const int64_t bytesPerFrame = static_cast<int64_t>(m_vi->nchannels) * sizeof(int16_t);
    const int64_t remainingBytes = remainingSamples > std::numeric_limits<qint64>::max() / bytesPerFrame
        ? std::numeric_limits<qint64>::max()
        : remainingSamples * bytesPerFrame;
    return remainingBytes + QIODevice::bytesAvailable();
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
        maximumLength <= 0 || m_vi->nchannels <= 0) {
        return 0;
    }

    const int64_t bytesPerFrame = static_cast<int64_t>(m_vi->nchannels) * sizeof(int16_t);
    const int64_t requestedSamples = maximumLength / bytesPerFrame;
    const int64_t remainingSamples = m_vi->num_audio_samples - m_currentSample;
    const int64_t samplesToRead = std::min(requestedSamples, remainingSamples);
    if (samplesToRead <= 0) return 0;

    if (!readAvsAsInt16(m_clip, m_vi, m_currentSample, samplesToRead, data)) return 0;
    m_currentSample += samplesToRead;
    return samplesToRead * bytesPerFrame;
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
        mAudioSink = new QAudioSink(defaultDevice, format);
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
