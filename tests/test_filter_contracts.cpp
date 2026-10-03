// Existing filter behavior contracts, independent of codec troubleshooting.
// Keep incoming-history/pixel-layout repairs separate from algorithm-parity work.
#include "VirtualDub/VDQtFilterSystem.h"

#include <QCoreApplication>
#include <QTransform>
#include <cmath>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool fieldHistory() {
    for (const auto type : {VDFilterType::FieldDelay, VDFilterType::Interlace}) {
        for (const auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888,
                                  QImage::Format_RGBA64}) {
            for (int parity : {0, 1}) {
                VDQtFilterSystem filters;
                filters.addFilter(type);
                auto params = filters.getActiveChain().first().params;
                params[type == VDFilterType::FieldDelay ? "field" : "fieldOrder"] = parity;
                filters.updateFilterParams(0, params);
                QImage previousInput;
                for (int frame = 0; frame < 4; ++frame) {
                    QImage input(17, 9, format);
                    input.fill(QColor(40 * (frame + 1), 30, 60, 80 + frame * 20));
                    const QImage original = input.copy();
                    const QImage output = filters.processFrame(input, {frame, frame / 25.0, 25});
                    if (!check(!output.isNull() && input == original,
                               "field processing leaves caller-owned input unchanged")) return false;
                    for (int y = 0; y < input.height(); ++y) {
                        const QImage& expected = !previousInput.isNull() && (y & 1) == parity
                            ? previousInput : input;
                        if (!check(output.pixelColor(5, y) == expected.pixelColor(5, y),
                                   "delayed field comes from the preceding incoming stage frame")) return false;
                    }
                    previousInput = input;
                }
                QImage discontinuous(17, 9, format);
                discontinuous.fill(QColor(210, 80, 40, 150));
                if (!check(filters.processFrame(discontinuous, {100, 4, 25}) == discontinuous,
                           "field history does not cross a seek discontinuity")) return false;
            }
        }
    }
    return true;
}

bool rotatedLayout() {
    for (const auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888,
                              QImage::Format_RGBA64}) {
        QImage input(33, 29, format);
        input.fill(QColor(255, 0, 0, 180));
        VDQtFilterSystem rotate;
        rotate.addFilter(VDFilterType::Rotate2);
        auto params = rotate.getActiveChain().first().params;
        params["angle"] = 15;
        rotate.updateFilterParams(0, params);
        const QImage rotated = rotate.processFrame(input);
        if (!check(!rotated.isNull(), "rotation produces an image")) return false;
        VDQtFilterSystem grayscale;
        grayscale.addFilter(VDFilterType::Grayscale);
        const QImage expected = grayscale.processFrame(rotated);
        rotate.addFilter(VDFilterType::Grayscale);
        const QImage actual = rotate.processFrame(input);
        if (!check(actual == expected,
                   "Rotate2 followed by grayscale respects actual layout and straight alpha")) return false;
    }
    return true;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.contains("field")) return fieldHistory() ? 0 : 1;
    if (args.contains("layout")) return rotatedLayout() ? 0 : 1;
    return fieldHistory() && rotatedLayout() ? 0 : 1;
}
