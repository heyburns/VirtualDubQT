// Real context-menu lifetime and exclusive-action checks, without a desktop.
#include "VirtualDub/VDQtVideoDisplay.h"
#include <QApplication>
#include <QTimer>
#include <iostream>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    VDVideoDisplayWidget display(QStringLiteral("Input"));
    display.show();
    for (int cycle = 0; cycle < 20; ++cycle) {
        bool inspected = false, exclusive = true;
        QTimer::singleShot(0, &display, [&] {
            const auto menus = display.findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
            if (menus.isEmpty()) return;
            QMenu *menu = menus.first();
            const auto groups = menu->findChildren<QActionGroup*>();
            inspected = groups.size() == 4;
            for (auto *group : groups) {
                int checked = 0;
                for (auto *action : group->actions()) checked += action->isChecked();
                exclusive &= group->isExclusive() && checked == 1;
            }
            QMenu *zoom = menu->actions().first()->menu();
            for (auto *action : zoom->actions())
                if (action->text() == QStringLiteral("50%")) action->trigger();
            exclusive &= display.zoomLevel() == 0.5;
            menu->close();
        });
        // Also close on a failed lookup so the harness never hangs in exec().
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &display, [&] {
            exclusive = false;
            for (auto *menu : display.findChildren<QMenu*>()) menu->close();
        });
        timeout.start(1000);
        QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(5, 5), display.mapToGlobal(QPoint(5, 5)));
        QApplication::sendEvent(&display, &event);
        if (!inspected || !exclusive || !display.findChildren<QActionGroup*>().isEmpty()) {
            std::cerr << "FAIL: display menus must retain exclusive controls only until they close\n";
            return 1;
        }
    }
    std::cout << "Twenty preview menu lifetimes and zoom controls passed\n";
    return 0;
}
