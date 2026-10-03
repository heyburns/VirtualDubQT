// Independent scalar kernel references for the eight existing resize choices.
// Match the retained Windows filter-mode contract, not its platform-specific
// SIMD rounding. Reference comparisons allow at most one final integer LSB.
#include "VirtualDub/VDQtFilterSystem.h"
#include "VirtualDub/VDQtImageResampler.h"

#include <QCoreApplication>
#include <QRgba64>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
double referenceKernel(double value, int mode) {
    const double x = std::abs(value);
    if (mode == 0) return x < 0.5 ? 1 : 0;
    if (mode == 1 || mode == 3) return x < 1 ? 1 - x : 0;
    if (mode == 7) {
        if (x >= 3) return 0;
        if (x == 0) return 1;
        const double p = std::acos(-1.0);
        return (std::sin(p * x) / (p * x)) * (std::sin(p * x / 3) / (p * x / 3));
    }
    const double a = mode == 5 ? -0.60 : mode == 6 ? -1.0 : -0.75;
    if (x <= 1) return (a + 2) * x * x * x - (a + 3) * x * x + 1;
    if (x <= 2) return a * x * x * x - 5 * a * x * x + 8 * a * x - 4 * a;
    return 0;
}
QImage pattern(QImage::Format format, QSize size) {
    QImage image(size, format);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            const int red = (x * 73 + y * 29) & 255, green = (x * 11 + y * 131) & 255;
            const int blue = (x * 167 + y * 41) & 255, alpha = 128 + ((x * 47 + y * 13) & 127);
            if (format == QImage::Format_RGBA64)
                reinterpret_cast<QRgba64 *>(image.scanLine(y))[x] = QRgba64::fromRgba64(
                    red * 257 + ((x + y) & 127), green * 257 - (green > 0 ? ((x * 3 + y) & 127) : 0),
                    blue * 257, alpha * 257);
            else image.setPixelColor(x, y, QColor(red, green, blue, alpha));
        }
    }
    return image;
}
std::array<int, 4> channels(const QImage& image, int x, int y) {
    if (image.format() == QImage::Format_RGBA64) {
        const auto pixel = reinterpret_cast<const QRgba64 *>(image.constScanLine(y))[x];
        return {pixel.red(), pixel.green(), pixel.blue(), pixel.alpha()};
    }
    const auto pixel = image.pixelColor(x, y);
    return {pixel.red(), pixel.green(), pixel.blue(), pixel.alpha()};
}
std::array<int, 4> referencePixel(const QImage& input, QSize target, int x, int y, int mode) {
    const double rx = input.width() / static_cast<double>(target.width());
    const double ry = input.height() / static_cast<double>(target.height());
    const double cx = (x + 0.5) * rx - 0.5, cy = (y + 0.5) * ry - 0.5;
    if (mode == 0) return channels(input, std::clamp(static_cast<int>(std::floor(cx + 0.5)), 0, input.width() - 1),
                                  std::clamp(static_cast<int>(std::floor(cy + 0.5)), 0, input.height() - 1));
    const double scaleX = mode >= 3 ? std::max(1.0, rx) : 1;
    const double scaleY = mode >= 3 ? std::max(1.0, ry) : 1;
    const double radius = mode == 1 || mode == 3 ? 1 : mode == 7 ? 3 : 2;
    const int maximum = input.format() == QImage::Format_RGBA64 ? 65535 : 255;
    std::array<double, 4> accumulated{};
    double total = 0;
    for (int sy = static_cast<int>(std::ceil(cy - radius * scaleY)); sy <= std::floor(cy + radius * scaleY); ++sy) {
        const double wy = referenceKernel((sy - cy) / scaleY, mode);
        for (int sx = static_cast<int>(std::ceil(cx - radius * scaleX)); sx <= std::floor(cx + radius * scaleX); ++sx) {
            const double weight = wy * referenceKernel((sx - cx) / scaleX, mode);
            const auto pixel = channels(input, std::clamp(sx, 0, input.width() - 1), std::clamp(sy, 0, input.height() - 1));
            for (int channel = 0; channel < 3; ++channel)
                accumulated[channel] += pixel[channel] * (pixel[3] / static_cast<double>(maximum)) * weight;
            accumulated[3] += pixel[3] * weight;
            total += weight;
        }
    }
    for (double& value : accumulated) value /= total;
    const double alpha = accumulated[3];
    for (int channel = 0; channel < 3; ++channel)
        accumulated[channel] = alpha > 1e-12 ? accumulated[channel] * maximum / alpha : 0;
    std::array<int, 4> result{};
    for (int channel = 0; channel < 4; ++channel)
        result[channel] = std::clamp(std::llround(accumulated[channel]), 0LL, static_cast<long long>(maximum));
    return result;
}
QImage pipeline(const QImage& input, QSize target, int mode, bool interlaced = false) {
    VDQtFilterSystem system;
    system.addFilter(VDFilterType::Resize);
    system.updateFilterParams(0, {{"sizeMode", 0}, {"width", target.width()}, {"height", target.height()},
        {"filterMode", mode}, {"interlaced", interlaced ? 1 : 0}});
    return system.processFrame(input);
}
bool references(bool standaloneOnly = false) {
    bool passed = true;
    for (auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888, QImage::Format_RGBA64}) {
        for (auto sizes : {std::pair<QSize, QSize>{{13, 9}, {5, 4}}, {{7, 5}, {11, 8}}}) {
            const QImage input = pattern(format, sizes.first);
            const QImage original = input.copy();
            for (int mode = 0; mode < 8; ++mode) {
                const QImage standalone = VDQtResampleImage(input, sizes.second, mode);
                const QImage actual = standaloneOnly ? standalone : pipeline(input, sizes.second, mode);
                if (!check(!actual.isNull() && actual.size() == sizes.second
                           && !standalone.isNull(), "every resize mode returns the requested geometry")) return false;
                for (int y = 0; y < sizes.second.height(); ++y) {
                    for (int x = 0; x < sizes.second.width(); ++x) {
                        const auto expected = referencePixel(input, sizes.second, x, y, mode);
                        for (const auto& image : {actual, standalone}) {
                            const auto pixel = channels(image, x, y);
                            for (int channel = 0; channel < 4; ++channel) {
                                if (std::abs(pixel[channel] - expected[channel]) > 1) {
                                    std::cerr << "FAIL: mode " << mode << " format " << format << " at " << x << ',' << y
                                              << ": " << pixel[channel] << " versus reference " << expected[channel] << '\n';
                                    return false;
                                }
                            }
                        }
                    }
                }
                passed &= check(input == original, "resampling preserves the caller's source image");
            }
        }
    }
    return passed;
}
bool modeContracts() {
    const QImage input = pattern(QImage::Format_RGBA64, {13, 9});
    bool passed = check(pipeline(input, {20, 14}, 1) == pipeline(input, {20, 14}, 3)
        && pipeline(input, {20, 14}, 2) == pipeline(input, {20, 14}, 4),
        "interpolation and precise modes intentionally agree when enlarging");
    passed &= check(pipeline(input, {5, 4}, 1) != pipeline(input, {5, 4}, 3)
        && pipeline(input, {5, 4}, 2) != pipeline(input, {5, 4}, 4),
        "precise modes widen kernels to suppress reduction aliasing");
    const QImage cubic075 = pipeline(input, {20, 14}, 4), cubic060 = pipeline(input, {20, 14}, 5);
    const QImage cubic100 = pipeline(input, {20, 14}, 6), lanczos = pipeline(input, {20, 14}, 7);
    passed &= check(cubic075 != cubic060 && cubic075 != cubic100 && cubic060 != cubic100 && lanczos != cubic075,
                    "cubic parameters and Lanczos are real distinct kernels");
    return passed;
}
bool precisionAlphaFieldsAndScratch() {
    bool passed = true;
    QImage solid(7, 5, QImage::Format_RGBA64);
    const auto expected = QRgba64::fromRgba64(1001, 2345, 3456, 4567);
    solid.fill(QColor::fromRgba64(expected));
    for (int mode = 0; mode < 8; ++mode) {
        const auto output = pipeline(solid, {13, 9}, mode);
        bool exact = !output.isNull();
        for (int y = 0; y < output.height(); ++y)
            for (int x = 0; x < output.width(); ++x)
                exact &= reinterpret_cast<const QRgba64 *>(output.constScanLine(y))[x] == expected;
        passed &= check(exact, "constant colors and alpha retain exact 16-bit precision");
    }
    QImage edge(2, 1, QImage::Format_RGBA8888);
    edge.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    edge.setPixelColor(1, 0, QColor(0, 0, 255, 0));
    const auto blend = pipeline(edge, {3, 1}, 1).pixelColor(1, 0);
    passed &= check(blend.red() == 255 && blend.green() == 0 && blend.blue() == 0 && std::abs(blend.alpha() - 128) <= 1,
                    "transparent RGB cannot bleed into visible resampled color");
    const auto nearest = pipeline(edge, {4, 1}, 0);
    passed &= check(nearest.pixelColor(3, 0) == edge.pixelColor(1, 0), "nearest copies invisible RGB and alpha without conversion");
    QImage fields(7, 8, QImage::Format_RGBA64);
    for (int y = 0; y < fields.height(); ++y)
        for (int x = 0; x < fields.width(); ++x)
            reinterpret_cast<QRgba64 *>(fields.scanLine(y))[x] = y & 1
                ? QRgba64::fromRgba64(31001, 12345, 23456, 45678) : expected;
    for (int mode = 0; mode < 8; ++mode) {
        const auto output = pipeline(fields, {13, 14}, mode, true);
        bool fieldParity = !output.isNull();
        for (int y = 0; y < output.height(); ++y)
            fieldParity &= reinterpret_cast<const QRgba64 *>(output.constScanLine(y))[4]
                == reinterpret_cast<const QRgba64 *>(fields.constScanLine(y & 1))[0];
        passed &= check(fieldParity, "interlaced resampling never mixes temporal field parity");
    }
    QImage narrow(1, 4097, QImage::Format_RGBA64);
    narrow.fill(QColor::fromRgba64(expected));
    const auto wide = VDQtResampleImage(narrow, {4097, 1}, 1);
    passed &= check(!wide.isNull() && reinterpret_cast<const QRgba64 *>(wide.constScanLine(0))[4096] == expected,
                    "cross-aspect resize uses bounded scanlines instead of a huge scratch intermediate");
    return passed;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const bool kernelOnly = application.arguments().contains("--kernel-only");
    bool passed = references(kernelOnly);
    if (kernelOnly) return passed ? 0 : 1;
    passed &= modeContracts();
    passed &= precisionAlphaFieldsAndScratch();
    return passed ? 0 : 1;
}
