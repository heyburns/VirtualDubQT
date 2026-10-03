// Static dependency inspection, not script evaluation. Unknown/dynamic input
// behavior is deliberately incomplete so output replacement stays conservative.
#pragma once

#include <QStringList>

struct VDQtScriptDependencyReport {
    QStringList resolvedPaths;
    // Ordered immediate concat entries, including intentional repetitions.
    // Editing/project code must not mistake recursive safety dependencies for
    // the sequence of media segments the manifest actually concatenates.
    QStringList topLevelConcatPaths;
    QStringList unresolvedPathLiterals;
    QStringList diagnostics;
    bool complete = false;
    // Read-operation counters (header probes included), useful for verifying
    // bounded work without machine-dependent time limits.
    quint64 filesRead = 0;
    quint64 bytesRead = 0;
};

class VDQtSourceDependencies {
public:
    enum class Kind { Media, Script, Concat, UnsupportedList, Unreadable };
    // Concat's header, not its extension, identifies autodetected manifests.
    static Kind classify(const QString& path);
    // Preserve '..' until filesystem resolution; lexical collapsing across a
    // symlink can identify a different input file.
    static QString absoluteLocalPath(const QString& path);
    static VDQtScriptDependencyReport audit(
        const QStringList& sources, const QStringList& knownConcatSources = {});
};
