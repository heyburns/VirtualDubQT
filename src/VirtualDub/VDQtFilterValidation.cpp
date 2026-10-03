#include "VDQtFilterValidation.h"

#include <cmath>
#include <QTransform>

namespace {
bool isOneOf(const QString& key, std::initializer_list<const char *> keys) {
    for (const char *candidate : keys) if (key == QLatin1String(candidate)) return true;
    return false;
}
}

VDQtFilterParameterSpec VDQtFilterParameter(VDFilterType type, const QString& key) {
    if (key == "_sylia.range.start") return {0, 9007199254740991.0, true};
    if (key == "_sylia.range.end") return {-1, 9007199254740991.0, true};
    if (key == "_sylia.opacity.count") return {0, 4096, true};
    if (key.startsWith("_sylia.opacity.")) {
        if (key.endsWith(".y")) return {0, 1, false};
        if (key.endsWith(".linear")) return {0, 1, true};
        if (key.endsWith(".x")) return {0, 9007199254740991.0, false};
    }
    if (key == "_sylia.clip.precise") return {0, 1, true};
    if (key.startsWith("_sylia.clip.") || key.startsWith("_sylia.opacityClip."))
        return {0, 32768, true};
    if (type == VDFilterType::SixAxis) {
        if (isOneOf(key, {"red_green", "yellow_blue"})) return {-1, 1, false};
        if (isOneOf(key, {"intensity", "saturation", "red", "orange", "lime", "emerald", "blue", "purple"}))
            return {0, 2, false};
    }
    if (key == "mode") {
        if (type == VDFilterType::BobDoubler) return {0, 4, true};
        if (type == VDFilterType::Rotate || type == VDFilterType::Deinterlace) return {0, 2, true};
    }
    if (isOneOf(key, {"field", "fieldOrder", "field_order", "format", "toLinear", "expand", "outline", "interlaced"})) {
        if (key == "format") return {0, 2, true};
        return {0, 1, true};
    }
    if (isOneOf(key, {"inputBlack", "inputWhite", "outputBlack", "outputWhite", "black", "white"}))
        return {0, 255, false};
    if (isOneOf(key, {"red", "green", "blue", "fillColorR", "fillColorG", "fillColorB"})) return {0, 255, true};
    if (isOneOf(key, {"opacity", "alpha", "scanline"})) return {0, key == "scanline" ? 0.8 : 1, false};
    if (key == "gamma") return {0.05, 20, false};
    if (key == "hueDegrees" || key == "angle") return {-360, 360, false};
    if (key == "saturation" || key == "value") return {0, 8, false};
    if (key == "threshold") return {0, 255, false};
    if (key == "levels") return {2, 256, true};
    if (key == "blockSize") return {2, 256, true};
    if (key == "strength") return {0, 8, false};
    if (key == "depth") return {0, 64, true};
    if (key == "chromaBlur") return {0, 8, true};
    if (key == "bright") return {-256, 256, true};
    if (key == "cont") return {0, 32, true};
    if (key == "amount") return {0, type == VDFilterType::Sharpen ? 64.0 : 1.0,
                                  type == VDFilterType::Sharpen};
    if (key == "power") return {1, 3, true};
    if (key == "radius") return {0, type == VDFilterType::Blur ? 50.0 : 8.0, true};
    if (key == "width" && type == VDFilterType::Blur) return {1, 48, true};
    if (isOneOf(key, {"width", "height", "absW", "absH", "frameW", "frameH"})) return {0, 32768, true};
    if (key == "x" || key == "y") return {-32768, 32768, true};
    if (isOneOf(key, {"left", "top", "right", "bottom"})) return {0, 32768, true};
    if (key == "size") return {6, 512, true};
    if (key == "sizeMode") return {0, 1, true};
    if (key == "aspectMode") return {0, 2, true};
    if (key == "filterMode") return {0, 7, true};
    if (key == "framingMode") return {0, 3, true};
    if (key == "codecAdjust") return {0, 16, true};
    if (key == "relW" || key == "relH") return {0.01, 1000, false};
    if (isOneOf(key, {"aspectW", "aspectH", "frameAspectW", "frameAspectH"})) return {1, 100, false};
    if (type == VDFilterType::Perspective) return {-8, 8, false};
    return {};
}

bool VDQtValidateFilter(const VDFilterInstance& filter, QString *error,
                        QSize inputSize, int bytesPerPixel) {
    const auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (error) error->clear();
    if (filter.type < VDFilterType::SixAxis || filter.type >= VDFilterType::Count)
        return fail(QStringLiteral("Unknown filter type."));
    for (auto it = filter.params.cbegin(); it != filter.params.cend(); ++it) {
        const auto spec = VDQtFilterParameter(filter.type, it.key());
        const double value = it.value();
        if (!std::isfinite(value) || value < spec.minimum || value > spec.maximum
            || (spec.integer && value != std::trunc(value)))
            return fail(QStringLiteral("Parameter '%1' must be %2between %3 and %4.")
                        .arg(it.key(), spec.integer ? QStringLiteral("an integer ") : QString(),
                             QString::number(spec.minimum, 'g', 16), QString::number(spec.maximum, 'g', 16)));
    }
    if (filter.type == VDFilterType::Levels) {
        if (filter.params.value("inputWhite", 255) <= filter.params.value("inputBlack", 0))
            return fail(QStringLiteral("Input white must be greater than input black."));
        if (filter.params.value("outputWhite", 255) < filter.params.value("outputBlack", 0))
            return fail(QStringLiteral("Output white must not be below output black."));
    }
    if (filter.type == VDFilterType::Curves
        && filter.params.value("white", 255) <= filter.params.value("black", 0))
        return fail(QStringLiteral("White must be greater than black."));
    if (filter.params.value("_sylia.range.end", -1) >= 0
        && filter.params.value("_sylia.range.end") < filter.params.value("_sylia.range.start", 0))
        return fail(QStringLiteral("The filter range end precedes its start."));
    const double adjust = filter.params.value("codecAdjust", 0);
    if (filter.type == VDFilterType::Resize && adjust != 0 && adjust != 2 && adjust != 4 && adjust != 8 && adjust != 16)
        return fail(QStringLiteral("Codec size alignment must be 0, 2, 4, 8 or 16."));
    // Bound frame allocations before converting calculated dimensions to int.
    // Caller passes actual stage geometry and precision, not original source size.
    if (!inputSize.isEmpty()) {
        if (filter.type == VDFilterType::ConvertFormat && filter.params.value("format", 0) == 2)
            bytesPerPixel = 8;
        const auto bounded = [bytesPerPixel](double width, double height) {
            return std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0
                && width <= 32768 && height <= 32768
                && static_cast<long double>(std::ceil(width)) * std::ceil(height)
                    * bytesPerPixel <= 512.0L * 1024 * 1024;
        };
        double width = inputSize.width(), height = inputSize.height();
        if (filter.type == VDFilterType::Resize) {
            if (filter.params.value("sizeMode", 0) == 1) {
                width *= filter.params.value("relW", 100) / 100;
                height *= filter.params.value("relH", 100) / 100;
            } else {
                width = filter.params.value("width", width);
                height = filter.params.value("height", height);
            }
            if (!bounded(width, height))
                return fail(QStringLiteral("Resized intermediate exceeds the frame dimension/memory budget."));
            const int framing = filter.params.value("framingMode", 0);
            if (framing == 1) {
                width = filter.params.value("frameW", width);
                height = filter.params.value("frameH", height);
            } else if (framing == 2 || framing == 3) {
                const double ratio = filter.params.value("frameAspectW", 4) / filter.params.value("frameAspectH", 3);
                if ((width / height < ratio) == (framing == 3)) width = height * ratio;
                else height = width / ratio;
            }
        } else if (filter.type == VDFilterType::Canvas || filter.type == VDFilterType::WarpResize) {
            width = filter.params.value("width", width);
            height = filter.params.value("height", height);
            if (width == 0) width = inputSize.width();
            if (height == 0) height = inputSize.height();
        } else if (filter.type == VDFilterType::Crop) {
            width -= filter.params.value("left", 0) + filter.params.value("right", 0);
            height -= filter.params.value("top", 0) + filter.params.value("bottom", 0);
        } else if (filter.type == VDFilterType::Rotate2) {
            QTransform rotation;
            rotation.rotate(filter.params.value("angle", 0));
            const QRect extent = QImage::trueMatrix(rotation, inputSize.width(), inputSize.height())
                .mapRect(QRect(QPoint(), inputSize));
            width = extent.width();
            height = extent.height();
        }
        if (!bounded(width, height))
            return fail(QStringLiteral("Filter dimensions are empty or exceed the 512 MiB frame budget."));
    }
    return true;
}
