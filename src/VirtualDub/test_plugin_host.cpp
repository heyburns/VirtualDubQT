// End-to-end host ABI test using vdqt_test_plugin built beside this executable.
// It verifies discovery, configuration, processing, and instance teardown.
#include "VDQtFilterSystem.h"
#include "VDQtPluginHost.h"
#include "VDQtVideoAspect.h"

#include <QCoreApplication>
#include <QColor>
#include <QImage>
#include <QDir>
#include <QLibrary>

#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
bool aspectContracts(const QList<VDQtPluginFilterInfo>& catalog, QLibrary& module,
                     bool includePipeline) {
    auto& host = VDQtPluginHost::instance();
    struct Cleanup { ~Cleanup() { VDQtPluginHost::instance().forgetAllInstances(); } } cleanup;
    const auto aspect = reinterpret_cast<qint64 (*)(int)>(module.resolve("VDQtTestAspect"));
    const auto initializations = reinterpret_cast<int (*)()>(module.resolve("VDQtTestFilterInitializations"));
    if (!aspect || !initializations) return false;
    const auto id = [&](const QString& name) {
        for (const auto& info : catalog) if (info.name == name) return info.id;
        return QString();
    };
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) std::cerr << "FAIL: " << message << '\n';
        passed &= condition;
        return condition;
    };
    const auto hasAspect = [](const QImage& image, int numerator, int denominator) {
        const AVRational ratio = VDQtImageSampleAspectRatio(image);
        return !image.isNull() && ratio.num == numerator && ratio.den == denominator;
    };
    QImage source(3, 2, QImage::Format_ARGB32);
    source.fill(QColor(255, 0, 0, 123));
    VDQtSetImageSampleAspectRatio(source, {3, 2});
    QImage output;
    QString error;
    VDFilterFrameContext context{12, 0.48, 25};
    const QString negotiated = id("VDQt negotiated aspect test");
    check(!negotiated.isEmpty() && host.processVideoFilter(negotiated, "aspect-negotiated", {},
            source, &output, &error, &context)
        && hasAspect(output, 4, 3) && aspect(0) == 3 && aspect(1) == 2
        && aspect(2) == 3 && aspect(3) == 2
        && aspect(4) == 4 && aspect(5) == 3 && aspect(8) == 4 && aspect(9) == 3,
        "source SAR reaches param/run; negotiated output SAR survives rebinding and normalization");
    const int negotiatedInstances = initializations();
    context.frameNumber = 13;
    check(host.processVideoFilter(negotiated, "aspect-negotiated", {}, source, &output, &error, &context)
        && initializations() == negotiatedInstances && hasAspect(output, 4, 3),
        "unchanged sequential aspect metadata retains a prepared plugin runtime");
    QImage alias = source;
    context.frameNumber = 14;
    check(host.processVideoFilter(negotiated, "aspect-negotiated", {}, alias, &alias, &error, &context)
        && hasAspect(alias, 4, 3) && alias.pixelColor(0, 0) == QColor(0, 255, 255, 123)
        && hasAspect(source, 3, 2), "in-place host calls retain input while tagging only the output copy");

    const QString perFrame = id("VDQt per-frame aspect test");
    context.frameNumber = 20;
    check(host.processVideoFilter(perFrame, "aspect-run", {}, source, &output, &error, &context)
        && hasAspect(output, 4, 3), "runProc can return an explicit per-frame SAR override");
    context.frameNumber = 21;
    check(host.processVideoFilter(perFrame, "aspect-run", {}, source, &output, &error, &context)
        && hasAspect(output, 3, 2) && aspect(4) == 3 && aspect(5) == 2,
        "a prior run override does not leak into the next frame's negotiated binding");

    context.frameNumber = 0;
    check(host.processVideoFilter(id("VDQt unknown aspect test"), "aspect-unknown", {},
            source, &output, &error, &context)
        && hasAspect(output, 1, 1) && aspect(4) == 0 && aspect(5) == 0
        && aspect(8) == 0 && aspect(9) == 0,
        "legal 0/0 stays unknown in VDX and uses the Windows-compatible square display fallback");
    check(host.processVideoFilter(id("VDQt wide aspect test"), "aspect-wide", {},
            source, &output, &error, &context) && hasAspect(output, 1, 1),
        "unsigned ABI rationals normalize without narrowing overflow");
    for (const QString& name : {QString("VDQt invalid negotiated aspect test"),
             QString("VDQt invalid per-frame aspect test"), QString("VDQt too-small aspect test")}) {
        output = source;
        error.clear();
        check(!host.processVideoFilter(id(name), "aspect-invalid", {}, source, &output, &error, &context)
            && output.isNull() && error.contains("aspect ratio"),
            "malformed or unrepresentable returned SAR fails with no stale output pixels/tags");
    }

    const QString history = id("VDQt previous-frame test");
    check(host.processVideoFilter(history, "aspect-history", {}, source, &output, &error, &context)
        && hasAspect(output, 3, 2) && aspect(6) == 3 && aspect(7) == 2,
        "the initial previous-frame binding contains its source SAR");
    QImage next = source;
    next.fill(Qt::green);
    context.frameNumber = 1;
    check(host.processVideoFilter(history, "aspect-history", {}, next, &output, &error, &context)
        && output.pixelColor(0, 0) == source.pixelColor(0, 0)
        && aspect(6) == 3 && aspect(7) == 2,
        "advancing previous-frame pixels and metadata describe the same frame");
    const int beforeAspectChange = initializations();
    VDQtSetImageSampleAspectRatio(next, {5, 4});
    context.frameNumber = 2;
    check(host.processVideoFilter(history, "aspect-history", {}, next, &output, &error, &context)
        && initializations() == beforeAspectChange + 1 && hasAspect(output, 5, 4)
        && output.pixelColor(0, 0) == next.pixelColor(0, 0)
        && aspect(0) == 5 && aspect(1) == 4 && aspect(6) == 5 && aspect(7) == 4,
        "changed per-frame source SAR renegotiates and cannot reuse incompatible history");
    next.fill(Qt::blue);
    context.frameNumber = 90;
    check(host.processVideoFilter(history, "aspect-history", {}, next, &output, &error, &context)
        && initializations() == beforeAspectChange + 2 && hasAspect(output, 5, 4)
        && output.pixelColor(0, 0) == next.pixelColor(0, 0),
        "a seek resets pixels and aspect history together");

    if (includePipeline) {
        VDQtFilterSystem pipeline;
        check(pipeline.addPluginFilter(negotiated)
            && hasAspect(pipeline.processFrame(source, {0, 0, 25}), 4, 3),
            "the central filter hand-off preserves returned plugin SAR instead of replacing it with input SAR");
        pipeline.addPluginFilter(id("VDQt native test invert"));
        pipeline.addFilter(VDFilterType::Rotate);
        auto chain = pipeline.getActiveChain();
        chain.last().params["mode"] = 0;
        pipeline.replaceActiveChainTransient(chain);
        check(hasAspect(pipeline.processFrame(source, {0, 0, 25}), 3, 4)
            && aspect(0) == 4 && aspect(1) == 3 && hasAspect(source, 3, 2),
            "downstream plugins and quarter turns consume the changed ratio without altering source metadata");
        pipeline.clearFilters();
        pipeline.addPluginFilter(negotiated);
        chain = pipeline.getActiveChain();
        chain.first().params["_sylia.opacity.count"] = 1;
        chain.first().params["_sylia.opacity.0.x"] = 0;
        chain.first().params["_sylia.opacity.0.y"] = 0.5;
        pipeline.replaceActiveChainTransient(chain);
        check(hasAspect(pipeline.processFrame(source, {0, 0, 25}), 4, 3),
            "opacity painting does not discard negotiated plugin stream metadata");
    }
    return passed;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (argc != 2 && !(argc == 3 && QByteArray(argv[2]) == "--host-only")) {
        std::cerr << "plugin directory argument is required\n";
        return 1;
    }
    qputenv("VIRTUALDUBQT_PLUGIN_PATH", QByteArray(argv[1]));
    QLibrary module(QDir(QString::fromLocal8Bit(argv[1])).filePath("vdqt_test_plugin.so"));
    const auto liveInstances = reinterpret_cast<int (*)()>(module.resolve("VDQtTestLiveInstances"));
    if (!liveInstances) { std::cerr << "test module counter unavailable\n"; return 1; }
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
    if (liveInstances() != 1) {
        std::cerr << "expected one live plugin runtime\n"; return 1;
    }
    {
        VDQtFilterSystem independent;
        independent.replaceActiveChainTransient(filters.getActiveChain());
        independent.processFrame(source);
        if (liveInstances() != 2) {
            std::cerr << "pipelines share a runtime despite identical serialized IDs\n";
            return 1;
        }
        filters.resetRuntimeState();
        if (liveInstances() != 1) {
            std::cerr << "reset destroyed another pipeline's runtime\n"; return 1;
        }
        independent.processFrame(source);
        if (liveInstances() != 1) {
            std::cerr << "independent runtime was unnecessarily recreated\n"; return 1;
        }
    }
    if (liveInstances() != 0) {
        std::cerr << "pipeline destruction retained plugin runtime\n"; return 1;
    }
    const auto runCalls = reinterpret_cast<int (*)()>(module.resolve("VDQtTestRunCalls"));
    filters.clearFilters();
    if (!runCalls || !filters.addPluginFilter(found->id)) return 1;
    filters.addFilter(VDFilterType::BobDoubler);
    const int callsBefore = runCalls();
    QList<QImage> phases;
    if (!filters.processFrameSequence(source, phases, {0, 0, 25})
        || phases.size() != 2 || runCalls() != callsBefore + 1) {
        std::cerr << "upstream plugin work was repeated for Bob output phases\n"; return 1;
    }
    const auto timing = reinterpret_cast<qint64 (*)(int)>(module.resolve("VDQtTestTiming"));
    filters.clearFilters();
    if (!timing || !filters.addPluginFilter(found->id)) return 1;
    VDFilterFrameContext context{40, 1.25, 30000.0 / 1001};
    context.sourceFrameNumber = 100;
    context.sourceTimestampSeconds = 3.125;
    context.inputDurationSeconds = 0.05;
    if (filters.processFrame(source, context).isNull()
        || timing(0) != 30000 || timing(1) != 1001
        || timing(2) != 40 || timing(3) != 40 || timing(4) != 12500000
        || timing(5) != 13000000 || timing(6) != 40 || timing(7) != 100
        || timing(8) != 50000 || timing(9) != 3125
        || timing(10) != 30000 || timing(11) != 1001 || timing(12) != 40) {
        std::cerr << "plugin preparation/run fields ignore actual frame context\n"; return 1;
    }
    filters.clearFilters();
    filters.addFilter(VDFilterType::BobDoubler);
    if (!filters.addPluginFilter(found->id)
        || !filters.processFrameSequence(source, phases, {4, 0.16, 25})
        || timing(0) != 50 || timing(1) != 1 || timing(2) != 9
        || timing(4) != 1800000 || timing(5) != 2000000) {
        std::cerr << "plugin after Bob lacks emitted-phase timing\n"; return 1;
    }
    const auto findPlugin = [&](const QString& name) {
        for (const auto& info : catalog) if (info.name == name) return info.id;
        return QString();
    };
    filters.clearFilters();
    if (!filters.addPluginFilter(findPlugin("VDQt previous-frame test"))) return 1;
    if (filters.processFrame(source, {10, 0.4, 25}).convertToFormat(QImage::Format_ARGB32) != source)
        return 1;
    QImage next = source;
    next.fill(Qt::green);
    if (filters.processFrame(next, {11, 0.44, 25}).convertToFormat(QImage::Format_ARGB32) != source
        || timing(13) != 10
        || filters.processFrame(next, {90, 3.6, 25}).convertToFormat(QImage::Format_ARGB32) != next) {
        std::cerr << "previous-frame history crossed a seek or lost its ordinal\n"; return 1;
    }
    for (const QString& name : {QString("VDQt unsupported future-frame test"), QString("VDQt unsupported rate test")}) {
        filters.clearFilters();
        if (!filters.addPluginFilter(findPlugin(name))) return 1;
        if (filters.processFrameSequence(source, phases, {0, 0, 25})
            || !phases.isEmpty() || filters.lastError().isEmpty()) {
            std::cerr << "unsupported plugin contract was silently accepted\n"; return 1;
        }
    }
    filters.clearFilters();
    if (!filters.addPluginFilter(found->id) || filters.processFrame(source, {0, 0, 25}).isNull()) return 1;
    QImage highDepth = source.convertToFormat(QImage::Format_RGBA64);
    if (filters.processFrameSequence(highDepth, phases, {1, 0.04, 25})
        || !phases.isEmpty() || !filters.lastError().contains("32-bit RGB")) {
        std::cerr << "existing runtime silently quantized high-depth input\n"; return 1;
    }
    const auto moduleBalance = reinterpret_cast<int (*)()>(module.resolve("VDQtTestModuleBalance"));
    filters.clearFilters();
    if (!aspectContracts(catalog, module, argc == 2)) return 1;
    for (int reload = 0; reload < 12; ++reload) {
        VDQtPluginHost::instance().reload();
        if (!moduleBalance || moduleBalance() != 1 || liveInstances() != 0
            || VDQtPluginHost::instance().videoFilters().size() < 4) {
            std::cerr << "module reload failed balanced teardown/rediscovery\n"; return 1;
        }
    }
    return 0;
}
