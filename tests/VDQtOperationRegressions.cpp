// Real-controller regressions for source ownership and export isolation. All
// media/settings/queues are disposable; no physical playback device is needed.
#include "VDQtOperationRegressions.h"
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtMainWindow.h"
#include "VirtualDub/VDQtFrameServer.h"
#include "VirtualDub/VDQtFrameDecodeWorker.h"
#include "VirtualDub/VDQtAudioExport.h"
#include "VirtualDub/VDQtFilterFrameContext.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QPointer>
#include <QProcess>
#include <QPainter>
#include <QProgressDialog>
#include <QSettings>
#include <QRadioButton>
#include <QTextEdit>
#include <QStandardPaths>
#include <QStatusBar>
#include <QThread>
#include <QtEndian>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
template <class Predicate> bool waitFor(Predicate predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return predicate();
}
bool invoke(VDQtMainWindow& window, const char *method) {
    return QMetaObject::invokeMethod(&window, method, Qt::DirectConnection);
}
QList<VDFilterInstance> invertChain() {
    VDQtFilterSystem factory;
    factory.addFilter(VDFilterType::InvertColor);
    return factory.getActiveChain();
}
QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool chooseProjectFile(VDQtMainWindow& window, const char *action, const QString& path,
                       bool expectLoadError = false) {
    bool chosen = false, failed = false, loadError = false;
    QElapsedTimer deadline;
    deadline.start();
    QTimer responder;
    responder.setInterval(5);
    QObject::connect(&responder, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QFileDialog*>(widget)) {
                if (dialog->isVisible() && deadline.elapsed() > 5000) {
                    std::cerr << "Project file dialog timed out selecting " << path.toStdString()
                              << "; selected=" << dialog->selectedFiles().join(',').toStdString() << '\n';
                    failed = true; dialog->reject(); continue;
                }
                if (!dialog->isVisible() || dialog->property("testChosen").toBool()) continue;
                dialog->setProperty("testChosen", true);
                dialog->setDirectory(QFileInfo(path).absolutePath());
                dialog->selectFile(QFileInfo(path).fileName());
                // QFileSystemModel populates asynchronously. Let it resolve
                // the existing project before accepting a load-file dialog.
                QTimer::singleShot(100, dialog, [dialog, path] {
                    if (auto *filename = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                        // Match a user's typed filename instead of relying on
                        // a still-populating view to retain programmatic selection.
                        filename->setFocus();
                        filename->setText(path);
                    } else dialog->selectFile(QFileInfo(path).fileName());
                    QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                });
                chosen = true;
            } else if (auto *message = qobject_cast<QMessageBox*>(widget)) {
                if (!message->isVisible()) continue;
                std::cerr << "Project dialog: " << message->text().toStdString() << '\n';
                loadError = message->windowTitle() == "Load Project Error";
                failed = true;
                message->accept();
            }
        }
    });
    responder.start();
    const bool invoked = invoke(window, action);
    responder.stop();
    return check(invoked && chosen && (expectLoadError ? loadError : !failed),
                 expectLoadError ? "project load reports the expected validation failure"
                                 : "project save/load dialog completes successfully");
}

bool logLifetime() {
    // Recreating an editor must not reuse its parent's deleted log dialog.
    // The weak pointer also proves that destruction actually occurred.
    for (int cycle = 0; cycle < 3; ++cycle) {
        QPointer<VDLogWindow> log;
        {
            QWidget owner;
            VDLogWindow *created = VDLogWindow::instance(&owner);
            if (!created) return check(false, "log dialog is created");
            log = created;
            if (!check(created->parentWidget() == &owner,
                       "new log dialog belongs to the current editor")) return false;
            log->appendLog(QStringLiteral("lifetime regression"));
            if (!check(VDLogWindow::instance(&owner) == log,
                       "live editor reuses its log dialog")) return false;
        }
        if (!check(log.isNull(), "editor destruction deletes the parent-owned log")) return false;
    }
    return true;
}

bool queuePersistence(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.show();
    auto *queue = window.findChild<VDQtJobQueue*>();
    auto *warning = window.findChild<QLabel*>("queuePersistenceWarning");
    if (!queue || !warning) return false;
    VDQtJobState job;
    job.sourcePaths = {fixtures.mp4};
    job.operation = VDQtJobOperation::VideoAnalysis;
    QString error;
    if (!queue->replaceJobs({job}, &error) || !queue->flush(&error)) return false;
    const QString destination = queue->autosavePath();
    const QByteArray original = readFile(destination);
    // Replace only this test's temporary queue file with a directory to force
    // QSaveFile failure, regardless of whether tests run as root.
    if (!QFile::remove(destination) || !QDir().mkdir(destination)) return false;
    if (!invoke(window, "runPendingJobs")
        || !check(queue->jobAt(0)->status == VDQtJobStatus::Pending && !queue->isRunning()
            && !queue->persistenceError().isEmpty() && warning->isVisible(),
            "failed initial durable checkpoint prevents the runner and exposes a persistent warning")) return false;
    bool prompted = false;
    QTimer responder;
    responder.setInterval(5);
    QObject::connect(&responder, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *message = qobject_cast<QMessageBox*>(widget);
            if (!message || !message->isVisible()) continue;
            prompted = message->windowTitle() == "Job Queue Not Saved";
            message->done(QMessageBox::Cancel);
        }
    });
    responder.start();
    const bool closed = window.close();
    responder.stop();
    if (!check(prompted && !closed && window.isVisible(),
        "Cancel keeps the editor open when Close cannot save its queue")) return false;
    if (!QDir().rmdir(destination) || !fixtures.writeText(destination, original)
        || !queue->flush(&error)) return false;
    if (!check(queue->persistenceError().isEmpty() && !warning->isVisible(),
        "a successful retry clears the persistent save warning")) return false;

    // A status listener can invalidate the destination after the initial save.
    // The second, per-job checkpoint must also stop before executeQueuedJob.
    bool invalidated = false;
    const auto connection = QObject::connect(queue, &VDQtJobQueue::jobChanged, &window,
        [&](int row) {
            const auto *record = queue->jobAt(row);
            if (!invalidated && record && record->status == VDQtJobStatus::Starting) {
                invalidated = QFile::remove(destination) && QDir().mkdir(destination);
            }
        });
    if (!invoke(window, "runPendingJobs")) return false;
    QObject::disconnect(connection);
    if (!check(invalidated && queue->jobAt(0)->status == VDQtJobStatus::Pending
        && !queue->isRunning() && warning->isVisible(),
        "a failed per-job intent checkpoint reverts Pending and prevents execution")) return false;
    if (!QDir().rmdir(destination) || !queue->flush(&error) || !invoke(window, "runPendingJobs")) return false;
    return check(queue->jobAt(0)->status == VDQtJobStatus::Complete,
        "after storage recovery the same pending analysis job can run and durably complete");
}

bool recoveryRetention(VDQtTestFixtures& fixtures) {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(directory)) return false;
    const QString recovery = QDir(directory).filePath("crash-recovery.vdqproject");
    VDQtProjectState project;
    project.sourcePath = fixtures.avs;
    project.sourcePaths = {fixtures.avs};
    project.audioDisabled = true;
    project.sourceFrameCount = 100; // Source really has 48: fails after old loader opens it.
    project.sourceFrameCountExact = true;
    project.timelineExplicit = true;
    project.timelineSegments = {{0, 8}};
    QString error;
    if (!VDQtProjectFile::saveProject(recovery, project, &error)) return false;
    const QByteArray original = readFile(recovery);
    bool asked = false;
    int failures = 0;
    QMessageBox::StandardButton decision = QMessageBox::Yes;
    QTimer responder;
    responder.setInterval(5);
    QObject::connect(&responder, &QTimer::timeout, qApp, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *message = qobject_cast<QMessageBox*>(widget);
            if (!message || !message->isVisible()) continue;
            if (message->windowTitle() == "Recover Editing Session?") {
                asked = true;
                message->button(decision)->click();
            } else {
                ++failures;
                message->accept();
            }
        }
    });
    responder.start();
    {
        VDQtMainWindow window;
        window.show();
        const bool recoveryFailed = waitFor([&] { return asked && failures > 0; });
        if (!recoveryFailed) std::cerr << "Recovery: asked=" << asked << ", failures=" << failures
                                     << ", snapshot exists=" << QFile::exists(recovery) << '\n';
        if (!check(recoveryFailed,
                   "saved invalid frame references reach the real recovery failure path")
            || !check(readFile(recovery) == original, "failed recovery preserves its original snapshot")) return false;
        if (!window.openVideoFile(fixtures.mp4)) return false;
        // Dispatch the actual autosave callback without waiting thirty seconds.
        for (QTimer *timer : window.findChildren<QTimer*>()) {
            if (timer->interval() == 30000)
                QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
        }
        if (!check(readFile(recovery) == original, "new session autosave cannot overwrite failed recovery")) return false;
        invoke(window, "onFileClose");
        window.close();
        if (!check(readFile(recovery) == original,
                   "source and application Close cannot delete failed recovery")) return false;
    }
    asked = false; decision = QMessageBox::No;
    {
        VDQtMainWindow window;
        if (!check(waitFor([&] { return asked; }) && !QFile::exists(recovery),
                   "explicit recovery discard removes the saved snapshot")) return false;
    }
    project.sourceFrameCount = 48;
    if (!VDQtProjectFile::saveProject(recovery, project, &error)) return false;
    asked = false; failures = 0; decision = QMessageBox::Yes;
    {
        VDQtMainWindow window;
        window.show();
        if (!check(waitFor([&] { return asked; }) && failures == 0 && !QFile::exists(recovery),
                   "successful recovery consumes its snapshot only after restoration")) return false;
        const auto panes = window.findChildren<VDVideoDisplayWidget*>();
        if (!check(waitFor([&] { return !panes.first()->frameImage().isNull(); }),
                   "successfully recovered source still previews")) return false;
    }
    return true;
}

bool editPreview(VDQtTestFixtures& fixtures) {
    VDQtVideoDecoder reference;
    if (!reference.openFile(fixtures.mp4)) return false;
    QMap<int, QImage> expected;
    for (int frame : {0, 8, 16})
        expected.insert(frame, reference.getFrameImage(frame).convertToFormat(QImage::Format_RGBA8888));
    if (!check(expected[0] != expected[16] && !expected[8].isNull(),
               "edit fixture has distinguishable source frames")) return false;
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4)) return false;
    const auto panes = window.findChildren<VDVideoDisplayWidget*>();
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    if (panes.size() != 2 || !position) return false;
    auto displays = [&](int frame) {
        return panes.first()->frameImage().convertToFormat(QImage::Format_RGBA8888) == expected[frame];
    };
    auto refreshed = [&](int frame, const char *message) {
        return check(waitFor([&] { return displays(frame); }) && position->GetPosition() == 0, message);
    };
    if (!refreshed(0, "frame-zero preview is ready")) return false;
    position->SetSelection(16, 24);
    if (!invoke(window, "onEditCropToSelection")
        || !refreshed(16, "crop refreshes changed source mapping without moving frame-zero playhead")
        || !invoke(window, "onEditUndo") || !refreshed(0, "undo refreshes the same numeric position")
        || !invoke(window, "onEditRedo") || !refreshed(16, "redo refreshes the same numeric position")
        || !invoke(window, "onEditResetTimeline") || !refreshed(0, "reset refreshes source identity")) return false;
    position->SetSelection(0, 8);
    if (!invoke(window, "onEditDelete") || !refreshed(8, "delete refreshes the held playhead")) return false;
    position->SetSelection(8, 16); // These are source frames 16..23 after deletion.
    if (!invoke(window, "onEditCopy")) return false;
    position->SetSelection(0, 0);
    if (!invoke(window, "onEditPaste") || !refreshed(16, "paste refreshes frame zero")) return false;
    if (!invoke(window, "onEditResetTimeline")) return false;
    position->SetSelection(16, 24);
    // Undo before the requested cropped frame returns. Old-generation results
    // must not repaint once the restored identity result has been delivered.
    if (!invoke(window, "onEditCropToSelection") || !invoke(window, "onEditUndo")) return false;
    QElapsedTimer settle; settle.start();
    return check(waitFor([&] { return settle.elapsed() >= 150; }) && displays(0),
                 "a stale in-flight crop decode cannot repaint after undo");
}

bool projectValidation(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4) || !invoke(window, "onEditSelectAll")) return false;
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    if (!position) return false;
    position->SetPosition(8);
    position->SetSelection(4, 16);
    position->SetZoomRange(0, 24);
    if (!invoke(window, "onEditToggleMarker")) return false;
    const QString original = fixtures.directory.filePath("original-session.vdqproject");
    if (!chooseProjectFile(window, "onFileSaveProjectAs", original)) return false;
    const QJsonObject before = QJsonDocument::fromJson(readFile(original)).object();
    VDQtProjectState candidate;
    QString error;
    if (!VDQtProjectFile::loadProject(original, &candidate, &error)) return false;
    candidate.sourcePath = fixtures.avs;
    candidate.sourcePaths = {fixtures.avs};
    candidate.audioDisabled = true;
    candidate.timelineExplicit = true;
    candidate.timelineSegments = {{0, 8}};
    candidate.sourceFrameCountExact = true;
    candidate.sourceFrameCount = 100;
    candidate.position = 0;
    candidate.hasSelection = false; candidate.selectionStart = candidate.selectionEnd = 0;
    candidate.zoomEnabled = false; candidate.zoomStart = candidate.zoomEnd = 0;
    candidate.markers.clear();
    candidate.processing.videoMode = VideoMode_FastRecompress;
    const QString invalid = fixtures.directory.filePath("invalid-references.vdqproject");
    for (int failure = 0; failure < 2; ++failure) {
        if (failure == 1) {
            candidate.sourceFrameCount = 48;
            candidate.timelineSegments = {{50, 8}};
        }
        if (!VDQtProjectFile::saveProject(invalid, candidate, &error)
            || !chooseProjectFile(window, "onFileLoadProject", invalid, true)) return false;
        if (!check(window.windowTitle().contains("source.mp4") && position->GetPosition() == 8,
                   "invalid project does not replace source or playhead")) return false;
        if (!invoke(window, "onFileSaveProject")) return false;
        const QJsonObject after = QJsonDocument::fromJson(readFile(original)).object();
        if (!check(after == before, "invalid project preserves selection, zoom, markers, settings and saved-project path")) return false;
    }
    // Validation succeeds, then the selected audio disappears during Open's
    // real UI event dispatch. Commit must roll back, not leave the AVS installed.
    const QString disappearingAudio = fixtures.directory.filePath("late-audio.wav");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "sine=frequency=440:duration=0.1",
                          "-c:a", "pcm_s16le", disappearingAudio})) return false;
    candidate.timelineSegments = {{0, 8}};
    candidate.audioDisabled = false;
    candidate.audioSourcePath = disappearingAudio;
    candidate.audioStreamIndex = 0;
    if (!VDQtProjectFile::saveProject(invalid, candidate, &error)) return false;
    bool removed = false;
    QTimer disappearing;
    disappearing.setInterval(0);
    QObject::connect(&disappearing, &QTimer::timeout, &window, [&] {
        if (removed) return;
        // The original zoom survives validation and is cleared only by source
        // replacement. Open dispatches queued events during window sizing.
        if (!position->HasZoomRange()) removed = QFile::remove(disappearingAudio);
    });
    disappearing.start();
    const bool rejected = chooseProjectFile(window, "onFileLoadProject", invalid, true);
    disappearing.stop();
    if (!check(removed && rejected && window.windowTitle().contains("source.mp4")
               && position->GetPosition() == 8, "late audio-open failure rolls back the original session")
        || !invoke(window, "onFileSaveProject")
        || !check(QJsonDocument::fromJson(readFile(original)).object() == before,
                  "late commit rollback preserves the complete saved editor state")) return false;
    const auto panes = window.findChildren<VDVideoDisplayWidget*>();
    return check(waitFor([&] { return !panes.first()->frameImage().isNull(); }),
                 "original editor remains usable after rejected project loads");
}

bool rawLifetime(VDQtTestFixtures& fixtures) {
    const QString raw = fixtures.directory.filePath("source.rgb");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=64x48:rate=24",
                          "-frames:v", "4", "-pix_fmt", "rgb24", "-c:v", "rawvideo",
                          "-threads", "1", "-f", "rawvideo", raw})) return false;
    const QByteArray original = readFile(raw);
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4)) return false;
    auto *queue = window.findChild<VDQtJobQueue*>();
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    const auto panes = window.findChildren<VDVideoDisplayWidget*>();
    if (!queue || !position || panes.isEmpty()) return false;
    VDQtJobState job;
    job.operation = VDQtJobOperation::VideoAnalysis;
    job.sourcePaths = {raw};
    job.rawPixelFormat = "rgb24";
    job.rawWidth = 64; job.rawHeight = 48; job.rawFrameRate = 24;
    job.audioDisabled = true;
    if (!queue->replaceJobs({job})) return false;
    const auto reload = [&] {
        return QMetaObject::invokeMethod(&window, "reloadQueuedJob", Qt::DirectConnection,
                                         Q_ARG(int, 0));
    };
    // Read the existing application log instead of adding a private test API
    // or persisting ephemeral copies in the recent-file menu.
    const auto currentMaterialization = [&] {
        const auto *text = VDLogWindow::instance(&window)->findChild<QTextEdit*>();
        if (!text) return QString();
        const QString entries = text->toPlainText();
        const QString prefix = QStringLiteral("[File] Opened video stream: ");
        const qsizetype last = entries.lastIndexOf(prefix);
        if (last < 0) return QString();
        const QString entry = entries.mid(last + prefix.size()).section('\n', 0, 0);
        return entry.left(entry.indexOf(" ("));
    };
    if (!reload() || !invoke(window, "onEditSelectAll")) return false;
    QString active = currentMaterialization();
    if (!check(active != raw && QFileInfo::exists(active)
               && QFileInfo(active).fileName() == "source.nut"
               && position->GetRangeEnd() == 3,
               "queued raw reload opens an owned materialized source")) return false;
    if (!check(!QSettings("VirtualDub", "VirtualDub_Port").value("recentFiles")
                   .toStringList().contains(active),
               "recent-file history never persists an owned raw copy")) return false;
    const QString session = QFileInfo(QFileInfo(active).absolutePath()).absolutePath();
    const auto materializations = [&] {
        return QDir(session).entryList({"raw-*"}, QDir::Dirs | QDir::NoDotAndDotDot).size();
    };
    if (!invoke(window, "onFileReopen")
        || !check(QFileInfo::exists(active) && materializations() == 1,
                  "reopening a raw source retains rather than deletes or recopies its lease")) return false;
    for (int iteration = 0; iteration < 8; ++iteration) {
        const QString prior = active;
        if (!reload() || !invoke(window, "onEditSelectAll")) return false;
        active = currentMaterialization();
        if (!check(active != prior && !QFileInfo::exists(prior) && QFileInfo::exists(active)
                   && materializations() == 1,
                   "successive raw loads release the previous file immediately")) return false;
    }
    position->SetZoomRange(0, 3);
    const QString project = fixtures.directory.filePath("raw-session.vdqproject");
    if (!chooseProjectFile(window, "onFileSaveProjectAs", project)) return false;
    VDQtProjectState saved;
    QString error;
    if (!VDQtProjectFile::loadProject(project, &saved, &error)
        || !check(saved.sourcePath == raw && saved.rawPixelFormat == "rgb24",
                  "projects save the original raw input, not an ephemeral copy")) return false;
    const QString prior = active;
    if (!chooseProjectFile(window, "onFileLoadProject", project)) return false;
    active = currentMaterialization();
    if (!check(active != prior && !QFileInfo::exists(prior) && QFileInfo::exists(active)
               && materializations() == 1, "successful raw project restore releases old and validation leases")) return false;

    // A late project failure must keep the raw source alive for rollback even
    // though normal replacement closes it before opening the candidate.
    position->SetZoomRange(0, 3);
    saved.sourcePath = fixtures.avs; saved.sourcePaths = {fixtures.avs};
    saved.rawPixelFormat.clear();
    saved.sourceFrameCount = 48; saved.sourceFrameCountExact = true;
    saved.timelineExplicit = true; saved.timelineSegments = {{0, 4}};
    saved.audioDisabled = false; saved.audioStreamIndex = 0;
    saved.audioSourcePath = fixtures.directory.filePath("raw-rollback-audio.wav");
    const QString candidate = fixtures.directory.filePath("raw-rollback.vdqproject");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "sine=duration=0.1", "-c:a", "pcm_s16le",
                          saved.audioSourcePath})
        || !VDQtProjectFile::saveProject(candidate, saved, &error)) return false;
    bool removed = false;
    QTimer disappearing;
    disappearing.setInterval(0);
    QObject::connect(&disappearing, &QTimer::timeout, &window, [&] {
        if (!removed && !position->HasZoomRange()) removed = QFile::remove(saved.audioSourcePath);
    });
    disappearing.start();
    const bool rejected = chooseProjectFile(window, "onFileLoadProject", candidate, true);
    disappearing.stop();
    if (!check(removed && rejected && QFileInfo::exists(active) && materializations() == 1
               && position->GetRangeEnd() == 3 && position->HasZoomRange(),
               "late project failure restores the still-owned raw source")
        || !check(waitFor([&] { return !panes.first()->frameImage().isNull(); }),
                  "restored raw source remains previewable")) return false;
    if (!invoke(window, "onFileClose")
        || !check(!QFileInfo::exists(active) && materializations() == 0,
                  "Close removes raw copies after decoder and preview consumers stop")) return false;
    if (!window.openVideoFile(fixtures.mp4) || !invoke(window, "runPendingJobs")) return false;
    return check(queue->jobAt(0)->status == VDQtJobStatus::Complete
                 && materializations() == 0 && readFile(raw) == original,
                 "offline raw analysis releases its own copy without changing the original input");
}

bool vfrJump(VDQtTestFixtures& fixtures) {
    const QString media = fixtures.directory.filePath("jump-vfr.mkv");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc=size=64x48:rate=30", "-frames:v", "3",
            "-vf", "settb=1/1000,setpts=N*10", "-enc_time_base", "1:1000", "-fps_mode", "passthrough",
            "-c:v", "ffv1", "-threads", "1", "-an", media})) return false;
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(media)) return false;
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    if (!position) return false;
    const auto jump = [&](const QString& text, qint64 expected, const QString& currentTime) {
        bool answered = false, correctTime = false, blocked = false;
        QTimer responder;
        responder.setInterval(5);
        QObject::connect(&responder, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<VDJumpToPositionDialog*>(QApplication::activeModalWidget());
            if (!dialog || answered) return;
            answered = true;
            auto *time = dialog->findChild<QLineEdit*>("jumpFrameTime");
            correctTime = time && time->text() == currentTime;
            // A modal dialog dispatches events: source replacement must not
            // invalidate the borrowed indexed timing callback while it runs.
            blocked = !window.openVideoFile(fixtures.avs);
            if (!time) { dialog->reject(); return; }
            for (auto *radio : dialog->findChildren<QRadioButton*>())
                if (radio->text().contains("at time")) radio->setChecked(true);
            time->setText(text);
            dialog->accept();
        });
        responder.start();
        const bool invoked = invoke(window, "onEditJumpToPosition");
        responder.stop();
        return check(invoked && answered && correctTime && blocked && position->GetPosition() == expected,
                     "real jump dialog uses edited VFR boundaries and holds source ownership");
    };
    if (!jump("20 ms", 2, "0:00.000")) return false;
    QString error;
    if (!window.runAutomationText("VirtualDub.subset.Clear(); VirtualDub.subset.AddRange(2,1); "
                                  "VirtualDub.subset.AddRange(0,2);", fixtures.directory.path(), &error)) return false;
    position->SetPosition(1);
    return jump("45 ms", 2, "0:00.033");
}

bool resizeScript(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4)) return false;
    QString error;
    if (!window.runAutomationText("VirtualDub.video.filters.Clear(); VirtualDub.video.filters.Add(\"resize\"); "
        "VirtualDub.video.filters.instance[0].Config(160,180,0); VirtualDub.video.filters.Add(\"resize\"); "
        "VirtualDub.video.filters.instance[1].Config(80,99,0,4,3,1,80,99,4,3,0,0,8,0);",
        fixtures.directory.path(), &error)) return check(false, error.toUtf8().constData());
    const auto& chain = VDQtFilterSystem::instance().getActiveChain();
    if (!check(chain.size() == 2 && chain.at(0).params.value("height") == 180
               && chain.at(1).params.value("width") == 80 && chain.at(1).params.value("height") == 88
               && chain.at(1).params.value("codecAdjust") == 8,
               "long resize scripts resolve aspect and alignment against the preceding stage")) return false;
    if (!window.runAutomationText("VirtualDub.video.filters.instance[1].Config(23,11,0);",
                                  fixtures.directory.path(), &error)) return false;
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(fixtures.mp4)) return false;
    VDQtFilterSystem filters;
    filters.replaceActiveChainTransient(VDQtFilterSystem::instance().getActiveChain());
    return check(filters.processFrame(decoder.getFrameImage(0)).size() == QSize(23, 11),
                 "short resize signature keeps explicit dimensions and clears prior aspect/alignment controls");
}

bool externalEncoderLogs(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4)) return false;
    const QString script = fixtures.directory.filePath("noisy-encoder.sh");
    const QByteArray noise = "dd if=/dev/zero bs=65536 count=96 2>/dev/null\n"
                             "printf 'FINAL_EXTERNAL_ENCODER_DIAGNOSTIC\\n' >&2\n";
    if (!fixtures.writeText(script, noise + "cp -- \"$1\" \"$2\"\n")) return false;
    QSettings settings;
    settings.beginGroup("ExternalEncoderSets/test-noisy");
    settings.setValue("program", "/bin/sh");
    settings.setValue("arguments", QString("\"%1\" \"{input}\" \"{output}\"").arg(script));
    settings.endGroup();
    QString error;
    if (!window.runAutomationText("VirtualDub.ExportViaEncoderSet(\"noisy-success.mkv\",\"test-noisy\");",
                                  fixtures.directory.path(), &error))
        return check(false, error.toUtf8().constData());
    VDQtVideoDecoder result;
    if (!check(result.openFile(fixtures.directory.filePath("noisy-success.mkv"))
               && !result.getFrameImage(0).isNull(), "verbose external encoder still commits a usable output")) return false;
    if (!fixtures.writeText(script, noise + "exit 7\n")) return false;
    return check(!window.runAutomationText("VirtualDub.ExportViaEncoderSet(\"noisy-failure.mkv\",\"test-noisy\");",
                                          fixtures.directory.path(), &error)
                 && error.contains("FINAL_EXTERNAL_ENCODER_DIAGNOSTIC")
                 && error.size() <= 17000
                 && !QFileInfo::exists(fixtures.directory.filePath("noisy-failure.mkv")),
                 "external failure retains the final diagnostic without publishing partial output");
}

bool appendState(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4) || !invoke(window, "onEditSelectAll")) return false;
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    if (!position) return false;
    QString error;
    if (!window.runAutomationText("VirtualDub.audio.SetSource(0);", fixtures.directory.path(), &error)) return false;
    position->SetPosition(8);
    position->SetSelection(4, 16);
    position->SetZoomRange(0, 24);
    if (!invoke(window, "onEditToggleMarker")) return false;
    const QString beforePath = fixtures.directory.filePath("before-append.vdqproject");
    if (!chooseProjectFile(window, "onFileSaveProjectAs", beforePath)) return false;
    VDQtProjectState before;
    if (!VDQtProjectFile::loadProject(beforePath, &before, &error)) return false;
    if (!window.runAutomationText("VirtualDub.Append(\"source.mp4\");", fixtures.directory.path(), &error))
        return check(false, error.toUtf8().constData());
    QString afterPath = fixtures.directory.filePath("after-append.vdqproject");
    if (!chooseProjectFile(window, "onFileSaveProjectAs", afterPath)) return false;
    VDQtProjectState after;
    if (!VDQtProjectFile::loadProject(afterPath, &after, &error)) return false;
    if (!check(after.sourcePaths.size() == 2 && after.sourceFrameCount == 96,
               "append includes both complete sources")
        || !check(after.audioDisabled && after.markers == before.markers,
                  "append preserves disabled audio and existing source markers")
        || !check(after.position == before.position && after.hasSelection == before.hasSelection
                  && after.selectionStart == before.selectionStart && after.selectionEnd == before.selectionEnd
                  && after.zoomEnabled == before.zoomEnabled && after.zoomStart == before.zoomStart
                  && after.zoomEnd == before.zoomEnd,
                  "append preserves playhead, selection and zoom")) return false;
    // A failed attempt must keep that two-source session and its project path.
    if (!check(!window.runAutomationText("VirtualDub.Append(\"missing.mp4\");",
                                        fixtures.directory.path(), &error), "missing append fails cleanly")
        || !invoke(window, "onFileSaveProject")) return false;
    bool cancelled = false;
    QTimer cancel;
    cancel.setInterval(0);
    QObject::connect(&cancel, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *progress = qobject_cast<QProgressDialog*>(widget);
            if (progress && progress->isVisible()
                && progress->labelText().contains("Validating appended")) {
                cancelled = true;
                progress->cancel();
            }
        }
    });
    cancel.start();
    const bool appended = window.runAutomationText("VirtualDub.Append(\"source.mp4\");",
                                                   fixtures.directory.path(), &error);
    cancel.stop();
    if (!check(cancelled && !appended && error.contains("cancel", Qt::CaseInsensitive),
               "append validation cancellation keeps the original session")
        || !invoke(window, "onFileSaveProject")) return false;
    VDQtProjectState retained;
    if (!check(VDQtProjectFile::loadProject(afterPath, &retained, &error)
                 && retained.sourcePaths == after.sourcePaths && retained.audioDisabled
                 && retained.markers == after.markers && retained.zoomEnabled,
                 "failed append retains the complete original session")) return false;
    const QString externalAudio = fixtures.directory.filePath("append-audio.wav");
    afterPath = fixtures.directory.filePath("external-append.vdqproject");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "sine=frequency=440:duration=0.1",
                          "-c:a", "pcm_s16le", externalAudio})
        || !window.runAutomationText("VirtualDub.audio.SetSource(\"append-audio.wav\");",
                                     fixtures.directory.path(), &error)
        || !window.runAutomationText("VirtualDub.Append(\"source.mp4\");",
                                     fixtures.directory.path(), &error)
        || !chooseProjectFile(window, "onFileSaveProjectAs", afterPath)
        || !VDQtProjectFile::loadProject(afterPath, &retained, &error)) return false;
    if (!check(!retained.audioDisabled && retained.audioSourcePath == externalAudio
               && retained.audioStreamIndex == 0 && retained.sourceFrameCount == 144,
               "append preserves external audio and its selected stream")) return false;
    position->SetSelection(16, 24);
    if (!invoke(window, "onEditCropToSelection")) return false;
    position->SetSelection(0, 4);
    if (!invoke(window, "onEditCopy")) return false;
    position->SetSelection(0, 0);
    afterPath = fixtures.directory.filePath("edited-append.vdqproject");
    if (!window.runAutomationText("VirtualDub.Append(\"source.mp4\");",
                                  fixtures.directory.path(), &error)
        || !invoke(window, "onEditPaste")
        || !chooseProjectFile(window, "onFileSaveProjectAs", afterPath)
        || !VDQtProjectFile::loadProject(afterPath, &retained, &error)) return false;
    qint64 outputFrames = 0;
    for (const auto& segment : retained.timelineSegments) outputFrames += segment.frameCount;
    return check(retained.sourceFrameCount == 192 && outputFrames == 60
                 && retained.timelineSegments.first().sourceStartFrame == 16,
                 "append preserves genuine edits and the frame clipboard for subsequent paste");
}

bool sourceLifetime(VDQtTestFixtures& fixtures) {
    const QString audioAvs = fixtures.directory.filePath("source-audio.avs");
    QByteArray script = readFile(fixtures.avs);
    script.replace("audio_rate=0", "audio_rate=48000");
    if (!fixtures.writeText(audioAvs, script)) return false;
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    const auto panes = window.findChildren<VDVideoDisplayWidget*>();
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    if (!check(panes.size() == 2 && position, "preview/controller available")) return false;
    for (int cycle = 0; cycle < 12; ++cycle) {
        if (!window.openVideoFile(audioAvs)
            || !waitFor([&] { return !panes.first()->frameImage().isNull(); })) return false;
        bool delivered = false, opened = false;
        QTimer::singleShot(0, &window, [&] {
            delivered = true;
            opened = window.openVideoFile(fixtures.mp4);
        });
        invoke(window, "onFileClose");
        // Before the fix Close dispatched this Open and then destroyed its new
        // source. A closed preview now stays closed until the outer call returns.
        if (!check(!delivered && panes.first()->frameImage().isNull(),
                   "Close does not dispatch queued Open during teardown")
            || !check(waitFor([&] { return delivered && opened
                    && !panes.first()->frameImage().isNull(); }),
                   "queued Open completes after Close")) return false;
        for (int i = 0; i < 10; ++i) {
            QMetaObject::invokeMethod(&window, "onTransportAction", Qt::DirectConnection,
                                      Q_ARG(int, VDQT_PCN_PLAY));
            QMetaObject::invokeMethod(&window, "onTransportAction", Qt::DirectConnection,
                                      Q_ARG(int, VDQT_PCN_STOP));
        }
    }
    // Open itself can dispatch events while sizing the window. Coalesce two
    // competing requests and load the latest only after the transition ends.
    bool attempted = false;
    QTimer::singleShot(0, &window, [&] {
        attempted = true;
        window.openVideoFile(fixtures.mp4);
        window.openVideoFile(fixtures.avs);
    });
    if (!window.openVideoFile(fixtures.mp4)) return false;
    return check(waitFor([&] { return attempted
            && window.windowTitle().contains("source.avs")
            && !panes.first()->frameImage().isNull(); }),
        "latest reentrant Open is applied after the transition");
}

bool exportSnapshot(VDQtTestFixtures& fixtures) {
    const auto chain = invertChain();
    VDQtFilterSystem::instance().replaceActiveChain(chain);
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(fixtures.avs)) return false;
    VDQtFilterSystem reference;
    reference.replaceActiveChainTransient(chain);
    const QImage expected = reference.processFrame(decoder.getFrameImage(0))
        .convertToFormat(QImage::Format_ARGB32);
    const QByteArray expectedFrame(reinterpret_cast<const char *>(expected.constBits()),
                                   expected.sizeInBytes());
    VDQtVideoExporter exporter;
    VDQtVideoExporter::RawExportOptions raw;
    raw.inputPath = fixtures.avs;
    raw.outputPath = fixtures.directory.filePath("snapshot.raw");
    raw.pixelFormat = "bgra";
    raw.endFrame = 7;
    raw.unattended = true;
    bool changed = false;
    if (!exporter.exportRawVideo(raw, &decoder, nullptr, nullptr,
            [&](int, int) {
                VDQtFilterSystem::instance().clearFilters();
                // Even the caller's request is allowed to go out of date. The
                // current operation must not consult it again after entry.
                raw.endFrame = 0;
                changed = true;
                return true;
            })) return check(false, "raw snapshot export succeeds");
    if (!check(changed && readFile(fixtures.directory.filePath("snapshot.raw"))
                    == expectedFrame.repeated(8), "raw output uses the captured chain/range")) return false;

    VDQtFilterSystem::instance().replaceActiveChain(chain);
    VDQtVideoExporter::ExportOptions video;
    video.inputPath = fixtures.avs;
    video.outputPath = fixtures.directory.filePath("snapshot.mkv");
    video.videoCodecOverride = "ffv1";
    video.videoPixelFormatOverride = "bgra";
    video.includeAudio = false;
    video.endFrame = 7;
    video.unattended = true;
    bool pixelsMatch = true;
    int framesSeen = 0;
    const bool result = exporter.exportVideo(video, &decoder, nullptr, nullptr,
        [&](int, const QImage&, const QImage& frame) {
            pixelsMatch &= frame.convertToFormat(QImage::Format_ARGB32) == expected;
            ++framesSeen;
            VDQtFilterSystem::instance().clearFilters();
        }, [&](int, int) {
            VDQtFilterSystem::instance().clearFilters();
            video.endFrame = 0;
            return true;
        });
    if (!check(result && framesSeen == 8 && pixelsMatch,
               "video output uses captured settings before its first callback")) return false;
    QWidget editor;
    editor.show();
    bool errorDismissed = false;
    QTimer::singleShot(0, [&] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(box, &enter);
            errorDismissed = !box->isVisible();
            // Always unblock the test process, even when the assertion fails.
            if (!errorDismissed) box->accept();
        }
    });
    return check(!exporter.exportRawVideo({}, nullptr, nullptr, &editor)
                    && errorDismissed, "editor locking leaves export error dialogs usable");
}

bool audioSnapshot(VDQtTestFixtures& fixtures) {
    const QString source = fixtures.directory.filePath("tone.wav");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "sine=frequency=440:duration=0.1:sample_rate=48000",
                          "-c:a", "pcm_s16le", source})) return false;
    VDQtAudioPlayer audio(false);
    if (!check(audio.openFile(source) && audio.hasAudio(), "offline audio opens without a playback sink"))
        return false;
    // With playback disabled there is intentionally no producer cursor/sink.
    // Exercise the public boundary and subsequent export, not an invented
    // audible-position assertion. Sample clamping itself has pure math tests.
    audio.seekToTimeSeconds(1e100);
    audio.seekToTimeSeconds(0);
    auto& editor = VDQtAudioFilterSystem::instance();
    auto mute = editor.createFilter(VDAudioFilterType::Gain);
    mute.params["decibels"] = -120;
    auto loud = mute;
    loud.params["decibels"] = 12;
    QList<VDAudioFilterInstance> captured{mute};
    editor.replaceActiveChain({loud});
    const QString single = fixtures.directory.filePath("muted.wav");
    if (!audio.exportAudioToFile(single, 0, 1024, [&](int, int) {
            captured = {loud};
            return true;
        }, &captured)) return check(false, "explicit audio snapshot exports");
    editor.replaceActiveChain({mute});
    const QString ranges = fixtures.directory.filePath("muted-ranges.wav");
    if (!audio.exportAudioRangesToFile(ranges, {{0, 512}, {512, 512}}, [&](int, int) {
            editor.replaceActiveChain({loud});
            return true;
        })) return check(false, "range audio snapshot exports");
    for (const QString& path : {single, ranges}) {
        const auto wav = readFile(path);
        const auto data = wav.indexOf("data", 12);
        if (!check(data >= 0 && data + 8 <= wav.size(), "WAV has PCM data")) return false;
        const quint32 bytes = qFromLittleEndian<quint32>(wav.constData() + data + 4);
        if (!check(bytes == 2048 && data + 8 + bytes <= wav.size(), "snapshot has exactly 1024 samples")) return false;
        for (quint32 offset = 0; offset < bytes; offset += 2) {
            const qint16 sample = qFromLittleEndian<qint16>(wav.constData() + data + 8 + offset);
            if (!check(std::abs(int(sample)) <= 1, "captured mute survives filter changes in callbacks")) return false;
        }
    }
    return true;
}

bool waveformPreview(VDQtTestFixtures& fixtures) {
    // Exact, constant plateaus make the chart itself an observable contract:
    // deleting/reordering a quarter changes its height, not just a file header.
    const QString source = fixtures.directory.filePath("waveform-source.mkv");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=32x24:rate=10:duration=2",
            "-f", "lavfi", "-i",
            "aevalsrc='if(lt(t,0.5),0.125,if(lt(t,1),0.75,if(lt(t,1.5),0.375,0.5)))':s=48000:d=2",
            "-c:v", "ffv1", "-c:a", "pcm_f32le", source})) return false;
    const QByteArray original = readFile(source);
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(source) || !invoke(window, "onEditSelectAll")) return false;
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    if (!position || position->GetRangeEnd() != 19) return false;
    position->SetPosition(7);
    position->SetSelection(0, 20);
    position->SetZoomRange(0, 19);

    enum class Outcome { Chart, Cancelled, Error };
    const auto capture = [&](Outcome expected, QImage *result) {
        bool shown = false, cancelled = false, errorShown = false, guarded = true;
        QString diagnostic;
        const QString title = window.windowTitle();
        const qint64 playhead = position->GetPosition(), last = position->GetRangeEnd();
        const qint64 selectionStart = position->GetSelectionStart();
        const qint64 selectionEnd = position->GetSelectionEnd();
        QElapsedTimer deadline;
        deadline.start();
        QTimer responder;
        responder.setInterval(1);
        QObject::connect(&responder, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *progress = qobject_cast<QProgressDialog*>(widget)) {
                    if (progress->isVisible() && progress->labelText().contains("waveform")) {
                        guarded &= !window.menuBar()->isEnabled() && !position->isEnabled();
                        if (expected == Outcome::Cancelled || deadline.elapsed() > 10000) {
                            cancelled = true;
                            progress->cancel();
                        }
                    }
                    continue;
                }
                if (auto *message = qobject_cast<QMessageBox*>(widget)) {
                    if (!message->isVisible()) continue;
                    errorShown = true;
                    diagnostic = message->text();
                    message->accept();
                    continue;
                }
                auto *dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || !dialog->isVisible() || dialog->windowTitle() != "Audio Waveform") continue;
                shown = true;
                guarded &= !window.menuBar()->isEnabled() && !position->isEnabled();
                // These are real public slots, not direct edits of controller
                // internals. Nested modal events cannot close or mutate the source.
                guarded &= invoke(window, "onFileClose") && invoke(window, "onEditResetTimeline")
                    && invoke(window, "onEditDelete");
                guarded &= window.windowTitle() == title && position->GetRangeEnd() == last
                    && position->GetPosition() == playhead
                    && position->GetSelectionStart() == selectionStart
                    && position->GetSelectionEnd() == selectionEnd;
                for (QLabel *label : dialog->findChildren<QLabel*>()) {
                    const QPixmap pixmap = label->pixmap();
                    if (!pixmap.isNull() && pixmap.size() == QSize(1100, 360) && result)
                        *result = pixmap.toImage();
                }
                dialog->reject();
            }
        });
        responder.start();
        const bool invoked = invoke(window, "onViewAudioWaveform");
        responder.stop();
        if (errorShown && expected != Outcome::Error)
            std::cerr << "Unexpected waveform diagnostic: " << diagnostic.toStdString() << '\n';
        return check(invoked && guarded && window.menuBar()->isEnabled() && position->isEnabled()
            && position->GetPosition() == playhead && position->GetRangeEnd() == last
            && position->GetSelectionStart() == selectionStart && position->GetSelectionEnd() == selectionEnd
            && (expected == Outcome::Chart ? shown && !errorShown && result && !result->isNull()
                : expected == Outcome::Cancelled ? cancelled && !shown && !errorShown
                : errorShown && !shown && !diagnostic.isEmpty()),
            "waveform modal operation preserves editor state, releases ownership, and reports the requested outcome");
    };
    const auto amplitudeAt = [](const QImage& chart, int x) {
        for (int y = 30; y <= 180; ++y)
            if (chart.pixelColor(x, y) == QColor(0, 205, 225))
                return (180 - y) / 162.0;
        return -1.0;
    };
    const auto peaksMatch = [&](const QImage& chart, const QList<double>& peaks) {
        for (int i = 0; i < peaks.size(); ++i) {
            const int x = static_cast<int>((i + 0.5) * chart.width() / peaks.size());
            if (std::abs(amplitudeAt(chart, x) - peaks[i]) >= 0.012)
                std::cerr << "Waveform x=" << x << " actual=" << amplitudeAt(chart, x)
                          << " expected=" << peaks[i] << '\n';
            if (!check(std::abs(amplitudeAt(chart, x) - peaks[i]) < 0.012,
                       "rendered waveform peaks follow included source intervals, including masks")) return false;
        }
        return true;
    };
    const auto captionMatches = [](const QImage& chart, double duration, double start, int rate) {
        // Re-render only the caption using the same public Qt image painter.
        // No OCR, private controller hook, or hidden chart metadata is required.
        QImage expected(chart.size(), QImage::Format_ARGB32_Premultiplied);
        expected.fill(QColor(18, 18, 24));
        QPainter painter(&expected);
        painter.setPen(QColor(210, 210, 220));
        painter.drawText(10, 18,
            QString("%1 s from timeline %2 s — %3 Hz, %4 channel(s), %5-bit")
                .arg(duration, 0, 'f', 3).arg(start, 0, 'f', 3).arg(rate).arg(1).arg(32));
        painter.end();
        return check(chart.copy(0, 0, chart.width(), 28).convertToFormat(QImage::Format_RGB32)
            == expected.copy(0, 0, expected.width(), 28).convertToFormat(QImage::Format_RGB32),
            "waveform caption reports the filtered output's actual duration and sample rate");
    };

    const QString project = fixtures.directory.filePath("waveform-session.vdqproject");
    if (!chooseProjectFile(window, "onFileSaveProjectAs", project)) return false;
    QJsonObject saved = QJsonDocument::fromJson(readFile(project)).object();
    QImage chart;
    if (!capture(Outcome::Chart, &chart) || !peaksMatch(chart, {0.125, 0.75, 0.375, 0.5})
        || !captionMatches(chart, 2, 0, 48000) || !invoke(window, "onFileSaveProject")
        || !check(QJsonDocument::fromJson(readFile(project)).object() == saved,
                  "waveform inspection leaves the complete saved session unchanged")) return false;
    position->SetSelection(0, 0);
    position->SetPosition(0);
    if (!capture(Outcome::Chart, &chart) || !captionMatches(chart, 2, 0, 48000)
        || !peaksMatch(chart, {0.125, 0.75, 0.375, 0.5})) return false;
    QString error;
    if (!window.runAutomationText(
            "VirtualDub.subset.Clear(); VirtualDub.subset.AddRange(15,5); "
            "VirtualDub.subset.AddMaskedRange(0,5); VirtualDub.subset.AddRange(10,5); "
            "VirtualDub.audio.filters.Clear(); VirtualDub.audio.filters.Add(\"resample\"); "
            "VirtualDub.audio.filters.instance[0].SetInt(0,24000);",
            fixtures.directory.path(), &error)) {
        std::cerr << error.toStdString() << '\n'; return false;
    }
    position->SetPosition(6);
    position->SetSelection(0, 15);
    position->SetZoomRange(0, 14);
    const auto effects = VDQtAudioFilterSystem::instance().activeChain();
    if (!invoke(window, "onFileSaveProject")) return false;
    saved = QJsonDocument::fromJson(readFile(project)).object();
    if (!capture(Outcome::Chart, &chart) || !peaksMatch(chart, {0.5, 0.125, 0.375})
        || !captionMatches(chart, 1.5, 0, 24000) || !invoke(window, "onFileSaveProject")
        || !check(readFile(source) == original
            && VDQtAudioFilterSystem::instance().activeChain() == effects
            && QJsonDocument::fromJson(readFile(project)).object() == saved,
            "edited/resampled waveform does not alter source media, effects, timeline, zoom or playhead")) return false;
    // Without a selection, start at the edited playhead, not source frame 5.
    position->SetSelection(0, 0);
    position->SetPosition(5);
    if (!capture(Outcome::Chart, &chart) || !peaksMatch(chart, {0.125, 0.375})
        || !captionMatches(chart, 1, 0.5, 24000) || !capture(Outcome::Cancelled, nullptr)) return false;

    // A one-sample external WAV is shorter than a single chart column. The
    // existing video/audio export contract pads a selected video interval with
    // silence; retain the real first sample and display that silent tail safely.
    const QString shortAudio = fixtures.directory.filePath("waveform-one-sample.wav");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "aevalsrc=0.25:s=48000:d=0.01",
            "-af", "atrim=end_sample=1", "-c:a", "pcm_s16le", shortAudio})
        || !window.runAutomationText(
            "VirtualDub.subset.Clear(); VirtualDub.subset.AddRange(0,20); "
            "VirtualDub.audio.filters.Clear(); VirtualDub.audio.SetSource(\"waveform-one-sample.wav\");",
            fixtures.directory.path(), &error)) return false;
    position->SetSelection(0, 20);
    position->SetPosition(3);
    if (!capture(Outcome::Chart, &chart) || !peaksMatch(chart, {0, 0, 0, 0})
        || !check(std::abs(amplitudeAt(chart, 0) - 0.25) < 0.012,
                  "one-sample audio retains its real first peak before the timeline's silent tail")
        || !captionMatches(chart, 2, 0, 48000)) return false;

    // Public configuration storage deliberately cannot guarantee every caller
    // passed validation. A bad effect must show an error, not be mistaken for
    // cancellation when the progress dialog is programmatically closed.
    auto invalid = VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::Gain);
    invalid.params["decibels"] = std::numeric_limits<double>::quiet_NaN();
    VDQtAudioFilterSystem::instance().replaceActiveChain({invalid});
    const bool diagnosed = capture(Outcome::Error, nullptr);
    VDQtAudioFilterSystem::instance().clear();
    return diagnosed && check(readFile(source) == original,
                               "all waveform success/cancel/error paths preserve original media");
}

bool exportPreview(VDQtTestFixtures& fixtures) {
    VDQtVideoDecoder reference;
    if (!reference.openFile(fixtures.mp4)) return false;
    const QImage raw = reference.getFrameImage(3).convertToFormat(QImage::Format_RGBA8888);
    VDQtFilterSystem processing;
    processing.replaceActiveChain(invertChain());
    const QImage filtered = processing.processFrame(raw).convertToFormat(QImage::Format_RGBA8888);
    if (raw.isNull() || filtered.isNull()) return false;
    const QByteArray original = readFile(fixtures.mp4);
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    QString error;
    if (!window.openVideoFile(fixtures.mp4)
        || !window.runAutomationText(
            "VirtualDub.video.SetMode(3); VirtualDub.audio.SetSource(0); "
            "VirtualDub.subset.Clear(); VirtualDub.subset.AddRange(16,4); VirtualDub.subset.AddRange(0,4); "
            "VirtualDub.video.filters.Clear(); VirtualDub.video.filters.Add(\"invert\");",
            fixtures.directory.path(), &error)) return false;
    VDQtCodecEngine::instance().setVideoParams(VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1"));
    VDSaveVideoSessionConfig save;
    save.fileTypeIndex = 2; // Matroska, compatible with the lossless fixture codec.
    VDQtCodecSettings::instance().setSaveVideoSessionConfig(save);
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    auto *inputPreview = window.findChild<VDVideoDisplayWidget*>("inputPreview");
    auto *outputPreview = window.findChild<VDVideoDisplayWidget*>("outputPreview");
    if (!position || !inputPreview || !outputPreview) return false;
    position->SetSelection(0, 0);
    position->SetPosition(1);
    const QString output = fixtures.directory.filePath("edited-export-preview.mkv");
    bool chosen = false, completed = false, correct = true;
    QTimer responder;
    responder.setInterval(5);
    QObject::connect(&responder, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<VDSaveVideoDialog*>(widget)) {
                if (!dialog->isVisible() || chosen) continue;
                const auto lines = dialog->findChildren<QLineEdit*>();
                if (lines.size() != 1) { correct = false; dialog->reject(); continue; }
                lines.first()->setText(output);
                chosen = true;
                dialog->accept();
            } else if (auto *message = qobject_cast<QMessageBox*>(widget)) {
                if (!message->isVisible()) continue;
                completed = message->windowTitle() == "Export Complete";
                const bool rawMatches = inputPreview->frameImage().convertToFormat(QImage::Format_RGBA8888) == raw;
                const bool filteredMatches = outputPreview->frameImage().convertToFormat(QImage::Format_RGBA8888) == filtered;
                const bool finalState = completed && position->GetPosition() == 7
                    && rawMatches && filteredMatches
                    && !window.menuBar()->isEnabled() && !position->isEnabled();
                if (!finalState)
                    std::cerr << "Export preview: dialog=" << message->windowTitle().toStdString()
                              << " playhead=" << position->GetPosition() << " raw=" << rawMatches
                              << " filtered=" << filteredMatches << " menu=" << window.menuBar()->isEnabled()
                              << " position=" << position->isEnabled() << '\n';
                if (!rawMatches) {
                    const QImage actual = inputPreview->frameImage().convertToFormat(QImage::Format_RGBA8888);
                    std::cerr << "Raw format=" << inputPreview->frameImage().format()
                              << " size=" << actual.width() << 'x' << actual.height()
                              << " swapped=" << (actual == filtered)
                              << " actualRGB=" << actual.pixelColor(0, 0).name().toStdString()
                              << " expectedRGB=" << raw.pixelColor(0, 0).name().toStdString() << '\n';
                    for (int candidate = 0; candidate < 24; ++candidate)
                        if (reference.getFrameImage(candidate).convertToFormat(QImage::Format_RGBA8888) == actual)
                            std::cerr << "Actual raw matches source ordinal " << candidate << '\n';
                    if (actual.size() == raw.size()) {
                        int maximumDifference = 0;
                        for (int y = 0; y < raw.height(); ++y)
                            for (int x = 0; x < raw.width() * 4; ++x)
                                maximumDifference = std::max(maximumDifference,
                                    std::abs(int(actual.constScanLine(y)[x]) - int(raw.constScanLine(y)[x])));
                        std::cerr << "Export raw maximum pixel difference=" << maximumDifference << '\n';
                    }
                }
                correct &= finalState;
                // Inspect before the completion dialog returns, when the final
                // forced export repaint is visible and ownership is still held.
                message->accept();
            }
        }
    });
    responder.start();
    const bool invoked = invoke(window, "onFileSaveAVI");
    responder.stop();
    if (!check(invoked && chosen && completed && correct
        && window.menuBar()->isEnabled() && position->isEnabled()
        && readFile(fixtures.mp4) == original,
        "actual Save Video finishes on the final edited ordinal with matching raw/filtered previews")) return false;
    VDQtVideoDecoder encoded;
    return check(encoded.openFile(output) && encoded.ensureFrameIndex().totalFrames == 8
        && !encoded.getFrameImage(7).isNull(), "the preview-tested export contains all edited frames");
}

bool queueIsolation(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4)) return false;
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    auto *queue = window.findChild<VDQtJobQueue*>();
    if (!position || !queue) return false;
    VDQtFilterSystem::instance().clearFilters();
    VDQtFilterSystem::instance().addFilter(VDFilterType::Grayscale);
    const auto originalChain = VDQtFilterSystem::instance().getActiveChain();
    const QString title = window.windowTitle();
    QDialog previousSettingsDialog(&window);
    const qint64 range = position->GetRangeEnd();
    VDQtJobState job;
    job.operation = VDQtJobOperation::RawVideoExport;
    job.sourcePaths = {fixtures.avs};
    job.audioDisabled = true;
    job.options.outputPath = fixtures.directory.filePath("queued.raw");
    job.options.endFrame = 7;
    job.processing.filters = invertChain();
    job.processing.rawVideo.pixelFormat = "bgra";
    if (!queue->addJobs({job})) return false;
    bool exercised = false, blocked = true, editorUnchanged = true;
    QObject::connect(queue, &VDQtJobQueue::jobChanged, &window, [&](int row) {
        const auto *running = queue->jobAt(row);
        if (exercised || !running || running->status != VDQtJobStatus::Running) return;
        exercised = true;
        blocked &= previousSettingsDialog.property("vdqtExistingOperationDialog").toBool();
        blocked &= !window.openVideoFile(fixtures.avs);
        QString error;
        blocked &= !window.runAutomationText("VirtualDub.Close();", fixtures.directory.path(), &error);
        blocked &= !error.isEmpty();
        invoke(window, "onFileClose");
        invoke(window, "onEditDelete");
        invoke(window, "onFileExportFilmstrip"); // Must return before any dialog.
        invoke(window, "onVideoFilters");
        invoke(window, "runPendingJobs");
        QMetaObject::invokeMethod(&window, "reloadQueuedJob", Qt::DirectConnection, Q_ARG(int, 0));
        const auto& active = VDQtFilterSystem::instance().getActiveChain();
        editorUnchanged &= window.windowTitle() == title && position->GetRangeEnd() == range
            && active.size() == 1 && active.first().id == originalChain.first().id;
    });
    invoke(window, "runPendingJobs");
    if (!check(exercised && blocked && editorUnchanged, "jobs block reentrant editor actions")
        || !check(queue->jobAt(0)->status == VDQtJobStatus::Complete,
                  "job completes with its own processing chain")) return false;
    VDQtVideoDecoder source;
    if (!source.openFile(fixtures.avs)) return false;
    VDQtFilterSystem reference;
    reference.replaceActiveChainTransient(job.processing.filters);
    const QImage expected = reference.processFrame(source.getFrameImage(0))
        .convertToFormat(QImage::Format_ARGB32);
    const QByteArray expectedFrame(reinterpret_cast<const char *>(expected.constBits()),
                                   expected.sizeInBytes());
    if (!check(readFile(job.options.outputPath) == expectedFrame.repeated(8),
               "job pixels use job settings, not editor settings")) return false;
    const auto& after = VDQtFilterSystem::instance().getActiveChain();
    if (!check(after.size() == 1 && after.first().id == originalChain.first().id,
               "job does not replace the session chain")) return false;

    job.options.outputPath = fixtures.directory.filePath("aborted.raw");
    // Existing outputs from unresolved script dependencies are deliberately
    // conservative in queue validation. That separate source-safety contract
    // is not what this cancellation test exercises.
    queue->clearCompleted();
    QString error;
    if (!queue->addJobs({job}, &error)) {
        std::cerr << "FAIL: add abort job: " << error.toStdString() << '\n';
        return false;
    }
    QObject::connect(queue, &VDQtJobQueue::jobChanged, &window, [&](int row) {
        const auto *running = queue->jobAt(row);
        if (row == 0 && running && running->status == VDQtJobStatus::Running
            && running->progress >= 0.25)
            invoke(window, "abortCurrentJob");
    });
    invoke(window, "runPendingJobs");
    return check(queue->jobAt(0)->status == VDQtJobStatus::Cancelled
                    && !QFileInfo::exists(job.options.outputPath)
                    && window.menuBar()->isEnabled() && position->isEnabled()
                    && !previousSettingsDialog.property("vdqtExistingOperationDialog").toBool()
                    && window.openVideoFile(fixtures.avs),
                 "abort stays available and operation ownership is released");
}

bool audioInclusion(VDQtTestFixtures& fixtures) {
    const QString source = fixtures.directory.filePath("inclusion-source.mkv");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=96x64:rate=24:duration=0.5",
                          "-f", "lavfi", "-i", "sine=frequency=440:duration=0.5:sample_rate=48000",
                          "-c:v", "ffv1", "-c:a", "pcm_s16le", source})) return false;
    VDQtVideoDecoder decoder;
    VDQtAudioPlayer audio(false);
    if (!decoder.openFile(source) || !audio.openFile(source) || !audio.hasAudio()) return false;
    VDQtVideoExporter exporter;
    VDQtVideoExporter::ExportOptions options;
    options.inputPath = source;
    options.outputPath = fixtures.directory.filePath("video-only.mkv");
    options.includeAudio = false;
    options.audioMode = AudioMode_FullProcessing;
    options.videoCodecOverride = "ffv1";
    options.videoPixelFormatOverride = "yuv420p";
    options.endFrame = 3;
    options.unattended = true;
    VDQtVideoExporter::ProcessingSnapshot processing;
    options.processing = processing;
    if (!exporter.exportVideo(options, &decoder, &audio)) return check(false, "video-only rendered export succeeds");
    VDQtAudioPlayer probe(false);
    if (!check(probe.openFile(options.outputPath) && !probe.hasAudio(),
               "includeAudio=false cannot prepare or map a supplied audio player")) return false;
    for (const QString& animation : {QString("gif"), QString("apng")}) {
        options.outputPath = fixtures.directory.filePath("video-only." + animation);
        options.videoCodecOverride = animation;
        options.videoPixelFormatOverride = animation == "gif" ? "rgb8" : "rgba";
        options.containerType = animation;
        if (!check(exporter.exportVideo(options, &decoder, &audio),
                   "GIF/APNG with a supplied audio player remains video-only")) return false;
    }
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    auto *queue = window.findChild<VDQtJobQueue*>();
    if (!queue) return false;
    VDQtJobState job;
    job.operation = VDQtJobOperation::VideoExport;
    job.sourcePaths = {fixtures.mp4}; // Intentionally silent.
    job.audioDisabled = false;
    job.options.includeAudio = true;
    job.options.audioMode = AudioMode_FullProcessing;
    job.options.videoCodecOverride = "ffv1";
    job.options.videoPixelFormatOverride = "yuv420p";
    job.options.outputPath = fixtures.directory.filePath("silent-queue.mkv");
    job.options.endFrame = 3;
    QString error;
    if (!queue->addJobs({job}, &error)) return false;
    invoke(window, "runPendingJobs");
    if (!check(queue->jobAt(0)->status == VDQtJobStatus::Complete,
               "default audio inclusion does not make a silent queued video fail")) {
        std::cerr << queue->jobAt(0)->error.toStdString() << '\n';
        return false;
    }
    job.sourcePaths = {source};
    job.audioDisabled = true;
    job.options.outputPath = fixtures.directory.filePath("disabled-queue.mkv");
    if (!queue->addJobs({job}, &error)) return false;
    invoke(window, "runPendingJobs");
    if (!check(queue->jobAt(1)->status == VDQtJobStatus::Complete
               && probe.openFile(job.options.outputPath) && !probe.hasAudio(),
               "queue audioDisabled overrides stale includeAudio=true")) return false;
    job.audioDisabled = false;
    job.audioStreamIndex = 0; // A video stream, not a valid explicit audio choice.
    job.options.outputPath = fixtures.directory.filePath("invalid-audio-queue.mkv");
    if (!queue->addJobs({job}, &error)) return false;
    invoke(window, "runPendingJobs");
    if (!check(queue->jobAt(2)->status == VDQtJobStatus::Failed
               && !QFileInfo::exists(job.options.outputPath),
               "unavailable explicitly selected audio still fails instead of becoming silent")) return false;
    if (!window.openVideoFile(source)
        || !window.runAutomationText("VirtualDub.audio.SetSource(0);", fixtures.directory.path(), &error)) return false;
    bool dialogSeen = false, unexpected = false;
    QElapsedTimer deadline;
    deadline.start();
    QTimer responder;
    responder.setInterval(5);
    QObject::connect(&responder, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<VDSaveAudioDialog*>(widget); dialog && dialog->isVisible()) {
                dialogSeen = true;
                dialog->reject();
            } else if (auto *message = qobject_cast<QMessageBox*>(widget); message && message->isVisible()) {
                unexpected = true;
                message->reject();
            } else if (deadline.elapsed() > 5000) {
                if (auto *dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible()) {
                    unexpected = true;
                    dialog->reject();
                }
            }
        }
    });
    responder.start();
    invoke(window, "onFileSaveAudio");
    responder.stop();
    if (!check(dialogSeen && !unexpected, "disabled audio can be inspected in Save Audio and cancelled")) return false;
    if (!check(!window.runAutomationText("VirtualDub.SaveWAV(\"still-disabled.wav\");", fixtures.directory.path(), &error)
               && error.contains("no decodable audio")
               && !QFileInfo::exists(fixtures.directory.filePath("still-disabled.wav")),
               "Save Audio must not reopen the session's disabled live player")) return false;
    return true;
}

bool audioExportContracts(VDQtTestFixtures& fixtures) {
    const QString source = fixtures.directory.filePath("audio-export-source.mkv");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=32x24:rate=10:duration=2",
                          "-f", "lavfi", "-i", "sine=frequency=440:duration=2:sample_rate=44100",
                          "-c:v", "ffv1", "-c:a", "pcm_s16le", source})) return false;
    const auto probe = [](const QString& path) {
        QProcess process;
        process.start("ffprobe", {"-v", "error", "-select_streams", "a:0",
            "-show_entries", "stream=codec_name,sample_rate,channels,bits_per_sample,duration",
            "-of", "json", path});
        if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
            process.kill(); process.waitForFinished(); return QJsonObject();
        }
        const auto streams = QJsonDocument::fromJson(process.readAllStandardOutput())
            .object().value("streams").toArray();
        return streams.isEmpty() ? QJsonObject() : streams.first().toObject();
    };
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    auto *queue = window.findChild<VDQtJobQueue*>();
    if (!queue) return false;
    VDQtJobState job;
    job.operation = VDQtJobOperation::AudioExport;
    job.sourcePaths = {source};
    job.options.outputPath = fixtures.directory.filePath("converted.wav");
    job.processing.audioCodec.codecId = "pcm_s24le";
    job.processing.audioCodec.bitDepth = 24;
    job.processing.audioCodec.sampleRate = 48000;
    job.processing.audioCodec.channels = 2;
    QString error;
    if (!queue->addJobs({job}, &error)) return false;
    invoke(window, "runPendingJobs");
    auto info = probe(job.options.outputPath);
    if (!check(queue->jobAt(0)->status == VDQtJobStatus::Complete
               && info.value("codec_name") == "pcm_s24le"
               && info.value("sample_rate") == "48000"
               && info.value("channels").toInt() == 2,
               "queued audio honors requested 24-bit, rate and channel conversion")) return false;
    job.options.outputPath = fixtures.directory.filePath("encoded.mp3");
    job.processing.audioCodec.codecId = "libmp3lame";
    job.processing.audioCodec.bitrateKbps = 192;
    if (!queue->addJobs({job}, &error)) return false;
    invoke(window, "runPendingJobs");
    if (!check(queue->jobAt(1)->status == VDQtJobStatus::Complete
               && probe(job.options.outputPath).value("codec_name") == "mp3",
               "queued selected MP3 encodes rather than copying PCM into MP3")) return false;
    job.options.outputPath = fixtures.directory.filePath("edited.wav");
    job.processing.audioCodec = VDAudioCodecParams{};
    job.processing.audioCodec.codecId = "pcm_s16le";
    job.options.timelineExplicit = true;
    job.options.timelineSegments = {{12, 3, false}, {2, 4, true}};
    if (!queue->addJobs({job}, &error)) return false;
    invoke(window, "runPendingJobs");
    info = probe(job.options.outputPath);
    if (!check(queue->jobAt(2)->status == VDQtJobStatus::Complete
               && std::abs(info.value("duration").toString().toDouble() - 0.7) < 1e-5,
               "queued edited audio concatenates actual source intervals, including masks")) return false;
    if (!window.openVideoFile(source)) return false;
    const QString scriptPath = fixtures.directory.filePath("script-edited.wav");
    if (!window.runAutomationText(
            QString("VirtualDub.audio.SetCompression(); VirtualDub.subset.Clear(); "
                    "VirtualDub.subset.AddRange(12,3); VirtualDub.subset.AddMaskedRange(2,4); "
                    "VirtualDub.SaveWAV(\"%1\");").arg(scriptPath),
            fixtures.directory.path(), &error)) {
        std::cerr << error.toStdString() << '\n'; return false;
    }
    const QString queuedRaw = fixtures.directory.filePath("queued-audio.raw");
    const QString scriptRaw = fixtures.directory.filePath("script-audio.raw");
    if (!fixtures.ffmpeg({"-i", job.options.outputPath, "-f", "s16le", queuedRaw})
        || !fixtures.ffmpeg({"-i", scriptPath, "-f", "s16le", scriptRaw})) return false;
    if (!check(readFile(queuedRaw).size() == 30870 * 2
                 && readFile(queuedRaw) == readFile(scriptRaw),
                 "script and queue produce identical edited PCM samples")) return false;

    // Exercise the actual Save Audio dialog, not just its shared helper.
    const QString manualPath = fixtures.directory.filePath("manual-edited.wav");
    VDSaveAudioSessionConfig session;
    session.codecId = "pcm_s16le";
    VDQtCodecSettings::instance().setSaveAudioSessionConfig(session);
    bool chosen = false, saved = false, failed = false;
    QElapsedTimer deadline;
    deadline.start();
    QTimer responder;
    responder.setInterval(5);
    QObject::connect(&responder, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<VDSaveAudioDialog*>(widget); dialog && dialog->isVisible()) {
                if (deadline.elapsed() > 5000) { failed = true; dialog->reject(); continue; }
                if (!dialog->property("testChosen").toBool()) {
                    dialog->setProperty("testChosen", true);
                    const auto lines = dialog->findChildren<QLineEdit*>();
                    if (lines.size() != 1) { failed = true; dialog->reject(); continue; }
                    lines.first()->setText(manualPath);
                    chosen = true;
                    QMetaObject::invokeMethod(dialog, "onSaveClicked", Qt::DirectConnection);
                }
            } else if (auto *box = qobject_cast<QMessageBox*>(widget); box && box->isVisible()) {
                saved = box->windowTitle() == "Save Audio" && box->text().contains("saved successfully");
                failed |= !saved;
                box->accept();
            } else if (deadline.elapsed() > 5000) {
                if (auto *dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible()) {
                    failed = true; dialog->reject();
                }
            }
        }
    });
    responder.start();
    invoke(window, "onFileSaveAudio");
    responder.stop();
    const QString manualRaw = fixtures.directory.filePath("manual-audio.raw");
    if (!check(chosen && saved && !failed, "manual Save Audio completes through the shared pipeline")
        || !fixtures.ffmpeg({"-i", manualPath, "-f", "s16le", manualRaw})
        || !check(readFile(manualRaw) == readFile(queuedRaw),
                  "manual, script and queue use identical edited samples")) return false;

    VDQtAudioPlayer offline(false);
    if (!offline.openFile(source)) return false;
    VDQtAudioExportRequest request;
    request.codec.codecId = "pcm_s16le";
    request.outputPath = fixtures.directory.filePath("continuous-filter.wav");
    request.sampleRanges = {{52920, 13230}, {8820, 17640}};
    auto lowpass = VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::LowPass);
    lowpass.params["cutoffHz"] = 1200;
    request.filters = {lowpass};
    const QString filteredPath = request.outputPath;
    if (!VDQtExportAudio(offline, request, [&](int, int) {
            request.filters.clear(); request.sampleRanges = {{0, 1}};
            request.codec.codecId = "aac";
            return true;
        }, &error)) return false;
    const QString expectedFiltered = fixtures.directory.filePath("expected-filter.raw");
    const QString actualFiltered = fixtures.directory.filePath("actual-filter.raw");
    if (!fixtures.ffmpeg({"-i", job.options.outputPath, "-af", "lowpass=f=1200", "-f", "s16le", expectedFiltered})
        || !fixtures.ffmpeg({"-i", filteredPath, "-f", "s16le", actualFiltered})
        || !check(readFile(actualFiltered) == readFile(expectedFiltered),
                  "effects run continuously after cuts and requests stay immutable during callbacks")) return false;
    const QString sentinel = fixtures.directory.filePath("cancelled.wav");
    if (!fixtures.writeText(sentinel, "original audio destination")) return false;
    request.outputPath = sentinel;
    request.codec.codecId = "pcm_s16le";
    request.sampleRanges = {{0, 4410}};
    request.replaceExisting = true;
    if (!check(!VDQtExportAudio(offline, request, [](int, int) { return false; }, &error)
               && readFile(sentinel) == "original audio destination",
               "cancelled audio leaves an approved existing destination intact")) return false;
    request.replaceExisting = false;
    return check(!VDQtExportAudio(offline, request, {}, &error)
                 && error.contains("not approved") && readFile(sentinel) == "original audio destination",
                 "shared audio export rejects unapproved replacement before encoding");
}

bool outputFamilies(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4)) return false;
    VDQtFilterSystem::instance().clearFilters();
    VDQtCodecEngine::instance().setVideoParams(
        VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1"));
    const QString directory = fixtures.directory.path();
    const QString first = fixtures.directory.filePath("segments.00.avi");
    const QString second = fixtures.directory.filePath("segments.01.avi");
    if (!fixtures.writeText(first, "original0") || !fixtures.writeText(second, "original1")) return false;
    const QString segmented = "VirtualDub.video.SetMode(3); VirtualDub.audio.SetSource(0); "
        "VirtualDub.video.SetRangeFrames(0,4); VirtualDub.SaveSegmentedAVI(\"segments.avi\",0,2,2);";
    QString error;
    if (!check(!window.runAutomationText(segmented, directory, &error)
               && error.contains("not approved") && error.contains(first)
               && readFile(first) == "original0" && readFile(second) == "original1",
               "unattended segmented export rejects actual numbered collisions")) return false;

    window.setAutomationUnattended(false);
    int answer = QMessageBox::Cancel;
    bool promptSeen = false, promptCorrect = true, changeDuringApproval = false;
    QTimer responder;
    responder.setInterval(5);
    QObject::connect(&responder, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *message = qobject_cast<QMessageBox*>(widget);
            if (!message || !message->isVisible() || message->property("testAnswered").toBool()
                || message->windowTitle() != "Replace AVI Segments?") continue;
            message->setProperty("testAnswered", true);
            promptSeen = true;
            promptCorrect &= message->detailedText().contains(first)
                && message->detailedText().contains(second)
                && message->defaultButton() == message->button(QMessageBox::Cancel);
            if (changeDuringApproval) fixtures.writeText(second, "changed during approval");
            message->button(static_cast<QMessageBox::StandardButton>(answer))->click();
        }
    });
    responder.start();
    if (!check(!window.runAutomationText(segmented, directory, &error)
               && promptSeen && promptCorrect
               && readFile(first) == "original0" && readFile(second) == "original1",
               "cancelling exact-target approval leaves numbered originals intact")) return false;
    answer = QMessageBox::Yes;
    changeDuringApproval = true;
    if (!check(!window.runAutomationText(segmented, directory, &error)
               && error.contains("changed") && readFile(first) == "original0"
               && readFile(second) == "changed during approval",
               "a destination modified inside the approval dialog is not replaced")) return false;
    changeDuringApproval = false;
    if (!check(window.runAutomationText(segmented, directory, &error),
               "explicit approval installs the rendered segment family")) {
        std::cerr << error.toStdString() << '\n';
        return false;
    }
    responder.stop();
    VDQtVideoDecoder verify;
    for (const QString& segment : {first, second}) {
        if (!verify.openFile(segment) || !verify.scanVideoStream().errorMessage.isEmpty()
            || !check(verify.getFrameCount() == 2, "approved AVI segments contain the requested frames")) return false;
        verify.close();
    }

    window.setAutomationUnattended(true);
    const QString image = fixtures.directory.filePath("script_00.png");
    fixtures.writeText(image, "original image");
    if (!check(!window.runAutomationText(
            "VirtualDub.SaveImageSequence(\"script_\",\".png\",2,3);", directory, &error)
            && error.contains("not approved") && readFile(image) == "original image"
            && !QFileInfo::exists(fixtures.directory.filePath("script_01.png")),
            "unattended image scripts require numbered-file replacement approval")) return false;

    auto *queue = window.findChild<VDQtJobQueue*>();
    if (!queue) return false;
    VDQtJobState job;
    job.operation = VDQtJobOperation::ImageSequenceExport;
    job.sourcePaths = {fixtures.mp4};
    job.audioDisabled = true;
    job.options.outputPath = fixtures.directory.filePath("queued.png");
    job.options.endFrame = 3;
    job.imageMinimumDigits = 2;
    const QString queuedFirst = fixtures.directory.filePath("queued_00.png");
    const QString queuedLast = fixtures.directory.filePath("queued_03.png");
    fixtures.writeText(queuedFirst, "queued original");
    if (!queue->replaceJobs({job}, &error)) return false;
    invoke(window, "runPendingJobs");
    if (!check(queue->jobAt(0)->status == VDQtJobStatus::Failed
               && readFile(queuedFirst) == "queued original",
               "image jobs reject collisions without queue replacement approval")) return false;
    job.replaceExisting = true;
    if (!queue->replaceJobs({job}, &error)) return false;
    bool appeared = false;
    const auto connection = QObject::connect(queue, &VDQtJobQueue::jobChanged, &window, [&](int row) {
        const auto *running = queue->jobAt(row);
        if (!appeared && running && running->status == VDQtJobStatus::Running && running->progress >= 0.5) {
            appeared = true;
            fixtures.writeText(queuedLast, "foreign image");
        }
    });
    invoke(window, "runPendingJobs");
    QObject::disconnect(connection);
    if (!check(appeared && queue->jobAt(0)->status == VDQtJobStatus::Failed
               && readFile(queuedFirst) == "queued original" && readFile(queuedLast) == "foreign image",
               "queue approval does not permit overwriting files that appear during rendering")) return false;
    if (!queue->replaceJobs({job}, &error)) return false;
    invoke(window, "runPendingJobs");
    return check(queue->jobAt(0)->status == VDQtJobStatus::Complete
                 && !QImage(queuedFirst).isNull() && !QImage(queuedLast).isNull(),
                 "approved image jobs still install a complete sequence");
}

bool unknownTimeline(VDQtTestFixtures& fixtures) {
    const QString unknown = fixtures.directory.filePath("unknown-count.h264");
    if (!fixtures.ffmpeg({"-i", fixtures.mp4, "-c:v", "copy", "-an",
                          "-bsf:v", "h264_mp4toannexb", "-f", "h264", unknown})) return false;
    const QString underestimated = fixtures.directory.filePath("underestimated.avi");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=320x180:rate=24",
                          "-frames:v", "48", "-c:v", "ffv1", "-threads", "2", "-an", underestimated})) return false;
    QByteArray avi = readFile(underestimated);
    const qsizetype mainHeader = avi.indexOf("avih"), streamHeader = avi.indexOf("strh");
    if (!check(mainHeader >= 0 && streamHeader >= 0 && streamHeader + 44 < avi.size()
               && avi.mid(streamHeader + 8, 4) == "vids", "disposable AVI has expected frame-count header fields")) return false;
    // Deliberately stale RIFF metadata: dwTotalFrames and video dwLength report
    // four frames while all 48 encoded frames and their index remain intact.
    qToLittleEndian<quint32>(4, reinterpret_cast<uchar*>(avi.data() + mainHeader + 24));
    qToLittleEndian<quint32>(4, reinterpret_cast<uchar*>(avi.data() + streamHeader + 40));
    if (!fixtures.writeText(underestimated, avi)) return false;
    VDQtVideoDecoder probe;
    if (!check(probe.openFile(underestimated) && !probe.isFrameCountExact() && probe.getFrameCount() == 4,
               "the AVI fixture really supplies an underestimated count")) return false;
    probe.close();
    // Production owns one main window per process. Exercise both files through
    // its actual source-close/reopen lifecycle, not a second application window.
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    for (const QString& input : {unknown, underestimated}) {
        if (!invoke(window, "onFileClose") || !window.openVideoFile(input)) return false;
        const auto panes = window.findChildren<VDVideoDisplayWidget*>();
        auto *position = window.findChild<VDQtPositionControlWidget*>();
        if (!check(panes.size() == 2 && position, "provisional-length preview controls are available")) return false;
        if (!check(waitFor([&] { return !panes.first()->frameImage().isNull(); }),
                   "unknown/estimated input displays frame zero without a full scan")) return false;
        const QImage first = panes.first()->frameImage();
        QMetaObject::invokeMethod(&window, "onTransportAction", Qt::DirectConnection,
                                  Q_ARG(int, VDQT_PCN_PLAY));
        const bool advanced = waitFor([&] {
            return position->GetPosition() >= 8 && panes.first()->frameImage() != first;
        });
        QMetaObject::invokeMethod(&window, "onTransportAction", Qt::DirectConnection,
                                  Q_ARG(int, VDQT_PCN_STOP));
        if (!check(advanced, "unknown/underestimated playback advances real video beyond provisional length")) return false;
    }
    return true;
}

bool indexReuse(VDQtTestFixtures& fixtures) {
    VDQtVideoDecoder decoder;
    VDQtVideoExporter exporter;
    if (!decoder.openFile(fixtures.mp4)) return false;
    VDQtVideoExporter::RawExportOptions raw;
    raw.inputPath = fixtures.mp4;
    raw.outputPath = fixtures.directory.filePath("first-index.raw");
    raw.startFrame = 2; raw.endFrame = 5;
    raw.pixelFormat = "rgb24";
    raw.unattended = true;
    if (!exporter.exportRawVideo(raw, &decoder)) return false;
    const QByteArray first = readFile(raw.outputPath);
    decoder.clearCache();
    decoder.resetPerformanceCounters();
    raw.outputPath = fixtures.directory.filePath("reused-index.raw");
    if (!exporter.exportRawVideo(raw, &decoder)) return false;
    if (!check(readFile(raw.outputPath) == first && first.size() == 4 * 320 * 180 * 3,
               "repeated raw exports retain identical selected pictures")) return false;
    if (!check(decoder.getDecodedFrameCount() < 48,
               "repeated raw export does not decode the whole source again")) return false;

    VDQtVideoExporter::ProcessingSnapshot processing;
    processing.videoCodec.codecId = "ffv1";
    processing.videoCodec.rateMode = "lossless";
    VDQtVideoExporter::ExportOptions video;
    video.inputPath = fixtures.mp4;
    video.outputPath = fixtures.directory.filePath("reused-index.mkv");
    video.startFrame = 2; video.endFrame = 5;
    video.processing = processing;
    video.includeAudio = false;
    video.unattended = true;
    decoder.setDecompressionConfig("Autoselect", 1, 0);
    decoder.resetPerformanceCounters();
    if (!check(exporter.exportVideo(video, &decoder),
               "video export succeeds with the reused index after a color change")) {
        std::cerr << exporter.lastError().toStdString() << '\n';
        return false;
    }
    if (!check(decoder.getDecodedFrameCount() < 48,
               "rendered video export reuses verified index knowledge")) return false;
    VDQtVideoDecoder exported;
    if (!exported.openFile(video.outputPath)
        || !check(exported.scanVideoStream().totalFrames == 4,
                  "cached-index video output contains exactly the selected four frames")) return false;
    decoder.resetPerformanceCounters();
    const auto rescanned = decoder.scanVideoStream();
    return check(rescanned.totalFrames == 48 && decoder.getDecodedFrameCount() >= 48,
                 "explicit error analysis still performs a fresh source scan");
}

bool sparseEof(VDQtTestFixtures& fixtures) {
    VDQtVideoDecoder reference;
    if (!reference.openFile(fixtures.mp4)) return false;
    const auto scan = reference.scanVideoStream();
    if (scan.totalFrames != 48 || !scan.errorMessage.isEmpty()) return false;
    const QImage lastImage = reference.getFrameImage(47);
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4)) return false;
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    const auto panes = window.findChildren<VDVideoDisplayWidget*>();
    if (!position || panes.size() != 2
        || !waitFor([&] { return !panes.first()->frameImage().isNull(); })) return false;
    position->SetPosition(47);
    if (!check(waitFor([&] { return panes.first()->frameImage() == lastImage; }),
               "sparse GUI seek displays the actual last image")) return false;
    QMetaObject::invokeMethod(&window, "onTransportAction", Qt::DirectConnection,
                              Q_ARG(int, VDQT_PCN_PLAY));
    QTimer *playback = nullptr;
    for (auto *timer : window.findChildren<QTimer*>()) {
        if (timer->isActive() && timer->timerType() == Qt::PreciseTimer)
            playback = timer;
    }
    if (!check(playback && waitFor([&] { return !playback->isActive(); }),
               "sparse EOF stops playback even without an exact frame count")) return false;
    return check(position->GetRangeEnd() >= 47 && position->GetPosition() == 47
                 && panes.first()->frameImage() == lastImage,
                 "sparse EOF retains the timeline and last displayed playhead");
}

bool indexedNavigation(VDQtTestFixtures& fixtures) {
    const QString source = fixtures.directory.filePath("key-navigation.mp4");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=96x64:rate=10",
                          "-frames:v", "48", "-c:v", "libx264", "-g", "12",
                          "-keyint_min", "12", "-sc_threshold", "0", "-bf", "3",
                          "-threads", "1", "-an", source})) return false;
    VDQtVideoDecoder reference;
    if (!reference.openFile(source) || reference.ensureFrameIndex().totalFrames != 48) return false;
    const int key = reference.getNextKeyFrame(0);
    if (!check(key == 12, "navigation fixture has known GOP positions")) return false;
    const QImage expected = reference.getFrameImage(key);
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(source)) return false;
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    const auto panes = window.findChildren<VDVideoDisplayWidget*>();
    if (!position || panes.size() != 2
        || !waitFor([&] { return !panes.first()->frameImage().isNull(); })) return false;
    QMetaObject::invokeMethod(&window, "onTransportAction", Qt::DirectConnection,
                              Q_ARG(int, VDQT_PCN_KEYNEXT));
    if (!check(position->GetPosition() == key
               && waitFor([&] { return panes.first()->frameImage() == expected; }),
               "cold GUI next-keyframe navigation uses actual keyframe knowledge")) return false;
    QMetaObject::invokeMethod(&window, "onTransportAction", Qt::DirectConnection,
                              Q_ARG(int, VDQT_PCN_KEYPREV));
    return check(position->GetPosition() == 0,
                 "GUI previous-keyframe navigation uses the same verified index");
}

bool importedImageTiming(VDQtTestFixtures& fixtures) {
    QStringList images;
    for (int frame = 0; frame < 10; ++frame) {
        QImage image(16, 16, QImage::Format_RGB888);
        image.fill(QColor(20 + frame * 20, 40, 60));
        const QString path = fixtures.directory.filePath(QString("import-%1.png").arg(frame));
        if (!image.save(path)) return false;
        images.append(path);
    }
    for (double fps : {25.0, 30.0, 60.0, 100.0, 30000.0 / 1001.0}) {
        VDQtProjectState project;
        project.sourcePath = images.first();
        project.sourcePaths = images;
        project.imageSequenceFps = fps;
        project.sourceFrameCount = 10;
        project.sourceFrameCountExact = true;
        project.timelineExplicit = true;
        project.timelineSegments = {{0, 10}};
        project.audioDisabled = true;
        const QString projectPath = fixtures.directory.filePath("images.vdqproject");
        QString error;
        if (!VDQtProjectFile::saveProject(projectPath, project, &error)) return false;
        VDQtMainWindow window;
        window.setAutomationUnattended(true);
        window.show();
        if (!chooseProjectFile(window, "onFileLoadProject", projectPath)) return false;
        const auto panes = window.findChildren<VDVideoDisplayWidget*>();
        auto *position = window.findChild<VDQtPositionControlWidget*>();
        if (!position || panes.size() != 2) return false;
        position->SetPosition(9);
        if (!check(waitFor([&] { return !panes.first()->frameImage().isNull()
                   && panes.first()->frameImage().pixelColor(0, 0).red() == 200; }),
                   "all imported images retain their presentation ordinals")) return false;
        const QString time = window.statusBar()->currentMessage()
            .section("Time: ", 1, 1).section("  |", 0, 0);
        const auto fields = time.split(':');
        const double seconds = fields.size() == 3
            ? fields.at(0).toDouble() * 3600 + fields.at(1).toDouble() * 60
                + fields.at(2).toDouble() : -1;
        if (!check(std::abs(seconds - 9.0 / fps) <= 0.002,
                   "imported image timestamps use the requested rational input frame rate")) {
            std::cerr << "rate=" << fps << ", last-frame time=" << seconds << '\n';
            return false;
        }
        const QString output = fixtures.directory.filePath(QString("images-%1.raw").arg(fps));
        if (!window.runAutomationText(
                QString("VirtualDub.SaveRawVideo(\"%1\",8,4,0,0);").arg(output),
                fixtures.directory.path(), &error)) return false;
        const QByteArray bytes = readFile(output);
        if (!check(bytes.size() == 10 * 16 * 16 * 4,
                   "image import/export retains exactly ten images at every tested rate")) return false;
        for (int frame = 0; frame < 10; ++frame) {
            if (!check(static_cast<unsigned char>(bytes.at(frame * 16 * 16 * 4 + 2))
                           == 20 + frame * 20,
                       "each exported image retains its distinct source pixels")) return false;
        }
    }
    return true;
}

bool editedFilterContext(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4)) return false;
    auto *input = window.findChild<VDVideoDisplayWidget*>("inputPreview");
    auto *output = window.findChild<VDVideoDisplayWidget*>("outputPreview");
    if (!input || !output || !waitFor([&] { return !input->frameImage().isNull(); })) return false;
    auto& session = VDQtFilterSystem::instance();
    session.clearFilters();
    session.addFilter(VDFilterType::InvertColor);
    auto params = session.getActiveChain().first().params;
    params["_sylia.range.start"] = 0;
    params["_sylia.range.end"] = 10;
    session.updateFilterParams(0, params);
    QString error;
    if (!window.runAutomationText("VirtualDub.subset.Clear(); VirtualDub.subset.AddRange(20,10);",
                                  fixtures.directory.path(), &error)) return false;
    VDQtVideoDecoder reference;
    if (!reference.openFile(fixtures.mp4)) return false;
    const QImage source = reference.getFrameImage(20);
    VDQtFilterSystem expectedFilters;
    expectedFilters.addFilter(VDFilterType::InvertColor);
    const QImage expected = expectedFilters.processFrame(source);
    if (!check(waitFor([&] { return input->frameImage() == source && output->frameImage() == expected; }),
               "edited preview activates a timed effect at timeline zero, not source frame twenty")) return false;
    params["_sylia.range.start"] = 2;
    params["_sylia.range.end"] = 3;
    session.updateFilterParams(0, params);
    VDQtVideoExporter exporter;
    VDQtVideoExporter::RawExportOptions raw;
    raw.inputPath = fixtures.mp4;
    raw.outputPath = fixtures.directory.filePath("context.raw");
    raw.pixelFormat = "rgb24";
    raw.swapChromaPlanes = false;
    raw.scanlineAlignment = 1;
    raw.startFrame = 2; raw.endFrame = 3;
    raw.timelineSegments = {{20, 10, false}};
    raw.unattended = true;
    if (!exporter.exportRawVideo(raw, &reference)) return false;
    QByteArray wanted;
    for (int frame : {22, 23}) {
        QImage image = reference.getFrameImage(frame).convertToFormat(QImage::Format_RGB888);
        if (frame == 22) image = expectedFilters.processFrame(image);
        for (int y = 0; y < image.height(); ++y)
            wanted.append(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 3);
    }
    if (!check(readFile(raw.outputPath) == wanted,
               "raw selection retains absolute edited-timeline positions for timed filters")) return false;
    VDQtVideoExporter::ExportOptions video;
    video.inputPath = fixtures.mp4;
    video.outputPath = fixtures.directory.filePath("context.mkv");
    video.includeAudio = false;
    video.videoCodecOverride = "ffv1";
    video.videoPixelFormatOverride = "rgb24";
    video.startFrame = 2; video.endFrame = 3;
    video.timelineSegments = raw.timelineSegments;
    video.unattended = true;
    QByteArray rendered;
    if (!exporter.exportVideo(video, &reference, nullptr, nullptr,
        [&](int, const QImage&, const QImage& outputImage) {
            const QImage image = outputImage.convertToFormat(QImage::Format_RGB888);
            for (int y = 0; y < image.height(); ++y)
                rendered.append(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 3);
        })) return false;
    if (!check(rendered == wanted, "rendered and raw selected exports agree with edited filter positions")) return false;

    const QString vfr = fixtures.directory.filePath("context-vfr.mkv");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=32x24:rate=24:duration=1",
            "-vf", "select='if(lt(n,12),1,not(mod(n,3)))'", "-fps_mode", "vfr",
            "-c:v", "ffv1", "-an", vfr}) || !reference.openFile(vfr)) return false;
    if (reference.ensureFrameIndex().totalFrames != 16) return false;
    const QList<VDQtTimelineSegment> edits{{12, 4, false}, {0, 2, false}, {2, 2, true}};
    const auto second = VDQtFilterContextForFrame(reference, edits, 1);
    if (!check(second.frameNumber == 1 && second.sourceFrameNumber == 13
               && std::abs(second.timestampSeconds - 0.125) < 1e-8,
               "edited VFR time uses actual frame durations rather than average FPS")) return false;
    const auto firstMask = VDQtFilterContextForFrame(reference, edits, 6);
    const auto secondMask = VDQtFilterContextForFrame(reference, edits, 7);
    if (!check(firstMask.sourceFrameNumber == 1 && secondMask.sourceFrameNumber == 1
               && secondMask.frameNumber == 7
               && std::abs(secondMask.timestampSeconds - firstMask.timestampSeconds
                   - reference.getFrameDurationSeconds(2)) < 1e-8,
               "held preview identity does not freeze timeline time or temporal-filter history")) return false;
    // Hold a short-duration picture while the actual masked frames have longer
    // durations. Both plain Play and filtered Play Preview must advance using
    // the latter; a picture timestamp is not the playback interval.
    VDQtFrameDecodeWorker worker;
    if (!worker.openSource(vfr, QString(), 0, 0, 0)
        || !worker.adoptFrameIndexSnapshot(reference.frameIndexSnapshot())) return false;
    const QList<VDQtTimelineSegment> heldEdits{{0, 1, false}, {12, 2, true}};
    bool delivered = false;
    double duration = 0;
    QObject::connect(&worker, &VDQtFrameDecodeWorker::frameReady, &worker,
        [&](int frame, quint64, const QImage&, const QList<QImage>&, bool,
            double, double interval, int, int, quint64, quint64, const QString&) {
            delivered = frame == 0;
            duration = interval;
        });
    for (bool filtered : {false, true}) {
        delivered = false;
        worker.requestFrame(0, filtered ? 2 : 1, true, filtered, 1, heldEdits);
        if (!check(waitFor([&] { return delivered; })
                   && std::abs(duration - reference.getFrameDurationSeconds(12)) < 1e-8,
                   "masked playback reports advancing VFR interval in either preview mode")) return false;
    }
    VDQtVideoDecoder recipient;
    if (!recipient.openFile(vfr) || !recipient.adoptFrameIndexSnapshot(reference.frameIndexSnapshot())) return false;
    recipient.resetPerformanceCounters();
    return check(recipient.getFrameElapsedSeconds(16) == reference.getFrameElapsedSeconds(16)
                 && recipient.getDecodedFrameCount() == 0,
                 "shared immutable timing prefix is reused without decoding");
}

bool failedFilterPreview(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4)) return false;
    // Splitter reparenting changes QObject child order; identify roles explicitly.
    auto *input = window.findChild<VDVideoDisplayWidget*>("inputPreview");
    auto *output = window.findChild<VDVideoDisplayWidget*>("outputPreview");
    if (!input || !output
        || !check(waitFor([&] { return !output->frameImage().isNull(); }),
                   "filter failure test begins with a successful output preview")) return false;
    QString error;
    if (!window.runAutomationText(
            "VirtualDub.video.filters.Clear();\nVirtualDub.video.filters.Add(\"logo\");\n"
            "VirtualDub.video.filters.instance[0].Config(\"missing-logo.png\",0,0,\"\",0,0,0,65536);",
            fixtures.directory.path(), &error)) return false;
    if (!check(waitFor([&] { return output->frameImage().isNull()
               && window.statusBar()->currentMessage().contains("logo", Qt::CaseInsensitive); })
               && !input->frameImage().isNull(),
               "failed filtering clears stale output and shows its actionable error beside valid input")) {
        std::cerr << "Input null=" << input->frameImage().isNull()
                  << ", output null=" << output->frameImage().isNull()
                  << ", status=" << window.statusBar()->currentMessage().toStdString() << '\n';
        for (const auto& filter : VDQtFilterSystem::instance().getActiveChain())
            std::cerr << filter.name.toStdString() << ": "
                      << filter.stringParams.value("path").toStdString() << '\n';
        return false;
    }
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(fixtures.mp4)) return false;
    VDQtVideoExporter exporter;
    VDQtVideoExporter::RawExportOptions raw;
    raw.inputPath = fixtures.mp4;
    raw.outputPath = fixtures.directory.filePath("missing-effect.raw");
    raw.endFrame = 3;
    raw.unattended = true;
    if (!check(!exporter.exportRawVideo(raw, &decoder)
               && exporter.lastError().contains("logo", Qt::CaseInsensitive)
               && !QFile::exists(raw.outputPath),
               "raw export propagates a missing effect without publishing partial output")) return false;
    VDQtVideoExporter::ExportOptions video;
    video.inputPath = fixtures.mp4;
    video.outputPath = fixtures.directory.filePath("missing-effect.mkv");
    video.includeAudio = false;
    video.endFrame = 3;
    video.videoCodecOverride = "ffv1";
    video.unattended = true;
    video.videoMode = VideoMode_FullProcessing;
    return check(!exporter.exportVideo(video, &decoder)
                 && exporter.lastError().contains("logo", Qt::CaseInsensitive)
                 && !QFile::exists(video.outputPath),
                 "rendered video export propagates the required-effect error");
}

bool emptyTimeline(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.avs)) return false;
    QString error;
    const QString output = fixtures.directory.filePath("must-not-be-exported.raw");
    const bool exported = window.runAutomationText(
        "VirtualDub.subset.Clear();\nVirtualDub.SaveRawVideo(\"must-not-be-exported.raw\",8,4,0,0);",
        fixtures.directory.path(), &error);
    if (!check(!exported && error.contains("timeline", Qt::CaseInsensitive)
               && !QFile::exists(output),
               "an explicitly empty GUI/script timeline cannot export the full original source")) return false;
    const auto panes = window.findChildren<VDVideoDisplayWidget*>();
    QElapsedTimer pending; pending.start();
    if (!waitFor([&] { return pending.elapsed() >= 100; })
        || !check(panes.size() == 2 && panes.first()->frameImage().isNull()
                  && panes.last()->frameImage().isNull(),
                  "a pending old preview cannot repaint a delete-all timeline")) return false;

    VDQtVideoExporter exporter;
    const QString sentinel = fixtures.directory.filePath("preserved-empty-output.mkv");
    if (!fixtures.writeText(sentinel, "original output")) return false;
    for (int mode : {VideoMode_DirectStreamCopy, VideoMode_FastRecompress,
                     VideoMode_NormalRecompress, VideoMode_FullProcessing}) {
        VDQtVideoExporter::ExportOptions options;
        options.inputPath = fixtures.avs;
        options.outputPath = sentinel;
        options.includeAudio = false;
        options.videoMode = mode;
        options.timelineExplicit = true;
        options.unattended = true;
        if (!check(!exporter.exportVideo(options) && exporter.lastError().contains("timeline")
                   && readFile(sentinel) == "original output",
                   "every video export mode rejects explicit-empty edits without touching output")) return false;
    }
    VDQtVideoExporter::RawExportOptions raw;
    raw.inputPath = fixtures.avs; raw.outputPath = sentinel;
    raw.timelineExplicit = true; raw.unattended = true;
    if (!check(!exporter.exportRawVideo(raw) && exporter.lastError().contains("timeline")
               && readFile(sentinel) == "original output", "raw export preserves existing output for empty edits")) return false;
    VDQtFrameServer server;
    VDQtFrameServer::Config config;
    config.sourcePath = fixtures.avs;
    config.pipePath = fixtures.directory.filePath("must-not-be-created.fifo");
    config.timelineExplicit = true;
    if (!check(!server.start(config, &error) && error.contains("timeline")
               && !server.isRunning() && !QFile::exists(config.pipePath),
               "empty edited frame serving fails before creating a FIFO or worker")) return false;

    const QString saved = fixtures.directory.filePath("empty.vdqproject");
    if (!chooseProjectFile(window, "onFileSaveProjectAs", saved)) return false;
    VDQtProjectState project;
    const bool projectLoaded = VDQtProjectFile::loadProject(saved, &project, &error);
    if (!projectLoaded || !project.timelineExplicit || !project.sourceFrameCountExact)
        std::cerr << "Empty project: loaded=" << projectLoaded << ", exists=" << QFile::exists(saved)
                  << ", explicit=" << project.timelineExplicit << ", exact=" << project.sourceFrameCountExact
                  << ", error=" << error.toStdString() << '\n';
    if (!check(projectLoaded
               && project.timelineExplicit && project.timelineSegments.isEmpty()
               && project.sourceFrameCountExact, "empty GUI project saves explicit intent and count accuracy")) return false;
    if (!window.openVideoFile(fixtures.mp4)
        || !chooseProjectFile(window, "onFileLoadProject", saved)) return false;
    if (!check(!window.runAutomationText("VirtualDub.SaveRawVideo(\"reloaded.raw\",8,4,0,0);",
                                        fixtures.directory.path(), &error)
               && error.contains("timeline") && !QFile::exists(fixtures.directory.filePath("reloaded.raw")),
               "loading an empty project keeps its deleted-all timeline")) return false;

    auto *queue = window.findChild<VDQtJobQueue*>();
    if (!queue) return false;
    for (VDQtJobOperation operation : {VDQtJobOperation::VideoExport, VDQtJobOperation::RawVideoExport,
                                      VDQtJobOperation::ImageSequenceExport, VDQtJobOperation::AudioExport}) {
        VDQtJobState job;
        job.operation = operation;
        job.sourcePaths = {fixtures.avs};
        job.audioDisabled = true;
        job.options.includeAudio = false;
        job.options.timelineExplicit = true;
        job.options.outputPath = fixtures.directory.filePath(QString("empty-job-%1.mkv").arg(int(operation)));
        const QString queuePath = fixtures.directory.filePath("empty-jobs.vdqjobs");
        QList<VDQtJobState> restored;
        if (!VDQtProjectFile::saveJobQueue(queuePath, {job}, &error)
            || !VDQtProjectFile::loadJobQueue(queuePath, &restored, &error)
            || !check(restored.size() == 1 && restored.first().options.timelineExplicit,
                      "queued empty intent survives serialization")
            || !queue->replaceJobs(restored, &error) || !invoke(window, "runPendingJobs")) return false;
        if (!check(queue->jobAt(0)->status == VDQtJobStatus::Failed
                   && queue->jobAt(0)->error.contains("timeline") && !QFile::exists(job.options.outputPath),
                   "video/raw/image/audio jobs refuse empty edits before preparing output")) return false;
    }

    // Old empty arrays mean identity. Version 7 must carry a real boolean, and
    // an untouched estimated source must not become an explicit bounded edit.
    QJsonObject document = QJsonDocument::fromJson(readFile(saved)).object();
    document["version"] = 6;
    document.remove("timelineExplicit"); document.remove("sourceFrameCountExact");
    const QString legacy = fixtures.directory.filePath("legacy.vdqproject");
    fixtures.writeText(legacy, QJsonDocument(document).toJson());
    if (!check(VDQtProjectFile::loadProject(legacy, &project, &error)
               && !project.hasExplicitTimeline(), "legacy empty project arrays retain source-identity meaning")) return false;
    document["version"] = 7;
    document["sourceFrameCountExact"] = false;
    document["timelineExplicit"] = "false";
    const QString malformed = fixtures.directory.filePath("bad-intent.vdqproject");
    fixtures.writeText(malformed, QJsonDocument(document).toJson());
    if (!check(!VDQtProjectFile::loadProject(malformed, &project, &error),
               "nonboolean timeline intent cannot silently become full-source identity")) return false;
    document["timelineExplicit"] = false;
    document["sourceFrameCount"] = 1;
    const QString provisional = fixtures.directory.filePath("provisional.vdqproject");
    fixtures.writeText(provisional, QJsonDocument(document).toJson());
    if (!chooseProjectFile(window, "onFileLoadProject", provisional)) return false;
    return check(waitFor([&] { return !panes.first()->frameImage().isNull(); }),
                 "a new-format untouched estimate reopens as source identity, not a fixed-length edit");
}

bool jsonIntegerInputs(VDQtTestFixtures& fixtures) {
    const QString projectPath = fixtures.directory.filePath("numeric-project.vdqproject");
    const QString jobPath = fixtures.directory.filePath("numeric-jobs.vdqjobs");
    VDQtProjectState project;
    project.sourcePath = fixtures.mp4;
    project.sourcePaths = {fixtures.mp4};
    project.sourceFrameCount = 48;
    project.sourceFrameCountExact = true;
    project.audioDisabled = true;
    QString error;
    if (!VDQtProjectFile::saveProject(projectPath, project, &error)) return false;
    const auto original = QJsonDocument::fromJson(readFile(projectPath)).object();
    for (qint64 offset : {qint64{9007199254740993}, std::numeric_limits<qint64>::max()}) {
        auto saved = project;
        saved.rawByteOffset = offset;
        VDQtProjectState restored;
        if (!check(VDQtProjectFile::saveProject(projectPath, saved, &error)
                   && VDQtProjectFile::loadProject(projectPath, &restored, &error)
                   && restored.rawByteOffset == offset,
                   "64-bit project offsets round-trip exactly, including above double precision")) return false;
    }
    const QMap<QString, QStringList> processingIntegers = {
        {"", {"videoMode", "audioMode"}},
        {"frameRate", {"sourceMode", "conversionMode", "decimateN"}},
        {"decompression", {"colorSpace", "componentRange"}},
        {"decoderError", {"mode"}},
        {"rawVideo", {"scanlineAlignment"}},
        {"videoCodec", {"crf", "targetBitrateKbps", "maxBitrateKbps", "keyframeInterval",
                        "bFrames", "proresProfile", "ffv1Version", "ffv1Coder", "ffv1Slices",
                        "huffyuvPredictor", "cineformQuality"}},
        {"audioCodec", {"vbrQuality", "bitrateKbps", "sampleRate", "channels", "bitDepth"}}
    };
    for (auto section = processingIntegers.cbegin(); section != processingIntegers.cend(); ++section) {
        for (const QString& key : section.value()) {
            for (const QJsonValue& invalid : {QJsonValue(1e100), QJsonValue(3.5), QJsonValue("4"),
                                             QJsonValue(QJsonValue::Null), QJsonValue(true)}) {
                auto root = original;
                auto processing = root.value("processing").toObject();
                if (section.key().isEmpty()) processing[key] = invalid;
                else {
                    auto nested = processing.value(section.key()).toObject();
                    nested[key] = invalid;
                    processing[section.key()] = nested;
                }
                root["processing"] = processing;
                VDQtProjectState retained = project;
                retained.position = 7;
                if (!fixtures.writeText(projectPath, QJsonDocument(root).toJson())
                    || !check(!VDQtProjectFile::loadProject(projectPath, &retained, &error)
                               && !error.isEmpty() && retained.position == 7,
                               "present malformed processing integers cannot become defaults")) {
                    std::cerr << section.key().toStdString() << '.' << key.toStdString() << '\n';
                    return false;
                }
            }
        }
    }
    for (const QString& key : {QString("customSourceFps"), QString("convertFps")}) {
        auto root = original;
        auto processing = root.value("processing").toObject();
        auto rate = processing.value("frameRate").toObject();
        rate[key] = "24";
        processing["frameRate"] = rate;
        root["processing"] = processing;
        if (!fixtures.writeText(projectPath, QJsonDocument(root).toJson())
            || !check(!VDQtProjectFile::loadProject(projectPath, &project, &error),
                       "present nonnumeric frame rates cannot become automatic defaults")) return false;
    }
    for (const QString& key : {QString("imageSequenceFps"), QString("rawFrameRate")}) {
        for (const QJsonValue& invalid : {QJsonValue("24"), QJsonValue(QJsonValue::Null), QJsonValue(true)}) {
            auto root = original;
            root[key] = invalid;
            if (!fixtures.writeText(projectPath, QJsonDocument(root).toJson())
                || !check(!VDQtProjectFile::loadProject(projectPath, &project, &error),
                           "present nonnumeric source rates cannot become defaults")) return false;
        }
    }
    auto legacyDefaults = original;
    legacyDefaults["processing"] = QJsonObject();
    if (!fixtures.writeText(projectPath, QJsonDocument(legacyDefaults).toJson())
        || !check(VDQtProjectFile::loadProject(projectPath, &project, &error)
                  && project.processing.videoCodec.crf == VDVideoCodecParams().crf
                  && project.processing.frameRate.decimateN == 2,
                  "omitted legacy processing fields still use established defaults")) return false;
    for (const QString& key : {QString("position"), QString("selectionStart"), QString("selectionEnd"),
                              QString("sourceFrameCount"), QString("zoomStart"), QString("zoomEnd"),
                              QString("rawByteOffset")}) {
        for (const QJsonValue& invalid : {QJsonValue(1e100), QJsonValue(3.5), QJsonValue("4")}) {
            auto root = original;
            root[key] = invalid;
            if (!fixtures.writeText(projectPath, QJsonDocument(root).toJson())) return false;
            VDQtProjectState retained = project;
            retained.position = 7;
            if (!check(!VDQtProjectFile::loadProject(projectPath, &retained, &error)
                       && !error.isEmpty() && retained.position == 7,
                       "saved project integer fields reject overflow, fractions and wrong types atomically")) {
                std::cerr << key.toStdString() << '\n'; return false;
            }
        }
    }
    auto root = original;
    root["rawByteOffset"] = std::ldexp(1.0, 63);
    if (!fixtures.writeText(projectPath, QJsonDocument(root).toJson())
        || !check(!VDQtProjectFile::loadProject(projectPath, &project, &error),
                  "rounded 2^63 cannot pass the signed raw-offset upper boundary")) return false;
    root["rawByteOffset"] = std::nextafter(std::ldexp(1.0, 63), 0.0);
    if (!fixtures.writeText(projectPath, QJsonDocument(root).toJson())
        || !check(VDQtProjectFile::loadProject(projectPath, &project, &error)
                  && project.rawByteOffset > 0,
                  "the nearest representable integer below 2^63 remains valid")) return false;
    for (const QString& key : {QString("sourceStartFrame"), QString("frameCount")}) {
        root = original;
        QJsonObject segment{{"sourceStartFrame", 0}, {"frameCount", 5}, {"masked", false}};
        segment[key] = 1e100;
        root["timelineExplicit"] = true;
        root["timelineSegments"] = QJsonArray{segment};
        if (!fixtures.writeText(projectPath, QJsonDocument(root).toJson())
            || !check(!VDQtProjectFile::loadProject(projectPath, &project, &error),
                      "segment integers are checked before conversion and addition")) return false;
    }
    VDQtJobState job;
    job.id = "retained-job";
    job.sourcePaths = {fixtures.mp4};
    job.options.outputPath = fixtures.directory.filePath("numeric-output.mkv");
    if (!VDQtProjectFile::saveJobQueue(jobPath, {job}, &error)) return false;
    const auto originalQueue = QJsonDocument::fromJson(readFile(jobPath)).object();
    for (qint64 offset : {qint64{9007199254740993}, std::numeric_limits<qint64>::max()}) {
        auto saved = job;
        saved.rawByteOffset = offset;
        QList<VDQtJobState> restored;
        if (!check(VDQtProjectFile::saveJobQueue(jobPath, {saved}, &error)
                   && VDQtProjectFile::loadJobQueue(jobPath, &restored, &error)
                   && restored.size() == 1 && restored.first().rawByteOffset == offset,
                   "64-bit queue offsets round-trip exactly, including above double precision")) return false;
    }
    for (const QString& key : {QString("progress"), QString("imageSequenceFps"),
                              QString("rawFrameRate"), QString("customFps")}) {
        auto queueRoot = originalQueue;
        auto saved = queueRoot.value("jobs").toArray().first().toObject();
        if (key == "customFps") {
            auto options = saved.value("options").toObject();
            options[key] = "24";
            saved["options"] = options;
        } else saved[key] = "24";
        queueRoot["jobs"] = QJsonArray{saved};
        QList<VDQtJobState> retained{job};
        if (!fixtures.writeText(jobPath, QJsonDocument(queueRoot).toJson())
            || !check(!VDQtProjectFile::loadJobQueue(jobPath, &retained, &error)
                      && retained.size() == 1 && retained.first().id == job.id,
                      "nonnumeric queued rates/progress cannot become defaults")) return false;
    }
    for (const QString& key : {QString("operation"), QString("status")}) {
        for (const QJsonValue& invalid : {QJsonValue(1e100), QJsonValue(3.5), QJsonValue("0")}) {
            auto queueRoot = originalQueue;
            auto saved = queueRoot.value("jobs").toArray().first().toObject();
            saved[key] = invalid;
            queueRoot["jobs"] = QJsonArray{saved};
            QList<VDQtJobState> retained{job};
            if (!fixtures.writeText(jobPath, QJsonDocument(queueRoot).toJson())
                || !check(!VDQtProjectFile::loadJobQueue(jobPath, &retained, &error)
                          && retained.size() == 1 && retained.first().id == job.id,
                          "malformed queued enums cannot become another operation/status")) return false;
        }
    }
    for (const QString& key : {QString("rawByteOffset"), QString("startFrame"), QString("endFrame")}) {
        auto queueRoot = originalQueue;
        auto saved = queueRoot.value("jobs").toArray().first().toObject();
        if (key == "rawByteOffset") saved[key] = std::ldexp(1.0, 63);
        else {
            auto options = saved.value("options").toObject();
            options[key] = 1e100;
            saved["options"] = options;
        }
        queueRoot["jobs"] = QJsonArray{saved};
        QList<VDQtJobState> retained{job};
        if (!fixtures.writeText(jobPath, QJsonDocument(queueRoot).toJson())
            || !check(!VDQtProjectFile::loadJobQueue(jobPath, &retained, &error)
                      && retained.size() == 1 && retained.first().id == job.id,
                      "queued integer inputs reject unsafe conversion without replacing loaded jobs")) return false;
    }
    return true;
}

bool scriptIntegerInputs(VDQtTestFixtures& fixtures) {
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(fixtures.mp4)) return false;
    QString error;
    for (const auto& invalid : {
        QString("VirtualDub.video.SetMode(4294967299);"),
        QString("VirtualDub.audio.SetMode(4294967297);"),
        QString("VirtualDub.video.SetMode(3.5);"),
        QString("VirtualDub.audio.SetConversion(4295015296,16,2);"),
        QString("VirtualDub.video.SetFrameRate2(1e100,1,1);"),
        QString("VirtualDub.video.SetCompression(0x34363248,0,0,9223372036854775807);"),
        QString("VirtualDub.audio.SetCompressionWithHint(1,48000,2,16,9223372036854775807,4);"),
        QString("VirtualDub.video.filters.Clear();VirtualDub.video.filters.Add(\"resize\");"
                "VirtualDub.video.filters.instance[0].Config(1e100,24,\"point\");"),
        QString("VirtualDub.video.filters.Clear();VirtualDub.video.filters.Add(\"resize\");"
                "VirtualDub.video.filters.instance[0].Config(\"64\",48,\"point\");"),
        QString("VirtualDub.video.filters.Clear();VirtualDub.video.filters.Add(\"canvas\");"
                "VirtualDub.video.filters.instance[0].Config(\"80\",60,0,0,0,0,0);"),
        QString("VirtualDub.video.filters.Clear();VirtualDub.video.filters.Add(\"fill\");"
                "VirtualDub.video.filters.instance[0].Config(2147483647,0,-2147483648,10,0);"),
        QString("VirtualDub.video.filters.Clear();VirtualDub.video.filters.Add(\"fill\");"
                "VirtualDub.video.filters.instance[0].Config(0,0,10,10,1.5);"),
        QString("VirtualDub.video.filters.Clear();VirtualDub.video.filters.Add(\"canvas\");"
                "VirtualDub.video.filters.instance[0].Config(80,60,0,0,0,0,\"4\");"),
        QString("VirtualDub.video.filters.Clear();VirtualDub.video.filters.Add(\"resize\");"
                "VirtualDub.video.filters.instance[0].Config(64,48,\"point\",80,60,-3000000000);"),
        QString("VirtualDub.video.filters.Clear();VirtualDub.video.filters.Add(\"resize\");"
                "VirtualDub.video.filters.instance[0].Config(64,48,0,4,3,0,80,60,4,3,1,0,0,1.5);"),
        QString("VirtualDub.video.filters.Clear();VirtualDub.video.filters.Add(\"resize\");"
                "VirtualDub.video.filters.instance[9999999999999999999999].SetEnabled(0);"),
        QString("VirtualDub.video.SetRangeFrames(0,1e100);"),
        QString("VirtualDub.SaveImageSequence(\"bad\",\".png\",4294967298,3);"),
        QString("VirtualDub.audio.filters.Clear();VirtualDub.audio.filters.Add(\"gain\");"
                "VirtualDub.audio.filters.instance[0].SetDouble(0,1e100);"),
        QString("VirtualDub.audio.filters.Clear();VirtualDub.audio.filters.Add(\"stretch\");"
                "VirtualDub.audio.filters.instance[0].SetDouble(0,1e-300);")}) {
        if (!check(!window.runAutomationText(invalid, fixtures.directory.path(), &error) && !error.isEmpty(),
                   "unsafe script numbers cannot wrap into a valid command")) {
            std::cerr << invalid.toStdString() << '\n'; return false;
        }
    }
    if (!check(window.runAutomationText(
        "VirtualDub.video.filters.Clear();VirtualDub.video.filters.Add(\"resize\");"
        "VirtualDub.video.filters.instance[0].Config(64,48,\"point\",80,60,0xFFFFFFFF);"
        "VirtualDub.video.filters.Add(\"fill\");"
        "VirtualDub.video.filters.instance[1].Config(0,0,10,10,-1);"
        "VirtualDub.audio.SetConversion(0,0,0);"
        "VirtualDub.audio.filters.Clear();VirtualDub.audio.filters.Add(\"gain\");"
        "VirtualDub.audio.filters.instance[0].SetLong(2,1234567890123);"
        "VirtualDub.video.SetFrameRate2(30000,1001,1);",
        fixtures.directory.path(), &error),
        "legitimate uint32 colors, auto conversion and unused 64-bit audio settings still execute")) return false;
    const QString verySlowClip = fixtures.directory.filePath("very-low-rate.avs");
    VDQtFilterSystem::instance().clearFilters();
    if (!fixtures.writeText(verySlowClip,
        "BlankClip(length=3,width=64,height=48,pixel_type=\"RGB32\",fps=1,fps_denominator=2147483647,audio_rate=0)\n")) return false;
    // Fail malformed fixtures before entering the editor's source-error dialog.
    {
        VDQtVideoDecoder validation;
        if (!check(validation.openFile(verySlowClip), "low-rate fixture validates before GUI open")) {
            std::cerr << validation.getLastError().toStdString() << '\n';
            return false;
        }
    }
    if (!window.openVideoFile(verySlowClip)) return false;
    auto *position = window.findChild<VDQtPositionControlWidget*>();
    if (!position) return false;
    position->SetPosition(2);
    return check(waitFor([&] { return window.statusBar()->currentMessage().contains("1193046:28:14.000"); }),
                 "real low-rate AVS preview uses safe hour/minute/second formatting");
}

bool sourceProtection(VDQtTestFixtures& fixtures) {
    const QString directory = fixtures.directory.path();
    const QString list = fixtures.directory.filePath("list.txt");
    const QString middle = fixtures.directory.filePath("middle.data");
    const QString outer = fixtures.directory.filePath("outer.ffconcat");
    if (!fixtures.writeText(list, "ffconcat version 1.0\nfile source.mp4\n")
        || !fixtures.writeText(middle, "ffconcat version 1.0\nfile list.txt\n")
        || !fixtures.writeText(outer, "ffconcat version 1.0\nfile middle.data\n")) return false;
    const QByteArray original = readFile(fixtures.mp4);
    VDQtVideoExporter exporter;
    for (const QString& input : {list, outer}) {
        VDQtVideoDecoder decoder;
        if (!check(decoder.openFile(input) && decoder.getInputFormatName() == "concat",
                   "valid misnamed and three-layer inputs open through the concat demuxer")) return false;
        VDQtVideoExporter::RawExportOptions raw;
        raw.inputPath = input;
        raw.outputPath = fixtures.mp4;
        raw.endFrame = 3;
        raw.pixelFormat = QStringLiteral("rgb24");
        raw.unattended = true;
        if (!check(!exporter.exportRawVideo(raw, &decoder) && exporter.lastError().contains("aliases")
                   && readFile(fixtures.mp4) == original,
                   "real raw exporter refuses to replace misnamed/nested-list input media")) return false;
        VDQtVideoExporter::ExportOptions video;
        video.inputPath = input;
        video.outputPath = fixtures.mp4;
        video.includeAudio = false;
        video.endFrame = 3;
        video.unattended = true;
        if (!check(!exporter.exportVideo(video, &decoder) && exporter.lastError().contains("aliases")
                   && readFile(fixtures.mp4) == original,
                   "real video exporter refuses dependency aliases before encoding")) return false;
        raw.outputPath = fixtures.directory.filePath(input == list ? "misnamed.raw" : "nested.raw");
        if (!check(exporter.exportRawVideo(raw, &decoder)
                   && readFile(raw.outputPath).size() == 4 * 320 * 180 * 3,
                   "unrelated output from misnamed/nested lists remains usable")) return false;
    }

    VDQtVideoDecoder decoder;
    if (!decoder.openFile(list)) return false;
    VDQtVideoExporter::RawExportOptions raw;
    raw.inputPath = list;
    raw.outputPath = fixtures.directory.filePath("newly-referenced.nut");
    raw.endFrame = 3;
    raw.unattended = true;
    fixtures.writeText(raw.outputPath, "foreign source sentinel");
    bool changed = false;
    const bool rendered = exporter.exportRawVideo(raw, &decoder, nullptr, nullptr,
        [&](int completed, int) {
            if (!changed && completed > 0) {
                changed = true;
                fixtures.writeText(list, "ffconcat version 1.0\nfile newly-referenced.nut\n");
            }
            return true;
        });
    if (!check(changed && !rendered && readFile(raw.outputPath) == "foreign source sentinel"
               && readFile(fixtures.mp4) == original,
               "real export's precommit refresh protects newly referenced files")) return false;
    fixtures.writeText(list, "ffconcat version 1.0\nfile source.mp4\n");

    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    if (!window.openVideoFile(outer)) return false;
    QString error;
    if (!check(!window.runAutomationText("VirtualDub.SaveAVI(\"source.mp4\");", directory, &error)
               && error.contains("aliases") && readFile(fixtures.mp4) == original,
               "GUI automation protects deep manifest dependencies")) return false;
    auto *queue = window.findChild<VDQtJobQueue*>();
    if (!queue) return false;
    VDQtJobState job;
    job.operation = VDQtJobOperation::RawVideoExport;
    job.sourcePaths = {outer};
    job.audioDisabled = true;
    job.replaceExisting = true;
    job.options.outputPath = fixtures.mp4;
    if (!check(!queue->addJobs({job}, &error) && error.contains("aliases")
               && readFile(fixtures.mp4) == original,
               "queue validation cannot approve replacing a nested input dependency")) return false;

    const QString audioSource = fixtures.directory.filePath("audio-source.mkv");
    const QString audioList = fixtures.directory.filePath("audio-list.txt");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=96x64:rate=24:duration=1",
                          "-f", "lavfi", "-i", "sine=frequency=440:duration=1:sample_rate=48000",
                          "-c:v", "ffv1", "-c:a", "pcm_s16le", audioSource})
        || !fixtures.writeText(audioList, "ffconcat version 1.0\nfile audio-source.mkv\n")
        || !window.openVideoFile(audioList)) return false;
    const QByteArray originalAudio = readFile(audioSource);
    if (!check(!window.runAutomationText("VirtualDub.SaveWAV(\"audio-source.mkv\");", directory, &error)
               && error.contains("aliases") && readFile(audioSource) == originalAudio,
               "audio automation protects a misnamed manifest's audio/video input")) return false;

    // AVS audio borrows the decoder's clip and has no independent source path.
    // The controller must still protect the script itself and save ordinary WAVs.
    const QString audioScript = fixtures.directory.filePath("audio-script.avs");
    QByteArray audioScriptBytes = readFile(fixtures.avs);
    audioScriptBytes.replace("audio_rate=0", "audio_rate=48000");
    if (!fixtures.writeText(audioScript, audioScriptBytes) || !window.openVideoFile(audioScript)) return false;
    if (!check(!window.runAutomationText("VirtualDub.SaveWAV(\"audio-script.avs\");", directory, &error)
               && error.contains("aliases") && readFile(audioScript) == audioScriptBytes,
               "borrowed AVS audio cannot overwrite its source script")) return false;
    if (!window.runAutomationText("VirtualDub.SaveWAV(\"safe-audio.wav\");", directory, &error)) {
        std::cerr << "Audio export failed: " << error.toStdString() << '\n';
        return false;
    }
    const QByteArray wav = readFile(fixtures.directory.filePath("safe-audio.wav"));
    return check(wav.startsWith("RIFF") && wav.size() > 44,
                 "staged script audio export retains normal WAV behavior");
}
} // namespace

bool VDQtRunOperationRegression(const QString& scenario, VDQtTestFixtures& fixtures) {
    if (scenario == "log_lifetime") return logLifetime();
    if (scenario == "recovery") return recoveryRetention(fixtures);
    if (scenario == "edit_preview") return editPreview(fixtures);
    if (scenario == "project_validation") return projectValidation(fixtures);
    if (scenario == "numeric_json") return jsonIntegerInputs(fixtures);
    if (scenario == "numeric_script") return scriptIntegerInputs(fixtures);
    if (scenario == "append_state") return appendState(fixtures);
    if (scenario == "raw_lifetime") return rawLifetime(fixtures);
    if (scenario == "vfr_jump") return vfrJump(fixtures);
    if (scenario == "resize_script") return resizeScript(fixtures);
    if (scenario == "external_encoder_logs") return externalEncoderLogs(fixtures);
    if (scenario == "source") return sourceLifetime(fixtures);
    if (scenario == "snapshot") return exportSnapshot(fixtures);
    if (scenario == "audio") return audioSnapshot(fixtures);
    if (scenario == "waveform") return waveformPreview(fixtures);
    if (scenario == "export_preview") return exportPreview(fixtures);
    if (scenario == "audio_inclusion") return audioInclusion(fixtures);
    if (scenario == "audio_export") return audioExportContracts(fixtures);
    if (scenario == "queue") return queueIsolation(fixtures);
    if (scenario == "queue_persistence") return queuePersistence(fixtures);
    if (scenario == "outputs") return outputFamilies(fixtures);
    if (scenario == "safety") return sourceProtection(fixtures);
    if (scenario == "unknown_timeline") return unknownTimeline(fixtures);
    if (scenario == "sparse_eof") return sparseEof(fixtures);
    if (scenario == "index_reuse") return indexReuse(fixtures);
    if (scenario == "indexed_navigation") return indexedNavigation(fixtures);
    if (scenario == "image_sequence") return importedImageTiming(fixtures);
    if (scenario == "filter_failure") return failedFilterPreview(fixtures);
    if (scenario == "filter_context") return editedFilterContext(fixtures);
    if (scenario == "empty_timeline") return emptyTimeline(fixtures);
    return check(false, "unknown operation regression");
}
