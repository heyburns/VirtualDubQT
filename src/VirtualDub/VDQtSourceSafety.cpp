// Output-path alias protection. Comparisons proceed from cheap normalized path
// checks to device/inode identity, then include conservatively audited script
// dependencies before an exporter is allowed to replace a destination.
#include "VDQtSourceSafety.h"

#include <QFile>
#include <QFileInfo>

#include <sys/stat.h>

bool VDQtSourceSafety::pathsReferToSameFile(const QString& firstPath,
                                            const QString& secondPath) {
    if (firstPath.isEmpty() || secondPath.isEmpty()) return false;
    const QFileInfo first(firstPath);
    const QFileInfo second(secondPath);
    if (first.absoluteFilePath() == second.absoluteFilePath()) return true;

    // Text comparison misses hard links and symlink aliases. Device/inode is
    // the authoritative identity for existing Unix files and does not require
    // resolving user-controlled path strings ourselves.
    struct stat firstStatus = {};
    struct stat secondStatus = {};
    const QByteArray firstName = QFile::encodeName(first.absoluteFilePath());
    const QByteArray secondName = QFile::encodeName(second.absoluteFilePath());
    return ::stat(firstName.constData(), &firstStatus) == 0
        && ::stat(secondName.constData(), &secondStatus) == 0
        && firstStatus.st_dev == secondStatus.st_dev
        && firstStatus.st_ino == secondStatus.st_ino;
}

bool VDQtSourceSafety::isScriptPath(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("avs") || suffix == QStringLiteral("avsi")
        || suffix == QStringLiteral("vpy") || suffix == QStringLiteral("py")
        || suffix == QStringLiteral("ffconcat");
}

VDQtOutputSafetyReport VDQtSourceSafety::evaluateOutputPath(
    const QString& outputPath,
    const QStringList& directlyLoadedSources,
    const QString& scriptPath) {
    VDQtOutputSafetyReport result;
    QStringList protectedSources = directlyLoadedSources;
    QStringList scriptPaths;
    if (!scriptPath.isEmpty() && isScriptPath(scriptPath))
        scriptPaths.append(scriptPath);
    for (const QString& sourcePath : directlyLoadedSources) {
        if (isScriptPath(sourcePath)) scriptPaths.append(sourcePath);
    }
    scriptPaths.removeDuplicates();
    // Script dependencies join directly loaded media in the protected set.
    // auditScriptDependencies is conservative: it reports whether every path
    // expression was statically resolvable rather than guessing dynamic values.
    if (!scriptPaths.isEmpty()) {
        result.scriptDependencies.complete = true;
        for (const QString& currentScript : scriptPaths) {
            const VDQtVideoDecoder::ScriptDependencyReport dependency =
                VDQtVideoDecoder::auditScriptDependencies(currentScript);
            result.scriptDependencies.complete =
                result.scriptDependencies.complete && dependency.complete;
            result.scriptDependencies.resolvedPaths.append(
                dependency.resolvedPaths);
            result.scriptDependencies.unresolvedPathLiterals.append(
                dependency.unresolvedPathLiterals);
            result.scriptDependencies.diagnostics.append(
                dependency.diagnostics);
            protectedSources.append(dependency.resolvedPaths);
        }
        result.scriptDependencies.resolvedPaths.removeDuplicates();
        result.scriptDependencies.unresolvedPathLiterals.removeDuplicates();
        result.scriptDependencies.diagnostics.removeDuplicates();
    }

    protectedSources.removeDuplicates();
    for (const QString& sourcePath : protectedSources) {
        if (pathsReferToSameFile(outputPath, sourcePath)) {
            result.issue = VDQtOutputSafetyIssue::AliasesLoadedSource;
            result.aliasedPath = sourcePath;
            return result;
        }
    }

    // An incomplete audit is not automatically fatal for a new destination.
    // Refuse replacement of an existing path, however, because that operation
    // could destroy a dynamically computed dependency before the script opens it.
    const QFileInfo output(outputPath);
    if (!scriptPaths.isEmpty()
        && !result.scriptDependencies.complete
        && (output.exists() || output.isSymLink())) {
        result.issue =
            VDQtOutputSafetyIssue::ExistingDestinationWithIncompleteScriptAudit;
    }
    return result;
}
