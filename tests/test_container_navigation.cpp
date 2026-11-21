// Container names alone do not establish picture order. Compare cold seeks,
// rewind, playback and adopted indexes against independently decoded BGRA hashes.
#include "VirtualDub/VDQtMediaRuntime.h"
#include "VirtualDub/VDQtVideoDecoder.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

namespace {
struct Case {
    const char *name;
    const char *extension;
    const char *codec;
    QStringList extra{};
    bool metadata = false;
    bool disposable = false;
    int frames = 300;
};

bool ffmpeg(const QStringList& arguments) {
    QProcess process;
    process.start(VDQtMediaRuntime::ffmpeg(), QStringList{
        "-nostdin", "-y", "-hide_banner", "-loglevel", "error"} + arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(45000)) {
        process.kill(); // Only this disposable fixture/oracle process.
        process.waitForFinished(5000);
        std::cerr << "Fixture/oracle timeout\n";
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode()) {
        std::cerr << process.readAllStandardError().constData();
        return false;
    }
    return true;
}

QByteArray pictureHash(const QImage& picture) {
    const QImage image = picture.convertToFormat(QImage::Format_ARGB32);
    if (image.isNull()) return {};
    QCryptographicHash hash(QCryptographicHash::Md5);
    for (int row = 0; row < image.height(); ++row)
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(row)),
                                   image.width() * 4));
    return hash.result().toHex();
}

bool markDisposablePictures(const QString& path) {
    // A generated I/P/I/P stream can safely make each P picture disposable:
    // the following I picture never references it. FLV1's first such picture
    // is not presented by a fresh decoder; a stale decoder used to emit it.
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)) return false;
    QByteArray bytes = file.readAll();
    const auto bit = [&](qsizetype position) {
        return (uchar(bytes.at(position / 8)) >> (7 - position % 8)) & 1;
    };
    int changed = 0;
    for (qsizetype at = 13; at + 15 <= bytes.size();) {
        const qsizetype length = (uchar(bytes.at(at + 1)) << 16)
            | (uchar(bytes.at(at + 2)) << 8) | uchar(bytes.at(at + 3));
        if (length > bytes.size() - at - 15) return false;
        if (uchar(bytes.at(at)) == 9 && length >= 10) {
            const qsizetype header = (at + 12) * 8;
            const int sizeFormat = (bit(header + 30) << 2) | (bit(header + 31) << 1)
                | bit(header + 32);
            const qsizetype type = header + 33 + (sizeFormat == 0 ? 16 : sizeFormat == 1 ? 32 : 0);
            if ((uchar(bytes.at(at + 11)) & 15) != 2 || type + 1 >= (at + 11 + length) * 8) return false;
            if (!bit(type) && bit(type + 1)) {
                bytes[type / 8] = char(uchar(bytes.at(type / 8)) | (1 << (7 - type % 8)));
                bytes[(type + 1) / 8] = char(uchar(bytes.at((type + 1) / 8)) & ~(1 << (7 - (type + 1) % 8)));
                bytes[at + 11] = '\x32';
                ++changed;
            }
        }
        at += 15 + length;
    }
    return changed == 150 && file.seek(0) && file.write(bytes) == bytes.size() && file.flush();
}

bool run(const Case& item, QTemporaryDir& directory) {
    const QString input = directory.filePath(QString("%1.%2").arg(item.name, item.extension));
    const QString oracle = directory.filePath(QString("%1.framemd5").arg(item.name));
    const QString codec = QString::fromLatin1(item.codec);
    QStringList arguments{"-f", "lavfi", "-i", "testsrc2=size=96x64:rate=30",
        "-frames:v", "300", "-c:v", codec, "-g", "30", "-threads", "1", "-an"};
    if (codec == "libx264" || codec == "libx265") arguments << "-preset" << "ultrafast";
    if (codec == "libx264") arguments << "-bf" << "3" << "-keyint_min" << "30"
        << "-sc_threshold" << "0";
    if (codec == "libx265") arguments << "-x265-params"
        << "pools=1:frame-threads=1:log-level=error:keyint=30:min-keyint=30:scenecut=0";
    if (!ffmpeg(arguments + item.extra + QStringList{input})
        || (item.disposable && !markDisposablePictures(input))
        || !ffmpeg({"-threads", "2", "-i", input, "-map", "0:v:0", "-an", "-sn",
                    "-sws_flags", "neighbor", "-fps_mode", "passthrough", "-pix_fmt", "bgra",
                    "-c:v", "rawvideo", "-threads:v", "1", "-f", "framemd5", oracle})) return false;
    QFile file(oracle);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QList<QByteArray> hashes;
    for (const auto& line : file.readAll().split('\n')) {
        if (!line.trimmed().isEmpty() && !line.startsWith('#'))
            hashes.append(line.split(',').last().trimmed());
    }
    const int frames = hashes.size();
    if (frames != (item.disposable ? 299 : item.frames)) return false;
    VDQtVideoDecoder decoder;
    // AVI may decode one calibration picture to establish FFmpeg's timestamp
    // origin. Navigation must never require a decoded traversal of the movie.
    if (!decoder.openFile(input) || decoder.getDecodedFrameCount() > (QString(item.extension) == "avi" ? 1 : 0)
        || (item.metadata && (!decoder.hasFrameNavigation() || decoder.getFrameCount() != frames))
        || (codec == "libxvid" && decoder.hasFrameNavigation())) return false;
    const bool metadata = decoder.hasFrameNavigation();
    QElapsedTimer timer;
    timer.start();
    bool correct = true;
    quint64 firstSeekDecoded = 0;
    for (int frame : {frames - 5, 1, frames - 1, frames / 2, 30, 0, 150, 29, 31}) {
        decoder.clearCache();
        decoder.resetPerformanceCounters();
        const bool match = pictureHash(decoder.getFrameImage(frame)) == hashes.at(frame);
        if (frame == frames - 5) firstSeekDecoded = decoder.getDecodedFrameCount();
        if (!match) {
            std::cerr << "FAIL: " << item.name << " frame " << frame << ": "
                      << decoder.getLastError().toStdString() << '\n';
            correct = false;
        }
    }
    // Playback after a rewind must retain the same first GOP as a fresh open.
    decoder.clearCache();
    for (int frame = 0; frame < 35; ++frame)
        correct = (pictureHash(decoder.getFrameImage(frame, true)) == hashes.at(frame)) && correct;
    VDQtVideoDecoder consumer;
    const auto snapshot = decoder.frameIndexSnapshot();
    if (!snapshot || !consumer.openFile(input, snapshot) || consumer.getDecodedFrameCount()
        || pictureHash(consumer.getFrameImage(frames - 1)) != hashes.at(frames - 1)
        || pictureHash(consumer.getFrameImage(0)) != hashes.at(0)) correct = false;
    if (codec == "huffyuv") {
        const auto health = decoder.scanVideoStream();
        consumer.close();
        correct = health.totalFrames == frames && health.keyFrames == frames
            && consumer.openFile(input, decoder.frameIndexSnapshot()) && consumer.getNextKeyFrame(0) == 1
            && pictureHash(consumer.getFrameImage(frames - 1)) == hashes.at(frames - 1) && correct;
    }
    if (metadata && firstSeekDecoded >= 100) correct = false;
    std::cout << item.name << ": metadata=" << metadata << ", first_seek_decoded="
              << firstSeekDecoded << ", ms=" << timer.elapsed() << ", accurate=" << correct << '\n';
    return correct;
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    VDQtVideoDecoder::setDecoderThreadCount(2);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const QList<Case> cases{
        {"mp4-h264", "mp4", "libx264", {}, true},
        {"mp4-hevc", "mp4", "libx265", {}, true},
        {"mp4-mpeg4", "mp4", "mpeg4", {"-bf", "2"}, true},
        {"mov-h264", "mov", "libx264", {}, true},
        {"3gp-h264", "3gp", "libx264", {}, true},
        {"mkv-hevc", "mkv", "libx265", {}, true},
        {"mkv-h264", "mkv", "libx264", {}, true},
        {"mkv-mjpeg", "mkv", "mjpeg", {}, true},
        {"mkv-ffv1", "mkv", "ffv1"},
        {"webm-vp8", "webm", "libvpx", {"-deadline", "realtime", "-cpu-used", "8"}},
        {"webm-vp9", "webm", "libvpx-vp9", {"-deadline", "realtime", "-cpu-used", "8"}},
        {"webm-av1", "webm", "libaom-av1", {"-cpu-used", "8", "-crf", "40"}},
        {"avi-mpeg4", "avi", "mpeg4", {"-bf", "2"}, true},
        {"avi-mpeg4-no-b", "avi", "mpeg4", {"-vtag", "DX50"}, true},
        {"avi-mjpeg", "avi", "mjpeg", {}, true},
        {"avi-ffv1", "avi", "ffv1", {}, true},
        {"avi-rawvideo", "avi", "rawvideo", {"-pix_fmt", "bgr0"}, true},
        {"avi-huffyuv", "avi", "huffyuv", {}, true},
        {"avi-xvid-packed", "avi", "libxvid", {"-bf", "2"}, false, false, 298},
        {"ts-h264", "ts", "libx264"},
        {"ts-hevc", "ts", "libx265"},
        {"ts-mpeg2", "ts", "mpeg2video", {"-bf", "2"}},
        {"m2ts-h264", "m2ts", "libx264", {"-mpegts_m2ts_mode", "1"}},
        {"mpg-mpeg1", "mpg", "mpeg1video", {"-bf", "2"}},
        {"mpg-mpeg2", "mpg", "mpeg2video", {"-bf", "2"}},
        {"vob-mpeg2", "vob", "mpeg2video", {"-bf", "2"}},
        {"flv-h264", "flv", "libx264"},
        {"flv-spark", "flv", "flv"},
        {"flv-disposable", "flv", "flv", {"-g", "2"}, false, true},
        {"ogv-theora", "ogv", "libtheora"},
        {"asf-wmv2", "asf", "wmv2", {}, true},
        {"nut-ffv1", "nut", "ffv1"},
        {"nut-rawvideo", "nut", "rawvideo", {"-pix_fmt", "bgr0"}},
        {"mxf-mpeg2", "mxf", "mpeg2video", {"-pix_fmt", "yuv422p", "-profile:v", "0",
                                           "-b:v", "5M", "-r", "25", "-g", "12"}}
    };
    bool correct = true;
    for (const auto& item : cases) correct = run(item, directory) && correct;
    return correct ? 0 : 1;
}
