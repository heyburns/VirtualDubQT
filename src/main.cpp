#include <QApplication>
#include "vdwin32_shim.h"
#include "VirtualDub/VDQtMainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("VirtualDub");
    app.setOrganizationName("VirtualDub Port");

    VDQtMainWindow w;
    w.show();
    if (argc > 1) {
        w.openVideoFile(QString::fromLocal8Bit(argv[1]));
    }
    return app.exec();
}
