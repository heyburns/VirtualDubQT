// Fault injection modifies only a copied, harness-owned bundle. Production
// code keeps its fixed-path rule and has no test/environment override switch.
#pragma once
#include "VirtualDub/VDQtMediaRuntime.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

class VDQtBundledToolFixture final {
public:
    explicit VDQtBundledToolFixture(const QByteArray& replacementDirectory) {
        mTool = VDQtMediaRuntime::ffmpeg();
        mSaved = mTool + QStringLiteral(".owned-test-backup");
        const QString ownedRoot = qEnvironmentVariable("VDQT_RUNTIME_TEST_ROOT");
        if (ownedRoot.isEmpty() || !mTool.startsWith(ownedRoot + QLatin1Char('/'))
            || QFileInfo::exists(mSaved))
            qFatal("Bundled tool fault injection requires a private runtime-test harness");
        if (!QFile::rename(mTool, mSaved)) qFatal("Cannot preserve owned test tool");
        const QString replacement = QDir(QString::fromUtf8(replacementDirectory)).filePath("ffmpeg");
        if (!QFile::link(replacement, mTool)) {
            QFile::rename(mSaved, mTool);
            qFatal("Cannot install owned encoder test fixture");
        }
    }
    ~VDQtBundledToolFixture() {
        QFile::remove(mTool); // Only this test's temporary symlink, never the real bundle.
        if (!QFile::rename(mSaved, mTool)) qFatal("Cannot restore owned test tool");
    }
    VDQtBundledToolFixture(const VDQtBundledToolFixture&) = delete;
    VDQtBundledToolFixture& operator=(const VDQtBundledToolFixture&) = delete;
private:
    QString mTool, mSaved;
};
