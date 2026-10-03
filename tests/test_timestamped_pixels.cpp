// A timestamped RAWVIDEO stream must identify its packed pixel layout, not just
// its codec. Compare every rendered sample, including alpha/high-bit-depth data,
// through a real VFR NUT pipe and lossless FFV1 output in owned temporary files.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtVideoExporter.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
QByteArray readBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QList<double> timestamps(const QString& path) {
    QProcess process;
    process.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_frames",
        "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", path});
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
        process.kill(); process.waitForFinished(5000); return {};
    }
    QList<double> result;
    for (const auto& frame : QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("frames").toArray()) {
        bool valid = false;
        const double time = frame.toObject().value("best_effort_timestamp_time").toString().toDouble(&valid);
        if (!valid) return {};
        result.append(time);
    }
    return result;
}

bool roundTrip(VDQtTestFixtures& fixtures, const QString& name,
               const QString& inputGraph, const QString& sourcePixelFormat,
               const QString& packedPixelFormat, QImage::Format imageFormat,
               int bytesPerPixel, bool hasAlpha, int depth) {
    const QString source = fixtures.directory.filePath(name + "-source.mkv");
    const QString output = fixtures.directory.filePath(name + "-output.mkv");
    const QString decoded = fixtures.directory.filePath(name + "-decoded.raw");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", inputGraph, "-frames:v", "8",
        "-vf", "select='lt(n,4)+gte(n,4)*not(mod(n,3))'", "-fps_mode", "vfr",
        "-pix_fmt", sourcePixelFormat, "-c:v", "ffv1", "-an", source})) return false;
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(source)) return false;
    const auto indexed = decoder.ensureFrameIndex();
    if (!check(indexed.totalFrames == 8 && decoder.hasCompleteFrameIndex(),
        "VFR sample fixture has exactly eight indexed pictures")) return false;
    const QImage first = decoder.getFrameImage(0);
    if (!check(decoder.sourceHasAlpha() == hasAlpha && first.depth() == depth,
        "fixture selects the intended packed exporter input layout")) return false;

    VDQtVideoExporter exporter;
    VDQtVideoExporter::ExportOptions request;
    request.inputPath = source;
    request.outputPath = output;
    request.endFrame = 7;
    request.includeAudio = false;
    request.unattended = true;
    request.containerType = "mkv";
    request.videoMode = VideoMode_FullProcessing;
    request.processing = VDQtVideoExporter::ProcessingSnapshot{};
    request.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
    request.processing->videoCodec.ffv1Slices = 4;
    request.processing->videoCodec.pixFmt = sourcePixelFormat;
    request.processing->videoCodec.colorMatrix = "auto";
    QByteArray expected;
    int framesSeen = 0;
    const bool exported = exporter.exportVideo(request, &decoder, nullptr, nullptr,
        [&](int, const QImage&, const QImage& rendered) {
            const QImage packed = rendered.convertToFormat(imageFormat);
            for (int row = 0; row < packed.height(); ++row)
                expected.append(reinterpret_cast<const char*>(packed.constScanLine(row)), packed.width() * bytesPerPixel);
            ++framesSeen;
        });
    if (!exported) std::cerr << exporter.lastError().toStdString() << '\n';
    if (!check(exported && framesSeen == 8,
        "VFR processed export renders every required picture")) return false;
    if (!fixtures.ffmpeg({"-i", output, "-pix_fmt", packedPixelFormat, "-fps_mode", "passthrough",
        "-f", "rawvideo", decoded})) return false;
    bool passed = check(readBytes(decoded) == expected,
        "timestamped NUT declares the packed layout and retains every color/alpha sample exactly");
    const auto sourceTimes = timestamps(source);
    const auto outputTimes = timestamps(output);
    passed &= check(sourceTimes.size() == 8 && sourceTimes == outputTimes,
        "pixel-layout correction leaves each VFR presentation timestamp unchanged");
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
    bool passed = roundTrip(fixtures, "rgb24", "testsrc=size=64x48:rate=24", "bgr0",
        "rgb24", QImage::Format_RGB888, 3, false, 24);
    passed &= roundTrip(fixtures, "rgba", "color=red@0.5:size=64x48:rate=24,format=rgba", "bgra",
        "rgba", QImage::Format_RGBA8888, 4, true, 32);
    passed &= roundTrip(fixtures, "rgba64", "color=blue@0.25:size=64x48:rate=24,format=rgba64le", "gbrap16le",
        "rgba64le", QImage::Format_RGBA64, 8, true, 64);
    if (!passed && !fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return passed ? 0 : 1;
}
