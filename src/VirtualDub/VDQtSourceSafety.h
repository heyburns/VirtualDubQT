#ifndef VDQTSAFETYSOURCES_H
#define VDQTSAFETYSOURCES_H

#include "VDQtVideoDecoder.h"

#include <QString>
#include <QStringList>

enum class VDQtOutputSafetyIssue {
    None,
    AliasesLoadedSource,
    ExistingDestinationWithIncompleteScriptAudit
};

struct VDQtOutputSafetyReport {
    VDQtOutputSafetyIssue issue = VDQtOutputSafetyIssue::None;
    QString aliasedPath;
    VDQtVideoDecoder::ScriptDependencyReport scriptDependencies;

    bool isSafe() const { return issue == VDQtOutputSafetyIssue::None; }
};

class VDQtSourceSafety {
public:
    static bool pathsReferToSameFile(const QString& firstPath, const QString& secondPath);
    static bool isScriptPath(const QString& path);
    static VDQtOutputSafetyReport evaluateOutputPath(
        const QString& outputPath,
        const QStringList& directlyLoadedSources,
        const QString& scriptPath = QString());
};

#endif
