#include "VirtualDub/VDQtPathIdentitySet.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <iostream>
#include <unistd.h>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool write(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("fixture") == 7;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    const QString first = directory.filePath("first.dat");
    const QString hard = directory.filePath("hard.dat");
    const QString symbolic = directory.filePath("symbolic.dat");
    if (!write(first) || ::link(QFile::encodeName(first).constData(), QFile::encodeName(hard).constData()) != 0
        || !QFile::link(first, symbolic)) return 2;
    VDQtPathIdentitySet existing;
    QString alias;
    bool passed = check(existing.insert(first), "first existing destination is unique");
    passed &= check(!existing.insert(hard, &alias) && alias == first,
                    "hard links share native inode identity");
    passed &= check(!existing.insert(symbolic, &alias) && alias == first,
                    "symlinks cannot hide duplicate existing destinations");
    const QString future = directory.filePath("future.dat");
    const QString dangling = directory.filePath("dangling.dat");
    if (!QFile::link(future, dangling)) return 2;
    VDQtPathIdentitySet prospective;
    passed &= check(prospective.insert(future)
        && !prospective.insert(directory.path() + "/./future.dat", &alias),
        "nonexistent outputs with normalized relative aliases collide");
    passed &= check(!prospective.insert(dangling, &alias) && alias == future,
                    "dangling links to the same future destination collide");
    const QString real = directory.filePath("real");
    if (!QDir().mkpath(real + "/child") || !QDir().mkdir(directory.filePath("other"))) return 2;
    const QString link = directory.filePath("other/linked");
    if (!QFile::link(real + "/child", link)) return 2;
    VDQtPathIdentitySet parents;
    passed &= check(parents.insert(link + "/../result.dat")
        && !parents.insert(real + "/result.dat", &alias),
        "directory symlinks are resolved before parent traversal normalization");
    VDQtPathIdentitySet many;
    QElapsedTimer timer;
    timer.start();
    for (int index = 0; index < 1000; ++index)
        if (!many.insert(directory.filePath(QString("output-%1.dat").arg(index)))) return 1;
    passed &= check(many.size() == 1000 && many.identityQueries() == 1000,
                    "1000 output records require linear, not pairwise, native identity queries");
    if (application.arguments().contains("--benchmark"))
        std::cout << "1000 unique output identities: " << timer.nsecsElapsed() / 1000000.0 << " ms\n";
    return passed ? 0 : 1;
}
