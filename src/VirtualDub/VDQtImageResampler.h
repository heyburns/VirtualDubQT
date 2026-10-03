// Integer-image implementation of the already offered VirtualDub resize modes.
// The retained Windows f_resize.cpp distinguishes interpolation-only modes
// (1/2) from reduction-aware modes (3..7). Widening the latter kernels while
// reducing is essential: selecting Lanczos must not call the same smooth Qt
// scaler as selecting bilinear. Coordinates are pixel-center based and edges
// are extended by repeating the closest source pixel.
#ifndef VDQTIMAGERESAMPLER_H
#define VDQTIMAGERESAMPLER_H

#include <QImage>
#include <QRgba64>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <new>
#include <vector>

namespace VDQtResampleDetail {
struct Tap { int position; double weight; };
using Axis = std::vector<std::vector<Tap>>;
using Pixel = std::array<double, 4>;

inline double kernel(double distance, int mode) {
    const double x = std::abs(distance);
    if (mode == 1 || mode == 3) return std::max(0.0, 1.0 - x);
    if (mode == 7) {
        if (x >= 3) return 0;
        if (x < 1e-12) return 1;
        constexpr double pi = 3.1415926535897932384626433832795;
        return std::sin(pi * x) * std::sin(pi * x / 3) / (pi * pi * x * x / 3);
    }
    const double a = mode == 5 ? -0.60 : mode == 6 ? -1.0 : -0.75;
    if (x < 1) return ((a + 2) * x - (a + 3)) * x * x + 1;
    if (x < 2) return ((a * x - 5 * a) * x + 8 * a) * x - 4 * a;
    return 0;
}

inline Axis axis(int source, int destination, int mode) {
    Axis result(destination);
    const double ratio = source / static_cast<double>(destination);
    const double scale = mode >= 3 ? std::max(1.0, ratio) : 1.0;
    const double support = (mode == 1 || mode == 3 ? 1.0 : mode == 7 ? 3.0 : 2.0) * scale;
    for (int out = 0; out < destination; ++out) {
        auto& taps = result[out];
        const double center = (out + 0.5) * ratio - 0.5;
        if (mode == 0) {
            taps.push_back({std::clamp(static_cast<int>(std::floor(center + 0.5)), 0, source - 1), 1});
            continue;
        }
        const int begin = static_cast<int>(std::ceil(center - support));
        const int end = static_cast<int>(std::floor(center + support));
        double total = 0;
        for (int sample = begin; sample <= end; ++sample) {
            const double weight = kernel((sample - center) / scale, mode);
            if (weight == 0) continue;
            const int position = std::clamp(sample, 0, source - 1);
            // Collapse repeated edge samples to avoid many identical reads
            // during large reductions. Normalize only after all contributions.
            if (!taps.empty() && taps.back().position == position) taps.back().weight += weight;
            else taps.push_back({position, weight});
            total += weight;
        }
        if (std::abs(total) < 1e-12) {
            taps.clear();
            taps.push_back({std::clamp(static_cast<int>(std::floor(center + 0.5)), 0, source - 1), 1});
        } else {
            for (auto& tap : taps) tap.weight /= total;
        }
    }
    return result;
}

inline Pixel read(const uchar *row, int x, QImage::Format format) {
    if (format == QImage::Format_RGBA64) {
        const QRgba64 pixel = reinterpret_cast<const QRgba64 *>(row)[x];
        const double alpha = pixel.alpha() / 65535.0;
        return {pixel.red() * alpha, pixel.green() * alpha, pixel.blue() * alpha, double(pixel.alpha())};
    }
    const int stride = format == QImage::Format_RGB888 ? 3 : 4;
    const uchar *pixel = row + x * stride;
    const double alpha = stride == 3 ? 1 : pixel[3] / 255.0;
    return {pixel[0] * alpha, pixel[1] * alpha, pixel[2] * alpha, stride == 3 ? 255.0 : double(pixel[3])};
}

inline void write(uchar *row, int x, QImage::Format format, Pixel pixel) {
    const int maximum = format == QImage::Format_RGBA64 ? 65535 : 255;
    // Filter color in premultiplied space to prevent invisible saturated pixels
    // from contaminating visible neighbors, but return straight RGBA as required
    // by the next raw-channel kernel. Quantize once, after both separable passes.
    const double alpha = pixel[3];
    if (alpha > 1e-12) {
        for (int channel = 0; channel < 3; ++channel) pixel[channel] *= maximum / alpha;
    } else {
        pixel[0] = pixel[1] = pixel[2] = 0;
    }
    const auto quantize = [maximum](double value) {
        return static_cast<quint16>(std::clamp(std::llround(value), 0LL, static_cast<long long>(maximum)));
    };
    if (format == QImage::Format_RGBA64) {
        reinterpret_cast<QRgba64 *>(row)[x] = QRgba64::fromRgba64(
            quantize(pixel[0]), quantize(pixel[1]), quantize(pixel[2]), quantize(alpha));
    } else {
        const int stride = format == QImage::Format_RGB888 ? 3 : 4;
        uchar *destination = row + x * stride;
        for (int channel = 0; channel < 3; ++channel) destination[channel] = quantize(pixel[channel]);
        if (stride == 4) destination[3] = quantize(alpha);
    }
}
} // namespace VDQtResampleDetail

// A caller-provided row scheduler shares the filter pipeline's existing task
// joining/allocation safeguards, rather than creating a second worker pool.
// Passing a sequential scheduler is useful for deterministic tiny reference
// tests. Destinations are detached before that scheduler can access them.
template <typename RowScheduler>
QImage VDQtResampleImage(const QImage& source, QSize target, int mode,
                        RowScheduler&& schedule, QString *error = nullptr) try {
    using namespace VDQtResampleDetail;
    const auto fail = [&](const char *message) {
        if (error) *error = QString::fromLatin1(message);
        return QImage();
    };
    if (error) error->clear();
    if (source.isNull() || target.isEmpty() || target.width() > 32768 || target.height() > 32768
        || source.width() > 32768 || source.height() > 32768 || mode < 0 || mode > 7)
        return fail("Invalid image geometry or resize mode.");
    if (source.format() != QImage::Format_RGB888 && source.format() != QImage::Format_RGBA8888
        && source.format() != QImage::Format_RGBA64)
        return fail("Resize requires a normalized straight RGB/RGBA integer image.");
    if (source.size() == target) return source;
    const int bytesPerPixel = source.depth() / 8;
    if (static_cast<qint64>(target.width()) * target.height() * bytesPerPixel > qint64{512} * 1024 * 1024)
        return fail("Resized image exceeds the 512 MiB frame allocation budget.");
    QImage output(target, source.format());
    uchar *destinationBits = output.bits();
    if (!destinationBits) return fail("Not enough memory for the resized image.");
    const qsizetype destinationStride = output.bytesPerLine();
    const uchar *sourceBits = source.constBits();
    const qsizetype sourceStride = source.bytesPerLine();
    if (mode == 0) {
        // Nearest is a sample copy, not a color transform. Preserve even RGB
        // hidden under fully transparent alpha, plus every high-depth bit.
        schedule(target.height(), static_cast<qint64>(target.width()) * target.height(), [&](int y) {
            const int sy = std::min(source.height() - 1,
                static_cast<int>((y + 0.5) * source.height() / target.height()));
            const uchar *inputRow = sourceBits + sy * sourceStride;
            uchar *row = destinationBits + y * destinationStride;
            for (int x = 0; x < target.width(); ++x) {
                const int sx = std::min(source.width() - 1,
                    static_cast<int>((x + 0.5) * source.width() / target.width()));
                std::memcpy(row + x * bytesPerPixel, inputRow + sx * bytesPerPixel, bytesPerPixel);
            }
        });
        return output;
    }
    const auto horizontal = axis(source.width(), target.width(), mode);
    const auto vertical = axis(source.height(), target.height(), mode);
    const auto horizontalRow = [&](int y, Pixel *row) {
        const uchar *inputRow = sourceBits + y * sourceStride;
        for (int x = 0; x < target.width(); ++x) {
            Pixel sum{};
            for (const auto& tap : horizontal[x]) {
                const Pixel pixel = read(inputRow, tap.position, source.format());
                for (int channel = 0; channel < 4; ++channel) sum[channel] += pixel[channel] * tap.weight;
            }
            row[x] = sum;
        }
    };
    // The usual separable path evaluates each horizontal source row once. A
    // pathological cross-aspect resize could otherwise require gigabytes of
    // double scratch despite tiny input/output images. Above 128 MiB, accumulate
    // one output row using two bounded scanlines instead of allocating the full
    // intermediate. That path is intentionally sequential to bound total scratch.
    constexpr qint64 scratchBudget = qint64{128} * 1024 * 1024;
    const qint64 scratchPixels = static_cast<qint64>(target.width()) * source.height();
    if (scratchPixels * static_cast<qint64>(sizeof(Pixel)) <= scratchBudget) {
        std::vector<Pixel> intermediate(static_cast<size_t>(scratchPixels));
        schedule(source.height(), scratchPixels, [&](int y) {
            horizontalRow(y, intermediate.data() + static_cast<qint64>(y) * target.width());
        });
        schedule(target.height(), static_cast<qint64>(target.width()) * target.height(), [&](int y) {
            uchar *row = destinationBits + y * destinationStride;
            for (int x = 0; x < target.width(); ++x) {
                Pixel sum{};
                for (const auto& tap : vertical[y]) {
                    const Pixel& pixel = intermediate[static_cast<qint64>(tap.position) * target.width() + x];
                    for (int channel = 0; channel < 4; ++channel) sum[channel] += pixel[channel] * tap.weight;
                }
                write(row, x, source.format(), sum);
            }
        });
    } else {
        std::vector<Pixel> horizontalValues(target.width()), accumulated(target.width());
        for (int y = 0; y < target.height(); ++y) {
            std::fill(accumulated.begin(), accumulated.end(), Pixel{});
            for (const auto& tap : vertical[y]) {
                horizontalRow(tap.position, horizontalValues.data());
                for (int x = 0; x < target.width(); ++x)
                    for (int channel = 0; channel < 4; ++channel)
                        accumulated[x][channel] += horizontalValues[x][channel] * tap.weight;
            }
            uchar *row = destinationBits + y * destinationStride;
            for (int x = 0; x < target.width(); ++x) write(row, x, source.format(), accumulated[x]);
        }
    }
    return output;
} catch (const std::bad_alloc&) {
    if (error) *error = QStringLiteral("Not enough memory to calculate the resize filter.");
    return {};
}

inline QImage VDQtResampleImage(const QImage& source, QSize target, int mode, QString *error = nullptr) {
    return VDQtResampleImage(source, target, mode,
        [](int rows, qint64, const auto& function) {
            for (int y = 0; y < rows; ++y) function(y);
        }, error);
}

#endif // VDQTIMAGERESAMPLER_H
