// Offscreen integration coverage and an opt-in session benchmark. Performance
// numbers are observations, not brittle speed assertions tied to this machine.
#include "support/VDQtTestFixtures.h"
#include "VDQtOperationRegressions.h"
#include "VirtualDub/VDQtMainWindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QThreadPool>
#include <iostream>

namespace {
bool require(bool condition, const QString& description) {
    if (!condition) std::cerr << "FAIL: " << description.toStdString() << '\n';
    return condition;
}

// Pump events with a deadline only in the test driver. Production source
// transitions are not changed by the harness.
template <typename Predicate>
bool waitUntil(Predicate predicate, int timeout = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return predicate();
}

QJsonObject processResources() {
    QJsonObject result;
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly)) return result;
    const auto lines = status.readAll().split('\n');
    for (const QByteArray& line : lines) {
        const auto fields = line.simplified().split(' ');
        if (fields.size() < 2) continue;
        if (fields.first() == "VmRSS:") result["rssKiB"] = fields.at(1).toDouble();
        if (fields.first() == "Threads:") result["threads"] = fields.at(1).toInt();
    }
    return result;
}

bool guiSmokeTest(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    const auto panes = window.findChildren<VDVideoDisplayWidget*>();
    auto* position = window.findChild<VDQtPositionControlWidget*>();
    if (!require(panes.size() == 2 && position, "controller constructs preview/transport"))
        return false;
    for (const QString& path : {fixtures.mp4, fixtures.avs, fixtures.mp4}) {
        if (!require(window.openVideoFile(path), "open " + path)
            || !require(waitUntil([&] { return !panes.first()->frameImage().isNull(); }),
                        "first frame reaches preview")) return false;
        const QImage firstFrame = panes.first()->frameImage();
        position->SetPosition(12);
        if (!require(waitUntil([&] {
                return position->GetPosition() == 12
                    && (path == fixtures.avs || panes.first()->frameImage() != firstFrame);
            }), "scrub reaches requested frame in preview")) return false;
        if (!require(QMetaObject::invokeMethod(&window, "onFileClose", Qt::DirectConnection),
                     "close source is callable")) return false;
        if (!require(panes.first()->frameImage().isNull(), "close clears preview")) return false;
    }
    // Verify frame-zero playback at controller level, not just decoder duration.
    if (!window.openVideoFile(fixtures.mp4)) return false;
    if (!require(QMetaObject::invokeMethod(&window, "onTransportAction",
                 Qt::DirectConnection, Q_ARG(int, VDQT_PCN_PLAY)), "start playback")) return false;
    const bool advanced = waitUntil([&] { return position->GetPosition() >= 3; });
    QMetaObject::invokeMethod(&window, "onTransportAction", Qt::DirectConnection,
                              Q_ARG(int, VDQT_PCN_STOP));
    return require(advanced, "playback advances from frame zero");
}

bool benchmark(VDQtTestFixtures& fixtures, int cycles, int frames, QJsonArray& samples) {
    VDQtVideoDecoder decoder;
    VDQtVideoExporter exporter;
    VDQtFilterSystem::instance().clearFilters();
    for (int cycle = 0; cycle < cycles; ++cycle) {
        QElapsedTimer timer;
        timer.start();
        if (!decoder.openFile(fixtures.mp4)) return require(false, decoder.getLastError());
        QJsonObject sample{{"cycle", cycle + 1}, {"frames", frames},
                           {"mp4OpenMs", double(timer.elapsed())}};
        timer.restart();
        for (int frame = 0; frame < frames; ++frame)
            if (decoder.getFrameImage(frame, true).isNull()) return false;
        sample["previewMs"] = double(timer.elapsed());
        timer.restart();
        for (int frame : {frames - 8, 3, frames / 2, 0, frames - 1, 12}) {
            decoder.clearCache();
            if (decoder.getFrameImage(frame).isNull()) return false;
        }
        sample["scrubMs"] = double(timer.elapsed());
        timer.restart();
        if (!decoder.openFile(fixtures.avs)) return require(false, decoder.getLastError());
        sample["avsOpenMs"] = double(timer.elapsed());
        VDQtVideoExporter::ExportOptions options;
        options.inputPath = fixtures.avs;
        options.outputPath = fixtures.directory.filePath(QStringLiteral("cycle-%1.mov").arg(cycle));
        options.includeAudio = false;
        options.unattended = true;
        options.endFrame = frames - 1;
        options.videoCodecOverride = QStringLiteral("prores_ks");
        options.videoPixelFormatOverride = QStringLiteral("yuv422p10le");
        timer.restart();
        const bool exported = exporter.exportVideo(options, &decoder);
        if (!exported) return require(false, exporter.lastError());
        const double exportMs = timer.nsecsElapsed() / 1000000.0;
        sample["exportMs"] = exportMs;
        sample["exportFps"] = frames * 1000.0 / std::max(0.001, exportMs);
        const QJsonObject resources = processResources();
        for (auto it = resources.begin(); it != resources.end(); ++it) sample[it.key()] = it.value();
        sample["tempDiskBytes"] = double(fixtures.diskBytes());
        samples.append(sample);
        // Remove just our completed output; growth in the measurements should
        // reflect runtime caches, not intentionally accumulated exported files.
        if (!QFile::remove(options.outputPath)) return require(false, "remove test output");
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    // Isolate all settings before constructing the window, even without presets.
    QTemporaryDir settings;
    if (!settings.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME", (settings.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (settings.path() + "/data").toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("VDQtRegression");
    QCoreApplication::setApplicationName("ApplicationBaseline");
    QThreadPool::globalInstance()->setMaxThreadCount(8);
    VDQtVideoDecoder::setDecoderThreadCount(2);
    const QStringList arguments = application.arguments();
    const int benchmarkIndex = arguments.indexOf("--benchmark");
    const bool measuring = benchmarkIndex >= 0;
    int cycles = 8;
    if (measuring && benchmarkIndex + 1 < arguments.size()) {
        bool valid = false;
        cycles = arguments.at(benchmarkIndex + 1).toInt(&valid);
        if (!valid || cycles < 1 || cycles > 100) return 2;
    }
    int frames = 48;
    const int framesIndex = arguments.indexOf("--frames");
    if (framesIndex >= 0) {
        if (!measuring || framesIndex + 1 >= arguments.size()) return 2;
        bool valid = false;
        frames = arguments.at(framesIndex + 1).toInt(&valid);
        if (!valid || frames < 48 || frames > 1440) return 2;
    }
    VDQtTestFixtures fixtures;
    if (!require(fixtures.createBasic(measuring ? 1920 : 320, measuring ? 1080 : 180, frames),
                 fixtures.error)) return 1;
    const int operationIndex = arguments.indexOf("--operation-test");
    if (operationIndex >= 0) {
        if (operationIndex + 1 >= arguments.size()) return 2;
        return VDQtRunOperationRegression(arguments.at(operationIndex + 1), fixtures) ? 0 : 1;
    }
    if (!measuring) {
        if (!require(fixtures.createEdgeCases(), fixtures.error) || !guiSmokeTest(fixtures)) return 1;
        // Just smoke-decode here. Edge-case contracts are asserted in their own
        // repair batches; the baseline must not encode today's broken behavior.
        for (const QString& media : fixtures.edgeCaseMedia) {
            VDQtVideoDecoder decoder;
            if (!require(decoder.openFile(media) && !decoder.getFrameImage(0).isNull(),
                         "decode fixture " + media)) return 1;
        }
        return 0;
    }
    QJsonArray samples;
    if (!benchmark(fixtures, cycles, frames, samples)) return 1;
    const QByteArray report = QJsonDocument(QJsonObject{
        {"fixture", "synthetic 1080p MP4 / built-in AVS / ProRes; no audio"},
        {"cycles", samples}}).toJson();
    std::cout << report.constData();
    const int reportIndex = arguments.indexOf("--report");
    if (reportIndex >= 0) {
        if (reportIndex + 1 >= arguments.size()) return 2;
        QFile file(arguments.at(reportIndex + 1));
        // A report path must be new: never overwrite an existing user file.
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            || file.write(report) != report.size() || !file.flush()) return 1;
    }
    return 0;
}
