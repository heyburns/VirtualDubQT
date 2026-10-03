// Exporter callers need an actionable error for every failure, and a distinct
// cancellation result. All paths/media/encoder substitutions belong to this test.
#include <QString>
#include "VirtualDub/VDQtVideoExporter.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
class PathScope final {
public:
    explicit PathScope(const QByteArray& path)
        : mOriginal(qgetenv("PATH")), mPresent(qEnvironmentVariableIsSet("PATH")) { qputenv("PATH", path); }
    ~PathScope() { if (mPresent) qputenv("PATH", mOriginal); else qunsetenv("PATH"); }
private:
    QByteArray mOriginal; bool mPresent;
};
bool fixture(const QString& ffmpeg, const QString& path) {
    QProcess process;
    process.start(ffmpeg, {"-nostdin", "-v", "error", "-y", "-f", "lavfi", "-i",
        "testsrc2=size=32x24:rate=10", "-frames:v", "10", "-c:v", "ffv1", path});
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        process.kill(); process.waitForFinished(3000); return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
// Exit success without creating output. Video pipe consumers must drain stdin
// first so the failure is genuinely invalid output, not merely a broken pipe.
int encoder(const QStringList& arguments) {
    const int input = arguments.indexOf("-i");
    if (input >= 0 && input + 1 < arguments.size() && arguments.at(input + 1) == "-") {
        char block[8192];
        while (std::fread(block, 1, sizeof block, stdin) > 0) {}
    }
    return 0;
}
}

int main(int argc, char **argv) {
    if (QFileInfo(QString::fromLocal8Bit(argv[0])).fileName() == QStringLiteral("ffmpeg")) {
        QCoreApplication app(argc, argv); return encoder(app.arguments());
    }
    QTemporaryDir dir;
    if (!dir.isValid()) return 2;
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", dir.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", dir.filePath("data").toUtf8());
    QApplication app(argc, argv);
    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    const QString source = dir.filePath("source.mkv");
    if (ffmpeg.isEmpty() || !fixture(ffmpeg, source)) return 3;
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(source) || decoder.ensureFrameIndex().totalFrames != 10) return 4;
    VDQtVideoExporter exporter;
    VDQtVideoExporter::ExportOptions base;
    base.inputPath = source; base.outputPath = dir.filePath("output.mkv");
    base.containerType = "mkv"; base.includeAudio = false; base.unattended = true;
    base.videoMode = VideoMode_FullProcessing;
    base.processing = VDQtVideoExporter::ProcessingSnapshot{};
    base.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
    bool passed = true;
    auto expectError = [&](const VDQtVideoExporter::ExportOptions& options, const char *message) {
        const bool failed = !exporter.exportVideo(options, &decoder);
        return check(failed && !exporter.wasCancelled() && !exporter.lastError().trimmed().isEmpty(), message);
    };
    auto options = base;
    options.outputPath = dir.filePath("missing-directory/output.mkv");
    passed &= expectError(options, "staging failure has an actionable error");
    options = base; options.videoMode = VideoMode_DirectStreamCopy; options.decimateFactor = 2;
    passed &= expectError(options, "unsupported direct-copy decimation has an error");
    options.decimateFactor = 1; options.customFps = 15;
    passed &= expectError(options, "unsupported direct-copy retiming has an error");
    options = base; options.smartRendering = true; options.decimateFactor = 2;
    options.processing->videoCodec.codecId = "nonexistent_vdqt_regression_encoder";
    passed &= expectError(options, "smart-render encoder fallback retains its diagnostic");
    options = base;
    passed &= check(!exporter.exportVideo(options, &decoder, nullptr, nullptr, {},
        [](int, int) { return false; }) && exporter.wasCancelled(), "callback cancellation is not reported as failure");

    // Cancel the keyframe warning through its real button, with a disposable
    // parent. No other modal error should appear for this supported source.
    options = base; options.videoMode = VideoMode_DirectStreamCopy;
    options.startFrame = 2; options.endFrame = 7; options.unattended = false;
    QWidget parent;
    bool warningCancelled = false;
    QTimer click;
    QObject::connect(&click, &QTimer::timeout, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                if (box->windowTitle() == "Keyframe-Aligned Direct Copy") {
                    warningCancelled = true; box->done(QMessageBox::Cancel);
                } else box->reject(); // Unexpected error fails promptly.
            }
        }
    });
    click.start(5);
    passed &= check(!exporter.exportVideo(options, &decoder, nullptr, &parent)
        && warningCancelled && exporter.wasCancelled(), "keyframe-warning Cancel retains cancellation status");
    click.stop();

    const QString fakeDirectory = dir.filePath("fake"); QDir().mkpath(fakeDirectory);
    if (!QFile::link(QCoreApplication::applicationFilePath(), fakeDirectory + "/ffmpeg")) return 5;
    const QByteArray marker("EXISTING_OUTPUT_MUST_SURVIVE");
    {
        PathScope isolated(fakeDirectory.toUtf8());
        for (int mode : {VideoMode_FullProcessing, VideoMode_NormalRecompress,
                         VideoMode_FastRecompress, VideoMode_DirectStreamCopy}) {
            options = base; options.videoMode = mode;
            options.outputPath = dir.filePath(QString("empty_%1.mkv").arg(mode));
            passed &= check(writeFile(options.outputPath, marker), "create original output");
            passed &= expectError(options, "exit-zero encoder without output has a nonempty failure diagnostic");
            passed &= check(readFile(options.outputPath) == marker, "invalid encoder output does not replace destination");
        }
    }
    const QString brokenDirectory = dir.filePath("broken"); QDir().mkpath(brokenDirectory);
    const QString brokenEncoder = brokenDirectory + "/ffmpeg";
    if (!writeFile(brokenEncoder, "#!/no/such/vdqt-regression-interpreter\n")
        || !QFile::setPermissions(brokenEncoder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) return 6;
    {
        PathScope isolated(brokenDirectory.toUtf8());
        for (int mode : {VideoMode_FullProcessing, VideoMode_NormalRecompress,
                         VideoMode_FastRecompress, VideoMode_DirectStreamCopy}) {
            options = base; options.videoMode = mode;
            options.outputPath = dir.filePath(QString("start_%1.mkv").arg(mode));
            passed &= check(writeFile(options.outputPath, marker), "create encoder-start target");
            passed &= expectError(options, "encoder start failure has a nonempty diagnostic");
            passed &= check(readFile(options.outputPath) == marker, "encoder start failure preserves destination");
        }
    }
    return passed ? 0 : 1;
}
