// Geometry contracts exercise the same plan used by filtering, allocation
// preflight and the filter list. Fixtures are synthetic and contain no codec or
// user-media dependencies. Keep Windows subpixel/alignment parity separate.
#include "VirtualDub/VDQtFilterGeometry.h"
#include "VirtualDub/VDQtFilterValidation.h"

#include <QCoreApplication>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool expect(VDFilterType type, QMap<QString, double> params, QSize input, QSize expected) {
    VDQtFilterSystem system;
    system.addFilter(type);
    auto configured = system.getActiveChain().first().params;
    for (auto it = params.cbegin(); it != params.cend(); ++it) configured[it.key()] = it.value();
    system.updateFilterParams(0, configured);
    const auto filter = system.getActiveChain().first();
    VDQtFilterGeometry geometry;
    QString error;
    bool passed = check(VDQtValidateFilter(filter, &error, input, 4)
        && VDQtComputeFilterGeometry(filter, input, 4, &geometry, &error), "valid filter geometry is accepted");
    passed &= check(geometry.outputKnown && geometry.outputSize == expected, "planned dimensions match the control contract");
    for (auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888, QImage::Format_RGBA64}) {
        QImage source(input, format);
        source.fill(QColor(50, 80, 130, 180));
        QImage output = system.processFrame(source);
        passed &= check(!output.isNull() && output.size() == expected, "rendered dimensions agree with planned dimensions at each precision");
    }
    return passed;
}
bool geometryMatrix() {
    bool passed = true;
    passed &= expect(VDFilterType::Resize, {{"sizeMode", 1}, {"relW", 50}, {"relH", 30}, {"aspectMode", 0}}, {100, 80}, {50, 24});
    passed &= expect(VDFilterType::Resize, {{"sizeMode", 1}, {"relW", 50}, {"relH", 30}, {"aspectMode", 1}}, {100, 80}, {50, 40});
    passed &= expect(VDFilterType::Resize, {{"sizeMode", 1}, {"relW", 50}, {"relH", 30}, {"aspectMode", 2}, {"aspectW", 5}, {"aspectH", 3}}, {100, 80}, {50, 30});
    passed &= expect(VDFilterType::Resize, {{"sizeMode", 1}, {"relW", 51}, {"relH", 51}, {"aspectMode", 0}, {"codecAdjust", 8}}, {100, 80}, {48, 40});
    passed &= expect(VDFilterType::Resize, {{"sizeMode", 0}, {"width", 51}, {"height", 37}, {"aspectMode", 1}, {"codecAdjust", 8}}, {100, 80}, {51, 37});
    passed &= expect(VDFilterType::Resize, {{"sizeMode", 1}, {"relW", 50}, {"relH", 50}, {"framingMode", 1}, {"frameW", 72}, {"frameH", 60}}, {100, 80}, {72, 60});
    passed &= expect(VDFilterType::Resize, {{"sizeMode", 1}, {"relW", 50}, {"relH", 50}, {"framingMode", 2}, {"frameAspectW", 1}, {"frameAspectH", 1}}, {100, 80}, {40, 40});
    passed &= expect(VDFilterType::Resize, {{"sizeMode", 1}, {"relW", 50}, {"relH", 50}, {"framingMode", 3}, {"frameAspectW", 1}, {"frameAspectH", 1}}, {100, 80}, {50, 50});
    passed &= expect(VDFilterType::Resize, {{"sizeMode", 1}, {"relW", 50}, {"relH", 50}, {"_sylia.clip.left", 10}, {"_sylia.clip.right", 10}}, {100, 80}, {40, 40});
    passed &= expect(VDFilterType::Canvas, {{"width", 72}, {"height", 60}}, {100, 80}, {72, 60});
    passed &= expect(VDFilterType::Canvas, {{"width", 0}, {"height", 0}}, {100, 80}, {100, 80});
    passed &= expect(VDFilterType::WarpResize, {{"width", 72}, {"height", 60}}, {100, 80}, {72, 60});
    passed &= expect(VDFilterType::Crop, {{"left", 10}, {"right", 7}, {"top", 3}, {"bottom", 2}}, {100, 80}, {83, 75});
    passed &= expect(VDFilterType::Reduce2, {}, {101, 81}, {50, 40});
    passed &= expect(VDFilterType::Reduce2HQ, {}, {101, 81}, {50, 40});
    passed &= expect(VDFilterType::Rotate, {{"mode", 0}}, {100, 80}, {80, 100});
    passed &= expect(VDFilterType::Rotate, {{"mode", 1}}, {100, 80}, {80, 100});
    passed &= expect(VDFilterType::Rotate, {{"mode", 2}}, {100, 80}, {100, 80});
    passed &= expect(VDFilterType::Rotate2, {{"angle", 90}, {"expand", 1}}, {100, 80}, {80, 100});
    passed &= expect(VDFilterType::Rotate2, {{"angle", 33}, {"expand", 0}}, {100, 80}, {100, 80});
    passed &= expect(VDFilterType::Perspective, {{"topLeftX", 0.2}}, {100, 80}, {100, 80});
    return passed;
}
bool budgetsAndConditionalGeometry() {
    VDFilterInstance filter;
    filter.type = VDFilterType::Resize;
    filter.params = {{"sizeMode", 1}, {"relW", 1000}, {"relH", 1000}};
    VDQtFilterGeometry plan;
    QString error;
    bool passed = check(!VDQtComputeFilterGeometry(filter, {10000, 10000}, 8, &plan, &error) && !error.isEmpty(),
                        "oversized calculated geometry fails before allocation");
    filter.params = {{"sizeMode", 0}, {"width", 100}, {"height", 80}, {"_sylia.clip.left", 100}};
    passed &= check(!VDQtComputeFilterGeometry(filter, {100, 80}, 4, &plan, &error), "clipping the complete image is rejected");
    filter.params = {{"sizeMode", 0}, {"width", 50}, {"height", 40}, {"_sylia.range.end", 10}};
    passed &= check(VDQtComputeFilterGeometry(filter, {100, 80}, 4, &plan, &error) && !plan.outputKnown,
                    "ranged geometry is explicitly conditional, not an invented fixed size");
    filter.enabled = false;
    passed &= check(VDQtComputeFilterGeometry(filter, {100, 80}, 4, &plan, &error) && plan.outputKnown && plan.outputSize == QSize(100, 80),
                    "disabled filters leave geometry unchanged");
    filter.enabled = true;
    filter.type = VDFilterType::Plugin;
    filter.params.clear();
    passed &= check(VDQtComputeFilterGeometry(filter, {100, 80}, 4, &plan, &error) && !plan.outputKnown,
                    "native plug-in geometry remains unknown until negotiation");
    return passed;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    bool passed = geometryMatrix();
    passed &= budgetsAndConditionalGeometry();
    return passed ? 0 : 1;
}
