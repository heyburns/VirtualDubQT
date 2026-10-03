// Pixel-shape metadata follows decoded images, not mutable display settings.
// All fixtures/exports live in one owned temporary directory; no user plugins.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtVideoAspect.h"
#include "VirtualDub/VDQtVideoDecoder.h"
#include "VirtualDub/VDQtVideoDisplay.h"
#include "VirtualDub/VDQtVideoExporter.h"
#include "VirtualDub/VDQtFilterSystem.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <limits>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool hasAspect(const QImage& image, int numerator, int denominator) {
    return !image.isNull() && av_cmp_q(VDQtImageSampleAspectRatio(image), AVRational{numerator, denominator}) == 0;
}
QByteArray bytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QJsonObject probe(const QString& path) {
    QProcess process;
    process.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-count_frames",
        "-show_entries", "stream=sample_aspect_ratio,display_aspect_ratio,width,height,nb_read_frames", "-of", "json", path});
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
        process.kill(); process.waitForFinished(5000); return {};
    }
    const auto streams = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("streams").toArray();
    return streams.isEmpty() ? QJsonObject{} : streams.first().toObject();
}
VDFilterInstance filter(VDFilterType type, QMap<QString, double> params = {}) {
    VDFilterInstance result;
    result.id = QString::number(static_cast<int>(type));
    result.name = "Aspect fixture";
    result.type = type;
    result.params = std::move(params);
    return result;
}

bool metadataAndDisplay() {
    bool passed = true;
    QImage source(64, 48, QImage::Format_RGB888);
    source.fill(Qt::white);
    VDQtSetImageSampleAspectRatio(source, {2, 1});
    QImage alias = source;
    const uchar *before = source.constBits();
    VDQtSetImageSampleAspectRatio(source, {4, 2});
    passed &= check(source.constBits() == before && source.constBits() == alias.constBits(),
        "reapplying equivalent aspect metadata does not detach pixel storage");
    passed &= check(hasAspect(source, 2, 1)
        && av_cmp_q(VDQtNormalizedSampleAspectRatio(-1, 0), AVRational{1, 1}) == 0,
        "positive rational metadata reduces exactly and malformed values default to square pixels");
    VDVideoDisplayWidget display("Aspect");
    display.resize(100, 100);
    display.setZoomLevel(0.5);
    display.setFrameImage(source);
    display.show();
    QApplication::processEvents();
    const QImage wide = display.grab().toImage();
    passed &= check(wide.pixelColor(20, 50) == QColor(Qt::white),
        "Source display mode uses the carried 2:1 pixel shape");
    display.setAspectRatioMode(VDVideoDisplayWidget::AspectRatioMode::PixelSquare);
    const QImage square = display.grab().toImage();
    passed &= check(square.pixelColor(20, 50) != QColor(Qt::white) && hasAspect(display.frameImage(), 2, 1),
        "manual square-pixel display override changes presentation only");
    display.setZoomLevel(std::numeric_limits<double>::quiet_NaN());
    passed &= check(display.zoomLevel() < 0, "non-finite display zoom becomes safe auto sizing");
    display.setAspectRatioMode(VDVideoDisplayWidget::AspectRatioMode::PixelSource);
    QImage extreme = source;
    VDQtSetImageSampleAspectRatio(extreme, {std::numeric_limits<int>::max(), 1});
    display.setFrameImage(extreme);
    passed &= check(!display.grab().isNull(), "extreme positive source aspect remains safely drawable");
    return passed;
}

bool decoderAndFilters(VDQtTestFixtures& fixtures, const QString& sourcePath) {
    bool passed = true;
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(sourcePath)) return false;
    QImage frame = decoder.getFrameImage(0);
    passed &= check(av_cmp_q(decoder.getSampleAspectRatio(), AVRational{2, 1}) == 0
        && hasAspect(frame, 2, 1) && hasAspect(decoder.getFrameImage(1), 2, 1)
        && hasAspect(decoder.getFrameImage(0), 2, 1), "FFmpeg stream aspect survives decoding and cache revisits");
    const QList<VDFilterInstance> stages = {
        filter(VDFilterType::InvertColor), filter(VDFilterType::Crop, {{"left", 2}, {"right", 2}}),
        filter(VDFilterType::Canvas, {{"width", 80}, {"height", 60}}),
        filter(VDFilterType::Resize, {{"width", 96}, {"height", 24}, {"aspectMode", 0}}),
        filter(VDFilterType::Rotate, {{"mode", 1}})};
    VDQtFilterSystem system;
    QList<VDFilterInstance> prefix;
    for (const auto& stage : stages) {
        prefix.append(stage);
        system.replaceActiveChainTransient(prefix);
        const QImage output = system.processFrame(frame);
        passed &= check(hasAspect(output, stage.type == VDFilterType::Rotate ? 1 : 2,
            stage.type == VDFilterType::Rotate ? 2 : 1),
            "color/crop/pad/resize preserve SAR and quarter-turn rotation reciprocates it");
    }
    for (double angle : {90., -270., 180., 89.999}) {
        system.replaceActiveChainTransient({filter(VDFilterType::Rotate2, {{"angle", angle}, {"expand", 0}})});
        const bool quarter = angle == 90 || angle == -270;
        passed &= check(hasAspect(system.processFrame(frame), quarter ? 1 : 2, quarter ? 2 : 1),
            "only exact odd quarter-turn Rotate2 operations reciprocate pixel shape");
    }
    system.replaceActiveChainTransient({filter(VDFilterType::Rotate,
        {{"mode", 1}, {"_sylia.range.start", 2}, {"_sylia.range.end", 3}})});
    VDFilterFrameContext context;
    context.frameNumber = 0;
    passed &= check(hasAspect(system.processFrame(frame, context), 2, 1),
        "out-of-range geometry filters do not change metadata");

    const QString avs = fixtures.directory.filePath("aspect.avs");
    if (!fixtures.writeText(avs,
        "a=BlankClip(length=1,width=64,height=48,fps=24,pixel_type=\"RGB24\",audio_rate=0)\n"
        "a.propSet(\"_SARNum\",8).propSet(\"_SARDen\",9) ++ "
        "a.propSet(\"_SARNum\",9).propSet(\"_SARDen\",8)\n")) return false;
    decoder.close();
    if (!decoder.openFile(avs)) return false;
    passed &= check(hasAspect(decoder.getFrameImage(0), 8, 9) && hasAspect(decoder.getFrameImage(1), 9, 8)
        && hasAspect(decoder.getFrameImage(0), 8, 9), "native AVS per-frame SAR stays attached to each cached image");
    decoder.close();
    passed &= check(av_cmp_q(decoder.getSampleAspectRatio(), AVRational{1, 1}) == 0,
        "Close clears source aspect fallback");
    return passed;
}

bool exportAspects(VDQtTestFixtures& fixtures, const QString& sourcePath) {
    bool passed = true;
    VDQtVideoExporter exporter;
    VDQtVideoExporter::ExportOptions request;
    request.inputPath = sourcePath;
    request.endFrame = 7;
    request.includeAudio = false;
    request.unattended = true;
    request.containerType = "mkv";
    request.processing = VDQtVideoExporter::ProcessingSnapshot{};
    request.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
    request.processing->videoCodec.ffv1Slices = 4;
    request.processing->videoCodec.colorMatrix = "bt709";
    for (int mode : {VideoMode_FullProcessing, VideoMode_NormalRecompress, VideoMode_FastRecompress}) {
        request.videoMode = mode;
        request.outputPath = fixtures.directory.filePath(QString("aspect-export-%1.mkv").arg(mode));
        const bool ok = exporter.exportVideo(request);
        if (!ok) std::cerr << exporter.lastError().toStdString() << '\n';
        passed &= check(ok && probe(request.outputPath).value("sample_aspect_ratio") == "2:1"
            && probe(request.outputPath).value("nb_read_frames") == "8",
            "Full/Normal/Fast export retains source SAR alongside explicit color conversion");
    }
    request.videoMode = VideoMode_FullProcessing;
    request.processing->filters = {filter(VDFilterType::Rotate, {{"mode", 1}})};
    request.outputPath = fixtures.directory.filePath("rotated-aspect.mkv");
    passed &= check(exporter.exportVideo(request)
        && probe(request.outputPath).value("sample_aspect_ratio") == "1:2"
        && probe(request.outputPath).value("width") == 48, "rotated processed output signals reciprocal SAR");
    request.processing->filters.clear();
    if (VDQtCodecEngine::instance().checkVideoEncoderAvailable("libx264")) {
        request.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("libx264");
        request.processing->videoCodec.rateMode = "bitrate";
        request.processing->videoCodec.targetBitrateKbps = 500;
        request.processing->videoCodec.twoPass = true;
        request.outputPath = fixtures.directory.filePath("two-pass-aspect.mkv");
        passed &= check(exporter.exportVideo(request)
            && probe(request.outputPath).value("sample_aspect_ratio") == "2:1",
            "both two-pass stages retain SAR after temporary-storage preflight");
    }
    request.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
    request.processing->videoCodec.ffv1Slices = 4;
    request.inputPath = fixtures.directory.filePath("aspect.avs");
    request.endFrame = 1;
    request.outputPath = fixtures.directory.filePath("existing-aspect-output.mkv");
    if (!fixtures.writeText(request.outputPath, "existing output")) return false;
    // AVS dependencies can be computed dynamically, so the general source
    // safety contract protects an existing destination before any rendering.
    passed &= check(!exporter.exportVideo(request)
        && bytes(request.outputPath) == "existing output", "AVS source safety still protects an existing output");
    request.outputPath = fixtures.directory.filePath("changing-aspect-output.mkv");
    passed &= check(!exporter.exportVideo(request) && exporter.lastError().contains("aspect ratio changes")
        && !QFileInfo::exists(request.outputPath), "changing per-frame SAR fails visibly and leaves no partial destination");
    return passed;
}
}

int main(int argc, char **argv) {
    VDQtTestFixtures fixtures;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", fixtures.directory.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", fixtures.directory.filePath("data").toUtf8());
    QApplication app(argc, argv);
    const QString source = fixtures.directory.filePath("non-square.mkv");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=64x48:rate=24", "-frames:v", "8",
        "-vf", "setsar=2/1", "-c:v", "ffv1", "-an", source})) return 1;
    bool passed = metadataAndDisplay();
    passed &= decoderAndFilters(fixtures, source);
    passed &= exportAspects(fixtures, source);
    if (!passed && !fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return passed ? 0 : 1;
}
