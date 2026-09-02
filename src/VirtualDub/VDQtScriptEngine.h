#ifndef VDQTSCRIPTENGINE_H
#define VDQTSCRIPTENGINE_H

#include <QList>
#include <QString>
#include <QVariant>

struct VDQtScriptCommand {
    QString name;               // Normalized dotted command name.
    QList<QVariant> arguments;  // Strings, numbers, booleans, or null.
    int line = 0;               // One-based source line for diagnostics.
    QString sourceText;         // Original statement shown on failure.
};

struct VDQtScriptProgram {
    QList<VDQtScriptCommand> commands;
    QString baseDirectory;
};

// Parser for the command-oriented subset of Sylia used by VirtualDub project
// and job scripts. It intentionally rejects general-purpose expressions
// instead of evaluating arbitrary code. Parsing and execution are separate:
// VDQtMainWindow interprets the resulting commands against application state.
class VDQtScriptEngine {
public:
    static bool parseFile(const QString& path,
                          VDQtScriptProgram *program,
                          QString *errorMessage = nullptr);
    static bool parseText(const QString& text,
                          const QString& baseDirectory,
                          VDQtScriptProgram *program,
                          QString *errorMessage = nullptr);
};

#endif // VDQTSCRIPTENGINE_H
