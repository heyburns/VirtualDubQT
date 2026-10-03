// Existing filter behavior contracts, independent of codec troubleshooting.
// Keep incoming-history/pixel-layout repairs separate from algorithm-parity work.
#include "VirtualDub/VDQtFilterSystem.h"
#include "VirtualDub/VDQtFilterValidation.h"
#include "VirtualDub/VDQtProjectFile.h"

#include <QCoreApplication>
#include <QTransform>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool fieldHistory() {
    for (const auto type : {VDFilterType::FieldDelay, VDFilterType::Interlace}) {
        for (const auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888,
                                  QImage::Format_RGBA64}) {
            for (int parity : {0, 1}) {
                VDQtFilterSystem filters;
                filters.addFilter(type);
                auto params = filters.getActiveChain().first().params;
                params[type == VDFilterType::FieldDelay ? "field" : "fieldOrder"] = parity;
                filters.updateFilterParams(0, params);
                QImage previousInput;
                for (int frame = 0; frame < 4; ++frame) {
                    QImage input(17, 9, format);
                    input.fill(QColor(40 * (frame + 1), 30, 60, 80 + frame * 20));
                    const QImage original = input.copy();
                    const QImage output = filters.processFrame(input, {frame, frame / 25.0, 25});
                    if (!check(!output.isNull() && input == original,
                               "field processing leaves caller-owned input unchanged")) return false;
                    for (int y = 0; y < input.height(); ++y) {
                        const QImage& expected = !previousInput.isNull() && (y & 1) == parity
                            ? previousInput : input;
                        if (!check(output.pixelColor(5, y) == expected.pixelColor(5, y),
                                   "delayed field comes from the preceding incoming stage frame")) return false;
                    }
                    previousInput = input;
                }
                QImage discontinuous(17, 9, format);
                discontinuous.fill(QColor(210, 80, 40, 150));
                if (!check(filters.processFrame(discontinuous, {100, 4, 25}) == discontinuous,
                           "field history does not cross a seek discontinuity")) return false;
            }
        }
    }
    return true;
}

bool rotatedLayout() {
    for (const auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888,
                              QImage::Format_RGBA64}) {
        QImage input(33, 29, format);
        input.fill(QColor(255, 0, 0, 180));
        VDQtFilterSystem rotate;
        rotate.addFilter(VDFilterType::Rotate2);
        auto params = rotate.getActiveChain().first().params;
        params["angle"] = 15;
        rotate.updateFilterParams(0, params);
        const QImage rotated = rotate.processFrame(input);
        if (!check(!rotated.isNull(), "rotation produces an image")) return false;
        VDQtFilterSystem grayscale;
        grayscale.addFilter(VDFilterType::Grayscale);
        const QImage expected = grayscale.processFrame(rotated);
        rotate.addFilter(VDFilterType::Grayscale);
        const QImage actual = rotate.processFrame(input);
        if (!check(actual == expected,
                   "Rotate2 followed by grayscale respects actual layout and straight alpha")) return false;
    }
    return true;
}

bool expandedSequence() {
    for (const auto format : {QImage::Format_RGB888, QImage::Format_RGBA8888, QImage::Format_RGBA64}) {
        VDQtFilterSystem combined, bobOnly, downstream;
        combined.addFilter(VDFilterType::BobDoubler);
        combined.addFilter(VDFilterType::FieldDelay);
        bobOnly.addFilter(VDFilterType::BobDoubler);
        downstream.addFilter(VDFilterType::FieldDelay);
        for (int frame = 0; frame < 4; ++frame) {
            QImage input(17, 6, format);
            for (int y = 0; y < input.height(); ++y)
                for (int x = 0; x < input.width(); ++x)
                    input.setPixelColor(x, y, QColor((y & 1 ? 120 : 40) + frame * 10, 50, 80, 180));
            const QImage original = input.copy();
            QList<QImage> phases, actual;
            if (!bobOnly.processFrameSequence(input, phases, {frame, frame / 25.0, 25})
                || !combined.processFrameSequence(input, actual, {frame, frame / 25.0, 25})
                || phases.size() != 2 || actual.size() != 2) return false;
            for (int phase = 0; phase < 2; ++phase) {
                const QImage expected = downstream.processFrame(phases.at(phase),
                    {frame * 2 + phase, (frame * 2 + phase) / 50.0, 50});
                if (!check(actual.at(phase) == expected,
                           "post-Bob field history follows the preceding emitted frame, not the same phase of the previous input")) return false;
            }
            if (!check(input == original, "sequence expansion leaves the source unchanged")) return false;
        }
    }
    QImage input(17, 6, QImage::Format_RGB888);
    for (int y = 0; y < input.height(); ++y)
        for (int x = 0; x < input.width(); ++x)
            input.setPixelColor(x, y, QColor(y & 1 ? 120 : 40, 50, 80));
    VDQtFilterSystem bob, timed, invert, stacked;
    bob.addFilter(VDFilterType::BobDoubler);
    timed.addFilter(VDFilterType::BobDoubler);
    timed.addFilter(VDFilterType::InvertColor);
    auto params = timed.getActiveChain().last().params;
    params["_sylia.range.start"] = 1;
    params["_sylia.range.end"] = 2;
    timed.updateFilterParams(1, params);
    invert.addFilter(VDFilterType::InvertColor);
    stacked.addFilter(VDFilterType::BobDoubler);
    stacked.addFilter(VDFilterType::BobDoubler);
    QList<QImage> phases, outputs, expanded;
    if (!bob.processFrameSequence(input, phases, {0, 0, 25})
        || !timed.processFrameSequence(input, outputs, {0, 0, 25})
        || !stacked.processFrameSequence(input, expanded, {0, 0, 25})) return false;
    if (!check(outputs.size() == 2 && outputs.first() == phases.first()
               && outputs.last() == invert.processFrame(phases.last()),
               "downstream timed ranges use emitted frame ordinals")) return false;
    if (!check(expanded.size() == 4 && expanded.at(0) == phases.first()
               && expanded.at(1) == phases.first() && expanded.at(2) == phases.last()
               && expanded.at(3) == phases.last(), "stacked expansion preserves chronological order")) return false;
    QList<QImage> aliased{input};
    if (!check(timed.processFrameSequence(aliased.first(), aliased, {0, 0, 25}) && aliased == outputs,
               "an input owned by the output container stays alive while replacing that container")) return false;
    VDQtFilterSystem excessive;
    for (int index = 0; index < 6; ++index) {
        excessive.addFilter(VDFilterType::BobDoubler);
        auto duplicate = excessive.getActiveChain().last().params;
        duplicate["mode"] = 4;
        excessive.updateFilterParams(index, duplicate);
    }
    // Pure duplicate phases share this allocation even in the unfixed code;
    // the logical sequence exceeds the budget without an expensive OOM test.
    QImage large(1025, 1025, QImage::Format_RGBA64);
    large.fill(Qt::black);
    return check(!excessive.processFrameSequence(large, outputs, {0, 0, 25})
                 && outputs.isEmpty() && excessive.lastError().contains("budget"),
                 "expansion enforces a byte budget and returns no partial sequence");
}

bool requiredEffects() {
    QImage input(17, 9, QImage::Format_RGBA8888);
    input.fill(QColor(100, 80, 60, 150));
    VDFilterInstance missing;
    missing.id = "missing-effect-instance";
    missing.name = "Required missing effect";
    missing.type = VDFilterType::Plugin;
    missing.enabled = true;
    missing.pluginId = "vdqt-regression-unavailable-plugin";
    VDQtFilterSystem filters;
    filters.replaceActiveChain({missing});
    QList<QImage> outputs{input};
    if (!check(!filters.processFrameSequence(input, outputs) && outputs.isEmpty(),
               "enabled missing plugin must fail, not silently produce unchanged output")) return false;
    if (!check(filters.processingError().filterId == missing.id
               && filters.lastError().contains(missing.name),
               "processing error retains the failed configuration identity")) return false;
    filters.clearFilters();
    filters.addFilter(VDFilterType::Logo);
    QTemporaryDir assets;
    if (!assets.isValid()) return false;
    const QString assetPath = assets.filePath("required-logo.png");
    filters.updateFilterStringParams(0, {{"path", assetPath}});
    if (!check(!filters.processFrameSequence(input, outputs) && outputs.isEmpty(),
               "missing required logo asset must fail, not disappear from output")) return false;
    if (!check(filters.lastError().contains(assetPath), "asset failure reports the exact path")) return false;
    filters.setFilterEnabled(0, false);
    if (!check(filters.processFrameSequence(input, outputs) && outputs.size() == 1
               && outputs.first() == input && filters.lastError().isEmpty(),
               "explicitly disabled missing effect is harmless")) return false;
    filters.setFilterEnabled(0, true);
    if (!input.save(assetPath)) return false;
    return check(filters.processFrameSequence(input, outputs) && outputs.size() == 1,
                 "creating a previously missing asset permits retry without stale negative cache");
}

bool uniqueIdentities() {
    VDQtFilterSystem reference, migrated;
    reference.addFilter(VDFilterType::MotionBlur);
    reference.addFilter(VDFilterType::MotionBlur);
    auto chain = reference.getActiveChain();
    const QString preserved = chain.first().id;
    chain.last().id = preserved;
    migrated.replaceActiveChain(chain);
    QImage input(17, 9, QImage::Format_RGB888);
    for (int frame = 0; frame < 4; ++frame) {
        input.fill(QColor(frame * 60, frame * 60, frame * 60));
        if (!check(migrated.processFrame(input, {frame, frame / 25.0, 25})
                   == reference.processFrame(input, {frame, frame / 25.0, 25}),
                   "duplicate serialized IDs must not share temporal history")) return false;
    }
    const auto normalized = migrated.getActiveChain();
    if (!check(normalized.first().id == preserved && normalized.last().id != preserved
               && !normalized.last().id.isEmpty(), "migration preserves the first valid identity")) return false;
    migrated.replaceActiveChainTransient(normalized);
    if (!check(migrated.getActiveChain().last().id == normalized.last().id,
               "normalized identities remain stable across worker snapshots")) return false;
    chain.first().id.clear();
    chain.last().id.clear();
    migrated.replaceActiveChainTransient(chain);
    return check(!migrated.getActiveChain().first().id.isEmpty()
                 && migrated.getActiveChain().first().id != migrated.getActiveChain().last().id,
                 "empty legacy IDs are assigned distinct identities");
}

bool parameterValidation() {
    QImage input(17, 9, QImage::Format_RGB888);
    input.fill(QColor(100, 80, 60));
    VDQtFilterSystem filters;
    for (const auto type : {VDFilterType::Levels, VDFilterType::Curves}) {
        filters.clearFilters();
        filters.addFilter(type);
        auto params = filters.getActiveChain().first().params;
        params[type == VDFilterType::Levels ? "inputBlack" : "black"] = 255;
        filters.updateFilterParams(0, params);
        QList<QImage> output{input};
        if (!check(!filters.processFrameSequence(input, output) && output.isEmpty()
                   && !filters.lastError().isEmpty(),
                   "invalid black/white relationships fail before pixel processing")) return false;
        QTemporaryDir directory;
        VDQtProcessingState saved, loaded;
        saved.filters = filters.getActiveChain();
        QString error;
        if (!VDQtProjectFile::saveProcessingSettings(directory.filePath("invalid.vdqsettings"), saved, &error)
            || !check(!VDQtProjectFile::loadProcessingSettings(directory.filePath("invalid.vdqsettings"), &loaded, &error)
                      && error.contains("white", Qt::CaseInsensitive),
                      "invalid saved level relationships fail at the JSON boundary")) return false;
    }
    filters.clearFilters();
    filters.addFilter(VDFilterType::Resize);
    const auto base = filters.getActiveChain().first().params;
    for (const auto& invalid : QList<QPair<QString, double>>{
            {"width", std::numeric_limits<double>::infinity()},
            {"height", std::numeric_limits<double>::quiet_NaN()},
            {"filterMode", 1.5}, {"width", 1e100},
            {"_sylia.range.start", 1e100}, {"codecAdjust", 3}}) {
        auto params = base;
        params[invalid.first] = invalid.second;
        filters.updateFilterParams(0, params);
        QList<QImage> output;
        if (!check(!filters.processFrameSequence(input, output) && output.isEmpty(),
                   "non-finite, oversized and nonintegral filter parameters are rejected")) return false;
    }
    auto tooLarge = base;
    tooLarge["sizeMode"] = 0;
    tooLarge["width"] = 32768;
    tooLarge["height"] = 32768;
    filters.updateFilterParams(0, tooLarge);
    QList<QImage> output;
    if (!check(!filters.processFrameSequence(input, output) && output.isEmpty(),
               "predicted excessive frame allocation is rejected before allocation")) return false;
    filters.setFilterEnabled(0, false);
    if (!check(filters.processFrameSequence(input, output) && output.first() == input,
               "disabled invalid configuration does not affect processing")) return false;
    filters.clearFilters();
    for (int type = int(VDFilterType::SixAxis); type < int(VDFilterType::Count); ++type) {
        if (VDFilterType(type) == VDFilterType::Plugin) continue;
        VDQtFilterSystem factory;
        factory.addFilter(VDFilterType(type));
        QString error;
        if (!check(!factory.getActiveChain().isEmpty()
                   && VDQtValidateFilter(factory.getActiveChain().first(), &error),
                   "all built-in defaults satisfy the shared schema")) return false;
    }
    filters.addFilter(VDFilterType::Levels);
    auto narrow = filters.getActiveChain().first().params;
    narrow["inputBlack"] = 254.99999;
    narrow["inputWhite"] = 255;
    filters.updateFilterParams(0, narrow);
    if (!check(filters.processFrameSequence(input, output),
               "a narrow but valid level interval does not form an invalid clamp")) return false;
    return true;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.contains("field")) return fieldHistory() ? 0 : 1;
    if (args.contains("layout")) return rotatedLayout() ? 0 : 1;
    if (args.contains("failure")) return requiredEffects() ? 0 : 1;
    if (args.contains("sequence")) return expandedSequence() ? 0 : 1;
    if (args.contains("identity")) return uniqueIdentities() ? 0 : 1;
    if (args.contains("validation")) return parameterValidation() ? 0 : 1;
    return fieldHistory() && rotatedLayout() && requiredEffects() && expandedSequence()
        && uniqueIdentities() && parameterValidation() ? 0 : 1;
}
