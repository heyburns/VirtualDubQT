#include "VDQtFilterSystem.h"
#include <QTransform>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxSequencedBobFilters = 6;

} // namespace

VDQtFilterSystem::VDQtFilterSystem() = default;

VDQtFilterSystem::~VDQtFilterSystem() = default;

void VDQtFilterSystem::clearFilters() {
    mActiveChain.clear();
}

void VDQtFilterSystem::replaceActiveChain(const QList<VDFilterInstance>& chain) {
    mActiveChain = chain;
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
    if (highPrecision) {
        // Qt's geometric operations retain 16-bit channel data. The legacy
        // pixel filters below are byte-oriented; reject those combinations
        // instead of silently quantizing a 10/12/16-bit source to RGB24.
        for (const auto& filter : mActiveChain) {
            if (!filter.enabled) continue;
            if (filter.type != VDFilterType::Resize
                && filter.type != VDFilterType::Rotate
                && filter.type != VDFilterType::FlipHorizontal
                && filter.type != VDFilterType::FlipVertical
                && filter.type != VDFilterType::InvertColor) {
                return QImage();
            }
        }
    }

    QImage result = inputFrame;
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
                result = result.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
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

            if (result.depth() > 32) return QImage();

            int bpp = (result.format() == QImage::Format_RGB888) ? 3 : 4;
            int w = result.width();
            int h = result.height();

            // "None - double frames" deliberately duplicates the unmodified
            // input in both temporal phases.
            if (mode == 4 || w <= 0 || h <= 1)
                break;

            QImage temp = result;

            for (int y = 0; y < h; ++y) {
                const bool scanIsOdd = (y & 1) != 0;
                if (scanIsOdd == retainedFieldIsOdd) {
                    memcpy(result.scanLine(y), temp.constScanLine(y), w * bpp);
                } else {
                    uchar *dst = result.scanLine(y);
                    int previousLine = y - 1;
                    int nextLine = y + 1;

                    if (previousLine < 0)
                        previousLine = nextLine < h ? nextLine : y;
                    if (nextLine >= h)
                        nextLine = previousLine >= 0 ? previousLine : y;

                    const uchar *src1 = temp.constScanLine(previousLine);
                    const uchar *src2 = temp.constScanLine(nextLine);

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
            }
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

            if (result.depth() > 32) return QImage();

            int bpp = (result.format() == QImage::Format_RGB888) ? 3 : 4;
            int h = result.height();
            int w = result.width();

            const float axesAngles[6] = { 0.0f, 30.0f, 90.0f, 180.0f, 240.0f, 300.0f };
            const float axesGains[6] = { redGain, orangeGain, limeGain, emeraldGain, blueGain, purpleGain };

            for (int y = 0; y < h; ++y) {
                uchar *scan = result.scanLine(y);
                for (int x = 0; x < w; ++x) {
                    float r = scan[x * bpp + 0] / 255.0f;
                    float g = scan[x * bpp + 1] / 255.0f;
                    float b = scan[x * bpp + 2] / 255.0f;

                    float cmax = std::max(r, std::max(g, b));
                    float cmin = std::min(r, std::min(g, b));
                    float delta = cmax - cmin;

                    float H = 0.0f;
                    float S = (cmax > 1e-5f) ? (delta / cmax) : 0.0f;
                    float V = cmax;

                    if (delta > 1e-5f) {
                        if (cmax == r) {
                            H = 60.0f * std::fmod(((g - b) / delta) + 6.0f, 6.0f);
                        } else if (cmax == g) {
                            H = 60.0f * (((b - r) / delta) + 2.0f);
                        } else {
                            H = 60.0f * (((r - g) / delta) + 4.0f);
                        }
                    }

                    float axisMod = 0.0f;
                    for (int k = 0; k < 6; ++k) {
                        float diff = std::abs(H - axesAngles[k]);
                        if (diff > 180.0f) diff = 360.0f - diff;
                        if (diff < 60.0f) {
                            float weight = 1.0f - (diff / 60.0f);
                            axisMod += weight * (axesGains[k] - 1.0f);
                        }
                    }

                    float newS = std::clamp(S * satGlobal * (1.0f + axisMod), 0.0f, 1.0f);

                    float C = V * newS;
                    float X = C * (1.0f - std::abs(std::fmod(H / 60.0f, 2.0f) - 1.0f));
                    float m = V - C;

                    float nr = 0.0f, ng = 0.0f, nb = 0.0f;
                    if (H < 60.0f)       { nr = C; ng = X; nb = 0.0f; }
                    else if (H < 120.0f) { nr = X; ng = C; nb = 0.0f; }
                    else if (H < 180.0f) { nr = 0.0f; ng = C; nb = X; }
                    else if (H < 240.0f) { nr = 0.0f; ng = X; nb = C; }
                    else if (H < 300.0f) { nr = X; ng = 0.0f; nb = C; }
                    else                 { nr = C; ng = 0.0f; nb = X; }

                    nr = (nr + m) * intensity + redGreen * 0.15f + yellowBlue * 0.08f;
                    ng = (ng + m) * intensity - redGreen * 0.15f + yellowBlue * 0.08f;
                    nb = (nb + m) * intensity - yellowBlue * 0.16f;

                    scan[x * bpp + 0] = static_cast<uchar>(std::clamp(std::round(nr * 255.0f), 0.0f, 255.0f));
                    scan[x * bpp + 1] = static_cast<uchar>(std::clamp(std::round(ng * 255.0f), 0.0f, 255.0f));
                    scan[x * bpp + 2] = static_cast<uchar>(std::clamp(std::round(nb * 255.0f), 0.0f, 255.0f));
                }
            }
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

            if (result.depth() > 32) return QImage();

            int bytesPerPixel = (result.format() == QImage::Format_RGB888) ? 3 : 4;
            int h = result.height();
            int w = result.width();

            for (int y = 0; y < h; ++y) {
                uchar *scan = result.scanLine(y);
                for (int x = 0; x < w * bytesPerPixel; ++x) {
                    if (bytesPerPixel == 4 && (x % 4 == 3)) continue; // skip alpha
                    scan[x] = table[scan[x]];
                }
            }
            break;
        }
        case VDFilterType::Blur: {
            int width = static_cast<int>(filter.params.value("width", 1));
            int power = static_cast<int>(filter.params.value("power", 1));
            if (width <= 0) width = 1;
            if (power < 1) power = 1;
            if (power > 3) power = 3;

            if (result.depth() > 32) return QImage();

            int bpp = (result.format() == QImage::Format_RGB888) ? 3 : 4;
            int w = result.width();
            int h = result.height();

            auto boxBlurPass = [bpp, w, h](QImage &img, int radius) {
                if (radius <= 0 || w <= 0 || h <= 0) return;
                QImage temp = img;
                int winSize = 2 * radius + 1;

                // Horizontal pass
                for (int y = 0; y < h; ++y) {
                    const uchar *srcRow = img.constScanLine(y);
                    uchar *dstRow = temp.scanLine(y);
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
                }

                // Vertical pass
                for (int x = 0; x < w; ++x) {
                    for (int c = 0; c < 3; ++c) {
                        int sum = 0;
                        for (int y = -radius; y <= radius; ++y) {
                            int cy = std::clamp(y, 0, h - 1);
                            sum += temp.constScanLine(cy)[x * bpp + c];
                        }
                        for (int y = 0; y < h; ++y) {
                            img.scanLine(y)[x * bpp + c] = static_cast<uchar>(sum / winSize);
                            int ty = std::clamp(y - radius, 0, h - 1);
                            int by = std::clamp(y + radius + 1, 0, h - 1);
                            sum += temp.constScanLine(by)[x * bpp + c] - temp.constScanLine(ty)[x * bpp + c];
                        }
                    }
                }
            };

            for (int i = 0; i < power; ++i) {
                boxBlurPass(result, width);
            }
            break;
        }
        case VDFilterType::Grayscale: {
            if (result.depth() > 32) return QImage();
            const int bpp = result.format() == QImage::Format_RGB888 ? 3 : 4;
            for (int y = 0; y < result.height(); y++) {
                uchar *scan = result.scanLine(y);
                for (int x = 0; x < result.width(); x++) {
                    int r = scan[x * bpp];
                    int g = scan[x * bpp + 1];
                    int b = scan[x * bpp + 2];
                    uchar gray = static_cast<uchar>(0.299 * r + 0.587 * g + 0.114 * b);
                    scan[x * bpp] = gray;
                    scan[x * bpp + 1] = gray;
                    scan[x * bpp + 2] = gray;
                }
            }
            break;
        }
        case VDFilterType::InvertColor: {
            result.invertPixels(QImage::InvertRgb);
            break;
        }
        case VDFilterType::Sharpen: {
            int v = static_cast<int>(filter.params.value("amount", 16));
            if (v <= 0) break;

            if (result.depth() > 32) return QImage();

            int bpp = (result.format() == QImage::Format_RGB888) ? 3 : 4;
            int w = result.width();
            int h = result.height();

            QImage temp = result;
            int centerWeight = 256 + 8 * v;

            for (int y = 0; y < h; ++y) {
                uchar *dstRow = result.scanLine(y);
                int yPrev = std::clamp(y - 1, 0, h - 1);
                int yNext = std::clamp(y + 1, 0, h - 1);

                const uchar *srcRowPrev = temp.constScanLine(yPrev);
                const uchar *srcRowCurr = temp.constScanLine(y);
                const uchar *srcRowNext = temp.constScanLine(yNext);

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
            }
            break;
        }
        default:
            break;
        }
    }

    return result;
}
