#ifndef VDQT_TEMPORARY_MEDIA_FILE_H
#define VDQT_TEMPORARY_MEDIA_FILE_H

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QTemporaryDir>

#include <memory>

// A materialized raw source is a file lease, not a window-lifetime cache.
// Consumers retain an Owner while using its path; the last release removes
// only the freshly generated directory and any partial source.nut within it.
// The enclosing session QTemporaryDir must outlive all leases, and callers
// must close/join decoder, audio, export and rollback consumers before release.
class VDQtTemporaryMediaFile final {
public:
    using Owner = std::shared_ptr<VDQtTemporaryMediaFile>;
    using WeakOwner = std::weak_ptr<VDQtTemporaryMediaFile>;

    static Owner create(const QString& sessionDirectory,
                        QString *errorMessage = nullptr) {
        const QFileInfo parent(sessionDirectory);
        const QString canonicalParent = parent.canonicalFilePath();
        if (sessionDirectory.isEmpty() || !parent.isDir() || canonicalParent.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "The temporary media session directory is unavailable.");
            return {};
        }
        Owner owner(new VDQtTemporaryMediaFile(
            QDir(canonicalParent).filePath(QStringLiteral("raw-XXXXXX"))));
        if (!owner->mDirectory.isValid()) {
            if (errorMessage) *errorMessage = owner->mDirectory.errorString();
            return {};
        }
        return owner;
    }

    QString path() const { return mDirectory.filePath(QStringLiteral("source.nut")); }
    QString directoryPath() const { return mDirectory.path(); }

    VDQtTemporaryMediaFile(const VDQtTemporaryMediaFile&) = delete;
    VDQtTemporaryMediaFile& operator=(const VDQtTemporaryMediaFile&) = delete;

private:
    explicit VDQtTemporaryMediaFile(const QString& directoryTemplate)
        : mDirectory(directoryTemplate) {}
    QTemporaryDir mDirectory;
};

// The path-only Open/reopen interface can recover an existing lease without
// making the registry itself retain old multi-gigabyte materializations. Never
// manufacture an owner from an arbitrary filename: only create() registers
// paths that this process actually owns.
class VDQtTemporaryMediaRegistry final {
public:
    using Owner = VDQtTemporaryMediaFile::Owner;
    Owner create(const QString& sessionDirectory,
                 QString *errorMessage = nullptr) {
        prune();
        Owner owner = VDQtTemporaryMediaFile::create(sessionDirectory, errorMessage);
        if (owner) mFiles.insert(owner->path(), owner);
        return owner;
    }

    Owner pin(const QString& path) const {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return mFiles.value(canonical.isEmpty() ? info.absoluteFilePath() : canonical).lock();
    }

    void prune() {
        for (auto it = mFiles.begin(); it != mFiles.end();) {
            if (it.value().expired()) it = mFiles.erase(it);
            else ++it;
        }
    }
    qsizetype trackedPaths() const { return mFiles.size(); }

private:
    QHash<QString, VDQtTemporaryMediaFile::WeakOwner> mFiles;
};

#endif
