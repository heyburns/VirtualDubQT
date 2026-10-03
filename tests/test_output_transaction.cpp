// Filesystem fault injection for output replacement/recovery. Every path is
// beneath a disposable fixture; no user media or user process is involved.
#include "VirtualDub/VDQtOutputTransaction.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool write(const QString& path, const QByteArray& data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
struct Fixture {
    QTemporaryDir directory;
    QTemporaryDir staging{directory.filePath(".stage-XXXXXX")};
    QStringList targets{directory.filePath("output.00.avi"), directory.filePath("output.01.avi")};
    QStringList stages{staging.filePath("a.avi"), staging.filePath("b.avi")};
    Fixture() { write(stages[0], "new0"); write(stages[1], "new1"); }
    void originals() { write(targets[0], "original0"); write(targets[1], "original1"); }
    bool originalsIntact() { return read(targets[0]) == "original0" && read(targets[1]) == "original1"; }
};
class FaultTransaction : public VDQtOutputTransaction {
public:
    using VDQtOutputTransaction::VDQtOutputTransaction;
    QSet<int> failRenames;
    bool failRemove = false;
    int calls = 0;
    std::function<void(int)> beforeRename;
protected:
    bool renameNoReplace(const QString& from, const QString& to, QString *error) override {
        ++calls;
        if (beforeRename) beforeRename(calls);
        if (failRenames.contains(calls)) {
            *error = QString("Injected move failure: %1 -> %2").arg(from, to);
            return false;
        }
        return VDQtOutputTransaction::renameNoReplace(from, to, error);
    }
    bool removeFile(const QString& path, QString *error) override {
        if (failRemove) {
            *error = QString("Injected removal failure: %1").arg(path);
            return false;
        }
        return VDQtOutputTransaction::removeFile(path, error);
    }
};

bool run(const QString& scenario) {
    Fixture f;
    QString error;
    FaultTransaction transaction(f.staging, f.targets);
    if (scenario == "fresh") {
        return transaction.inspect(&error) && transaction.existingTargets().isEmpty()
            && transaction.commit(f.stages, false, &error)
            && read(f.targets[0]) == "new0" && read(f.targets[1]) == "new1";
    }
    if (scenario == "staging-destination") {
        VDQtOutputTransaction invalid(f.staging, {f.staging.filePath("output-a"), f.staging.filePath("output-b")});
        return !invalid.inspect(&error) && error.contains("staging directory");
    }
    if (scenario == "approved") {
        write(f.targets[0], "original0");
        QFile::setPermissions(f.targets[0], QFile::ReadOwner);
        return transaction.inspect(&error) && transaction.existingTargets() == QStringList{f.targets[0]}
            && transaction.commit(f.stages, true, &error)
            && read(f.targets[0]) == "new0" && read(f.targets[1]) == "new1"
            && !(QFileInfo(f.targets[0]).permissions() & QFile::WriteOwner);
    }
    if (scenario == "unapproved") {
        f.originals();
        return transaction.inspect(&error) && !transaction.commit(f.stages, false, &error)
            && error.contains("not approved") && f.originalsIntact();
    }
    if (scenario == "late-collision") {
        if (!transaction.inspect(&error)) return false;
        write(f.targets[1], "foreign");
        return !transaction.commit(f.stages, true, &error) && read(f.targets[1]) == "foreign"
            && !QFileInfo::exists(f.targets[0]);
    }
    if (scenario == "changed-file" || scenario == "replaced-file") {
        f.originals();
        if (!transaction.inspect(&error)) return false;
        if (scenario == "replaced-file") {
            // Keep the old inode alive so inode reuse cannot invalidate the test.
            if (!QFile::rename(f.targets[0], f.directory.filePath("previous.avi"))) return false;
        }
        write(f.targets[0], "foreign-file");
        return !transaction.commit(f.stages, true, &error) && error.contains("changed")
            && read(f.targets[0]) == "foreign-file" && read(f.targets[1]) == "original1";
    }
    if (scenario == "invalid-stage") {
        f.originals();
        if (!transaction.inspect(&error)) return false;
        QFile::remove(f.stages[1]);
        return !transaction.commit(f.stages, true, &error) && f.originalsIntact();
    }
    if (scenario == "duplicate" || scenario == "directory") {
        if (scenario == "duplicate") {
            f.originals();
            QFile::remove(f.targets[1]);
            if (::link(QFile::encodeName(f.targets[0]).constData(),
                       QFile::encodeName(f.targets[1]).constData()) != 0) return false;
        } else {
            if (!QDir().mkdir(f.targets[0])) return false;
        }
        return !transaction.inspect(&error);
    }
    if (scenario == "dangling-symlink") {
        if (!QFile::link(f.directory.filePath("missing"), f.targets[0])) return false;
        transaction.failRenames = {3}; // backup link; install0; fail install1
        return transaction.inspect(&error) && transaction.existingTargets().size() == 1
            && !transaction.commit(f.stages, true, &error)
            && QFileInfo(f.targets[0]).isSymLink() && !QFileInfo::exists(f.targets[1]);
    }
    if (scenario == "changed-parent") {
        const QString parent = f.directory.filePath("outputs");
        const QString otherParent = f.directory.filePath("other-outputs");
        QDir().mkdir(parent); QDir().mkdir(otherParent);
        const QString alias = f.directory.filePath("directory-link");
        if (!QFile::link(parent, alias)) return false;
        VDQtOutputTransaction changing(f.staging, {QDir(alias).filePath("a"), QDir(alias).filePath("b")});
        if (!changing.inspect(&error)) return false;
        QFile::remove(alias);
        if (!QFile::link(otherParent, alias)) return false;
        return !changing.commit(f.stages, true, &error)
            && !QFileInfo::exists(QDir(parent).filePath("a"))
            && !QFileInfo::exists(QDir(otherParent).filePath("a"));
    }
    if (scenario == "commit-failure" || scenario == "backup-failure" || scenario == "unsupported-rename") {
        f.originals();
        transaction.failRenames = {scenario == "commit-failure" ? 4 : (scenario == "backup-failure" ? 2 : 1)};
        return transaction.inspect(&error) && !transaction.commit(f.stages, true, &error)
            && f.originalsIntact() && f.staging.autoRemove();
    }
    if (scenario == "racing-collision") {
        // Inject an entry after commit's absent-name check but before rename.
        transaction.beforeRename = [&](int call) { if (call == 2) write(f.targets[1], "foreign"); };
        return transaction.inspect(&error) && !transaction.commit(f.stages, false, &error)
            && read(f.targets[1]) == "foreign" && !QFileInfo::exists(f.targets[0]);
    }
    if (scenario == "backup-race") {
        f.originals();
        transaction.beforeRename = [&](int call) {
            if (call == 1) write(f.targets[0], "changed before backup move");
        };
        return transaction.inspect(&error) && !transaction.commit(f.stages, true, &error)
            && read(f.targets[0]) == "changed before backup move"
            && read(f.targets[1]) == "original1";
    }
    if (scenario == "exception") {
        f.originals();
        QString backup;
        bool caught = false;
        {
            QTemporaryDir stage(f.directory.filePath(".exception-XXXXXX"));
            const QStringList files{stage.filePath("a"), stage.filePath("b")};
            write(files[0], "new0"); write(files[1], "new1");
            FaultTransaction failing(stage, f.targets);
            failing.beforeRename = [](int call) {
                if (call == 2) throw std::runtime_error("unexpected operation failure");
            };
            backup = stage.filePath("backups/0");
            try {
                if (!failing.inspect(&error)) return false;
                failing.commit(files, true, &error);
            } catch (const std::runtime_error&) { caught = true; }
        }
        return caught && read(backup) == "original0" && read(f.targets[1]) == "original1";
    }
    if (scenario == "restore-failure" || scenario == "remove-failure"
        || scenario == "foreign-replacement" || scenario == "backup-restore-failure"
        || scenario == "rollback-race") {
        // A separate staging scope proves the preserved originals survive its
        // destructor, rather than merely remaining present during commit.
        QString retained, backup;
        f.originals();
        {
            QTemporaryDir stage(f.directory.filePath(".recovery-XXXXXX"));
            const QStringList files{stage.filePath("a"), stage.filePath("b")};
            write(files[0], "new0"); write(files[1], "new1");
            FaultTransaction failing(stage, f.targets);
            failing.failRenames = scenario == "backup-restore-failure" ? QSet<int>{2, 3} : QSet<int>{4};
            if (scenario == "restore-failure") failing.failRenames.insert(7);
            if (scenario == "remove-failure") failing.failRemove = true;
            if (scenario == "foreign-replacement" || scenario == "rollback-race") {
                failing.beforeRename = [&](int call) {
                    if (call == (scenario == "rollback-race" ? 5 : 4)) {
                        // Avoid inode reuse by keeping the rendered file alive.
                        QFile::rename(f.targets[0], f.directory.filePath("moved-new-output"));
                        write(f.targets[0], "foreign");
                    }
                };
            }
            if (!failing.inspect(&error) || failing.commit(files, true, &error) || stage.autoRemove()) return false;
            retained = stage.path();
            backup = stage.filePath(scenario == "remove-failure" ? "rollback-output-0" : "backups/0");
            if (!error.contains(backup) || (scenario != "remove-failure" && !error.contains(f.targets[0]))) return false;
        }
        return read(backup) == (scenario == "remove-failure" ? "new0" : "original0")
            && (scenario != "remove-failure" || read(f.targets[0]) == "original0")
            && read(f.targets[1]) == "original1"
            && read(QDir(retained).filePath("recovery.txt")).contains(f.targets[0].toUtf8())
            && ((scenario != "foreign-replacement" && scenario != "rollback-race")
                || read(f.targets[0]) == "foreign");
    }
    return false;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList scenarios{
        "fresh", "approved", "unapproved", "late-collision", "changed-file", "replaced-file",
        "invalid-stage", "duplicate", "directory", "dangling-symlink", "changed-parent",
        "commit-failure", "backup-failure", "racing-collision", "restore-failure",
        "remove-failure", "foreign-replacement", "backup-restore-failure",
        "unsupported-rename", "backup-race", "exception", "rollback-race", "staging-destination"
    };
    bool passed = true;
    for (const QString& scenario : scenarios) {
        passed = check(run(scenario), qPrintable(scenario)) && passed;
    }
    if (passed) std::cout << scenarios.size() << " output transaction regressions passed\n";
    return passed ? 0 : 1;
}
