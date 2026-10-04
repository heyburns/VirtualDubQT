// Real-controller contract; fixtures/settings/outputs are disposable.
// A modal responder counts every outcome dialog and cancels the actual progress
// dialog. This is not a test-only export path or a mocked exporter.
#include <QString>
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtMainWindow.h"
#include <QApplication>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
QByteArray readFile(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
class PathScope final {
public:
    explicit PathScope(const QByteArray& path)
        : mOriginal(qgetenv("PATH")), mPresent(qEnvironmentVariableIsSet("PATH")) { qputenv("PATH", path); }
    ~PathScope() { if (mPresent) qputenv("PATH", mOriginal); else qunsetenv("PATH"); }
private:
    QByteArray mOriginal;
    bool mPresent;
};
enum class Outcome { StagingError, Cancelled, EncoderStartFailure };
bool exerciseSave(VDQtMainWindow& window, const QString& output, Outcome outcome) {
    const bool cancel = outcome == Outcome::Cancelled;
    bool chosen = false, progressCancelled = false, timedOut = false;
    int exporterErrors = 0, genericFailures = 0, confirmations = 0, unexpected = 0;
    QString specificError;
    QElapsedTimer deadline; deadline.start();
    QTimer responder; responder.setInterval(1);
    QObject::connect(&responder, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible()) continue;
            if (deadline.elapsed() > 10000) {
                timedOut = true;
                if (auto *progress = qobject_cast<QProgressDialog*>(widget)) progress->cancel();
                else if (auto *dialog = qobject_cast<QDialog*>(widget)) dialog->reject();
                continue;
            }
            if (auto *dialog = qobject_cast<VDSaveVideoDialog*>(widget)) {
                if (chosen) continue;
                const auto fields = dialog->findChildren<QLineEdit*>();
                if (fields.size() != 1) { ++unexpected; dialog->reject(); continue; }
                fields.first()->setText(output); chosen = true; dialog->accept();
            } else if (auto *message = qobject_cast<QMessageBox*>(widget)) {
                if (message->property("vdqtManualResultsHandled").toBool()) continue;
                message->setProperty("vdqtManualResultsHandled", true);
                const QString title = message->windowTitle();
                if (cancel && title == "Replace Existing Video File?") {
                    ++confirmations;
                    if (auto *yes = message->button(QMessageBox::Yes)) yes->click();
                    else { ++unexpected; message->reject(); }
                } else {
                    if (title == "Export Error") { ++exporterErrors; specificError = message->text(); }
                    else if (title == "Export Failed") ++genericFailures;
                    else ++unexpected;
                    message->accept();
                }
            } else if (auto *progress = qobject_cast<QProgressDialog*>(widget)) {
                if (cancel && !progressCancelled
                    && progress->labelText().startsWith("Exporting ")) {
                    progressCancelled = true; progress->cancel();
                }
            }
        }
    });
    responder.start();
    const bool invoked = QMetaObject::invokeMethod(&window, "onFileSaveAVI", Qt::DirectConnection);
    responder.stop();
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    auto *log = VDLogWindow::instance(&window)->findChild<QTextEdit*>();
    const QString status = window.statusBar()->currentMessage();
    const QString text = log ? log->toPlainText() : QString();
    if (unexpected || genericFailures || timedOut || (cancel && !progressCancelled))
        std::cerr << "Manual Save Video: cancel=" << cancel << " progress=" << progressCancelled
                  << " exporterErrors=" << exporterErrors << " genericFailures=" << genericFailures
                  << " confirmations=" << confirmations << " unexpected=" << unexpected
                  << " timedOut=" << timedOut << " status=" << status.toStdString() << '\n';
    bool passed = check(invoked && chosen && !timedOut && unexpected == 0
        && window.menuBar()->isEnabled() && position && position->isEnabled(),
        "Save Video returns and restores editor controls without unexpected dialogs");
    if (cancel) {
        passed &= check(progressCancelled && confirmations == 1 && exporterErrors == 0
            && genericFailures == 0 && status == "Video export cancelled."
            && text.contains("[Export] Video export cancelled.")
            && !text.contains("failed or was cancelled"),
            "progress cancellation reports cancellation without any error warning");
    } else if (outcome == Outcome::StagingError) {
        const QString detail = "A temporary output could not be created in the destination directory.";
        passed &= check(exporterErrors == 1 && genericFailures == 0 && confirmations == 0
            && specificError.contains(detail) && status.contains(detail) && text.contains(detail),
            "missing destination directory reports one specific modal error plus matching status/log");
    } else {
        const QString detail = "The FFmpeg video encoder could not be started:";
        passed &= check(exporterErrors == 0 && genericFailures == 0 && confirmations == 0
            && status.contains(detail) && text.contains(detail),
            "a phase without its own error dialog still exposes the specific failure in status/log");
    }
    return passed;
}
}

int main(int argc, char **argv) {
    QTemporaryDir settings;
    if (!settings.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", settings.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", settings.filePath("data").toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("VDQtRegression");
    QCoreApplication::setApplicationName("ManualExportResults");
    VDQtTestFixtures fixtures;
    if (!fixtures.createBasic()) return 3;
    const QByteArray original = readFile(fixtures.mp4);
    VDQtMainWindow window;
    window.show();
    QString error;
    if (!window.openVideoFile(fixtures.mp4)
        || !window.runAutomationText("VirtualDub.video.SetMode(3); VirtualDub.audio.SetSource(0);",
            fixtures.directory.path(), &error)) return 4;
    VDQtCodecEngine::instance().setVideoParams(VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1"));
    VDSaveVideoSessionConfig save; save.fileTypeIndex = 2;
    VDQtCodecSettings::instance().setSaveVideoSessionConfig(save);
    bool passed = exerciseSave(window, fixtures.directory.filePath("missing/output.mkv"), Outcome::StagingError);
    const QString output = fixtures.directory.filePath("cancelled-output.mkv");
    const QByteArray originalOutput("PREEXISTING_OUTPUT_MUST_REMAIN");
    passed &= check(writeFile(output, originalOutput), "create existing cancellation target");
    passed &= exerciseSave(window, output, Outcome::Cancelled);
    passed &= check(readFile(output) == originalOutput && readFile(fixtures.mp4) == original,
        "cancelled export preserves the original output and input byte-for-byte");
    // Executable is discoverable, but its private nonexistent interpreter makes
    // QProcess::waitForStarted fail. This Full-export phase has no modal error.
    const QString brokenDirectory = fixtures.directory.filePath("broken-encoder");
    const QString broken = brokenDirectory + "/ffmpeg";
    if (!QDir().mkpath(brokenDirectory) || !writeFile(broken, "#!/no/such/vdqt-test-interpreter\n")
        || !QFile::setPermissions(broken, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) return 5;
    {
        PathScope isolated(brokenDirectory.toUtf8());
        const QString uncreated = fixtures.directory.filePath("never-created.mkv");
        passed &= exerciseSave(window, uncreated, Outcome::EncoderStartFailure);
        passed &= check(!QFileInfo::exists(uncreated) && readFile(fixtures.mp4) == original,
            "encoder start failure does not create output or modify source");
    }
    return passed ? 0 : 1;
}
