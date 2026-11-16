// Fixed-path policy and real tool/library agreement, without a window/device.
#include "VirtualDub/VDQtMediaRuntime.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString ffmpeg = VDQtMediaRuntime::ffmpeg();
    const QString ffprobe = VDQtMediaRuntime::ffprobe();
    QTemporaryDir unrelatedWorkingDirectory;
    qputenv("PATH", "/unrelated/system/tools");
    qputenv("APPDIR", "/unrelated/application");
    qputenv("VDQT_FFMPEG_BUNDLE", "/unrelated/runtime");
    if (!unrelatedWorkingDirectory.isValid() || !QDir::setCurrent(unrelatedWorkingDirectory.path())) return 1;
    if (!QFileInfo(ffmpeg).isAbsolute() || !QFileInfo(ffprobe).isAbsolute()
        || VDQtMediaRuntime::ffmpeg() != ffmpeg || VDQtMediaRuntime::ffprobe() != ffprobe
        || VDQtMediaRuntime::encoderProgram("ffmpeg") != ffmpeg
        || VDQtMediaRuntime::encoderProgram("/usr/bin/ffmpeg") != ffmpeg
        || VDQtMediaRuntime::encoderProgram("/usr/local/bin/ffprobe") != ffprobe
        || VDQtMediaRuntime::encoderProgram("/chosen/custom-encoder") != "/chosen/custom-encoder") {
        std::cerr << "Runtime paths changed with environment/cwd, or an encoder set bypassed the policy\n";
        return 1;
    }
    QString error;
    if (!VDQtMediaRuntime::validate(&error)) {
        std::cerr << error.toStdString() << '\n';
        return 1;
    }
    std::cout << VDQtMediaRuntime::report().toStdString() << '\n';
    return 0;
}
