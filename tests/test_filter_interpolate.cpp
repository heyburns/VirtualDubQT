// The current Interpolate amount control blends two incoming stage frames.
// Its history must not become the recursively blended output used by Motion
// Blur. This repair intentionally does not implement Windows rate conversion.
#include "VirtualDub/VDQtFilterSystem.h"

#include <QCoreApplication>
#include <QPainter>
#include <QRgba64>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
QImage source(QImage::Format format, int frame) {
    QImage image(17, 10, format);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            // Deliberately nonmonotonic colors and alpha reveal feedback over
            // four frames, while native low bits exercise 16-bit composition.
            const int r = (frame * frame * 193 + x * 23 + y * 41) & 255;
            const int g = (frame * 67 + x * 11 + y * 7) & 255;
            const int b = (frame * frame * 131 + x * 3 + y * 19) & 255;
            const int a = (frame * 53 + x * 47 + y * 13) & 255;
            if (format == QImage::Format_RGBA64) {
                reinterpret_cast<QRgba64*>(image.scanLine(y))[x] = QRgba64::fromRgba64(
                    (r * 257 + x * 11 + 31) & 65535,
                    (g * 257 + y * 17 + 53) & 65535,
                    (b * 257 + x * 7 + y * 3 + 71) & 65535,
                    (a * 257 + x * 3 + y * 11 + 13) & 65535);
            } else {
                const int bpp = format == QImage::Format_RGB888 ? 3 : 4;
                uchar *pixel = image.scanLine(y) + x * bpp;
                pixel[0] = static_cast<uchar>(r);
                pixel[1] = static_cast<uchar>(g);
                pixel[2] = static_cast<uchar>(b);
                if (bpp == 4) pixel[3] = static_cast<uchar>(a);
            }
        }
    }
    return image;
}
QImage adjacentReference(const QImage& current, const QImage& previous, double amount) {
    if (previous.isNull() || previous.size() != current.size()) return current;
    if (amount == 0.0 && current.format() == QImage::Format_RGBA64) return current;
    QImage output = current.copy();
    QImage source = previous;
    const bool highPrecision = current.format() == QImage::Format_RGBA64;
    if (highPrecision) {
        source = previous.copy();
        // Avoid Qt 6.4's broken straight RGBA64 fetch/conversion. QPainter
        // still supplies an independent source-over composition reference.
        for (QImage *image : {&output, &source}) {
            for (int y = 0; y < image->height(); ++y) {
                auto *row = reinterpret_cast<QRgba64*>(image->scanLine(y));
                for (int x = 0; x < image->width(); ++x) row[x] = row[x].premultiplied();
            }
            image->reinterpretAsFormat(QImage::Format_RGBA64_Premultiplied);
        }
    }
    QPainter painter(&output);
    painter.setOpacity(amount);
    painter.drawImage(0, 0, source);
    painter.end();
    if (highPrecision) {
        for (int y = 0; y < output.height(); ++y) {
            auto *row = reinterpret_cast<QRgba64*>(output.scanLine(y));
            for (int x = 0; x < output.width(); ++x) row[x] = row[x].unpremultiplied();
        }
        output.reinterpretAsFormat(QImage::Format_RGBA64);
    }
    return output;
}
void configure(VDQtFilterSystem& filters, int index, double amount) {
    auto parameters = filters.getActiveChain().at(index).params;
    parameters["amount"] = amount;
    filters.updateFilterParams(index, parameters);
}
bool adjacentInputs(QImage::Format format, double amount) {
    VDQtFilterSystem interpolation, recursive;
    interpolation.addFilter(VDFilterType::Interpolate);
    recursive.addFilter(VDFilterType::MotionBlur);
    configure(interpolation, 0, amount);
    configure(recursive, 0, amount);
    QImage previousInput, previousRecursive;
    for (int frame = 0; frame < 4; ++frame) {
        const QImage input = source(format, frame);
        const QImage snapshot = input.copy();
        const VDFilterFrameContext context{frame, frame / 25.0, 25};
        if (!check(interpolation.processFrame(input, context)
                       == adjacentReference(input, previousInput, amount),
                   "Interpolate blends only consecutive incoming images, including alpha and native low bits")
            || !check(input == snapshot, "interpolation leaves caller-owned input unchanged")) return false;
        previousRecursive = adjacentReference(input, previousRecursive, amount);
        if (!check(recursive.processFrame(input, context) == previousRecursive,
                   "Motion Blur keeps its intentionally recursive output history")) return false;
        previousInput = input;
    }
    const QImage sought = source(format, 6);
    if (!check(interpolation.processFrame(sought, {100, 4, 25}) == sought,
               "a seek does not blend with stale interpolation history")) return false;
    const QImage next = source(format, 7);
    const QImage resumed = interpolation.processFrame(next, {101, 4.04, 25});
    if (format == QImage::Format_RGBA64 && !resumed.isNull()) {
        for (int y = 0; y < next.height(); ++y) {
            const auto *input = reinterpret_cast<const QRgba64*>(next.constScanLine(y));
            const auto *output = reinterpret_cast<const QRgba64*>(resumed.constScanLine(y));
            for (int x = 0; x < next.width(); ++x) {
                if (!check(output[x].alpha() >= input[x].alpha(),
                           "16-bit source-over composition never reduces destination alpha")) return false;
            }
        }
    }
    if (!check(resumed == adjacentReference(next, sought, amount),
               "interpolation resumes with the new incoming image after a seek")) return false;
    interpolation.resetRuntimeState();
    if (!check(interpolation.processFrame(next, {101, 4.04, 25}) == next,
               "runtime reset clears adjacent-image history")) return false;
    interpolation.replaceActiveChainTransient(interpolation.getActiveChain());
    if (!check(interpolation.processFrame(next) == next,
               "preview-chain replacement clears adjacent-image history")) return false;
    return check(interpolation.processFrame(sought)
                     == adjacentReference(sought, next, amount),
                 "unknown frame contexts still advance through incoming images");
}
bool upstreamImages(QImage::Format format, double amount) {
    VDQtFilterSystem combined;
    combined.addFilter(VDFilterType::InvertColor);
    combined.addFilter(VDFilterType::Interpolate);
    configure(combined, 1, amount);
    QImage previous;
    for (int frame = 0; frame < 4; ++frame) {
        const QImage input = source(format, frame);
        const QImage snapshot = input.copy();
        QImage incoming = input.copy();
        incoming.invertPixels(QImage::InvertRgb);
        if (!check(combined.processFrame(input, {frame, frame / 25.0, 25})
                       == adjacentReference(incoming, previous, amount),
                   "interpolation history belongs to the incoming filter stage, not raw decoder images")
            || !check(input == snapshot, "upstream chain preserves caller-owned pixels")) return false;
        previous = incoming;
    }
    return true;
}
bool bobPhases(QImage::Format format, double amount) {
    VDQtFilterSystem combined, upstream;
    combined.addFilter(VDFilterType::BobDoubler);
    combined.addFilter(VDFilterType::Interpolate);
    upstream.addFilter(VDFilterType::BobDoubler);
    configure(combined, 1, amount);
    QImage previousPhase;
    for (int frame = 0; frame < 4; ++frame) {
        const QImage input = source(format, frame);
        const QImage snapshot = input.copy();
        const VDFilterFrameContext context{frame, frame / 25.0, 25};
        QList<QImage> incoming, outputs;
        if (!check(upstream.processFrameSequence(input, incoming, context)
                       && combined.processFrameSequence(input, outputs, context)
                       && incoming.size() == 2 && outputs.size() == 2,
                   "adjacent interpolation preserves Bob's existing count/rate sequence contract")) return false;
        for (int phase = 0; phase < incoming.size(); ++phase) {
            if (!check(outputs.at(phase) == adjacentReference(incoming.at(phase), previousPhase, amount),
                       "downstream interpolation advances through incoming Bob phases in order")) return false;
            previousPhase = incoming.at(phase);
        }
        if (!check(input == snapshot, "Bob/interpolation leave caller-owned pixels unchanged")) return false;
    }
    return check(combined.getTimingInfo().outputFramesPerInput == 2,
                 "repair does not change configured filter timing");
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    for (const auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888, QImage::Format_RGBA64}) {
        for (double amount : {0.0, 0.001, 0.125, 0.333, 0.5, 0.998, 1.0}) {
            if (!adjacentInputs(format, amount) || !upstreamImages(format, amount)
                || !bobPhases(format, amount)) {
                std::cerr << "format=" << format << " amount=" << amount << '\n';
                return 1;
            }
        }
    }
    return 0;
}
