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

// Explains why an output path was accepted or rejected and carries dependency
// details suitable for a user-facing warning.
struct VDQtOutputSafetyReport {
    VDQtOutputSafetyIssue issue = VDQtOutputSafetyIssue::None;
    QString aliasedPath;
    VDQtVideoDecoder::ScriptDependencyReport scriptDependencies;

    bool isSafe() const { return issue == VDQtOutputSafetyIssue::None; }
};

// Last line of defense against overwriting input media. Lexical path comparison
// is insufficient because symlinks/hard links may name the same inode, and an
// AVS/VPY script may reference additional sources. Existing destinations are
// rejected conservatively whenever a script dependency audit is incomplete.
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
