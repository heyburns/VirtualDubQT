#include "VDQtTestFixtures.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRgba64>

bool VDQtTestFixtures::writeText(const QString& path, const QByteArray& contents) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()
        || !file.flush()) {
        error = QStringLiteral("Cannot write fixture %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool VDQtTestFixtures::ffmpeg(const QStringList& arguments) {
    QProcess process;
    process.start(QStringLiteral("ffmpeg"),
                  QStringList{"-hide_banner", "-loglevel", "error", "-nostdin", "-y"}
                      + arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        // Only this test's subprocess can be stopped, never an application the
        // user has open. Bound fixture generation so a broken encoder cannot hang CI.
        process.kill();
        process.waitForFinished(5000);
        error = QStringLiteral("Fixture ffmpeg timed out: %1").arg(process.errorString());
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = QString::fromUtf8(process.readAllStandardError());
        return false;
    }
    return true;
}

bool VDQtTestFixtures::createBasic(int width, int height, int frames) {
    if (!directory.isValid()) {
        error = directory.errorString();
        return false;
    }
    mp4 = directory.filePath(QStringLiteral("source.mp4"));
    avs = directory.filePath(QStringLiteral("source.avs"));
    const QString pattern = QStringLiteral("testsrc2=size=%1x%2:rate=24")
        .arg(width).arg(height);
    if (!ffmpeg({"-f", "lavfi", "-i", pattern, "-frames:v", QString::number(frames),
                 "-c:v", "libx264", "-preset", "ultrafast", "-threads", "2",
                 "-g", "24", "-bf", "2", "-an", mp4})) return false;
    // Built-ins only: portable tests do not depend on a user's AVS plugins. This
    // exercises script/media alternation, not the user's particular plugin graph.
    return writeText(avs, QStringLiteral(
        "BlankClip(length=%1, width=%2, height=%3, fps=24, pixel_type=\"RGB24\", "
        "audio_rate=0, color=$204080)\n").arg(frames).arg(width).arg(height).toUtf8());
}

bool VDQtTestFixtures::createEdgeCases() {
    const QString source = QStringLiteral("testsrc2=size=96x64:rate=24:duration=1");
    const auto make = [&](const QString& name, const QStringList& arguments) {
        const QString path = directory.filePath(name);
        if (!ffmpeg(arguments + QStringList{path})) return false;
        edgeCaseMedia.append(path);
        return true;
    };
    if (!make("vfr.mkv", {"-f", "lavfi", "-i", source, "-vf",
              "select='if(lt(n,12),1,not(mod(n,3)))'", "-fps_mode", "vfr",
              "-c:v", "ffv1", "-an"})
        || !make("unknown-count.h264", {"-f", "lavfi", "-i", source,
              "-c:v", "libx264", "-preset", "ultrafast", "-threads", "2",
              "-f", "h264", "-an"})
        || !make("duplicate-pts.mkv", {"-f", "lavfi", "-i", source,
              "-vf", "setpts=floor(N/2)/(24*TB)", "-fps_mode", "passthrough",
              "-c:v", "ffv1", "-an"})
        || !make("long-audio.mkv", {"-f", "lavfi", "-i", source,
              "-f", "lavfi", "-i", "sine=frequency=440:duration=2:sample_rate=48000",
              "-c:v", "ffv1", "-c:a", "pcm_s16le"})
        || !make("short-audio.mkv", {"-f", "lavfi", "-i", source,
              "-f", "lavfi", "-i", "sine=frequency=440:duration=0.25:sample_rate=48000",
              "-c:v", "ffv1", "-c:a", "pcm_s16le"})
        || !make("high-depth.mkv", {"-f", "lavfi", "-i", source,
              "-pix_fmt", "yuv420p10le", "-c:v", "ffv1", "-an"})) return false;

    for (int frame = 0; frame < 3; ++frame) {
        const QString path = directory.filePath(QStringLiteral("image-%1.png")
            .arg(frame, 3, 10, QLatin1Char('0')));
        if (!patternedImage(97, 65, QImage::Format_RGBA8888, frame).save(path)) {
            error = QStringLiteral("Cannot save PNG fixture");
            return false;
        }
    }
    // A valid manifest can have an arbitrary filename. Nested manifests and
    // unusable destinations are retained for later source-safety/rollback tests.
    return writeText(directory.filePath("inner.ffconcat"),
                     "ffconcat version 1.0\nfile 'source.mp4'\n")
        && writeText(directory.filePath("manifest.txt"),
                     "ffconcat version 1.0\nfile 'inner.ffconcat'\n")
        && writeText(directory.filePath("not-a-directory"), "regular file\n");
}

qint64 VDQtTestFixtures::diskBytes() const {
    qint64 bytes = 0;
    QDirIterator files(directory.path(), QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        bytes += files.fileInfo().size();
    }
    return bytes;
}

QImage VDQtTestFixtures::patternedImage(int width, int height,
                                      QImage::Format format, int frame) {
    QImage image(width, height, format);
    uchar* bits = image.bits();
    if (!bits) return {};
    const qsizetype stride = image.bytesPerLine();
    for (int y = 0; y < height; ++y) {
        uchar* row = bits + y * stride;
        for (int x = 0; x < width; ++x) {
            const int r = (x * 7 + y * 11 + frame * 3) & 255;
            const int g = (x * 13 + y * 3 + frame * 5) & 255;
            const int b = (x * 3 + y * 17 + frame * 7) & 255;
            const int a = (x + y * 5) & 255;
            if (format == QImage::Format_RGBA64) {
                reinterpret_cast<QRgba64*>(row)[x] = QRgba64::fromRgba64(
                    r * 257, g * 257, b * 257, a * 257);
            } else {
                const int channels = format == QImage::Format_RGB888 ? 3 : 4;
                row[x * channels] = static_cast<uchar>(r);
                row[x * channels + 1] = static_cast<uchar>(g);
                row[x * channels + 2] = static_cast<uchar>(b);
                if (channels == 4) row[x * channels + 3] = static_cast<uchar>(a);
            }
        }
    }
    return image;
}
