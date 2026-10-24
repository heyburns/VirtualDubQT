// Reproduce MPEG-2 bottom-row damage without storing any user/copyrighted video.
// Extra zero stuffing at a picture boundary is ignored by ordinary decoding,
// but AV_EF_BITSTREAM makes FFmpeg conceal the entire last macroblock row.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtVideoDecoder.h"
#include "VirtualDub/VDQtVideoExporter.h"

#include <QApplication>
#include <QFile>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace {
constexpr int width = 96, height = 64, frames = 20;
constexpr int frameBytes = width * height * 3;

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
QByteArray readBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray withPictureStuffing(const QByteArray& source) {
    const QByteArray prefix("\0\0\1", 3);
    QByteArray result;
    qsizetype copied = 0;
    bool wasSlice = false;
    for (qsizetype pos = source.indexOf(prefix); pos >= 0 && pos + 3 < source.size();
         pos = source.indexOf(prefix, pos + 4)) {
        const auto code = static_cast<unsigned char>(source.at(pos + 3));
        const bool isSlice = code >= 1 && code <= 0xaf;
        result.append(source.constData() + copied, pos - copied);
        if (wasSlice && !isSlice) result.append(QByteArray(3, '\0'));
        copied = pos;
        wasSlice = isSlice;
    }
    result.append(source.constData() + copied, source.size() - copied);
    if (wasSlice) result.append(QByteArray(3, '\0'));
    return result;
}

bool referenceDecode(VDQtTestFixtures& fixtures, const QString& input, const QString& output) {
    return fixtures.ffmpeg({"-i", input, "-an", "-sws_flags", "fast_bilinear",
        "-pix_fmt", "rgb24", "-fps_mode", "passthrough", "-c:v", "rawvideo",
        "-threads", "1", "-f", "rawvideo", output});
}

bool matches(const QImage& decoded, const QByteArray& reference, int frame) {
    const auto image = decoded.convertToFormat(QImage::Format_RGB888);
    if (image.size() != QSize(width, height) || frame < 0
        || reference.size() < (frame + 1) * frameBytes) return false;
    for (int row = 0; row < height; ++row) {
        if (std::memcmp(image.constScanLine(row),
                reference.constData() + frame * frameBytes + row * width * 3, width * 3)) {
            std::cerr << "First different row=" << row << " frame=" << frame << '\n';
            return false;
        }
    }
    return true;
}

bool playbackAndSeeking(const QString& input, const QByteArray& reference) {
    for (int mode : {0, 1, 2}) {
        VDQtVideoDecoder decoder;
        decoder.setErrorMode(mode);
        if (!check(decoder.openFile(input), "padded MPEG-2 opens in every error mode")) return false;
        for (int frame = 0; frame < frames; ++frame)
            if (!check(matches(decoder.getFrameImage(frame, true), reference, frame),
                       "sequential decoding retains every row, including bottom macroblocks")) return false;

        const auto scan = decoder.ensureFrameIndex();
        if (!check(scan.totalFrames == frames && scan.badFrames == 0 && !scan.cancelled
                   && scan.errorMessage.isEmpty() && decoder.hasCompleteFrameIndex(),
                   "ordinary picture stuffing is not counted as damaged video")) return false;
        for (int frame : {19, 2, 11, 0, 18}) {
            decoder.clearCache();
            if (!check(matches(decoder.getFrameImage(frame), reference, frame),
                       "indexed forward/backward seeks retain the bottom row")) return false;
        }

        // Exercise the already-open context, not just the pre-open policy.
        // Returning to mode 0 used to reinstate aggressive bitstream checking.
        decoder.setErrorMode(1);
        decoder.setErrorMode(0);
        for (int frame : {0, 12, 3}) {
            decoder.clearCache();
            if (!check(matches(decoder.getFrameImage(frame), reference, frame),
                       "changing back to normal error reporting does not corrupt pixels")) return false;
        }
        if (!check(decoder.openFile(input)
                   && matches(decoder.getFrameImage(0), reference, 0),
                   "reopening applies the same safe normal policy")) return false;
    }
    return true;
}

bool processedExports(VDQtTestFixtures& fixtures, const QString& input, const QByteArray& reference) {
    for (int mode : {VideoMode_FullProcessing, VideoMode_NormalRecompress}) {
        VDQtVideoDecoder decoder;
        if (!decoder.openFile(input)) return false;
        VDQtVideoExporter exporter;
        VDQtVideoExporter::ExportOptions request;
        request.inputPath = input;
        request.outputPath = fixtures.directory.filePath(QString("export-%1.mkv").arg(mode));
        request.endFrame = frames - 1;
        request.includeAudio = false;
        request.unattended = true;
        request.videoMode = mode;
        request.containerType = "mkv";
        request.processing = VDQtVideoExporter::ProcessingSnapshot{};
        request.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
        // Preserve the decoder's RGB exactly; a YUV round-trip would introduce
        // unrelated chroma-subsampling/rounding differences in this comparison.
        request.processing->videoCodec.pixFmt = "bgr0";
        if (!check(exporter.exportVideo(request, &decoder), "processed MPEG-2 export succeeds")) {
            std::cerr << exporter.lastError().toStdString() << '\n';
            return false;
        }
        const QString raw = fixtures.directory.filePath(QString("export-%1.rgb").arg(mode));
        if (!referenceDecode(fixtures, request.outputPath, raw)
            || !check(readBytes(raw) == reference, "processed exports preserve every reference pixel")) return false;
    }
    return true;
}

bool genuineFailures(VDQtTestFixtures& fixtures, const QByteArray& encoded) {
    const QString invalid = fixtures.directory.filePath("invalid.m2v");
    if (!fixtures.writeText(invalid, "not a video stream")) return false;
    for (int mode : {0, 1, 2}) {
        VDQtVideoDecoder decoder;
        decoder.setErrorMode(mode);
        if (!check(!decoder.openFile(invalid) && !decoder.getLastError().isEmpty(),
                   "unreadable input still produces an error in every mode")) return false;
    }

    // Unlike harmless trailing stuffing, destroy the encoded contents of the
    // first slice. Normal decoding must still flag actual picture damage.
    QByteArray damaged = encoded;
    const qsizetype slice = damaged.indexOf(QByteArray("\0\0\1\1", 4));
    const qsizetype next = slice < 0 ? -1 : damaged.indexOf(QByteArray("\0\0\1", 3), slice + 4);
    if (!check(slice >= 0 && next > slice + 16, "generated fixture has a writable first slice")) return false;
    std::fill(damaged.begin() + slice + 8, damaged.begin() + next, char(0xff));
    const QString corrupt = fixtures.directory.filePath("damaged.m2v");
    if (!fixtures.writeText(corrupt, damaged)) return false;
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(corrupt)) return !decoder.getLastError().isEmpty();
    const auto scan = decoder.scanVideoStream();
    return check(scan.badFrames > 0 || !scan.errorMessage.isEmpty(),
                 "normal policy still reports genuinely corrupted MPEG-2 pictures");
}
}

int main(int argc, char** argv) {
    VDQtTestFixtures fixtures;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", fixtures.directory.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", fixtures.directory.filePath("data").toUtf8());
    QApplication app(argc, argv);
    const QString clean = fixtures.directory.filePath("clean.m2v");
    const QString padded = fixtures.directory.filePath("padded.m2v");
    const QString cleanRgb = fixtures.directory.filePath("clean.rgb");
    const QString paddedRgb = fixtures.directory.filePath("padded.rgb");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=96x64:rate=25",
            "-frames:v", QString::number(frames), "-c:v", "mpeg2video", "-g", "8",
            "-bf", "2", "-q:v", "3", "-threads", "1", "-f", "mpeg2video", clean})) return 1;
    const QByteArray original = readBytes(clean);
    const QByteArray stuffed = withPictureStuffing(original);
    if (!check(!original.isEmpty() && stuffed.size() > original.size(), "picture stuffing was added")
        || !fixtures.writeText(padded, stuffed) || !referenceDecode(fixtures, clean, cleanRgb)
        || !referenceDecode(fixtures, padded, paddedRgb)) return 1;
    const QByteArray reference = readBytes(cleanRgb);
    if (!check(reference.size() == frames * frameBytes && readBytes(paddedRgb) == reference,
               "independent normal decoding confirms stuffing leaves every pixel unchanged")) return 1;
    const bool passed = playbackAndSeeking(padded, reference)
        && processedExports(fixtures, padded, reference) && genuineFailures(fixtures, original);
    if (!fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return passed ? 0 : 1;
}
