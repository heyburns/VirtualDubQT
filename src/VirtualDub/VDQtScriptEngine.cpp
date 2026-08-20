#include "VDQtScriptEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr qint64 kMaximumScriptBytes = qint64{4} * 1024 * 1024;
constexpr int kMaximumCommands = 100000;
constexpr int kMaximumArguments = 256;

void setError(QString *errorMessage, const QString& message) {
    if (errorMessage) *errorMessage = message;
}

bool decodeQuotedString(const QString& token, QString *value,
                        QString *errorMessage) {
    if (token.size() < 2 || token.front() != QLatin1Char('"')
        || token.back() != QLatin1Char('"')) {
        setError(errorMessage, QStringLiteral("A string argument is not terminated."));
        return false;
    }
    QString decoded;
    decoded.reserve(token.size() - 2);
    for (int index = 1; index + 1 < token.size(); ++index) {
        QChar character = token.at(index);
        if (character != QLatin1Char('\\')) {
            decoded += character;
            continue;
        }
        if (++index >= token.size() - 1) {
            setError(errorMessage, QStringLiteral("A string ends with an escape character."));
            return false;
        }
        const QChar escaped = token.at(index);
        if (escaped == QLatin1Char('n')) decoded += QLatin1Char('\n');
        else if (escaped == QLatin1Char('r')) decoded += QLatin1Char('\r');
        else if (escaped == QLatin1Char('t')) decoded += QLatin1Char('\t');
        else if (escaped == QLatin1Char('\\')) decoded += QLatin1Char('\\');
        else if (escaped == QLatin1Char('"')) decoded += QLatin1Char('"');
        else if (escaped == QLatin1Char('x')) {
            if (index + 2 >= token.size() - 1) {
                setError(errorMessage, QStringLiteral("A hexadecimal string escape is incomplete."));
                return false;
            }
            bool ok = false;
            const ushort code = token.mid(index + 1, 2).toUShort(&ok, 16);
            if (!ok) {
                setError(errorMessage, QStringLiteral("A hexadecimal string escape is invalid."));
                return false;
            }
            decoded += QChar(code);
            index += 2;
        } else if (escaped == QLatin1Char('u')) {
            if (index + 4 >= token.size() - 1) {
                setError(errorMessage, QStringLiteral("A Unicode string escape is incomplete."));
                return false;
            }
            bool ok = false;
            const ushort code = token.mid(index + 1, 4).toUShort(&ok, 16);
            if (!ok) {
                setError(errorMessage, QStringLiteral("A Unicode string escape is invalid."));
                return false;
            }
            decoded += QChar(code);
            index += 4;
        } else {
            // Sylia-generated paths commonly escape punctuation conservatively.
            decoded += escaped;
        }
    }
    if (value) *value = decoded;
    return true;
}

class ValueExpressionParser {
public:
    ValueExpressionParser(const QString& text,
                          const QMap<QString, QVariant>& variables,
                          QString *errorMessage)
        : mText(text), mVariables(variables), mErrorMessage(errorMessage) {}

    bool parse(QVariant *value) {
        skipSpace();
        if (atEnd()) return fail(QStringLiteral("An argument is empty."));
        if (!parseBitwiseOr(value)) return false;
        skipSpace();
        if (!atEnd()) {
            return fail(QStringLiteral("Unsupported argument expression near: %1")
                            .arg(mText.mid(mPosition).left(80)));
        }
        return true;
    }

private:
    bool fail(const QString& message) {
        setError(mErrorMessage, message);
        return false;
    }

    void skipSpace() {
        while (!atEnd() && mText.at(mPosition).isSpace()) ++mPosition;
    }

    bool atEnd() const { return mPosition >= mText.size(); }

    bool consume(const QString& token) {
        skipSpace();
        if (!mText.mid(mPosition).startsWith(token)) return false;
        mPosition += token.size();
        return true;
    }

    static bool isDouble(const QVariant& value) {
        return value.typeId() == QMetaType::Double;
    }

    bool numeric(const QVariant& value, double *real, qlonglong *integer,
                 bool *floating) {
        bool ok = false;
        if (isDouble(value)) {
            const double converted = value.toDouble(&ok);
            if (!ok || !std::isfinite(converted)) return false;
            if (real) *real = converted;
            if (integer) *integer = static_cast<qlonglong>(converted);
            if (floating) *floating = true;
            return true;
        }
        const qlonglong converted = value.toLongLong(&ok);
        if (!ok) return false;
        if (real) *real = static_cast<double>(converted);
        if (integer) *integer = converted;
        if (floating) *floating = false;
        return true;
    }

    bool applyArithmetic(const QVariant& left, const QVariant& right,
                         QChar operation, QVariant *result) {
        if (operation == QLatin1Char('+')
            && (left.typeId() == QMetaType::QString
                || right.typeId() == QMetaType::QString)) {
            *result = left.toString() + right.toString();
            return true;
        }
        double leftReal = 0.0, rightReal = 0.0;
        qlonglong leftInteger = 0, rightInteger = 0;
        bool leftFloating = false, rightFloating = false;
        if (!numeric(left, &leftReal, &leftInteger, &leftFloating)
            || !numeric(right, &rightReal, &rightInteger, &rightFloating)) {
            return fail(QStringLiteral("An arithmetic operator requires numeric values."));
        }
        if (operation == QLatin1Char('%')) {
            if (!rightInteger) return fail(QStringLiteral("Integer division by zero."));
            if (leftInteger == std::numeric_limits<qlonglong>::min()
                && rightInteger == -1) {
                return fail(QStringLiteral("Integer expression overflow."));
            }
            *result = leftInteger % rightInteger;
            return true;
        }
        if (operation == QLatin1Char('/')) {
            if (rightReal == 0.0) return fail(QStringLiteral("Division by zero."));
            *result = leftReal / rightReal;
            return true;
        }
        if (leftFloating || rightFloating) {
            if (operation == QLatin1Char('+')) *result = leftReal + rightReal;
            else if (operation == QLatin1Char('-')) *result = leftReal - rightReal;
            else *result = leftReal * rightReal;
        } else {
            const qlonglong minimum = std::numeric_limits<qlonglong>::min();
            const qlonglong maximum = std::numeric_limits<qlonglong>::max();
            qlonglong computed = 0;
            if (operation == QLatin1Char('+')) {
                if ((rightInteger > 0 && leftInteger > maximum - rightInteger)
                    || (rightInteger < 0
                        && leftInteger < minimum - rightInteger)) {
                    return fail(QStringLiteral("Integer expression overflow."));
                }
                computed = leftInteger + rightInteger;
            } else if (operation == QLatin1Char('-')) {
                if ((rightInteger < 0 && leftInteger > maximum + rightInteger)
                    || (rightInteger > 0
                        && leftInteger < minimum + rightInteger)) {
                    return fail(QStringLiteral("Integer expression overflow."));
                }
                computed = leftInteger - rightInteger;
            } else {
                if (leftInteger == -1 && rightInteger == minimum)
                    return fail(QStringLiteral("Integer expression overflow."));
                if (rightInteger == -1 && leftInteger == minimum)
                    return fail(QStringLiteral("Integer expression overflow."));
                if (leftInteger != 0 && rightInteger != 0) {
                    if (leftInteger > 0) {
                        if ((rightInteger > 0 && leftInteger > maximum / rightInteger)
                            || (rightInteger < 0
                                && rightInteger < minimum / leftInteger)) {
                            return fail(QStringLiteral("Integer expression overflow."));
                        }
                    } else if ((rightInteger > 0
                                && leftInteger < minimum / rightInteger)
                               || (rightInteger < 0
                                   && leftInteger < maximum / rightInteger)) {
                        return fail(QStringLiteral("Integer expression overflow."));
                    }
                }
                computed = leftInteger * rightInteger;
            }
            *result = computed;
        }
        return true;
    }

    bool parseBitwiseOr(QVariant *value) {
        if (!parseBitwiseXor(value)) return false;
        while (consume(QStringLiteral("|"))) {
            QVariant right;
            if (!parseBitwiseXor(&right)) return false;
            bool leftOk = false, rightOk = false;
            const qlonglong left = value->toLongLong(&leftOk);
            const qlonglong converted = right.toLongLong(&rightOk);
            if (!leftOk || !rightOk)
                return fail(QStringLiteral("A bitwise operator requires integers."));
            *value = left | converted;
        }
        return true;
    }

    bool parseBitwiseXor(QVariant *value) {
        if (!parseBitwiseAnd(value)) return false;
        while (consume(QStringLiteral("^"))) {
            QVariant right;
            if (!parseBitwiseAnd(&right)) return false;
            bool leftOk = false, rightOk = false;
            const qlonglong left = value->toLongLong(&leftOk);
            const qlonglong converted = right.toLongLong(&rightOk);
            if (!leftOk || !rightOk)
                return fail(QStringLiteral("A bitwise operator requires integers."));
            *value = left ^ converted;
        }
        return true;
    }

    bool parseBitwiseAnd(QVariant *value) {
        if (!parseShift(value)) return false;
        while (consume(QStringLiteral("&"))) {
            QVariant right;
            if (!parseShift(&right)) return false;
            bool leftOk = false, rightOk = false;
            const qlonglong left = value->toLongLong(&leftOk);
            const qlonglong converted = right.toLongLong(&rightOk);
            if (!leftOk || !rightOk)
                return fail(QStringLiteral("A bitwise operator requires integers."));
            *value = left & converted;
        }
        return true;
    }

    bool parseShift(QVariant *value) {
        if (!parseAdditive(value)) return false;
        for (;;) {
            const bool leftShift = consume(QStringLiteral("<<"));
            const bool rightShift = !leftShift && consume(QStringLiteral(">>"));
            if (!leftShift && !rightShift) return true;
            QVariant right;
            if (!parseAdditive(&right)) return false;
            bool leftOk = false, rightOk = false;
            const qlonglong left = value->toLongLong(&leftOk);
            const qlonglong count = right.toLongLong(&rightOk);
            if (!leftOk || !rightOk || count < 0 || count > 63)
                return fail(QStringLiteral("A shift operator requires integers and a count from 0 to 63."));
            const quint64 bits = static_cast<quint64>(left);
            *value = static_cast<qlonglong>(
                leftShift ? bits << count : bits >> count);
        }
    }

    bool parseAdditive(QVariant *value) {
        if (!parseMultiplicative(value)) return false;
        for (;;) {
            skipSpace();
            if (atEnd()) return true;
            const QChar operation = mText.at(mPosition);
            if (operation != QLatin1Char('+')
                && operation != QLatin1Char('-')) return true;
            ++mPosition;
            QVariant right;
            if (!parseMultiplicative(&right)
                || !applyArithmetic(*value, right, operation, value)) return false;
        }
    }

    bool parseMultiplicative(QVariant *value) {
        if (!parseUnary(value)) return false;
        for (;;) {
            skipSpace();
            if (atEnd()) return true;
            const QChar operation = mText.at(mPosition);
            if (operation != QLatin1Char('*')
                && operation != QLatin1Char('/')
                && operation != QLatin1Char('%')) return true;
            ++mPosition;
            QVariant right;
            if (!parseUnary(&right)
                || !applyArithmetic(*value, right, operation, value)) return false;
        }
    }

    bool parseUnary(QVariant *value) {
        skipSpace();
        if (consume(QStringLiteral("+"))) return parseUnary(value);
        if (consume(QStringLiteral("-"))) {
            if (!parseUnary(value)) return false;
            bool ok = false;
            if (isDouble(*value)) {
                const double converted = value->toDouble(&ok);
                if (ok) *value = -converted;
            } else {
                const qlonglong converted = value->toLongLong(&ok);
                if (ok && converted == std::numeric_limits<qlonglong>::min())
                    return fail(QStringLiteral("Integer expression overflow."));
                if (ok) *value = -converted;
            }
            return ok || fail(QStringLiteral("Unary minus requires a number."));
        }
        if (consume(QStringLiteral("~"))) {
            if (!parseUnary(value)) return false;
            bool ok = false;
            const qlonglong converted = value->toLongLong(&ok);
            if (!ok) return fail(QStringLiteral("Bitwise not requires an integer."));
            *value = ~converted;
            return true;
        }
        if (consume(QStringLiteral("!"))) {
            if (!parseUnary(value)) return false;
            *value = !value->toBool();
            return true;
        }
        return parsePrimary(value);
    }

    bool parsePrimary(QVariant *value) {
        skipSpace();
        if (atEnd()) return fail(QStringLiteral("An expression is incomplete."));
        if (consume(QStringLiteral("("))) {
            if (!parseBitwiseOr(value) || !consume(QStringLiteral(")")))
                return fail(QStringLiteral("An expression has an unmatched parenthesis."));
            return true;
        }
        if (mText.at(mPosition) == QLatin1Char('"')) {
            const int start = mPosition++;
            bool escaped = false;
            while (!atEnd()) {
                const QChar character = mText.at(mPosition++);
                if (escaped) escaped = false;
                else if (character == QLatin1Char('\\')) escaped = true;
                else if (character == QLatin1Char('"')) {
                    QString decoded;
                    if (!decodeQuotedString(
                            mText.mid(start, mPosition - start), &decoded,
                            mErrorMessage)) return false;
                    *value = decoded;
                    return true;
                }
            }
            return fail(QStringLiteral("A string argument is not terminated."));
        }
        const int start = mPosition;
        if (mText.at(mPosition).isLetter()
            || mText.at(mPosition) == QLatin1Char('_')) {
            ++mPosition;
            while (!atEnd()
                   && (mText.at(mPosition).isLetterOrNumber()
                       || mText.at(mPosition) == QLatin1Char('_'))) {
                ++mPosition;
            }
            const QString identifier = mText.mid(start, mPosition - start);
            if (identifier.compare(QStringLiteral("true"),
                                   Qt::CaseInsensitive) == 0) {
                *value = true;
            } else if (identifier.compare(QStringLiteral("false"),
                                          Qt::CaseInsensitive) == 0) {
                *value = false;
            } else if (identifier.compare(QStringLiteral("null"),
                                          Qt::CaseInsensitive) == 0) {
                *value = QVariant();
            } else if (mVariables.contains(identifier)) {
                *value = mVariables.value(identifier);
            } else {
                return fail(QStringLiteral("Unknown script variable: %1")
                                .arg(identifier));
            }
            return true;
        }
        static const QRegularExpression numberExpression(
            QStringLiteral("^(?:0[xX][0-9A-Fa-f]+|(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)"));
        const QRegularExpressionMatch numberMatch =
            numberExpression.match(mText.mid(mPosition));
        if (!numberMatch.hasMatch()) {
            return fail(QStringLiteral("Unsupported argument expression: %1")
                            .arg(mText.mid(mPosition).left(80)));
        }
        const QString token = numberMatch.captured(0);
        mPosition += token.size();
        bool integerOk = false;
        const qlonglong integer = token.toLongLong(&integerOk, 0);
        if (integerOk) {
            *value = integer;
            return true;
        }
        bool doubleOk = false;
        const double real = token.toDouble(&doubleOk);
        if (doubleOk && std::isfinite(real)) {
            *value = real;
            return true;
        }
        return fail(QStringLiteral("Unsupported argument expression: %1")
                        .arg(token));
    }

    const QString mText;
    const QMap<QString, QVariant>& mVariables;
    QString *mErrorMessage = nullptr;
    int mPosition = 0;
};

bool parseValue(const QString& text,
                const QMap<QString, QVariant>& variables,
                QVariant *value, QString *errorMessage) {
    ValueExpressionParser parser(text.trimmed(), variables, errorMessage);
    return parser.parse(value);
}

bool parseArguments(const QString& text,
                    const QMap<QString, QVariant>& variables,
                    QList<QVariant> *arguments,
                    QString *errorMessage) {
    if (text.trimmed().isEmpty()) return true;
    QString current;
    bool quoted = false;
    bool escaped = false;
    int nested = 0;
    const auto finish = [&]() {
        QVariant value;
        if (!parseValue(current, variables, &value, errorMessage)) return false;
        arguments->append(value);
        current.clear();
        return arguments->size() <= kMaximumArguments;
    };
    for (const QChar character : text) {
        if (quoted) {
            current += character;
            if (escaped) escaped = false;
            else if (character == QLatin1Char('\\')) escaped = true;
            else if (character == QLatin1Char('"')) quoted = false;
            continue;
        }
        if (character == QLatin1Char('"')) {
            quoted = true;
            current += character;
        } else if (character == QLatin1Char('(')
                   || character == QLatin1Char('[')
                   || character == QLatin1Char('{')) {
            ++nested;
            current += character;
        } else if (character == QLatin1Char(')')
                   || character == QLatin1Char(']')
                   || character == QLatin1Char('}')) {
            --nested;
            current += character;
        } else if (character == QLatin1Char(',') && nested == 0) {
            if (!finish()) return false;
        } else {
            current += character;
        }
    }
    if (quoted || nested != 0) {
        setError(errorMessage, QStringLiteral("An argument list is not balanced."));
        return false;
    }
    return finish();
}

struct Statement {
    QString text;
    int line = 0;
};

bool splitStatements(const QString& text, QList<Statement> *statements,
                     QString *errorMessage) {
    QString current;
    bool quoted = false;
    bool escaped = false;
    bool lineComment = false;
    int line = 1;
    int statementLine = 1;
    int nested = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar character = text.at(index);
        if (lineComment) {
            if (character == QLatin1Char('\n')) {
                lineComment = false;
                ++line;
            }
            continue;
        }
        if (quoted) {
            current += character;
            if (escaped) escaped = false;
            else if (character == QLatin1Char('\\')) escaped = true;
            else if (character == QLatin1Char('"')) quoted = false;
            if (character == QLatin1Char('\n')) ++line;
            continue;
        }
        if (character == QLatin1Char('/') && index + 1 < text.size()
            && text.at(index + 1) == QLatin1Char('/')) {
            lineComment = true;
            ++index;
            continue;
        }
        if (character == QLatin1Char('"')) {
            quoted = true;
            current += character;
        } else if (character == QLatin1Char('(')) {
            ++nested;
            current += character;
        } else if (character == QLatin1Char(')')) {
            --nested;
            if (nested < 0) {
                setError(errorMessage, QStringLiteral("Line %1 has an unmatched parenthesis.").arg(line));
                return false;
            }
            current += character;
        } else if (character == QLatin1Char(';') && nested == 0) {
            const QString statement = current.trimmed();
            if (!statement.isEmpty()) statements->append({statement, statementLine});
            current.clear();
            statementLine = line;
            if (statements->size() > kMaximumCommands) {
                setError(errorMessage, QStringLiteral("The script contains too many commands."));
                return false;
            }
        } else {
            if (current.isEmpty() && !character.isSpace()) statementLine = line;
            current += character;
            if (character == QLatin1Char('\n')) ++line;
        }
    }
    if (quoted || nested != 0) {
        setError(errorMessage, QStringLiteral("The script ends inside a string or argument list."));
        return false;
    }
    if (!current.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("Line %1 is missing a semicolon.").arg(statementLine));
        return false;
    }
    return true;
}

} // namespace

bool VDQtScriptEngine::parseFile(const QString& path,
                                 VDQtScriptProgram *program,
                                 QString *errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, file.errorString());
        return false;
    }
    if (file.size() < 0 || file.size() > kMaximumScriptBytes) {
        setError(errorMessage, QStringLiteral("The script is too large."));
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.contains('\0')) {
        setError(errorMessage, QStringLiteral("The script contains binary data."));
        return false;
    }
    return parseText(QString::fromUtf8(bytes), QFileInfo(path).absolutePath(),
                     program, errorMessage);
}

bool VDQtScriptEngine::parseText(const QString& text,
                                 const QString& baseDirectory,
                                 VDQtScriptProgram *program,
                                 QString *errorMessage) {
    if (!program) {
        setError(errorMessage, QStringLiteral("No script destination was provided."));
        return false;
    }
    QList<Statement> statements;
    if (!splitStatements(text, &statements, errorMessage)) return false;
    VDQtScriptProgram parsed;
    parsed.baseDirectory = QFileInfo(baseDirectory).absoluteFilePath();
    // VirtualDub-generated project scripts use a local variable for opacity
    // curves.  We do not execute arbitrary Sylia expressions, but retaining
    // this one object alias lets the command-oriented interpreter consume the
    // exact project form emitted by Job.cpp.
    QMap<QString, QString> objectAliases;
    QMap<QString, QVariant> scalarVariables;
    int videoFilterCount = 0;
    int audioFilterCount = 0;
    static const QRegularExpression declarationExpression(
        QStringLiteral("^declare\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(.+)$"));
    static const QRegularExpression assignmentExpression(
        QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*(.+)$"));
    static const QRegularExpression aliasCallExpression(
        QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\s*\\((.*)\\)$"),
        QRegularExpression::DotMatchesEverythingOption);
    for (const Statement& statement : statements) {
        QString expressionText = statement.text.trimmed();
        QString declaredAlias;
        const QRegularExpressionMatch declarationMatch =
            declarationExpression.match(expressionText);
        if (declarationMatch.hasMatch()) {
            declaredAlias = declarationMatch.captured(1);
            expressionText = declarationMatch.captured(2).trimmed();
        }

        const int root = expressionText.indexOf(QStringLiteral("VirtualDub."));
        if (root < 0) {
            const QRegularExpressionMatch aliasMatch =
                aliasCallExpression.match(expressionText);
            if (aliasMatch.hasMatch()
                && objectAliases.contains(aliasMatch.captured(1))) {
                const QString method = aliasMatch.captured(2);
                if (method != QStringLiteral("AddPoint")) {
                    setError(errorMessage,
                             QStringLiteral("Line %1 calls an unsupported object method: %2")
                                 .arg(statement.line).arg(method));
                    return false;
                }
                VDQtScriptCommand command;
                command.name = objectAliases.value(aliasMatch.captured(1))
                    + QStringLiteral(".OpacityCurve.AddPoint");
                command.line = statement.line;
                command.sourceText = statement.text;
                if (!parseArguments(aliasMatch.captured(3), scalarVariables,
                                    &command.arguments,
                                    errorMessage)) {
                    if (errorMessage
                        && !errorMessage->startsWith(QStringLiteral("Line "))) {
                        *errorMessage = QStringLiteral("Line %1: %2")
                            .arg(statement.line).arg(*errorMessage);
                    }
                    return false;
                }
                parsed.commands.append(command);
                continue;
            }
            QString scalarName = declaredAlias;
            QString scalarExpression = expressionText;
            if (scalarName.isEmpty()) {
                const QRegularExpressionMatch assignmentMatch =
                    assignmentExpression.match(expressionText);
                if (assignmentMatch.hasMatch()
                    && scalarVariables.contains(assignmentMatch.captured(1))) {
                    scalarName = assignmentMatch.captured(1);
                    scalarExpression = assignmentMatch.captured(2);
                }
            }
            if (!scalarName.isEmpty()) {
                QVariant value;
                if (!parseValue(scalarExpression, scalarVariables, &value,
                                errorMessage)) {
                    if (errorMessage
                        && !errorMessage->startsWith(QStringLiteral("Line "))) {
                        *errorMessage = QStringLiteral("Line %1: %2")
                            .arg(statement.line).arg(*errorMessage);
                    }
                    return false;
                }
                scalarVariables.insert(scalarName, value);
                continue;
            }
            setError(errorMessage,
                     QStringLiteral("Line %1 is not a VirtualDub command: %2")
                         .arg(statement.line).arg(statement.text.left(160)));
            return false;
        }
        if (root != 0) {
            setError(errorMessage,
                     QStringLiteral("Line %1 contains an unsupported expression before the VirtualDub command.")
                         .arg(statement.line));
            return false;
        }
        const int open = expressionText.indexOf(QLatin1Char('('), root);
        const int close = expressionText.lastIndexOf(QLatin1Char(')'));
        if (open < 0 || close < open
            || !expressionText.mid(close + 1).trimmed().isEmpty()) {
            setError(errorMessage,
                     QStringLiteral("Line %1 is not a supported function call.")
                         .arg(statement.line));
            return false;
        }
        VDQtScriptCommand command;
        command.name = expressionText.mid(root + 11, open - root - 11).trimmed();
        static const QRegularExpression indexedVariableExpression(
            QStringLiteral("\\[([A-Za-z_][A-Za-z0-9_]*)\\]"));
        for (;;) {
            const QRegularExpressionMatch indexedMatch =
                indexedVariableExpression.match(command.name);
            if (!indexedMatch.hasMatch()) break;
            const QString variableName = indexedMatch.captured(1);
            if (!scalarVariables.contains(variableName)) {
                setError(errorMessage,
                         QStringLiteral("Line %1 references an unknown filter index variable: %2")
                             .arg(statement.line).arg(variableName));
                return false;
            }
            bool indexOk = false;
            const qlonglong indexValue =
                scalarVariables.value(variableName).toLongLong(&indexOk);
            if (!indexOk || indexValue < 0) {
                setError(errorMessage,
                         QStringLiteral("Line %1 uses a non-integer filter index variable: %2")
                             .arg(statement.line).arg(variableName));
                return false;
            }
            command.name.replace(indexedMatch.capturedStart(0),
                                 indexedMatch.capturedLength(0),
                                 QStringLiteral("[%1]").arg(indexValue));
        }
        command.line = statement.line;
        command.sourceText = statement.text;
        if (command.name.isEmpty()
            || !parseArguments(expressionText.mid(open + 1, close - open - 1),
                               scalarVariables, &command.arguments,
                               errorMessage)) {
            if (errorMessage && !errorMessage->startsWith(QStringLiteral("Line ")))
                *errorMessage = QStringLiteral("Line %1: %2")
                    .arg(statement.line).arg(*errorMessage);
            return false;
        }
        if (!declaredAlias.isEmpty()) {
            if (command.name.endsWith(QStringLiteral(".AddOpacityCurve"))) {
                objectAliases.insert(
                    declaredAlias,
                    command.name.left(command.name.size()
                                      - QStringLiteral(".AddOpacityCurve").size()));
            } else if (command.name == QStringLiteral("video.filters.Add")) {
                scalarVariables.insert(declaredAlias, videoFilterCount);
            } else if (command.name == QStringLiteral("audio.filters.Add")) {
                scalarVariables.insert(declaredAlias, audioFilterCount);
            } else {
                setError(errorMessage,
                         QStringLiteral("Line %1 assigns an unsupported VirtualDub return value.")
                             .arg(statement.line));
                return false;
            }
        }
        parsed.commands.append(command);
        if (command.name == QStringLiteral("video.filters.Clear")) {
            videoFilterCount = 0;
        } else if (command.name == QStringLiteral("video.filters.Add")) {
            ++videoFilterCount;
        } else if (command.name.endsWith(QStringLiteral(".Remove"))
                   && command.name.startsWith(
                       QStringLiteral("video.filters.instance["))) {
            videoFilterCount = std::max(0, videoFilterCount - 1);
        } else if (command.name == QStringLiteral("audio.filters.Clear")) {
            audioFilterCount = 0;
        } else if (command.name == QStringLiteral("audio.filters.Add")) {
            ++audioFilterCount;
        }
    }
    *program = parsed;
    if (errorMessage) errorMessage->clear();
    return true;
}
