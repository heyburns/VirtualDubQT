// Real FIFO producer tests with built-in-only AVS fixtures. Every script, log
// and pipe is disposable; stop() affects only this test's server subprocess.
#include "VirtualDub/VDQtFrameServer.h"
#include "VirtualDub/VDQtVideoDecoder.h"
#include "VirtualDub/VDQtTimingMath.h"
#include "support/VDQtTestFixtures.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <atomic>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
template<class Predicate> bool waitFor(Predicate predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return predicate();
}
bool nativeStartup(VDQtTestFixtures& fixtures) {
    const QString script = fixtures.directory.filePath("startup.avs");
    // A billion-frame built-in clip has authoritative metadata but cannot be
    // eagerly rendered within this test's deadline. No runtime logging filter
    // is needed to prove that startup avoids evaluating the complete graph.
    const QByteArray contents =
        "BlankClip(length=1000000000,width=64,height=48,pixel_type=\"RGB24\",fps=25,audio_rate=0)\n";
    if (!fixtures.writeText(script, contents)) return false;
    VDQtFrameServer server;
    VDQtFrameServer::Config config;
    config.sourcePath = script;
    config.pipePath = fixtures.directory.filePath("startup.nutpipe");
    std::atomic_bool started{false};
    // Observe at the exact publication point, before the asynchronous FIFO
    // write. The release/acquire pair publishes preparation to the main thread.
    QObject::connect(&server, &VDQtFrameServer::serverStarted, &server,
        [&](const QString&) {
            started.store(true, std::memory_order_release);
        }, Qt::DirectConnection);
    QString error;
    if (!check(server.start(config, &error), "native frame server starts")) return false;
    const bool published = waitFor([&] { return started.load(std::memory_order_acquire) || !server.isRunning(); });
    QElapsedTimer stopping;
    stopping.start();
    server.stop(); // Intentionally no reader: cancellation must unblock the FIFO.
    bool passed = check(published && started.load(std::memory_order_acquire), "native source publishes a prepared stream");
    passed &= check(stopping.elapsed() < 3000 && !server.isRunning() && !QFileInfo::exists(config.pipePath),
                    "no-reader cancellation completes and removes the owned FIFO");
    return passed;
}

bool capture(VDQtTestFixtures& fixtures, VDQtFrameServer::Config config,
             const QString& name, QString *output) {
    config.pipePath = fixtures.directory.filePath(name + ".fifo");
    *output = fixtures.directory.filePath(name + ".nut");
    VDQtFrameServer server;
    bool finished = false;
    QString servingError;
    QObject::connect(&server, &VDQtFrameServer::serverFinished, &server,
        [&](const QString& error) { servingError = error; finished = true; });
    QString error;
    if (!check(server.start(config, &error), "capture server starts")) return false;
    QProcess reader;
    reader.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-nostdin", "-y",
        "-i", config.pipePath, "-map", "0:v:0", "-c:v", "copy", "-an", "-f", "nut", *output});
    const bool started = reader.waitForStarted(5000);
    const bool stopped = started && waitFor([&] { return finished; });
    server.stop();
    if (!stopped || !reader.waitForFinished(5000)) {
        reader.kill();
        reader.waitForFinished(5000);
    }
    if (!servingError.isEmpty()) std::cerr << servingError.toStdString() << '\n';
    if (reader.exitCode()) std::cerr << reader.readAllStandardError().constData() << '\n';
    return check(stopped && servingError.isEmpty() && reader.exitCode() == 0
        && !QFileInfo::exists(config.pipePath), "reader receives a complete stream and owned FIFO is removed");
}

bool servedTimingAndPrecision(VDQtTestFixtures& fixtures) {
    bool passed = true;
    const QString vfr = fixtures.directory.filePath("short-vfr.mkv");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc=size=64x48:rate=30", "-frames:v", "3",
            "-vf", "settb=1/1000,setpts=N*10", "-enc_time_base", "1:1000", "-fps_mode", "passthrough",
            "-c:v", "ffv1", "-threads", "1", "-an", vfr})) return false;
    VDQtVideoDecoder source;
    if (!source.openFile(vfr) || !source.ensureFrameIndex().errorMessage.isEmpty()) return false;
    passed &= check(source.getFrameCount() == 3 && source.getFps() == 30
        && std::abs(source.getFrameTimestampSeconds(1) - .01) < 1e-8
        && std::abs(source.getFrameElapsedSeconds(3) - .053) < .001,
        "VFR fixture independently has two short intervals and a final nominal interval");
    VDQtFrameServer::Config config;
    config.sourcePath = vfr;
    QString output;
    if (!capture(fixtures, config, "vfr-grid", &output)) return false;
    VDQtVideoDecoder received;
    if (!received.openFile(output) || !received.ensureFrameIndex().errorMessage.isEmpty()) return false;
    passed &= check(received.getFrameCount() == 2, "short VFR frames do not each force a CFR output tick");
    passed &= check(received.getFrameImage(0).convertToFormat(QImage::Format_RGB888)
        == source.getFrameImage(0).convertToFormat(QImage::Format_RGB888)
        && received.getFrameImage(1).convertToFormat(QImage::Format_RGB888)
        == source.getFrameImage(2).convertToFormat(QImage::Format_RGB888),
        "CFR grid samples the picture active at 0 and 1/30 seconds");

    for (const QString& pixelType : {QString("RGBAP"), QString("RGBAP16")}) {
        const QString script = fixtures.directory.filePath(pixelType + ".avs");
        const QString channels = pixelType == "RGBAP16" ? "1001,2345,3456,4567" : "17,63,111,97";
        if (!fixtures.writeText(script, QString(
            "BlankClip(length=4,width=64,height=48,pixel_type=\"%1\",fps=24,audio_rate=0,colors=[%2])\n")
                .arg(pixelType, channels).toUtf8()) || !source.openFile(script)) return false;
        const QImage original = source.getFrameImage(0);
        config = {};
        config.sourcePath = script;
        if (!capture(fixtures, config, pixelType, &output) || !received.openFile(output)
            || !received.ensureFrameIndex().errorMessage.isEmpty()) return false;
        const QImage decoded = received.getFrameImage(0);
        passed &= check(received.getFrameCount() == 4 && decoded.depth() == original.depth()
            && decoded.hasAlphaChannel() && decoded == original,
            "served native RGB alpha and 16-bit channels are lossless, not RGB8 quantized");
    }

    const QString rates = fixtures.directory.filePath("rates.avs");
    if (!fixtures.writeText(rates,
        "BlankClip(length=5,width=64,height=48,pixel_type=\"RGB24\",fps=24,audio_rate=0)\n")) return false;
    config = {};
    config.sourcePath = rates;
    config.decimateFactor = 2;
    if (!capture(fixtures, config, "decimated", &output) || !received.openFile(output)
        || !received.ensureFrameIndex().errorMessage.isEmpty()) return false;
    passed &= check(received.getFrameCount() == 3 && std::abs(received.getFps() - 12) < 1e-8,
        "decimation retains a source frame per chunk at the reduced output rate");
    config.decimateFactor = 1;
    config.customFps = 12;
    config.convertFpsPreserveDuration = true;
    if (!capture(fixtures, config, "converted", &output) || !received.openFile(output)
        || !received.ensureFrameIndex().errorMessage.isEmpty()) return false;
    passed &= check(received.getFrameCount() == 3 && std::abs(received.getFps() - 12) < 1e-8,
        "rate conversion samples unchanged duration at the requested output rate");
    config.convertFpsPreserveDuration = false;
    if (!capture(fixtures, config, "reinterpreted", &output) || !received.openFile(output)
        || !received.ensureFrameIndex().errorMessage.isEmpty()) return false;
    passed &= check(received.getFrameCount() == 5 && std::abs(received.getFps() - 12) < 1e-8,
        "source-rate reinterpretation retains all source frames at the requested clock");

    VDQtFrameServer invalid;
    config.pipePath = fixtures.directory.filePath("invalid.fifo");
    config.customFps = std::numeric_limits<double>::infinity();
    QString error;
    passed &= check(!invalid.start(config, &error) && error.contains("rate")
        && !QFileInfo::exists(config.pipePath), "invalid rate fails before FIFO creation");
    int64_t boundary = 0;
    passed &= check(VDQtCfrBoundaryFrames(.01L, 30, &boundary) && boundary == 1
        && VDQtCfrBoundaryFrames(.02L, 30, &boundary) && boundary == 1
        && VDQtCfrBoundaryFrames(1.0L / 30, 30, &boundary) && boundary == 1
        && VDQtCfrBoundaryFrames(1000000000.0L / 30, 30, &boundary) && boundary == 1000000000
        && !VDQtCfrBoundaryFrames(1e100L, 30, &boundary)
        && !VDQtCfrBoundaryFrames(-1, 30, &boundary),
        "cumulative grid bounds reject overflow and retain exact CFR boundaries");
    return passed;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    VDQtTestFixtures fixtures;
    return nativeStartup(fixtures) && servedTimingAndPrecision(fixtures) ? 0 : 1;
}
