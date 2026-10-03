// Source safety regressions use only disposable local files. Timing is reported
// as an observation; deterministic read counters establish the scaling contract.
#include "VirtualDub/VDQtSourceSafety.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>
#include <unistd.h>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool write(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
QByteArray concat(const QByteArray& name) { return "ffconcat version 1.0\nfile " + name + '\n'; }

bool cases() {
    QTemporaryDir directory;
    const auto path = [&](const QString& name) { return directory.filePath(name); };
    const QString media = path("source.nut"), unrelated = path("existing-output");
    if (!write(media, "synthetic media") || !write(unrelated, "sentinel")) return false;
    const QString misnamed = path("list.txt"), disguised = path("looks-like-video.avi");
    write(misnamed, concat("source.nut")); write(disguised, concat("list.txt"));
    auto audit = VDQtSourceDependencies::audit({disguised});
    if (!check(audit.complete && audit.resolvedPaths.contains(media) && audit.resolvedPaths.contains(misnamed),
               "content detects arbitrary extensions and nested concat dependencies")
        || !check(!VDQtSourceSafety::evaluateOutputPath(media, {disguised}).isSafe(),
                  "nested/misnamed manifest media is protected")) return false;
    const QString misleadingScript = path("manifest.avs"), misleadingParent = path("parent.txt");
    write(misleadingScript, concat("source.nut")); write(misleadingParent, concat("manifest.avs"));
    audit = VDQtSourceDependencies::audit({misleadingParent});
    if (!check(audit.complete && audit.resolvedPaths.contains(media),
               "concat content takes precedence over a misleading script extension")) return false;
    QDir().mkpath(path("left")); QDir().mkpath(path("right"));
    const QString shared = path("shared.txt"), leftMedia = path("left/leaf.nut"), rightMedia = path("right/leaf.nut");
    write(shared, concat("leaf.nut")); write(leftMedia, "left fixture"); write(rightMedia, "right fixture");
    if (!QFile::link(shared, path("left/list.txt")) || !QFile::link(shared, path("right/list.txt"))) return false;
    const QString aliases = path("aliases.ffconcat");
    write(aliases, concat("left/list.txt") + "file right/list.txt\n");
    audit = VDQtSourceDependencies::audit({aliases});
    if (!check(audit.complete && audit.resolvedPaths.contains(leftMedia) && audit.resolvedPaths.contains(rightMedia),
               "the same manifest through different directories keeps both relative dependency contexts")) return false;
    const QString hardlink = path("source-hardlink");
    if (::link(QFile::encodeName(media).constData(), QFile::encodeName(hardlink).constData()) != 0) return false;
    const QString symlink = path("source-symlink");
    if (!QFile::link(media, symlink)) return false;
    auto snapshot = VDQtSourceSafety::captureSources({disguised, misnamed, media, disguised});
    if (!check(!snapshot.evaluateOutputPath(hardlink).isSafe() && !snapshot.evaluateOutputPath(symlink).isSafe(),
               "cached inode checks protect hardlink and symlink aliases")
        || !check(snapshot.evaluateOutputPath(unrelated).isSafe(), "complete audit permits unrelated replacement")) return false;

    QDir().mkpath(path("physical/deep")); QDir().mkpath(path("lexical"));
    const QString physical = path("physical/through-symlink.nut");
    write(physical, "physical source"); write(path("lexical/through-symlink.nut"), "different source");
    if (!QFile::link(path("physical/deep"), path("lexical/link"))) return false;
    const QString symlinkScript = path("symlink-path.avs");
    write(symlinkScript, "AVISource(\"lexical/link/../through-symlink.nut\")\n");
    if (!check(!VDQtSourceSafety::evaluateOutputPath(physical, {symlinkScript}).isSafe(),
               "parent traversal through a symlink is resolved by the filesystem, not lexically")) return false;
    write(symlinkScript, "ImageSource(\"lexical/link/../through-*.nut\")\n");
    if (!check(!VDQtSourceSafety::evaluateOutputPath(physical, {symlinkScript}).isSafe(),
               "pattern directories also preserve physical symlink/../ traversal")) return false;

    const QString repeated = path("repeat.ffconcat");
    write(repeated, concat("source.nut") + "file source.nut\n");
    audit = VDQtSourceDependencies::audit({repeated});
    if (!check(audit.complete && audit.resolvedPaths.size() == 1
               && audit.topLevelConcatPaths == QStringList{media, media},
               "timeline media order/repetition is separate from deduplicated safety dependencies")) return false;

    // These spellings follow av_get_token's concat grammar, not shell/Python
    // quoting: backslashes are literal inside single quotes, and double quotes
    // are ordinary filename characters.
    const QString backslash = path("back\\slash.nut"), quotes = path("\"double\".nut"), apostrophe = path("apostrophe's media.nut");
    write(backslash, "fixture"); write(quotes, "fixture"); write(apostrophe, "fixture");
    const QString quoted = path("quoted.ffconcat");
    write(quoted, "ffconcat version 1.0\nfile 'back\\slash.nut'\nfile \"double\".nut\nfile 'apostrophe'\\''s media.nut'\n");
    audit = VDQtSourceDependencies::audit({quoted});
    if (!check(audit.complete && audit.resolvedPaths.contains(backslash)
               && audit.resolvedPaths.contains(quotes) && audit.resolvedPaths.contains(apostrophe),
               "concat quote/escape grammar preserves exact Linux filenames")) return false;

    const QString script = path("import.avs"), imported = path("helpers.data");
    write(script, "Import(\"helpers.data\")\n");
    write(imported, "AVISource(\"list.txt\")\n");
    audit = VDQtSourceDependencies::audit({script});
    if (!check(audit.complete && audit.resolvedPaths.contains(media),
               "imports without conventional script extensions recurse through manifests")) return false;
    const QString secondImport = path("second)helper.data");
    write(secondImport, "AVISource(\"next-import.nut\")\n");
    const QString nextImport = path("next-import.nut");
    write(nextImport, "fixture");
    write(script, "Import(file=\"helpers.data\",\"second)helper.data\",utf8=true)\n");
    audit = VDQtSourceDependencies::audit({script});
    if (!check(audit.complete && audit.resolvedPaths.contains(media) && audit.resolvedPaths.contains(nextImport),
               "named/multiple imports and parentheses in filenames are fully followed")) return false;
    write(path("helper"), "");
    write(script, "Import(\"helper\"+\".data\")\n");
    if (!check(!VDQtSourceDependencies::audit({script}).complete,
               "an import argument beginning with a literal is not necessarily a literal path")) return false;
    write(script, "other=\"second)helper.data\"\nImport(\"helpers.data\",other)\n");
    if (!check(!VDQtSourceDependencies::audit({script}).complete,
               "a later variable import argument cannot claim complete protection")) return false;
    const QString python = path("modules.vpy");
    write(python, "from hidden_module import clip\n");
    if (!check(!VDQtSourceDependencies::audit({python}).complete,
               "unresolved Python module dependencies stay conservative")) return false;

    const QString cycleA = path("cycle-a.ffconcat"), cycleB = path("cycle-b.data");
    write(cycleA, concat("cycle-b.data")); write(cycleB, concat("cycle-a.ffconcat"));
    audit = VDQtSourceDependencies::audit({cycleA});
    if (!check(!audit.complete && audit.filesRead < 10
               && !VDQtSourceSafety::evaluateOutputPath(unrelated, {cycleA}).isSafe(),
               "cycles terminate and keep replacement conservative")) return false;
    for (int index = 0; index < 35; ++index) {
        write(path(QString("deep-%1.txt").arg(index)), concat(index == 34
            ? QByteArray("source.nut") : QString("deep-%1.txt").arg(index + 1).toUtf8()));
    }
    if (!check(!VDQtSourceDependencies::audit({path("deep-0.txt")}).complete, "nesting limit is enforced")) return false;

    const QString missing = path("missing.ffconcat"), malformed = path("malformed.ffconcat"), remote = path("remote.ffconcat");
    write(missing, concat("not-present.nut"));
    write(malformed, concat("'source.nut"));
    write(remote, concat("https://example.invalid/media.nut"));
    for (const QString& input : {missing, malformed, remote}) {
        if (!check(!VDQtSourceDependencies::audit({input}).complete
                   && !VDQtSourceSafety::evaluateOutputPath(unrelated, {input}).isSafe()
                   && VDQtSourceSafety::evaluateOutputPath(path("new-output"), {input}).isSafe(),
                   "unresolved/malformed/remote inputs reject replacement but allow a new destination")) return false;
    }
    const QString dynamic = path("dynamic.avs");
    write(dynamic, "name=\"source.nut\"\nAVISource(name)\n");
    if (!check(!VDQtSourceDependencies::audit({dynamic}).complete, "dynamic scripts remain incomplete")) return false;
    const QString unsupported = path("playlist.m3u8");
    write(unsupported, "#EXTM3U\nsource.nut\n");
    if (!check(!VDQtSourceSafety::evaluateOutputPath(unrelated, {unsupported}).isSafe(),
               "unsupported dependency-bearing lists cannot claim complete protection")) return false;
    const QString excessive = path("large.avs");
    write(excessive, QByteArray(8 * 1024 * 1024 + 1, '#'));
    audit = VDQtSourceDependencies::audit({excessive});
    if (!check(!audit.complete && audit.bytesRead <= 32, "oversize dependency documents are rejected before full read")) return false;
    const QString binary = path("binary.avs");
    QByteArray binaryScript("AVISource(\"source.nut\")\n");
    binaryScript.append('\0');
    write(binary, binaryScript);
    if (!check(!VDQtSourceDependencies::audit({binary}).complete,
               "binary dependency text cannot claim a complete audit")) return false;
    const QString many = path("many.ffconcat");
    QByteArray manyFiles("ffconcat version 1.0\n");
    for (int index = 0; index < 100001; ++index) manyFiles += "file source.nut\n";
    write(many, manyFiles);
    if (!check(!VDQtSourceDependencies::audit({many}).complete, "repeated references also count toward the work limit")) return false;

    // Refresh protects both the clip already in use and newly referenced media.
    const QString changed = path("changed.txt"), next = path("next.nut");
    write(changed, concat("source.nut")); write(next, "next fixture");
    snapshot = VDQtSourceSafety::captureSources({changed});
    const quint64 originalReads = snapshot.dependencyReport().filesRead;
    write(changed, concat("next.nut"));
    snapshot.refresh();
    if (!check(!snapshot.evaluateOutputPath(media).isSafe() && !snapshot.evaluateOutputPath(next).isSafe()
               && snapshot.dependencyReport().filesRead > originalReads,
               "precommit refresh retains old and discovers new dependencies")) return false;
    // Even replacing a source inode must not forget the original open file.
    const QString held = path("held-original");
    snapshot = VDQtSourceSafety::captureSources({media});
    if (!QFile::rename(media, held) || !write(media, "new source inode")) return false;
    snapshot.refresh();
    if (!check(!snapshot.evaluateOutputPath(held).isSafe() && !snapshot.evaluateOutputPath(media).isSafe(),
               "refresh retains original open-file identity")) return false;
    write(changed, "no longer a manifest");
    snapshot = VDQtSourceSafety::captureSources({changed}, {}, {changed});
    return check(!snapshot.dependencyReport().complete && !snapshot.evaluateOutputPath(unrelated).isSafe(),
                 "known concat demuxer information overrides a changed/missing header");
}

bool scaling() {
    for (int count : {1000, 8000}) {
        QTemporaryDir directory;
        for (int index = 0; index < count; ++index)
            if (!write(directory.filePath(QString("frame%1.png").arg(index, 5, 10, QLatin1Char('0'))), "fixture")) return false;
        const QString script = directory.filePath("images.avs");
        write(script, "ImageSource(\"frame%05d.png\")\n");
        QElapsedTimer timer; timer.start();
        auto snapshot = VDQtSourceSafety::captureSources({script, script});
        const qint64 captureUs = timer.nsecsElapsed() / 1000;
        const auto initial = snapshot.dependencyReport();
        timer.restart();
        for (int index = 0; index < 8000; ++index) {
            if (!snapshot.evaluateOutputPath(directory.filePath(QString("output%1.png").arg(index))).isSafe()) return false;
        }
        const qint64 checksUs = timer.nsecsElapsed() / 1000;
        if (!check(initial.complete && initial.resolvedPaths.size() == count
                   && initial.filesRead == quint64(count + 2)
                   && snapshot.dependencyReport().filesRead == initial.filesRead,
                   "thousands of destination checks perform no additional source reads")) return false;
        snapshot.refresh();
        if (!check(snapshot.dependencyReport().filesRead == 2 * initial.filesRead,
                   "one precommit refresh performs exactly one more dependency walk")) return false;
        std::cout << count << " sources: capture " << captureUs << " us, 8000 destination checks "
                  << checksUs << " us, reads " << initial.filesRead << '\n';
    }
    return true;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool passed = app.arguments().contains("--scaling-test") ? scaling() : cases();
    if (passed) std::cout << "Source safety checks passed\n";
    return passed ? 0 : 1;
}
