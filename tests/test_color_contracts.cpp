// Exercise actual conversions/encoded files, not just generated option strings.
// Media, exports and settings are disposable; no user configuration is loaded.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtProjectFile.h"
#include "VirtualDub/VDQtColorPolicy.h"
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

QJsonObject probe(const QString& path) {
    QProcess process;
    process.start("ffprobe", {"-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=color_space,color_transfer,color_primaries",
        "-of", "json", path});
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(5000);
        return {};
    }
    const auto streams = QJsonDocument::fromJson(process.readAllStandardOutput()).object()
        .value("streams").toArray();
    return streams.isEmpty() ? QJsonObject() : streams.first().toObject();
}

bool exportColor(VDQtTestFixtures& fixtures) {
    VDQtColorMatrixInfo info;
    bool passed = check(VDQtResolveColorMatrix("bt2020", 12, &info)
        && info.matrix == "bt2020nc" && info.primaries == "bt2020"
        && info.transfer == "bt2020-12"
        && VDQtResolveColorMatrix("bt601", 8, &info) && info.matrix == "smpte170m"
        && VDQtResolveColorMatrix("bt709", 8, &info) && info.matrix == "bt709"
        && !VDQtResolveColorMatrix("nonsense", 8),
        "color-family aliases and 12-bit transfer mapping have explicit domains");
    if (!fixtures.createBasic(64, 48, 2)) return false;
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(fixtures.avs)) return false;
    const QImage image = decoder.getFrameImage(0).convertToFormat(QImage::Format_RGB888);
    QByteArray rgb;
    for (int row = 0; row < image.height(); ++row)
        rgb.append(reinterpret_cast<const char*>(image.constScanLine(row)), image.width() * 3);
    const QString rgbPath = fixtures.directory.filePath("rgb.raw");
    if (!fixtures.writeText(rgbPath, rgb)) return false;

    VDQtVideoExporter exporter;
    VDQtVideoExporter::RawExportOptions raw;
    raw.inputPath = fixtures.avs;
    raw.outputPath = fixtures.directory.filePath("raw-2020.yuv");
    raw.endFrame = 0;
    raw.pixelFormat = "yuv444p";
    raw.swapChromaPlanes = false;
    raw.scanlineAlignment = 1;
    raw.colorMatrix = "bt2020";
    raw.unattended = true;
    raw.processing = VDQtVideoExporter::ProcessingSnapshot{};
    const QString reference = fixtures.directory.filePath("reference.yuv");
    if (!fixtures.ffmpeg({"-f", "rawvideo", "-pixel_format", "rgb24",
        "-video_size", "64x48", "-i", rgbPath,
        "-vf", "scale=out_color_matrix=bt2020:out_range=tv:flags=bicubic",
        "-pix_fmt", "yuv444p", "-frames:v", "1", "-f", "rawvideo", reference})) return false;
    const bool rawOk = exporter.exportRawVideo(raw, &decoder);
    passed &= check(rawOk && readBytes(raw.outputPath) == readBytes(reference),
                    "raw BT.2020 uses BT.2020 coefficients, not Rec.601");

    VDQtProcessingState settings;
    settings.rawVideo.colorMatrix = "bt2020";
    settings.videoCodec.colorMatrix = "bt2020nc";
    const QString settingsPath = fixtures.directory.filePath("color.vdsettings");
    VDQtProcessingState loaded;
    QString error;
    passed &= check(VDQtProjectFile::saveProcessingSettings(settingsPath, settings, &error)
        && VDQtProjectFile::loadProcessingSettings(settingsPath, &loaded, &error)
        && loaded.rawVideo.colorMatrix == "bt2020"
        && loaded.videoCodec.colorMatrix == "bt2020nc",
        "BT.2020 selections survive settings save/reload");

    VDQtVideoExporter::ExportOptions encoded;
    encoded.inputPath = fixtures.avs;
    encoded.outputPath = fixtures.directory.filePath("encoded-2020.mkv");
    encoded.endFrame = 0;
    encoded.includeAudio = false;
    encoded.containerType = "mkv";
    encoded.unattended = true;
    encoded.processing = VDQtVideoExporter::ProcessingSnapshot{};
    encoded.processing->videoCodec.codecId = "ffv1";
    encoded.processing->videoCodec.pixFmt = "yuv444p10le";
    encoded.processing->videoCodec.ffv1Slices = 4;
    encoded.processing->videoCodec.colorMatrix = "bt2020nc";
    const bool encodedOk = exporter.exportVideo(encoded, &decoder);
    passed &= check(encodedOk, "processed BT.2020 export uses valid independent color tags");
    if (encodedOk) {
        const auto tags = probe(encoded.outputPath);
        std::cout << "Encoded color tags: " << QJsonDocument(tags).toJson(QJsonDocument::Compact).constData() << '\n';
        passed &= check(tags.value("color_space") == "bt2020nc"
            && tags.value("color_primaries") == "bt2020"
            && tags.value("color_transfer") == "bt2020-10",
            "encoded BT.2020 matrix/primaries/transfer tags match their separate domains");
        const QString encodedPixels = fixtures.directory.filePath("encoded.yuv");
        const QString highReference = fixtures.directory.filePath("reference-10.yuv");
        passed &= check(fixtures.ffmpeg({"-i", encoded.outputPath, "-pix_fmt", "yuv444p10le",
                "-frames:v", "1", "-f", "rawvideo", encodedPixels})
            && fixtures.ffmpeg({"-f", "rawvideo", "-pixel_format", "rgb24",
                "-video_size", "64x48", "-i", rgbPath,
                "-vf", "scale=out_color_matrix=bt2020:out_range=tv:flags=bicubic",
                "-pix_fmt", "yuv444p10le", "-frames:v", "1", "-f", "rawvideo", highReference})
            && readBytes(encodedPixels) == readBytes(highReference),
            "processed export converts pixels with the selected matrix, not just tags");
    }
    return passed;
}
}

int main(int argc, char **argv) {
    VDQtTestFixtures fixtures;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", fixtures.directory.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", fixtures.directory.filePath("data").toUtf8());
    QApplication application(argc, argv);
    const bool passed = exportColor(fixtures);
    if (!fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return passed ? 0 : 1;
}
