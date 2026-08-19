#include "VDQtFilterSystem.h"
#include <QTransform>
#include <QUuid>
#include <QRgba64>
#include <QFuture>
#include <QPainter>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {

constexpr int kMaxSequencedBobFilters = 6;
constexpr qint64 kParallelFilterPixelThreshold = 256 * 1024;

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
    for (int task = 0; task < taskCount; ++task) {
        const int begin = count * task / taskCount;
        const int end = count * (task + 1) / taskCount;
        futures.append(QtConcurrent::run([begin, end, &function]() {
            for (int index = begin; index < end; ++index) function(index);
        }));
    }
    for (QFuture<void>& future : futures) future.waitForFinished();
}

template <typename Transform>
void transformRgbPixels(QImage& image, bool highPrecision, Transform&& transform) {
    const int width = image.width();
    const int height = image.height();
    uchar *bits = image.bits();
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

VDQtFilterSystem::VDQtFilterSystem() = default;

VDQtFilterSystem::~VDQtFilterSystem() = default;

void VDQtFilterSystem::clearFilters() {
    mActiveChain.clear();
    mSixAxisLutCache.clear();
}

void VDQtFilterSystem::replaceActiveChain(const QList<VDFilterInstance>& chain) {
    mActiveChain = chain;
    mSixAxisLutCache.clear();
}

VDQtFilterSystem& VDQtFilterSystem::instance() {
    static VDQtFilterSystem sys;
    return sys;
}

QList<VDQtFilterSystem::FilterInfo> VDQtFilterSystem::getAvailableFilters() const {
    return {
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
    };
}

void VDQtFilterSystem::addFilter(VDFilterType type) {
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
    case VDFilterType::Count:
        return;
    }

    mActiveChain.append(inst);
}

void VDQtFilterSystem::removeFilter(int index) {
    if (index >= 0 && index < mActiveChain.size()) {
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
    }
}

void VDQtFilterSystem::updateFilterParams(int index, const QMap<QString, double>& params) {
    if (index >= 0 && index < mActiveChain.size()) {
        mActiveChain[index].params = params;
        mSixAxisLutCache.clear();
    }
}

QImage VDQtFilterSystem::processFrame(const QImage& inputFrame) {
    // The historical API can return only one image. Use the first field phase
    // so preview remains deterministic; rate-aware pipelines must call
    // processFrameSequence() and consume every returned frame.
    return processFrameForPhase(inputFrame, 0);
}

VDFilterTimingInfo VDQtFilterSystem::getTimingInfo() const {
    int bobFilters = 0;
    for (const auto& filter : mActiveChain) {
        if (filter.enabled && filter.type == VDFilterType::BobDoubler)
            ++bobFilters;
    }

    if (bobFilters > kMaxSequencedBobFilters)
        return { 0, false };

    return { 1 << bobFilters, true };
}

bool VDQtFilterSystem::processFrameSequence(const QImage& inputFrame, QList<QImage>& outputFrames) {
    outputFrames.clear();

    const VDFilterTimingInfo timing = getTimingInfo();
    if (!timing.sequenceSupported)
        return false;

    outputFrames.reserve(timing.outputFramesPerInput);
    for (int phase = 0; phase < timing.outputFramesPerInput; ++phase)
        outputFrames.append(processFrameForPhase(inputFrame, static_cast<quint64>(phase)));

    return true;
}

QImage VDQtFilterSystem::processFrameForPhase(const QImage& inputFrame, quint64 bobPhaseMask) {
    if (inputFrame.isNull() || mActiveChain.isEmpty()) return inputFrame;

    const bool highPrecision = inputFrame.depth() > 32;

    QImage result = highPrecision
        ? inputFrame.convertToFormat(QImage::Format_RGBA64)
        : inputFrame;
    if (!highPrecision) {
        result = inputFrame.hasAlphaChannel()
            ? inputFrame.convertToFormat(QImage::Format_RGBA8888)
            : inputFrame.convertToFormat(QImage::Format_RGB888);
    }
    int bobFilterIndex = 0;

    for (const auto& filter : mActiveChain) {
        if (!filter.enabled) continue;

        switch (filter.type) {
        case VDFilterType::Resize: {
            int w = static_cast<int>(filter.params.value("width", result.width()));
            int h = static_cast<int>(filter.params.value("height", result.height()));
            if (w > 0 && h > 0) {
                const int filterMode = static_cast<int>(
                    filter.params.value("filterMode", 4));
                const Qt::TransformationMode transformation = filterMode == 0
                    ? Qt::FastTransformation : Qt::SmoothTransformation;
                const bool interlaced = filter.params.value("interlaced", 0) > 0.5;
                if (interlaced && result.height() > 1 && h > 1) {
                    const int evenSourceHeight = (result.height() + 1) / 2;
                    const int oddSourceHeight = result.height() / 2;
                    QImage even(result.width(), evenSourceHeight, result.format());
                    QImage odd(result.width(), std::max(1, oddSourceHeight), result.format());
                    for (int y = 0; y < result.height(); ++y) {
                        QImage& field = (y & 1) ? odd : even;
                        std::memcpy(field.scanLine(y / 2), result.constScanLine(y),
                                    static_cast<size_t>(std::min(
                                        field.bytesPerLine(), result.bytesPerLine())));
                    }
                    const int evenTargetHeight = (h + 1) / 2;
                    const int oddTargetHeight = h / 2;
                    even = even.scaled(w, evenTargetHeight,
                                       Qt::IgnoreAspectRatio, transformation);
                    odd = odd.scaled(w, std::max(1, oddTargetHeight),
                                     Qt::IgnoreAspectRatio, transformation);
                    QImage woven(w, h, result.format());
                    for (int y = 0; y < h; ++y) {
                        const QImage& field = (y & 1) ? odd : even;
                        std::memcpy(woven.scanLine(y), field.constScanLine(y / 2),
                                    static_cast<size_t>(std::min(
                                        woven.bytesPerLine(), field.bytesPerLine())));
                    }
                    result = woven;
                } else {
                    result = result.scaled(w, h, Qt::IgnoreAspectRatio,
                                           transformation);
                }

                const int framingMode = static_cast<int>(
                    filter.params.value("framingMode", 0));
                const QColor fillColor(
                    std::clamp(static_cast<int>(filter.params.value("fillColorR", 0)), 0, 255),
                    std::clamp(static_cast<int>(filter.params.value("fillColorG", 0)), 0, 255),
                    std::clamp(static_cast<int>(filter.params.value("fillColorB", 0)), 0, 255));
                if (framingMode == 1) {
                    const int frameWidth = std::max(1, static_cast<int>(
                        filter.params.value("frameW", result.width())));
                    const int frameHeight = std::max(1, static_cast<int>(
                        filter.params.value("frameH", result.height())));
                    QImage framed(frameWidth, frameHeight, result.format());
                    framed.fill(fillColor);
                    QPainter painter(&framed);
                    painter.drawImage((frameWidth - result.width()) / 2,
                                      (frameHeight - result.height()) / 2,
                                      result);
                    painter.end();
                    result = framed;
                } else if (framingMode == 2 || framingMode == 3) {
                    const double aspectWidth = std::max(
                        1.0, filter.params.value("frameAspectW", 4.0));
                    const double aspectHeight = std::max(
                        1.0, filter.params.value("frameAspectH", 3.0));
                    const double desiredAspect = aspectWidth / aspectHeight;
                    const double currentAspect = static_cast<double>(result.width())
                        / std::max(1, result.height());
                    if (framingMode == 2) {
                        int cropWidth = result.width();
                        int cropHeight = result.height();
                        if (currentAspect > desiredAspect)
                            cropWidth = std::max(1, static_cast<int>(std::llround(
                                result.height() * desiredAspect)));
                        else
                            cropHeight = std::max(1, static_cast<int>(std::llround(
                                result.width() / desiredAspect)));
                        result = result.copy((result.width() - cropWidth) / 2,
                                             (result.height() - cropHeight) / 2,
                                             cropWidth, cropHeight);
                    } else {
                        int frameWidth = result.width();
                        int frameHeight = result.height();
                        if (currentAspect < desiredAspect)
                            frameWidth = std::max(1, static_cast<int>(std::llround(
                                result.height() * desiredAspect)));
                        else
                            frameHeight = std::max(1, static_cast<int>(std::llround(
                                result.width() / desiredAspect)));
                        QImage framed(frameWidth, frameHeight, result.format());
                        framed.fill(fillColor);
                        QPainter painter(&framed);
                        painter.drawImage((frameWidth - result.width()) / 2,
                                          (frameHeight - result.height()) / 2,
                                          result);
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
            if ((bobPhaseMask >> bobFilterIndex) & 1U)
                retainedFieldIsOdd = !retainedFieldIsOdd;
            ++bobFilterIndex;

            int w = result.width();
            int h = result.height();

            // "None - double frames" deliberately duplicates the unmodified
            // input in both temporal phases.
            if (mode == 4 || w <= 0 || h <= 1)
                break;

            QImage temp = result;

            if (highPrecision) {
                uchar *resultBits = result.bits();
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
            uchar *resultBits = result.bits();
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
                uchar *resultBits = result.bits();
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
            auto cacheIt = mSixAxisLutCache.find(lutKey);
            if (cacheIt == mSixAxisLutCache.end()) {
                QByteArray lut(gridSize * gridSize * gridSize * 3,
                               Qt::Uninitialized);
                uchar *table = reinterpret_cast<uchar *>(lut.data());
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
                cacheIt = mSixAxisLutCache.insert(lutKey, std::move(lut));
            }
            const uchar *lut = reinterpret_cast<const uchar *>(cacheIt.value().constData());
            uchar *resultBits = result.bits();
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
                    const int rf = r == 255 ? 8 : (r & 7);
                    const int gf = g == 255 ? 8 : (g & 7);
                    const int bf = b == 255 ? 8 : (b & 7);
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
                        const int c00 = c000 * (8 - bf) + c001 * bf;
                        const int c01 = c010 * (8 - bf) + c011 * bf;
                        const int c10 = c100 * (8 - bf) + c101 * bf;
                        const int c11 = c110 * (8 - bf) + c111 * bf;
                        const int c0 = c00 * (8 - gf) + c01 * gf;
                        const int c1 = c10 * (8 - gf) + c11 * gf;
                        scan[x * bpp + channel] = static_cast<uchar>(
                            (c0 * (8 - rf) + c1 * rf + 256) >> 9);
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
                uchar *resultBits = result.bits();
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
            uchar *resultBits = result.bits();
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
                    const qint64 windowSize = static_cast<qint64>(radius) * 2 + 1;
                    const uchar *imageSourceBits = image.constBits();
                    const int imageSourceStride = image.bytesPerLine();
                    uchar *horizontalBits = horizontal.bits();
                    const int horizontalStride = horizontal.bytesPerLine();
                    parallelFor(h, static_cast<qint64>(w) * h, [&](int y) {
                        const QRgba64 *src = reinterpret_cast<const QRgba64 *>(
                            imageSourceBits + static_cast<qint64>(y) * imageSourceStride);
                        QRgba64 *dst = reinterpret_cast<QRgba64 *>(
                            horizontalBits + static_cast<qint64>(y) * horizontalStride);
                        for (int c = 0; c < 3; ++c) {
                            qint64 sum = 0;
                            for (int offset = -radius; offset <= radius; ++offset)
                                sum += rgba64Channel(src[std::clamp(offset, 0, w - 1)], c);
                            for (int x = 0; x < w; ++x) {
                                QRgba64 pixel = dst[x];
                                setRgba64Channel(pixel, c,
                                    static_cast<quint16>(sum / windowSize));
                                dst[x] = pixel;
                                const int left = std::clamp(x - radius, 0, w - 1);
                                const int right = std::clamp(x + radius + 1, 0, w - 1);
                                sum += static_cast<qint64>(rgba64Channel(src[right], c))
                                     - static_cast<qint64>(rgba64Channel(src[left], c));
                            }
                        }
                    });
                    const uchar *horizontalSourceBits = horizontal.constBits();
                    uchar *imageDestinationBits = image.bits();
                    const int imageDestinationStride = image.bytesPerLine();
                    parallelFor(w, static_cast<qint64>(w) * h, [&](int x) {
                        for (int c = 0; c < 3; ++c) {
                            qint64 sum = 0;
                            for (int offset = -radius; offset <= radius; ++offset) {
                                const int row = std::clamp(offset, 0, h - 1);
                                const QRgba64 *src = reinterpret_cast<const QRgba64 *>(
                                    horizontalSourceBits
                                        + static_cast<qint64>(row) * horizontalStride);
                                sum += rgba64Channel(src[x], c);
                            }
                            for (int y = 0; y < h; ++y) {
                                QRgba64 *dst = reinterpret_cast<QRgba64 *>(
                                    imageDestinationBits
                                        + static_cast<qint64>(y) * imageDestinationStride);
                                QRgba64 pixel = dst[x];
                                setRgba64Channel(pixel, c,
                                    static_cast<quint16>(sum / windowSize));
                                dst[x] = pixel;
                                const int top = std::clamp(y - radius, 0, h - 1);
                                const int bottom = std::clamp(y + radius + 1, 0, h - 1);
                                const QRgba64 *topRow = reinterpret_cast<const QRgba64 *>(
                                    horizontalSourceBits
                                        + static_cast<qint64>(top) * horizontalStride);
                                const QRgba64 *bottomRow = reinterpret_cast<const QRgba64 *>(
                                    horizontalSourceBits
                                        + static_cast<qint64>(bottom) * horizontalStride);
                                sum += static_cast<qint64>(rgba64Channel(bottomRow[x], c))
                                     - static_cast<qint64>(rgba64Channel(topRow[x], c));
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
                uchar *temporaryBits = temp.bits();
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
                uchar *imageDestinationBits = img.bits();
                const int imageDestinationStride = img.bytesPerLine();
                parallelFor(w, static_cast<qint64>(w) * h, [&](int x) {
                    for (int c = 0; c < 3; ++c) {
                        int sum = 0;
                        for (int y = -radius; y <= radius; ++y) {
                            int cy = std::clamp(y, 0, h - 1);
                            sum += temporarySourceBits[
                                static_cast<qint64>(cy) * temporaryStride
                                + x * bpp + c];
                        }
                        for (int y = 0; y < h; ++y) {
                            imageDestinationBits[
                                static_cast<qint64>(y) * imageDestinationStride
                                + x * bpp + c] = static_cast<uchar>(sum / winSize);
                            int ty = std::clamp(y - radius, 0, h - 1);
                            int by = std::clamp(y + radius + 1, 0, h - 1);
                            sum += temporarySourceBits[
                                       static_cast<qint64>(by) * temporaryStride
                                       + x * bpp + c]
                                 - temporarySourceBits[
                                       static_cast<qint64>(ty) * temporaryStride
                                       + x * bpp + c];
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
            uchar *destinationBits = result.bits();
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
            uchar *destinationBits = result.bits();
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
            uchar *destinationBits = result.bits();
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
        case VDFilterType::Levels: {
            const double inputBlack = std::clamp(
                filter.params.value("inputBlack", 0.0) / 255.0, 0.0, 1.0);
            const double inputWhite = std::clamp(
                filter.params.value("inputWhite", 255.0) / 255.0,
                inputBlack + 1.0 / 65535.0, 1.0);
            const double gamma = std::clamp(
                filter.params.value("gamma", 1.0), 0.05, 20.0);
            const double outputBlack = std::clamp(
                filter.params.value("outputBlack", 0.0) / 255.0, 0.0, 1.0);
            const double outputWhite = std::clamp(
                filter.params.value("outputWhite", 255.0) / 255.0,
                outputBlack, 1.0);
            const auto adjust = [=](double value) {
                const double normalized = std::clamp(
                    (value - inputBlack) / (inputWhite - inputBlack), 0.0, 1.0);
                return outputBlack + (outputWhite - outputBlack)
                    * std::pow(normalized, 1.0 / gamma);
            };
            transformRgbPixels(result, highPrecision,
                [&](double& red, double& green, double& blue) {
                    red = adjust(red); green = adjust(green); blue = adjust(blue);
                });
            break;
        }
        case VDFilterType::Gamma: {
            const double gamma = std::clamp(
                filter.params.value("gamma", 1.0), 0.05, 20.0);
            transformRgbPixels(result, highPrecision,
                [=](double& red, double& green, double& blue) {
                    red = std::pow(std::clamp(red, 0.0, 1.0), 1.0 / gamma);
                    green = std::pow(std::clamp(green, 0.0, 1.0), 1.0 / gamma);
                    blue = std::pow(std::clamp(blue, 0.0, 1.0), 1.0 / gamma);
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
            uchar *destinationBits = result.bits();
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
            const int left = std::max(0, static_cast<int>(
                std::llround(filter.params.value("left", 0.0))));
            const int top = std::max(0, static_cast<int>(
                std::llround(filter.params.value("top", 0.0))));
            const int right = std::max(0, static_cast<int>(
                std::llround(filter.params.value("right", 0.0))));
            const int bottom = std::max(0, static_cast<int>(
                std::llround(filter.params.value("bottom", 0.0))));
            const int width = result.width() - left - right;
            const int height = result.height() - top - bottom;
            if (width > 0 && height > 0)
                result = result.copy(left, top, width, height);
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
            uchar *destinationBits = result.bits();
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
            uchar *bits = result.bits();
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
                uchar *resultBits = result.bits();
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
            uchar *resultBits = result.bits();
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
                uchar *resultBits = result.bits();
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
            uchar *resultBits = result.bits();
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
    }

    return result;
}
