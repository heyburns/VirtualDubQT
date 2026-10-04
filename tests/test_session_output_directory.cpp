// Real-controller contract. Both processes share one disposable settings
// tree, proving that session choices are not written to disk. No media export
// is needed: accepting Save Video into the job queue exercises the same saved
// output-directory decision and snapshots the configured processing choices.
#include <QString>
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtMainWindow.h"
#include "VirtualDub/VDQtJobQueue.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <iostream>
#include <memory>

namespace {
bool check(bool valid, const char *description) {
    if (!valid) std::cerr << "FAIL: " << description << '\n';
    return valid;
}
bool sameDirectory(const QString& path, const QString& directory) {
    return QDir::cleanPath(QFileInfo(path).absolutePath())
        == QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
}
struct SaveResult { bool opened = false, accepted = false, timedOut = false; QString suggested; int unexpected = 0, queued = 0; };
SaveResult saveDialog(VDQtMainWindow& window, const QString& chosen = {}) {
    SaveResult result;
    QElapsedTimer deadline; deadline.start();
    QTimer responder; responder.setInterval(1);
    QObject::connect(&responder, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible()) continue;
            if (deadline.elapsed() > 10000) {
                result.timedOut = true;
                if (auto *dialog = qobject_cast<QDialog*>(widget)) dialog->reject();
                continue;
            }
            if (auto *dialog = qobject_cast<VDSaveVideoDialog*>(widget)) {
                if (result.opened) continue;
                result.opened = true;
                result.suggested = dialog->getSelectedFilePath();
                if (chosen.isEmpty()) { dialog->reject(); continue; }
                const auto fields = dialog->findChildren<QLineEdit*>();
                const auto queues = dialog->findChildren<QCheckBox*>();
                auto *buttons = dialog->findChild<QDialogButtonBox*>();
                QAbstractButton *save = buttons ? buttons->button(QDialogButtonBox::Save) : nullptr;
                if (fields.size() != 1 || queues.size() != 1 || !save) {
                    ++result.unexpected; dialog->reject(); continue;
                }
                fields.first()->setText(chosen);
                queues.first()->setChecked(true);
                result.accepted = true;
                // Click the real button so its session preference callback runs.
                save->click();
            } else if (auto *message = qobject_cast<QMessageBox*>(widget)) {
                if (!chosen.isEmpty() && message->windowTitle() == "Job Queued") {
                    ++result.queued; message->accept(); continue;
                }
                ++result.unexpected;
                std::cerr << "Unexpected dialog: " << message->windowTitle().toStdString()
                          << ": " << message->text().toStdString() << '\n';
                message->reject();
            }
        }
    });
    responder.start();
    const bool invoked = QMetaObject::invokeMethod(&window, "onFileSaveAVI", Qt::DirectConnection);
    responder.stop();
    if (!invoked) ++result.unexpected;
    return result;
}
bool sessionProcessingPresent() {
    const auto& video = VDQtCodecEngine::instance().getVideoParams();
    const auto& audio = VDQtCodecEngine::instance().getAudioParams();
    return video.codecId == "ffv1" && audio.codecId == "pcm_s32le"
        && VDQtFilterSystem::instance().getActiveChain().size() == 1
        && VDQtFilterSystem::instance().getActiveChain().first().type == VDFilterType::InvertColor
        && VDQtAudioFilterSystem::instance().activeChain().size() == 1
        && VDQtAudioFilterSystem::instance().activeChain().first().type == VDAudioFilterType::Gain;
}
bool factoryProcessingPresent() {
    const VDQtCodecEngine factory;
    const auto& video = VDQtCodecEngine::instance().getVideoParams();
    const auto& audio = VDQtCodecEngine::instance().getAudioParams();
    return video.codecId == factory.getVideoParams().codecId
        && audio.codecId == factory.getAudioParams().codecId
        && VDQtFilterSystem::instance().getActiveChain().isEmpty()
        && VDQtAudioFilterSystem::instance().activeChain().isEmpty()
        && VDQtCodecSettings::instance().getSaveVideoSessionConfig().fileTypeIndex == 6;
}
int freshProcess(const QString& source, const QString& previousDirectory) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    bool passed = check(factoryProcessingPresent(),
        "fresh process restores default codecs/save format and empty video/audio filter chains");
    passed &= check(window.openVideoFile(source), "fresh process opens owned source");
    const SaveResult next = saveDialog(window);
    passed &= check(next.opened && !next.timedOut && !next.unexpected
        && sameDirectory(next.suggested, QFileInfo(source).absolutePath())
        && !sameDirectory(next.suggested, previousDirectory)
        && QFileInfo(next.suggested).suffix() == "mp4",
        "fresh process uses source directory and default save format, not prior session output directory");
    window.close();
    return passed ? 0 : 1;
}
}

int main(int argc, char **argv) {
    // Set isolation before QApplication/QSettings/QStandardPaths initialization.
    const QStringList rawArguments = [&] {
        QStringList values; for (int index = 0; index < argc; ++index) values.append(QString::fromLocal8Bit(argv[index])); return values;
    }();
    const bool child = rawArguments.size() == 5 && rawArguments.at(1) == "--fresh";
    VDQtTestFixtures fixtures;
    if (!fixtures.directory.isValid()) return 2;
    const QString settings = child ? rawArguments.at(2) : fixtures.directory.filePath("settings");
    qputenv("XDG_CONFIG_HOME", QDir(settings).filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", QDir(settings).filePath("data").toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("VDQtRegression");
    QCoreApplication::setApplicationName("SessionOutputDirectory");
    if (child) return freshProcess(rawArguments.at(3), rawArguments.at(4));
    if (!fixtures.createBasic(64, 48, 12)) return 3;
    const QString secondDirectory = fixtures.directory.filePath("different-source");
    const QString second = QDir(secondDirectory).filePath("second.mp4");
    const QString outputDirectory = fixtures.directory.filePath("chosen/nested/exports");
    const QString chosen = QDir(outputDirectory).filePath("first-export.mkv");
    if (!QDir().mkpath(secondDirectory) || !QDir().mkpath(outputDirectory)
        || !QFile::copy(fixtures.mp4, second)) return 4;
    auto window = std::make_unique<VDQtMainWindow>();
    window->setAutomationUnattended(true);
    window->show();
    QString error;
    if (!window->openVideoFile(fixtures.mp4)
        || !window->runAutomationText("VirtualDub.video.SetMode(3); VirtualDub.audio.SetSource(0);",
            fixtures.directory.path(), &error)) return 5;
    VDQtCodecEngine::instance().setVideoParams(VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1"));
    VDAudioCodecParams audio; audio.codecId = "pcm_s32le";
    VDQtCodecEngine::instance().setAudioParams(audio);
    VDQtFilterSystem::instance().addFilter(VDFilterType::InvertColor);
    VDQtAudioFilterSystem::instance().addFilter(VDAudioFilterType::Gain);
    VDSaveVideoSessionConfig save; save.fileTypeIndex = 2;
    VDQtCodecSettings::instance().setSaveVideoSessionConfig(save);
    const SaveResult accepted = saveDialog(*window, chosen);
    auto *queue = window->findChild<VDQtJobQueue*>();
    bool passed = check(accepted.opened && accepted.accepted && !accepted.timedOut
        && !accepted.unexpected && accepted.queued == 1 && queue && queue->jobs().size() == 1
        && queue->jobs().first().options.outputPath == chosen && queue->persistenceError().isEmpty()
        && !QFileInfo::exists(chosen), "real Save Video accepts nested output and snapshots a durable, unrun job");
    passed &= check(window->openVideoFile(second), "load a different owned source without closing the application");
    const SaveResult replaced = saveDialog(*window);
    passed &= check(replaced.opened && !replaced.timedOut && !replaced.unexpected
        && sameDirectory(replaced.suggested, outputDirectory)
        && QFileInfo(replaced.suggested).fileName() == "second.mkv" && sessionProcessingPresent(),
        "source replacement retains output directory, codecs and both filter chains for this session");
    passed &= check(QMetaObject::invokeMethod(window.get(), "onFileClose", Qt::DirectConnection)
        && window->openVideoFile(fixtures.mp4), "close video and open another source in the same application");
    const SaveResult reopened = saveDialog(*window);
    passed &= check(reopened.opened && !reopened.timedOut && !reopened.unexpected
        && sameDirectory(reopened.suggested, outputDirectory) && sessionProcessingPresent(),
        "Close Video does not reset session output directory or processing choices");
    window->close();
    window.reset(); // Release queue lock/source consumers before a fresh process.
    QProcess restart;
    restart.start(QCoreApplication::applicationFilePath(), {"--fresh", settings, second, outputDirectory});
    const bool started = restart.waitForStarted(3000);
    const bool finished = started && restart.waitForFinished(20000);
    if (!finished) { restart.kill(); restart.waitForFinished(3000); }
    const QByteArray diagnostics = restart.readAllStandardError();
    if (!diagnostics.isEmpty()) std::cerr << diagnostics.constData();
    passed &= check(finished && restart.exitStatus() == QProcess::NormalExit && restart.exitCode() == 0,
        "fresh process validates restart boundary with the same disposable on-disk configuration");
    return passed ? 0 : 1;
}
