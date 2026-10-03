// One effects backend for live and offline audio. The live graph consumes
// source-rate packed S16, then converts only its final output to the sink format.
#include "VDQtAudioFilterSystem.h"

#include <QUuid>
#include <QStringList>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

namespace {

QString number(double value) {
    return QString::number(value, 'f', 8);
}

QStringList atempoChain(double factor) {
    // FFmpeg's atempo node accepts only 0.5..2.0. Factor the requested rate into
    // a legal chain so extreme but supported values behave predictably.
    QStringList filters;
    while (factor < 0.5) {
        filters.append(QStringLiteral("atempo=0.5"));
        factor /= 0.5;
    }
    while (factor > 2.0) {
        filters.append(QStringLiteral("atempo=2.0"));
        factor /= 2.0;
    }
    filters.append(QString("atempo=%1").arg(number(factor)));
    return filters;
}

} // namespace

bool VDQtValidateAudioFilters(const QList<VDAudioFilterInstance>& chain, QString *errorMessage) {
    const auto fail = [&](const QString& message) {
        if (errorMessage) *errorMessage = message;
        return false;
    };
    if (errorMessage) errorMessage->clear();
    if (chain.size() > 256) return fail(QStringLiteral("Too many audio filters."));
    double combinedTempo = 1.0;
    for (const auto& filter : chain) {
        if (filter.type < VDAudioFilterType::Gain || filter.type >= VDAudioFilterType::Count)
            return fail(QStringLiteral("Unknown audio filter type."));
        for (auto parameter = filter.params.cbegin(); parameter != filter.params.cend(); ++parameter) {
            const double value = parameter.value();
            // Unknown serialized/legacy fields are retained but never used in
            // DSP arithmetic. In particular SetLong may store a 64-bit value;
            // reject nonfinite data, not a harmless large configuration field.
            double minimum = std::numeric_limits<double>::lowest();
            double maximum = std::numeric_limits<double>::max();
            const auto& key = parameter.key();
            // Deep attenuation is a valid mute used by existing scripts/tests;
            // do not mistake it for an unsafe positive-gain exponent.
            if (key == "decibels") { minimum = -1000; maximum = 96; }
            else if (key == "cutoffHz") { minimum = 1; maximum = 768000; }
            else if (key == "sampleRate") { minimum = 1000; maximum = 768000; }
            else if (key == "semitones") { minimum = -48; maximum = 48; }
            else if (key == "factor") { minimum = 0.03125; maximum = 32; }
            else if (key == "mix") { minimum = 0; maximum = 1; }
            else if (key == "delayMs" || key == "depthMs") { minimum = 0; maximum = 1000; }
            else if (key == "rateHz") { minimum = 0.01; maximum = 20; }
            else if (key == "left" || key == "right" || key == "crossfeed") { minimum = -4; maximum = 4; }
            if (!std::isfinite(value) || value < minimum || value > maximum
                || (key == "sampleRate" && std::trunc(value) != value))
                return fail(QStringLiteral("Invalid audio parameter '%1' in '%2'.").arg(key, filter.name));
        }
        if (filter.enabled && filter.type == VDAudioFilterType::TimeStretch) {
            combinedTempo *= filter.params.value("factor", 1.0);
            // Bound intermediate expansion, not just the final chain: opposite
            // enormous stretches can still exhaust memory before cancelling.
            if (combinedTempo < 0.03125 || combinedTempo > 32)
                return fail(QStringLiteral("Combined audio time stretch exceeds the supported range."));
        }
    }
    return true;
}

// Libavfilter emits complete packed frames; this adapter exposes a byte stream
// without discarding short input fragments or output tails. No decoder work is
// moved back into the sink callback: the upstream devices remain decode-ahead.
struct VDQtAudioFilterDevice::GraphProcessor {
    AVFilterGraph *graph = nullptr;
    AVFilterContext *source = nullptr;
    AVFilterContext *sink = nullptr;
    QByteArray pending;
    QByteArray inputTail;
    qsizetype pendingOffset = 0;
    qint64 nextPts = 0;
    int sampleRate = 0;
    int channels = 0;
    QAudioFormat outputFormat;
    AVSampleFormat outputSampleFormat = AV_SAMPLE_FMT_NONE;
    QString error;
    mutable QMutex errorMutex; // Callback writes; UI reads without racing QString.
    bool sourceFlushed = false;
    bool sinkFinished = false;

    ~GraphProcessor() {
        avfilter_graph_free(&graph);
    }

    qint64 pendingBytes() const {
        return std::max<qint64>(0, pending.size() - pendingOffset);
    }

    bool fail(const QString& message) {
        const QMutexLocker lock(&errorMutex);
        if (error.isEmpty()) error = message;
        sinkFinished = true;
        return false;
    }

    QString failure() const {
        const QMutexLocker lock(&errorMutex);
        return error;
    }

    bool fail(const char *operation, int code) {
        char detail[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(code, detail, sizeof(detail));
        return fail(QStringLiteral("Audio %1 failed: %2").arg(QLatin1String(operation),
                                                             QString::fromUtf8(detail)));
    }

    bool configure(const QList<VDAudioFilterInstance>& chain,
                   int requestedSampleRate,
                   int requestedChannels, const QAudioFormat& requestedOutput) {
        avfilter_graph_free(&graph);
        source = nullptr;
        sink = nullptr;
        pending.clear();
        inputTail.clear();
        error.clear();
        pendingOffset = 0;
        nextPts = 0;
        sourceFlushed = false;
        sinkFinished = false;
        sampleRate = requestedSampleRate;
        channels = requestedChannels;
        outputFormat = requestedOutput;
        switch (outputFormat.sampleFormat()) {
        case QAudioFormat::UInt8: outputSampleFormat = AV_SAMPLE_FMT_U8; break;
        case QAudioFormat::Int16: outputSampleFormat = AV_SAMPLE_FMT_S16; break;
        case QAudioFormat::Int32: outputSampleFormat = AV_SAMPLE_FMT_S32; break;
        case QAudioFormat::Float: outputSampleFormat = AV_SAMPLE_FMT_FLT; break;
        default: return fail(QStringLiteral("Unsupported audio device sample format."));
        }

        // Effects precede device conversion, even if the sound card prefers
        // another rate, channel count or floating-point sample format.
        VDQtAudioFilterSystem graphSystem;
        graphSystem.replaceActiveChain(chain);
        QString description = graphSystem.ffmpegFilterGraph(sampleRate, &error);
        if (!error.isEmpty()) return false;
        if (description.isEmpty()) description = QStringLiteral("anull");

        AVChannelLayout layout = {};
        av_channel_layout_default(&layout, channels);
        char layoutName[128] = {};
        if (av_channel_layout_describe(
                &layout, layoutName, sizeof(layoutName)) < 0) {
            av_channel_layout_uninit(&layout);
            return fail(QStringLiteral("Could not describe the source audio layout."));
        }
        const QString inputLayoutName = QString::fromUtf8(layoutName);
        av_channel_layout_uninit(&layout);
        av_channel_layout_default(&layout, outputFormat.channelCount());
        if (av_channel_layout_describe(&layout, layoutName, sizeof(layoutName)) < 0) {
            av_channel_layout_uninit(&layout);
            return fail(QStringLiteral("Could not describe the device audio layout."));
        }
        description += QString(
            ",aresample=%1,aformat=sample_fmts=%2:sample_rates=%1:channel_layouts=%3")
            .arg(outputFormat.sampleRate())
            .arg(QLatin1String(av_get_sample_fmt_name(outputSampleFormat)), QString::fromUtf8(layoutName));

        graph = avfilter_graph_alloc();
        if (!graph) {
            av_channel_layout_uninit(&layout);
            return fail(QStringLiteral("Could not allocate the audio graph."));
        }
        // These are small streaming blocks. Auto-sizing a graph to every CPU
        // creates unnecessary session threads and increases callback latency.
        graph->nb_threads = 1;
        const AVFilter *bufferFilter = avfilter_get_by_name("abuffer");
        const AVFilter *sinkFilter = avfilter_get_by_name("abuffersink");
        const QByteArray sourceArguments = QString(
            "time_base=1/%1:sample_rate=%1:sample_fmt=s16:channel_layout=%2")
            .arg(sampleRate)
            .arg(inputLayoutName)
            .toUtf8();
        av_channel_layout_uninit(&layout);
        if (!bufferFilter || !sinkFilter
            || avfilter_graph_create_filter(
                   &source, bufferFilter, "in", sourceArguments.constData(),
                   nullptr, graph) < 0
            || avfilter_graph_create_filter(
                   &sink, sinkFilter, "out", nullptr, nullptr, graph) < 0) {
            avfilter_graph_free(&graph);
            source = nullptr;
            sink = nullptr;
            return fail(QStringLiteral("Could not create the audio graph endpoints."));
        }

        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            avfilter_inout_free(&outputs);
            avfilter_inout_free(&inputs);
            avfilter_graph_free(&graph);
            source = nullptr;
            sink = nullptr;
            return fail(QStringLiteral("Could not allocate audio graph connections."));
        }
        outputs->name = av_strdup("in");
        outputs->filter_ctx = source;
        outputs->pad_idx = 0;
        outputs->next = nullptr;
        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink;
        inputs->pad_idx = 0;
        inputs->next = nullptr;
        const QByteArray utf8Description = description.toUtf8();
        const int parseResult = avfilter_graph_parse_ptr(
            graph, utf8Description.constData(), &inputs, &outputs, nullptr);
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        const int configResult = parseResult < 0 ? parseResult : avfilter_graph_config(graph, nullptr);
        if (configResult < 0) {
            avfilter_graph_free(&graph);
            source = nullptr;
            sink = nullptr;
            return fail("graph configuration", configResult);
        }
        return true;
    }

    void drainSink() {
        // libavfilter may emit zero, one, or several frames per input block.
        // Convert that push behavior into pending bytes consumed by readData().
        if (!sink || sinkFinished) return;
        AVFrame *frame = av_frame_alloc();
        if (!frame) { fail(QStringLiteral("Could not allocate an audio output frame.")); return; }
        for (;;) {
            av_frame_unref(frame);
            const int result = av_buffersink_get_frame(sink, frame);
            if (result == AVERROR(EAGAIN)) break;
            if (result == AVERROR_EOF) {
                sinkFinished = true;
                break;
            }
            if (result < 0) {
                fail("filtering", result);
                break;
            }
            const int outputChannels = frame->ch_layout.nb_channels;
            const qint64 byteCount = static_cast<qint64>(frame->nb_samples)
                * outputFormat.bytesPerFrame();
            if (frame->format == outputSampleFormat && frame->data[0]
                && outputChannels == outputFormat.channelCount()
                && byteCount > 0 && byteCount <= 32 * 1024 * 1024
                && pendingBytes() <= 32 * 1024 * 1024 - byteCount) {
                pending.append(
                    reinterpret_cast<const char *>(frame->data[0]),
                    static_cast<int>(byteCount));
            } else { fail(QStringLiteral("Invalid or excessive audio graph output.")); break; }
        }
        av_frame_free(&frame);
    }

    bool feed(QIODevice *input, qint64 preferredBytes) {
        if (!source || !input || sourceFlushed || !error.isEmpty()) return false;
        const qint64 frameBytes = channels * sizeof(qint16);
        // Read complete interleaved sample frames and cap temporary allocation;
        // a rate-changing graph can otherwise amplify a very large pull request.
        qint64 requestBytes = std::max<qint64>(
            preferredBytes, frameBytes * 4096);
        requestBytes -= requestBytes % frameBytes;
        requestBytes = std::min<qint64>(requestBytes, 1024 * 1024);
        QByteArray block = std::move(inputTail);
        const qsizetype previousBytes = block.size();
        block.resize(previousBytes + requestBytes);
        const qint64 bytesRead = input->read(block.data() + previousBytes, requestBytes);
        block.resize(previousBytes + std::max<qint64>(0, bytesRead));
        if (bytesRead < 0) return fail(QStringLiteral("Audio input failed: %1").arg(input->errorString()));
        if (bytesRead == 0) {
            inputTail = std::move(block);
            if (input->atEnd()) {
                if (!inputTail.isEmpty()) return fail(QStringLiteral("Audio input ended with an incomplete sample frame."));
                const int flushResult = av_buffersrc_add_frame_flags(
                    source, nullptr, 0);
                sourceFlushed = true;
                if (flushResult < 0) return fail("graph flush", flushResult);
                drainSink();
            }
            return false;
        }
        const qint64 completeBytes = block.size() - block.size() % frameBytes;
        inputTail = block.mid(completeBytes);
        if (completeBytes == 0) return true; // Fragment retained; ask for more.

        AVFrame *frame = av_frame_alloc();
        if (!frame) return fail(QStringLiteral("Could not allocate an audio input frame."));
        frame->format = AV_SAMPLE_FMT_S16;
        frame->sample_rate = sampleRate;
        av_channel_layout_default(&frame->ch_layout, channels);
        frame->nb_samples = static_cast<int>(completeBytes / frameBytes);
        frame->pts = nextPts;
        if (nextPts > std::numeric_limits<qint64>::max() - frame->nb_samples) {
            av_frame_free(&frame);
            return fail(QStringLiteral("Audio sample timestamp overflow."));
        }
        nextPts += frame->nb_samples;
        bool accepted = false;
        if (av_frame_get_buffer(frame, 0) >= 0) {
            std::memcpy(frame->data[0], block.constData(),
                        static_cast<size_t>(completeBytes));
            accepted = av_buffersrc_add_frame_flags(
                source, frame, AV_BUFFERSRC_FLAG_KEEP_REF) >= 0;
        }
        av_frame_free(&frame);
        if (accepted) drainSink();
        else fail(QStringLiteral("Could not submit the audio input frame."));
        return accepted;
    }

    qint64 read(QIODevice *input, char *data, qint64 maximumLength) {
        if (!data || maximumLength <= 0 || !graph) return 0;
        if (!error.isEmpty()) return -1;
        drainSink();
        for (int attempt = 0;
             pendingBytes() == 0 && !sinkFinished && attempt < 64;
             ++attempt) {
            if (!feed(input, maximumLength)) {
                drainSink();
                if (!sourceFlushed) break;
            }
        }
        const qint64 copied = std::min(maximumLength, pendingBytes());
        if (!error.isEmpty()) return -1;
        if (copied <= 0) return 0;
        std::memcpy(data, pending.constData() + pendingOffset,
                    static_cast<size_t>(copied));
        pendingOffset += copied;
        if (pendingOffset >= pending.size()) {
            pending.clear();
            pendingOffset = 0;
        } else if (pendingOffset > 256 * 1024) {
            pending.remove(0, pendingOffset);
            pendingOffset = 0;
        }
        return copied;
    }
};

// ---------------------------------------------------------------------------
// Live pull-through device and session chain catalog
// ---------------------------------------------------------------------------

VDQtAudioFilterDevice::VDQtAudioFilterDevice(
    QIODevice *source,
    int sampleRate,
    int channels,
    QObject *parent)
    : VDQtAudioFilterDevice(source, sampleRate, channels, [=] {
        QAudioFormat format;
        format.setSampleRate(sampleRate);
        format.setChannelCount(channels);
        format.setSampleFormat(QAudioFormat::Int16);
        return format;
      }(), parent) {}

VDQtAudioFilterDevice::VDQtAudioFilterDevice(
    QIODevice *source, int sampleRate, int channels,
    const QAudioFormat& outputFormat, QObject *parent)
    : QIODevice(parent)
    , mSource(source)
    , mSampleRate(sampleRate)
    , mChannels(channels)
    , mOutputFormat(outputFormat) {
    if (mSource) {
        QObject::connect(mSource, &QIODevice::readyRead, this,
                         [this]() { Q_EMIT readyRead(); });
    }
    open(QIODevice::ReadOnly);
    setFilterChain({});
}

VDQtAudioFilterDevice::~VDQtAudioFilterDevice() = default;

bool VDQtAudioFilterDevice::setFilterChain(
    const QList<VDAudioFilterInstance>& chain) {
    const auto snapshot = chain; // resetProcessor may pass our own mChain.
    QIODevice::close(); // Discard Qt's read-ahead bytes on seek/configuration.
    open(QIODevice::ReadOnly);
    mChain = snapshot;
    mGraphProcessor.reset();
    mConfigurationError.clear();
    if (!mSource || mSampleRate < 1000 || mSampleRate > 768000
        || mChannels < 1 || mChannels > 64 || !mOutputFormat.isValid()
        || mOutputFormat.sampleRate() < 1000 || mOutputFormat.sampleRate() > 768000
        || mOutputFormat.channelCount() > 64) {
        mConfigurationError = QStringLiteral("Invalid audio source or output format.");
        return false;
    }
    if (!VDQtValidateAudioFilters(chain, &mConfigurationError)) return false;
    const bool needsGraph = std::any_of(
        chain.cbegin(), chain.cend(), [](const VDAudioFilterInstance& filter) {
            return filter.enabled;
        }) || mOutputFormat.sampleRate() != mSampleRate
            || mOutputFormat.channelCount() != mChannels
            || mOutputFormat.sampleFormat() != QAudioFormat::Int16;
    if (needsGraph) {
        auto processor = std::make_unique<GraphProcessor>();
        if (!processor->configure(chain, mSampleRate, mChannels, mOutputFormat)) {
            mConfigurationError = processor->error;
            return false;
        }
        mGraphProcessor = std::move(processor);
    }
    return true;
}

bool VDQtAudioFilterDevice::resetProcessor() {
    return setFilterChain(mChain);
}

QString VDQtAudioFilterDevice::error() const {
    return mGraphProcessor ? mGraphProcessor->failure() : mConfigurationError;
}

bool VDQtAudioFilterDevice::atEnd() const {
    if (!error().isEmpty() || !mSource) return true;
    // Upstream EOF does not mean Qt's already buffered output has been heard.
    if (QIODevice::bytesAvailable() > 0) return false;
    if (mGraphProcessor) {
        return mGraphProcessor->sinkFinished
            && mGraphProcessor->pendingBytes() == 0;
    }
    return mSource->atEnd();
}

qint64 VDQtAudioFilterDevice::bytesAvailable() const {
    const qint64 buffered = QIODevice::bytesAvailable();
    if (!error().isEmpty()) return buffered;
    if (!mGraphProcessor) return buffered + (mSource ? mSource->bytesAvailable() : 0);
    // Upstream and downstream bytes have different units after conversion.
    // Advertise a minimal readable hint, not a fictitious unfiltered byte count.
    return buffered + mGraphProcessor->pendingBytes()
        + (mSource && (mSource->bytesAvailable() > 0 || mSource->atEnd())
           && !mGraphProcessor->sinkFinished ? mOutputFormat.bytesPerFrame() : 0);
}

qint64 VDQtAudioFilterDevice::readData(char *data, qint64 maximumLength) {
    if (!mSource || !data || maximumLength <= 0) return 0;
    if (!error().isEmpty()) { setErrorString(error()); return -1; }
    if (mGraphProcessor) {
        const auto read = mGraphProcessor->read(mSource, data, maximumLength);
        if (read < 0) setErrorString(error());
        return read;
    }
    return mSource->read(data, maximumLength);
}

VDQtAudioFilterSystem& VDQtAudioFilterSystem::instance() {
    static VDQtAudioFilterSystem system;
    return system;
}

QList<VDQtAudioFilterSystem::FilterInfo>
VDQtAudioFilterSystem::availableFilters() const {
    return {
        {VDAudioFilterType::Gain, QStringLiteral("gain"),
         QStringLiteral("Adjust audio level in decibels.")},
        {VDAudioFilterType::LowPass, QStringLiteral("low-pass"),
         QStringLiteral("Attenuate frequencies above a cutoff.")},
        {VDAudioFilterType::HighPass, QStringLiteral("high-pass"),
         QStringLiteral("Attenuate frequencies below a cutoff.")},
        {VDAudioFilterType::Resample, QStringLiteral("resample"),
         QStringLiteral("Convert the audio sample rate.")},
        {VDAudioFilterType::ChannelMix, QStringLiteral("stereo channel mix"),
         QStringLiteral("Adjust left/right gain and crossfeed.")},
        {VDAudioFilterType::PitchShift, QStringLiteral("pitch shift"),
         QStringLiteral("Change pitch by semitones while retaining sample rate.")},
        {VDAudioFilterType::TimeStretch, QStringLiteral("time stretch"),
         QStringLiteral("Change duration without changing pitch.")},
        {VDAudioFilterType::CenterCut, QStringLiteral("center cut"),
         QStringLiteral("Remove content common to left and right channels.")},
        {VDAudioFilterType::CenterMix, QStringLiteral("center mix"),
         QStringLiteral("Mix left and right into a centered signal.")},
        {VDAudioFilterType::Chorus, QStringLiteral("chorus"),
         QStringLiteral("Add a modulated delayed copy of the signal.")}
    };
}

void VDQtAudioFilterSystem::replaceActiveChain(
    const QList<VDAudioFilterInstance>& chain) {
    mActiveChain = chain;
}

void VDQtAudioFilterSystem::addFilter(VDAudioFilterType type) {
    mActiveChain.append(createFilter(type));
}

VDAudioFilterInstance VDQtAudioFilterSystem::createFilter(
    VDAudioFilterType type) const {
    VDAudioFilterInstance filter;
    filter.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    filter.type = type;
    filter.enabled = true;
    const auto catalog = availableFilters();
    const auto found = std::find_if(catalog.cbegin(), catalog.cend(),
        [type](const FilterInfo& info) { return info.type == type; });
    filter.name = found != catalog.cend() ? found->name : QStringLiteral("audio filter");
    switch (type) {
    case VDAudioFilterType::Gain: filter.params[QStringLiteral("decibels")] = 0.0; break;
    case VDAudioFilterType::LowPass: filter.params[QStringLiteral("cutoffHz")] = 3000.0; break;
    case VDAudioFilterType::HighPass: filter.params[QStringLiteral("cutoffHz")] = 120.0; break;
    case VDAudioFilterType::Resample: filter.params[QStringLiteral("sampleRate")] = 48000.0; break;
    case VDAudioFilterType::ChannelMix:
        filter.params[QStringLiteral("left")] = 1.0;
        filter.params[QStringLiteral("right")] = 1.0;
        filter.params[QStringLiteral("crossfeed")] = 0.0;
        break;
    case VDAudioFilterType::PitchShift: filter.params[QStringLiteral("semitones")] = 0.0; break;
    case VDAudioFilterType::TimeStretch: filter.params[QStringLiteral("factor")] = 1.0; break;
    case VDAudioFilterType::Chorus:
        filter.params[QStringLiteral("delayMs")] = 20.0;
        filter.params[QStringLiteral("depthMs")] = 5.0;
        filter.params[QStringLiteral("rateHz")] = 0.8;
        filter.params[QStringLiteral("mix")] = 0.35;
        break;
    default: break;
    }
    return filter;
}

void VDQtAudioFilterSystem::removeFilter(int index) {
    if (index >= 0 && index < mActiveChain.size()) mActiveChain.removeAt(index);
}

void VDQtAudioFilterSystem::moveFilter(int from, int to) {
    if (from >= 0 && from < mActiveChain.size()
        && to >= 0 && to < mActiveChain.size())
        mActiveChain.move(from, to);
}

void VDQtAudioFilterSystem::setEnabled(int index, bool enabled) {
    if (index >= 0 && index < mActiveChain.size())
        mActiveChain[index].enabled = enabled;
}

void VDQtAudioFilterSystem::updateParams(
    int index, const QMap<QString, double>& params) {
    if (index >= 0 && index < mActiveChain.size())
        mActiveChain[index].params = params;
}

void VDQtAudioFilterSystem::clear() {
    mActiveChain.clear();
}

bool VDQtAudioFilterSystem::hasEnabledFilters() const {
    return std::any_of(mActiveChain.cbegin(), mActiveChain.cend(),
        [](const VDAudioFilterInstance& filter) { return filter.enabled; });
}

// All live/offline effects share this translation and parameter validation.
QString VDQtAudioFilterSystem::ffmpegFilterGraph(int sourceSampleRate, QString *errorMessage) const {
    if (!VDQtValidateAudioFilters(mActiveChain, errorMessage)) return {};
    // This output is parsed both by libavfilter and ffmpeg(1); do not introduce
    // shell quoting or options that are valid in only one of those contexts.
    sourceSampleRate = std::clamp(sourceSampleRate, 1000, 768000);
    int currentSampleRate = sourceSampleRate;
    QStringList graph;
    for (const VDAudioFilterInstance& filter : mActiveChain) {
        if (!filter.enabled) continue;
        switch (filter.type) {
        case VDAudioFilterType::Gain:
            graph << QString("volume=%1dB").arg(number(
                filter.params.value(QStringLiteral("decibels"), 0.0)));
            break;
        case VDAudioFilterType::LowPass:
            graph << QString("lowpass=f=%1").arg(number(
                filter.params.value(QStringLiteral("cutoffHz"), 3000.0)));
            break;
        case VDAudioFilterType::HighPass:
            graph << QString("highpass=f=%1").arg(number(
                filter.params.value(QStringLiteral("cutoffHz"), 120.0)));
            break;
        case VDAudioFilterType::Resample: {
            currentSampleRate = std::clamp(
                static_cast<int>(std::llround(filter.params.value(
                    QStringLiteral("sampleRate"), 48000.0))),
                1000, 768000);
            graph << QString("aresample=%1").arg(currentSampleRate);
            break;
        }
        case VDAudioFilterType::ChannelMix: {
            const double left = filter.params.value(QStringLiteral("left"), 1.0);
            const double right = filter.params.value(QStringLiteral("right"), 1.0);
            const double cross = filter.params.value(QStringLiteral("crossfeed"), 0.0);
            graph << QString("pan=stereo|c0=%1*c0+%2*c1|c1=%3*c1+%2*c0")
                .arg(number(left), number(cross), number(right));
            break;
        }
        case VDAudioFilterType::PitchShift: {
            const double ratio = std::pow(2.0,
                filter.params.value(QStringLiteral("semitones"), 0.0) / 12.0);
            const int shiftedRate = std::clamp(
                static_cast<int>(std::llround(std::clamp(currentSampleRate * ratio,
                                                        1000.0, 768000.0))),
                1000, 768000);
            graph << QString("asetrate=%1").arg(shiftedRate);
            graph << QString("aresample=%1").arg(currentSampleRate);
            graph.append(atempoChain(1.0 / ratio));
            break;
        }
        case VDAudioFilterType::TimeStretch:
            graph.append(atempoChain(filter.params.value(
                QStringLiteral("factor"), 1.0)));
            break;
        case VDAudioFilterType::CenterCut:
            graph << QStringLiteral("pan=stereo|c0=0.5*c0-0.5*c1|c1=0.5*c1-0.5*c0");
            break;
        case VDAudioFilterType::CenterMix:
            graph << QStringLiteral("pan=stereo|c0=0.5*c0+0.5*c1|c1=0.5*c0+0.5*c1");
            break;
        case VDAudioFilterType::Chorus:
            graph << QString("chorus=0.7:0.9:%1:%2:%3:%4")
                .arg(number(filter.params.value(QStringLiteral("delayMs"), 20.0)),
                     number(filter.params.value(QStringLiteral("mix"), 0.35)),
                     number(filter.params.value(QStringLiteral("rateHz"), 0.8)),
                     number(filter.params.value(QStringLiteral("depthMs"), 5.0)));
            break;
        default: break;
        }
    }
    return graph.join(QLatin1Char(','));
}
