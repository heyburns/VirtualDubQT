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
    if (!scriptPath.isEmpty() && isScriptPath(scriptPath)) {
        result.scriptDependencies =
            VDQtVideoDecoder::auditScriptDependencies(scriptPath);
        protectedSources.append(result.scriptDependencies.resolvedPaths);
    }

    protectedSources.removeDuplicates();
    for (const QString& sourcePath : protectedSources) {
        if (pathsReferToSameFile(outputPath, sourcePath)) {
            result.issue = VDQtOutputSafetyIssue::AliasesLoadedSource;
            result.aliasedPath = sourcePath;
            return result;
        }
    }

    const QFileInfo output(outputPath);
    if (!scriptPath.isEmpty() && isScriptPath(scriptPath)
        && !result.scriptDependencies.complete
        && (output.exists() || output.isSymLink())) {
        result.issue =
            VDQtOutputSafetyIssue::ExistingDestinationWithIncompleteScriptAudit;
    }
    return result;
}
