#include "VDQtVideoDecoder.h"

#include <array>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>

namespace {

bool runProcess(const QString& program, const QStringList& arguments)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        std::cerr << process.readAllStandardError().constData() << '\n';
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;

    const QString fixture = directory.filePath(QStringLiteral("playback_benchmark.mp4"));
    if (!runProcess(
            QStringLiteral("ffmpeg"),
            { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
              QStringLiteral("-f"), QStringLiteral("lavfi"),
              QStringLiteral("-i"), QStringLiteral("testsrc2=size=320x180:rate=60:duration=6"),
              QStringLiteral("-c:v"), QStringLiteral("libx264"),
              QStringLiteral("-preset"), QStringLiteral("ultrafast"),
              QStringLiteral("-g"), QStringLiteral("60"),
              QStringLiteral("-bf"), QStringLiteral("2"),
              QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
              QStringLiteral("-an"), QStringLiteral("-y"), fixture })) {
        return 1;
    }

    VDQtVideoDecoder decoder;
    if (!decoder.openFile(fixture)) {
        std::cerr << decoder.getLastError().toStdString() << '\n';
        return 1;
    }

    constexpr int sequentialFrames = 180;
    decoder.resetPerformanceCounters();
    QElapsedTimer sequentialTimer;
    sequentialTimer.start();
    for (int frame = 0; frame < sequentialFrames; ++frame) {
        if (decoder.getFrameImage(frame, true).isNull()) {
            std::cerr << "FAIL: sequential benchmark could not decode frame " << frame << '\n';
            return 1;
        }
    }
    const qint64 sequentialMs = sequentialTimer.elapsed();
    const double sequentialFps = sequentialMs > 0
        ? sequentialFrames * 1000.0 / sequentialMs
        : sequentialFrames * 1000.0;
    if (decoder.getSeekCount() != 0 || sequentialMs > 15000) {
        std::cerr << "FAIL: sequential playback regression (" << sequentialMs
                  << " ms, seeks=" << decoder.getSeekCount() << ")\n";
        return 1;
    }

    static constexpr std::array<int, 16> scrubTargets = {
        300, 25, 270, 50, 240, 75, 210, 100,
        330, 130, 290, 160, 350, 10, 200, 110
    };
    decoder.clearCache();
    decoder.resetPerformanceCounters();
    QElapsedTimer scrubTimer;
    scrubTimer.start();
    for (const int target : scrubTargets) {
        if (decoder.getFrameImage(target).isNull()) {
            std::cerr << "FAIL: scrub benchmark could not decode frame " << target << '\n';
            return 1;
        }
    }
    const qint64 scrubMs = scrubTimer.elapsed();
    const double averageScrubMs =
        static_cast<double>(scrubMs) / scrubTargets.size();
    if (scrubMs > 15000) {
        std::cerr << "FAIL: scrub latency regression (" << averageScrubMs
                  << " ms/request)\n";
        return 1;
    }

    std::cout << "playback benchmark: sequential=" << sequentialFps
              << " fps (" << sequentialMs << " ms), scrub-average="
              << averageScrubMs << " ms, scrub-seeks="
              << decoder.getSeekCount() << '\n';
    return 0;
}
