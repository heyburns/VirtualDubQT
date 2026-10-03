// Independent integer box-blur contracts and an opt-in controlled benchmark.
// Preserve the port's existing horizontal/vertical order for every power pass;
// this optimization must not silently change rounding, borders or alpha.
#include "VirtualDub/VDQtFilterSystem.h"
#include "support/VDQtTestFixtures.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QRgba64>
#include <algorithm>
#include <array>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
int readChannel(const QImage& image, int x, int y, int channel) {
    if (image.format() == QImage::Format_RGBA64) {
        const QRgba64 pixel = reinterpret_cast<const QRgba64*>(image.constScanLine(y))[x];
        return channel == 0 ? pixel.red() : channel == 1 ? pixel.green() : pixel.blue();
    }
    const int bpp = image.format() == QImage::Format_RGB888 ? 3 : 4;
    return image.constScanLine(y)[x * bpp + channel];
}
void writeChannel(QImage& image, int x, int y, int channel, int value) {
    if (image.format() == QImage::Format_RGBA64) {
        QRgba64& pixel = reinterpret_cast<QRgba64*>(image.scanLine(y))[x];
        if (channel == 0) pixel.setRed(value);
        else if (channel == 1) pixel.setGreen(value);
        else pixel.setBlue(value);
    } else {
        const int bpp = image.format() == QImage::Format_RGB888 ? 3 : 4;
        image.scanLine(y)[x * bpp + channel] = static_cast<uchar>(value);
    }
}
QImage scalarBlur(QImage input, int radius, int power) {
    const int divisor = radius * 2 + 1;
    for (int pass = 0; pass < power; ++pass) {
        QImage horizontal = input.copy();
        for (int y = 0; y < input.height(); ++y)
            for (int x = 0; x < input.width(); ++x)
                for (int c = 0; c < 3; ++c) {
                    qint64 sum = 0;
                    for (int dx = -radius; dx <= radius; ++dx)
                        sum += readChannel(input, std::clamp(x + dx, 0, input.width() - 1), y, c);
                    writeChannel(horizontal, x, y, c, static_cast<int>(sum / divisor));
                }
        QImage output = horizontal.copy();
        for (int y = 0; y < input.height(); ++y)
            for (int x = 0; x < input.width(); ++x)
                for (int c = 0; c < 3; ++c) {
                    qint64 sum = 0;
                    for (int dy = -radius; dy <= radius; ++dy)
                        sum += readChannel(horizontal, x, std::clamp(y + dy, 0, input.height() - 1), c);
                    writeChannel(output, x, y, c, static_cast<int>(sum / divisor));
                }
        input = output;
    }
    return input;
}
QImage sourceImage(QSize size, QImage::Format format) {
    QImage image = VDQtTestFixtures::patternedImage(size.width(), size.height(), format, 3);
    if (format == QImage::Format_RGBA64) {
        for (int y = 0; y < image.height(); ++y) {
            auto *row = reinterpret_cast<QRgba64*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                row[x].setRed((row[x].red() + x * 11 + y * 19 + 31) & 65535);
                row[x].setGreen((row[x].green() + x * 7 + y * 3 + 53) & 65535);
                row[x].setBlue((row[x].blue() + x * 3 + y * 23 + 71) & 65535);
                row[x].setAlpha((row[x].alpha() + x * 17 + y * 31 + 13) & 65535);
            }
        }
    }
    return image;
}
void configure(VDQtFilterSystem& filters, int radius, int power) {
    auto parameters = filters.getActiveChain().first().params;
    parameters["width"] = radius;
    parameters["power"] = power;
    filters.updateFilterParams(0, parameters);
}
bool contracts() {
    for (QImage::Format format : {QImage::Format_RGB888, QImage::Format_RGBA8888, QImage::Format_RGBA64}) {
        for (QSize size : {QSize(1, 1), QSize(1, 17), QSize(17, 1), QSize(17, 9),
                           QSize(64, 65), QSize(65, 65), QSize(97, 65), QSize(1921, 257)}) {
            const QImage input = sourceImage(size, format);
            const QImage snapshot = input.copy();
            VDQtFilterSystem filters;
            filters.addFilter(VDFilterType::Blur);
            for (int radius = 1; radius <= 48; ++radius) {
                if (size != QSize(17, 9) && radius != 1 && radius != 3
                    && radius != 8 && radius != 24 && radius != 48) continue;
                // Large parallel images cover row scheduling without making the
                // intentionally O(radius) reference a benchmark itself.
                if (size.width() > 100 && radius != 1 && radius != 3 && radius != 48) continue;
                for (int power : {1, 2, 3}) {
                    configure(filters, radius, power);
                    const QImage actual = filters.processFrame(input);
                    const QImage expected = scalarBlur(input, radius, power);
                    if (!check(!actual.isNull() && actual == expected,
                               "blur pixels exactly match independent scalar floor/border/alpha reference")
                        || !check(input == snapshot, "blur leaves caller-owned pixels unchanged")) {
                        std::cerr << "size=" << size.width() << 'x' << size.height()
                                  << " format=" << format << " radius=" << radius << " power=" << power << '\n';
                        return false;
                    }
                }
            }
        }
        // Saturated channels exercise the actual accumulator bound, while
        // alpha remains unchanged even at the maximum validated radius.
        QImage saturated(3, 2, format);
        saturated.fill(QColor(255, 255, 255, 37));
        VDQtFilterSystem filters;
        filters.addFilter(VDFilterType::Blur);
        configure(filters, 48, 3);
        if (!check(filters.processFrame(saturated) == saturated,
                   "maximum-radius saturated channels cannot overflow the exact accumulator")) return false;
    }
    return true;
}
bool benchmark() {
    for (QImage::Format format : {QImage::Format_RGB888, QImage::Format_RGBA8888, QImage::Format_RGBA64}) {
        for (QSize size : {QSize(1920, 1080), QSize(3840, 2160)}) {
            const QImage input = sourceImage(size, format);
            VDQtFilterSystem filters;
            filters.addFilter(VDFilterType::Blur);
            configure(filters, 3, 2);
            QImage output = filters.processFrame(input);
            if (output.isNull()) return false;
            std::array<double, 7> samples;
            QElapsedTimer timer;
            for (double& elapsed : samples) {
                timer.start();
                output = filters.processFrame(input);
                if (output.isNull()) return false;
                elapsed = timer.nsecsElapsed() / 1000000.0;
            }
            std::sort(samples.begin(), samples.end());
            std::cout << size.width() << 'x' << size.height() << " format=" << format
                      << " radius3/power2 median=" << samples[3] << " ms\n";
        }
    }
    return true;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QThreadPool::globalInstance()->setMaxThreadCount(8);
    return application.arguments().contains("--benchmark")
        ? (benchmark() ? 0 : 1) : (contracts() ? 0 : 1);
}
