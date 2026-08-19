#include "VDQtAudioFilterSystem.h"

#include <QUuid>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

qint16 clippedSample(double value) {
    return static_cast<qint16>(std::clamp(
        std::lround(value),
        static_cast<long>(std::numeric_limits<qint16>::min()),
        static_cast<long>(std::numeric_limits<qint16>::max())));
}

QString number(double value) {
    return QString::number(value, 'f', 8);
}

QStringList atempoChain(double factor) {
    factor = std::clamp(factor, 0.03125, 32.0);
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

void VDQtAudioFilterProcessor::configure(
    const QList<VDAudioFilterInstance>& chain,
    int sampleRate,
    int channels) {
    mChain = chain;
    mSampleRate = std::max(1, sampleRate);
    mChannels = std::max(1, channels);
    mStates.resize(mChain.size());
    reset();
}

void VDQtAudioFilterProcessor::reset() {
    for (int index = 0; index < mStates.size(); ++index) {
        State& state = mStates[index];
        state.previousInput.fill(0.0, mChannels);
        state.previousOutput.fill(0.0, mChannels);
        state.delayPosition = 0;
        state.phase = 0.0;
        state.delay.clear();
        if (index < mChain.size()
            && mChain.at(index).type == VDAudioFilterType::Chorus) {
            const double delayMs = std::clamp(
                mChain.at(index).params.value(QStringLiteral("delayMs"), 20.0),
                1.0, 100.0);
            const double depthMs = std::clamp(
                mChain.at(index).params.value(QStringLiteral("depthMs"), 5.0),
                0.0, 50.0);
            const int delayFrames = std::max(
                2, static_cast<int>(std::ceil(
                    (delayMs + depthMs + 2.0) * mSampleRate / 1000.0)));
            state.delay.fill(0, delayFrames * mChannels);
        }
    }
}

void VDQtAudioFilterProcessor::processInt16(char *data, qint64 bytes) {
    if (!data || bytes <= 0 || mChannels <= 0) return;
    const qint64 frameBytes = static_cast<qint64>(mChannels) * sizeof(qint16);
    const qint64 frames = bytes / frameBytes;
    qint16 *samples = reinterpret_cast<qint16 *>(data);

    for (int filterIndex = 0; filterIndex < mChain.size(); ++filterIndex) {
        const VDAudioFilterInstance& filter = mChain.at(filterIndex);
        if (!filter.enabled) continue;
        State& state = mStates[filterIndex];

        if (filter.type == VDAudioFilterType::Gain) {
            const double gain = std::pow(
                10.0, filter.params.value(QStringLiteral("decibels"), 0.0) / 20.0);
            for (qint64 index = 0; index < frames * mChannels; ++index)
                samples[index] = clippedSample(samples[index] * gain);
        } else if (filter.type == VDAudioFilterType::LowPass) {
            const double cutoff = std::clamp(
                filter.params.value(QStringLiteral("cutoffHz"), 3000.0),
                10.0, mSampleRate * 0.49);
            const double alpha = 1.0 - std::exp(
                -2.0 * std::acos(-1.0) * cutoff / mSampleRate);
            for (qint64 frame = 0; frame < frames; ++frame) {
                for (int channel = 0; channel < mChannels; ++channel) {
                    const qint64 offset = frame * mChannels + channel;
                    state.previousOutput[channel] += alpha
                        * (samples[offset] - state.previousOutput[channel]);
                    samples[offset] = clippedSample(state.previousOutput[channel]);
                }
            }
        } else if (filter.type == VDAudioFilterType::HighPass) {
            const double cutoff = std::clamp(
                filter.params.value(QStringLiteral("cutoffHz"), 120.0),
                10.0, mSampleRate * 0.49);
            const double rc = 1.0 / (2.0 * std::acos(-1.0) * cutoff);
            const double dt = 1.0 / mSampleRate;
            const double alpha = rc / (rc + dt);
            for (qint64 frame = 0; frame < frames; ++frame) {
                for (int channel = 0; channel < mChannels; ++channel) {
                    const qint64 offset = frame * mChannels + channel;
                    const double input = samples[offset];
                    const double output = alpha
                        * (state.previousOutput[channel] + input
                           - state.previousInput[channel]);
                    state.previousInput[channel] = input;
                    state.previousOutput[channel] = output;
                    samples[offset] = clippedSample(output);
                }
            }
        } else if (filter.type == VDAudioFilterType::ChannelMix
                   && mChannels >= 2) {
            const double left = filter.params.value(QStringLiteral("left"), 1.0);
            const double right = filter.params.value(QStringLiteral("right"), 1.0);
            const double cross = filter.params.value(QStringLiteral("crossfeed"), 0.0);
            for (qint64 frame = 0; frame < frames; ++frame) {
                qint16 *sample = samples + frame * mChannels;
                const double originalLeft = sample[0];
                const double originalRight = sample[1];
                sample[0] = clippedSample(originalLeft * left + originalRight * cross);
                sample[1] = clippedSample(originalRight * right + originalLeft * cross);
            }
        } else if (filter.type == VDAudioFilterType::CenterCut
                   && mChannels >= 2) {
            for (qint64 frame = 0; frame < frames; ++frame) {
                qint16 *sample = samples + frame * mChannels;
                const double difference = (sample[0] - sample[1]) * 0.5;
                sample[0] = clippedSample(difference);
                sample[1] = clippedSample(-difference);
            }
        } else if (filter.type == VDAudioFilterType::CenterMix
                   && mChannels >= 2) {
            for (qint64 frame = 0; frame < frames; ++frame) {
                qint16 *sample = samples + frame * mChannels;
                const qint16 mixed = clippedSample((sample[0] + sample[1]) * 0.5);
                sample[0] = mixed;
                sample[1] = mixed;
            }
        } else if (filter.type == VDAudioFilterType::Chorus
                   && !state.delay.isEmpty()) {
            const double delayMs = filter.params.value(QStringLiteral("delayMs"), 20.0);
            const double depthMs = filter.params.value(QStringLiteral("depthMs"), 5.0);
            const double rateHz = std::clamp(
                filter.params.value(QStringLiteral("rateHz"), 0.8), 0.05, 10.0);
            const double mix = std::clamp(
                filter.params.value(QStringLiteral("mix"), 0.35), 0.0, 1.0);
            const qint64 delayFrames = state.delay.size() / mChannels;
            for (qint64 frame = 0; frame < frames; ++frame) {
                const double modulation = (std::sin(state.phase) + 1.0) * 0.5;
                const qint64 offsetFrames = std::clamp<qint64>(
                    static_cast<qint64>(std::llround(
                        (delayMs + depthMs * modulation) * mSampleRate / 1000.0)),
                    1, delayFrames - 1);
                const qint64 readFrame = (state.delayPosition - offsetFrames
                    + delayFrames) % delayFrames;
                for (int channel = 0; channel < mChannels; ++channel) {
                    const qint64 sampleOffset = frame * mChannels + channel;
                    const qint64 writeOffset = state.delayPosition * mChannels + channel;
                    const qint64 readOffset = readFrame * mChannels + channel;
                    const qint16 dry = samples[sampleOffset];
                    samples[sampleOffset] = clippedSample(
                        dry * (1.0 - mix) + state.delay[readOffset] * mix);
                    state.delay[writeOffset] = dry;
                }
                state.delayPosition = (state.delayPosition + 1) % delayFrames;
                state.phase += 2.0 * std::acos(-1.0) * rateHz / mSampleRate;
                if (state.phase > 2.0 * std::acos(-1.0))
                    state.phase -= 2.0 * std::acos(-1.0);
            }
        }
    }
}

VDQtAudioFilterDevice::VDQtAudioFilterDevice(
    QIODevice *source,
    int sampleRate,
    int channels,
    QObject *parent)
    : QIODevice(parent)
    , mSource(source)
    , mSampleRate(sampleRate)
    , mChannels(channels) {
    if (mSource) {
        QObject::connect(mSource, &QIODevice::readyRead, this,
                         [this]() { Q_EMIT readyRead(); });
    }
    open(QIODevice::ReadOnly);
}

void VDQtAudioFilterDevice::setFilterChain(
    const QList<VDAudioFilterInstance>& chain) {
    mProcessor.configure(chain, mSampleRate, mChannels);
}

void VDQtAudioFilterDevice::resetProcessor() {
    mProcessor.reset();
}

bool VDQtAudioFilterDevice::atEnd() const {
    return !mSource || mSource->atEnd();
}

qint64 VDQtAudioFilterDevice::bytesAvailable() const {
    return (mSource ? mSource->bytesAvailable() : 0)
        + QIODevice::bytesAvailable();
}

qint64 VDQtAudioFilterDevice::readData(char *data, qint64 maximumLength) {
    if (!mSource || !data || maximumLength <= 0) return 0;
    const qint64 frameBytes = std::max<qint64>(1, mChannels * sizeof(qint16));
    maximumLength -= maximumLength % frameBytes;
    const qint64 read = mSource->read(data, maximumLength);
    if (read > 0) mProcessor.processInt16(data, read);
    return read;
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

QString VDQtAudioFilterSystem::ffmpegFilterGraph(int sourceSampleRate) const {
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
                static_cast<int>(std::llround(currentSampleRate * ratio)),
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
