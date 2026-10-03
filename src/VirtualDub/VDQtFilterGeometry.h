// Shared, allocation-free geometry planning for the existing built-in filters.
// The caller first checks typed parameter limits with VDQtValidateFilter(). This
// helper then checks calculated sizes before any rounding, int cast or QImage
// allocation. Preview, allocation validation and the filter table must use the
// same plan: a UI-only estimate is not an authoritative frame-size contract.
#ifndef VDQTFILTERGEOMETRY_H
#define VDQTFILTERGEOMETRY_H

#include "VDQtFilterSystem.h"

#include <QRect>
#include <QTransform>
#include <algorithm>
#include <cmath>

struct VDQtFilterGeometry {
    QRect inputCrop;             // Reserved script clip, applied before the filter.
    QSize inputSize;             // Size after inputCrop.
    QSize intermediateSize;      // Resized/rotated image, before framing.
    QSize outputSize;
    QRect outputCrop;            // Center crop or ordinary Crop filter.
    QPoint imageOffset;          // Placement in a framed Resize/Canvas output.
    bool outputKnown = true;    // Plug-ins and conditional geometry need a frame.
};

inline bool VDQtComputeFilterGeometry(const VDFilterInstance& filter, QSize source,
                                     int bytesPerPixel, VDQtFilterGeometry *geometry,
                                     QString *error = nullptr, bool resolveAbsoluteResizeControls = false) {
    const auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (error) error->clear();
    if (!geometry || source.isEmpty() || bytesPerPixel <= 0)
        return fail(QStringLiteral("There is no valid input geometry to filter."));
    VDQtFilterGeometry plan;
    plan.inputCrop = QRect(QPoint(), source);
    plan.inputSize = plan.intermediateSize = plan.outputSize = source;
    if (!filter.enabled) {
        *geometry = plan;
        return true;
    }

    constexpr long double maximumBytes = 512.0L * 1024 * 1024;
    const auto size = [&](long double width, long double height, QSize *result) {
        if (!std::isfinite(width) || !std::isfinite(height)
            || width <= 0 || height <= 0 || width > 32768 || height > 32768)
            return false;
        const long double roundedWidth = std::max(1.0L, std::round(width));
        const long double roundedHeight = std::max(1.0L, std::round(height));
        if (roundedWidth * roundedHeight * bytesPerPixel > maximumBytes) return false;
        *result = QSize(static_cast<int>(roundedWidth), static_cast<int>(roundedHeight));
        return true;
    };
    const auto parameter = [&](const char *key, double fallback) {
        return static_cast<long double>(filter.params.value(QLatin1String(key), fallback));
    };
    const long double left = parameter("_sylia.clip.left", 0);
    const long double top = parameter("_sylia.clip.top", 0);
    const long double right = parameter("_sylia.clip.right", 0);
    const long double bottom = parameter("_sylia.clip.bottom", 0);
    if (!size(source.width() - left - right, source.height() - top - bottom, &plan.inputSize))
        return fail(QStringLiteral("Input clipping is empty or exceeds the frame allocation budget."));
    // Typed validation guarantees integer clip offsets in the 0..32768 range.
    plan.inputCrop = QRect(static_cast<int>(left), static_cast<int>(top),
                           plan.inputSize.width(), plan.inputSize.height());
    plan.intermediateSize = plan.outputSize = plan.inputSize;

    if (filter.type == VDFilterType::ConvertFormat && parameter("format", 0) == 2)
        bytesPerPixel = 8;
    if (filter.type == VDFilterType::Resize) {
        long double width = parameter("width", plan.inputSize.width());
        long double height = parameter("height", plan.inputSize.height());
        if (parameter("sizeMode", 0) == 1 || resolveAbsoluteResizeControls) {
            if (parameter("sizeMode", 0) == 1) {
                width = plan.inputSize.width() * parameter("relW", 100) / 100;
                height = plan.inputSize.height() * parameter("relH", 100) / 100;
            }
            // Absolute width/height in existing projects are already resolved
            // by the dialog. Only relative values must be resolved again for
            // the current incoming stage size; otherwise aspect/alignment is
            // silently discarded when an upstream size or source changes.
            // The Qt dialog resolves width first and derives height from that
            // rounded width when aspect is constrained. Match its controls,
            // rather than introducing a second half-pixel rounding decision.
            width = std::max(1.0L, std::round(width));
            const long double aspectMode = parameter("aspectMode", 0);
            if (aspectMode == 1)
                height = width * plan.inputSize.height() / plan.inputSize.width();
            else if (aspectMode == 2)
                height = width * parameter("aspectH", 3) / parameter("aspectW", 4);
            if (!size(width, height, &plan.intermediateSize))
                return fail(QStringLiteral("Resized intermediate exceeds the frame dimension/memory budget."));
            const int alignment = static_cast<int>(parameter("codecAdjust", 0));
            if (alignment > 1) {
                width = std::max(alignment, plan.intermediateSize.width() / alignment * alignment);
                height = std::max(alignment, plan.intermediateSize.height() / alignment * alignment);
            }
        }
        if (!size(width, height, &plan.intermediateSize))
            return fail(QStringLiteral("Resized intermediate exceeds the frame dimension/memory budget."));
        width = plan.intermediateSize.width();
        height = plan.intermediateSize.height();
        plan.outputSize = plan.intermediateSize;
        const long double framing = parameter("framingMode", 0);
        if (framing == 1) {
            width = parameter("frameW", width);
            height = parameter("frameH", height);
        } else if (framing == 2 || framing == 3) {
            const long double aspect = parameter("frameAspectW", 4) / parameter("frameAspectH", 3);
            const long double currentAspect = width / height;
            if ((currentAspect > aspect) == (framing == 2)) width = height * aspect;
            else height = width / aspect;
        }
        if (!size(width, height, &plan.outputSize))
            return fail(QStringLiteral("Framed resize exceeds the frame dimension/memory budget."));
        if (framing == 2) {
            plan.outputCrop = QRect((plan.intermediateSize.width() - plan.outputSize.width()) / 2,
                                    (plan.intermediateSize.height() - plan.outputSize.height()) / 2,
                                    plan.outputSize.width(), plan.outputSize.height());
        } else {
            plan.imageOffset = QPoint((plan.outputSize.width() - plan.intermediateSize.width()) / 2,
                                      (plan.outputSize.height() - plan.intermediateSize.height()) / 2);
        }
    } else if (filter.type == VDFilterType::Canvas || filter.type == VDFilterType::WarpResize) {
        long double width = parameter("width", plan.inputSize.width());
        long double height = parameter("height", plan.inputSize.height());
        if (width == 0) width = plan.inputSize.width();
        if (height == 0) height = plan.inputSize.height();
        if (!size(width, height, &plan.outputSize))
            return fail(QStringLiteral("Canvas/warp dimensions exceed the frame allocation budget."));
        plan.imageOffset = QPoint(static_cast<int>(parameter("x", 0)), static_cast<int>(parameter("y", 0)));
        if (filter.type == VDFilterType::WarpResize) plan.intermediateSize = plan.outputSize;
    } else if (filter.type == VDFilterType::Crop) {
        const long double cropLeft = parameter("left", 0), cropTop = parameter("top", 0);
        if (!size(plan.inputSize.width() - cropLeft - parameter("right", 0),
                  plan.inputSize.height() - cropTop - parameter("bottom", 0), &plan.outputSize))
            return fail(QStringLiteral("Crop removes the whole image or exceeds the allocation budget."));
        plan.outputCrop = QRect(static_cast<int>(cropLeft), static_cast<int>(cropTop),
                                plan.outputSize.width(), plan.outputSize.height());
    } else if (filter.type == VDFilterType::Reduce2 || filter.type == VDFilterType::Reduce2HQ) {
        plan.outputSize = QSize(std::max(1, plan.inputSize.width() / 2),
                                std::max(1, plan.inputSize.height() / 2));
        plan.intermediateSize = plan.outputSize;
    } else if (filter.type == VDFilterType::Rotate || filter.type == VDFilterType::Rotate2) {
        long double angle = parameter("angle", 0);
        if (filter.type == VDFilterType::Rotate) {
            const long double mode = parameter("mode", 0);
            angle = mode == 0 ? 270 : mode == 1 ? 90 : 180;
        }
        QTransform rotation;
        rotation.rotate(static_cast<double>(angle));
        const QRect extent = QImage::trueMatrix(rotation, plan.inputSize.width(), plan.inputSize.height())
            .mapRect(QRect(QPoint(), plan.inputSize));
        if (!size(extent.width(), extent.height(), &plan.intermediateSize))
            return fail(QStringLiteral("Rotated intermediate exceeds the frame allocation budget."));
        plan.outputSize = filter.type == VDFilterType::Rotate2 && parameter("expand", 1) <= 0.5
            ? plan.inputSize : plan.intermediateSize;
    } else if (filter.type == VDFilterType::Plugin) {
        // Its negotiate/process contract, not the input dimensions, determines
        // output size. Do not advertise an invented value for later table rows.
        plan.outputKnown = false;
    }
    if (!size(plan.outputSize.width(), plan.outputSize.height(), &plan.outputSize))
        return fail(QStringLiteral("Filter dimensions exceed the frame allocation budget."));
    if (plan.outputSize != source && filter.params.value("_sylia.range.end", -1) >= 0)
        plan.outputKnown = false;
    if (plan.outputSize != plan.inputSize && filter.params.value("_sylia.opacity.count", 0) > 0)
        plan.outputKnown = false;
    *geometry = plan;
    return true;
}

#endif // VDQTFILTERGEOMETRY_H
