// Exercise the production output builder with an infinite synthetic input;
// never enumerate or open a user's camera, microphone or existing process.
#include "VirtualDub/VDQtCapturePolicy.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    for (bool preview : {false, true}) {
        VDQtCaptureOutputConfig config;
        config.path = directory.filePath(preview ? "preview.nut" : "recording.nut");
        config.preview = preview;
        config.durationSeconds = 0.3;
        QString error;
        QStringList args{"-hide_banner", "-loglevel", "error", "-re", "-f", "lavfi", "-i",
                         "testsrc2=size=32x24:rate=30"};
        args += VDQtCaptureOutputArguments(config, &error);
        QProcess process;
        process.start("ffmpeg", args);
        if (!error.isEmpty() || !process.waitForStarted(1000)) return 1;
        QElapsedTimer elapsed;
        elapsed.start();
        QByteArray images, diagnostics;
        while (process.state() != QProcess::NotRunning && elapsed.elapsed() < 2000) {
            process.waitForFinished(20);
            images += process.readAllStandardOutput();
            diagnostics += process.readAllStandardError();
        }
        if (process.state() != QProcess::NotRunning) {
            process.kill(); // Only this disposable test's child process.
            process.waitForFinished(1000);
            std::cerr << "Timed capture stayed alive with preview=" << preview << '\n';
            return 1;
        }
        images += process.readAllStandardOutput();
        diagnostics += process.readAllStandardError();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
            || !QFileInfo(config.path).size() || (preview && !images.contains("\xFF\xD8"))) {
            std::cerr << diagnostics.toStdString() << '\n'; return 1;
        }
        QProcess decoder;
        decoder.start("ffmpeg", {"-v", "error", "-i", config.path, "-f", "null", "-"});
        if (!decoder.waitForFinished(3000) || decoder.exitCode() != 0) return 1;
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-count_frames",
                               "-show_entries", "stream=nb_read_frames", "-of", "default=nw=1:nk=1", config.path});
        if (!probe.waitForFinished(3000) || probe.exitCode() != 0
            || probe.readAllStandardOutput().trimmed().toInt() != 9) {
            std::cerr << "Timed recording did not retain all nine 30 fps frames\n";
            return 1;
        }
    }
    if (VDQtCaptureTimeExpired(0, 1000000) || VDQtCaptureTimeExpired(0.3, 299)
        || !VDQtCaptureTimeExpired(0.3, 300)) return 1;
    VDQtCaptureOutputConfig invalid;
    invalid.path = directory.filePath("invalid.nut");
    invalid.durationSeconds = std::numeric_limits<double>::infinity();
    QString error;
    if (!VDQtCaptureOutputArguments(invalid, &error).isEmpty() || error.isEmpty()) return 1;
    return 0;
}
