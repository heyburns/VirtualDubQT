// Reproduces the shared-QImage detach race under real row parallelism and checks
// complete pixel output against an independent, deliberately scalar reference.
// Keep this test distinct from codec troubleshooting and speed benchmarks.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtFilterSystem.h"

#include <QCoreApplication>
#include <QFile>
#include <QPainter>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <iostream>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
bool require(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

QImage scalarReference(const QImage& source, const QImage& previous, VDFilterType type) {
    QImage result = source.copy();
    if (result.depth() > 32) {
        // These repairs preserve existing high-depth behavior; implementing the
        // currently missing high-depth algorithms is a separate audit batch.
        if (type == VDFilterType::TemporalSmoother && !previous.isNull()) {
            QPainter painter(&result);
            painter.setOpacity(0.25); // strength=4/8, existing high-depth blend.
            painter.drawImage(0, 0, previous);
        }
        return result;
    }
    const int channels = source.format() == QImage::Format_RGB888 ? 3 : 4;
    const int width = source.width(), height = source.height();
    uchar* bits = result.bits();
    const qsizetype stride = result.bytesPerLine();
    for (int y = 0; y < height; ++y) {
        uchar* destination = bits + y * stride;
        const uchar* row = source.constScanLine(y);
        const uchar* above = source.constScanLine(std::max(0, y - 1));
        const uchar* below = source.constScanLine(std::min(height - 1, y + 1));
        for (int x = 0; x < width; ++x) {
            const int base = x * channels;
            if (type == VDFilterType::TemporalSmoother && !previous.isNull()) {
                const uchar* prior = previous.constScanLine(y);
                int maximum = 0;
                for (int c = 0; c < 3; ++c)
                    maximum = std::max(maximum, std::abs(row[base + c] - prior[base + c]));
                if (maximum <= 12)
                    for (int c = 0; c < 3; ++c)
                        destination[base + c] = static_cast<uchar>(
                            std::lround((row[base + c] + prior[base + c]) * 0.5));
            } else if (type == VDFilterType::InverseTelecine && y > 0 && y < height - 1) {
                for (int c = 0; c < 3; ++c) {
                    const int prediction = (above[base + c] + below[base + c] + 1) / 2;
                    if (std::abs(row[base + c] - prediction) > 12)
                        destination[base + c] = static_cast<uchar>(prediction);
                }
            } else if (type == VDFilterType::Television) {
                int totals[3] = {0, 0, 0};
                for (int offset = -2; offset <= 2; ++offset) {
                    const int sample = std::clamp(x + offset, 0, width - 1) * channels;
                    for (int c = 0; c < 3; ++c) totals[c] += row[sample + c];
                }
                const int luma = (77 * row[base] + 150 * row[base + 1] + 29 * row[base + 2]) / 256;
                const int averageLuma = (77 * totals[0] + 150 * totals[1] + 29 * totals[2]) / 1280;
                for (int c = 0; c < 3; ++c)
                    destination[base + c] = static_cast<uchar>(std::clamp(
                        int((luma + totals[c] / 5 - averageLuma) * ((y & 1) ? 0.92 : 1.0)), 0, 255));
            } else if (type == VDFilterType::WarpSharp) {
                const int left = std::max(0, x - 1) * channels;
                const int right = std::min(width - 1, x + 1) * channels;
                for (int c = 0; c < 3; ++c) {
                    const int average = (row[left + c] + row[right + c]
                        + above[base + c] + below[base + c] + 2) / 4;
                    destination[base + c] = static_cast<uchar>(std::clamp(
                        row[base + c] + (row[base + c] - average) * 8 / 16, 0, 255));
                }
            }
        }
    }
    return result;
}

bool runCase(VDFilterType type, QSize size, QImage::Format format, int frames,
             bool doubled = false) {
    VDQtFilterSystem filters;
    if (doubled) {
        filters.addFilter(VDFilterType::BobDoubler);
        auto bob = filters.getActiveChain().first().params;
        bob["mode"] = 4; // Duplicate, without changing spatial pixels.
        filters.updateFilterParams(0, bob);
    }
    filters.addFilter(type);
    // Even a full-strength opacity curve creates another shared image owner in
    // the real chain. This reproduces the race's export/preview aliasing case.
    const int filterIndex = doubled ? 1 : 0;
    QMap<QString, double> params = filters.getActiveChain().at(filterIndex).params;
    params["_sylia.opacity.count"] = 1;
    params["_sylia.opacity.0.y"] = 1;
    filters.updateFilterParams(filterIndex, params);
    QImage previous;
    QList<QImage> retained, snapshots;
    for (int frame = 0; frame < frames; ++frame) {
        const QImage input = VDQtTestFixtures::patternedImage(size.width(), size.height(), format, frame);
        if (!require(!input.isNull(), "allocate test image")) return false;
        const QImage inputSnapshot = input.copy();
        const QImage expected = scalarReference(input, previous, type);
        QList<QImage> outputs;
        if (!require(filters.processFrameSequence(input, outputs, {frame, frame / 24.0, 24.0}),
                     "processing succeeds")
            || !require(outputs.size() == (doubled ? 2 : 1), "expected phase count")
            || !require(std::all_of(outputs.cbegin(), outputs.cend(), [&](const QImage& output) {
                    return !output.isNull() && output == expected;
                }), "parallel phase pixels match scalar reference")
            || !require(input == inputSnapshot, "caller-owned source stays unchanged")) return false;
        for (int index = 0; index < retained.size(); ++index)
            if (!require(retained.at(index) == snapshots.at(index), "old outputs stay unchanged")) return false;
        previous = expected;
        retained.append(outputs.first());
        snapshots.append(outputs.first().copy());
    }
    // A discontinuity must not blend with the previous frame. Seeks are common
    // during scrubbing and should not leave stale temporal history in the result.
    if (type == VDFilterType::TemporalSmoother) {
        const QImage input = VDQtTestFixtures::patternedImage(size.width(), size.height(), format, 50);
        if (!require(filters.processFrame(input, {50, 50 / 24.0, 24.0}) == input,
                     "seek resets temporal blending")) return false;
    }
    std::cout << size.width() << 'x' << size.height() << " format=" << int(format)
              << " frames=" << frames << " passed\n";
    return true;
}

bool allocationFailure(VDFilterType type) {
#ifdef __linux__
    VDQtFilterSystem filters;
    filters.addFilter(type);
    const QImage previous = VDQtTestFixtures::patternedImage(3841, 2160, QImage::Format_RGB888);
    if (type == VDFilterType::TemporalSmoother
        && filters.processFrame(previous, {0, 0.0, 24.0}).isNull()) return false;
    const QImage input = VDQtTestFixtures::patternedImage(3841, 2160, QImage::Format_RGB888, 1);
    const QImage snapshot = input.copy();
    if (previous.isNull() || input.isNull() || snapshot.isNull()) return false;
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) return false;
    bool valid = false;
    const auto pages = statm.readAll().simplified().split(' ').first().toULongLong(&valid);
    const long pageSize = sysconf(_SC_PAGESIZE);
    struct rlimit saved;
    if (!valid || pageSize <= 0 || getrlimit(RLIMIT_AS, &saved) != 0) return false;
    struct rlimit limited = saved;
    limited.rlim_cur = std::min(saved.rlim_cur, rlim_t(pages * pageSize + 1024 * 1024));
    if (setrlimit(RLIMIT_AS, &limited) != 0) return false;
    // This affects only our disposable test process. The large shared source
    // cannot be detached within the remaining address-space budget. ASan's huge
    // shadow mappings are incompatible with RLIMIT_AS, so CMake selects this
    // genuine allocator-failure case only for the non-sanitized Linux build.
    QList<QImage> outputs;
    const bool processed = filters.processFrameSequence(input, outputs, {1, 1 / 24.0, 24.0});
    const bool restored = setrlimit(RLIMIT_AS, &saved) == 0;
    if (!require(restored, "restore test process memory limit")
        || !require(!processed && outputs.isEmpty(), "allocation failure returns empty failed sequence")
        || !require(input == snapshot, "failed detachment preserves source")) return false;
    // Normal processing must recover after the allocation budget is restored.
    return require(filters.processFrameSequence(input, outputs, {2, 2 / 24.0, 24.0})
                       && outputs.size() == 1 && !outputs.first().isNull(),
                   "pipeline recovers after allocation failure");
#else
    (void)type;
    return false;
#endif
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2 && argc != 3) return 2;
    VDFilterType type;
    const QString name = QString::fromLocal8Bit(argv[1]);
    if (name == "temporal") type = VDFilterType::TemporalSmoother;
    else if (name == "ivtc") type = VDFilterType::InverseTelecine;
    else if (name == "television") type = VDFilterType::Television;
    else if (name == "warp") type = VDFilterType::WarpSharp;
    else return 2;
    QThreadPool::globalInstance()->setMaxThreadCount(8);
    if (argc == 3) {
        if (QString::fromLocal8Bit(argv[2]) != "--allocation-failure") return 2;
        return allocationFailure(type) ? 0 : 1;
    }
    if (QThread::idealThreadCount() <= 1)
        std::cout << "Single-CPU host: pixel checks run but parallel race coverage is unavailable\n";
    for (const auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888}) {
        if (!runCase(type, QSize(97, 65), format, 4)
            || !runCase(type, QSize(1921, 1080), format, 8)
            || !runCase(type, QSize(3841, 2160), format, 3)) return 1;
    }
    if (!runCase(type, QSize(1921, 1080), QImage::Format_RGBA64, 2)
        || !runCase(type, QSize(3841, 2160), QImage::Format_RGBA64, 2)
        || !runCase(type, QSize(1921, 1080), QImage::Format_RGB888, 4, true)
        || !runCase(type, QSize(1921, 1080), QImage::Format_RGBA8888, 4, true)) return 1;

    // Never report a successful phase sequence containing null images. A
    // rejected stage in a multi-phase chain must clear the sequence.
    VDQtFilterSystem invalid;
    invalid.addFilter(type);
    QList<QImage> outputs{QImage(1, 1, QImage::Format_RGB888)};
    if (!require(!invalid.processFrameSequence(QImage(), outputs) && outputs.isEmpty(),
                 "null input is a failed, empty sequence")) return 1;
    invalid.addFilter(VDFilterType::BobDoubler);
    auto params = invalid.getActiveChain().last().params;
    params["_sylia.clip.left"] = 10000;
    invalid.updateFilterParams(1, params);
    const QImage source = VDQtTestFixtures::patternedImage(97, 65, QImage::Format_RGB888);
    if (!require(!invalid.processFrameSequence(source, outputs) && outputs.isEmpty(),
                 "invalid stage yields no partial output sequence")) return 1;
    return 0;
}
