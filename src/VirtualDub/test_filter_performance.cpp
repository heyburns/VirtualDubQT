#include "VDQtFilterSystem.h"

#include <QGuiApplication>
#include <QElapsedTimer>
#include <QImage>

#include <algorithm>
#include <iostream>
#include <set>

namespace {

bool require(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

QImage makeGradient(int width, int height) {
    QImage image(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            row[x * 3] = static_cast<uchar>((x * 255) / std::max(1, width - 1));
            row[x * 3 + 1] = static_cast<uchar>((y * 255) / std::max(1, height - 1));
            row[x * 3 + 2] = static_cast<uchar>((x + y) & 255);
        }
    }
    return image;
}

} // namespace

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);

    // Every advertised built-in must be constructible and produce a valid
    // image with its defaults. This catches catalog entries that are UI-only
    // placeholders or switch cases that were omitted as the catalog grows.
    VDQtFilterSystem catalogSystem;
    const auto catalog = catalogSystem.getAvailableFilters();
    std::set<int> builtInTypes;
    const QImage catalogSource = makeGradient(96, 64);
    for (const auto& info : catalog) {
        if (!info.pluginId.isEmpty()) continue;
        builtInTypes.insert(static_cast<int>(info.type));
        VDQtFilterSystem single;
        single.addFilter(info.type);
        if (!require(single.getActiveChain().size() == 1,
                     "catalog filter is constructible"))
            return 1;
        VDFilterFrameContext context;
        context.frameNumber = 0;
        context.timestampSeconds = 1.25;
        context.frameRate = 24.0;
        QList<QImage> frames;
        if (!require(single.processFrameSequence(catalogSource, frames, context)
                         && !frames.isEmpty()
                         && std::all_of(frames.cbegin(), frames.cend(),
                            [](const QImage& frame) { return !frame.isNull(); }),
                     "catalog filter has a working processing path"))
            return 1;
    }
    if (!require(static_cast<int>(builtInTypes.size())
                     == static_cast<int>(VDFilterType::Count) - 1,
                 "every built-in filter type is advertised exactly once"))
        return 1;

    VDQtFilterSystem temporal;
    temporal.addFilter(VDFilterType::MotionBlur);
    VDFilterFrameContext firstContext{0, 0.0, 25.0};
    VDFilterFrameContext secondContext{1, 0.04, 25.0};
    const QImage firstTemporal = temporal.processFrame(catalogSource, firstContext);
    QImage solid(catalogSource.size(), QImage::Format_RGB888);
    solid.fill(Qt::white);
    const QImage secondTemporal = temporal.processFrame(solid, secondContext);
    if (!require(firstTemporal != secondTemporal && secondTemporal != solid,
                 "temporal filters retain sequential frame state"))
        return 1;

    const QImage source = makeGradient(1920, 1080);

    VDQtFilterSystem filters;
    filters.addFilter(VDFilterType::SixAxis);
    QMap<QString, double> sixAxis =
        filters.getActiveChain().first().params;
    sixAxis[QStringLiteral("saturation")] = 1.15;
    sixAxis[QStringLiteral("red")] = 1.10;
    sixAxis[QStringLiteral("blue")] = 0.90;
    filters.updateFilterParams(0, sixAxis);
    filters.addFilter(VDFilterType::Blur);
    QMap<QString, double> blur = filters.getActiveChain().last().params;
    blur[QStringLiteral("width")] = 3.0;
    blur[QStringLiteral("power")] = 2.0;
    filters.updateFilterParams(1, blur);

    QImage output = filters.processFrame(source);
    if (!require(!output.isNull() && output.size() == source.size(),
                 "combined heavy filter output"))
        return 1;

    constexpr int measuredFrames = 3;
    QElapsedTimer timer;
    timer.start();
    for (int frame = 0; frame < measuredFrames; ++frame) {
        output = filters.processFrame(source);
        if (output.isNull()) return 1;
    }
    const double millisecondsPerFrame =
        static_cast<double>(timer.nsecsElapsed()) / 1000000.0 / measuredFrames;
    std::cout << "1080p six-axis + radius-3 two-pass blur: "
              << millisecondsPerFrame << " ms/frame\n";

    // This is intentionally a missed-deadline guard, not a microbenchmark.
    // It leaves substantial room for slower CI hosts while preventing a
    // regression to the multi-second scalar preview path.
    if (!require(millisecondsPerFrame < 750.0,
                 "heavy 1080p filter preview stays below 750 ms/frame"))
        return 1;

    VDQtFilterSystem expanded;
    expanded.addFilter(VDFilterType::HSVAdjust);
    QMap<QString, double> hsv = expanded.getActiveChain().last().params;
    hsv[QStringLiteral("hueDegrees")] = 18.0;
    hsv[QStringLiteral("saturation")] = 1.2;
    expanded.updateFilterParams(0, hsv);
    expanded.addFilter(VDFilterType::Smoother);
    QMap<QString, double> smoother = expanded.getActiveChain().last().params;
    smoother[QStringLiteral("amount")] = 0.65;
    expanded.updateFilterParams(1, smoother);
    expanded.addFilter(VDFilterType::Pixelate);
    QMap<QString, double> pixelate = expanded.getActiveChain().last().params;
    pixelate[QStringLiteral("blockSize")] = 4.0;
    expanded.updateFilterParams(2, pixelate);
    output = expanded.processFrame(source);
    if (!require(!output.isNull() && output.size() == source.size(),
                 "expanded heavy filter output"))
        return 1;
    timer.restart();
    for (int frame = 0; frame < measuredFrames; ++frame) {
        output = expanded.processFrame(source);
        if (output.isNull()) return 1;
    }
    const double expandedMillisecondsPerFrame =
        static_cast<double>(timer.nsecsElapsed()) / 1000000.0 / measuredFrames;
    std::cout << "1080p HSV + smoother + pixelate: "
              << expandedMillisecondsPerFrame << " ms/frame\n";
    if (!require(expandedMillisecondsPerFrame < 750.0,
                 "expanded heavy 1080p preview stays below 750 ms/frame"))
        return 1;
    return 0;
}
