#include "VDQtSourceDependencies.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr qint64 kFileTextLimit = 8 * 1024 * 1024;
constexpr qint64 kTotalTextLimit = 32 * 1024 * 1024;
constexpr qsizetype kPathCountLimit = 100000;
constexpr qsizetype kPathBytesLimit = 32 * 1024 * 1024;
constexpr int kDepthLimit = 32;

bool openRegularFile(QFile& file, const QString& path) {
    // A path can change after QFileInfo's check. Nonblocking open + fstat
    // prevents a replaced FIFO/device from hanging a safety audit indefinitely.
    const QByteArray native = QFile::encodeName(path);
    const int descriptor = ::open(native.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) return false;
    struct stat status {};
    if (::fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode)
        && file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) return true;
    ::close(descriptor);
    return false;
}

VDQtSourceDependencies::Kind classifyFile(const QString& path, VDQtScriptDependencyReport *report) {
    using Kind = VDQtSourceDependencies::Kind;
    const QString suffix = QFileInfo(path).suffix().toLower();
    QFile file(path);
    if (!openRegularFile(file, path)) return Kind::Unreadable;
    const QByteArray header = file.read(32);
    if (report) { ++report->filesRead; report->bytesRead += header.size(); }
    if (file.error() != QFile::NoError) return Kind::Unreadable;
    if (header.startsWith("ffconcat version 1.0")) return Kind::Concat;
    if (header.startsWith("#EXTM3U") || header.startsWith("<?xml")) return Kind::UnsupportedList;
    if (suffix == "avs" || suffix == "avsi" || suffix == "vpy" || suffix == "py") return Kind::Script;
    if (suffix == "ffconcat") return Kind::Concat;
    if (suffix == "m3u" || suffix == "m3u8" || suffix == "mpd") return Kind::UnsupportedList;
    return Kind::Media;
}

bool looksLikePath(const QString& value) {
    static const QRegularExpression extension(
        R"(\.(?:avs|avsi|vpy|py|ffconcat|avi|mp4|m4v|mkv|mov|webm|nut|ts|m2ts|mpg|mpeg|vob|wav|flac|mp3|aac|m4a|ogg|opus|png|jpe?g|bmp|tiff?|webp)$)",
        QRegularExpression::CaseInsensitiveOption);
    return value.contains('/') || value.contains('\\') || value.contains('*')
        || value.contains('#') || value.contains('%') || extension.match(value).hasMatch();
}

// FFmpeg concat tokens use single quotes and backslash escaping OUTSIDE those
// quotes. Backslashes inside a quote are literal; double quotes are not quotes.
bool concatToken(QStringView line, qsizetype *position, QString *token) {
    while (*position < line.size() && line[*position].isSpace()) ++*position;
    token->clear();
    bool quoted = false, present = false;
    while (*position < line.size()) {
        const QChar c = line[(*position)++];
        if (!quoted && c.isSpace()) break;
        present = true;
        if (c == '\'') quoted = !quoted;
        else if (!quoted && c == '\\') {
            if (*position >= line.size()) return false;
            *token += line[(*position)++];
        } else *token += c;
    }
    return present && !quoted;
}

class Walker {
public:
    VDQtScriptDependencyReport report;
    QSet<QString> forcedConcat;
    Walker() { report.complete = true; }
    bool workLimitReached() const { return exhausted; }
    void forceConcat(const QString& path) {
        const QString absolute = VDQtSourceDependencies::absoluteLocalPath(path);
        if (track(absolute)) forcedConcat.insert(absolute);
    }

    void visit(const QString& path, int depth = 0, bool root = false, bool forcedScript = false) {
        if (exhausted) return;
        const QFileInfo info(path);
        const QString absolute = VDQtSourceDependencies::absoluteLocalPath(path);
        if (absolute.size() > 4096) { incomplete("A dependency path is too long."); return; }
        if (depth > kDepthLimit) { incomplete("Dependency nesting exceeds 32 levels."); return; }
        const QByteArray native = QFile::encodeName(absolute);
        char *resolvedName = ::realpath(native.constData(), nullptr);
        const QString canonical = resolvedName ? QFile::decodeName(resolvedName) : QString();
        std::free(resolvedName);
        const QString key = canonical.isEmpty() ? absolute : canonical;
        if (active.contains(key)) { incomplete(QString("Dependency cycle: %1").arg(absolute)); return; }
        // An alias in another directory may resolve relative child paths
        // differently. Deduplicate visits by spelling/context, but detect
        // recursion by physical identity so symlink cycles still terminate.
        if (visited.contains(absolute) && (!forcedScript || scripted.contains(absolute))) return;
        if (!track(absolute)) return;
        visited.insert(absolute);
        if (!info.isFile()) {
            unresolvedPath(absolute);
            incomplete(QString("A dependency is missing or not a regular file: %1").arg(absolute));
            return;
        }
        if (!root && !add(absolute, resolved, report.resolvedPaths)) return;
        const auto kind = forcedConcat.contains(absolute) ? VDQtSourceDependencies::Kind::Concat
            : forcedScript ? VDQtSourceDependencies::Kind::Script : classifyFile(absolute, &report);
        if (kind == VDQtSourceDependencies::Kind::Media) return;
        if (kind == VDQtSourceDependencies::Kind::Script) scripted.insert(absolute);
        if (kind == VDQtSourceDependencies::Kind::UnsupportedList || kind == VDQtSourceDependencies::Kind::Unreadable) {
            incomplete(QString("Dependency behavior cannot be inspected safely: %1").arg(absolute));
            return;
        }
        if (++documents > 1024) { incomplete("Too many dependency documents."); exhausted = true; return; }
        QFile file(absolute);
        if (!openRegularFile(file, absolute)) {
            unresolvedPath(absolute); incomplete(QString("Cannot read dependency document: %1").arg(absolute)); return;
        }
        const qint64 limit = std::min(kFileTextLimit, kTotalTextLimit - textBytes);
        if (limit <= 0 || file.size() > limit) {
            incomplete(QString("Dependency text exceeds the audit size limit: %1").arg(absolute));
            exhausted = true; return;
        }
        const QByteArray bytes = file.read(limit + 1);
        ++report.filesRead;
        report.bytesRead += bytes.size();
        textBytes += bytes.size();
        if (bytes.size() > limit || file.error() != QFile::NoError || bytes.contains('\0')) {
            incomplete(QString("Dependency text is too large, unreadable, or contains binary data: %1").arg(absolute)); return;
        }
        active.insert(key);
        const QString content = QString::fromUtf8(bytes);
        const QString directory = absolute.left(absolute.lastIndexOf('/'));
        if (kind == VDQtSourceDependencies::Kind::Concat) concat(content, directory, depth);
        else script(content, directory, depth);
        active.remove(key);
    }

private:
    QSet<QString> visited, scripted, active, resolved, unresolved, diagnostics, tracked;
    qsizetype pathBytes = 0;
    qint64 textBytes = 0;
    int documents = 0;
    int references = 0;
    bool exhausted = false;

    void incomplete(const QString& message) {
        report.complete = false;
        // Diagnostics are bounded independently from input/path count.
        if (diagnostics.size() < 128 && !diagnostics.contains(message)) {
            diagnostics.insert(message); report.diagnostics.append(message);
        }
    }
    bool track(const QString& path) {
        if (tracked.contains(path)) return true;
        if (tracked.size() >= kPathCountLimit || pathBytes > kPathBytesLimit - path.size() * 2) {
            incomplete("Dependency collection exceeds the audit work limit."); exhausted = true; return false;
        }
        tracked.insert(path); pathBytes += path.size() * 2;
        return true;
    }
    bool add(const QString& path, QSet<QString>& seen, QStringList& list) {
        if (!track(path)) return false;
        if (!seen.contains(path)) { seen.insert(path); list.append(path); }
        return true;
    }
    void unresolvedPath(const QString& path) { add(path, unresolved, report.unresolvedPathLiterals); report.complete = false; }

    void dependency(const QString& token, const QString& directory, int depth, bool forcedScript = false,
                    bool pattern = false, bool concatEntry = false) {
        if (exhausted) return;
        if (++references > kPathCountLimit) {
            incomplete("Too many dependency references."); exhausted = true; return;
        }
        QString path = token;
        if (path.isEmpty() || path.size() > 4096) { incomplete("Invalid dependency path."); return; }
        if (path.startsWith("file://", Qt::CaseInsensitive)) path.remove(0, 7);
        else if (path.startsWith("file:", Qt::CaseInsensitive)) path.remove(0, 5);
        static const QRegularExpression protocol(R"(^[A-Za-z][A-Za-z0-9+.-]*:)");
        if (protocol.match(path).hasMatch()) {
            unresolvedPath(token); incomplete("A dependency uses a runtime or remote input protocol."); return;
        }
        const QString absolute = QFileInfo(path).isAbsolute() ? path : directory + QLatin1Char('/') + path;
        if (concatEntry && depth == 0 && track(absolute)) report.topLevelConcatPaths.append(absolute);
        QString wildcard = absolute;
        if (pattern) {
            wildcard.replace(QRegularExpression(R"(%[-+0 #]*\d*(?:\.\d+)?[diu])"), "*");
            wildcard.replace(QRegularExpression("#+"), "*");
        }
        if (!pattern || (!wildcard.contains('*') && !wildcard.contains('?') && !wildcard.contains('['))) {
            visit(absolute, depth + 1, false, forcedScript);
            return;
        }
        const int separator = wildcard.lastIndexOf('/');
        const QString parent = wildcard.left(separator);
        const QString filename = wildcard.mid(separator + 1);
        if (parent.contains('*') || parent.contains('?') || parent.contains('[')) {
            unresolvedPath(absolute); incomplete("Wildcard directories cannot be resolved statically."); return;
        }
        // Stream directory entries and stop on the shared work limit; don't
        // allocate an unbounded QFileInfoList before checking that limit.
        // QDir cleans '..' lexically. Resolve the directory through the actual
        // filesystem first so symlink/../ patterns enumerate the right files.
        const QByteArray nativeParent = QFile::encodeName(parent);
        char *resolvedParent = ::realpath(nativeParent.constData(), nullptr);
        const QString physicalParent = resolvedParent ? QFile::decodeName(resolvedParent) : QString();
        std::free(resolvedParent);
        if (physicalParent.isEmpty()) { unresolvedPath(absolute); return; }
        QDirIterator entries(physicalParent, {filename}, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        bool found = false;
        while (entries.hasNext() && !exhausted) {
            found = true;
            visit(entries.next(), depth + 1, false, forcedScript);
        }
        if (!found) unresolvedPath(absolute);
    }

    void concat(const QString& content, const QString& directory, int depth) {
        static const QSet<QString> safeDirectives{
            "duration", "inpoint", "outpoint", "file_packet_meta", "file_packet_metadata",
            "stream", "exact_stream_id", "stream_codec", "stream_extradata", "stream_meta", "chapter"
        };
        static const QSet<QString> safeOptions{"framerate", "video_size", "pixel_format", "safe", "format_whitelist", "protocol_whitelist"};
        int files = 0;
        qsizetype start = 0;
        while (start < content.size() && !exhausted) {
            qsizetype end = content.indexOf('\n', start);
            if (end < 0) end = content.size();
            const QStringView line = QStringView(content).mid(start, end - start).trimmed();
            start = end + 1;
            if (line.isEmpty() || line.startsWith('#')) continue;
            qsizetype position = 0;
            QString directive, value;
            if (!concatToken(line, &position, &directive)) { incomplete("Malformed concat directive."); continue; }
            if (directive == "ffconcat") {
                if (line != u"ffconcat version 1.0") incomplete("Unsupported concat header.");
            } else if (directive == "file") {
                ++files;
                if (!concatToken(line, &position, &value) || !line.mid(position).trimmed().isEmpty())
                    incomplete("Malformed concat file entry.");
                if (!value.isEmpty()) dependency(value, directory, depth, false, false, true);
            } else if (directive == "option") {
                if (!concatToken(line, &position, &value) || !safeOptions.contains(value))
                    incomplete("A concat input option has unaudited dependency behavior.");
            } else if (!safeDirectives.contains(directive)) {
                incomplete(QString("Unknown concat directive: %1").arg(directive.left(160)));
            }
        }
        if (files == 0) incomplete("The concat manifest contains no file entries.");
    }

    void script(const QString& content, const QString& directory, int depth) {
        static const QRegularExpression literal(R"vdq("((?:\\.|[^"\\\r\n])*)"|'((?:\\.|[^'\\\r\n])*)')vdq");
        static const QRegularExpression imports(R"(\bImport\s*\()", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression literalArgument(
            R"vdq(^\s*(?:[A-Za-z_]\w*\s*=\s*)?(?:"(?:\\.|[^"\\\r\n])*"|'(?:\\.|[^'\\\r\n])*')\s*$)vdq");
        static const QRegularExpression importFlag(R"(^\s*utf8\s*=\s*(?:true|false|0|1)\s*$)",
                                                   QRegularExpression::CaseInsensitiveOption);
        QVector<QPair<qsizetype, qsizetype>> importRanges;
        auto importMatches = imports.globalMatch(content);
        while (importMatches.hasNext()) {
            const qsizetype start = importMatches.next().capturedEnd();
            // The enclosing Import already covers all its literals. Skip
            // nested matches rather than scanning overlapping suffixes again.
            if (!importRanges.isEmpty() && start <= importRanges.last().second) continue;
            if (importRanges.size() >= kPathCountLimit) {
                incomplete("Too many script imports."); exhausted = true; break;
            }
            qsizetype cursor = start, argumentStart = start;
            int nesting = 1;
            QChar quote;
            bool escaped = false;
            const auto checkArgument = [&](qsizetype end) {
                const QString argument = content.mid(argumentStart, end - argumentStart);
                if (!literalArgument.match(argument).hasMatch() && !importFlag.match(argument).hasMatch())
                    incomplete("An import path is supplied through a variable or expression.");
            };
            for (; cursor < content.size(); ++cursor) {
                const QChar c = content[cursor];
                if (escaped) { escaped = false; continue; }
                if (!quote.isNull()) {
                    if (c == '\\') escaped = true;
                    else if (c == quote) quote = {};
                } else if (c == '"' || c == '\'') quote = c;
                else if (c == '(') {
                    if (++nesting > kDepthLimit) { incomplete("Import expression nesting is too deep."); break; }
                } else if (c == ')' && --nesting == 0) { checkArgument(cursor); break; }
                else if (c == ',' && nesting == 1) { checkArgument(cursor); argumentStart = cursor + 1; }
            }
            if (cursor == content.size() || nesting != 0) incomplete("An import expression could not be parsed completely.");
            importRanges.append({start, cursor});
        }
        auto matches = literal.globalMatch(content);
        qsizetype importIndex = 0;
        while (matches.hasNext() && !exhausted) {
            const auto match = matches.next();
            QString path = match.captured(1).isNull() ? match.captured(2) : match.captured(1);
            path.replace("\\\\", "\\");
            path.replace('\\', '/');
            while (importIndex < importRanges.size() && match.capturedStart() > importRanges[importIndex].second) ++importIndex;
            const bool imported = importIndex < importRanges.size() && match.capturedStart() >= importRanges[importIndex].first;
            // Preserve the existing conservative collection of any existing
            // quoted file (assets/plugins too), not just known source calls.
            if (looksLikePath(path) || QFileInfo(directory + QLatin1Char('/') + path).isFile())
                dependency(path, directory, depth, imported, true);
        }
        static const QRegularExpression dynamic(
            R"((?:\+\s*[A-Za-z_]|[A-Za-z_]\s*\+|\b(?:eval|exec|getenv|environ|glob|format)\b|\$\{|\{[^}\r\n]*\}))",
            QRegularExpression::CaseInsensitiveOption);
        if (dynamic.match(content).hasMatch()) incomplete("The script contains runtime path construction.");
        static const QRegularExpression pythonImport(R"(^\s*(?:import\s|from\s))", QRegularExpression::MultilineOption);
        if (pythonImport.match(content).hasMatch()) incomplete("Python module search paths cannot be resolved statically.");
        static const QRegularExpression sources(
            R"(\b(?:AVISource|OpenDMLSource|DirectShowSource|FFVideoSource|FFAudioSource|LWLibavVideoSource|LWLibavAudioSource|ImageSource|ImageReader|Import|Source)\s*\(\s*(?:[A-Za-z_]\w*\s*=\s*)?([^,\)\r\n]+))",
            QRegularExpression::CaseInsensitiveOption);
        auto sourceMatches = sources.globalMatch(content);
        while (sourceMatches.hasNext()) {
            const QString argument = sourceMatches.next().captured(1).trimmed();
            if (!argument.startsWith('"') && !argument.startsWith('\''))
                incomplete("A source path is supplied through a variable or expression.");
        }
        static const QSet<QString> audited{"avisource", "opendmlsource", "directshowsource", "ffvideosource",
            "ffaudiosource", "lwlibavvideosource", "lwlibavaudiosource", "imagesource", "imagereader", "import", "source"};
        static const QRegularExpression calls(R"(\b([A-Za-z_]\w*)\s*\()");
        auto functionMatches = calls.globalMatch(content);
        while (functionMatches.hasNext()) {
            const QString name = functionMatches.next().captured(1).toLower();
            if (!audited.contains(name)) incomplete(QString("Dependency behavior of script function '%1' cannot be proven.").arg(name));
        }
    }
};
}

VDQtSourceDependencies::Kind VDQtSourceDependencies::classify(const QString& path) {
    return classifyFile(path, nullptr);
}

QString VDQtSourceDependencies::absoluteLocalPath(const QString& path) {
    return QFileInfo(path).isAbsolute() ? path : QDir::currentPath() + QLatin1Char('/') + path;
}

VDQtScriptDependencyReport VDQtSourceDependencies::audit(
    const QStringList& sources, const QStringList& knownConcatSources) {
    Walker walker;
    for (const QString& path : knownConcatSources) {
        walker.forceConcat(path);
        if (walker.workLimitReached()) break;
    }
    for (const QString& source : sources) {
        if (walker.workLimitReached()) break;
        if (!source.isEmpty()) walker.visit(source, 0, true);
    }
    return walker.report;
}
