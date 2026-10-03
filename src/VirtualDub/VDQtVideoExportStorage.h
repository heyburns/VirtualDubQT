#ifndef VDQT_VIDEO_EXPORT_STORAGE_H
#define VDQT_VIDEO_EXPORT_STORAGE_H

#include "VDQtAudioFilterSystem.h"
#include "VDQtTimingMath.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Estimate the PCM files that can coexist with a two-pass video intermediate.
// Raw extraction preserves source precision, including double PCM. Edited
// exports retain source segments while joining one raw WAV; effects then render
// that soundtrack once to double PCM. Eight bytes/channel covers each file.
// This is an estimate, not a disk reservation or exact encoder promise.
inline bool VDQtEstimatePcmTemporaryStorage(double durationSeconds, int sourceRate,
        int sourceChannels, const QList<VDAudioFilterInstance>& filters,
        qint64 segments, qint64 *requiredBytes, QString *error = nullptr) {
    const auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (!requiredBytes || !std::isfinite(durationSeconds) || durationSeconds < 0
        || sourceRate <= 0 || sourceChannels <= 0 || sourceChannels > 65535 || segments <= 0)
        return fail(QStringLiteral("The temporary audio storage parameters are invalid."));
    if (!VDQtValidateAudioFilters(filters, error)) return false;
    // Independently rounded edit boundaries can add at most one sample/range.
    const long double sourceSeconds = durationSeconds + static_cast<long double>(segments) / sourceRate;
    long double filteredSeconds = sourceSeconds;
    int configuredRate = std::clamp(sourceRate, 1000, 768000);
    int actualRate = sourceRate;
    int filteredChannels = sourceChannels;
    bool hasEffects = false;
    for (const auto& filter : filters) {
        if (!filter.enabled) continue;
        hasEffects = true;
        switch (filter.type) {
        case VDAudioFilterType::Resample:
            configuredRate = actualRate = static_cast<int>(filter.params.value("sampleRate", 48000));
            break;
        case VDAudioFilterType::ChannelMix:
        case VDAudioFilterType::CenterCut:
        case VDAudioFilterType::CenterMix:
            filteredChannels = 2;
            break;
        case VDAudioFilterType::TimeStretch:
            filteredSeconds /= filter.params.value("factor", 1.0);
            break;
        case VDAudioFilterType::PitchShift: {
            const double ratio = std::pow(2.0, filter.params.value("semitones", 0.0) / 12.0);
            const int shiftedRate = static_cast<int>(std::llround(
                std::clamp(configuredRate * ratio, 1000.0, 768000.0)));
            // Mirrors asetrate, aresample, atempo in the shared graph. At the
            // rate limits, pitch shifting can also expand duration substantially.
            filteredSeconds *= static_cast<long double>(actualRate) / shiftedRate * ratio;
            actualRate = configuredRate;
            break;
        }
        case VDAudioFilterType::Chorus:
            // The composed chain emits one tail, not a tail for every cut.
            filteredSeconds += 2.L;
            break;
        default:
            break;
        }
        if (!std::isfinite(filteredSeconds))
            return fail(QStringLiteral("The filtered temporary audio duration is too large."));
    }
    qint64 sourceBytes = 0, filteredBytes = 0;
    if (!VDQtCheckedRoundedNonnegative(std::ceil(sourceSeconds * sourceRate * sourceChannels * 8), &sourceBytes)
        || !VDQtCheckedRoundedNonnegative(std::ceil(
            (filteredSeconds * actualRate + (hasEffects ? 1024.L : 0)) * filteredChannels * 8), &filteredBytes))
        return fail(QStringLiteral("The temporary audio storage estimate is too large."));
    constexpr qint64 maximum = std::numeric_limits<qint64>::max();
    const qint64 copies = segments > 1 ? 2 : 1;
    if (sourceBytes > maximum / copies)
        return fail(QStringLiteral("The retained temporary audio files are too large."));
    qint64 total = sourceBytes * copies;
    if (hasEffects) {
        if (filteredBytes > maximum - total)
            return fail(QStringLiteral("The temporary audio extraction is too large."));
        total += filteredBytes;
    }
    // WAV/RF64 headers, a concat manifest and per-file rounding/headroom. The
    // video/storage estimator adds the shared fixed safety reserve separately.
    if (segments > (maximum / 4096 - 1) / (hasEffects ? 2 : 1))
        return fail(QStringLiteral("There are too many temporary audio files."));
    const qint64 headers = (segments * (hasEffects ? 2 : 1) + 1) * 4096;
    if (headers > maximum - total)
        return fail(QStringLiteral("The temporary audio storage estimate is too large."));
    *requiredBytes = total + headers;
    if (error) error->clear();
    return true;
}

#endif
