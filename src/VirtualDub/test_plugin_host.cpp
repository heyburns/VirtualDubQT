#include "VDQtFilterSystem.h"
#include "VDQtPluginHost.h"

#include <QCoreApplication>
#include <QColor>
#include <QImage>

#include <iostream>
#include <algorithm>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (argc != 2) {
        std::cerr << "plugin directory argument is required\n";
        return 1;
    }
    qputenv("VIRTUALDUBQT_PLUGIN_PATH", QByteArray(argv[1]));
    VDQtPluginHost::instance().reload();
    const auto catalog = VDQtPluginHost::instance().videoFilters();
    auto found = std::find_if(catalog.cbegin(), catalog.cend(), [](const auto& info) {
        return info.name == QStringLiteral("VDQt native test invert");
    });
    if (found == catalog.cend()) {
        std::cerr << VDQtPluginHost::instance().report().toStdString() << '\n';
        return 1;
    }

    VDQtFilterSystem filters;
    if (!filters.addPluginFilter(found->id)) {
        std::cerr << "could not add discovered plugin filter\n";
        return 1;
    }
    QImage source(3, 2, QImage::Format_ARGB32);
    source.fill(QColor(255, 0, 0, 123));
    const QImage output = filters.processFrame(source).convertToFormat(
        QImage::Format_ARGB32);
    const QColor pixel = output.pixelColor(1, 1);
    if (pixel.red() != 0 || pixel.green() != 255 || pixel.blue() != 255
        || pixel.alpha() != 123) {
        std::cerr << "plugin output mismatch: " << pixel.red() << ','
                  << pixel.green() << ',' << pixel.blue() << ',' << pixel.alpha()
                  << '\n';
        return 1;
    }
    return 0;
}
