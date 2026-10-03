// Durable serial job queue. This module enforces legal records/status changes,
// trims unbounded logs, emits model notifications, and coalesces autosave writes.
// Encoding remains the responsibility of VDQtMainWindow's queue runner.
#include "VDQtJobQueue.h"

#include "VDQtSourceSafety.h"
#include "VDQtPathIdentitySet.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr qsizetype kMaximumStructuralBytes = qsizetype{3} * 1024 * 1024;
// JSON can escape one QString code unit into six bytes. These combined limits
// leave space below the document's 4 MiB cap for diagnostics, indentation,
// timestamps and future status transitions of all 1000 permitted records.
constexpr qsizetype kMaximumDiagnosticCharacters = qsizetype{128} * 1024;
constexpr qsizetype kMaximumDiagnosticEntries = 2048;

QString ownedPath(const QString& path) {
    const QFileInfo info(path);
    const QString existing = info.canonicalFilePath();
    if (!existing.isEmpty()) return existing;
    const QString parent = info.absoluteDir().canonicalPath();
    return QDir(parent.isEmpty() ? info.absolutePath() : parent).filePath(info.fileName());
}

std::unique_ptr<QLockFile> claimPath(const QString& path) {
    auto lock = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
    // Long exports legitimately exceed the default lock age. Only a provably
    // dead owner, not an old modification time, makes this lock stale.
    lock->setStaleLockTime(0);
    return lock->tryLock(0) ? std::move(lock) : nullptr;
}

void trimJobLog(QStringList *entries) {
    // Jobs are persisted, so an unlimited encoder log would make autosave files
    // grow forever. Trim oldest entries by both count and total character size.
    if (!entries) return;
    qsizetype characters = 0;
    for (const QString& entry : std::as_const(*entries))
        characters += entry.size();
    constexpr qsizetype kMaximumLogCharacters = qsizetype{256} * 1024;
    while (entries->size() > 1000 || characters > kMaximumLogCharacters) {
        if (entries->isEmpty()) break;
        characters -= entries->constFirst().size();
        entries->removeFirst();
    }
}

} // namespace

VDQtJobQueue::VDQtJobQueue(QObject *parent)
    : QObject(parent) {
    mAutosaveTimer.setSingleShot(true);
    mAutosaveTimer.setInterval(250);
    connect(&mAutosaveTimer, &QTimer::timeout, this, [this]() {
        QString error;
        if (!flush(&error) && !error.isEmpty())
            qWarning() << "[Job queue] Autosave failed:" << error;
    });
}

VDQtJobQueue::~VDQtJobQueue() {
    if (mAutosaveTimer.isActive()) mAutosaveTimer.stop();
    QString error;
    if (!flush(&error) && !error.isEmpty())
        qWarning() << "[Job queue] Final autosave failed:" << error;
}

const VDQtJobState *VDQtJobQueue::jobAt(int index) const {
    return index >= 0 && index < mJobs.size() ? &mJobs.at(index) : nullptr;
}

VDQtJobState *VDQtJobQueue::mutableJobAt(int index) {
    return index >= 0 && index < mJobs.size() ? &mJobs[index] : nullptr;
}

void VDQtJobQueue::normalizeNewJob(VDQtJobState *job) {
    if (!job) return;
    if (job->id.trimmed().isEmpty())
        job->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (job->name.trimmed().isEmpty()) {
        const QString source = job->sourcePaths.value(0);
        const QString base = QFileInfo(source).completeBaseName();
        job->name = base.isEmpty() ? operationText(job->operation)
                                   : base;
    }
    // Running is meaningful only inside one process lifetime. A queue restored
    // after a crash must expose those records as interrupted and retryable.
    if (job->status == VDQtJobStatus::Starting
        || job->status == VDQtJobStatus::Running
        || job->status == VDQtJobStatus::Aborting) {
        job->status = VDQtJobStatus::Interrupted;
        job->endedAtUtc = QDateTime::currentDateTimeUtc();
        job->error = QStringLiteral(
            "The previous application session ended while this job was running.");
    }
    job->progress = std::clamp(job->progress, 0.0, 1.0);
    trimJobLog(&job->logEntries);
    job->error = job->error.left(4096);
}

void VDQtJobQueue::boundDiagnostics(QList<VDQtJobState> *jobs) {
    qsizetype characters = 0;
    qsizetype entries = 0;
    for (const VDQtJobState& job : std::as_const(*jobs)) {
        characters += job.error.size();
        entries += job.logEntries.size();
        for (const QString& entry : job.logEntries) characters += entry.size();
    }
    // Completed/earlier jobs surrender their oldest messages first. Operational
    // snapshots and statuses never disappear just because an encoder is noisy.
    for (VDQtJobState& job : *jobs) {
        while (!job.logEntries.isEmpty()
               && (characters > kMaximumDiagnosticCharacters
                   || entries > kMaximumDiagnosticEntries)) {
            characters -= job.logEntries.constFirst().size();
            job.logEntries.removeFirst();
            --entries;
        }
    }
    if (characters > kMaximumDiagnosticCharacters) {
        // At most 1000 short error summaries now use 128000 code units. Keep
        // every failure visible even if there are no log entries left to trim.
        for (VDQtJobState& job : *jobs) job.error = job.error.left(128);
    }
}

bool VDQtJobQueue::validateAdmission(const QList<VDQtJobState>& jobs,
                                    QString *errorMessage,
                                    const QString& proposedPath) const {
    QList<VDQtJobState> structural = jobs;
    for (VDQtJobState& job : structural) {
        job.error.clear();
        job.logEntries.clear();
    }
    QByteArray serialized;
    const QString path = !proposedPath.isEmpty() ? proposedPath : mAutosavePath.isEmpty()
        ? QDir::temp().filePath(QStringLiteral("VirtualDub.vdqjobs")) : mAutosavePath;
    if (!VDQtProjectFile::serializeJobQueue(path, structural, &serialized, errorMessage))
        return false;
    if (serialized.size() > kMaximumStructuralBytes) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The queue's processing snapshots exceed the 3 MiB admission limit. "
            "Remove jobs or save a separate job list; space is reserved for durable "
            "progress, errors and diagnostic history.");
        return false;
    }
    return true;
}

bool VDQtJobQueue::addJobs(const QList<VDQtJobState>& jobs,
                           QString *errorMessage) {
    if (jobs.isEmpty()) return true;
    if (jobs.size() > 1000 - mJobs.size()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The session queue is limited to 1000 jobs.");
        return false;
    }
    // Work on a copy so invalid additions cannot partially mutate the live
    // queue or produce a confusing sequence of model notifications.
    QList<VDQtJobState> candidates = mJobs;
    for (VDQtJobState job : jobs) {
        normalizeNewJob(&job);
        candidates.append(job);
    }
    boundDiagnostics(&candidates);
    if (!validateJobs(candidates, errorMessage)
        || !validateAdmission(candidates, errorMessage)) return false;
    Q_EMIT queueAboutToReset();
    mJobs = candidates;
    Q_EMIT queueReset();
    scheduleAutosave();
    if (mAutoRun && !mRunning) Q_EMIT runRequested();
    return true;
}

bool VDQtJobQueue::replaceJobs(const QList<VDQtJobState>& jobs,
                               QString *errorMessage) {
    if (mRunning) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The queue cannot be replaced while it is running.");
        return false;
    }
    if (!validateJobs(jobs, errorMessage)) return false;
    QList<VDQtJobState> normalized = jobs;
    for (VDQtJobState& job : normalized) normalizeNewJob(&job);
    boundDiagnostics(&normalized);
    if (!validateAdmission(normalized, errorMessage)) return false;
    Q_EMIT queueAboutToReset();
    mJobs = normalized;
    Q_EMIT queueReset();
    scheduleAutosave();
    if (mAutoRun && !mJobs.isEmpty()) Q_EMIT runRequested();
    return true;
}

bool VDQtJobQueue::appendFromFile(const QString& path, QString *errorMessage) {
    QList<VDQtJobState> loaded;
    return VDQtProjectFile::loadJobQueue(path, &loaded, errorMessage)
        && addJobs(loaded, errorMessage);
}

bool VDQtJobQueue::replaceFromFile(const QString& path, QString *errorMessage) {
    QList<VDQtJobState> loaded;
    return VDQtProjectFile::loadJobQueue(path, &loaded, errorMessage)
        && replaceJobs(loaded, errorMessage);
}

bool VDQtJobQueue::saveToFile(const QString& path, QString *errorMessage) const {
    // Explicit Save As is not allowed to bypass another editor's ownership.
    // Our own autosave lock already proves ownership of the local destination.
    std::unique_ptr<QLockFile> temporaryLock;
    const QString destination = ownedPath(path);
    if (destination != mAutosavePath || !mAutosaveLock) {
        temporaryLock = claimPath(destination);
        if (!temporaryLock) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "The selected job list is in use by another application instance, "
                "or its ownership lock could not be created.");
            return false;
        }
    }
    return VDQtProjectFile::saveJobQueue(destination, mJobs, errorMessage);
}

void VDQtJobQueue::removeRows(const QList<int>& rows) {
    if (mRunning) return;
    QList<int> sorted = rows;
    // Descending removal keeps later row indices valid as earlier rows vanish.
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    QList<VDQtJobState> remaining = mJobs;
    bool changed = false;
    for (int row : sorted) {
        if (row < 0 || row >= remaining.size()) continue;
        const VDQtJobStatus status = remaining.at(row).status;
        if (status == VDQtJobStatus::Starting
            || status == VDQtJobStatus::Running
            || status == VDQtJobStatus::Aborting)
            continue;
        remaining.removeAt(row);
        changed = true;
    }
    if (changed) {
        Q_EMIT queueAboutToReset();
        mJobs = remaining;
        Q_EMIT queueReset();
        scheduleAutosave();
    }
}

void VDQtJobQueue::clearAll() {
    if (mRunning) return;
    if (mJobs.isEmpty()) return;
    Q_EMIT queueAboutToReset();
    mJobs.clear();
    Q_EMIT queueReset();
    scheduleAutosave();
}

void VDQtJobQueue::clearCompleted() {
    if (mRunning) return;
    QList<VDQtJobState> remaining;
    remaining.reserve(mJobs.size());
    for (const VDQtJobState& job : mJobs) {
        if (job.status != VDQtJobStatus::Complete) remaining.append(job);
    }
    if (remaining.size() != mJobs.size()) {
        Q_EMIT queueAboutToReset();
        mJobs = remaining;
        Q_EMIT queueReset();
        scheduleAutosave();
    }
}

void VDQtJobQueue::retryFailed() {
    bool changed = false;
    for (int i = 0; i < mJobs.size(); ++i) {
        VDQtJobState& job = mJobs[i];
        if (job.status == VDQtJobStatus::Failed
            || job.status == VDQtJobStatus::Cancelled
            || job.status == VDQtJobStatus::Interrupted) {
            job.status = VDQtJobStatus::Pending;
            job.error.clear();
            job.progress = 0.0;
            Q_EMIT jobChanged(i);
            changed = true;
        }
    }
    if (changed) scheduleAutosave();
}

void VDQtJobQueue::setAllPendingPostponed(bool postponed) {
    bool changed = false;
    for (int i = 0; i < mJobs.size(); ++i) {
        VDQtJobState& job = mJobs[i];
        if ((postponed && job.status == VDQtJobStatus::Pending)
            || (!postponed && job.status == VDQtJobStatus::Postponed)) {
            job.status = postponed ? VDQtJobStatus::Postponed
                                   : VDQtJobStatus::Pending;
            Q_EMIT jobChanged(i);
            changed = true;
        }
    }
    if (changed) scheduleAutosave();
}

bool VDQtJobQueue::moveJob(int from, int to) {
    if (mRunning || from < 0 || from >= mJobs.size()
        || to < 0 || to >= mJobs.size() || from == to)
        return false;
    Q_EMIT queueAboutToReset();
    mJobs.move(from, to);
    Q_EMIT queueReset();
    scheduleAutosave();
    return true;
}

bool VDQtJobQueue::setJobName(int index, const QString& name) {
    VDQtJobState *job = mutableJobAt(index);
    const QString normalized = name.trimmed();
    if (!job || normalized.isEmpty() || normalized.size() > 1024) return false;
    QList<VDQtJobState> candidates = mJobs;
    candidates[index].name = normalized;
    if (!validateAdmission(candidates, nullptr)) return false;
    job->name = normalized;
    Q_EMIT jobChanged(index);
    scheduleAutosave();
    return true;
}

bool VDQtJobQueue::setJobStatus(int index, VDQtJobStatus status,
                                const QString& error) {
    VDQtJobState *job = mutableJobAt(index);
    if (!job || status < VDQtJobStatus::Pending || status > VDQtJobStatus::Interrupted)
        return false;
    job->status = status;
    job->error = error.left(4096);
    if (status == VDQtJobStatus::Starting) {
        job->startedAtUtc = QDateTime::currentDateTimeUtc();
        job->endedAtUtc = QDateTime();
        job->progress = 0.0;
    } else if (status == VDQtJobStatus::Complete
               || status == VDQtJobStatus::Cancelled
               || status == VDQtJobStatus::Failed
               || status == VDQtJobStatus::Interrupted) {
        job->endedAtUtc = QDateTime::currentDateTimeUtc();
        if (status == VDQtJobStatus::Complete) job->progress = 1.0;
    }
    boundDiagnostics(&mJobs);
    Q_EMIT jobChanged(index);
    scheduleAutosave();
    return true;
}

bool VDQtJobQueue::setJobProgress(int index, double progress,
                                  const QString& message) {
    VDQtJobState *job = mutableJobAt(index);
    if (!job || !std::isfinite(progress)) return false;
    job->progress = std::clamp(progress, 0.0, 1.0);
    if (!message.isEmpty()
        && (job->logEntries.isEmpty() || job->logEntries.constLast() != message)) {
        job->logEntries.append(message.left(65536));
        trimJobLog(&job->logEntries);
        boundDiagnostics(&mJobs);
    }
    Q_EMIT jobChanged(index);
    // Progress is deliberately not autosaved on every callback; exporters can
    // report many times per second. Status/log transitions provide checkpoints
    // without turning the queue file into an I/O bottleneck.
    return true;
}

bool VDQtJobQueue::appendJobLog(int index, const QString& message) {
    VDQtJobState *job = mutableJobAt(index);
    if (!job || message.isEmpty()) return false;
    job->logEntries.append(message.left(65536));
    trimJobLog(&job->logEntries);
    boundDiagnostics(&mJobs);
    Q_EMIT jobChanged(index);
    scheduleAutosave();
    return true;
}

bool VDQtJobQueue::setReplaceExisting(int index, bool enabled) {
    VDQtJobState *job = mutableJobAt(index);
    if (!job) return false;
    job->replaceExisting = enabled;
    Q_EMIT jobChanged(index);
    scheduleAutosave();
    return true;
}

void VDQtJobQueue::setRunning(bool running, int currentIndex) {
    if (mRunning == running && mCurrentIndex == currentIndex) return;
    mRunning = running;
    mCurrentIndex = running ? currentIndex : -1;
    Q_EMIT runningChanged(mRunning, mCurrentIndex);
}

int VDQtJobQueue::pendingCount() const {
    int result = 0;
    for (const VDQtJobState& job : mJobs)
        if (job.status == VDQtJobStatus::Pending) ++result;
    return result;
}

void VDQtJobQueue::setAutoRunEnabled(bool enabled) {
    if (mAutoRun == enabled) return;
    mAutoRun = enabled;
    if (mAutoRun && !mRunning && pendingCount() > 0) Q_EMIT runRequested();
}

bool VDQtJobQueue::setAutosavePath(const QString& path, QString *errorMessage) {
    const QString requested = path.isEmpty() ? QString() : ownedPath(path);
    if (requested == mAutosavePath && mAutosaveLock) return true;
    // Relative paths can be substantially longer in a new document directory.
    // Reject before releasing the old lock or changing any durable destination.
    if (!requested.isEmpty() && !validateAdmission(mJobs, errorMessage, requested))
        return false;
    if (!mAutosavePath.isEmpty() && mAutosaveLock && !flush(errorMessage)) return false;
    mAutosaveTimer.stop();
    mAutosaveLock.reset();
    mAutosavePath.clear();
    mRecoveryPath.clear();
    mAutosaveProtected = false;
    if (requested.isEmpty()) {
        reportPersistence(QString());
        return true;
    }
    QString selected = requested;
    auto lock = claimPath(selected);
    if (!lock) {
        // Reuse an abandoned secondary session instead of allocating a new
        // orphan on every restart. Live sessions retain their locks and cannot
        // be mistaken for crash-recovery candidates.
        const QFileInfo original(requested);
        const QString prefix = original.completeBaseName() + QStringLiteral("-session-");
        const QString suffix = original.suffix().isEmpty()
            ? QString() : QLatin1Char('.') + original.suffix();
        const QDir directory = original.absoluteDir();
        const QStringList candidates = directory.entryList(
            {prefix + QLatin1Char('*') + suffix}, QDir::Files, QDir::Time);
        for (const QString& candidate : candidates) {
            const QString candidatePath = directory.filePath(candidate);
            auto candidateLock = claimPath(candidatePath);
            if (candidateLock) {
                selected = candidatePath;
                lock = std::move(candidateLock);
                break;
            }
        }
        if (!lock) {
            selected = directory.filePath(prefix
                + QUuid::createUuid().toString(QUuid::WithoutBraces) + suffix);
            lock = claimPath(selected);
        }
    }
    mAutosavePath = selected;
    if (!lock) {
        const QString error = QStringLiteral(
            "The local job list could not acquire an ownership lock. "
            "Queue changes cannot be saved automatically: %1").arg(selected);
        reportPersistence(error);
        if (errorMessage) *errorMessage = error;
        return false;
    }
    mAutosaveLock = std::move(lock);
    // Preserve the established primary recovery filename for existing sessions.
    // Secondary editors have an independent, equally durable snapshot path.
    mRecoveryPath = QFileInfo(selected).absoluteDir().filePath(
        selected == requested && QFileInfo(selected).fileName() == QStringLiteral("VirtualDub.vdqjobs")
        ? QStringLiteral("crash-recovery.vdqproject")
        : QFileInfo(selected).completeBaseName() + QStringLiteral("-recovery.vdqproject"));
    // Publish the session marker before any recovery snapshot can be written.
    // A secondary editor may crash without ever adding a job; its empty queue
    // still lets the next eligible instance find that paired recovery file.
    if (!QFileInfo::exists(mAutosavePath) && !flush(errorMessage)) {
        mRecoveryPath.clear();
        return false;
    }
    reportPersistence(QString());
    return true;
}

bool VDQtJobQueue::loadAutosave(QString *errorMessage) {
    if (mAutosavePath.isEmpty() || !QFileInfo::exists(mAutosavePath)) return true;
    QString error;
    const bool loaded = replaceFromFile(mAutosavePath, &error);
    if (!loaded) {
        mAutosaveProtected = true;
        reportPersistence(QStringLiteral(
            "The previous local job list could not be restored and is preserved. "
            "Automatic saving is paused; save new jobs to a separate job list. %1").arg(error));
        if (errorMessage) *errorMessage = mPersistenceError;
    }
    return loaded;
}

void VDQtJobQueue::reportPersistence(const QString& error) {
    if (mPersistenceError == error) return;
    mPersistenceError = error;
    Q_EMIT persistenceStatusChanged(error);
}

bool VDQtJobQueue::flush(QString *errorMessage) {
    if (mAutosavePath.isEmpty()) return true;
    QString error;
    bool saved = false;
    if (!mAutosaveLock) {
        error = QStringLiteral("The local job list has no ownership lock; automatic saving is unavailable.");
    } else if (mAutosaveProtected) {
        error = mPersistenceError;
    } else {
        boundDiagnostics(&mJobs);
        saved = VDQtProjectFile::saveJobQueue(mAutosavePath, mJobs, &error);
    }
    if (errorMessage) *errorMessage = error;
    reportPersistence(saved ? QString() : error);
    return saved;
}

void VDQtJobQueue::scheduleAutosave() {
    // Restarting the single-shot timer coalesces a burst of UI edits into one
    // atomic project-file write.
    if (!mAutosavePath.isEmpty()) mAutosaveTimer.start();
}

bool VDQtJobQueue::validateJobs(const QList<VDQtJobState>& jobs,
                                QString *errorMessage) {
    // Validation is global because a path can be safe within one record yet
    // collide with a source or destination belonging to another queued record.
    if (jobs.size() > 1000) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The session queue is limited to 1000 jobs.");
        return false;
    }
    QStringList allSources;
    QStringList outputs;
    for (const VDQtJobState& job : jobs) {
        if (job.sourcePaths.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("A queued job has no source.");
            return false;
        }
        allSources.append(job.sourcePaths);
        if (!job.audioSourcePath.isEmpty()) allSources.append(job.audioSourcePath);
        const QString output = job.options.outputPath;
        if (job.operation != VDQtJobOperation::VideoAnalysis && output.isEmpty()) {
            if (errorMessage)
                *errorMessage = QString("Job '%1' has no destination.").arg(job.name);
            return false;
        }
        if (!output.isEmpty()) outputs.append(output);
    }
    const auto sourceSafety = VDQtSourceSafety::captureSources(allSources);

    VDQtPathIdentitySet destinations;
    for (const QString& output : std::as_const(outputs)) {
        const VDQtOutputSafetyReport safety =
            sourceSafety.evaluateOutputPath(output);
        if (!safety.isSafe()) {
            if (errorMessage) {
                *errorMessage = safety.issue == VDQtOutputSafetyIssue::AliasesLoadedSource
                    ? QString("A queued destination aliases a queued source:\n%1\n\nSource:\n%2")
                          .arg(output, safety.aliasedPath)
                    : QString("An existing queued destination cannot be safely distinguished "
                              "from a dynamically computed script dependency:\n%1")
                          .arg(output);
            }
            return false;
        }
        QString alias;
        if (!destinations.insert(output, &alias)) {
            if (errorMessage)
                *errorMessage = QString("Two queued jobs have the same destination:\n%1\n\n%2")
                    .arg(output, alias);
            return false;
        }
    }
    return true;
}

QString VDQtJobQueue::operationText(VDQtJobOperation operation) {
    switch (operation) {
    case VDQtJobOperation::VideoExport: return QStringLiteral("Video export");
    case VDQtJobOperation::AudioExport: return QStringLiteral("Audio export");
    case VDQtJobOperation::RawVideoExport: return QStringLiteral("Raw video export");
    case VDQtJobOperation::ImageSequenceExport: return QStringLiteral("Image sequence");
    case VDQtJobOperation::VideoAnalysis: return QStringLiteral("Video analysis");
    }
    return QStringLiteral("Unknown");
}

QString VDQtJobQueue::statusText(VDQtJobStatus status) {
    switch (status) {
    case VDQtJobStatus::Pending: return QStringLiteral("Waiting");
    case VDQtJobStatus::Starting: return QStringLiteral("Starting");
    case VDQtJobStatus::Running: return QStringLiteral("In progress");
    case VDQtJobStatus::Aborting: return QStringLiteral("Aborting");
    case VDQtJobStatus::Complete: return QStringLiteral("Done");
    case VDQtJobStatus::Postponed: return QStringLiteral("Postponed");
    case VDQtJobStatus::Cancelled: return QStringLiteral("Aborted");
    case VDQtJobStatus::Failed: return QStringLiteral("Error");
    case VDQtJobStatus::Interrupted: return QStringLiteral("Interrupted");
    }
    return QStringLiteral("Unknown");
}
