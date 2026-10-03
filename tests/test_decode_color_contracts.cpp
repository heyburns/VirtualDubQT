// Declared source matrices/ranges take precedence over resolution guesses.
// Independent ffmpeg conversions establish pixel references; no user media.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtVideoDecoder.h"

#include <QCoreApplication>
#include <QFile>
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
QByteArray reference(VDQtTestFixtures& fixtures, int width, int height,
                     const QString& matrix, bool fullRange) {
    const QString input = fixtures.directory.filePath("source.yuv");
    const QString output = fixtures.directory.filePath("reference.rgb");
    const int pixels = width * height;
    const QByteArray planes = QByteArray(pixels, char(96)) + QByteArray(pixels, char(90))
        + QByteArray(pixels, char(180));
    if (!fixtures.writeText(input, planes) || !fixtures.ffmpeg({"-f", "rawvideo",
        "-pixel_format", "yuv444p", "-video_size", QString("%1x%2").arg(width).arg(height),
        "-i", input, "-vf", QString("scale=in_color_matrix=%1:in_range=%2:out_range=pc:flags=fast_bilinear")
            .arg(matrix, fullRange ? "pc" : "tv"), "-pix_fmt", "rgb24",
        "-frames:v", "1", "-f", "rawvideo", output})) return {};
    return readBytes(output);
}
bool matches(const QImage& frame, const QByteArray& expected) {
    if (frame.isNull() || expected.size() != frame.width() * frame.height() * 3) return false;
    const auto rgb = frame.convertToFormat(QImage::Format_RGB888);
    for (int row = 0; row < rgb.height(); ++row)
        for (int byte = 0; byte < rgb.width() * 3; ++byte)
            if (std::abs(int(rgb.constScanLine(row)[byte])
                - int(static_cast<unsigned char>(expected.at(row * rgb.width() * 3 + byte)))) > 1)
            {
                const QColor color = rgb.pixelColor(0, 0);
                std::cerr << "Pixel mismatch: actual=" << color.red() << ',' << color.green() << ',' << color.blue()
                    << " expected=" << int(static_cast<unsigned char>(expected.at(0))) << ','
                    << int(static_cast<unsigned char>(expected.at(1))) << ','
                    << int(static_cast<unsigned char>(expected.at(2))) << '\n';
                return false;
            }
    return true;
}
bool ordinarySources(VDQtTestFixtures& fixtures) {
    bool passed = true;
    for (const auto& item : {std::make_pair(QString("bt709"), QSize(64, 48)),
                            std::make_pair(QString("bt601"), QSize(1280, 720)),
                            std::make_pair(QString("bt2020"), QSize(64, 48))}) {
        const int width = item.second.width(), height = item.second.height();
        const auto expected = reference(fixtures, width, height, item.first, false);
        const QString input = fixtures.directory.filePath("source.yuv");
        const QString source = fixtures.directory.filePath(item.first + ".mkv");
        const QString tag = item.first == "bt601" ? QString("smpte170m")
            : item.first == "bt2020" ? QString("bt2020nc") : item.first;
        if (expected.isEmpty() || !fixtures.ffmpeg({"-f", "rawvideo", "-pixel_format", "yuv444p",
            "-video_size", QString("%1x%2").arg(width).arg(height), "-i", input,
            "-vf", QString("setparams=range=limited:colorspace=%1").arg(tag),
            "-c:v", "ffv1", "-colorspace", tag, "-color_range", "tv", "-frames:v", "1", source})) return false;
        VDQtVideoDecoder decoder;
        passed &= check(decoder.openFile(source) && matches(decoder.getFrameImage(0), expected),
                        "automatic RGB conversion follows the declared matrix, not resolution");
        decoder.setDecompressionConfig("Autoselect", 1, 1);
        passed &= check(matches(decoder.getFrameImage(0), reference(fixtures, width, height, "bt601", false)),
                        "explicit matrix/range overrides still supersede source properties");
    }
    return passed;
}
bool nativeFrameProperties(VDQtTestFixtures& fixtures) {
    const QString source = fixtures.directory.filePath("frame-properties.avs");
    if (!fixtures.writeText(source,
        "a=BlankClip(length=1,width=64,height=48,pixel_type=\"YV24\",audio_rate=0,color_yuv=$605ab4)\n"
        "a.propSet(\"_Matrix\",1).propSet(\"_ColorRange\",1) ++ "
        "a.propSet(\"_Matrix\",6).propSet(\"_ColorRange\",1) ++ "
        "a.propSet(\"_Matrix\",1).propSet(\"_ColorRange\",0)\n")) return false;
    VDQtVideoDecoder decoder;
    if (!check(decoder.openFile(source), "native frame-property fixture opens")) return false;
    const auto limited709 = reference(fixtures, 64, 48, "bt709", false);
    const auto limited601 = reference(fixtures, 64, 48, "bt601", false);
    const auto full709 = reference(fixtures, 64, 48, "bt709", true);
    bool passed = check(matches(decoder.getFrameImage(0), limited709), "native _Matrix is honored");
    passed &= check(matches(decoder.getFrameImage(1), limited601), "matrix changes reconfigure conversion");
    passed &= check(matches(decoder.getFrameImage(2), full709), "native _ColorRange changes reconfigure conversion");
    decoder.setDecompressionConfig("Autoselect", 1, 1);
    passed &= check(matches(decoder.getFrameImage(2), limited601), "user overrides beat native frame properties");
    decoder.setDecompressionConfig("Autoselect", 0, 0);
    passed &= check(matches(decoder.getFrameImage(0), limited709), "resetting overrides restores per-frame metadata");
    return passed;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    VDQtTestFixtures fixtures;
    bool passed = ordinarySources(fixtures);
    passed &= nativeFrameProperties(fixtures);
    if (!fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return passed ? 0 : 1;
}
