#ifndef VDQTTIMINGMATH_H
#define VDQTTIMINGMATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Output timing/counts must fail, not saturate and publish a different export.
// A power-of-two exclusive ceiling avoids INT64_MAX rounding up as double on
// platforms where long double has no additional mantissa precision.
template<class Integer>
inline bool VDQtCheckedRoundedNonnegative(long double value, Integer *result) {
    static_assert(std::numeric_limits<Integer>::is_integer && std::numeric_limits<Integer>::is_signed);
    const long double rounded = std::round(value);
    if (!result || !std::isfinite(rounded) || rounded < 0
        || rounded >= std::ldexp(1.0L, std::numeric_limits<Integer>::digits)) return false;
    *result = static_cast<Integer>(rounded);
    return true;
}

inline int VDQtDecimatedFrameCount(int frames, int step) {
    return frames <= 0 ? 0 : 1 + (frames - 1) / std::max(1, step);
}

inline int VDQtSourceFrameAtOffset(int first, int last, long double offset) {
    first = std::max(0, first);
    last = std::max(first, last);
    const int span = last - first;
    if (std::isnan(offset) || offset <= 0) return first;
    if (offset >= span) return last;
    return first + static_cast<int>(std::floor(offset + 1e-9L));
}

inline int VDQtScaledProgress(int current, int total, int scale) {
    total = std::max(1, total);
    return static_cast<int>(int64_t(std::clamp(current, 0, total)) * std::max(0, scale) / total);
}

// The CFR samples inside [0, seconds) are 0, 1/fps, 2/fps, ... . Use the
// cumulative boundary, not independent per-source-frame rounding (which drifts
// and incorrectly forces short VFR frames to occupy at least one CFR tick).
// The fixed tolerance only absorbs reciprocal-clock roundoff at exact ticks;
// a relative tolerance would swallow whole frames on long streams.
inline bool VDQtCfrBoundaryFrames(long double seconds, double fps, int64_t *result) {
    if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(fps) || fps <= 0)
        return false;
    const long double count = std::max(0.0L, std::ceil(seconds * fps - 1e-7L));
    return VDQtCheckedRoundedNonnegative(count, result);
}

// Time/rate products can exceed integer domains even when both inputs are
// finite. Bound in floating-point BEFORE rounding/casting. Positions past a
// known source end clamp to that end; unknown lengths use the integer ceiling.
template<class Integer>
inline Integer VDQtRoundedNonnegative(long double value, Integer maximum) {
    if (std::isnan(value) || value <= 0 || maximum <= 0) return 0;
    if (value >= static_cast<long double>(maximum)) return maximum;
    const long double rounded = std::round(value);
    if (rounded >= static_cast<long double>(maximum)) return maximum;
    return static_cast<Integer>(rounded);
}

inline int64_t VDQtSamplePosition(double seconds, int sampleRate,
                                  int64_t maximum = std::numeric_limits<int64_t>::max()) {
    if (sampleRate <= 0) return 0;
    return VDQtRoundedNonnegative(static_cast<long double>(seconds) * sampleRate, maximum);
}

struct VDQtFrameAdvance {
    int framesElapsed = 0;
    int targetFrame = 0;
};

// The public decoder/UI frame domain is int. A delayed timer or large decimation
// factor must reach its ceiling, never wrap negative and request earlier frames.
inline VDQtFrameAdvance VDQtAdvanceFrame(int current, double elapsed, double duration, int step) {
    current = std::max(0, current);
    if (!std::isfinite(elapsed) || !std::isfinite(duration) || duration <= 0 || elapsed < duration)
        return {0, current};
    const long double intervals = std::floor(static_cast<long double>(elapsed) / duration);
    const int count = intervals >= std::numeric_limits<int>::max()
        ? std::numeric_limits<int>::max() : static_cast<int>(intervals);
    const int64_t target = int64_t(current) + int64_t(count) * std::max(1, step);
    return {count, static_cast<int>(std::min<int64_t>(target, std::numeric_limits<int>::max()))};
}

#endif
