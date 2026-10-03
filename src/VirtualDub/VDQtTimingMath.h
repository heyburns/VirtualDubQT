#ifndef VDQTTIMINGMATH_H
#define VDQTTIMINGMATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

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
