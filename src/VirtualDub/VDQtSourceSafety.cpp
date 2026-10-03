// Operation-scoped output alias protection. Dependency parsing and source stat
// work happen once at capture/refresh; every destination uses hash lookups.
#include "VDQtSourceSafety.h"
#include <QFile>
#include <QHash>
#include <QSet>
#include <sys/stat.h>

namespace {
struct Identity {
    quint64 device = 0, inode = 0;
    friend bool operator==(const Identity& a, const Identity& b) {
        return a.device == b.device && a.inode == b.inode;
    }
    friend size_t qHash(const Identity& value, size_t seed = 0) {
        return qHashMulti(seed, value.device, value.inode);
    }
};
bool fileIdentity(const QString& path, Identity *identity) {
    struct stat status {};
    const QByteArray name = QFile::encodeName(path);
    if (::stat(name.constData(), &status) != 0) return false;
    *identity = {quint64(status.st_dev), quint64(status.st_ino)};
    return true;
}
void appendUnique(QStringList& destination, const QStringList& values) {
    QSet<QString> seen(destination.cbegin(), destination.cend());
    for (const QString& value : values) if (!seen.contains(value)) {
        seen.insert(value); destination.append(value);
    }
}
}

struct VDQtSourceSafetySnapshot::Data {
    QStringList roots;
    QStringList knownConcatSources;
    VDQtScriptDependencyReport dependencies;
    QHash<QString, QString> paths;
    QHash<Identity, QString> identities;
};

bool VDQtSourceSafety::pathsReferToSameFile(const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty()) return false;
    const QString first = VDQtSourceDependencies::absoluteLocalPath(a), second = VDQtSourceDependencies::absoluteLocalPath(b);
    if (first == second) return true;
    Identity x, y;
    return fileIdentity(first, &x) && fileIdentity(second, &y) && x == y;
}

bool VDQtSourceSafety::isScriptPath(const QString& path) {
    const auto kind = VDQtSourceDependencies::classify(path);
    return kind == VDQtSourceDependencies::Kind::Script || kind == VDQtSourceDependencies::Kind::Concat
        || kind == VDQtSourceDependencies::Kind::UnsupportedList;
}

VDQtSourceSafetySnapshot VDQtSourceSafety::captureSources(
    const QStringList& directlyLoadedSources, const QString& scriptPath,
    const QStringList& knownConcatSources) {
    VDQtSourceSafetySnapshot result;
    auto data = std::make_shared<VDQtSourceSafetySnapshot::Data>();
    QSet<QString> roots;
    qsizetype pathBytes = 0;
    bool limited = false;
    const auto addRoot = [&](const QString& source) {
        if (source.isEmpty() || limited) return;
        const QString absolute = VDQtSourceDependencies::absoluteLocalPath(source);
        if (roots.contains(absolute)) return;
        if (roots.size() >= 100000 || pathBytes > 32 * 1024 * 1024 - absolute.size() * 2) {
            limited = true; return;
        }
        roots.insert(absolute); data->roots.append(absolute); pathBytes += absolute.size() * 2;
    };
    for (const QString& source : directlyLoadedSources) { addRoot(source); if (limited) break; }
    addRoot(scriptPath);
    for (const QString& source : knownConcatSources) {
        addRoot(source);
        if (limited) break;
        data->knownConcatSources.append(source);
    }
    data->dependencies = VDQtSourceDependencies::audit(data->roots, data->knownConcatSources);
    if (limited) {
        data->dependencies.complete = false;
        data->dependencies.diagnostics.append(QStringLiteral("The source set exceeds the audit work limit."));
    }
    const auto protect = [&](const QString& source) {
        const QString absolute = VDQtSourceDependencies::absoluteLocalPath(source);
        if (data->paths.contains(absolute)) return;
        data->paths.insert(absolute, absolute);
        Identity identity;
        if (fileIdentity(absolute, &identity)) data->identities.insert(identity, absolute);
        else data->dependencies.complete = false;
    };
    for (const QString& source : data->roots) protect(source);
    for (const QString& source : data->dependencies.resolvedPaths) protect(source);
    result.mData = std::move(data);
    return result;
}

VDQtOutputSafetyReport VDQtSourceSafetySnapshot::evaluateOutputPath(const QString& outputPath) const {
    VDQtOutputSafetyReport result;
    if (!mData) {
        result.issue = VDQtOutputSafetyIssue::ExistingDestinationWithIncompleteScriptAudit;
        return result;
    }
    result.scriptDependencies = mData->dependencies;
    const QString absolute = VDQtSourceDependencies::absoluteLocalPath(outputPath);
    result.aliasedPath = mData->paths.value(absolute);
    if (result.aliasedPath.isEmpty()) {
        Identity identity;
        if (fileIdentity(absolute, &identity)) result.aliasedPath = mData->identities.value(identity);
    }
    if (!result.aliasedPath.isEmpty()) {
        result.issue = VDQtOutputSafetyIssue::AliasesLoadedSource;
        return result;
    }
    if (!mData->dependencies.complete) {
        struct stat status {};
        const QByteArray name = QFile::encodeName(absolute);
        if (::lstat(name.constData(), &status) == 0)
            result.issue = VDQtOutputSafetyIssue::ExistingDestinationWithIncompleteScriptAudit;
    }
    return result;
}

void VDQtSourceSafetySnapshot::refresh() {
    if (!mData) return;
    const auto fresh = VDQtSourceSafety::captureSources(mData->roots, {}, mData->knownConcatSources);
    auto merged = std::make_shared<Data>(*fresh.mData);
    for (auto it = mData->paths.cbegin(); it != mData->paths.cend(); ++it) merged->paths.insert(it.key(), it.value());
    for (auto it = mData->identities.cbegin(); it != mData->identities.cend(); ++it) merged->identities.insert(it.key(), it.value());
    merged->dependencies.complete &= mData->dependencies.complete;
    appendUnique(merged->dependencies.resolvedPaths, mData->dependencies.resolvedPaths);
    appendUnique(merged->dependencies.unresolvedPathLiterals, mData->dependencies.unresolvedPathLiterals);
    appendUnique(merged->dependencies.diagnostics, mData->dependencies.diagnostics);
    merged->dependencies.filesRead += mData->dependencies.filesRead;
    merged->dependencies.bytesRead += mData->dependencies.bytesRead;
    mData = std::move(merged);
}

VDQtScriptDependencyReport VDQtSourceSafetySnapshot::dependencyReport() const {
    return mData ? mData->dependencies : VDQtScriptDependencyReport{};
}

VDQtOutputSafetyReport VDQtSourceSafety::evaluateOutputPath(
    const QString& outputPath, const QStringList& sources, const QString& scriptPath) {
    return captureSources(sources, scriptPath).evaluateOutputPath(outputPath);
}
