#include "VDQtAudioPlayer.h"

#include <cmath>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMediaDevices>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

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
    // Device enumeration is deliberately opt-in: headless CI hosts often have
    // PulseAudio/PipeWire client libraries but no reachable server, which can
    // block inside platform discovery. Run this on a desktop with:
    // VD_RUN_REAL_AUDIO_TESTS=1 ctest -R audio_backend_tests -V
    if (qEnvironmentVariableIntValue("VD_RUN_REAL_AUDIO_TESTS") != 1) {
        std::cout << "SKIP: set VD_RUN_REAL_AUDIO_TESTS=1 to exercise the real audio backend\n";
        return 77;
    }

    qunsetenv("VD_DISABLE_AUDIO_OUTPUT");
    QCoreApplication application(argc, argv);
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        std::cout << "SKIP: no PipeWire/PulseAudio output device is available\n";
        return 77;
    }

    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const QString fixture = directory.filePath(QStringLiteral("backend_tone.wav"));
    if (!runProcess(
            QStringLiteral("ffmpeg"),
            { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
              QStringLiteral("-f"), QStringLiteral("lavfi"),
              QStringLiteral("-i"), QStringLiteral("sine=frequency=523:sample_rate=48000:duration=3"),
              QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
              QStringLiteral("-ac"), QStringLiteral("2"),
              QStringLiteral("-y"), fixture })) {
        return 1;
    }

    VDQtAudioPlayer player;
    if (!player.openFile(fixture) || !player.hasAudio()) return 1;
    player.play();
    if (!player.isPlaying()) {
        std::cerr << "FAIL: the selected Qt audio backend did not start\n";
        return 1;
    }

    QElapsedTimer timer;
    timer.start();
    double previousTime = -1.0;
    int advancingSamples = 0;
    while (timer.elapsed() < 1800) {
        application.processEvents();
        QThread::msleep(20);
        const double currentTime = player.getCurrentAudioTimeSeconds();
        if (std::isfinite(currentTime) && currentTime > previousTime + 0.005)
            ++advancingSamples;
        previousTime = currentTime;
        if (currentTime >= 0.75) break;
    }
    player.pause();

    if (previousTime < 0.5 || advancingSamples < 5) {
        std::cerr << "FAIL: real audio playback did not advance continuously (time="
                  << previousTime << ", advancing polls=" << advancingSamples << ")\n";
        return 1;
    }

    std::cout << "real audio backend playback passed at " << previousTime << " seconds\n";
    return 0;
}
