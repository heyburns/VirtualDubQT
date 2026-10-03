// Deterministic cache-lifetime and asset-freshness checks plus an optional
// restart benchmark. No user files, hardware or private-member test tricks.
#include "VirtualDub/VDQtFilterSystem.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool lookupTables(bool benchmark) {
    VDQtFilterSystem system;
    system.addFilter(VDFilterType::SixAxis);
    auto params = system.getActiveChain().first().params;
    params["saturation"] = 1.15;
    params["red"] = 1.1;
    system.updateFilterParams(0, params);
    QImage input(128, 96, QImage::Format_RGB888);
    input.fill(QColor(120, 65, 200));
    const auto expected = system.processFrame(input);
    auto chain = system.getActiveChain();
    bool passed = check(!expected.isNull() && system.cacheStatistics().sixAxisEntries == 1,
                        "six-axis processing builds a bounded parameter-keyed lookup table");
    system.replaceActiveChainTransient(chain);
    passed &= check(system.cacheStatistics().sixAxisEntries == 1,
                    "preview restart retains the immutable lookup table before processing");
    passed &= check(system.processFrame(input) == expected, "warm restart output is unchanged");
    chain.first().params["saturation"] = 0.75;
    system.replaceActiveChainTransient(chain);
    VDQtFilterSystem fresh;
    fresh.replaceActiveChainTransient(chain);
    passed &= check(system.processFrame(input) == fresh.processFrame(input),
                    "changed parameter keys cannot reuse a stale lookup table");
    for (int value = 0; value < 24; ++value) {
        chain.first().params["saturation"] = 0.1 + value * 0.05;
        system.replaceActiveChainTransient(chain);
        passed &= check(!system.processFrame(input).isNull() && system.cacheStatistics().sixAxisEntries <= 8,
                        "parameter changes keep the eight-table cache budget");
    }
    if (benchmark) {
        system.replaceActiveChainTransient(chain);
        system.processFrame(input);
        QElapsedTimer timer;
        timer.start();
        for (int cycle = 0; cycle < 200; ++cycle) {
            system.replaceActiveChainTransient(chain);
            if (system.processFrame(input).isNull()) return false;
        }
        std::cout << "200 six-axis preview restarts: " << timer.nsecsElapsed() / 1000000.0 << " ms\n";
    }
    system.clearFilters();
    passed &= check(system.cacheStatistics().sixAxisEntries == 0, "Clear releases derived lookup tables");
    return passed;
}
bool assets() {
    QTemporaryDir directory;
    if (!directory.isValid()) return false;
    const QString path = directory.filePath("logo.png");
    QImage first(8, 8, QImage::Format_RGBA8888);
    first.fill(Qt::red);
    if (!first.save(path)) return false;
    VDQtFilterSystem system;
    system.addFilter(VDFilterType::Logo);
    system.updateFilterStringParams(0, {{"path", path}});
    QImage input(8, 8, QImage::Format_RGBA8888);
    input.fill(Qt::black);
    bool passed = check(system.processFrame(input).pixelColor(0, 0) == QColor(Qt::red), "first logo loads");
    // A changed size makes freshness deterministic without timestamp sleeps.
    QImage replacement(9, 9, QImage::Format_RGBA8888);
    replacement.fill(Qt::blue);
    if (!replacement.save(path)) return false;
    passed &= check(system.processFrame(input).pixelColor(0, 0) == QColor(Qt::blue),
                    "replacing a cached logo at the same path refreshes its pixels");
    if (!QFile::remove(path)) return false;
    passed &= check(system.processFrame(input).isNull() && !system.lastError().isEmpty(),
                    "deleting a cached required logo cannot display stale pixels");
    if (!replacement.save(path)) return false;
    passed &= check(!system.processFrame(input).isNull(), "restored assets recover without restarting");
    const QString sameSizePath = directory.filePath("same-size.bmp");
    if (!first.save(sameSizePath)) return false;
    auto sameSizeChain = system.getActiveChain();
    sameSizeChain.first().stringParams["path"] = sameSizePath;
    system.replaceActiveChainTransient(sameSizeChain);
    const qint64 savedSize = QFileInfo(sameSizePath).size();
    passed &= check(system.processFrame(input).pixelColor(0, 0) == QColor(Qt::red), "same-size fixture loads");
    first.fill(Qt::green);
    if (!first.save(sameSizePath)) return false;
    QFile timestamp(sameSizePath);
    if (!timestamp.open(QIODevice::ReadWrite)
        || !timestamp.setFileTime(QDateTime::currentDateTimeUtc().addSecs(2), QFileDevice::FileModificationTime)) return false;
    timestamp.close();
    passed &= check(QFileInfo(sameSizePath).size() == savedSize
        && system.processFrame(input).pixelColor(0, 0) == QColor(Qt::green),
        "a modified timestamp refreshes same-size replacement content");
    QImage large(1024, 1024, QImage::Format_RGBA8888);
    for (int index = 0; index < 24; ++index) {
        large.fill(QColor(index * 10, 25, 120));
        const QString next = directory.filePath(QString("asset-%1.png").arg(index));
        if (!large.save(next)) return false;
        auto chain = system.getActiveChain();
        chain.first().stringParams["path"] = next;
        system.replaceActiveChainTransient(chain);
        const auto output = system.processFrame(input);
        const auto cache = system.cacheStatistics();
        passed &= check(!output.isNull() && cache.assetEntries <= 64 && cache.assetBytes <= 64 * 1024 * 1024,
                        "retained assets remain inside entry and pixel-memory budgets");
    }
    for (int index = 0; index < 70; ++index) {
        const QString next = directory.filePath(QString("tiny-%1.png").arg(index));
        if (!first.save(next)) return false;
        auto chain = system.getActiveChain();
        chain.first().stringParams["path"] = next;
        system.replaceActiveChainTransient(chain);
        passed &= check(!system.processFrame(input).isNull()
            && system.cacheStatistics().assetEntries <= 64,
            "many small assets cannot bypass the entry budget");
    }
    system.clearFilters();
    passed &= check(system.cacheStatistics().assetEntries == 0, "Clear releases cached assets");
    return passed;
}
bool temporalReset() {
    VDQtFilterSystem system;
    system.addFilter(VDFilterType::MotionBlur);
    QImage first(8, 8, QImage::Format_RGBA8888), second(first.size(), first.format());
    first.fill(Qt::red);
    second.fill(Qt::blue);
    const VDFilterFrameContext before{0, 0, 25}, after{1, 0.04, 25};
    if (system.processFrame(first, before).isNull()) return false;
    if (!check(system.processFrame(second, after) != second, "sequential filtering retains history")) return false;
    system.replaceActiveChainTransient(system.getActiveChain());
    return check(system.processFrame(second, after) == second,
                 "reusing immutable caches must still reset temporal history");
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    bool passed = lookupTables(application.arguments().contains("--benchmark"));
    passed &= assets();
    passed &= temporalReset();
    return passed ? 0 : 1;
}
