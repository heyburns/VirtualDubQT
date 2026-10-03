#include "VirtualDub/VDQtPlaybackClock.h"
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
