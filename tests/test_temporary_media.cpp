// Lease ownership tests run without FFmpeg, widgets, user media or settings.
#include "VirtualDub/VDQtTemporaryMediaFile.h"
#include "VirtualDub/VDQtTemporaryStorage.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <thread>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool leases() {
    QTemporaryDir session;
    if (!session.isValid()) return false;
    const QString input = session.filePath("original.raw");
    const QByteArray original("original data is never temporary-owned media");
    if (!write(input, original)) return false;
    VDQtTemporaryMediaRegistry registry;
    QString error;
    auto active = registry.create(session.path(), &error);
    if (!active || !write(active->path(), QByteArray(128 * 1024, 'x'))) return false;
    const QString first = active->path();
    const QString firstDirectory = active->directoryPath();
    bool passed = check(firstDirectory != session.path()
        && QDir(session.path()).relativeFilePath(firstDirectory).startsWith("raw-"),
        "each materialization owns only its generated nested directory");
    passed &= check(!registry.pin(input), "the registry cannot take ownership of original input files");
    auto rollback = active;
    auto candidateForReopen = registry.pin(first);
    active.reset();
    passed &= check(QFileInfo::exists(first), "rollback and reopen pins retain an old source through Close");
    active = candidateForReopen;
    candidateForReopen.reset();
    rollback.reset();
    passed &= check(QFileInfo::exists(first), "reopened active source transfers the lease without deleting its file");
    active.reset();
    passed &= check(!QFileInfo::exists(firstDirectory) && !registry.pin(first),
                    "last consumer releases the entire owned raw materialization immediately");
    passed &= check(QFileInfo::exists(input) && QFileInfo::exists(session.path()),
                    "releasing temporary media never removes the original source or session root");
    registry.prune();
    passed &= check(registry.trackedPaths() == 0, "weak registry entries are pruned without retaining file data");
    for (int index = 0; index < 200; ++index) {
        auto attempt = registry.create(session.path(), &error);
        if (!attempt || !write(attempt->path(), QByteArray(8192, 'a'))) return false;
        const QString partial = attempt->directoryPath();
        attempt.reset();
        passed &= check(!QFileInfo::exists(partial), "cancelled/failed materialization releases partial output");
    }
    registry.prune();
    passed &= check(registry.trackedPaths() == 0
        && QDir(session.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(),
        "repeated retries do not accumulate directories or registry entries");
    return passed;
}
bool isolatedJobsAndFailure() {
    QTemporaryDir session;
    VDQtTemporaryMediaRegistry registry;
    QString error;
    auto active = registry.create(session.path(), &error);
    auto candidate = registry.create(session.path(), &error);
    if (!active || !candidate || !write(active->path(), "active")
        || !write(candidate->path(), "candidate")) return false;
    const QString activePath = active->path();
    const QString candidatePath = candidate->path();
    auto rollback = active;
    active.reset();
    active = candidate;
    // A late candidate-open failure closes the candidate before recovering
    // the previous source. Ownership ordering must not destroy rollback data.
    active.reset();
    candidate.reset();
    bool passed = check(!QFileInfo::exists(candidatePath)
        && QFileInfo::exists(activePath), "failed replacement releases only its own candidate file");
    active = rollback;
    rollback.reset();
    {
        auto job = registry.create(session.path(), &error);
        if (!job || !write(job->path(), "offline job")) return false;
        const QString jobPath = job->path();
        job.reset();
        passed &= check(!QFileInfo::exists(jobPath) && QFileInfo::exists(activePath),
                        "offline job completion does not release the editor's materialization");
    }
    // Releasing on a joined worker does not require a QObject event loop; the
    // owner is deliberately just filesystem storage, not a controller object.
    const QString finalDirectory = active->directoryPath();
    std::thread consumer([last = std::move(active)]() mutable { last.reset(); });
    consumer.join();
    passed &= check(!QFileInfo::exists(finalDirectory), "joined final consumer releases its lease safely");
    auto invalid = registry.create(session.filePath("does-not-exist"), &error);
    passed &= check(!invalid && !error.isEmpty(), "invalid parent directories fail without manufacturing ownership");
    return passed;
}

bool storageBounds() {
    constexpr qint64 reserve = qint64{64} * 1024 * 1024;
    constexpr qint64 maximum = std::numeric_limits<qint64>::max();
    qint64 bytes = -1;
    bool passed = check(VDQtEstimateTemporaryStorage(1000, 4, 128, &bytes)
        && bytes == reserve + 1512, "raw storage estimate includes bounded packet overhead and reserve");
    passed &= check(VDQtEstimateFrameTemporaryStorage(1920, 1080, 8, 100, 128, &bytes)
        && bytes == reserve + qint64{1920} * 1080 * 8 * 100 + 12800,
        "two-pass storage uses checked integer frame payloads");
    bytes = 123;
    passed &= check(!VDQtEstimateTemporaryStorage(maximum, 1, 1, &bytes)
        && !VDQtEstimateTemporaryStorage(0, maximum, 128, &bytes)
        && !VDQtEstimateFrameTemporaryStorage(maximum, 2, 8, 2, 128, &bytes)
        && !VDQtEstimateFrameTemporaryStorage(1, 1, 1, -1, 128, &bytes)
        && bytes == 123, "invalid/overflowing estimates fail without changing the output");
    QTemporaryDir directory;
    QString error;
    passed &= check(directory.isValid()
        && VDQtRequireTemporaryStorage(directory.path(), 0, &error) && error.isEmpty(),
        "ready writable temporary volumes admit a zero-byte probe");
    passed &= check(!VDQtRequireTemporaryStorage(directory.path(), maximum, &error)
        && error.contains("not enough", Qt::CaseInsensitive),
        "insufficient space fails before allocating an intermediate");
    passed &= check(!VDQtRequireTemporaryStorage(directory.path(), -1, &error)
        && !error.isEmpty(), "invalid storage requests have a useful diagnostic");
    return passed;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    return leases() && isolatedJobsAndFailure() && storageBounds() ? 0 : 1;
}
