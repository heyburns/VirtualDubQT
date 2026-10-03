#ifndef VDQT_PATH_IDENTITY_SET_H
#define VDQT_PATH_IDENTITY_SET_H

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <sys/stat.h>

// Shared native identity primitive for operation-scoped source/output audits.
// A filename alone cannot distinguish hard links or differently named symlinks.
struct VDQtFileIdentity {
    quint64 device = 0;
    quint64 inode = 0;
    friend bool operator==(const VDQtFileIdentity& a, const VDQtFileIdentity& b) {
        return a.device == b.device && a.inode == b.inode;
    }
    friend size_t qHash(const VDQtFileIdentity& identity, size_t seed = 0) {
        return qHashMulti(seed, identity.device, identity.inode);
    }
};

inline bool VDQtReadFileIdentity(const QString& path, VDQtFileIdentity *identity) {
    struct stat status {};
    const QByteArray native = QFile::encodeName(path);
    if (::stat(native.constData(), &status) != 0) return false;
    *identity = {quint64(status.st_dev), quint64(status.st_ino)};
    return true;
}

inline QString VDQtOutputPathKey(const QString& path) {
    // QFileInfo::absoluteFilePath() cleans '..' before resolving directory
    // symlinks. Preserve the raw traversal until the directory is canonical.
    QString target = QFileInfo(path).isAbsolute() ? path
        : QDir::currentPath() + QLatin1Char('/') + path;
    QSet<QString> visited;
    // Resolve leaf symlinks too, including a dangling link whose future target
    // is another queued destination. Bound loops just as the OS does; broken
    // loops cannot become valid writable output files.
    for (int depth = 0; depth < 40; ++depth) {
        if (visited.contains(target)) break;
        visited.insert(target);
        const QFileInfo info(target);
        if (!info.isSymLink()) break;
        const QString next = info.symLinkTarget();
        if (next.isEmpty()) break;
        target = next;
    }
    // Resolve the directory before lexical cleanup. A symlink followed by '..'
    // refers to its target's parent, not necessarily the symlink's own parent.
    const qsizetype slash = target.lastIndexOf(QLatin1Char('/'));
    const QString parent = QDir(slash == 0 ? QStringLiteral("/") : target.left(slash)).canonicalPath();
    return parent.isEmpty() ? QDir::cleanPath(target)
                            : QDir(parent).filePath(target.mid(slash + 1));
}

// Insert each output once instead of stat'ing every pair of destinations.
// Callers still revalidate live aliases before committing output: this is an
// operation-scoped duplicate detector, not proof against external file changes.
class VDQtPathIdentitySet final {
public:
    bool insert(const QString& path, QString *existingAlias = nullptr) {
        const QString key = VDQtOutputPathKey(path);
        QString alias = mPaths.value(key);
        VDQtFileIdentity identity;
        bool hasIdentity = false;
        if (alias.isEmpty()) {
            ++mIdentityQueries;
            hasIdentity = VDQtReadFileIdentity(path, &identity);
            if (hasIdentity) alias = mIdentities.value(identity);
        }
        if (!alias.isEmpty()) {
            if (existingAlias) *existingAlias = alias;
            return false;
        }
        mPaths.insert(key, path);
        if (hasIdentity) mIdentities.insert(identity, path);
        if (existingAlias) existingAlias->clear();
        return true;
    }
    qsizetype size() const { return mPaths.size(); }
    // Counts this helper's native inode queries, not Qt's directory/symlink
    // normalization work. One query per distinct prospective destination.
    qsizetype identityQueries() const { return mIdentityQueries; }

private:
    QHash<QString, QString> mPaths;
    QHash<VDQtFileIdentity, QString> mIdentities;
    qsizetype mIdentityQueries = 0;
};

#endif
