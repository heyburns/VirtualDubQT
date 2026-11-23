// Confirm that runtime library ownership is resolved through the actual
// linked function even when an executable exposes a canonical PLT entry.
#include "VirtualDub/VDQtMediaRuntime.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <dlfcn.h>
#include <iostream>

extern "C" const void* vdqtNonPicVersionAddress();

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Dl_info location{};
    if (!dladdr(vdqtNonPicVersionAddress(), &location) || !location.dli_fname
        || QFileInfo(QString::fromLocal8Bit(location.dli_fname)).canonicalFilePath()
            != QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()) {
        std::cerr << "Linker did not produce the canonical-PLT fixture; skipped\n";
        return 77;
    }
    QString error;
    if (!VDQtMediaRuntime::validate(&error)) {
        std::cerr << error.toStdString() << '\n';
        return 1;
    }
    std::cout << "Private backend identified correctly despite executable PLT addresses\n";
    return 0;
}
