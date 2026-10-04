// Independent SixAxis cube-weight/neutral contracts, not a codec test suite.
// The existing RGB cube remains approximate; its last cell must nevertheless
// interpolate its real 248..255 endpoints, and a neutral filter is an identity.
#include "VirtualDub/VDQtFilterSystem.h"
#include "VirtualDub/VDQtVideoAspect.h"
#include "VirtualDub/VDQtVideoExporter.h"
#include "support/VDQtTestFixtures.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QRgba64>
#include <QThreadPool>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
const std::array<const char*, 10> controls{
    "intensity", "red_green", "yellow_blue", "saturation",
    "red", "orange", "lime", "emerald", "blue", "purple"};
QMap<QString, double> neutral() {
    QMap<QString, double> result;
    for (const char *control : controls) result[control] = 1.0;
    result["red_green"] = result["yellow_blue"] = 0.0;
    return result;
}
std::array<float, 3> scalarCorrection(float r, float g, float b,
                                     const QMap<QString, double>& p) {
    // Independently evaluated control equations. This defines the quantized
    // lattice nodes and native-depth scalar expectation, not production weights.
    const float maximum = std::max(r, std::max(g, b));
    const float minimum = std::min(r, std::min(g, b));
    const float delta = maximum - minimum;
    float hue = 0.0f;
    const float saturation = maximum > 1e-5f ? delta / maximum : 0.0f;
    if (delta > 1e-5f) {
        if (maximum == r) hue = 60.0f * std::fmod(((g - b) / delta) + 6.0f, 6.0f);
        else if (maximum == g) hue = 60.0f * (((b - r) / delta) + 2.0f);
        else hue = 60.0f * (((r - g) / delta) + 4.0f);
    }
    const std::array<float, 6> angles{0, 30, 90, 180, 240, 300};
    float modulation = 0;
    for (int axis = 0; axis < 6; ++axis) {
        float distance = std::abs(hue - angles[axis]);
        if (distance > 180.0f) distance = 360.0f - distance;
        if (distance < 60.0f)
            modulation += (1.0f - distance / 60.0f)
                * (float(p.value(controls[axis + 4], 1.0)) - 1.0f);
    }
    const float chroma = maximum * std::clamp(
        saturation * float(p.value("saturation", 1.0)) * (1.0f + modulation), 0.0f, 1.0f);
    const float middle = chroma * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    const float match = maximum - chroma;
    std::array<float, 3> rgb{};
    if (hue < 60) { rgb[0] = chroma; rgb[1] = middle; }
    else if (hue < 120) { rgb[0] = middle; rgb[1] = chroma; }
    else if (hue < 180) { rgb[1] = chroma; rgb[2] = middle; }
    else if (hue < 240) { rgb[1] = middle; rgb[2] = chroma; }
    else if (hue < 300) { rgb[0] = middle; rgb[2] = chroma; }
    else { rgb[0] = chroma; rgb[2] = middle; }
    const float intensity = float(p.value("intensity", 1.0));
    const float redGreen = float(p.value("red_green", 0.0));
    const float yellowBlue = float(p.value("yellow_blue", 0.0));
    return {(rgb[0] + match) * intensity + redGreen * 0.15f + yellowBlue * 0.08f,
            (rgb[1] + match) * intensity - redGreen * 0.15f + yellowBlue * 0.08f,
            (rgb[2] + match) * intensity - yellowBlue * 0.16f};
}
using Cube = std::vector<std::array<int, 3>>;
Cube referenceCube(const QMap<QString, double>& p) {
    Cube cube(33 * 33 * 33);
    for (int r = 0; r < 33; ++r)
        for (int g = 0; g < 33; ++g)
            for (int b = 0; b < 33; ++b) {
                const auto adjusted = scalarCorrection(std::min(255, r * 8) / 255.0f,
                    std::min(255, g * 8) / 255.0f, std::min(255, b * 8) / 255.0f, p);
                auto& node = cube[(r * 33 + g) * 33 + b];
                for (int c = 0; c < 3; ++c)
                    node[c] = int(std::clamp(std::lround(adjusted[c] * 255.0f), 0L, 255L));
            }
    return cube;
}
std::array<int, 3> cubePixel(std::array<int, 3> rgb, const Cube& cube, bool corrected) {
    std::array<int, 3> index, fraction, span;
    int divisor = 1;
    for (int c = 0; c < 3; ++c) {
        index[c] = rgb[c] >> 3;
        span[c] = corrected && index[c] == 31 ? 7 : 8;
        fraction[c] = !corrected && rgb[c] == 255 ? 8 : rgb[c] & 7;
        divisor *= span[c];
    }
    std::array<qint64, 3> sums{};
    // Cartesian products sum all eight corners directly. Production uses
    // separable interpolation; neither implementation rounds intermediate axes.
    for (int corner = 0; corner < 8; ++corner) {
        std::array<int, 3> node = index;
        int weight = 1;
        for (int c = 0; c < 3; ++c) {
            const bool high = corner & (1 << c);
            weight *= high ? fraction[c] : span[c] - fraction[c];
            if (high) ++node[c];
        }
        const auto& value = cube[(node[0] * 33 + node[1]) * 33 + node[2]];
        for (int c = 0; c < 3; ++c) sums[c] += value[c] * weight;
    }
    return {int((sums[0] + divisor / 2) / divisor),
            int((sums[1] + divisor / 2) / divisor),
            int((sums[2] + divisor / 2) / divisor)};
}
QImage image(QImage::Format format, QSize size = QSize(256, 4), bool interior = false) {
    QImage result(size, format);
    for (int y = 0; y < result.height(); ++y) {
        uchar *row = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            std::array<int, 3> rgb{(x * 53 + y * 13) & 255, (x * 17 + y * 29) & 255,
                                  (x * 31 + y * 7) & 255};
            if ((y & 3) == 0) rgb = {x & 255, x & 255, x & 255};
            if ((y & 3) == 1) rgb = {248 + (x & 7), (x * 29) & 255, 248 + ((x >> 3) & 7)};
            if (interior || (y & 3) == 2) for (int& value : rgb) value %= 248;
            const int alpha = (x * 11 + y * 19) & 255;
            if (format == QImage::Format_RGBA64)
                reinterpret_cast<QRgba64*>(row)[x] = QRgba64::fromRgba64(
                    (rgb[0] * 257 + x * 11 + 31) & 65535,
                    (rgb[1] * 257 + y * 17 + 53) & 65535,
                    (rgb[2] * 257 + x * 7 + y * 3 + 71) & 65535,
                    (alpha * 257 + x * 3 + y * 11 + 13) & 65535);
            else {
                const int bpp = format == QImage::Format_RGB888 ? 3 : 4;
                for (int c = 0; c < 3; ++c) row[x * bpp + c] = uchar(rgb[c]);
                if (bpp == 4) row[x * bpp + 3] = uchar(alpha);
            }
        }
    }
    return result;
}
QImage correctedImage(const QImage& input, const QMap<QString, double>& p, const Cube& cube) {
    QImage expected = input.copy();
    const int bpp = input.format() == QImage::Format_RGB888 ? 3 : 4;
    for (int y = 0; y < expected.height(); ++y) {
        for (int x = 0; x < expected.width(); ++x) {
            if (input.format() == QImage::Format_RGBA64) {
                const QRgba64 original = reinterpret_cast<const QRgba64*>(input.constScanLine(y))[x];
                const auto adjusted = scalarCorrection(original.red() / 65535.0f,
                    original.green() / 65535.0f, original.blue() / 65535.0f, p);
                reinterpret_cast<QRgba64*>(expected.scanLine(y))[x] = QRgba64::fromRgba64(
                    quint16(std::clamp(std::lround(adjusted[0] * 65535.0f), 0L, 65535L)),
                    quint16(std::clamp(std::lround(adjusted[1] * 65535.0f), 0L, 65535L)),
                    quint16(std::clamp(std::lround(adjusted[2] * 65535.0f), 0L, 65535L)), original.alpha());
            } else {
                const uchar *original = input.constScanLine(y) + x * bpp;
                const auto rgb = cubePixel({original[0], original[1], original[2]}, cube, true);
                for (int c = 0; c < 3; ++c) expected.scanLine(y)[x * bpp + c] = uchar(rgb[c]);
                if (original[0] < 248 && original[1] < 248 && original[2] < 248
                    && rgb != cubePixel({original[0], original[1], original[2]}, cube, false)) return {};
            }
        }
    }
    return expected;
}
bool contracts() {
    std::vector<QMap<QString, double>> profiles{neutral()};
    auto intensity = neutral(); intensity["intensity"] = 0.75; profiles.push_back(intensity);
    auto mixed = neutral();
    mixed["red_green"] = 0.125; mixed["yellow_blue"] = -0.2;
    mixed["saturation"] = 1.2; mixed["red"] = 1.3; mixed["orange"] = 0.7;
    mixed["lime"] = 0.8; mixed["emerald"] = 1.4; mixed["blue"] = 0.8;
    mixed["purple"] = 1.5; profiles.push_back(mixed);
    for (const char *key : controls) {
        auto changed = neutral();
        changed[key] += changed.value(key) == 0.0 ? 0.125 : -0.25;
        profiles.push_back(changed);
    }
    for (const auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888, QImage::Format_RGBA64}) {
        QImage input = image(format);
        VDQtSetImageSampleAspectRatio(input, {4, 3});
        const QImage snapshot = input.copy();
        VDQtFilterSystem filters; filters.addFilter(VDFilterType::SixAxis);
        for (size_t profile = 0; profile < profiles.size(); ++profile) {
            const auto& parameters = profiles[profile];
            filters.updateFilterParams(0, parameters);
            const QImage actual = filters.processFrame(input);
            const QImage expected = profile == 0 ? input
                : correctedImage(input, parameters, referenceCube(parameters));
            if (!check(!actual.isNull() && actual == expected,
                       "SixAxis matches actual-width trilinear/native-depth reference with unchanged interior and alpha")
                || !check(input == snapshot, "SixAxis leaves caller-owned pixels unchanged")
                || !check(av_cmp_q(VDQtImageSampleAspectRatio(actual), {4, 3}) == 0,
                          "SixAxis preserves sample aspect metadata")) {
                std::cerr << "format=" << format << " profile=" << profile << '\n'; return false;
            }
            if (profile == 0 && !check(actual.constBits() == input.constBits()
                    && filters.cacheStatistics().sixAxisEntries == 0,
                    "neutral controls preserve shared storage without building a cube")) return false;
        }
        const auto changed = profiles[1];
        filters.updateFilterParams(0, neutral());
        if (!check(filters.processFrame(input) == input,
                   "restoring neutral controls cannot reuse a nonneutral cached correction")) return false;
        filters.updateFilterParams(0, changed);
        if (!check(filters.processFrame(input) == correctedImage(input, changed, referenceCube(changed))
                && filters.cacheStatistics().sixAxisEntries <= 8,
                   "returning to nonneutral controls uses the correct bounded cache")) return false;
        filters.clearFilters();
        if (!check(filters.cacheStatistics().sixAxisEntries == 0, "Clear releases SixAxis cache")) return false;
    }
    return true;
}
bool wrappers() {
    QImage input = image(QImage::Format_RGBA8888);
    VDQtSetImageSampleAspectRatio(input, {8, 9});
    const QImage snapshot = input.copy();
    VDQtFilterSystem filters; filters.addFilter(VDFilterType::SixAxis);
    auto p = neutral();
    p["_sylia.clip.left"] = 1;
    filters.updateFilterParams(0, p);
    if (!check(filters.processFrame(input) == input.copy(1, 0, input.width() - 1, input.height()),
               "neutral bypass still applies input clipping")) return false;
    p = neutral(); p["_sylia.opacity.count"] = 1; p["_sylia.opacity.0.y"] = 0.5;
    filters.updateFilterParams(0, p);
    QImage expected = input.copy();
    QPainter painter(&expected); painter.setOpacity(0.5); painter.drawImage(0, 0, input); painter.end();
    const QImage blended = filters.processFrame(input);
    if (!check(blended == expected && av_cmp_q(VDQtImageSampleAspectRatio(blended), {8, 9}) == 0,
               "neutral bypass keeps the existing common opacity and metadata tail")) return false;
    p = neutral(); p["saturation"] = 0.75;
    p["_sylia.range.start"] = 10; p["_sylia.range.end"] = 20;
    filters.updateFilterParams(0, p);
    if (!check(filters.processFrame(input, {0, 0, 25}) == input
            && filters.cacheStatistics().sixAxisEntries == 0, "range bypass still precedes SixAxis processing")) return false;
    filters.setFilterEnabled(0, false);
    if (!check(filters.processFrame(input, {10, 0.4, 25}) == input,
               "disabled SixAxis remains an identity")) return false;
    filters.setFilterEnabled(0, true);
    p = neutral(); p["intensity"] = 1.0 + 1e-12;
    filters.updateFilterParams(0, p);
    if (!check(!filters.processFrame(input).isNull() && filters.cacheStatistics().sixAxisEntries == 1,
               "neutral detection is exact, not an epsilon or float-rounding shortcut")) return false;
    p["intensity"] = std::numeric_limits<double>::quiet_NaN();
    filters.updateFilterParams(0, p);
    return check(filters.processFrame(input).isNull() && !filters.lastError().isEmpty()
                     && input == snapshot, "neutral optimization cannot bypass invalid-control checks or mutate input");
}
bool native12Raw(VDQtTestFixtures& fixtures) {
    const QString source = fixtures.directory.filePath("sixaxis-native12.avs");
    if (!fixtures.writeText(source,
        "ClearAutoloadDirs()\n"
        "BlankClip(length=1,width=17,height=10,pixel_type=\"RGBAP12\",audio_rate=0,colors=[1025,513,2049,2048])\n")) return false;
    VDQtVideoDecoder decoder;
    if (!check(decoder.openFile(source) && decoder.getSourceBitDepth() == 12,
               "native12-bit owned source opens at its actual source precision")) return false;
    const QImage decoded = decoder.getFrameImage(0);
    if (!check(!decoded.isNull() && decoded.format() == QImage::Format_RGBA64,
               "native12-bit source normalizes to the native16-bit filter buffer")) return false;
    const auto first = reinterpret_cast<const QRgba64*>(decoded.constScanLine(0))[0];
    if (!check(first.red() % 257 || first.green() % 257 || first.blue() % 257,
               "native12-bit fixture genuinely contains details below8-bit precision")) return false;
    VDQtFilterSystem filters; filters.addFilter(VDFilterType::SixAxis);
    if (!check(filters.processFrame(decoded) == decoded, "neutral SixAxis preserves every normalized12-bit sample")) return false;
    VDQtVideoExporter::RawExportOptions options;
    options.inputPath = source; options.outputPath = fixtures.directory.filePath("sixaxis-native12.raw");
    options.endFrame = 0; options.pixelFormat = Q_BYTE_ORDER == Q_LITTLE_ENDIAN ? "rgba64le" : "rgba64be";
    options.scanlineAlignment = 1; options.unattended = true;
    options.processing = VDQtVideoExporter::ProcessingSnapshot{};
    options.processing->filters = filters.getActiveChain();
    VDQtVideoExporter exporter;
    QFile output(options.outputPath);
    return check(exporter.exportRawVideo(options, &decoder) && output.open(QIODevice::ReadOnly)
        && output.readAll() == QByteArray(reinterpret_cast<const char*>(decoded.constBits()), decoded.sizeInBytes()),
        "raw export with neutral SixAxis matches native12-bit decoded bytes, including alpha and low bits");
}
bool benchmark() {
    for (const auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888, QImage::Format_RGBA64}) {
        for (const QSize size : {QSize(1920, 1080), QSize(3840, 2160)}) {
            for (int mode = 0; mode < 3; ++mode) {
                const QImage input = image(format, size, mode == 1);
                auto p = neutral();
                if (mode != 0) { p["intensity"] = 0.75; p["saturation"] = 1.2; p["red"] = 1.3; }
                VDQtFilterSystem filters; filters.addFilter(VDFilterType::SixAxis);
                filters.updateFilterParams(0, p);
                QImage output = filters.processFrame(input);
                std::array<double, 9> samples;
                QElapsedTimer elapsed;
                for (double& sample : samples) {
                    elapsed.start(); output = filters.processFrame(input);
                    if (output.isNull()) return false;
                    sample = elapsed.nsecsElapsed() / 1000000.0;
                }
                std::sort(samples.begin(), samples.end());
                std::cout << size.width() << 'x' << size.height() << " format=" << format
                          << " mode=" << (mode == 0 ? "neutral" : mode == 1 ? "nonneutral-interior" : "nonneutral-endpoint")
                          << " median=" << samples[4] << "ms\n";
            }
        }
    }
    return true;
}
}

int main(int argc, char **argv) {
    VDQtTestFixtures fixtures;
    if (!fixtures.directory.isValid()) {
        std::cerr << "Cannot create isolated test directory: "
                  << fixtures.directory.errorString().toStdString() << '\n';
        return 2;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", fixtures.directory.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", fixtures.directory.filePath("data").toUtf8());
    QApplication application(argc, argv);
    QThreadPool::globalInstance()->setMaxThreadCount(8);
    const bool passed = application.arguments().contains("--benchmark")
        ? benchmark() : contracts() && wrappers() && native12Raw(fixtures);
    if (!fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return passed ? 0 : 1;
}
