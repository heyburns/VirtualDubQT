#ifndef VDQTSAFETYSOURCES_H
#define VDQTSAFETYSOURCES_H

#include "VDQtSourceDependencies.h"

#include <QString>
#include <QStringList>
#include <memory>

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
    VDQtScriptDependencyReport scriptDependencies;

    bool isSafe() const { return issue == VDQtOutputSafetyIssue::None; }
};

// Immutable source/path/inode sets for one operation. Output checks inspect
// only the destination; refresh() re-audits once before commit and conservatively
// retains BOTH the original and newly discovered dependency identities.
class VDQtSourceSafetySnapshot {
public:
    VDQtOutputSafetyReport evaluateOutputPath(const QString& outputPath) const;
    void refresh();
    VDQtScriptDependencyReport dependencyReport() const;
private:
    friend class VDQtSourceSafety;
    struct Data;
    std::shared_ptr<const Data> mData;
};

// Last line of defense against overwriting input media. Lexical path comparison
// is insufficient because symlinks/hard links may name the same inode, and a
// script or concat manifest may reference additional sources. Existing
// destinations are rejected whenever dependency inspection is incomplete.
class VDQtSourceSafety {
public:
    static bool pathsReferToSameFile(const QString& firstPath, const QString& secondPath);
    static bool isScriptPath(const QString& path);
    static VDQtSourceSafetySnapshot captureSources(
        const QStringList& directlyLoadedSources,
        const QString& scriptPath = QString(),
        const QStringList& knownConcatSources = {});
    static VDQtOutputSafetyReport evaluateOutputPath(
        const QString& outputPath,
        const QStringList& directlyLoadedSources,
        const QString& scriptPath = QString());
};

#endif
