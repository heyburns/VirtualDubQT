// Run the actual installed-style entry point, not a linked controller substitute.
// Every source, export and setting belongs to this test's temporary directory.
#include "support/VDQtTestFixtures.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <iostream>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2) {
        std::cerr << "Usage: cli_media_smoke_tests /path/to/VirtualDubQt\n";
        return 1;
    }
    const QFileInfo executable(application.arguments().at(1));
    if (!executable.isFile() || !executable.isExecutable()) {
        std::cerr << "Application executable is missing or not executable\n";
        return 1;
    }
    VDQtTestFixtures fixtures;
    if (!fixtures.createBasic(64, 48, 8)) {
        std::cerr << fixtures.error.toStdString() << '\n';
        return 1;
    }
    int sourceIndex = 0;
    for (const QString& source : {fixtures.mp4, fixtures.avs}) {
        const QString output = fixtures.directory.filePath(
            QStringLiteral("export-%1.raw").arg(sourceIndex));
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("VD_DISABLE_AUDIO_OUTPUT", "1");
        environment.insert("XDG_CONFIG_HOME", fixtures.directory.filePath(
            QStringLiteral("config-%1").arg(sourceIndex)));
        environment.insert("XDG_DATA_HOME", fixtures.directory.filePath(
            QStringLiteral("data-%1").arg(sourceIndex)));
        QProcess child;
        child.setProcessEnvironment(environment);
        child.start(executable.absoluteFilePath(), {source, "--command",
            QStringLiteral("VirtualDub.audio.SetSource(0);"
                           "VirtualDub.SaveRawVideo(\"%1\",8,4,0,0);").arg(output),
            "--exit"});
        if (!child.waitForStarted(5000) || !child.waitForFinished(30000)) {
            // This is solely our disposable subprocess, never a running editor.
            child.kill();
            child.waitForFinished(5000);
            std::cerr << "Application media smoke timed out: "
                      << child.errorString().toStdString() << '\n';
            return 1;
        }
        QFile raw(output);
        if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0
            || !raw.open(QIODevice::ReadOnly) || raw.size() != 8 * 64 * 48 * 4) {
            std::cerr << "Application failed decode/raw-export smoke for "
                      << source.toStdString() << ":\n"
                      << child.readAllStandardError().constData() << '\n';
            return 1;
        }
        const QByteArray bytes = raw.readAll();
        if (sourceIndex == 1 && bytes.left(4) != QByteArray::fromHex("804020ff")) {
            std::cerr << "Native AviSynth raw output has unexpected BGRA pixels\n";
            return 1;
        }
        ++sourceIndex;
    }
    std::cout << "Actual application opened/exported MP4 and native AVS fixtures\n";
    return 0;
}
