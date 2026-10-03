#include "VirtualDub/VDQtPlaybackClock.h"
#include "VirtualDub/VDQtTimingMath.h"
#include <iostream>
#include <limits>

namespace {
bool near(double actual, double expected) {
    if (std::abs(actual - expected) < 1e-8) return true;
    std::cerr << "clock: expected " << expected << ", got " << actual << '\n';
    return false;
}
}

int main() {
    int frames = 0;
    int64_t ticks = 0;
    if (!VDQtCheckedRoundedNonnegative(24.4L, &frames) || frames != 24
        || VDQtCheckedRoundedNonnegative(1e100L, &ticks)
        || VDQtCheckedRoundedNonnegative(std::ldexp(1.0L, 63), &ticks)
        || !VDQtCheckedRoundedNonnegative(1234567890123.0L, &ticks) || ticks != 1234567890123
        || VDQtDecimatedFrameCount(std::numeric_limits<int>::max(), 1000000) != 2148
        || VDQtSourceFrameAtOffset(10, 20, 1e100L) != 20
        || VDQtSourceFrameAtOffset(10, 20, 4.5L) != 14
        || VDQtScaledProgress(std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 850) != 850) {
        std::cerr << "Checked output timing/count math failed\n";
        return 1;
    }
    const auto ordinary = VDQtAdvanceFrame(5, 0.1, 0.04, 3);
    const auto decimated = VDQtAdvanceFrame(10, 3.0, 0.001, 1000000);
    const auto delayed = VDQtAdvanceFrame(0, 1e100, 0.001, 1);
    if (ordinary.framesElapsed != 2 || ordinary.targetFrame != 11
        || decimated.targetFrame != std::numeric_limits<int>::max()
        || delayed.targetFrame != std::numeric_limits<int>::max()
        || VDQtAdvanceFrame(std::numeric_limits<int>::max(), 1, 1, 1).targetFrame
            != std::numeric_limits<int>::max()
        || VDQtAdvanceFrame(5, 0.01, 0.04, 1).targetFrame != 5
        || VDQtSamplePosition(1.25, 48000) != 60000
        || VDQtSamplePosition(1e100, 48000, 96000) != 96000
        || VDQtSamplePosition(1e100, 48000) != std::numeric_limits<int64_t>::max()
        || VDQtSamplePosition(-10, 48000) != 0
        || VDQtRoundedNonnegative(1e100L, 32767) != 32767) {
        std::cerr << "Bounded playback/sample timing math failed\n";
        return 1;
    }
    VDQtPlaybackClock clock;
    if (!near(clock.elapsed(0), 0) || !near(clock.elapsed(0.3), 0.3)) return 1;
    clock.reset();
    if (!near(clock.elapsed(0, 0), 0) || !near(clock.elapsed(0.45, 0.4), 0.4)
        || !near(clock.elapsed(0.5, -1), 0.4) || !near(clock.elapsed(0.6, -1), 0.5)
        || !near(clock.elapsed(0.8, 0.41), 0.7)) return 1;
    // A sink claiming to be active can freeze; recovery must not repin video.
    clock.reset();
    if (!near(clock.elapsed(0, 0), 0) || !near(clock.elapsed(0.1, 0.1), 0.1)
        || !near(clock.elapsed(0.3, 0.1), 0.1) || !near(clock.elapsed(0.4, 0.1), 0.1)
        || !near(clock.elapsed(0.5, 0.1), 0.2) || !near(clock.elapsed(0.6, 0.11), 0.3)) return 1;
    clock.reset();
    if (!near(clock.elapsed(0, 0), 0) || !near(clock.elapsed(0.2, 0.2), 0.2)
        || !near(clock.elapsed(0.3, 0.19), 0.2)
        || !near(clock.elapsed(0.4, std::numeric_limits<double>::quiet_NaN()), 0.2)
        || !near(clock.elapsed(0.5), 0.3) || !near(clock.elapsed(0.49), 0.3)) return 1;
    clock.reset();
    return near(clock.elapsed(0, 0), 0) && near(clock.elapsed(0.1, 0.1), 0.1) ? 0 : 1;
}
