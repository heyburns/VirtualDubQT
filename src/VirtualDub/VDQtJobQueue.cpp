#include "VDQtJobQueue.h"

#include "VDQtSourceSafety.h"

#include <QDebug>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {

void trimJobLog(QStringList *entries) {
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

VDQtJobState *VDQtJobQueue::jobAt(int index) {
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
    if (job->status == VDQtJobStatus::Starting
        || job->status == VDQtJobStatus::Running
        || job->status == VDQtJobStatus::Aborting) {
        job->status = VDQtJobStatus::Interrupted;
        job->endedAtUtc = QDateTime::currentDateTimeUtc();
        job->error = QStringLiteral(
            "The previous application session ended while this job was running.");
    }
    job->progress = std::clamp(job->progress, 0.0, 1.0);
}

bool VDQtJobQueue::addJobs(const QList<VDQtJobState>& jobs,
                           QString *errorMessage) {
    if (jobs.isEmpty()) return true;
    if (mJobs.size() + jobs.size() > 1000) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The session queue is limited to 1000 jobs.");
        return false;
    }
    QList<VDQtJobState> candidates = mJobs;
    for (VDQtJobState job : jobs) {
        normalizeNewJob(&job);
        candidates.append(job);
    }
    if (!validateJobs(candidates, errorMessage)) return false;
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
    if (jobs.size() > 1000 || !validateJobs(jobs, errorMessage)) return false;
    QList<VDQtJobState> normalized = jobs;
    for (VDQtJobState& job : normalized) normalizeNewJob(&job);
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
    return VDQtProjectFile::saveJobQueue(path, mJobs, errorMessage);
}

void VDQtJobQueue::removeRows(const QList<int>& rows) {
    if (mRunning) return;
    QList<int> sorted = rows;
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
    VDQtJobState *job = jobAt(index);
    const QString normalized = name.trimmed();
    if (!job || normalized.isEmpty() || normalized.size() > 1024) return false;
    job->name = normalized;
    Q_EMIT jobChanged(index);
    scheduleAutosave();
    return true;
}

bool VDQtJobQueue::setJobStatus(int index, VDQtJobStatus status,
                                const QString& error) {
    VDQtJobState *job = jobAt(index);
    if (!job) return false;
    job->status = status;
    job->error = error;
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
    Q_EMIT jobChanged(index);
    scheduleAutosave();
    return true;
}

bool VDQtJobQueue::setJobProgress(int index, double progress,
                                  const QString& message) {
    VDQtJobState *job = jobAt(index);
    if (!job) return false;
    job->progress = std::clamp(progress, 0.0, 1.0);
    if (!message.isEmpty()
        && (job->logEntries.isEmpty() || job->logEntries.constLast() != message)) {
        job->logEntries.append(message.left(65536));
        trimJobLog(&job->logEntries);
    }
    Q_EMIT jobChanged(index);
    return true;
}

bool VDQtJobQueue::appendJobLog(int index, const QString& message) {
    VDQtJobState *job = jobAt(index);
    if (!job || message.isEmpty()) return false;
    job->logEntries.append(message.left(65536));
    trimJobLog(&job->logEntries);
    Q_EMIT jobChanged(index);
    scheduleAutosave();
    return true;
}

bool VDQtJobQueue::setReplaceExisting(int index, bool enabled) {
    VDQtJobState *job = jobAt(index);
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

void VDQtJobQueue::setAutosavePath(const QString& path) {
    mAutosavePath = path;
}

bool VDQtJobQueue::loadAutosave(QString *errorMessage) {
    if (mAutosavePath.isEmpty() || !QFileInfo::exists(mAutosavePath)) return true;
    return replaceFromFile(mAutosavePath, errorMessage);
}

bool VDQtJobQueue::flush(QString *errorMessage) const {
    if (mAutosavePath.isEmpty()) return true;
    return VDQtProjectFile::saveJobQueue(mAutosavePath, mJobs, errorMessage);
}

void VDQtJobQueue::scheduleAutosave() {
    if (!mAutosavePath.isEmpty()) mAutosaveTimer.start();
}

bool VDQtJobQueue::validateJobs(const QList<VDQtJobState>& jobs,
                                QString *errorMessage) {
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
    allSources.removeDuplicates();

    for (int i = 0; i < outputs.size(); ++i) {
        const QString& output = outputs.at(i);
        const VDQtOutputSafetyReport safety =
            VDQtSourceSafety::evaluateOutputPath(output, allSources);
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
        for (int other = 0; other < i; ++other) {
            if (VDQtSourceSafety::pathsReferToSameFile(output, outputs.at(other))) {
                if (errorMessage)
                    *errorMessage = QString("Two queued jobs have the same destination:\n%1")
                        .arg(output);
                return false;
            }
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
