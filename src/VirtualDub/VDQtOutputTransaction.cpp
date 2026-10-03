#include "VDQtOutputTransaction.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QVector>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
struct FileState {
    bool exists = false;
    struct stat value {};
};

QString systemError() {
    return QString::fromLocal8Bit(std::strerror(errno));
}

// lstat includes dangling symlinks: replacing a symlink replaces that directory
// entry, not its referent. Never confuse a dangling link with an unused name.
bool inspectFile(const QString& path, FileState *state, QString *error) {
    *state = {};
    const QByteArray name = QFile::encodeName(path);
    if (::lstat(name.constData(), &state->value) == 0) {
        state->exists = true;
        return true;
    }
    if (errno == ENOENT) return true;
    if (error) *error = QString("Could not inspect %1: %2").arg(path, systemError());
    return false;
}

bool sameIdentity(const FileState& a, const FileState& b) {
    return a.exists == b.exists && (!a.exists
        || (a.value.st_dev == b.value.st_dev && a.value.st_ino == b.value.st_ino));
}

bool unchanged(const FileState& a, const FileState& b) {
    return sameIdentity(a, b) && (!a.exists
        || (a.value.st_mode == b.value.st_mode && a.value.st_size == b.value.st_size
            && a.value.st_mtim.tv_sec == b.value.st_mtim.tv_sec
            && a.value.st_mtim.tv_nsec == b.value.st_mtim.tv_nsec
            && a.value.st_ctim.tv_sec == b.value.st_ctim.tv_sec
            && a.value.st_ctim.tv_nsec == b.value.st_ctim.tv_nsec));
}

// Moving/chmodding a file can change ctime without changing its contents.
bool samePayload(const FileState& a, const FileState& b) {
    return sameIdentity(a, b) && (!a.exists
        || (a.value.st_size == b.value.st_size
            && a.value.st_mtim.tv_sec == b.value.st_mtim.tv_sec
            && a.value.st_mtim.tv_nsec == b.value.st_mtim.tv_nsec));
}

bool directoryIdentity(const QString& path, FileState *state, QString *error) {
    const QByteArray name = QFile::encodeName(path);
    *state = {};
    if (::stat(name.constData(), &state->value) == 0
        && S_ISDIR(state->value.st_mode)) {
        state->exists = true;
        return true;
    }
    if (error) *error = QString("The output directory is unavailable: %1").arg(path);
    return false;
}
}

struct VDQtOutputTransaction::State {
    struct Entry {
        QString target;
        QString parent;
        FileState expected;
        FileState parentIdentity;
    };
    QTemporaryDir& staging;
    QStringList targets;
    QVector<Entry> entries;
    bool inspected = false;
    bool attempted = false;
};

VDQtOutputTransaction::VDQtOutputTransaction(
    QTemporaryDir& staging, const QStringList& targets)
    : mState(new State{staging, targets, {}, false, false}) {}

VDQtOutputTransaction::~VDQtOutputTransaction() = default;

bool VDQtOutputTransaction::inspect(QString *errorMessage) {
    if (errorMessage) errorMessage->clear();
    if (mState->attempted) {
        if (errorMessage) *errorMessage = QStringLiteral("This output transaction has already been used.");
        return false;
    }
    mState->inspected = false;
    mState->entries.clear();
    QSet<QString> names;
    QSet<QString> existingIdentities;
    if (!mState->staging.isValid() || mState->targets.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No valid staged output family was provided.");
        return false;
    }
    const QString stagingDirectory = QDir(mState->staging.path()).canonicalPath();
    for (const QString& target : mState->targets) {
        State::Entry entry;
        const QFileInfo info(target);
        entry.target = info.absoluteFilePath();
        entry.parent = info.absolutePath();
        if (target.isEmpty() || !directoryIdentity(entry.parent, &entry.parentIdentity, errorMessage)
            || !inspectFile(entry.target, &entry.expected, errorMessage)) return false;
        if (entry.expected.exists && !S_ISREG(entry.expected.value.st_mode)
            && !S_ISLNK(entry.expected.value.st_mode)) {
            if (errorMessage) *errorMessage = QString("The output is not a regular file or symlink: %1").arg(entry.target);
            return false;
        }
        const QString parentDirectory = QDir(entry.parent).canonicalPath();
        if (parentDirectory == stagingDirectory
            || parentDirectory.startsWith(stagingDirectory + QLatin1Char('/'))) {
            if (errorMessage) *errorMessage = QString("An output cannot be installed inside its temporary staging directory: %1")
                .arg(entry.target);
            return false;
        }
        const QString name = parentDirectory + QLatin1Char('/') + info.fileName();
        const QString identity = entry.expected.exists
            ? QString("%1:%2").arg(entry.expected.value.st_dev).arg(entry.expected.value.st_ino)
            : QString();
        if (names.contains(name) || (!identity.isEmpty() && existingIdentities.contains(identity))) {
            if (errorMessage) *errorMessage = QString("Two outputs refer to the same destination: %1").arg(entry.target);
            return false;
        }
        names.insert(name);
        if (!identity.isEmpty()) existingIdentities.insert(identity);
        mState->entries.append(entry);
    }
    mState->inspected = true;
    return true;
}

QStringList VDQtOutputTransaction::existingTargets() const {
    QStringList result;
    if (mState->inspected) {
        for (const State::Entry& entry : mState->entries)
            if (entry.expected.exists) result.append(entry.target);
    }
    return result;
}

bool VDQtOutputTransaction::renameNoReplace(
    const QString& from, const QString& to, QString *errorMessage) {
    const QByteArray source = QFile::encodeName(from), destination = QFile::encodeName(to);
    // Unlike QFile::rename's cross-device copy fallback, this cannot partially
    // copy a backup or overwrite a racing file. Fail safely on unsupported FSes.
    if (::syscall(SYS_renameat2, AT_FDCWD, source.constData(), AT_FDCWD,
                  destination.constData(), RENAME_NOREPLACE) == 0) return true;
    if (errorMessage) *errorMessage = QString("Could not move %1 to %2: %3")
        .arg(from, to, systemError());
    return false;
}

bool VDQtOutputTransaction::removeFile(const QString& path, QString *errorMessage) {
    const QByteArray name = QFile::encodeName(path);
    if (::unlink(name.constData()) == 0 || errno == ENOENT) return true;
    if (errorMessage) *errorMessage = QString("Could not remove the new output %1: %2")
        .arg(path, systemError());
    return false;
}

bool VDQtOutputTransaction::commit(
    const QStringList& stagedPaths, bool replaceExisting, QString *errorMessage) {
    if (errorMessage) errorMessage->clear();
    const auto failUnchanged = [&](const QString& error) {
        if (errorMessage) *errorMessage = error + QStringLiteral("\nNo output files were replaced.");
        return false;
    };
    if (!mState->inspected || mState->attempted
        || stagedPaths.size() != mState->entries.size())
        return failUnchanged(QStringLiteral("The output transaction was not prepared correctly."));
    mState->attempted = true;
    if (!replaceExisting && !existingTargets().isEmpty())
        return failUnchanged(QString("Replacement of existing output files was not approved:\n%1")
                                 .arg(existingTargets().join(QLatin1Char('\n'))));

    const auto validateTarget = [&](const State::Entry& entry, bool expectedAbsent, QString *error) {
        FileState parent, current;
        if (!directoryIdentity(entry.parent, &parent, error)
            || !inspectFile(entry.target, &current, error)) return false;
        if (!sameIdentity(parent, entry.parentIdentity)
            || (expectedAbsent ? current.exists : !unchanged(current, entry.expected))) {
            *error = QString("An output path changed after inspection/approval: %1").arg(entry.target);
            return false;
        }
        return true;
    };

    QString error;
    QVector<FileState> stages;
    QSet<QString> stageNames;
    for (int index = 0; index < stagedPaths.size(); ++index) {
        const State::Entry& entry = mState->entries.at(index);
        FileState stage;
        const QFileInfo staged(stagedPaths.at(index));
        if (!validateTarget(entry, false, &error)) return failUnchanged(error);
        if (staged.dir().canonicalPath() != QDir(mState->staging.path()).canonicalPath()
            || stageNames.contains(staged.absoluteFilePath())
            || !inspectFile(staged.absoluteFilePath(), &stage, &error)
            || !stage.exists || !S_ISREG(stage.value.st_mode) || stage.value.st_size <= 0
            || stage.value.st_dev != entry.parentIdentity.value.st_dev)
            return failUnchanged(QString("A completed file is missing, invalid, or not staged beside its destination: %1")
                                     .arg(stagedPaths.at(index)));
        stageNames.insert(staged.absoluteFilePath());
        // Preserve ordinary file permissions, but never follow an old symlink
        // to copy permissions from (or modify) its referent.
        if (entry.expected.exists && S_ISREG(entry.expected.value.st_mode)
            && !QFile::setPermissions(staged.absoluteFilePath(), QFileInfo(entry.target).permissions()))
            return failUnchanged(QString("Could not preserve output permissions: %1").arg(entry.target));
        stages.append(stage);
    }

    const QString backupDirectory = mState->staging.filePath(QStringLiteral("backups"));
    if (!QDir(mState->staging.path()).mkdir(QStringLiteral("backups")))
        return failUnchanged(QStringLiteral("A private output backup directory could not be created."));
    // Leave a human-readable map even if the process is interrupted halfway
    // through commit. A multi-file operation is not globally atomic or a
    // filesystem crash-consistency guarantee; individual moves are atomic.
    QByteArray recoveryMap("Original output -> backup (only backups still present need recovery)\n");
    for (int index = 0; index < mState->entries.size(); ++index) {
        const State::Entry& entry = mState->entries.at(index);
        if (entry.expected.exists)
            recoveryMap += entry.target.toUtf8() + " -> "
                + QDir(backupDirectory).filePath(QString::number(index)).toUtf8() + '\n';
    }
    QSaveFile manifest(mState->staging.filePath(QStringLiteral("recovery.txt")));
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(recoveryMap) != recoveryMap.size()
        || !manifest.commit())
        return failUnchanged(QStringLiteral("The output recovery map could not be written."));

    QVector<int> backedUp, installed;
    backedUp.reserve(mState->entries.size());
    installed.reserve(mState->entries.size());
    const bool autoRemove = mState->staging.autoRemove();
    // Preserve originals even if an unexpected exception unwinds the caller.
    // Normal success/complete rollback restores the caller's cleanup policy.
    mState->staging.setAutoRemove(false);
    const auto rollback = [&](const QString& primaryError) {
        QStringList recoveryProblems;
        for (auto it = installed.crbegin(); it != installed.crend(); ++it) {
            const QString target = mState->entries.at(*it).target;
            FileState current;
            QString detail;
            if (!inspectFile(target, &current, &detail)
                || (current.exists && !samePayload(current, stages.at(*it)))) {
                recoveryProblems.append(detail.isEmpty()
                    ? QString("A different file now occupies %1; it was left untouched.").arg(target) : detail);
            } else if (current.exists) {
                // Quarantine first; never unlink a public path after a stat
                // check, since a different file could arrive between them.
                const QString retired = mState->staging.filePath(
                    QString("rollback-output-%1").arg(*it));
                FileState moved;
                if (!renameNoReplace(target, retired, &detail)) {
                    recoveryProblems.append(detail);
                } else if (!inspectFile(retired, &moved, &detail)
                           || !samePayload(moved, stages.at(*it))) {
                    QString restoreError;
                    if (!renameNoReplace(retired, target, &restoreError))
                        recoveryProblems.append(QString("A changed output was preserved at %1 (original path %2).\n%3")
                                                    .arg(retired, target, restoreError));
                    else
                        recoveryProblems.append(QString("A changed output at %1 was left untouched.").arg(target));
                } else if (!removeFile(retired, &detail)) {
                    recoveryProblems.append(QString("%1\nNew output preserved at: %2").arg(detail, retired));
                }
            }
        }
        for (auto it = backedUp.crbegin(); it != backedUp.crend(); ++it) {
            const QString original = mState->entries.at(*it).target;
            const QString backup = QDir(backupDirectory).filePath(QString::number(*it));
            QString detail;
            if (!renameNoReplace(backup, original, &detail)) {
                recoveryProblems.append(QString("Original: %1\nPreserved backup: %2\n%3")
                                            .arg(original, backup, detail));
            }
        }
        QString message = primaryError;
        if (recoveryProblems.isEmpty()) {
            mState->staging.setAutoRemove(autoRemove);
            message += QStringLiteral("\nAny previous output files were restored.");
        } else {
            // Critical: the caller's QTemporaryDir must not delete the only
            // surviving originals when this scope exits after failed recovery.
            mState->staging.setAutoRemove(false);
            message += QString("\nAutomatic recovery was incomplete. Do not delete this recovery directory:\n%1\n\n%2")
                           .arg(mState->staging.path(), recoveryProblems.join(QStringLiteral("\n\n")));
        }
        if (errorMessage) *errorMessage = message;
        return false;
    };

    for (int index = 0; index < mState->entries.size(); ++index) {
        const State::Entry& entry = mState->entries.at(index);
        if (!validateTarget(entry, false, &error)) return rollback(error);
        if (!entry.expected.exists) continue;
        const QString backup = QDir(backupDirectory).filePath(QString::number(index));
        if (!renameNoReplace(entry.target, backup, &error)) return rollback(error);
        backedUp.append(index);
        FileState moved;
        if (!inspectFile(backup, &moved, &error) || !samePayload(moved, entry.expected))
            return rollback(QString("The output changed while its backup was being made: %1").arg(entry.target));
    }
    for (int index = 0; index < mState->entries.size(); ++index) {
        const State::Entry& entry = mState->entries.at(index);
        if (!validateTarget(entry, true, &error)
            || !renameNoReplace(stagedPaths.at(index), entry.target, &error)) return rollback(error);
        installed.append(index);
    }
    mState->staging.setAutoRemove(autoRemove);
    return true;
}
