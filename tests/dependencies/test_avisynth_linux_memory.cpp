// Pure dependency-helper regression: no media, kernel pressure or plugins.
#include "core/memory_helpers.h"
#include <cstdio>
#include <sstream>

int main() {
    constexpr uint64_t MiB = 1024 * 1024;
    bool passed = true;
    auto check = [&](bool good, const char* message) {
        if (!good) { std::fprintf(stderr, "FAIL %s\n", message); passed = false; }
    };
    uint64_t bytes = 123;
    std::istringstream cached("MemTotal: 67108864 kB\nMemFree: 524288 kB\nMemAvailable: 20971520 kB\nCached: 20447232 kB\n");
    check(avs_memory::ReadLinuxAvailable(cached, bytes) && bytes == 20 * 1024 * MiB,
          "available RAM must include reclaimable cache, not just MemFree");
    check(avs_memory::Constrain(4096 * MiB, bytes, 1024 * MiB) == 4096 * MiB,
          "a disk-cache-heavy host must retain the normal cache allowance");
    for (const char* text : {"", "MemFree: 1024 kB\n", "MemAvailable: nope kB\n",
                            "MemAvailable: -1 kB\n", "MemAvailable: 1024 MB\n",
                            "MemAvailable: 18446744073709551615 kB\n",
                            "MemAvailable: 9007199254740992 kB\n"}) {
        std::istringstream input(text);
        bytes = 123;
        check(!avs_memory::ReadLinuxAvailable(input, bytes) && bytes == 123,
              "missing/malformed/overflowing readings must request fallback");
    }
    std::istringstream emptyRam("MemAvailable: 0 kB\n");
    check(avs_memory::ReadLinuxAvailable(emptyRam, bytes) && bytes == 0, "zero available RAM is valid");
    for (uint64_t limit : {uint64_t(0), 32 * MiB, 1024 * MiB})
        check(avs_memory::Constrain(4096 * MiB, limit, 1024 * MiB) == 64 * MiB,
              "reserve subtraction must not wrap on low-memory hosts");
    check(avs_memory::Constrain(4096 * MiB, 2048 * MiB, 1024 * MiB) == 1024 * MiB,
          "honor the OS reserve");
    check(avs_memory::Constrain(128 * MiB, 8192 * MiB, 1024 * MiB) == 128 * MiB,
          "honor explicit smaller requests");
    std::printf("%s Linux memory accounting and low-memory limits\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
