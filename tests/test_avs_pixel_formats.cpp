// Built-in AVS clips exercise native plane layout, precision and alpha. The
// raw exporter must see the same image as preview; no external codec suite.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtVideoExporter.h"

#include <QApplication>
#include <QFile>
#include <QRgba64>
#include <QRegularExpression>
#include <iostream>

namespace {
bool check(bool condition, const QString& message) {
    if (!condition) std::cerr << "FAIL: " << message.toStdString() << '\n';
    return condition;
}
bool formats(VDQtTestFixtures& fixtures) {
    QStringList names{"Y8", "YV12", "YV16", "YV24", "YV411", "YUY2", "RGB24", "RGB32", "RGB48", "RGB64",
                      "RGBP", "RGBAP", "YUVA420", "YUVA422", "YUVA444"};
    for (int bits : {10, 12, 14, 16})
        for (const QString& kind : {QString("Y"), QString("YUV420P"), QString("YUV422P"), QString("YUV444P"),
                                   QString("RGBP"), QString("RGBAP"), QString("YUVA420P"), QString("YUVA422P"), QString("YUVA444P")})
            names << kind + QString::number(bits);
    bool passed = true;
    for (const QString& name : names) {
        const QSize dimensions = name == "YUV420P10" ? QSize(648, 362)
            : name == "RGBAP14" ? QSize(97, 65) : QSize(64, 48);
        const QString path = fixtures.directory.filePath(name + ".avs");
        const bool yuv = name.startsWith('Y');
        const bool alpha = name.startsWith("YUVA") || name.startsWith("RGBAP")
            || name == "RGB32" || name == "RGB64";
        const auto depthMatch = QRegularExpression("(10|12|14|16)$").match(name);
        const int bits = name == "RGB48" || name == "RGB64" ? 16
            : !name.startsWith("YV") && depthMatch.hasMatch() ? depthMatch.captured(1).toInt() : 8;
        const int scale = 1 << (bits - 8);
        const int detail = bits > 8 ? 1 : 0;
        QStringList channels;
        channels << QString::number((yuv ? 96 : 64) * scale + detail);
        if (!QRegularExpression("^Y(8|10|12|14|16)$").match(name).hasMatch())
            channels << QString::number((yuv ? 128 : 32) * scale + detail)
                     << QString::number(128 * scale + (yuv ? 0 : detail));
        if (alpha) channels << QString::number(128 * scale);
        const QByteArray script = QString("BlankClip(length=2,width=%1,height=%2,pixel_type=\"%3\",audio_rate=0,colors=[%4])\n")
            .arg(dimensions.width()).arg(dimensions.height()).arg(name, channels.join(',')).toUtf8();
        if (!fixtures.writeText(path, script)) return false;
        VDQtVideoDecoder decoder;
        const bool opened = decoder.openFile(path);
        passed &= check(opened, name + " opens: " + decoder.getLastError());
        if (!opened) continue;
        const auto image = decoder.getFrameImage(0);
        passed &= check(!image.isNull(), name + " renders: " + decoder.getLastError());
        if (image.isNull()) continue;
        passed &= check((decoder.getSourceBitDepth() <= 8 || image.depth() == 64)
            && image.hasAlphaChannel() == (alpha || decoder.getSourceBitDepth() > 8),
            name + " keeps its source precision/alpha storage contract");
        const QColor color = image.pixelColor(0, 0);
        if (yuv)
            passed &= check(std::abs(color.red() - color.green()) <= 1 && std::abs(color.green() - color.blue()) <= 1,
                            name + " maps neutral chroma/gray planes correctly");
        else
            passed &= check(std::abs(color.red() - 64) <= 2 && std::abs(color.green() - 32) <= 2
                && std::abs(color.blue() - 128) <= 2, name + " maps RGB plane order correctly");
        if (alpha)
            passed &= check(std::abs(color.alpha() - 128) <= 1, name + " retains straight alpha");
        if (decoder.getSourceBitDepth() > 8 && !yuv) {
            // These values are not multiples of 257, so an unnoticed 8-bit
            // conversion cannot pass simply because the display looks right.
            const auto *pixels = reinterpret_cast<const QRgba64*>(image.constScanLine(0));
            passed &= check(pixels[0].red() % 257 != 0 || pixels[0].green() % 257 != 0
                || pixels[0].blue() % 257 != 0, name + " is not secretly quantized through RGB8");
        }
        VDQtVideoExporter exporter;
        VDQtVideoExporter::RawExportOptions options;
        options.inputPath = path;
        options.outputPath = fixtures.directory.filePath(name + ".raw");
        options.endFrame = 0;
        options.pixelFormat = image.depth() > 32 ? "rgba64le" : "rgba";
        options.scanlineAlignment = 1;
        options.unattended = true;
        options.processing = VDQtVideoExporter::ProcessingSnapshot{};
        const bool exported = exporter.exportRawVideo(options, &decoder);
        const auto expected = image.convertToFormat(image.depth() > 32
            ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
        QFile output(options.outputPath);
        passed &= check(exported && output.open(QIODevice::ReadOnly)
            && output.readAll() == QByteArray(reinterpret_cast<const char*>(expected.constBits()), expected.sizeInBytes()),
            name + " raw export matches preview at its full precision");
    }
    for (const QString& name : {QString("Y32"), QString("YUV420PS"), QString("RGBPS")}) {
        const QString path = fixtures.directory.filePath(name + ".avs");
        if (!fixtures.writeText(path, QString("BlankClip(length=1,width=64,height=48,pixel_type=\"%1\",audio_rate=0)\n")
            .arg(name).toUtf8())) return false;
        VDQtVideoDecoder decoder;
        passed &= check(!decoder.openFile(path) && decoder.getLastError().contains("float", Qt::CaseInsensitive),
                        name + " rejects unsupported float precision at open, not a null preview later");
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
    const bool passed = formats(fixtures);
    if (!fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return passed ? 0 : 1;
}
