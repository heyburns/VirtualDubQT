// The noisy subprocess is this test executable, not a codec troubleshooting
// program. No media, hardware, user configuration or external service is used.
#include "VirtualDub/VDQtProcessDiagnosticTail.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <iostream>

namespace {
constexpr qint64 kExpectedNoiseBytes = qint64{6} * 1024 * 1024;
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
int emitNoise(const QString& outcome) {
    const QByteArray block(256 * 1024, 'x');
    for (int index = 0; index < 12; ++index) {
        std::cout.write(block.constData(), block.size());
        std::cerr.write(block.constData(), block.size());
    }
    std::cout.flush();
    std::cerr << "\nFINAL_" << outcome.toUpper().toStdString() << "_MARKER\n" << std::flush;
    if (outcome == "cancel") std::cin.get(); // Owner terminates this waiting child.
    return outcome == "failure" ? 7 : 0;
}
bool exercise(const QString& executable, const QString& outcome,
              QProcess::ProcessChannelMode channels = QProcess::MergedChannels) {
    QProcess process;
    process.setProcessChannelMode(channels);
    VDQtProcessDiagnosticTail diagnostics(process);
    process.start(executable, {"--emit-noise", outcome});
    if (!process.waitForStarted(5000)) return false;
    bool passed = true;
    QElapsedTimer timer;
    timer.start();
    const QByteArray marker = "FINAL_" + outcome.toUpper().toLatin1() + "_MARKER";
    while (process.state() != QProcess::NotRunning && timer.elapsed() < 10000) {
        process.waitForFinished(25);
        passed &= check(diagnostics.retainedBytes() <= diagnostics.capacity()
            && diagnostics.capacity() == 64 * 1024,
            "live subprocess diagnostics stay within the fixed byte budget");
        if (outcome == "cancel" && diagnostics.bytes().contains(marker)) {
            process.terminate();
            if (!process.waitForFinished(3000)) {
                process.kill();
                process.waitForFinished(3000);
            }
        }
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
        return false;
    }
    diagnostics.drain(); // Includes residual output after success/failure/cancel.
    passed &= check(diagnostics.totalBytesDrained() >= kExpectedNoiseBytes,
                    "several MiB from both streams were continuously drained");
    passed &= check(diagnostics.bytes().size() == 64 * 1024
        && diagnostics.bytes().contains(marker), "bounded capture retains the final diagnostic marker");
    if (outcome != "cancel") {
        passed &= check(process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == (outcome == "failure" ? 7 : 0),
            "diagnostic capture does not change the encoder result");
    }
    return passed;
}
bool lifetime(const QString& executable) {
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    {
        VDQtProcessDiagnosticTail shortLived(process, 128);
        process.start(executable, {"--emit-noise", "cancel"});
        if (!process.waitForStarted(5000)) return false;
        process.waitForFinished(25);
        if (!check(shortLived.retainedBytes() <= 128, "small diagnostic capacities are respected")) return false;
    }
    // Destroying the helper disconnects its capture before QProcess teardown.
    process.kill();
    return process.waitForFinished(3000);
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().value(1) == "--emit-noise")
        return emitNoise(application.arguments().value(2));
    bool passed = exercise(application.applicationFilePath(), "success");
    passed &= exercise(application.applicationFilePath(), "failure");
    passed &= exercise(application.applicationFilePath(), "cancel");
    passed &= exercise(application.applicationFilePath(), "failure", QProcess::SeparateChannels);
    passed &= lifetime(application.applicationFilePath());
    return passed ? 0 : 1;
}
