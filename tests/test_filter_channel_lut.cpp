// Exact integer-channel reference checks for Gamma/GammaCorrect/Curves/Levels.
// LUT optimization must preserve every supported channel value, including low
// 16-bit bits and straight alpha; speed alone is not an acceptance criterion.
#include "VirtualDub/VDQtFilterSystem.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRgba64>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
double reference(VDFilterType type, const QMap<QString, double>& parameters, double value) {
    if (type == VDFilterType::GammaCorrect) {
        if (parameters.value("toLinear", 1) > 0.5)
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    }
    const double gamma = parameters.value("gamma", 1);
    if (type == VDFilterType::Gamma) return std::pow(value, 1.0 / gamma);
    const bool levels = type == VDFilterType::Levels;
    const double black = parameters.value(levels ? "inputBlack" : "black", 0) / 255;
    const double white = parameters.value(levels ? "inputWhite" : "white", 255) / 255;
    const double mapped = std::pow(std::clamp((value - black) / (white - black), 0.0, 1.0), 1.0 / gamma);
    if (!levels) return mapped;
    const double outputBlack = parameters.value("outputBlack", 0) / 255;
    const double outputWhite = parameters.value("outputWhite", 255) / 255;
    return outputBlack + (outputWhite - outputBlack) * mapped;
}
QImage fullRange(QImage::Format format) {
    const bool high = format == QImage::Format_RGBA64;
    QImage image(high ? QSize(512, 128) : QSize(256, 1), format);
    for (int y = 0; y < image.height(); ++y) {
        auto *bytes = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const int value = y * image.width() + x;
            if (high) {
                reinterpret_cast<QRgba64 *>(bytes)[x] = QRgba64::fromRgba64(
                    value, (value * 257 + 13) & 65535, (65535 - value), (value * 131 + 31) & 65535);
            } else {
                const int bpp = format == QImage::Format_RGB888 ? 3 : 4;
                bytes[x * bpp] = value;
                bytes[x * bpp + 1] = (value * 17 + 13) & 255;
                bytes[x * bpp + 2] = 255 - value;
                if (bpp == 4) bytes[x * bpp + 3] = (value * 131 + 31) & 255;
            }
        }
    }
    return image;
}
bool exactChannels(VDFilterType type, QMap<QString, double> parameters) {
    VDQtFilterSystem system;
    system.addFilter(type);
    system.updateFilterParams(0, parameters);
    bool passed = true;
    for (auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888, QImage::Format_RGBA64}) {
        const QImage input = fullRange(format);
        const QImage original = input.copy();
        const QImage output = system.processFrame(input);
        if (!check(!output.isNull() && output.size() == input.size(), "channel transform returns valid dimensions")) return false;
        if (!check(input == original, "channel transform leaves caller-owned pixels unchanged")) return false;
        const int maximum = format == QImage::Format_RGBA64 ? 65535 : 255;
        const int bpp = format == QImage::Format_RGB888 ? 3 : 4;
        for (int y = 0; y < input.height(); ++y) {
            const auto *sourceBytes = input.constScanLine(y), *destinationBytes = output.constScanLine(y);
            for (int x = 0; x < input.width(); ++x) {
                const QRgba64 before = format == QImage::Format_RGBA64
                    ? reinterpret_cast<const QRgba64 *>(sourceBytes)[x] : QRgba64();
                const QRgba64 after = format == QImage::Format_RGBA64
                    ? reinterpret_cast<const QRgba64 *>(destinationBytes)[x] : QRgba64();
                for (int channel = 0; channel < 3; ++channel) {
                    const int value = maximum == 65535
                        ? channel == 0 ? before.red() : channel == 1 ? before.green() : before.blue()
                        : sourceBytes[x * bpp + channel];
                    const int actual = maximum == 65535
                        ? channel == 0 ? after.red() : channel == 1 ? after.green() : after.blue()
                        : destinationBytes[x * bpp + channel];
                    const int expected = std::clamp(std::llround(reference(type, parameters,
                        value / static_cast<double>(maximum)) * maximum), 0LL, static_cast<long long>(maximum));
                    if (actual != expected) {
                        std::cerr << "FAIL: filter " << static_cast<int>(type) << ", depth " << maximum
                                  << ", value " << value << ": got " << actual << ", expected " << expected << '\n';
                        return false;
                    }
                }
                if ((maximum == 65535 && before.alpha() != after.alpha())
                    || (format == QImage::Format_RGBA8888 && sourceBytes[x * bpp + 3] != destinationBytes[x * bpp + 3]))
                    return check(false, "independent channel transforms preserve straight alpha exactly");
            }
        }
        const auto chain = system.getActiveChain();
        system.replaceActiveChainTransient(chain);
        passed &= check(system.processFrame(input) == output, "transient restart preserves exact transformed pixels");
    }
    return passed;
}
bool benchmark() {
    const int savedThreads = QThreadPool::globalInstance()->maxThreadCount();
    QThreadPool::globalInstance()->setMaxThreadCount(8);
    for (auto format : {QImage::Format_RGB888, QImage::Format_RGBA64}) {
        QImage input(1920, 1080, format);
        input.fill(QColor(85, 140, 200, 180));
        VDQtFilterSystem system;
        for (auto type : {VDFilterType::Gamma, VDFilterType::GammaCorrect, VDFilterType::Curves, VDFilterType::Levels}) {
            system.addFilter(type);
            auto parameters = system.getActiveChain().last().params;
            if (type != VDFilterType::GammaCorrect) parameters["gamma"] = 1.8;
            system.updateFilterParams(system.getActiveChain().size() - 1, parameters);
        }
        if (system.processFrame(input).isNull()) return false;
        QElapsedTimer timer;
        timer.start();
        for (int frame = 0; frame < 12; ++frame)
            if (system.processFrame(input).isNull()) return false;
        std::cout << "1080p Gamma + GammaCorrect + Curves + Levels, "
                  << (format == QImage::Format_RGBA64 ? "16" : "8") << "-bit: "
                  << timer.nsecsElapsed() / 1000000.0 / 12 << " ms/frame\n";
    }
    QThreadPool::globalInstance()->setMaxThreadCount(savedThreads);
    return true;
}
bool boundedReuseAndNumericEdges() {
    VDQtFilterSystem system;
    system.addFilter(VDFilterType::Gamma);
    QImage input = fullRange(QImage::Format_RGBA64);
    system.updateFilterParams(0, {{"gamma", 2.2}});
    const QImage expected = system.processFrame(input);
    if (!check(!expected.isNull() && system.cacheStatistics().channelLutEntries == 1,
               "channel processing builds one exact high-depth table")) return false;
    auto chain = system.getActiveChain();
    system.replaceActiveChainTransient(chain);
    bool passed = check(system.cacheStatistics().channelLutEntries == 1 && system.processFrame(input) == expected,
                        "preview restarts retain immutable exact tables");
    chain.first().params["gamma"] = 1.7;
    system.replaceActiveChainTransient(chain);
    VDQtFilterSystem fresh;
    fresh.replaceActiveChainTransient(chain);
    passed &= check(system.processFrame(input) == fresh.processFrame(input), "changed scalar parameters cannot reuse stale tables");
    for (int value = 0; value < 24; ++value) {
        chain.first().params["gamma"] = 0.5 + value * 0.1;
        system.replaceActiveChainTransient(chain);
        passed &= check(!system.processFrame(input).isNull() && system.cacheStatistics().channelLutEntries <= 8
            && system.cacheStatistics().channelLutBytes <= 1024 * 1024,
            "exact lookup tables obey eight-entry and one-MiB retained budgets");
    }
    system.clearFilters();
    passed &= check(system.cacheStatistics().channelLutEntries == 0 && system.cacheStatistics().channelLutBytes == 0,
                    "Clear releases scalar lookup tables");
    for (auto type : {VDFilterType::Curves, VDFilterType::Levels}) {
        VDQtFilterSystem tiny;
        tiny.addFilter(type);
        auto parameters = tiny.getActiveChain().first().params;
        parameters[type == VDFilterType::Curves ? "white" : "inputWhite"] = std::numeric_limits<double>::denorm_min();
        tiny.updateFilterParams(0, parameters);
        passed &= check(tiny.processFrame(input).isNull() && !tiny.lastError().isEmpty(),
                        "a white/black span that underflows during normalization fails before division and rounding");
    }
    return passed;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    bool passed = true;
    for (double gamma : {0.05, 1.0, 2.2, 20.0})
        passed &= exactChannels(VDFilterType::Gamma, {{"gamma", gamma}});
    for (int direction : {0, 1})
        passed &= exactChannels(VDFilterType::GammaCorrect, {{"toLinear", direction}});
    passed &= exactChannels(VDFilterType::Curves, {{"black", 13.25}, {"white", 239.5}, {"gamma", 2.2}});
    passed &= exactChannels(VDFilterType::Levels, {{"inputBlack", 13.25}, {"inputWhite", 239.5},
        {"outputBlack", 9.5}, {"outputWhite", 246.75}, {"gamma", 2.2}});
    passed &= boundedReuseAndNumericEdges();
    if (application.arguments().contains("--benchmark")) passed &= benchmark();
    return passed ? 0 : 1;
}
