// QImage-based built-in and VDX plug-in filter pipeline. Catalog construction
// and default parameters precede the hot processing loop; expensive filters use
// cached lookup tables/assets and QtConcurrent row bands where that is safe.
// Temporal state is per pipeline instance and is reset at discontinuities.
#include "VDQtFilterSystem.h"
#include "VDQtPluginHost.h"
#include "VDQtFilterValidation.h"
#include "VDQtFilterGeometry.h"
#include "VDQtImageResampler.h"
#include "VDQtVideoAspect.h"
#include <QTransform>
#include <QUuid>
#include <QRgba64>
#include <QFuture>
#include <QPainter>
#include <QFont>
#include <QPolygonF>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <new>
#include <utility>
#include <QDebug>
#include <QSet>
#include <QFileInfo>
#include <QDateTime>
#include <QImageReader>

namespace {

constexpr int kMaxSequencedBobFilters = 6;
constexpr qint64 kParallelFilterPixelThreshold = 256 * 1024;
constexpr qsizetype kMaximumAssetBytes = qsizetype{64} * 1024 * 1024;
constexpr qsizetype kMaximumAssetEntries = 64;
constexpr int kBlurColumnBandWidth = 64;

// QImage's copy-on-write allocation reports failure with a null pointer rather
// than an exception. Centralize the check at each real mutation, before row
// tasks or painters can touch that pointer. Do not pre-detach every stage:
// geometry-only/no-op stages should still hand off shared immutable pixels.
uchar *checkedImageBits(QImage& image) {
    uchar *bits = image.bits();
    if (!bits) throw std::bad_alloc();
    return bits;
}

// Split independent rows across the global Qt thread pool only when the image
// is large enough to repay task scheduling. Callers must capture disjoint output
// rows and treat their input image as immutable. Detach destinations with bits()
// BEFORE submitting tasks: even scanLine() can detach a shared QImage and must
// never run concurrently on the same image object. Capture pointers/strides, not
// mutable QImage access. This function joins all tasks before owners can expire.
template <typename Function>
void parallelFor(int count, qint64 workItems, Function&& function) {
    const int idealThreads = std::max(1, QThread::idealThreadCount());
    if (count <= 1 || idealThreads <= 1
        || workItems < kParallelFilterPixelThreshold) {
        for (int index = 0; index < count; ++index) function(index);
        return;
    }
    const int taskCount = std::min(count, idealThreads);
    QList<QFuture<void>> futures;
    futures.reserve(taskCount);
    try {
        for (int task = 0; task < taskCount; ++task) {
            const int begin = count * task / taskCount;
            const int end = count * (task + 1) / taskCount;
            futures.append(QtConcurrent::run([begin, end, &function]() {
                for (int index = begin; index < end; ++index) function(index);
            }));
        }
        for (QFuture<void>& future : futures) future.waitForFinished();
    } catch (...) {
        // Submission can fail after earlier rows have started (for example,
        // allocation failure). Keep the callable and its image owners alive
        // until EVERY submitted task finishes, then propagate the first error.
        const auto failure = std::current_exception();
        for (QFuture<void>& future : futures) {
            try { future.waitForFinished(); } catch (...) {}
        }
        std::rethrow_exception(failure);
    }
}

template <typename Transform>
void transformRgbPixels(QImage& image, bool highPrecision, Transform&& transform) {
    const int width = image.width();
    const int height = image.height();
    uchar *bits = checkedImageBits(image);
    const int stride = image.bytesPerLine();
    if (highPrecision) {
        parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
            QRgba64 *row = reinterpret_cast<QRgba64 *>(
                bits + static_cast<qint64>(y) * stride);
            for (int x = 0; x < width; ++x) {
                double red = row[x].red() / 65535.0;
                double green = row[x].green() / 65535.0;
                double blue = row[x].blue() / 65535.0;
                transform(red, green, blue);
                row[x] = QRgba64::fromRgba64(
                    static_cast<quint16>(std::clamp(
                        std::llround(red * 65535.0), 0LL, 65535LL)),
                    static_cast<quint16>(std::clamp(
                        std::llround(green * 65535.0), 0LL, 65535LL)),
                    static_cast<quint16>(std::clamp(
                        std::llround(blue * 65535.0), 0LL, 65535LL)),
                    row[x].alpha());
            }
        });
        return;
    }
    const int bytesPerPixel = image.format() == QImage::Format_RGB888 ? 3 : 4;
    parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
        uchar *row = bits + static_cast<qint64>(y) * stride;
        for (int x = 0; x < width; ++x) {
            double red = row[x * bytesPerPixel] / 255.0;
            double green = row[x * bytesPerPixel + 1] / 255.0;
            double blue = row[x * bytesPerPixel + 2] / 255.0;
            transform(red, green, blue);
            row[x * bytesPerPixel] = static_cast<uchar>(std::clamp(
                std::llround(red * 255.0), 0LL, 255LL));
            row[x * bytesPerPixel + 1] = static_cast<uchar>(std::clamp(
                std::llround(green * 255.0), 0LL, 255LL));
            row[x * bytesPerPixel + 2] = static_cast<uchar>(std::clamp(
                std::llround(blue * 255.0), 0LL, 255LL));
        }
    });
}

bool applyChannelLut(QImage& image, bool highPrecision, const QByteArray& bytes) {
    const int maximum = highPrecision ? 65535 : 255;
    if (bytes.size() != (maximum + 1) * static_cast<qsizetype>(sizeof(quint16))) return false;
    const auto *table = reinterpret_cast<const quint16 *>(bytes.constData());
    uchar *bits = checkedImageBits(image);
    const qsizetype stride = image.bytesPerLine();
    const int width = image.width(), height = image.height();
    if (highPrecision) {
        parallelFor(height, static_cast<qint64>(width) * height, [=](int y) {
            auto *row = reinterpret_cast<QRgba64 *>(bits + y * stride);
            for (int x = 0; x < width; ++x) {
                const QRgba64 pixel = row[x];
                row[x] = QRgba64::fromRgba64(table[pixel.red()], table[pixel.green()],
                                           table[pixel.blue()], pixel.alpha());
            }
        });
    } else {
        const int bpp = image.format() == QImage::Format_RGB888 ? 3 : 4;
        parallelFor(height, static_cast<qint64>(width) * height, [=](int y) {
            uchar *row = bits + y * stride;
            for (int x = 0; x < width; ++x)
                for (int channel = 0; channel < 3; ++channel)
                    row[x * bpp + channel] = static_cast<uchar>(table[row[x * bpp + channel]]);
        });
    }
    return true;
}

quint16 rgba64Channel(const QRgba64& pixel, int channel) {
    switch (channel) {
    case 0: return pixel.red();
    case 1: return pixel.green();
    default: return pixel.blue();
    }
}

void setRgba64Channel(QRgba64& pixel, int channel, quint16 value) {
    switch (channel) {
    case 0: pixel.setRed(value); break;
    case 1: pixel.setGreen(value); break;
    default: pixel.setBlue(value); break;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Catalog and active-chain configuration
// ---------------------------------------------------------------------------

VDQtFilterSystem::VDQtFilterSystem()
    : mRuntimeNamespace(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    // Establish singleton destruction order: the host must outlive pipelines.
    VDQtPluginHost::instance();
}

VDQtFilterSystem::~VDQtFilterSystem() { forgetRuntimeInstances(); }

QString VDQtFilterSystem::runtimeInstanceId(const QString& filterId) const {
    return mRuntimeNamespace + QLatin1Char('/') + filterId;
}

void VDQtFilterSystem::forgetRuntimeInstances() {
    for (const VDFilterInstance& filter : std::as_const(mActiveChain)) {
        if (filter.type == VDFilterType::Plugin)
            VDQtPluginHost::instance().forgetInstance(runtimeInstanceId(filter.id));
    }
}

VDQtFilterSystem::CacheStatistics VDQtFilterSystem::cacheStatistics() const {
    CacheStatistics result;
    result.sixAxisEntries = mSixAxisLutCache.size();
    result.channelLutEntries = mChannelLutCache.size();
    for (const auto& table : std::as_const(mChannelLutCache)) result.channelLutBytes += table.size();
    result.assetEntries = mAssetCache.size();
    for (const auto& asset : std::as_const(mAssetCache)) result.assetBytes += asset.image.sizeInBytes();
    return result;
}

void VDQtFilterSystem::clearFilters() {
    // A persistent-chain change invalidates plugin instances and every cache;
    // those objects may contain state tied to an entry that no longer exists.
    forgetRuntimeInstances();
    mActiveChain.clear();
    mSixAxisLutCache.clear();
    mChannelLutCache.clear();
    mAssetCache.clear();
    mTemporalStates.clear();
}

QList<VDFilterInstance> VDQtFilterSystem::normalizeChainIds(QList<VDFilterInstance> chain) {
    QSet<QString> used;
    for (auto& filter : chain) {
        if (filter.id.trimmed().isEmpty() || used.contains(filter.id)) {
            do {
                filter.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            } while (used.contains(filter.id));
        }
        used.insert(filter.id);
    }
    return chain;
}

void VDQtFilterSystem::replaceActiveChain(const QList<VDFilterInstance>& chain) {
    forgetRuntimeInstances();
    mActiveChain = normalizeChainIds(chain);
    mSixAxisLutCache.clear();
    mChannelLutCache.clear();
    mAssetCache.clear();
    mTemporalStates.clear();
}

void VDQtFilterSystem::replaceActiveChainTransient(
    const QList<VDFilterInstance>& chain) {
    // Worker/export copies use the same filter descriptions but independent
    // runtime state. Asset cache entries are retained here because changing a
    // numeric preview parameter should not reload unchanged logo files.
    forgetRuntimeInstances();
    mActiveChain = normalizeChainIds(chain);
    mTemporalStates.clear();
}

VDQtFilterSystem& VDQtFilterSystem::instance() {
    static VDQtFilterSystem sys;
    return sys;
}

QList<VDQtFilterSystem::FilterInfo> VDQtFilterSystem::getAvailableFilters() const {
    // Built-ins have stable enum identities. Plugin records are discovered at
    // runtime and carry pluginId because their display names are not guaranteed
    // unique or stable enough for project serialization.
    QList<FilterInfo> filters = {
        { VDFilterType::SixAxis, "6-axis color correction", "6-axis hue, saturation, and color balance correction." },
        { VDFilterType::BobDoubler, "bob doubler", "Upsamples an interlaced video to double frame rate." },
        { VDFilterType::Blur, "box blur", "Performs a fast box, triangle, or cubic blur." },
        { VDFilterType::BrightnessContrast, "brightness/contrast", "Adjust color brightness and contrast." },
        { VDFilterType::FlipHorizontal, "Flip Horizontal", "Mirror video frame horizontally." },
        { VDFilterType::FlipVertical, "Flip Vertical", "Flip video frame upside down." },
        { VDFilterType::Grayscale, "Grayscale / Desaturate", "Convert color frame to monochrome grayscale." },
        { VDFilterType::InvertColor, "Invert Color", "Invert RGB color channels." },
        { VDFilterType::Resize, "Resize / Rescale", "Adjust frame dimensions (Width, Height, Scaling Mode)." },
        { VDFilterType::Rotate, "Rotate", "Rotate frame by 90, 180, or 270 degrees." },
        { VDFilterType::Sharpen, "sharpen", "Enhance contrast between adjacent elements in an image." }
        ,{ VDFilterType::Deinterlace, "deinterlace", "Blend or interpolate interlaced scan lines." }
        ,{ VDFilterType::Emboss, "emboss", "Create a directional embossed relief image." }
        ,{ VDFilterType::FieldSwap, "field swap", "Swap adjacent even and odd scan lines." }
        ,{ VDFilterType::HSVAdjust, "HSV adjust", "Adjust hue, saturation, and value." }
        ,{ VDFilterType::Levels, "levels", "Set input/output levels and gamma." }
        ,{ VDFilterType::Threshold, "threshold", "Convert luma to a two-level image." }
        ,{ VDFilterType::Posterize, "posterize", "Reduce the number of color levels." }
        ,{ VDFilterType::Gamma, "gamma", "Apply a gamma transfer curve." }
        ,{ VDFilterType::Smoother, "smoother", "Reduce fine image noise with a spatial smoother." }
        ,{ VDFilterType::Crop, "crop", "Crop pixels from the frame edges." }
        ,{ VDFilterType::ChromaShift, "chroma shift", "Correct horizontal or vertical chroma displacement." }
        ,{ VDFilterType::Pixelate, "pixelate", "Replace blocks with their average color." }
        ,{ VDFilterType::Fill, "fill", "Fill the whole frame or a selected rectangle with a color." }
        ,{ VDFilterType::Canvas, "canvas", "Place the source frame on a larger or smaller canvas." }
        ,{ VDFilterType::Curves, "curves", "Adjust black point, white point, and tonal curve." }
        ,{ VDFilterType::ChromaSmoother, "chroma smoother", "Reduce color noise while retaining luma detail." }
        ,{ VDFilterType::DrawText, "draw text", "Draw configured text over the video." }
        ,{ VDFilterType::DrawTime, "draw time", "Draw the current frame time over the video." }
        ,{ VDFilterType::FieldDelay, "field delay", "Delay one interlaced field by one frame." }
        ,{ VDFilterType::GammaCorrect, "gamma correct", "Convert between gamma-encoded and linear-light RGB." }
        ,{ VDFilterType::Interlace, "interlace", "Weave fields from consecutive frames." }
        ,{ VDFilterType::Interpolate, "interpolate", "Blend consecutive frames." }
        ,{ VDFilterType::InverseTelecine, "inverse telecine", "Reduce combing in telecined material." }
        ,{ VDFilterType::MotionBlur, "motion blur", "Blend adjacent frames to soften motion." }
        ,{ VDFilterType::NullTransform, "null transform", "Pass video through unchanged." }
        ,{ VDFilterType::Perspective, "perspective", "Apply a four-corner perspective transform." }
        ,{ VDFilterType::Reduce2, "2:1 reduction", "Reduce both dimensions by two." }
        ,{ VDFilterType::Reduce2HQ, "2:1 reduction (high quality)", "Reduce both dimensions by two with smooth resampling." }
        ,{ VDFilterType::Rotate2, "rotate2", "Rotate by an arbitrary angle." }
        ,{ VDFilterType::TemporalSmoother, "temporal smoother", "Reduce frame-to-frame noise without blurring strong motion." }
        ,{ VDFilterType::Television, "TV", "Simulate analog television luma/chroma bandwidth." }
        ,{ VDFilterType::WarpResize, "warp resize", "Resize with an edge-preserving smooth transform." }
        ,{ VDFilterType::WarpSharp, "warp sharp", "Tighten edges with a strong unsharp transform." }
        ,{ VDFilterType::Logo, "logo", "Overlay an image with adjustable position and opacity." }
        ,{ VDFilterType::ConvertFormat, "convert format", "Convert between the RGB storage formats supported by the native pipeline." }
    };
    for (const VDQtPluginFilterInfo& plugin
         : VDQtPluginHost::instance().videoFilters()) {
        FilterInfo info;
        info.type = VDFilterType::Plugin;
        info.name = plugin.name;
        info.description = plugin.description;
        if (!plugin.author.isEmpty())
            info.description += QStringLiteral("\nAuthor: %1").arg(plugin.author);
        info.description += QStringLiteral("\nNative module: %1")
            .arg(plugin.modulePath);
        info.pluginId = plugin.id;
        filters.append(info);
    }
    return filters;
}

bool VDQtFilterSystem::addPluginFilter(const QString& pluginId) {
    const auto plugins = VDQtPluginHost::instance().videoFilters();
    const auto found = std::find_if(
        plugins.cbegin(), plugins.cend(), [&](const VDQtPluginFilterInfo& info) {
            return info.id == pluginId;
        });
    if (found == plugins.cend()) return false;
    VDFilterInstance instance;
    instance.id = QUuid::createUuid().toString();
    instance.name = found->name;
    instance.type = VDFilterType::Plugin;
    instance.enabled = true;
    instance.pluginId = found->id;
    mActiveChain.append(instance);
    return true;
}

void VDQtFilterSystem::addFilter(VDFilterType type) {
    // Defaults define a harmless or conventional initial configuration and are
    // part of script/project compatibility. Dialogs edit these named values;
    // processFrameForPhase interprets the same keys later.
    VDFilterInstance inst;
    inst.id = QUuid::createUuid().toString();
    inst.type = type;
    inst.enabled = true;

    switch (type) {
    case VDFilterType::SixAxis:
        inst.name = "6-axis color correction";
        inst.params["intensity"] = 1.0;
        inst.params["red_green"] = 0.0;
        inst.params["yellow_blue"] = 0.0;
        inst.params["saturation"] = 1.0;
        inst.params["red"] = 1.0;
        inst.params["orange"] = 1.0;
        inst.params["lime"] = 1.0;
        inst.params["emerald"] = 1.0;
        inst.params["blue"] = 1.0;
        inst.params["purple"] = 1.0;
        break;
    case VDFilterType::BobDoubler:
        inst.name = "bob doubler";
        inst.params["field_order"] = 1; // 0: TFF, 1: BFF
        inst.params["mode"] = 0;        // 0: Bob, 1: ELA, 2: Adaptive ELA, 3: None-alternate, 4: None-double
        break;
    case VDFilterType::Resize:
        inst.name = "Resize / Rescale";
        inst.params["sizeMode"] = 1; // Relative %
        inst.params["relW"] = 100;
        inst.params["relH"] = 100;
        inst.params["absW"] = 1920;
        inst.params["absH"] = 1080;
        inst.params["aspectMode"] = 1; // Same as source
        inst.params["aspectW"] = 4;
        inst.params["aspectH"] = 3;
        inst.params["filterMode"] = 4; // Precise bicubic (A=-0.75)
        inst.params["interlaced"] = 0;
        inst.params["framingMode"] = 0;
        inst.params["codecAdjust"] = 0;
        inst.params["width"] = 1920;
        inst.params["height"] = 1080;
        break;
    case VDFilterType::Rotate:
        inst.name = "Rotate";
        inst.params["mode"] = 0; // Left by 90°
        inst.params["angle"] = 270;
        break;
    case VDFilterType::FlipHorizontal:
        inst.name = "Flip Horizontal";
        break;
    case VDFilterType::FlipVertical:
        inst.name = "Flip Vertical";
        break;
    case VDFilterType::BrightnessContrast:
        inst.name = "brightness/contrast";
        inst.params["bright"] = 0;
        inst.params["cont"] = 16;
        break;
    case VDFilterType::Grayscale:
        inst.name = "Grayscale";
        break;
    case VDFilterType::InvertColor:
        inst.name = "Invert Color";
        break;
    case VDFilterType::Blur:
        inst.name = "box blur";
        inst.params["width"] = 1;
        inst.params["power"] = 1;
        inst.params["radius"] = 1;
        break;
    case VDFilterType::Sharpen:
        inst.name = "sharpen";
        inst.params["amount"] = 16;
        break;
    case VDFilterType::Deinterlace:
        inst.name = "deinterlace";
        inst.params["mode"] = 0;
        break;
    case VDFilterType::Emboss:
        inst.name = "emboss";
        inst.params["strength"] = 1.0;
        break;
    case VDFilterType::FieldSwap:
        inst.name = "field swap";
        break;
    case VDFilterType::HSVAdjust:
        inst.name = "HSV adjust";
        inst.params["hueDegrees"] = 0.0;
        inst.params["saturation"] = 1.0;
        inst.params["value"] = 1.0;
        break;
    case VDFilterType::Levels:
        inst.name = "levels";
        inst.params["inputBlack"] = 0.0;
        inst.params["inputWhite"] = 255.0;
        inst.params["gamma"] = 1.0;
        inst.params["outputBlack"] = 0.0;
        inst.params["outputWhite"] = 255.0;
        break;
    case VDFilterType::Threshold:
        inst.name = "threshold";
        inst.params["threshold"] = 128.0;
        break;
    case VDFilterType::Posterize:
        inst.name = "posterize";
        inst.params["levels"] = 8.0;
        break;
    case VDFilterType::Gamma:
        inst.name = "gamma";
        inst.params["gamma"] = 1.0;
        break;
    case VDFilterType::Smoother:
        inst.name = "smoother";
        inst.params["amount"] = 0.5;
        break;
    case VDFilterType::Crop:
        inst.name = "crop";
        inst.params["left"] = 0.0;
        inst.params["top"] = 0.0;
        inst.params["right"] = 0.0;
        inst.params["bottom"] = 0.0;
        break;
    case VDFilterType::ChromaShift:
        inst.name = "chroma shift";
        inst.params["x"] = 0.0;
        inst.params["y"] = 0.0;
        break;
    case VDFilterType::Pixelate:
        inst.name = "pixelate";
        inst.params["blockSize"] = 8.0;
        break;
    case VDFilterType::Fill:
        inst.name = "fill";
        inst.params["red"] = 0; inst.params["green"] = 0;
        inst.params["blue"] = 0; inst.params["alpha"] = 1.0;
        inst.params["x"] = 0; inst.params["y"] = 0;
        inst.params["width"] = 0; inst.params["height"] = 0;
        break;
    case VDFilterType::Canvas:
        inst.name = "canvas";
        // Zero means "use the current frame dimension".  A newly added
        // canvas filter must be a no-op until the user chooses a size.
        inst.params["width"] = 0; inst.params["height"] = 0;
        inst.params["x"] = 0; inst.params["y"] = 0;
        inst.params["red"] = 0; inst.params["green"] = 0; inst.params["blue"] = 0;
        break;
    case VDFilterType::Curves:
        inst.name = "curves";
        inst.params["black"] = 0; inst.params["white"] = 255;
        inst.params["gamma"] = 1.0;
        break;
    case VDFilterType::ChromaSmoother:
        inst.name = "chroma smoother";
        inst.params["radius"] = 1;
        break;
    case VDFilterType::DrawText:
        inst.name = "draw text";
        inst.stringParams["text"] = QStringLiteral("Text");
        inst.params["x"] = 16; inst.params["y"] = 32;
        inst.params["size"] = 24; inst.params["red"] = 255;
        inst.params["green"] = 255; inst.params["blue"] = 255;
        inst.params["outline"] = 1;
        break;
    case VDFilterType::DrawTime:
        inst.name = "draw time";
        inst.params["x"] = 16; inst.params["y"] = 32;
        inst.params["size"] = 24;
        break;
    case VDFilterType::FieldDelay:
        inst.name = "field delay";
        inst.params["field"] = 1;
        break;
    case VDFilterType::GammaCorrect:
        inst.name = "gamma correct";
        inst.params["toLinear"] = 1;
        break;
    case VDFilterType::Interlace:
        inst.name = "interlace";
        inst.params["fieldOrder"] = 0;
        break;
    case VDFilterType::Interpolate:
        inst.name = "interpolate";
        inst.params["amount"] = 0.5;
        break;
    case VDFilterType::InverseTelecine:
        inst.name = "inverse telecine";
        inst.params["threshold"] = 12;
        break;
    case VDFilterType::MotionBlur:
        inst.name = "motion blur";
        inst.params["amount"] = 0.5;
        break;
    case VDFilterType::NullTransform:
        inst.name = "null transform";
        break;
    case VDFilterType::Perspective:
        inst.name = "perspective";
        inst.params["topLeftX"] = 0; inst.params["topLeftY"] = 0;
        inst.params["topRightX"] = 1; inst.params["topRightY"] = 0;
        inst.params["bottomRightX"] = 1; inst.params["bottomRightY"] = 1;
        inst.params["bottomLeftX"] = 0; inst.params["bottomLeftY"] = 1;
        break;
    case VDFilterType::Reduce2:
        inst.name = "2:1 reduction";
        break;
    case VDFilterType::Reduce2HQ:
        inst.name = "2:1 reduction (high quality)";
        break;
    case VDFilterType::Rotate2:
        inst.name = "rotate2";
        inst.params["angle"] = 0;
        inst.params["expand"] = 1;
        break;
    case VDFilterType::TemporalSmoother:
        inst.name = "temporal smoother";
        inst.params["strength"] = 4;
        inst.params["threshold"] = 12;
        break;
    case VDFilterType::Television:
        inst.name = "TV";
        inst.params["chromaBlur"] = 2;
        inst.params["scanline"] = 0.08;
        break;
    case VDFilterType::WarpResize:
        inst.name = "warp resize";
        inst.params["width"] = 0; inst.params["height"] = 0;
        break;
    case VDFilterType::WarpSharp:
        inst.name = "warp sharp";
        inst.params["depth"] = 8;
        inst.params["threshold"] = 16;
        break;
    case VDFilterType::Logo:
        inst.name = "logo";
        inst.stringParams["path"] = QString();
        inst.params["x"] = 0; inst.params["y"] = 0;
        inst.params["opacity"] = 1.0;
        break;
    case VDFilterType::ConvertFormat:
        inst.name = "convert format";
        inst.params["format"] = 0;
        break;
    case VDFilterType::Plugin:
        return;
    case VDFilterType::Count:
        return;
    }

    mActiveChain.append(inst);
}

void VDQtFilterSystem::removeFilter(int index) {
    if (index >= 0 && index < mActiveChain.size()) {
        VDQtPluginHost::instance().forgetInstance(runtimeInstanceId(mActiveChain.at(index).id));
        mActiveChain.removeAt(index);
    }
}

void VDQtFilterSystem::moveFilterUp(int index) {
    if (index > 0 && index < mActiveChain.size()) {
        mActiveChain.swapItemsAt(index, index - 1);
    }
}

void VDQtFilterSystem::moveFilterDown(int index) {
    if (index >= 0 && index < mActiveChain.size() - 1) {
        mActiveChain.swapItemsAt(index, index + 1);
    }
}

void VDQtFilterSystem::setFilterEnabled(int index, bool enabled) {
    if (index >= 0 && index < mActiveChain.size()) {
        mActiveChain[index].enabled = enabled;
        mTemporalStates.clear();
    }
}

void VDQtFilterSystem::updateFilterParams(int index, const QMap<QString, double>& params) {
    if (index >= 0 && index < mActiveChain.size()) {
        mActiveChain[index].params = params;
        // Lookup tables are immutable and keyed by ALL parameter values. Keep
        // recent keys for slider changes/restarts; insertion bounds the cache.
        mTemporalStates.clear();
    }
}

void VDQtFilterSystem::updateFilterStringParams(
    int index, const QMap<QString, QString>& stringParams) {
    if (index >= 0 && index < mActiveChain.size()) {
        mActiveChain[index].stringParams = stringParams;
        mAssetCache.clear();
        mTemporalStates.clear();
    }
}

void VDQtFilterSystem::resetRuntimeState() {
    mTemporalStates.clear();
    forgetRuntimeInstances();
}

QByteArray VDQtFilterSystem::channelLut(const VDFilterInstance& filter, bool highPrecision) {
    const int maximum = highPrecision ? 65535 : 255;
    const bool gammaCorrect = filter.type == VDFilterType::GammaCorrect;
    const double gamma = gammaCorrect ? 1 : std::clamp(filter.params.value("gamma", 1.0), 0.05, 20.0);
    const bool levels = filter.type == VDFilterType::Levels;
    const bool curves = filter.type == VDFilterType::Curves;
    const double black = levels || curves ? std::clamp(filter.params.value(
        levels ? "inputBlack" : "black", 0.0) / 255.0, 0.0, 1.0) : 0;
    const double white = levels || curves ? filter.params.value(
        levels ? "inputWhite" : "white", 255.0) / 255.0 : 1;
    const double outputBlack = levels ? std::clamp(filter.params.value("outputBlack", 0.0) / 255.0, 0.0, 1.0) : 0;
    const double outputWhite = levels ? std::clamp(filter.params.value("outputWhite", 255.0) / 255.0, outputBlack, 1.0) : 1;
    const bool toLinear = gammaCorrect && filter.params.value("toLinear", 1) > 0.5;
    QString key = QStringLiteral("%1:%2:").arg(static_cast<int>(filter.type)).arg(maximum);
    // Key only values that affect this scalar transform. Opacity/range/clip
    // metadata is applied separately and must not trigger a table rebuild.
    for (double value : {gamma, black, white, outputBlack, outputWhite, toLinear ? 1.0 : 0.0})
        key += QString::number(value, 'g', 17) + QLatin1Char(';');
    QByteArray bytes = mChannelLutCache.value(key);
    if (!bytes.isEmpty()) return bytes;
    bytes.resize((maximum + 1) * static_cast<qsizetype>(sizeof(quint16)));
    auto *table = reinterpret_cast<quint16 *>(bytes.data());
    if (!table) return {};
    for (int channel = 0; channel <= maximum; ++channel) {
        const double value = channel / static_cast<double>(maximum);
        double adjusted;
        if (filter.type == VDFilterType::GammaCorrect) {
            adjusted = toLinear
                ? value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4)
                : value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
        } else {
            const double normalized = std::clamp((value - black) / (white - black), 0.0, 1.0);
            adjusted = outputBlack + (outputWhite - outputBlack) * std::pow(normalized, 1.0 / gamma);
        }
        table[channel] = static_cast<quint16>(std::clamp(std::llround(adjusted * maximum),
                                                       0LL, static_cast<long long>(maximum)));
    }
    if (mChannelLutCache.size() >= 8) mChannelLutCache.erase(mChannelLutCache.begin());
    mChannelLutCache.insert(key, bytes);
    return bytes;
}

// ---------------------------------------------------------------------------
// Frame processing and temporal-rate expansion
// ---------------------------------------------------------------------------

QImage VDQtFilterSystem::processFrame(const QImage& inputFrame) {
    // The historical API can return only one image. Use the first field phase
    // so preview remains deterministic; rate-aware pipelines must call
    // processFrameSequence() and consume every returned frame.
    return processFrame(inputFrame, {});
}

QImage VDQtFilterSystem::processFrame(
    const QImage& inputFrame, const VDFilterFrameContext& context) {
    QList<QImage> outputs;
    return processFrameSequence(inputFrame, outputs, context) && !outputs.isEmpty()
        ? outputs.first() : QImage();
}

QImage VDQtFilterSystem::failProcessing(const QString& message, const VDFilterInstance *filter) {
    mProcessingError = {filter ? filter->id : QString(),
                        filter ? filter->name : QString(), message};
    return {};
}

VDFilterTimingInfo VDQtFilterSystem::getTimingInfo() const {
    int bobFilters = 0;
    for (const auto& filter : mActiveChain) {
        if (filter.enabled && filter.type == VDFilterType::BobDoubler)
            ++bobFilters;
    }

    // Each bob filter doubles phases; cap the exponential expansion before a
    // malformed project can request an impractical number of output images.
    if (bobFilters > kMaxSequencedBobFilters)
        return { 0, false };

    return { 1 << bobFilters, true };
}

bool VDQtFilterSystem::processFrameSequence(const QImage& inputFrame, QList<QImage>& outputFrames) {
    return processFrameSequence(inputFrame, outputFrames, {});
}

bool VDQtFilterSystem::processFrameSequence(
    const QImage& inputFrame, QList<QImage>& outputFrames,
    const VDFilterFrameContext& context) try {
    // The input reference may name an image in outputFrames itself. Keep its
    // value alive before clearing that container (an implicit, not pixel copy).
    QImage stableInput = inputFrame;
    outputFrames.clear();
    mProcessingError = {};

    if (stableInput.isNull()) {
        failProcessing(QStringLiteral("There is no input frame to filter."));
        return false;
    }

    const VDFilterTimingInfo timing = getTimingInfo();
    if (!timing.sequenceSupported) {
        failProcessing(QStringLiteral("The configured temporal output sequence is not supported."));
        return false;
    }

    // Bound retained logical image bytes (including temporal history), not just
    // the phase count. Conservative accounting also covers shared duplicates
    // that a later stage may detach. Caller-owned images/kernel scratch are
    // outside this pipeline budget and retain normal allocation-error handling.
    constexpr qint64 sequenceBudget = qint64{512} * 1024 * 1024;
    const auto failSequence = [&](const QString& message, const VDFilterInstance *filter = nullptr) {
        if (mProcessingError.message.isEmpty()) failProcessing(message, filter);
        outputFrames.clear();
        resetRuntimeState();
        return false;
    };
    const auto historyBytes = [this] {
        qint64 bytes = 0;
        for (const auto& state : std::as_const(mTemporalStates)) bytes += state.previousFrame.sizeInBytes();
        return bytes;
    };
    const double duration = context.inputDurationSeconds > 0 && std::isfinite(context.inputDurationSeconds)
        ? context.inputDurationSeconds : context.frameRate > 0 && std::isfinite(context.frameRate)
        ? 1.0 / context.frameRate : 0.0;
    QList<QImage> current{std::move(stableInput)};
    for (int filterIndex = 0; filterIndex < mActiveChain.size(); ++filterIndex) {
        const auto& filter = mActiveChain.at(filterIndex);
        if (!filter.enabled) continue;
        const int inputPhases = current.size();
        if (context.frameNumber >= 0 && context.frameNumber
            > (std::numeric_limits<qint64>::max() - inputPhases + 1) / inputPhases)
            return failSequence(QStringLiteral("The expanded frame position is too large."), &filter);
        const int expansion = filter.type == VDFilterType::BobDoubler ? 2 : 1;
        QList<QImage> next;
        next.reserve(inputPhases * expansion);
        qint64 remainingBytes = 0, nextBytes = 0;
        for (const QImage& image : std::as_const(current)) remainingBytes += image.sizeInBytes();
        if (remainingBytes + historyBytes() > sequenceBudget)
            return failSequence(QStringLiteral("The filter sequence exceeds its 512 MiB image budget."), &filter);
        for (int inputPhase = 0; inputPhase < inputPhases; ++inputPhase) {
            VDFilterFrameContext stageContext = context;
            stageContext.frameNumber = context.frameNumber >= 0 ? context.frameNumber * inputPhases + inputPhase : -1;
            stageContext.frameRate = context.frameRate * inputPhases;
            stageContext.inputDurationSeconds = duration / inputPhases;
            stageContext.timestampSeconds = context.timestampSeconds >= 0
                ? context.timestampSeconds + inputPhase * stageContext.inputDurationSeconds : -1;
            stageContext.outputFrameNumber = stageContext.frameNumber;
            stageContext.outputPhase = inputPhase;
            stageContext.outputTimestampSeconds = stageContext.timestampSeconds;
            const qint64 consumedBytes = current.at(inputPhase).sizeInBytes();
            for (int phase = 0; phase < expansion; ++phase) {
                // Earlier phases borrow an immutable copy; the final phase
                // consumes ownership. Ordinary single-phase stages therefore
                // keep unique buffers without a full-image copy at every stage.
                QImage incoming = phase + 1 == expansion ? std::move(current[inputPhase]) : current.at(inputPhase);
                QImage output = processFilterForPhase(std::move(incoming), phase, stageContext, filterIndex);
                if (output.isNull()) return failSequence(QStringLiteral("The filter chain could not produce an output frame."), &filter);
                nextBytes += output.sizeInBytes();
                if (remainingBytes + nextBytes + historyBytes() > sequenceBudget)
                    return failSequence(QStringLiteral("The filter sequence exceeds its 512 MiB image budget."), &filter);
                next.append(std::move(output));
            }
            remainingBytes -= consumedBytes;
        }
        current.swap(next);
    }
    outputFrames.swap(current);
    return true;
} catch (const std::bad_alloc&) {
    outputFrames.clear();
    resetRuntimeState();
    failProcessing(QStringLiteral("Not enough memory to process the filter sequence."));
    return false;
}

QImage VDQtFilterSystem::processFilterForPhase(
    QImage inputFrame, quint64 bobPhaseMask,
    const VDFilterFrameContext& context, int filterIndex) try {
    if (inputFrame.isNull() || mActiveChain.isEmpty()) return inputFrame;
    const auto& filter = mActiveChain.at(filterIndex);
    if (!filter.enabled) return inputFrame;
    QString configurationError;
    if (!VDQtValidateFilter(filter, &configurationError, inputFrame.size(),
                            inputFrame.depth() > 32 ? 8 : 4))
        return failProcessing(configurationError, &filter);
    const qint64 rangeEnd = static_cast<qint64>(filter.params.value(
        QStringLiteral("_sylia.range.end"), -1.0));
    if (rangeEnd >= 0 && context.frameNumber >= 0) {
        const qint64 rangeStart = static_cast<qint64>(filter.params.value(
            QStringLiteral("_sylia.range.start"), 0.0));
        if (context.frameNumber < rangeStart || context.frameNumber >= rangeEnd) return inputFrame;
    }
    VDQtFilterGeometry geometry;
    if (!VDQtComputeFilterGeometry(filter, inputFrame.size(), inputFrame.depth() > 32 ? 8 : 4,
                                  &geometry, &configurationError))
        return failProcessing(configurationError, &filter);
    AVRational outputAspect = VDQtImageSampleAspectRatio(inputFrame);

    // Normalize only when needed, retaining unique intermediate storage. The
    // caller's original and borrowed earlier Bob phases still detach on write.
    QImage result = std::move(inputFrame);
    bool highPrecision = result.depth() > 32;
    const QImage::Format format = highPrecision ? QImage::Format_RGBA64
        : result.hasAlphaChannel() ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    if (result.format() != format) result = result.convertToFormat(format);
    if (result.isNull()) return {};

    // The chain is interpreted in order. Reserved _sylia.* parameters carry
    // script-only range, clipping, and opacity-curve metadata without widening
    // the public filter ABI or losing round-trip compatibility with VCF files.
    {
        if (geometry.inputCrop != QRect(QPoint(), result.size())) {
            result = result.copy(geometry.inputCrop);
            if (result.isNull()) return {};
        }

        // QImage transforms/painters may return ARGB32 or premultiplied layouts
        // without changing depth. Raw channel kernels require straight RGBA,
        // not merely "four bytes per pixel". Preserve high-depth precision.
        const QImage::Format workingFormat = result.depth() > 32
            ? QImage::Format_RGBA64
            : result.hasAlphaChannel() ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
        if (result.format() != workingFormat) {
            result = result.convertToFormat(workingFormat);
            if (result.isNull()) return {};
        }

        const int opacityPointCount = std::clamp(
            static_cast<int>(filter.params.value(
                QStringLiteral("_sylia.opacity.count"), 0.0)), 0, 4096);
        const QImage opacitySource = opacityPointCount > 0 ? result : QImage();

        // A plug-in or format-conversion filter may change the working image
        // depth.  Re-evaluate it for every stage so the following filter uses
        // the actual pixel layout instead of the input frame's layout.
        highPrecision = result.depth() > 32;

        // Each case must leave result as a detached image. Geometry-changing
        // filters replace it; in-place filters write only after detachment.
        switch (filter.type) {
        case VDFilterType::Plugin: {
            QImage pluginResult;
            QString errorMessage;
            if (!VDQtPluginHost::instance().processVideoFilter(
                    filter.pluginId, runtimeInstanceId(filter.id), filter.pluginConfiguration,
                    result, &pluginResult, &errorMessage, &context)) {
                return failProcessing(errorMessage.isEmpty()
                    ? QStringLiteral("The native plugin could not process this frame.") : errorMessage, &filter);
            }
            // Native filters may negotiate a new pixel aspect ratio. Keep it
            // as this stage's output metadata rather than restoring the input
            // ratio in the common geometry/opacity tail below.
            outputAspect = VDQtImageSampleAspectRatio(pluginResult);
            result = pluginResult.convertToFormat(QImage::Format_RGBA8888);
            if (result.isNull())
                return failProcessing(QStringLiteral("The native plugin returned no valid image."), &filter);
            break;
        }
        case VDFilterType::Fill: {
            const int x = static_cast<int>(filter.params.value("x", 0));
            const int y = static_cast<int>(filter.params.value("y", 0));
            int width = static_cast<int>(filter.params.value("width", 0));
            int height = static_cast<int>(filter.params.value("height", 0));
            if (width <= 0) width = result.width() - x;
            if (height <= 0) height = result.height() - y;
            QColor color(
                std::clamp(static_cast<int>(filter.params.value("red", 0)), 0, 255),
                std::clamp(static_cast<int>(filter.params.value("green", 0)), 0, 255),
                std::clamp(static_cast<int>(filter.params.value("blue", 0)), 0, 255));
            color.setAlphaF(std::clamp(filter.params.value("alpha", 1.0), 0.0, 1.0));
            checkedImageBits(result);
            QPainter painter;
            if (!painter.begin(&result)) throw std::bad_alloc();
            painter.fillRect(QRect(x, y, width, height), color);
            break;
        }
        case VDFilterType::Canvas: {
            QImage canvas(geometry.outputSize, result.format());
            checkedImageBits(canvas);
            canvas.fill(QColor(
                std::clamp(static_cast<int>(filter.params.value("red", 0)), 0, 255),
                std::clamp(static_cast<int>(filter.params.value("green", 0)), 0, 255),
                std::clamp(static_cast<int>(filter.params.value("blue", 0)), 0, 255)));
            QPainter painter;
            if (!painter.begin(&canvas)) throw std::bad_alloc();
            painter.drawImage(geometry.imageOffset, result);
            result = canvas;
            break;
        }
        case VDFilterType::Curves:
        case VDFilterType::Levels:
        case VDFilterType::Gamma:
        case VDFilterType::GammaCorrect: {
            // Every representable channel has its own exact old-formula entry.
            // Expensive pow/transfer work occurs once per parameter/depth key,
            // not six million times on each HD preview frame. Alpha is untouched.
            const QByteArray table = channelLut(filter, highPrecision);
            if (!applyChannelLut(result, highPrecision, table))
                return failProcessing(QStringLiteral("Not enough memory to apply the channel lookup table."), &filter);
            break;
        }
        case VDFilterType::ChromaSmoother: {
            const int radius = std::clamp(
                static_cast<int>(filter.params.value("radius", 1)), 1, 8);
            const QImage source = result;
            const int width = result.width();
            const int height = result.height();
            if (highPrecision) {
                const uchar *sourceBits = source.constBits();
                const int sourceStride = source.bytesPerLine();
                uchar *destinationBits = checkedImageBits(result);
                const int destinationStride = result.bytesPerLine();
                parallelFor(height, static_cast<qint64>(width) * height,
                    [&](int y) {
                    auto *destination = reinterpret_cast<QRgba64 *>(
                        destinationBits + static_cast<qint64>(y) * destinationStride);
                    for (int x = 0; x < width; ++x) {
                        quint64 red = 0, green = 0, blue = 0, luma = 0;
                        int count = 0;
                        for (int offset = -radius; offset <= radius; ++offset) {
                            const int sampleX = std::clamp(x + offset, 0, width - 1);
                            const auto *row = reinterpret_cast<const QRgba64 *>(
                                sourceBits + static_cast<qint64>(y) * sourceStride);
                            const QRgba64 sample = row[sampleX];
                            red += sample.red(); green += sample.green(); blue += sample.blue();
                            luma += (77ULL * sample.red() + 150ULL * sample.green()
                                     + 29ULL * sample.blue()) >> 8;
                            ++count;
                        }
                        const QRgba64 original = reinterpret_cast<const QRgba64 *>(
                            sourceBits + static_cast<qint64>(y) * sourceStride)[x];
                        const qint64 originalLuma =
                            (77LL * original.red() + 150LL * original.green()
                             + 29LL * original.blue()) >> 8;
                        const qint64 averageLuma = static_cast<qint64>(luma / count);
                        destination[x] = QRgba64::fromRgba64(
                            static_cast<quint16>(std::clamp<qint64>(
                                originalLuma + static_cast<qint64>(red / count) - averageLuma, 0, 65535)),
                            static_cast<quint16>(std::clamp<qint64>(
                                originalLuma + static_cast<qint64>(green / count) - averageLuma, 0, 65535)),
                            static_cast<quint16>(std::clamp<qint64>(
                                originalLuma + static_cast<qint64>(blue / count) - averageLuma, 0, 65535)),
                            original.alpha());
                    }
                });
                break;
            }
            const int bytesPerPixel = result.format() == QImage::Format_RGB888 ? 3 : 4;
            const uchar *sourceBits = source.constBits();
            const int sourceStride = source.bytesPerLine();
            uchar *destinationBits = checkedImageBits(result);
            const int destinationStride = result.bytesPerLine();
            parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
                const uchar *sourceRow = sourceBits + static_cast<qint64>(y) * sourceStride;
                uchar *destination = destinationBits + static_cast<qint64>(y) * destinationStride;
                for (int x = 0; x < width; ++x) {
                    int sums[3] = {}; int lumaSum = 0; int count = 0;
                    for (int offset = -radius; offset <= radius; ++offset) {
                        const int sampleX = std::clamp(x + offset, 0, width - 1);
                        const uchar *sample = sourceRow + sampleX * bytesPerPixel;
                        sums[0] += sample[0]; sums[1] += sample[1]; sums[2] += sample[2];
                        lumaSum += (77 * sample[0] + 150 * sample[1] + 29 * sample[2]) >> 8;
                        ++count;
                    }
                    const uchar *original = sourceRow + x * bytesPerPixel;
                    const int originalLuma =
                        (77 * original[0] + 150 * original[1] + 29 * original[2]) >> 8;
                    const int averageLuma = lumaSum / count;
                    for (int channel = 0; channel < 3; ++channel)
                        destination[x * bytesPerPixel + channel] = static_cast<uchar>(
                            std::clamp(originalLuma + sums[channel] / count - averageLuma,
                                       0, 255));
                }
            });
            break;
        }
        case VDFilterType::DrawText:
        case VDFilterType::DrawTime: {
            QString text = filter.type == VDFilterType::DrawText
                ? filter.stringParams.value("text", QStringLiteral("Text"))
                : QStringLiteral("%1  frame %2")
                    .arg(context.timestampSeconds >= 0.0
                             ? QString::number(context.timestampSeconds, 'f', 3)
                             : QStringLiteral("--:--.---"))
                    .arg(context.frameNumber >= 0
                             ? QString::number(context.frameNumber)
                             : QStringLiteral("?"));
            checkedImageBits(result);
            QPainter painter;
            if (!painter.begin(&result)) throw std::bad_alloc();
            painter.setRenderHint(QPainter::TextAntialiasing, true);
            QFont font = painter.font();
            font.setPixelSize(std::clamp(
                static_cast<int>(filter.params.value("size", 24)), 6, 512));
            painter.setFont(font);
            const QPoint position(
                static_cast<int>(filter.params.value("x", 16)),
                static_cast<int>(filter.params.value("y", 32)));
            if (filter.params.value("outline", 1) > 0.5) {
                painter.setPen(Qt::black);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (dx || dy) painter.drawText(position + QPoint(dx, dy), text);
            }
            painter.setPen(QColor(
                std::clamp(static_cast<int>(filter.params.value("red", 255)), 0, 255),
                std::clamp(static_cast<int>(filter.params.value("green", 255)), 0, 255),
                std::clamp(static_cast<int>(filter.params.value("blue", 255)), 0, 255)));
            painter.drawText(position, text);
            break;
        }
        case VDFilterType::FieldDelay:
        case VDFilterType::Interlace:
        case VDFilterType::Interpolate:
        case VDFilterType::MotionBlur:
        case VDFilterType::TemporalSmoother: {
            // Field weaving and adjacent-frame interpolation need the preceding
            // *incoming stage frame*. Feeding back an interpolated output turns
            // a two-frame blend into an unintended progressively fading trail.
            // Motion blur/smoother history intentionally remains output-based.
            const bool incomingHistory = filter.type == VDFilterType::FieldDelay
                || filter.type == VDFilterType::Interlace
                || filter.type == VDFilterType::Interpolate;
            const QImage incomingHistoryFrame = incomingHistory ? result : QImage();
            const QString stateKey = filter.id;
            TemporalState& temporal = mTemporalStates[stateKey];
            const bool sequential = !temporal.previousFrame.isNull()
                && (context.frameNumber < 0
                    || (context.frameNumber > 0
                        && temporal.lastFrameNumber == context.frameNumber - 1));
            const QImage previous = sequential
                ? temporal.previousFrame.convertToFormat(result.format()) : QImage();
            if (sequential && previous.isNull()) return {};
            if (!previous.isNull() && previous.size() == result.size()) {
                if (filter.type == VDFilterType::FieldDelay
                    || filter.type == VDFilterType::Interlace) {
                    const int delayedParity = static_cast<int>(filter.params.value(
                        filter.type == VDFilterType::FieldDelay ? "field" : "fieldOrder", 1)) & 1;
                    // incomingHistoryFrame deliberately shares this buffer for
                    // history. Detach once here, not via unchecked scanLine()
                    // on every row; allocation failure must abort the sequence.
                    uchar *destinationBits = checkedImageBits(result);
                    const qsizetype destinationStride = result.bytesPerLine();
                    const uchar *previousBits = previous.constBits();
                    const qsizetype previousStride = previous.bytesPerLine();
                    for (int y = delayedParity; y < result.height(); y += 2)
                        std::memcpy(destinationBits + y * destinationStride,
                                    previousBits + y * previousStride,
                                    static_cast<size_t>(std::min(
                                        destinationStride, previousStride)));
                } else if (filter.type == VDFilterType::Interpolate
                           || filter.type == VDFilterType::MotionBlur) {
                    const double opacity = std::clamp(
                        filter.params.value("amount", 0.5), 0.0, 1.0);
                    if (highPrecision) {
                        // Qt 6.4's AVX2 straight RGBA64 fetch also multiplies
                        // alpha by itself. Compose native 16-bit pixels here
                        // so alpha and rounding do not depend on Qt's SIMD path.
                        // Match QPainter's 8-bit coverage for this amount control.
                        if (opacity > 0.0) {
                            const quint32 coverage = (255 * static_cast<int>(opacity * 256)) >> 8;
                            const auto scale = [](quint32 value, quint32 alpha) -> quint16 {
                                const quint32 product = value * alpha;
                                return (product + (product >> 16) + 0x8000) >> 16;
                            };
                            uchar *destination = checkedImageBits(result);
                            const qsizetype stride = result.bytesPerLine();
                            const uchar *prior = previous.constBits();
                            const qsizetype priorStride = previous.bytesPerLine();
                            const int width = result.width();
                            parallelFor(result.height(), qint64(width) * result.height(), [=](int y) {
                                auto *row = reinterpret_cast<QRgba64*>(destination + y * stride);
                                const auto *source = reinterpret_cast<const QRgba64*>(prior + y * priorStride);
                                for (int x = 0; x < width; ++x) {
                                    const QRgba64 s = source[x].premultiplied();
                                    const QRgba64 d = row[x].premultiplied();
                                    const quint32 alpha = scale(s.alpha(), coverage * 257);
                                    row[x] = QRgba64::fromRgba64(
                                        scale(s.red(), coverage * 257) + scale(d.red(), 65535 - alpha),
                                        scale(s.green(), coverage * 257) + scale(d.green(), 65535 - alpha),
                                        scale(s.blue(), coverage * 257) + scale(d.blue(), 65535 - alpha),
                                        alpha + scale(d.alpha(), 65535 - alpha)).unpremultiplied();
                                }
                            });
                        }
                    } else {
                        checkedImageBits(result);
                        QPainter painter;
                        if (!painter.begin(&result)) throw std::bad_alloc();
                        painter.setOpacity(opacity);
                        painter.drawImage(0, 0, previous);
                    }
                } else {
                    const int threshold = std::clamp(
                        static_cast<int>(filter.params.value("threshold", 12)), 0, 255);
                    const double strength = std::clamp(
                        filter.params.value("strength", 4.0) / 8.0, 0.0, 1.0);
                    if (!highPrecision) {
                        const int bytesPerPixel = result.format() == QImage::Format_RGB888 ? 3 : 4;
                        const int width = result.width(), height = result.height();
                        const uchar *previousBits = previous.constBits();
                        const qsizetype previousStride = previous.bytesPerLine();
                        // The input, prior output, and opacity snapshot may all
                        // share storage. Detach once on the caller thread; the
                        // previous QImage keeps its read buffer alive until join.
                        uchar *destinationBits = checkedImageBits(result);
                        const qsizetype destinationStride = result.bytesPerLine();
                        parallelFor(height, static_cast<qint64>(width) * height,
                                    [=](int y) {
                            uchar *current = destinationBits + y * destinationStride;
                            const uchar *prior = previousBits + y * previousStride;
                            for (int x = 0; x < width; ++x) {
                                int maximumDifference = 0;
                                for (int channel = 0; channel < 3; ++channel)
                                    maximumDifference = std::max(maximumDifference,
                                        std::abs(current[x * bytesPerPixel + channel]
                                                 - prior[x * bytesPerPixel + channel]));
                                if (maximumDifference <= threshold) {
                                    for (int channel = 0; channel < 3; ++channel) {
                                        const int offset = x * bytesPerPixel + channel;
                                        current[offset] = static_cast<uchar>(std::lround(
                                            current[offset] * (1.0 - strength)
                                            + prior[offset] * strength));
                                    }
                                }
                            }
                        });
                    } else {
                        const int width = result.width(), height = result.height();
                        const uchar *previousBits = previous.constBits();
                        const qsizetype previousStride = previous.bytesPerLine();
                        uchar *destinationBits = checkedImageBits(result);
                        const qsizetype destinationStride = result.bytesPerLine();
                        // Use the same motion gate and strength as the 8-bit
                        // kernel, without quantizing RGB or blending alpha.
                        parallelFor(height, static_cast<qint64>(width) * height, [=](int y) {
                            auto *current = reinterpret_cast<QRgba64*>(
                                destinationBits + y * destinationStride);
                            const auto *prior = reinterpret_cast<const QRgba64*>(
                                previousBits + y * previousStride);
                            for (int x = 0; x < width; ++x) {
                                int maximumDifference = 0;
                                for (int channel = 0; channel < 3; ++channel)
                                    maximumDifference = std::max(maximumDifference,
                                        std::abs(int(rgba64Channel(current[x], channel))
                                                 - int(rgba64Channel(prior[x], channel))));
                                if (maximumDifference <= threshold * 257)
                                    for (int channel = 0; channel < 3; ++channel)
                                        setRgba64Channel(current[x], channel, std::lround(
                                            rgba64Channel(current[x], channel) * (1.0 - strength)
                                            + rgba64Channel(prior[x], channel) * strength));
                            }
                        });
                    }
                }
            }
            temporal.previousFrame = incomingHistory ? incomingHistoryFrame : result;
            temporal.lastFrameNumber = context.frameNumber >= 0
                ? context.frameNumber : temporal.lastFrameNumber < std::numeric_limits<qint64>::max()
                ? temporal.lastFrameNumber + 1 : temporal.lastFrameNumber;
            break;
        }
        case VDFilterType::InverseTelecine: {
            // A single-frame chain cannot change cadence here, but adaptive
            // vertical interpolation removes the visible combing that would
            // otherwise survive into preview and recompression.
            const QImage source = result;
            const int threshold = std::clamp(
                static_cast<int>(filter.params.value("threshold", 12)), 0, 255);
            if (!highPrecision) {
                const int bytesPerPixel = result.format() == QImage::Format_RGB888 ? 3 : 4;
                const int width = result.width(), height = result.height();
                const uchar *sourceBits = source.constBits();
                const qsizetype sourceStride = source.bytesPerLine();
                uchar *destinationBits = checkedImageBits(result);
                const qsizetype destinationStride = result.bytesPerLine();
                parallelFor(std::max(0, height - 2),
                            static_cast<qint64>(width) * height,
                            [=](int row) {
                    const int y = row + 1;
                    uchar *destination = destinationBits + y * destinationStride;
                    const uchar *above = sourceBits + (y - 1) * sourceStride;
                    const uchar *current = sourceBits + y * sourceStride;
                    const uchar *below = sourceBits + (y + 1) * sourceStride;
                    for (int x = 0; x < width; ++x) {
                        for (int channel = 0; channel < 3; ++channel) {
                            const int offset = x * bytesPerPixel + channel;
                            const int prediction = (above[offset] + below[offset] + 1) >> 1;
                            if (std::abs(current[offset] - prediction) > threshold)
                                destination[offset] = static_cast<uchar>(prediction);
                        }
                    }
                });
            } else {
                const int width = result.width(), height = result.height();
                const uchar *sourceBits = source.constBits();
                const qsizetype sourceStride = source.bytesPerLine();
                uchar *destinationBits = checkedImageBits(result);
                const qsizetype destinationStride = result.bytesPerLine();
                parallelFor(std::max(0, height - 2), static_cast<qint64>(width) * height, [=](int row) {
                    const int y = row + 1;
                    auto *destination = reinterpret_cast<QRgba64*>(destinationBits + y * destinationStride);
                    const auto *above = reinterpret_cast<const QRgba64*>(sourceBits + (y - 1) * sourceStride);
                    const auto *current = reinterpret_cast<const QRgba64*>(sourceBits + y * sourceStride);
                    const auto *below = reinterpret_cast<const QRgba64*>(sourceBits + (y + 1) * sourceStride);
                    for (int x = 0; x < width; ++x) {
                        for (int channel = 0; channel < 3; ++channel) {
                            const int prediction = (int(rgba64Channel(above[x], channel))
                                + rgba64Channel(below[x], channel) + 1) / 2;
                            if (std::abs(int(rgba64Channel(current[x], channel)) - prediction) > threshold * 257)
                                setRgba64Channel(destination[x], channel, prediction);
                        }
                    }
                });
            }
            break;
        }
        case VDFilterType::NullTransform:
            break;
        case VDFilterType::Perspective: {
            const qreal width = result.width();
            const qreal height = result.height();
            const QPolygonF sourceQuad({QPointF(0, 0), QPointF(width, 0),
                                        QPointF(width, height), QPointF(0, height)});
            const QPolygonF destinationQuad({
                QPointF(filter.params.value("topLeftX", 0) * width,
                        filter.params.value("topLeftY", 0) * height),
                QPointF(filter.params.value("topRightX", 1) * width,
                        filter.params.value("topRightY", 0) * height),
                QPointF(filter.params.value("bottomRightX", 1) * width,
                        filter.params.value("bottomRightY", 1) * height),
                QPointF(filter.params.value("bottomLeftX", 0) * width,
                        filter.params.value("bottomLeftY", 1) * height)});
            QTransform transform;
            if (QTransform::quadToQuad(sourceQuad, destinationQuad, transform)) {
                QImage transformed(result.size(), result.format());
                checkedImageBits(transformed);
                transformed.fill(Qt::black);
                QPainter painter;
                if (!painter.begin(&transformed)) throw std::bad_alloc();
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.setTransform(transform);
                painter.drawImage(0, 0, result);
                result = transformed;
            }
            break;
        }
        case VDFilterType::Reduce2:
        case VDFilterType::Reduce2HQ:
            result = result.scaled(
                geometry.outputSize, Qt::IgnoreAspectRatio,
                filter.type == VDFilterType::Reduce2
                    ? Qt::FastTransformation : Qt::SmoothTransformation);
            break;
        case VDFilterType::Rotate2: {
            const QSize originalSize = result.size();
            QTransform transform;
            transform.rotate(filter.params.value("angle", 0));
            result = result.transformed(transform, Qt::SmoothTransformation);
            if (result.isNull()) throw std::bad_alloc();
            if (filter.params.value("expand", 1) <= 0.5
                && result.size() != originalSize) {
                QImage cropped(originalSize, result.format());
                checkedImageBits(cropped);
                cropped.fill(Qt::black);
                QPainter painter;
                if (!painter.begin(&cropped)) throw std::bad_alloc();
                painter.drawImage((originalSize.width() - result.width()) / 2,
                                  (originalSize.height() - result.height()) / 2,
                                  result);
                result = cropped;
            }
            break;
        }
        case VDFilterType::Television: {
            const QImage source = result;
            const int radius = std::clamp(
                static_cast<int>(filter.params.value("chromaBlur", 2)), 0, 8);
            const double scanline = std::clamp(
                filter.params.value("scanline", 0.08), 0.0, 0.8);
            if (!highPrecision) {
                const int bytesPerPixel = result.format() == QImage::Format_RGB888 ? 3 : 4;
                const int width = result.width(), height = result.height();
                const uchar *sourceBits = source.constBits();
                const qsizetype sourceStride = source.bytesPerLine();
                uchar *destinationBits = checkedImageBits(result);
                const qsizetype destinationStride = result.bytesPerLine();
                parallelFor(height, static_cast<qint64>(width) * height,
                            [=](int y) {
                    uchar *destination = destinationBits + y * destinationStride;
                    const uchar *row = sourceBits + y * sourceStride;
                    for (int x = 0; x < width; ++x) {
                        int red = 0, green = 0, blue = 0, count = 0;
                        for (int offset = -radius; offset <= radius; ++offset) {
                            const int sx = std::clamp(x + offset, 0, width - 1);
                            red += row[sx * bytesPerPixel];
                            green += row[sx * bytesPerPixel + 1];
                            blue += row[sx * bytesPerPixel + 2];
                            ++count;
                        }
                        const int luma = (77 * row[x * bytesPerPixel]
                            + 150 * row[x * bytesPerPixel + 1]
                            + 29 * row[x * bytesPerPixel + 2]) >> 8;
                        const int averageLuma = (77 * red + 150 * green + 29 * blue)
                            / (256 * count);
                        const double lineScale = (y & 1) ? 1.0 - scanline : 1.0;
                        destination[x * bytesPerPixel] = static_cast<uchar>(std::clamp(
                            static_cast<int>((luma + red / count - averageLuma) * lineScale), 0, 255));
                        destination[x * bytesPerPixel + 1] = static_cast<uchar>(std::clamp(
                            static_cast<int>((luma + green / count - averageLuma) * lineScale), 0, 255));
                        destination[x * bytesPerPixel + 2] = static_cast<uchar>(std::clamp(
                            static_cast<int>((luma + blue / count - averageLuma) * lineScale), 0, 255));
                    }
                });
            } else {
                const int width = result.width(), height = result.height();
                const uchar *sourceBits = source.constBits();
                const qsizetype sourceStride = source.bytesPerLine();
                uchar *destinationBits = checkedImageBits(result);
                const qsizetype destinationStride = result.bytesPerLine();
                parallelFor(height, static_cast<qint64>(width) * height, [=](int y) {
                    auto *destination = reinterpret_cast<QRgba64*>(destinationBits + y * destinationStride);
                    const auto *row = reinterpret_cast<const QRgba64*>(sourceBits + y * sourceStride);
                    for (int x = 0; x < width; ++x) {
                        int totals[3] = {0, 0, 0};
                        const int count = radius * 2 + 1;
                        for (int offset = -radius; offset <= radius; ++offset)
                            for (int channel = 0; channel < 3; ++channel)
                                totals[channel] += rgba64Channel(row[std::clamp(x + offset, 0, width - 1)], channel);
                        const int luma = (77 * row[x].red() + 150 * row[x].green() + 29 * row[x].blue()) / 256;
                        const int averageLuma = (77 * totals[0] + 150 * totals[1] + 29 * totals[2]) / (256 * count);
                        const double lineScale = (y & 1) ? 1.0 - scanline : 1.0;
                        for (int channel = 0; channel < 3; ++channel)
                            setRgba64Channel(destination[x], channel, std::clamp(
                                int((luma + totals[channel] / count - averageLuma) * lineScale), 0, 65535));
                    }
                });
            }
            break;
        }
        case VDFilterType::WarpResize: {
            result = result.scaled(geometry.outputSize, Qt::IgnoreAspectRatio,
                                   Qt::SmoothTransformation);
            break;
        }
        case VDFilterType::WarpSharp: {
            const int amount = std::clamp(
                static_cast<int>(filter.params.value("depth", 8)), 0, 64);
            if (amount <= 0) break;
            const QImage source = result;
            if (!highPrecision) {
                const int bytesPerPixel = result.format() == QImage::Format_RGB888 ? 3 : 4;
                const int width = result.width(), height = result.height();
                const uchar *sourceBits = source.constBits();
                const qsizetype sourceStride = source.bytesPerLine();
                uchar *destinationBits = checkedImageBits(result);
                const qsizetype destinationStride = result.bytesPerLine();
                parallelFor(height, static_cast<qint64>(width) * height,
                            [=](int y) {
                    uchar *destination = destinationBits + y * destinationStride;
                    const uchar *current = sourceBits + y * sourceStride;
                    const uchar *above = sourceBits + std::max(0, y - 1) * sourceStride;
                    const uchar *below = sourceBits + std::min(height - 1, y + 1) * sourceStride;
                    for (int x = 0; x < width; ++x) {
                        const int left = std::max(0, x - 1);
                        const int right = std::min(width - 1, x + 1);
                        for (int channel = 0; channel < 3; ++channel) {
                            const int center = current[x * bytesPerPixel + channel];
                            const int average = (current[left * bytesPerPixel + channel]
                                + current[right * bytesPerPixel + channel]
                                + above[x * bytesPerPixel + channel]
                                + below[x * bytesPerPixel + channel] + 2) / 4;
                            destination[x * bytesPerPixel + channel] = static_cast<uchar>(
                                std::clamp(center + (center - average) * amount / 16, 0, 255));
                        }
                    }
                });
            } else {
                const int width = result.width(), height = result.height();
                const uchar *sourceBits = source.constBits();
                const qsizetype sourceStride = source.bytesPerLine();
                uchar *destinationBits = checkedImageBits(result);
                const qsizetype destinationStride = result.bytesPerLine();
                parallelFor(height, static_cast<qint64>(width) * height, [=](int y) {
                    auto *destination = reinterpret_cast<QRgba64*>(destinationBits + y * destinationStride);
                    const auto *current = reinterpret_cast<const QRgba64*>(sourceBits + y * sourceStride);
                    const auto *above = reinterpret_cast<const QRgba64*>(sourceBits + std::max(0, y - 1) * sourceStride);
                    const auto *below = reinterpret_cast<const QRgba64*>(sourceBits + std::min(height - 1, y + 1) * sourceStride);
                    for (int x = 0; x < width; ++x) {
                        const int left = std::max(0, x - 1), right = std::min(width - 1, x + 1);
                        for (int channel = 0; channel < 3; ++channel) {
                            const int center = rgba64Channel(current[x], channel);
                            const int average = (int(rgba64Channel(current[left], channel))
                                + rgba64Channel(current[right], channel) + rgba64Channel(above[x], channel)
                                + rgba64Channel(below[x], channel) + 2) / 4;
                            setRgba64Channel(destination[x], channel,
                                std::clamp(center + (center - average) * amount / 16, 0, 65535));
                        }
                    }
                });
            }
            break;
        }
        case VDFilterType::Logo: {
            const QString path = filter.stringParams.value("path");
            if (path.isEmpty())
                return failProcessing(QStringLiteral("Choose an image for the enabled logo filter."), &filter);
            const QFileInfo fileInfo(path);
            const QString cacheKey = fileInfo.absoluteFilePath();
            const QString canonical = fileInfo.canonicalFilePath();
            const qint64 fileSize = fileInfo.size();
            const qint64 modified = fileInfo.lastModified().toMSecsSinceEpoch();
            const auto asset = mAssetCache.constFind(cacheKey);
            QImage logo;
            if (fileInfo.isFile() && asset != mAssetCache.cend()
                && asset->canonicalPath == canonical && asset->fileSize == fileSize
                && asset->modifiedMs == modified) {
                logo = asset->image;
            } else {
                // The cache is reusable, not a substitute for the required
                // file. Drop stale/missing entries so errors and restored files
                // are observable without replacing the chain.
                mAssetCache.remove(cacheKey);
                QImageReader reader(cacheKey);
                const QSize size = reader.size();
                if (size.isValid() && (size.width() > 32768 || size.height() > 32768
                    || static_cast<long double>(size.width()) * size.height() * 8
                        > 512.0L * 1024 * 1024))
                    return failProcessing(QStringLiteral("The required logo exceeds the filter image allocation budget."), &filter);
                logo = reader.read();
                if (logo.isNull())
                    return failProcessing(QString("Could not load the required image: %1 (%2)")
                        .arg(path, reader.errorString()), &filter);
                // Larger valid images can be drawn, but are not retained. A
                // chain can visit many filenames over a session; cap bytes AND
                // entries instead of retaining each decoded bitmap forever.
                const qsizetype bytes = logo.sizeInBytes();
                if (bytes <= kMaximumAssetBytes) {
                    qsizetype retained = cacheStatistics().assetBytes;
                    while (!mAssetCache.isEmpty()
                        && (mAssetCache.size() >= kMaximumAssetEntries
                            || retained + bytes > kMaximumAssetBytes)) {
                        auto victim = mAssetCache.begin();
                        retained -= victim->image.sizeInBytes();
                        mAssetCache.erase(victim);
                    }
                    mAssetCache.insert(cacheKey, CachedAsset{logo, canonical, fileSize, modified});
                }
            }
            {
                checkedImageBits(result);
                QPainter painter;
                if (!painter.begin(&result)) throw std::bad_alloc();
                painter.setOpacity(std::clamp(
                    filter.params.value("opacity", 1.0), 0.0, 1.0));
                painter.drawImage(
                    static_cast<int>(filter.params.value("x", 0)),
                    static_cast<int>(filter.params.value("y", 0)), logo);
            }
            break;
        }
        case VDFilterType::ConvertFormat: {
            const int format = static_cast<int>(filter.params.value("format", 0));
            if (format == 1) result = result.convertToFormat(QImage::Format_RGBA8888);
            else if (format == 2) result = result.convertToFormat(QImage::Format_RGBA64);
            else result = result.convertToFormat(QImage::Format_RGB888);
            break;
        }
        case VDFilterType::Resize: {
            const int w = geometry.intermediateSize.width();
            const int h = geometry.intermediateSize.height();
            if (w > 0 && h > 0) {
                const int filterMode = static_cast<int>(
                    filter.params.value("filterMode", 4));
                const auto resizeRows = [](int rows, qint64 pixels, const auto& function) {
                    parallelFor(rows, pixels, function);
                };
                QString resizeError;
                const bool interlaced = filter.params.value("interlaced", 0) > 0.5;
                if (interlaced && result.height() > 1 && h > 1) {
                    const int evenSourceHeight = (result.height() + 1) / 2;
                    const int oddSourceHeight = result.height() / 2;
                    QImage even(result.width(), evenSourceHeight, result.format());
                    QImage odd(result.width(), std::max(1, oddSourceHeight), result.format());
                    if (even.isNull() || odd.isNull())
                        return failProcessing(QStringLiteral("Not enough memory to separate the resized fields."), &filter);
                    uchar *evenBits = checkedImageBits(even);
                    uchar *oddBits = checkedImageBits(odd);
                    const qsizetype evenStride = even.bytesPerLine();
                    const qsizetype oddStride = odd.bytesPerLine();
                    const uchar *sourceBits = result.constBits();
                    const qsizetype sourceStride = result.bytesPerLine();
                    for (int y = 0; y < result.height(); ++y) {
                        const qsizetype fieldStride = (y & 1) ? oddStride : evenStride;
                        uchar *fieldBits = (y & 1) ? oddBits : evenBits;
                        std::memcpy(fieldBits + (y / 2) * fieldStride,
                                    sourceBits + y * sourceStride,
                                    static_cast<size_t>(std::min(
                                        fieldStride, sourceStride)));
                    }
                    const int evenTargetHeight = (h + 1) / 2;
                    const int oddTargetHeight = h / 2;
                    even = VDQtResampleImage(even, QSize(w, evenTargetHeight), filterMode, resizeRows, &resizeError);
                    if (even.isNull()) return failProcessing(resizeError, &filter);
                    odd = VDQtResampleImage(odd, QSize(w, std::max(1, oddTargetHeight)), filterMode, resizeRows, &resizeError);
                    if (odd.isNull()) return failProcessing(resizeError, &filter);
                    QImage woven(w, h, result.format());
                    if (woven.isNull())
                        return failProcessing(QStringLiteral("Not enough memory to weave the resized fields."), &filter);
                    uchar *wovenBits = checkedImageBits(woven);
                    const qsizetype wovenStride = woven.bytesPerLine();
                    for (int y = 0; y < h; ++y) {
                        const QImage& field = (y & 1) ? odd : even;
                        std::memcpy(wovenBits + y * wovenStride, field.constScanLine(y / 2),
                                    static_cast<size_t>(std::min(
                                        wovenStride, field.bytesPerLine())));
                    }
                    result = woven;
                } else {
                    result = VDQtResampleImage(result, QSize(w, h), filterMode, resizeRows, &resizeError);
                    if (result.isNull()) return failProcessing(resizeError, &filter);
                }

                const int framingMode = static_cast<int>(
                    filter.params.value("framingMode", 0));
                const QColor fillColor(
                    std::clamp(static_cast<int>(filter.params.value("fillColorR", 0)), 0, 255),
                    std::clamp(static_cast<int>(filter.params.value("fillColorG", 0)), 0, 255),
                    std::clamp(static_cast<int>(filter.params.value("fillColorB", 0)), 0, 255));
                if (framingMode == 1) {
                    QImage framed(geometry.outputSize, result.format());
                    checkedImageBits(framed);
                    framed.fill(fillColor);
                    QPainter painter;
                    if (!painter.begin(&framed)) throw std::bad_alloc();
                    painter.drawImage(geometry.imageOffset, result);
                    painter.end();
                    result = framed;
                } else if (framingMode == 2 || framingMode == 3) {
                    if (framingMode == 2) {
                        result = result.copy(geometry.outputCrop);
                    } else {
                        QImage framed(geometry.outputSize, result.format());
                        checkedImageBits(framed);
                        framed.fill(fillColor);
                        QPainter painter;
                        if (!painter.begin(&framed)) throw std::bad_alloc();
                        painter.drawImage(geometry.imageOffset, result);
                        painter.end();
                        result = framed;
                    }
                }
            }
            break;
        }
        case VDFilterType::Rotate: {
            int mode = static_cast<int>(filter.params.value("mode", 0));
            double angle = 270;
            if (mode == 1) angle = 90;
            else if (mode == 2) angle = 180;
            else if (mode == 0) angle = 270;
            else angle = filter.params.value("angle", 270);

            QTransform trans;
            trans.rotate(angle);
            result = result.transformed(trans, Qt::SmoothTransformation);
            break;
        }
        case VDFilterType::FlipHorizontal:
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            result = result.flipped(Qt::Horizontal);
#else
            result = result.mirrored(true, false);
#endif
            break;

        case VDFilterType::FlipVertical:
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            result = result.flipped(Qt::Vertical);
#else
            result = result.mirrored(false, true);
#endif
            break;

        case VDFilterType::BobDoubler: {
            int fieldOrder = static_cast<int>(filter.params.value("field_order", 1)); // 0: TFF, 1: BFF
            int mode = static_cast<int>(filter.params.value("mode", 0));
            bool retainedFieldIsOdd = (fieldOrder == 1);
            if (bobPhaseMask & 1U)
                retainedFieldIsOdd = !retainedFieldIsOdd;

            int w = result.width();
            int h = result.height();

            // "None - double frames" deliberately duplicates the unmodified
            // input in both temporal phases.
            if (mode == 4 || w <= 0 || h <= 1)
                break;

            QImage temp = result;

            if (highPrecision) {
                uchar *resultBits = checkedImageBits(result);
                const int resultStride = result.bytesPerLine();
                const uchar *tempBits = temp.constBits();
                const int tempStride = temp.bytesPerLine();
                parallelFor(h, static_cast<qint64>(w) * h, [&](int y) {
                    const bool scanIsOdd = (y & 1) != 0;
                    if (scanIsOdd == retainedFieldIsOdd)
                        return;

                    QRgba64 *dst = reinterpret_cast<QRgba64 *>(
                        resultBits + static_cast<qint64>(y) * resultStride);
                    int previousLine = y - 1;
                    int nextLine = y + 1;
                    if (previousLine < 0)
                        previousLine = nextLine < h ? nextLine : y;
                    if (nextLine >= h)
                        nextLine = previousLine >= 0 ? previousLine : y;
                    const QRgba64 *src1 = reinterpret_cast<const QRgba64 *>(
                        tempBits + static_cast<qint64>(previousLine) * tempStride);
                    const QRgba64 *src2 = reinterpret_cast<const QRgba64 *>(
                        tempBits + static_cast<qint64>(nextLine) * tempStride);

                    if (mode == 0) {
                        for (int x = 0; x < w; ++x) {
                            dst[x] = QRgba64::fromRgba64(
                                static_cast<quint16>((static_cast<quint32>(src1[x].red()) + src2[x].red() + 1U) >> 1),
                                static_cast<quint16>((static_cast<quint32>(src1[x].green()) + src2[x].green() + 1U) >> 1),
                                static_cast<quint16>((static_cast<quint32>(src1[x].blue()) + src2[x].blue() + 1U) >> 1),
                                static_cast<quint16>((static_cast<quint32>(src1[x].alpha()) + src2[x].alpha() + 1U) >> 1));
                        }
                    } else if (mode == 1 || mode == 2) {
                        for (int x = 0; x < w; ++x) {
                            const int xPrev = std::clamp(x - 1, 0, w - 1);
                            const int xNext = std::clamp(x + 1, 0, w - 1);
                            qint64 d0 = 0, d1 = 0, d2 = 0;
                            for (int c = 0; c < 3; ++c) {
                                d0 += std::abs(static_cast<int>(rgba64Channel(src1[xPrev], c))
                                             - static_cast<int>(rgba64Channel(src2[xNext], c)));
                                d1 += std::abs(static_cast<int>(rgba64Channel(src1[x], c))
                                             - static_cast<int>(rgba64Channel(src2[x], c)));
                                d2 += std::abs(static_cast<int>(rgba64Channel(src1[xNext], c))
                                             - static_cast<int>(rgba64Channel(src2[xPrev], c)));
                            }
                            const QRgba64 *a = src1;
                            const QRgba64 *b = src2;
                            int ax = x;
                            int bx = x;
                            if (d0 < d1 && d0 < d2) {
                                ax = xPrev;
                                bx = xNext;
                            } else if (d2 < d1 && d2 < d0) {
                                ax = xNext;
                                bx = xPrev;
                            }
                            dst[x] = QRgba64::fromRgba64(
                                static_cast<quint16>((static_cast<quint32>(a[ax].red()) + b[bx].red() + 1U) >> 1),
                                static_cast<quint16>((static_cast<quint32>(a[ax].green()) + b[bx].green() + 1U) >> 1),
                                static_cast<quint16>((static_cast<quint32>(a[ax].blue()) + b[bx].blue() + 1U) >> 1),
                                src1[x].alpha());
                        }
                    } else {
                        memcpy(dst, src1, static_cast<size_t>(w) * sizeof(QRgba64));
                    }
                });
                break;
            }

            int bpp = (result.format() == QImage::Format_RGB888) ? 3 : 4;
            uchar *resultBits = checkedImageBits(result);
            const int resultStride = result.bytesPerLine();
            const uchar *tempBits = temp.constBits();
            const int tempStride = temp.bytesPerLine();

            parallelFor(h, static_cast<qint64>(w) * h, [&](int y) {
                const bool scanIsOdd = (y & 1) != 0;
                if (scanIsOdd == retainedFieldIsOdd) {
                    memcpy(resultBits + static_cast<qint64>(y) * resultStride,
                           tempBits + static_cast<qint64>(y) * tempStride,
                           w * bpp);
                } else {
                    uchar *dst = resultBits
                        + static_cast<qint64>(y) * resultStride;
                    int previousLine = y - 1;
                    int nextLine = y + 1;

                    if (previousLine < 0)
                        previousLine = nextLine < h ? nextLine : y;
                    if (nextLine >= h)
                        nextLine = previousLine >= 0 ? previousLine : y;

                    const uchar *src1 = tempBits
                        + static_cast<qint64>(previousLine) * tempStride;
                    const uchar *src2 = tempBits
                        + static_cast<qint64>(nextLine) * tempStride;

                    if (mode == 0) { // Bob (Linear vertical interpolation)
                        for (int x = 0; x < w * bpp; ++x) {
                            dst[x] = static_cast<uchar>((static_cast<int>(src1[x]) + static_cast<int>(src2[x]) + 1) >> 1);
                        }
                    } else if (mode == 1 || mode == 2) { // ELA / Adaptive ELA (Edge-directed interpolation)
                        for (int x = 0; x < w; ++x) {
                            int xPrev = std::clamp(x - 1, 0, w - 1);
                            int xNext = std::clamp(x + 1, 0, w - 1);

                            int d0 = 0, d1 = 0, d2 = 0;
                            for (int c = 0; c < 3; ++c) {
                                int diff0 = std::abs(static_cast<int>(src1[xPrev * bpp + c]) - static_cast<int>(src2[xNext * bpp + c]));
                                int diff1 = std::abs(static_cast<int>(src1[x * bpp + c])     - static_cast<int>(src2[x * bpp + c]));
                                int diff2 = std::abs(static_cast<int>(src1[xNext * bpp + c]) - static_cast<int>(src2[xPrev * bpp + c]));
                                d0 += diff0; d1 += diff1; d2 += diff2;
                            }

                            if (d0 < d1 && d0 < d2) {
                                for (int c = 0; c < 3; ++c) {
                                    dst[x * bpp + c] = static_cast<uchar>((static_cast<int>(src1[xPrev * bpp + c]) + static_cast<int>(src2[xNext * bpp + c]) + 1) >> 1);
                                }
                            } else if (d2 < d1 && d2 < d0) {
                                for (int c = 0; c < 3; ++c) {
                                    dst[x * bpp + c] = static_cast<uchar>((static_cast<int>(src1[xNext * bpp + c]) + static_cast<int>(src2[xPrev * bpp + c]) + 1) >> 1);
                                }
                            } else {
                                for (int c = 0; c < 3; ++c) {
                                    dst[x * bpp + c] = static_cast<uchar>((static_cast<int>(src1[x * bpp + c]) + static_cast<int>(src2[x * bpp + c]) + 1) >> 1);
                                }
                            }
                            if (bpp == 4) dst[x * 4 + 3] = src1[x * 4 + 3];
                        }
                    } else { // None - alternate fields
                        memcpy(dst, src1, w * bpp);
                    }
                }
            });
            break;
        }

        case VDFilterType::SixAxis: {
            // A neutral correction must be a true identity, including alpha
            // and native low bits. Skip its table and image detachment, but
            // still run the common clipping/opacity/metadata processing tail.
            // Check stored controls exactly; do not treat small real changes
            // as neutral or let unrelated script metadata affect this choice.
            if (filter.params.value("intensity", 1.0) == 1.0
                && filter.params.value("red_green", 0.0) == 0.0
                && filter.params.value("yellow_blue", 0.0) == 0.0
                && filter.params.value("saturation", 1.0) == 1.0
                && filter.params.value("red", 1.0) == 1.0
                && filter.params.value("orange", 1.0) == 1.0
                && filter.params.value("lime", 1.0) == 1.0
                && filter.params.value("emerald", 1.0) == 1.0
                && filter.params.value("blue", 1.0) == 1.0
                && filter.params.value("purple", 1.0) == 1.0) break;

            float intensity = static_cast<float>(filter.params.value("intensity", 1.0));
            float redGreen = static_cast<float>(filter.params.value("red_green", 0.0));
            float yellowBlue = static_cast<float>(filter.params.value("yellow_blue", 0.0));
            float satGlobal = static_cast<float>(filter.params.value("saturation", 1.0));
            float redGain = static_cast<float>(filter.params.value("red", 1.0));
            float orangeGain = static_cast<float>(filter.params.value("orange", 1.0));
            float limeGain = static_cast<float>(filter.params.value("lime", 1.0));
            float emeraldGain = static_cast<float>(filter.params.value("emerald", 1.0));
            float blueGain = static_cast<float>(filter.params.value("blue", 1.0));
            float purpleGain = static_cast<float>(filter.params.value("purple", 1.0));

            const float axesAngles[6] = { 0.0f, 30.0f, 90.0f, 180.0f, 240.0f, 300.0f };
            const float axesGains[6] = { redGain, orangeGain, limeGain, emeraldGain, blueGain, purpleGain };

            auto adjustPixel = [&](float r, float g, float b) {
                float cmax = std::max(r, std::max(g, b));
                float cmin = std::min(r, std::min(g, b));
                float delta = cmax - cmin;
                float hue = 0.0f;
                const float saturation = cmax > 1e-5f ? delta / cmax : 0.0f;
                const float value = cmax;
                if (delta > 1e-5f) {
                    if (cmax == r)
                        hue = 60.0f * std::fmod(((g - b) / delta) + 6.0f, 6.0f);
                    else if (cmax == g)
                        hue = 60.0f * (((b - r) / delta) + 2.0f);
                    else
                        hue = 60.0f * (((r - g) / delta) + 4.0f);
                }

                float axisMod = 0.0f;
                for (int k = 0; k < 6; ++k) {
                    float diff = std::abs(hue - axesAngles[k]);
                    if (diff > 180.0f) diff = 360.0f - diff;
                    if (diff < 60.0f)
                        axisMod += (1.0f - diff / 60.0f) * (axesGains[k] - 1.0f);
                }

                const float newS = std::clamp(
                    saturation * satGlobal * (1.0f + axisMod), 0.0f, 1.0f);
                const float chroma = value * newS;
                const float intermediate = chroma
                    * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
                const float match = value - chroma;
                float nr = 0.0f, ng = 0.0f, nb = 0.0f;
                if (hue < 60.0f)       { nr = chroma; ng = intermediate; }
                else if (hue < 120.0f) { nr = intermediate; ng = chroma; }
                else if (hue < 180.0f) { ng = chroma; nb = intermediate; }
                else if (hue < 240.0f) { ng = intermediate; nb = chroma; }
                else if (hue < 300.0f) { nr = intermediate; nb = chroma; }
                else                   { nr = chroma; nb = intermediate; }

                return std::array<float, 3>{
                    (nr + match) * intensity + redGreen * 0.15f + yellowBlue * 0.08f,
                    (ng + match) * intensity - redGreen * 0.15f + yellowBlue * 0.08f,
                    (nb + match) * intensity - yellowBlue * 0.16f
                };
            };

            int h = result.height();
            int w = result.width();
            if (highPrecision) {
                uchar *resultBits = checkedImageBits(result);
                const int resultStride = result.bytesPerLine();
                parallelFor(h, static_cast<qint64>(w) * h, [&](int y) {
                    QRgba64 *scan = reinterpret_cast<QRgba64 *>(
                        resultBits + static_cast<qint64>(y) * resultStride);
                    for (int x = 0; x < w; ++x) {
                        const QRgba64 original = scan[x];
                        const auto adjusted = adjustPixel(
                            original.red() / 65535.0f,
                            original.green() / 65535.0f,
                            original.blue() / 65535.0f);
                        scan[x] = QRgba64::fromRgba64(
                            static_cast<quint16>(std::clamp(std::lround(adjusted[0] * 65535.0f), 0L, 65535L)),
                            static_cast<quint16>(std::clamp(std::lround(adjusted[1] * 65535.0f), 0L, 65535L)),
                            static_cast<quint16>(std::clamp(std::lround(adjusted[2] * 65535.0f), 0L, 65535L)),
                            original.alpha());
                    }
                });
                break;
            }

            int bpp = (result.format() == QImage::Format_RGB888) ? 3 : 4;

            QString lutKey;
            for (auto it = filter.params.cbegin(); it != filter.params.cend(); ++it) {
                lutKey += it.key();
                lutKey += QLatin1Char('=');
                lutKey += QString::number(it.value(), 'g', 17);
                lutKey += QLatin1Char(';');
            }
            constexpr int gridSize = 33;
            constexpr int gridStrideG = gridSize * 3;
            constexpr int gridStrideR = gridSize * gridSize * 3;
            // Retain a cheap implicitly shared value, not a hash iterator that
            // insertion/eviction could invalidate. Its pixels outlive row tasks.
            QByteArray lutBytes = mSixAxisLutCache.value(lutKey);
            if (lutBytes.isEmpty()) {
                lutBytes.resize(gridSize * gridSize * gridSize * 3);
                uchar *table = reinterpret_cast<uchar *>(lutBytes.data());
                parallelFor(gridSize,
                            static_cast<qint64>(gridSize) * gridSize * gridSize,
                            [&](int ri) {
                    const float r = std::min(255, ri * 8) / 255.0f;
                    for (int gi = 0; gi < gridSize; ++gi) {
                        const float g = std::min(255, gi * 8) / 255.0f;
                        for (int bi = 0; bi < gridSize; ++bi) {
                            const float b = std::min(255, bi * 8) / 255.0f;
                            const auto adjusted = adjustPixel(r, g, b);
                            const int offset = ri * gridStrideR
                                             + gi * gridStrideG + bi * 3;
                            table[offset] = static_cast<uchar>(std::clamp(
                                std::lround(adjusted[0] * 255.0f), 0L, 255L));
                            table[offset + 1] = static_cast<uchar>(std::clamp(
                                std::lround(adjusted[1] * 255.0f), 0L, 255L));
                            table[offset + 2] = static_cast<uchar>(std::clamp(
                                std::lround(adjusted[2] * 255.0f), 0L, 255L));
                        }
                    }
                });
                if (mSixAxisLutCache.size() >= 8)
                    mSixAxisLutCache.erase(mSixAxisLutCache.begin());
                mSixAxisLutCache.insert(lutKey, lutBytes);
            }
            const uchar *lut = reinterpret_cast<const uchar *>(lutBytes.constData());
            uchar *resultBits = checkedImageBits(result);
            const int resultStride = result.bytesPerLine();
            parallelFor(h, static_cast<qint64>(w) * h, [&](int y) {
                uchar *scan = resultBits
                    + static_cast<qint64>(y) * resultStride;
                for (int x = 0; x < w; ++x) {
                    const int r = scan[x * bpp];
                    const int g = scan[x * bpp + 1];
                    const int b = scan[x * bpp + 2];
                    const int ri = r >> 3;
                    const int gi = g >> 3;
                    const int bi = b >> 3;
                    const int rf = r & 7;
                    const int gf = g & 7;
                    const int bf = b & 7;
                    // Lattice nodes are 0,8,...,248,255. The final cell spans
                    // seven values, not eight; treating it as eight dims 253
                    // and 254 even with neutral controls. Mixed cells need the
                    // product of their actual spans, with one final rounding.
                    const int rSpan = ri == 31 ? 7 : 8;
                    const int gSpan = gi == 31 ? 7 : 8;
                    const int bSpan = bi == 31 ? 7 : 8;
                    const int divisor = rSpan * gSpan * bSpan;
                    const int base = ri * gridStrideR
                                   + gi * gridStrideG + bi * 3;
                    for (int channel = 0; channel < 3; ++channel) {
                        const int c000 = lut[base + channel];
                        const int c001 = lut[base + 3 + channel];
                        const int c010 = lut[base + gridStrideG + channel];
                        const int c011 = lut[base + gridStrideG + 3 + channel];
                        const int upper = base + gridStrideR;
                        const int c100 = lut[upper + channel];
                        const int c101 = lut[upper + 3 + channel];
                        const int c110 = lut[upper + gridStrideG + channel];
                        const int c111 = lut[upper + gridStrideG + 3 + channel];
                        const int c00 = c000 * (bSpan - bf) + c001 * bf;
                        const int c01 = c010 * (bSpan - bf) + c011 * bf;
                        const int c10 = c100 * (bSpan - bf) + c101 * bf;
                        const int c11 = c110 * (bSpan - bf) + c111 * bf;
                        const int c0 = c00 * (gSpan - gf) + c01 * gf;
                        const int c1 = c10 * (gSpan - gf) + c11 * gf;
                        const int weighted = c0 * (rSpan - rf) + c1 * rf;
                        scan[x * bpp + channel] = static_cast<uchar>(divisor == 512
                            ? (weighted + 256) >> 9
                            : (weighted + divisor / 2) / divisor);
                    }
                }
            });
            break;
        }

        case VDFilterType::BrightnessContrast: {
            int bright = static_cast<int>(filter.params.value("bright", 0));
            int cont = static_cast<int>(filter.params.value("cont", 16));

            float bias = bright - 0.5f;
            float scale = static_cast<float>(cont) / 16.0f;

            uint8_t table[256];
            int32_t y0 = static_cast<int32_t>(std::round(bias * 65536.0f)) + 0x8000;
            int32_t dydx = static_cast<int32_t>(std::round(scale * 65536.0f));

            for (int i = 0; i < 256; ++i) {
                int y = y0 >> 16;
                y0 += dydx;
                table[i] = static_cast<uint8_t>(std::clamp(y, 0, 255));
            }

            if (highPrecision) {
                uchar *resultBits = checkedImageBits(result);
                const int resultStride = result.bytesPerLine();
                parallelFor(result.height(),
                            static_cast<qint64>(result.width()) * result.height(),
                            [&](int y) {
                    QRgba64 *scan = reinterpret_cast<QRgba64 *>(
                        resultBits + static_cast<qint64>(y) * resultStride);
                    for (int x = 0; x < result.width(); ++x) {
                        QRgba64 pixel = scan[x];
                        for (int c = 0; c < 3; ++c) {
                            const qint64 source = rgba64Channel(pixel, c);
                            const qint64 mapped = static_cast<qint64>(std::llround(
                                static_cast<double>(source) * scale
                                + static_cast<double>(bright) * 257.0));
                            setRgba64Channel(pixel, c, static_cast<quint16>(
                                std::clamp<qint64>(mapped, 0, 65535)));
                        }
                        scan[x] = pixel;
                    }
                });
                break;
            }

            int bytesPerPixel = (result.format() == QImage::Format_RGB888) ? 3 : 4;
            int h = result.height();
            int w = result.width();
            uchar *resultBits = checkedImageBits(result);
            const int resultStride = result.bytesPerLine();

            parallelFor(h, static_cast<qint64>(w) * h, [&](int y) {
                uchar *scan = resultBits
                    + static_cast<qint64>(y) * resultStride;
                for (int x = 0; x < w * bytesPerPixel; ++x) {
                    if (bytesPerPixel == 4 && (x % 4 == 3)) continue; // skip alpha
                    scan[x] = table[scan[x]];
                }
            });
            break;
        }
        case VDFilterType::Blur: {
            int width = static_cast<int>(filter.params.value("width", 1));
            int power = static_cast<int>(filter.params.value("power", 1));
            if (width <= 0) width = 1;
            if (power < 1) power = 1;
            if (power > 3) power = 3;

            int w = result.width();
            int h = result.height();

            if (highPrecision) {
                auto boxBlurPass64 = [w, h](QImage& image, int radius) {
                    if (radius <= 0 || w <= 0 || h <= 0) return;
                    QImage horizontal = image;
                    // Validation caps radius at 48: a rolling RGB sum is at
                    // most 97 * 65535 = 6,356,895, safely within signed int.
                    const int windowSize = radius * 2 + 1;
                    const uchar *imageSourceBits = image.constBits();
                    const int imageSourceStride = image.bytesPerLine();
                    uchar *horizontalBits = checkedImageBits(horizontal);
                    const int horizontalStride = horizontal.bytesPerLine();
                    parallelFor(h, static_cast<qint64>(w) * h, [&](int y) {
                        const QRgba64 *src = reinterpret_cast<const QRgba64 *>(
                            imageSourceBits + static_cast<qint64>(y) * imageSourceStride);
                        QRgba64 *dst = reinterpret_cast<QRgba64 *>(
                            horizontalBits + static_cast<qint64>(y) * horizontalStride);
                        // Update the RGB sums together and write each packed
                        // pixel once, not one row pass per separate channel.
                        std::array<int, 3> sums{};
                        for (int offset = -radius; offset <= radius; ++offset) {
                            const QRgba64 pixel = src[std::clamp(offset, 0, w - 1)];
                            sums[0] += pixel.red();
                            sums[1] += pixel.green();
                            sums[2] += pixel.blue();
                        }
                        for (int x = 0; x < w; ++x) {
                            dst[x] = QRgba64::fromRgba64(
                                static_cast<quint16>(sums[0] / windowSize),
                                static_cast<quint16>(sums[1] / windowSize),
                                static_cast<quint16>(sums[2] / windowSize), src[x].alpha());
                            const QRgba64 left = src[std::clamp(x - radius, 0, w - 1)];
                            const QRgba64 right = src[std::clamp(x + radius + 1, 0, w - 1)];
                            sums[0] += int(right.red()) - left.red();
                            sums[1] += int(right.green()) - left.green();
                            sums[2] += int(right.blue()) - left.blue();
                        }
                    });
                    const uchar *horizontalSourceBits = horizontal.constBits();
                    uchar *imageDestinationBits = checkedImageBits(image);
                    const int imageDestinationStride = image.bytesPerLine();
                    // A whole column repeatedly misses cache lines and makes
                    // neighboring workers fight over the same output lines.
                    // Roll a bounded band of sums down the image instead: each
                    // row reads/writes adjacent pixels, with no new heap scratch.
                    const int bands = (w + kBlurColumnBandWidth - 1) / kBlurColumnBandWidth;
                    parallelFor(bands, static_cast<qint64>(w) * h, [&](int band) {
                        const int begin = band * kBlurColumnBandWidth;
                        const int columns = std::min(kBlurColumnBandWidth, w - begin);
                        std::array<int, kBlurColumnBandWidth * 3> sums{};
                        for (int offset = -radius; offset <= radius; ++offset) {
                            const auto *row = reinterpret_cast<const QRgba64 *>(horizontalSourceBits
                                + static_cast<qint64>(std::clamp(offset, 0, h - 1)) * horizontalStride);
                            for (int column = 0; column < columns; ++column) {
                                const QRgba64 pixel = row[begin + column];
                                sums[column * 3] += pixel.red();
                                sums[column * 3 + 1] += pixel.green();
                                sums[column * 3 + 2] += pixel.blue();
                            }
                        }
                        for (int y = 0; y < h; ++y) {
                            auto *destination = reinterpret_cast<QRgba64 *>(imageDestinationBits
                                + static_cast<qint64>(y) * imageDestinationStride);
                            const auto *top = reinterpret_cast<const QRgba64 *>(horizontalSourceBits
                                + static_cast<qint64>(std::clamp(y - radius, 0, h - 1)) * horizontalStride);
                            const auto *bottom = reinterpret_cast<const QRgba64 *>(horizontalSourceBits
                                + static_cast<qint64>(std::clamp(y + radius + 1, 0, h - 1)) * horizontalStride);
                            for (int column = 0; column < columns; ++column) {
                                const int x = begin + column;
                                int *sum = sums.data() + column * 3;
                                destination[x] = QRgba64::fromRgba64(
                                    static_cast<quint16>(sum[0] / windowSize),
                                    static_cast<quint16>(sum[1] / windowSize),
                                    static_cast<quint16>(sum[2] / windowSize), destination[x].alpha());
                                sum[0] += int(bottom[x].red()) - top[x].red();
                                sum[1] += int(bottom[x].green()) - top[x].green();
                                sum[2] += int(bottom[x].blue()) - top[x].blue();
                            }
                        }
                    });
                };
                for (int i = 0; i < power; ++i)
                    boxBlurPass64(result, width);
                break;
            }

            int bpp = (result.format() == QImage::Format_RGB888) ? 3 : 4;

            auto boxBlurPass = [bpp, w, h](QImage &img, int radius) {
                if (radius <= 0 || w <= 0 || h <= 0) return;
                QImage temp = img;
                int winSize = 2 * radius + 1;
                const uchar *imageSourceBits = img.constBits();
                const int imageSourceStride = img.bytesPerLine();
                uchar *temporaryBits = checkedImageBits(temp);
                const int temporaryStride = temp.bytesPerLine();

                // Horizontal pass
                parallelFor(h, static_cast<qint64>(w) * h, [&](int y) {
                    const uchar *srcRow = imageSourceBits
                        + static_cast<qint64>(y) * imageSourceStride;
                    uchar *dstRow = temporaryBits
                        + static_cast<qint64>(y) * temporaryStride;
                    for (int c = 0; c < 3; ++c) {
                        int sum = 0;
                        for (int x = -radius; x <= radius; ++x) {
                            int cx = std::clamp(x, 0, w - 1);
                            sum += srcRow[cx * bpp + c];
                        }
                        for (int x = 0; x < w; ++x) {
                            dstRow[x * bpp + c] = static_cast<uchar>(sum / winSize);
                            int lx = std::clamp(x - radius, 0, w - 1);
                            int rx = std::clamp(x + radius + 1, 0, w - 1);
                            sum += srcRow[rx * bpp + c] - srcRow[lx * bpp + c];
                        }
                    }
                });

                // Vertical pass
                const uchar *temporarySourceBits = temp.constBits();
                uchar *imageDestinationBits = checkedImageBits(img);
                const int imageDestinationStride = img.bytesPerLine();
                // The same bounded, row-contiguous vertical bands as RGBA64.
                // RGB channels keep the old positive-integer floor division;
                // an optional fourth alpha byte is never read or overwritten.
                const int bands = (w + kBlurColumnBandWidth - 1) / kBlurColumnBandWidth;
                parallelFor(bands, static_cast<qint64>(w) * h, [&](int band) {
                    const int begin = band * kBlurColumnBandWidth;
                    const int columns = std::min(kBlurColumnBandWidth, w - begin);
                    std::array<int, kBlurColumnBandWidth * 3> sums{};
                    for (int offset = -radius; offset <= radius; ++offset) {
                        const uchar *row = temporarySourceBits
                            + static_cast<qint64>(std::clamp(offset, 0, h - 1)) * temporaryStride;
                        for (int column = 0; column < columns; ++column)
                            for (int c = 0; c < 3; ++c)
                                sums[column * 3 + c] += row[(begin + column) * bpp + c];
                    }
                    for (int y = 0; y < h; ++y) {
                        uchar *destination = imageDestinationBits
                            + static_cast<qint64>(y) * imageDestinationStride;
                        const uchar *top = temporarySourceBits
                            + static_cast<qint64>(std::clamp(y - radius, 0, h - 1)) * temporaryStride;
                        const uchar *bottom = temporarySourceBits
                            + static_cast<qint64>(std::clamp(y + radius + 1, 0, h - 1)) * temporaryStride;
                        for (int column = 0; column < columns; ++column) {
                            const int pixelOffset = (begin + column) * bpp;
                            for (int c = 0; c < 3; ++c) {
                                int& sum = sums[column * 3 + c];
                                destination[pixelOffset + c] = static_cast<uchar>(sum / winSize);
                                sum += bottom[pixelOffset + c] - top[pixelOffset + c];
                            }
                        }
                    }
                });
            };

            for (int i = 0; i < power; ++i) {
                boxBlurPass(result, width);
            }
            break;
        }
        case VDFilterType::Deinterlace: {
            const int mode = std::clamp(
                static_cast<int>(filter.params.value("mode", 0)), 0, 2);
            const int width = result.width();
            const int height = result.height();
            if (height < 2) break;
            const QImage source = result;
            const uchar *sourceBits = source.constBits();
            const int sourceStride = source.bytesPerLine();
            uchar *destinationBits = checkedImageBits(result);
            const int destinationStride = result.bytesPerLine();
            if (highPrecision) {
                parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
                    const bool replace = mode == 0 ? (y & 1)
                        : mode == 1 ? (y & 1) : !(y & 1);
                    if (!replace) return;
                    const int previousY = std::max(0, y - 1);
                    const int nextY = std::min(height - 1, y + 1);
                    const QRgba64 *previous = reinterpret_cast<const QRgba64 *>(
                        sourceBits + static_cast<qint64>(previousY) * sourceStride);
                    const QRgba64 *next = reinterpret_cast<const QRgba64 *>(
                        sourceBits + static_cast<qint64>(nextY) * sourceStride);
                    QRgba64 *destination = reinterpret_cast<QRgba64 *>(
                        destinationBits + static_cast<qint64>(y) * destinationStride);
                    for (int x = 0; x < width; ++x) {
                        destination[x] = QRgba64::fromRgba64(
                            static_cast<quint16>((static_cast<quint32>(previous[x].red()) + next[x].red() + 1U) / 2U),
                            static_cast<quint16>((static_cast<quint32>(previous[x].green()) + next[x].green() + 1U) / 2U),
                            static_cast<quint16>((static_cast<quint32>(previous[x].blue()) + next[x].blue() + 1U) / 2U),
                            static_cast<quint16>((static_cast<quint32>(previous[x].alpha()) + next[x].alpha() + 1U) / 2U));
                    }
                });
            } else {
                const int bpp = result.format() == QImage::Format_RGB888 ? 3 : 4;
                parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
                    const bool replace = mode == 0 ? (y & 1)
                        : mode == 1 ? (y & 1) : !(y & 1);
                    if (!replace) return;
                    const uchar *previous = sourceBits
                        + static_cast<qint64>(std::max(0, y - 1)) * sourceStride;
                    const uchar *next = sourceBits
                        + static_cast<qint64>(std::min(height - 1, y + 1)) * sourceStride;
                    uchar *destination = destinationBits
                        + static_cast<qint64>(y) * destinationStride;
                    for (int x = 0; x < width * bpp; ++x)
                        destination[x] = static_cast<uchar>(
                            (static_cast<int>(previous[x]) + next[x] + 1) / 2);
                });
            }
            break;
        }
        case VDFilterType::FieldSwap: {
            const QImage source = result;
            const uchar *sourceBits = source.constBits();
            uchar *destinationBits = checkedImageBits(result);
            const int sourceStride = source.bytesPerLine();
            const int destinationStride = result.bytesPerLine();
            const int height = result.height();
            parallelFor(height,
                        static_cast<qint64>(result.width()) * height,
                        [&](int y) {
                int sourceY = (y & 1) ? y - 1 : y + 1;
                if (sourceY >= height) sourceY = y;
                std::memcpy(destinationBits + static_cast<qint64>(y) * destinationStride,
                            sourceBits + static_cast<qint64>(sourceY) * sourceStride,
                            static_cast<size_t>(std::min(sourceStride, destinationStride)));
            });
            break;
        }
        case VDFilterType::Emboss: {
            const double strength = std::clamp(
                filter.params.value("strength", 1.0), 0.0, 8.0);
            const int width = result.width();
            const int height = result.height();
            const QImage source = result;
            const uchar *sourceBits = source.constBits();
            const int sourceStride = source.bytesPerLine();
            uchar *destinationBits = checkedImageBits(result);
            const int destinationStride = result.bytesPerLine();
            if (highPrecision) {
                parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
                    const QRgba64 *current = reinterpret_cast<const QRgba64 *>(
                        sourceBits + static_cast<qint64>(y) * sourceStride);
                    const QRgba64 *previous = reinterpret_cast<const QRgba64 *>(
                        sourceBits + static_cast<qint64>(std::max(0, y - 1)) * sourceStride);
                    QRgba64 *destination = reinterpret_cast<QRgba64 *>(
                        destinationBits + static_cast<qint64>(y) * destinationStride);
                    for (int x = 0; x < width; ++x) {
                        const int previousX = std::max(0, x - 1);
                        const auto channel = [&](int c) {
                            return static_cast<quint16>(std::clamp(
                                std::llround(32768.0 + strength
                                    * (static_cast<double>(rgba64Channel(current[x], c))
                                       - rgba64Channel(previous[previousX], c))),
                                0LL, 65535LL));
                        };
                        destination[x] = QRgba64::fromRgba64(
                            channel(0), channel(1), channel(2), current[x].alpha());
                    }
                });
            } else {
                const int bpp = result.format() == QImage::Format_RGB888 ? 3 : 4;
                parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
                    const uchar *current = sourceBits
                        + static_cast<qint64>(y) * sourceStride;
                    const uchar *previous = sourceBits
                        + static_cast<qint64>(std::max(0, y - 1)) * sourceStride;
                    uchar *destination = destinationBits
                        + static_cast<qint64>(y) * destinationStride;
                    for (int x = 0; x < width; ++x) {
                        const int previousX = std::max(0, x - 1);
                        for (int channel = 0; channel < 3; ++channel) {
                            destination[x * bpp + channel] = static_cast<uchar>(
                                std::clamp(std::lround(128.0 + strength
                                    * (current[x * bpp + channel]
                                       - previous[previousX * bpp + channel])),
                                    0L, 255L));
                        }
                    }
                });
            }
            break;
        }
        case VDFilterType::HSVAdjust: {
            const double hueOffset = filter.params.value("hueDegrees", 0.0);
            const double saturationScale = std::clamp(
                filter.params.value("saturation", 1.0), 0.0, 8.0);
            const double valueScale = std::clamp(
                filter.params.value("value", 1.0), 0.0, 8.0);
            transformRgbPixels(result, highPrecision,
                [=](double& red, double& green, double& blue) {
                const double maximum = std::max({red, green, blue});
                const double minimum = std::min({red, green, blue});
                const double delta = maximum - minimum;
                double hue = 0.0;
                if (delta > 1e-12) {
                    if (maximum == red)
                        hue = 60.0 * std::fmod((green - blue) / delta + 6.0, 6.0);
                    else if (maximum == green)
                        hue = 60.0 * ((blue - red) / delta + 2.0);
                    else
                        hue = 60.0 * ((red - green) / delta + 4.0);
                }
                hue = std::fmod(hue + hueOffset, 360.0);
                if (hue < 0.0) hue += 360.0;
                const double saturation = maximum > 1e-12
                    ? std::clamp(delta / maximum * saturationScale, 0.0, 1.0)
                    : 0.0;
                const double value = std::clamp(maximum * valueScale, 0.0, 1.0);
                const double chroma = value * saturation;
                const double intermediate = chroma
                    * (1.0 - std::abs(std::fmod(hue / 60.0, 2.0) - 1.0));
                const double match = value - chroma;
                if (hue < 60.0)       { red = chroma; green = intermediate; blue = 0.0; }
                else if (hue < 120.0) { red = intermediate; green = chroma; blue = 0.0; }
                else if (hue < 180.0) { red = 0.0; green = chroma; blue = intermediate; }
                else if (hue < 240.0) { red = 0.0; green = intermediate; blue = chroma; }
                else if (hue < 300.0) { red = intermediate; green = 0.0; blue = chroma; }
                else                  { red = chroma; green = 0.0; blue = intermediate; }
                red += match; green += match; blue += match;
            });
            break;
        }
        case VDFilterType::Threshold: {
            const double threshold = std::clamp(
                filter.params.value("threshold", 128.0) / 255.0, 0.0, 1.0);
            transformRgbPixels(result, highPrecision,
                [=](double& red, double& green, double& blue) {
                    const double value = 0.299 * red + 0.587 * green + 0.114 * blue
                        >= threshold ? 1.0 : 0.0;
                    red = value; green = value; blue = value;
                });
            break;
        }
        case VDFilterType::Posterize: {
            const int levels = std::clamp(
                static_cast<int>(std::llround(filter.params.value("levels", 8.0))),
                2, 256);
            transformRgbPixels(result, highPrecision,
                [=](double& red, double& green, double& blue) {
                    const auto quantize = [levels](double value) {
                        return std::round(std::clamp(value, 0.0, 1.0)
                                          * (levels - 1)) / (levels - 1);
                    };
                    red = quantize(red); green = quantize(green); blue = quantize(blue);
                });
            break;
        }
        case VDFilterType::Smoother: {
            const double amount = std::clamp(
                filter.params.value("amount", 0.5), 0.0, 1.0);
            if (amount <= 0.0) break;
            const int width = result.width();
            const int height = result.height();
            const QImage source = result;
            const uchar *sourceBits = source.constBits();
            const int sourceStride = source.bytesPerLine();
            uchar *destinationBits = checkedImageBits(result);
            const int destinationStride = result.bytesPerLine();
            if (highPrecision) {
                parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
                    QRgba64 *destination = reinterpret_cast<QRgba64 *>(
                        destinationBits + static_cast<qint64>(y) * destinationStride);
                    const QRgba64 *center = reinterpret_cast<const QRgba64 *>(
                        sourceBits + static_cast<qint64>(y) * sourceStride);
                    for (int x = 0; x < width; ++x) {
                        QRgba64 pixel = destination[x];
                        for (int channel = 0; channel < 3; ++channel) {
                            quint64 sum = 0;
                            for (int dy = -1; dy <= 1; ++dy) {
                                const QRgba64 *row = reinterpret_cast<const QRgba64 *>(
                                    sourceBits + static_cast<qint64>(
                                        std::clamp(y + dy, 0, height - 1)) * sourceStride);
                                for (int dx = -1; dx <= 1; ++dx)
                                    sum += rgba64Channel(row[std::clamp(x + dx, 0, width - 1)], channel);
                            }
                            const double mixed = rgba64Channel(center[x], channel)
                                * (1.0 - amount) + (sum / 9.0) * amount;
                            setRgba64Channel(pixel, channel,
                                static_cast<quint16>(std::clamp(
                                    std::llround(mixed), 0LL, 65535LL)));
                        }
                        destination[x] = pixel;
                    }
                });
            } else {
                const int bpp = result.format() == QImage::Format_RGB888 ? 3 : 4;
                parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
                    uchar *destination = destinationBits
                        + static_cast<qint64>(y) * destinationStride;
                    const uchar *center = sourceBits
                        + static_cast<qint64>(y) * sourceStride;
                    for (int x = 0; x < width; ++x) {
                        for (int channel = 0; channel < 3; ++channel) {
                            int sum = 0;
                            for (int dy = -1; dy <= 1; ++dy) {
                                const uchar *row = sourceBits + static_cast<qint64>(
                                    std::clamp(y + dy, 0, height - 1)) * sourceStride;
                                for (int dx = -1; dx <= 1; ++dx)
                                    sum += row[std::clamp(x + dx, 0, width - 1) * bpp + channel];
                            }
                            destination[x * bpp + channel] = static_cast<uchar>(
                                std::clamp(std::lround(center[x * bpp + channel]
                                    * (1.0 - amount) + (sum / 9.0) * amount),
                                    0L, 255L));
                        }
                    }
                });
            }
            break;
        }
        case VDFilterType::Crop: {
            result = result.copy(geometry.outputCrop);
            break;
        }
        case VDFilterType::ChromaShift: {
            const int shiftX = std::clamp(
                static_cast<int>(std::llround(filter.params.value("x", 0.0))),
                -256, 256);
            const int shiftY = std::clamp(
                static_cast<int>(std::llround(filter.params.value("y", 0.0))),
                -256, 256);
            if (shiftX == 0 && shiftY == 0) break;
            const int width = result.width();
            const int height = result.height();
            const QImage source = result;
            const uchar *sourceBits = source.constBits();
            const int sourceStride = source.bytesPerLine();
            uchar *destinationBits = checkedImageBits(result);
            const int destinationStride = result.bytesPerLine();
            if (highPrecision) {
                parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
                    QRgba64 *destination = reinterpret_cast<QRgba64 *>(
                        destinationBits + static_cast<qint64>(y) * destinationStride);
                    const QRgba64 *redRow = reinterpret_cast<const QRgba64 *>(
                        sourceBits + static_cast<qint64>(
                            std::clamp(y - shiftY, 0, height - 1)) * sourceStride);
                    const QRgba64 *blueRow = reinterpret_cast<const QRgba64 *>(
                        sourceBits + static_cast<qint64>(
                            std::clamp(y + shiftY, 0, height - 1)) * sourceStride);
                    for (int x = 0; x < width; ++x) {
                        QRgba64 pixel = destination[x];
                        pixel.setRed(redRow[std::clamp(x - shiftX, 0, width - 1)].red());
                        pixel.setBlue(blueRow[std::clamp(x + shiftX, 0, width - 1)].blue());
                        destination[x] = pixel;
                    }
                });
            } else {
                const int bpp = result.format() == QImage::Format_RGB888 ? 3 : 4;
                parallelFor(height, static_cast<qint64>(width) * height, [&](int y) {
                    uchar *destination = destinationBits
                        + static_cast<qint64>(y) * destinationStride;
                    const uchar *redRow = sourceBits + static_cast<qint64>(
                        std::clamp(y - shiftY, 0, height - 1)) * sourceStride;
                    const uchar *blueRow = sourceBits + static_cast<qint64>(
                        std::clamp(y + shiftY, 0, height - 1)) * sourceStride;
                    for (int x = 0; x < width; ++x) {
                        destination[x * bpp] = redRow[
                            std::clamp(x - shiftX, 0, width - 1) * bpp];
                        destination[x * bpp + 2] = blueRow[
                            std::clamp(x + shiftX, 0, width - 1) * bpp + 2];
                    }
                });
            }
            break;
        }
        case VDFilterType::Pixelate: {
            const int blockSize = std::clamp(
                static_cast<int>(std::llround(filter.params.value("blockSize", 8.0))),
                2, 256);
            const int width = result.width();
            const int height = result.height();
            uchar *bits = checkedImageBits(result);
            const int stride = result.bytesPerLine();
            const int blockRows = (height + blockSize - 1) / blockSize;
            parallelFor(blockRows, static_cast<qint64>(width) * height, [&](int blockRow) {
                const int y0 = blockRow * blockSize;
                const int y1 = std::min(height, y0 + blockSize);
                for (int x0 = 0; x0 < width; x0 += blockSize) {
                    const int x1 = std::min(width, x0 + blockSize);
                    const qint64 count = static_cast<qint64>(x1 - x0) * (y1 - y0);
                    if (highPrecision) {
                        quint64 sums[3] = {};
                        for (int y = y0; y < y1; ++y) {
                            QRgba64 *row = reinterpret_cast<QRgba64 *>(
                                bits + static_cast<qint64>(y) * stride);
                            for (int x = x0; x < x1; ++x) {
                                sums[0] += row[x].red(); sums[1] += row[x].green();
                                sums[2] += row[x].blue();
                            }
                        }
                        for (int y = y0; y < y1; ++y) {
                            QRgba64 *row = reinterpret_cast<QRgba64 *>(
                                bits + static_cast<qint64>(y) * stride);
                            for (int x = x0; x < x1; ++x)
                                row[x] = QRgba64::fromRgba64(
                                    sums[0] / count, sums[1] / count,
                                    sums[2] / count, row[x].alpha());
                        }
                    } else {
                        const int bpp = result.format() == QImage::Format_RGB888 ? 3 : 4;
                        qint64 sums[3] = {};
                        for (int y = y0; y < y1; ++y) {
                            uchar *row = bits + static_cast<qint64>(y) * stride;
                            for (int x = x0; x < x1; ++x)
                                for (int channel = 0; channel < 3; ++channel)
                                    sums[channel] += row[x * bpp + channel];
                        }
                        for (int y = y0; y < y1; ++y) {
                            uchar *row = bits + static_cast<qint64>(y) * stride;
                            for (int x = x0; x < x1; ++x)
                                for (int channel = 0; channel < 3; ++channel)
                                    row[x * bpp + channel] = static_cast<uchar>(
                                        sums[channel] / count);
                        }
                    }
                }
            });
            break;
        }
        case VDFilterType::Grayscale: {
            if (highPrecision) {
                uchar *resultBits = checkedImageBits(result);
                const int resultStride = result.bytesPerLine();
                parallelFor(result.height(),
                            static_cast<qint64>(result.width()) * result.height(),
                            [&](int y) {
                    QRgba64 *scan = reinterpret_cast<QRgba64 *>(
                        resultBits + static_cast<qint64>(y) * resultStride);
                    for (int x = 0; x < result.width(); ++x) {
                        const QRgba64 pixel = scan[x];
                        const quint16 gray = static_cast<quint16>((
                            static_cast<quint64>(pixel.red()) * 19595U
                            + static_cast<quint64>(pixel.green()) * 38470U
                            + static_cast<quint64>(pixel.blue()) * 7471U
                            + 32768U) >> 16);
                        scan[x] = QRgba64::fromRgba64(
                            gray, gray, gray, pixel.alpha());
                    }
                });
                break;
            }
            const int bpp = result.format() == QImage::Format_RGB888 ? 3 : 4;
            uchar *resultBits = checkedImageBits(result);
            const int resultStride = result.bytesPerLine();
            parallelFor(result.height(),
                        static_cast<qint64>(result.width()) * result.height(),
                        [&](int y) {
                uchar *scan = resultBits
                    + static_cast<qint64>(y) * resultStride;
                for (int x = 0; x < result.width(); x++) {
                    int r = scan[x * bpp];
                    int g = scan[x * bpp + 1];
                    int b = scan[x * bpp + 2];
                    uchar gray = static_cast<uchar>(0.299 * r + 0.587 * g + 0.114 * b);
                    scan[x * bpp] = gray;
                    scan[x * bpp + 1] = gray;
                    scan[x * bpp + 2] = gray;
                }
            });
            break;
        }
        case VDFilterType::InvertColor: {
            checkedImageBits(result);
            result.invertPixels(QImage::InvertRgb);
            break;
        }
        case VDFilterType::Sharpen: {
            int v = static_cast<int>(filter.params.value("amount", 16));
            if (v <= 0) break;

            int w = result.width();
            int h = result.height();

            QImage temp = result;
            const qint64 centerWeight = 256LL + 8LL * v;

            if (highPrecision) {
                uchar *resultBits = checkedImageBits(result);
                const int resultStride = result.bytesPerLine();
                const uchar *tempBits = temp.constBits();
                const int tempStride = temp.bytesPerLine();
                parallelFor(h, static_cast<qint64>(w) * h, [&](int y) {
                    QRgba64 *dst = reinterpret_cast<QRgba64 *>(
                        resultBits + static_cast<qint64>(y) * resultStride);
                    const int yPrev = std::clamp(y - 1, 0, h - 1);
                    const int yNext = std::clamp(y + 1, 0, h - 1);
                    const QRgba64 *previous = reinterpret_cast<const QRgba64 *>(
                        tempBits + static_cast<qint64>(yPrev) * tempStride);
                    const QRgba64 *current = reinterpret_cast<const QRgba64 *>(
                        tempBits + static_cast<qint64>(y) * tempStride);
                    const QRgba64 *next = reinterpret_cast<const QRgba64 *>(
                        tempBits + static_cast<qint64>(yNext) * tempStride);
                    for (int x = 0; x < w; ++x) {
                        const int xPrev = std::clamp(x - 1, 0, w - 1);
                        const int xNext = std::clamp(x + 1, 0, w - 1);
                        QRgba64 pixel = dst[x];
                        for (int c = 0; c < 3; ++c) {
                            const qint64 neighbors =
                                rgba64Channel(previous[xPrev], c)
                                + rgba64Channel(previous[x], c)
                                + rgba64Channel(previous[xNext], c)
                                + rgba64Channel(current[xPrev], c)
                                + rgba64Channel(current[xNext], c)
                                + rgba64Channel(next[xPrev], c)
                                + rgba64Channel(next[x], c)
                                + rgba64Channel(next[xNext], c);
                            const qint64 value =
                                (static_cast<qint64>(rgba64Channel(current[x], c))
                                     * centerWeight
                                 - neighbors * static_cast<qint64>(v) + 128LL) >> 8;
                            setRgba64Channel(pixel, c, static_cast<quint16>(
                                std::clamp<qint64>(value, 0, 65535)));
                        }
                        dst[x] = pixel;
                    }
                });
                break;
            }

            int bpp = (result.format() == QImage::Format_RGB888) ? 3 : 4;
            uchar *resultBits = checkedImageBits(result);
            const int resultStride = result.bytesPerLine();
            const uchar *tempBits = temp.constBits();
            const int tempStride = temp.bytesPerLine();

            parallelFor(h, static_cast<qint64>(w) * h, [&](int y) {
                uchar *dstRow = resultBits
                    + static_cast<qint64>(y) * resultStride;
                int yPrev = std::clamp(y - 1, 0, h - 1);
                int yNext = std::clamp(y + 1, 0, h - 1);

                const uchar *srcRowPrev = tempBits
                    + static_cast<qint64>(yPrev) * tempStride;
                const uchar *srcRowCurr = tempBits
                    + static_cast<qint64>(y) * tempStride;
                const uchar *srcRowNext = tempBits
                    + static_cast<qint64>(yNext) * tempStride;

                for (int x = 0; x < w; ++x) {
                    int xPrev = std::clamp(x - 1, 0, w - 1);
                    int xNext = std::clamp(x + 1, 0, w - 1);

                    for (int c = 0; c < 3; ++c) {
                        int centerVal = srcRowCurr[x * bpp + c];
                        int sumNeighbors =
                            srcRowPrev[xPrev * bpp + c] + srcRowPrev[x * bpp + c] + srcRowPrev[xNext * bpp + c] +
                            srcRowCurr[xPrev * bpp + c]                            + srcRowCurr[xNext * bpp + c] +
                            srcRowNext[xPrev * bpp + c] + srcRowNext[x * bpp + c] + srcRowNext[xNext * bpp + c];

                        int val = (centerVal * centerWeight - sumNeighbors * v + 128) >> 8;
                        dstRow[x * bpp + c] = static_cast<uchar>(std::clamp(val, 0, 255));
                    }
                }
            });
            break;
        }
        default:
            break;
        }

        if (result.isNull()) return {};

        if (!opacitySource.isNull() && opacitySource.size() == result.size()) {
            struct OpacityPoint {
                double x = 0.0;
                double y = 1.0;
                bool linear = true;
            };
            QList<OpacityPoint> points;
            points.reserve(opacityPointCount);
            for (int index = 0; index < opacityPointCount; ++index) {
                const QString prefix = QStringLiteral("_sylia.opacity.%1.")
                    .arg(index);
                points.append({
                    filter.params.value(prefix + QStringLiteral("x"), 0.0),
                    std::clamp(filter.params.value(
                        prefix + QStringLiteral("y"), 1.0), 0.0, 1.0),
                    filter.params.value(prefix + QStringLiteral("linear"), 1.0)
                        != 0.0
                });
            }
            std::sort(points.begin(), points.end(),
                      [](const OpacityPoint& left, const OpacityPoint& right) {
                          return left.x < right.x;
                      });
            const double position = context.frameNumber >= 0
                ? static_cast<double>(context.frameNumber) : 0.0;
            double opacity = points.first().y;
            if (position >= points.last().x) {
                opacity = points.last().y;
            } else {
                for (int index = 1; index < points.size(); ++index) {
                    if (position > points.at(index).x) continue;
                    const OpacityPoint& left = points.at(index - 1);
                    const OpacityPoint& right = points.at(index);
                    if (right.x > left.x && right.linear) {
                        const double fraction = std::clamp(
                            (position - left.x) / (right.x - left.x), 0.0, 1.0);
                        opacity = left.y + (right.y - left.y) * fraction;
                    } else {
                        opacity = left.y;
                    }
                    break;
                }
            }
            if (opacity < 1.0) {
                QImage blended = opacitySource;
                checkedImageBits(blended);
                QPainter painter;
                if (!painter.begin(&blended)) throw std::bad_alloc();
                const int opacityLeft = std::max(0, static_cast<int>(
                    filter.params.value(QStringLiteral(
                        "_sylia.opacityClip.left"), 0.0)));
                const int opacityTop = std::max(0, static_cast<int>(
                    filter.params.value(QStringLiteral(
                        "_sylia.opacityClip.top"), 0.0)));
                const int opacityRight = std::max(0, static_cast<int>(
                    filter.params.value(QStringLiteral(
                        "_sylia.opacityClip.right"), 0.0)));
                const int opacityBottom = std::max(0, static_cast<int>(
                    filter.params.value(QStringLiteral(
                        "_sylia.opacityClip.bottom"), 0.0)));
                if (opacityLeft || opacityTop || opacityRight || opacityBottom) {
                    painter.setClipRect(
                        opacityLeft, opacityTop,
                        std::max(0, blended.width() - opacityLeft - opacityRight),
                        std::max(0, blended.height() - opacityTop - opacityBottom));
                }
                painter.setOpacity(std::clamp(opacity, 0.0, 1.0));
                painter.drawImage(0, 0, result);
                painter.end();
                result = blended;
                if (result.isNull()) return {};
            }
        }
    }

    // QImage conversion/copy/painting paths do not uniformly preserve custom
    // text metadata. Keep sample shape with each immutable frame hand-off. The
    // Windows resize/crop/canvas contract retains SAR; quarter turns exchange
    // its axes. User display overrides never change this source-owned value.
    if (filter.type == VDFilterType::Rotate && filter.params.value("mode", 0) != 2) {
        std::swap(outputAspect.num, outputAspect.den);
    } else if (filter.type == VDFilterType::Rotate2) {
        const double angle = filter.params.value("angle", 0);
        if (angle == 90 || angle == -90 || angle == 270 || angle == -270)
            std::swap(outputAspect.num, outputAspect.den);
    }
    VDQtSetImageSampleAspectRatio(result, outputAspect);
    return result;
} catch (const std::bad_alloc&) {
    // QImage allocation normally returns a null image, while Qt container/task
    // bookkeeping can throw. Both mean this phase failed, including legacy
    // callers that consume processFrame() rather than processFrameSequence().
    resetRuntimeState();
    const VDFilterInstance *failedFilter = filterIndex >= 0 && filterIndex < mActiveChain.size()
        ? &mActiveChain.at(filterIndex) : nullptr;
    failProcessing(QStringLiteral("Not enough memory to process the filter frame."), failedFilter);
    return {};
}
