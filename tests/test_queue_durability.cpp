// Job lists are durable state, not an encoder-specific troubleshooting suite.
// Every path/process/lock below belongs to a disposable fixture directory.
#include "VirtualDub/VDQtJobQueue.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
VDQtJobState job(const QTemporaryDir& directory, int index) {
    VDQtJobState result;
    result.id = QStringLiteral("fixture-%1").arg(index);
    result.name = QStringLiteral("Fixture %1").arg(index);
    result.sourcePaths = {directory.filePath("source.avi")};
    result.options.outputPath = directory.filePath(QStringLiteral("output-%1.avi").arg(index));
    return result;
}
QByteArray contents(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool pressure() {
    QTemporaryDir directory;
    VDQtJobQueue queue;
    QString error;
    const QString path = directory.filePath("VirtualDub.vdqjobs");
    if (!directory.isValid() || !queue.setAutosavePath(path, &error)) return false;
    QList<VDQtJobState> records;
    // The old individually permitted logs made these 17 accepted records too
    // large to save. Escape-heavy text checks bytes rather than ASCII alone.
    const QString noisy(65536, QChar('\x01'));
    for (int index = 0; index < 17; ++index) {
        auto record = job(directory, index);
        record.logEntries = {noisy, noisy, noisy, noisy};
        records.append(record);
    }
    bool passed = check(queue.addJobs(records, &error), "noisy jobs remain admissible after global history trimming");
    passed &= check(queue.flush(&error), "accepted noisy jobs have a durable checkpoint");
    passed &= check(QFileInfo(path).size() <= 4 * 1024 * 1024, "escaped diagnostic history fits the document cap");
    QList<VDQtJobState> restored;
    passed &= check(VDQtProjectFile::loadJobQueue(path, &restored, &error)
        && restored.size() == records.size(), "bounded diagnostics do not discard operational job snapshots");
    for (int cycle = 0; cycle < 40; ++cycle) {
        const int index = cycle % queue.count();
        passed &= check(queue.appendJobLog(index, noisy), "runtime log pressure is accepted");
        passed &= check(queue.setJobStatus(index, VDQtJobStatus::Failed,
            QString(65536, QChar('\x02'))), "runtime failures retain bounded summaries");
        passed &= check(queue.flush(&error), "every noisy status transition remains serializable");
    }
    passed &= check(!queue.setJobProgress(0, std::numeric_limits<double>::quiet_NaN()),
                    "nonfinite progress cannot poison persistence");
    passed &= check(!queue.setJobStatus(0, static_cast<VDQtJobStatus>(100)),
                    "invalid runtime status cannot poison persistence");
    const QByteArray before = contents(path);
    const int count = queue.count();
    auto oversized = job(directory, 100);
    for (int field = 0; field < 60; ++field)
        oversized.processing.textMetadata.insert(QStringLiteral("field-%1").arg(field), QString(65536, 'x'));
    passed &= check(!queue.addJobs({oversized}, &error) && !error.isEmpty()
        && queue.count() == count, "structural overflow is rejected atomically at admission");
    passed &= check(contents(path) == before, "rejected additions leave the last durable file untouched");
    return passed;
}
bool faults() {
    QTemporaryDir directory;
    const QString path = directory.filePath("VirtualDub.vdqjobs");
    VDQtJobQueue queue;
    QString error;
    if (!directory.isValid() || !queue.setAutosavePath(path, &error)
        || !queue.addJobs({job(directory, 0)}, &error)) return false;
    int failures = 0, recoveries = 0;
    QObject::connect(&queue, &VDQtJobQueue::persistenceStatusChanged,
        [&failures, &recoveries](const QString& text) {
            if (text.isEmpty()) ++recoveries;
            else ++failures;
        });
    // A directory at the destination forces a write failure even as root and
    // avoids relying on a machine's permission/disk configuration.
    if (!QFile::remove(path) || !QDir().mkdir(path)) return false;
    bool passed = check(!queue.flush(&error) && !error.isEmpty()
        && !queue.persistenceError().isEmpty() && failures == 1,
        "checkpoint failures are exposed as sticky queue status");
    passed &= check(!queue.flush(&error) && failures == 1,
                    "unchanged repeated failures do not flood notifications");
    if (!QDir().rmdir(path)) return false;
    passed &= check(queue.flush(&error) && queue.persistenceError().isEmpty()
        && recoveries == 1, "successful retry clears the save failure");
    return passed;
}
bool malformedPreserved() {
    QTemporaryDir directory;
    const QString path = directory.filePath("VirtualDub.vdqjobs");
    const QByteArray corrupt("not valid JSON\n");
    if (!directory.isValid() || !write(path, corrupt)) return false;
    bool passed = true;
    {
        VDQtJobQueue queue;
        QString error;
        if (!queue.setAutosavePath(path, &error)) return false;
        passed &= check(!queue.loadAutosave(&error) && !error.isEmpty(), "invalid old queue is reported");
        passed &= check(queue.addJobs({job(directory, 0)}, &error), "new in-memory work is still possible");
        passed &= check(!queue.flush(&error) && contents(path) == corrupt,
                        "autosave cannot erase the queue that failed restoration");
        passed &= check(queue.saveToFile(directory.filePath("rescued.vdqjobs"), &error),
                        "new work can be rescued into a separate job list");
    }
    passed &= check(contents(path) == corrupt, "destruction preserves the failed original job list");
    return passed;
}
bool owners(const QString& executable) {
    QTemporaryDir directory;
    QString error;
    const QString path = directory.filePath("VirtualDub.vdqjobs");
    VDQtJobQueue primary;
    if (!directory.isValid() || !primary.setAutosavePath(path, &error)
        || !primary.addJobs({job(directory, 0)}, &error) || !primary.flush(&error)) return false;
    const QByteArray primaryBytes = contents(path);
    QString secondaryPath, secondaryRecovery;
    bool passed = true;
    {
        VDQtJobQueue secondary;
        if (!secondary.setAutosavePath(path, &error)) return false;
        secondaryPath = secondary.autosavePath();
        secondaryRecovery = secondary.recoveryPath();
        passed &= check(QFileInfo::exists(secondaryPath),
                        "an empty secondary session publishes a discoverable recovery marker");
        passed &= check(secondaryPath != primary.autosavePath()
            && secondaryRecovery != primary.recoveryPath(), "concurrent editors own independent queue and recovery paths");
        passed &= check(secondary.addJobs({job(directory, 1)}, &error) && secondary.flush(&error),
                        "secondary sessions retain durable queues");
        passed &= check(!secondary.saveToFile(path, &error) && contents(path) == primaryBytes,
                        "explicit Save As cannot overwrite another editor's owned queue");
    }
    {
        VDQtJobQueue resumed;
        passed &= check(resumed.setAutosavePath(path, &error)
            && resumed.autosavePath() == secondaryPath
            && resumed.recoveryPath() == secondaryRecovery
            && resumed.loadAutosave(&error) && resumed.count() == 1
            && resumed.jobAt(0)->id == QStringLiteral("fixture-1"),
            "an unlocked secondary session resumes its independent queue and recovery path");
    }
    // Check ownership across real processes, not only QObject instances. The
    // helper terminates on stdin EOF and touches no path outside this fixture.
    QProcess child;
    child.start(executable, {QStringLiteral("--hold-owned"), path});
    if (!child.waitForStarted(5000) || !child.waitForReadyRead(5000)) return false;
    const QString childPath = QString::fromUtf8(child.readAllStandardOutput()).trimmed();
    passed &= check(!childPath.isEmpty() && childPath != path,
                    "another process cannot acquire the primary editor's queue");
    passed &= check(!primary.saveToFile(childPath, &error), "cross-process ownership also protects explicit saves");
    child.closeWriteChannel();
    if (!child.waitForFinished(5000)) {
        child.kill();
        child.waitForFinished(5000);
        return false;
    }
    passed &= check(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                    "owned helper exits cleanly");
    passed &= check(contents(path) == primaryBytes, "independent queue activity never rewrites the primary file");
    return passed;
}
bool movedDirectoryAdmission() {
    QTemporaryDir directory;
    VDQtJobQueue queue;
    QString error;
    const QString original = directory.filePath("original.vdqjobs");
    if (!directory.isValid() || !queue.setAutosavePath(original, &error)) return false;
    QList<VDQtJobState> records;
    for (int index = 0; index < 100; ++index) {
        auto record = job(directory, index);
        record.processing.textMetadata["description"] = QString(25000, 'x');
        records.append(record);
    }
    if (!queue.addJobs(records, &error) || !queue.flush(&error)) {
        std::cerr << error.toStdString() << '\n';
        return false;
    }
    const QByteArray before = contents(original);
    // The new directory does not need to exist: admission must fail before
    // lock creation, using the much longer ../../ paths it would serialize.
    const QString proposed = directory.path() + QString("/d").repeated(1000) + "/moved.vdqjobs";
    bool passed = check(!queue.setAutosavePath(proposed, &error)
        && (error.contains("admission") || error.contains("safety limit")),
        "moving the durable document revalidates expanded relative-path storage");
    passed &= check(queue.autosavePath() == original && contents(original) == before
        && queue.flush(&error), "rejected destination changes retain the old ownership and durable queue");
    return passed;
}
int benchmarkValidation() {
    QTemporaryDir directory;
    if (!directory.isValid() || !write(directory.filePath("source.avi"), "source fixture")) return 2;
    QList<VDQtJobState> records;
    for (int index = 0; index < 1000; ++index) {
        const auto record = job(directory, index);
        if (!write(record.options.outputPath, "existing destination")) return 2;
        records.append(record);
    }
    QElapsedTimer timer;
    timer.start();
    QString error;
    const bool valid = VDQtJobQueue::validateJobs(records, &error);
    std::cout << "1000-job existing-output validation: "
              << timer.nsecsElapsed() / 1000000.0 << " ms\n";
    if (!valid) std::cerr << error.toStdString() << '\n';
    return valid ? 0 : 1;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().contains("--benchmark-validation")) return benchmarkValidation();
    if (application.arguments().value(1) == QStringLiteral("--hold-owned")) {
        VDQtJobQueue queue;
        QString error;
        if (!queue.setAutosavePath(application.arguments().value(2), &error)
            || !queue.flush(&error)) return 2;
        std::cout << queue.autosavePath().toStdString() << std::endl;
        std::cin.get();
        return 0;
    }
    bool passed = pressure();
    passed &= faults();
    passed &= malformedPreserved();
    passed &= owners(application.applicationFilePath());
    passed &= movedDirectoryAdmission();
    return passed ? 0 : 1;
}
