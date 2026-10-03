// Genuine QImage detach/allocation failure in a disposable Linux process.
// This is not a codec test and must never run under ASan, whose shadow address
// space is incompatible with RLIMIT_AS. Limit is restored before comparison,
// diagnostics and recovery, and cannot affect any other application process.
#include "VirtualDub/VDQtFilterSystem.h"

#include <QGuiApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QThreadPool>
#include <algorithm>
#include <iostream>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
const QMap<QString, VDFilterType> filters = {
    {"sixaxis", VDFilterType::SixAxis}, {"bob", VDFilterType::BobDoubler},
    {"resize", VDFilterType::Resize}, {"rotate", VDFilterType::Rotate},
    {"fliph", VDFilterType::FlipHorizontal}, {"flipv", VDFilterType::FlipVertical},
    {"brightness", VDFilterType::BrightnessContrast}, {"grayscale", VDFilterType::Grayscale},
    {"invert", VDFilterType::InvertColor}, {"blur", VDFilterType::Blur},
    {"sharpen", VDFilterType::Sharpen}, {"deinterlace", VDFilterType::Deinterlace},
    {"emboss", VDFilterType::Emboss}, {"fieldswap", VDFilterType::FieldSwap},
    {"hsv", VDFilterType::HSVAdjust}, {"levels", VDFilterType::Levels},
    {"threshold", VDFilterType::Threshold}, {"posterize", VDFilterType::Posterize},
    {"gamma", VDFilterType::Gamma}, {"smoother", VDFilterType::Smoother},
    {"crop", VDFilterType::Crop}, {"chromashift", VDFilterType::ChromaShift},
    {"pixelate", VDFilterType::Pixelate}, {"fill", VDFilterType::Fill},
    {"canvas", VDFilterType::Canvas}, {"curves", VDFilterType::Curves},
    {"chromasmoother", VDFilterType::ChromaSmoother}, {"fielddelay", VDFilterType::FieldDelay},
    {"gammacorrect", VDFilterType::GammaCorrect}, {"interlace", VDFilterType::Interlace},
    {"interpolate", VDFilterType::Interpolate}, {"motion", VDFilterType::MotionBlur},
    {"temporal", VDFilterType::TemporalSmoother}, {"ivtc", VDFilterType::InverseTelecine},
    {"television", VDFilterType::Television}, {"warpsharp", VDFilterType::WarpSharp},
    {"drawtext", VDFilterType::DrawText}, {"drawtime", VDFilterType::DrawTime},
    {"perspective", VDFilterType::Perspective}, {"reduce", VDFilterType::Reduce2},
    {"reducehq", VDFilterType::Reduce2HQ}, {"rotate2", VDFilterType::Rotate2},
    {"warpresize", VDFilterType::WarpResize}, {"logo", VDFilterType::Logo},
    {"convert", VDFilterType::ConvertFormat},
    {"resize_interlaced", VDFilterType::Resize},
    {"resize_framed", VDFilterType::Resize},
    {"rotate2_cropped", VDFilterType::Rotate2},
    {"opacity", VDFilterType::NullTransform},
    {"clipping", VDFilterType::NullTransform}
};
bool exercise(const QString& name, QImage::Format format) {
#ifdef __linux__
    VDQtFilterSystem pipeline;
    const VDFilterType type = filters.value(name);
    pipeline.addFilter(type);
    auto parameters = pipeline.getActiveChain().first().params;
    if (type == VDFilterType::Resize || type == VDFilterType::WarpResize) {
        parameters["width"] = 1920; parameters["height"] = 1080;
        if (type == VDFilterType::Resize) parameters["sizeMode"] = 0;
    } else if (type == VDFilterType::SixAxis) parameters["saturation"] = 1.2;
    else if (type == VDFilterType::BrightnessContrast) parameters["bright"] = 10;
    else if (type == VDFilterType::HSVAdjust) parameters["hueDegrees"] = 17;
    else if (type == VDFilterType::Levels || type == VDFilterType::Curves || type == VDFilterType::Gamma) parameters["gamma"] = 1.7;
    else if (type == VDFilterType::ChromaShift) parameters["x"] = 1;
    else if (type == VDFilterType::Crop) parameters["left"] = 1;
    else if (type == VDFilterType::Perspective) parameters["topLeftX"] = 0.1;
    else if (type == VDFilterType::Rotate2) parameters["angle"] = 13;
    else if (type == VDFilterType::ConvertFormat) parameters["format"] = format == QImage::Format_RGBA64 ? 0 : 2;
    if (name == "resize_interlaced") parameters["interlaced"] = 1;
    else if (name == "resize_framed") {
        parameters["framingMode"] = 1;
        parameters["frameW"] = 2048;
        parameters["frameH"] = 1200;
    } else if (name == "rotate2_cropped") parameters["expand"] = 0;
    else if (name == "opacity") {
        parameters["_sylia.opacity.count"] = 1;
        parameters["_sylia.opacity.0.y"] = 0.5;
    } else if (name == "clipping") parameters["_sylia.clip.left"] = 1;
    pipeline.updateFilterParams(0, parameters);
    QTemporaryDir assets;
    if (!assets.isValid()) return false;
    if (type == VDFilterType::Logo) {
        QImage logo(8, 8, QImage::Format_RGBA8888);
        logo.fill(Qt::red);
        const QString path = assets.filePath("owned-logo.png");
        if (!logo.save(path)) return false;
        pipeline.updateFilterStringParams(0, {{"path", path}});
    }
    QImage previous(3841, 2160, format);
    previous.fill(QColor(88, 139, 202, 180));
    // Warm the task pool, scalar tables/assets and temporal stage before imposing
    // the limit. History-dependent filters then genuinely try to mutate frame 1.
    if (!check(!previous.isNull() && !pipeline.processFrame(previous, {0, 0, 25}).isNull(),
               "warm valid filter before allocator failure")) return false;
    QImage input(previous.size(), format);
    input.fill(QColor(92, 140, 205, 180));
    const QImage snapshot = input.copy();
    if (input.isNull() || snapshot.isNull()) return false;
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) return false;
    bool valid = false;
    const auto pages = statm.readAll().simplified().split(' ').first().toULongLong(&valid);
    const long pageSize = sysconf(_SC_PAGESIZE);
    struct rlimit saved;
    if (!valid || pageSize <= 0 || getrlimit(RLIMIT_AS, &saved) != 0) return false;
    struct rlimit limited = saved;
    limited.rlim_cur = std::min(saved.rlim_cur, rlim_t(pages * pageSize + 1024 * 1024));
    if (setrlimit(RLIMIT_AS, &limited) != 0) return false;
    QList<QImage> outputs;
    const bool processed = pipeline.processFrameSequence(input, outputs, {1, 1.0 / 25, 25});
    const bool restored = setrlimit(RLIMIT_AS, &saved) == 0;
    if (!check(restored, "restore disposable test-process allocation budget")
        || !check(!processed && outputs.isEmpty() && !pipeline.lastError().isEmpty(),
                  "failed allocation returns an empty failed sequence with a diagnostic")
        || !check(input == snapshot, "failed allocation preserves caller-owned image")) return false;
    return check(pipeline.processFrameSequence(input, outputs, {2, 2.0 / 25, 25})
                 && !outputs.isEmpty() && !outputs.first().isNull(), "normal processing recovers after restoring allocations");
#else
    (void)name; (void)format;
    return false;
#endif
}
}

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    if (argc != 3 || !filters.contains(QString::fromLocal8Bit(argv[1]))) return 2;
    const QString depth = QString::fromLocal8Bit(argv[2]);
    if (depth != "8" && depth != "16") return 2;
    QThreadPool::globalInstance()->setMaxThreadCount(8);
    return exercise(QString::fromLocal8Bit(argv[1]), depth == "16" ? QImage::Format_RGBA64 : QImage::Format_RGB888) ? 0 : 1;
}
