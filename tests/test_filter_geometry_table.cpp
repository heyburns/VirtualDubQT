// The displayed filter-chain resolutions must use the actual geometry plan,
// including relative sizing, disabled stages and negotiated/conditional sizes.
#include "VirtualDub/VDQtDialogs.h"
#include "VirtualDub/VDQtFilterSystem.h"

#include <QApplication>
#include <QTableWidget>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
void configure(int index, QMap<QString, double> changes) {
    auto& filters = VDQtFilterSystem::instance();
    auto parameters = filters.getActiveChain().at(index).params;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) parameters[it.key()] = it.value();
    filters.updateFilterParams(index, parameters);
}
bool chainDimensions() {
    auto& filters = VDQtFilterSystem::instance();
    filters.clearFilters();
    filters.addFilter(VDFilterType::Resize);
    configure(0, {{"sizeMode", 1}, {"relW", 50}, {"relH", 30}, {"aspectMode", 1}});
    filters.addFilter(VDFilterType::Canvas);
    configure(1, {{"width", 72}, {"height", 60}});
    filters.addFilter(VDFilterType::Rotate);
    filters.addFilter(VDFilterType::Reduce2);
    VDVideoFiltersDialog dialog(100, 80);
    auto *table = dialog.findChild<QTableWidget *>();
    return check(table && table->rowCount() == 4
        && table->item(0, 1)->text() == "100x80" && table->item(0, 2)->text() == "50x40"
        && table->item(1, 1)->text() == "50x40" && table->item(1, 2)->text() == "72x60"
        && table->item(2, 1)->text() == "72x60" && table->item(2, 2)->text() == "60x72"
        && table->item(3, 1)->text() == "60x72" && table->item(3, 2)->text() == "30x36",
        "filter table follows actual relative/canvas/rotation/reduction geometry");
}
bool disabledAndConditional() {
    auto& filters = VDQtFilterSystem::instance();
    filters.clearFilters();
    filters.addFilter(VDFilterType::Resize);
    configure(0, {{"sizeMode", 1}, {"relW", 50}, {"relH", 50}});
    filters.setFilterEnabled(0, false);
    filters.addFilter(VDFilterType::Rotate2);
    configure(1, {{"angle", 33}, {"expand", 0}});
    bool passed;
    {
        VDVideoFiltersDialog dialog(100, 80);
        auto *table = dialog.findChild<QTableWidget *>();
        passed = check(table && table->item(0, 2)->text() == "100x80"
            && table->item(1, 1)->text() == "100x80" && table->item(1, 2)->text() == "100x80",
            "disabled filters and non-expanding rotation retain displayed size");
    }
    filters.setFilterEnabled(0, true);
    configure(0, {{"_sylia.range.start", 0}, {"_sylia.range.end", 10}});
    {
        VDVideoFiltersDialog dialog(100, 80);
        auto *table = dialog.findChild<QTableWidget *>();
        passed &= check(table && table->item(0, 2)->text() == "Depends on frame"
            && table->item(1, 1)->text() == "Depends on frame" && table->item(1, 2)->text() == "Depends on frame",
            "conditional geometry propagates an honest unknown size through the table");
    }
    return passed;
}
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    bool passed = chainDimensions();
    passed &= disabledAndConditional();
    VDQtFilterSystem::instance().clearFilters();
    return passed ? 0 : 1;
}
