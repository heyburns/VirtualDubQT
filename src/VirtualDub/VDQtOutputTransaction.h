// Recoverable installation of a rendered output family. Rendering stays in a
// caller-owned temporary directory; destination inspection precedes approval,
// and commit never treats approval of one file as approval of another file.
#pragma once

#include <QStringList>
#include <memory>

class QTemporaryDir;

class VDQtOutputTransaction {
public:
    VDQtOutputTransaction(QTemporaryDir& staging, const QStringList& targets);
    virtual ~VDQtOutputTransaction();
    VDQtOutputTransaction(const VDQtOutputTransaction&) = delete;
    VDQtOutputTransaction& operator=(const VDQtOutputTransaction&) = delete;

    // Snapshot exact destination identities before asking for approval (or
    // before a long render). Directories and duplicate destinations are errors.
    bool inspect(QString *errorMessage);
    QStringList existingTargets() const;

    // replaceExisting authorizes only the existing files captured by inspect.
    // Newly appeared/replaced/modified files cause failure, not silent overwrite.
    // An incomplete rollback disables staging auto-removal and reports every
    // remaining original backup's absolute path in errorMessage.
    bool commit(const QStringList& stagedPaths, bool replaceExisting,
                QString *errorMessage);

protected:
    // Narrow filesystem seam used by deterministic fault-injection regressions.
    // Production renames are atomic, same-filesystem, and never overwrite an
    // entry that appeared between the collision check and rename.
    virtual bool renameNoReplace(const QString& from, const QString& to,
                                 QString *errorMessage);
    virtual bool removeFile(const QString& path, QString *errorMessage);

private:
    struct State;
    std::unique_ptr<State> mState;
};
