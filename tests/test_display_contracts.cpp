// Real context-menu lifetime and exclusive-action checks, without a desktop.
#include "VirtualDub/VDQtVideoDisplay.h"
#include "VirtualDub/VDQtDialogs.h"
#include <QApplication>
#include <QTimer>
#include <QLineEdit>
#include <QRadioButton>
#include <iostream>
#include <limits>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const VDJumpToPositionDialog::FrameTimeMapping clock = [](qint64 frame) {
        static constexpr double boundaries[] = {0, .01, .02, .12, .15};
        return frame >= 0 && frame < 5 ? boundaries[frame]
            : std::numeric_limits<double>::quiet_NaN();
    };
    qint64 target = -1;
    if (!VDJumpToPositionDialog::parseTimePosition("75 ms", 0, 0, 3, 30, &target, clock)
            || target != 2
        || !VDJumpToPositionDialog::parseTimePosition("+110 ms", 1, 0, 3, 30, &target, clock)
            || target != 3
        || !VDJumpToPositionDialog::parseTimePosition("-115 ms", 3, 0, 3, 30, &target, clock)
            || target != 0
        || VDJumpToPositionDialog::parseTimePosition("150 ms", 0, 0, 3, 30, &target, clock)
        || VDJumpToPositionDialog::parseTimePosition("1e100 s", 0, 0, 3, 30, &target, clock)
        || VDJumpToPositionDialog::parseTimePosition("9223372036854775808 s", 0, 0,
            std::numeric_limits<qint64>::max(), 1, &target)) {
        std::cerr << "FAIL: edited/VFR jump times must use real boundaries and checked bounds\n";
        return 1;
    }
    VDJumpToPositionDialog jump(3, 0, 3, 30, nullptr, clock);
    auto *timeEdit = jump.findChild<QLineEdit*>("jumpFrameTime");
    if (!timeEdit || timeEdit->text() != "0:00.120") {
        std::cerr << "FAIL: jump dialog must show the current frame's actual edited time\n";
        return 1;
    }
    for (auto *radio : jump.findChildren<QRadioButton*>())
        if (radio->text().contains("at time")) radio->setChecked(true);
    timeEdit->setText("75 ms");
    jump.accept();
    if (jump.result() != QDialog::Accepted || jump.selectedFrame() != 2) return 1;
    if (VDJumpToPositionDialog::formatFrameTime(2, 1.0 / 2147483647)
            != QStringLiteral("1193046:28:14.000")
        || VDJumpToPositionDialog::formatFrameTime(std::numeric_limits<qint64>::max(), 1e-20)
            != QStringLiteral("Time exceeds the supported display range")) {
        std::cerr << "FAIL: very long frame times must not overflow or become zero\n";
        return 1;
    }
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
