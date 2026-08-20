#include "VDQtAudioFilterSystem.h"
#include "VDQtFilterSystem.h"
#include "VDQtScriptEngine.h"
#include "VDQtTimeline.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cmath>
#include <cstring>
#include <iostream>

#ifndef VDQT_PARITY_REFERENCE_PATH
#error VDQT_PARITY_REFERENCE_PATH must name the checked-in parity contract
#endif

namespace {

QString argumentContract(const QVariant& value) {
    if (!value.isValid() || value.isNull()) {
        return QStringLiteral("null");
    } else if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("b:true")
                              : QStringLiteral("b:false");
    } else if (value.typeId() == QMetaType::Double) {
        return QStringLiteral("d:")
            + QString::number(value.toDouble(), 'g', 17);
    } else if (value.typeId() == QMetaType::QString) {
        return QStringLiteral("s:") + value.toString();
    } else if (value.typeId() == QMetaType::LongLong
               || value.typeId() == QMetaType::ULongLong
               || value.typeId() == QMetaType::Int
               || value.typeId() == QMetaType::UInt) {
        return QStringLiteral("i:") + QString::number(value.toLongLong());
    }
    return QStringLiteral("s:") + value.toString();
}

QJsonArray rgbaPixels(const QImage& source) {
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    QJsonArray pixels;
    for (int y = 0; y < image.height(); ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            QJsonArray pixel;
            for (int channel = 0; channel < 4; ++channel)
                pixel.append(line[x * 4 + channel]);
            pixels.append(pixel);
        }
    }
    return pixels;
}

QJsonObject buildContract(QString *errorMessage) {
    QJsonObject contract;
    contract[QStringLiteral("schema")] = 1;

    const QString generatedScript = QStringLiteral(
        "VirtualDub.video.SetInputFormat(7);\n"
        "VirtualDub.video.SetInputMatrix(2, 1);\n"
        "VirtualDub.video.SetOutputFormat(57);\n"
        "VirtualDub.video.SetOutputMatrix(2, 2);\n"
        "VirtualDub.video.filters.BeginUpdate();\n"
        "VirtualDub.video.filters.Clear();\n"
        "VirtualDub.video.filters.Add(\"invert\");\n"
        "VirtualDub.video.filters.instance[0].SetClipping(1, 0, 0, 0);\n"
        "VirtualDub.video.filters.instance[0].SetRangeFrames(3, 12);\n"
        "declare curve = VirtualDub.video.filters.instance[0].AddOpacityCurve();\n"
        "curve.AddPoint(3, 0.25, 1);\n"
        "curve.AddPoint(12, 1.0, 1);\n"
        "VirtualDub.video.filters.EndUpdate();\n"
        "VirtualDub.audio.filters.Clear();\n"
        "VirtualDub.audio.filters.Add(\"input\");\n"
        "VirtualDub.audio.filters.Add(\"gain\");\n"
        "VirtualDub.audio.filters.instance[1].SetDouble(0, 0.5);\n"
        "VirtualDub.audio.filters.Add(\"output\");\n"
        "VirtualDub.audio.filters.Connect(0, 0, 1, 0);\n"
        "VirtualDub.audio.filters.Connect(1, 0, 2, 0);\n"
        "VirtualDub.subset.Clear();\n"
        "VirtualDub.subset.AddRange(2, 3);\n"
        "VirtualDub.subset.AddMaskedRange(6, 2);\n"
        "VirtualDub.video.SetRangeFrames(1, 6);\n"
        "VirtualDub.project.AddTextInfo(\"INAM\", \"Parity clip\");\n");
    VDQtScriptProgram program;
    QString parseError;
    if (!VDQtScriptEngine::parseText(generatedScript, QStringLiteral("/fixture"),
                                     &program, &parseError)) {
        if (errorMessage) *errorMessage = parseError;
        return {};
    }
    QJsonArray commands;
    for (const VDQtScriptCommand& command : program.commands) {
        QStringList arguments;
        for (const QVariant& argument : command.arguments)
            arguments.append(argumentContract(argument));
        commands.append(command.name + QLatin1Char('(')
                        + arguments.join(QLatin1Char(',')) + QLatin1Char(')'));
    }
    contract[QStringLiteral("generatedScriptCommands")] = commands;

    VDQtTimeline timeline;
    timeline.reset(12, true);
    QString timelineError;
    if (!timeline.replaceSegments({{2, 3, false}, {6, 2, true},
                                   {9, 2, false}}, &timelineError)) {
        if (errorMessage) *errorMessage = timelineError;
        return {};
    }
    QJsonArray mapping;
    QJsonArray masks;
    for (qint64 frame = 0; frame < timeline.frameCount(); ++frame) {
        mapping.append(timeline.mapOutputToSource(frame));
        masks.append(timeline.isOutputFrameMasked(frame));
    }
    QJsonObject timelineObject;
    timelineObject[QStringLiteral("mapping")] = mapping;
    timelineObject[QStringLiteral("masked")] = masks;
    timelineObject[QStringLiteral("sourceToOutputForward")]
        = timeline.mapSourceToOutput(9, 0, true);
    contract[QStringLiteral("timeline")] = timelineObject;

    QImage input(2, 1, QImage::Format_RGBA8888);
    uchar *inputLine = input.scanLine(0);
    const uchar inputBytes[] = {10, 20, 30, 255, 200, 150, 100, 128};
    std::memcpy(inputLine, inputBytes, sizeof(inputBytes));

    VDQtFilterSystem filters;
    filters.addFilter(VDFilterType::InvertColor);
    const QImage inverted = filters.processFrame(input, {5, 0.2, 25.0});
    QJsonObject filterObject;
    filterObject[QStringLiteral("invertRgba")] = rgbaPixels(inverted);

    QList<VDFilterInstance> clippedChain = filters.getActiveChain();
    clippedChain[0].params[QStringLiteral("_sylia.clip.left")] = 1.0;
    clippedChain[0].params[QStringLiteral("_sylia.range.start")] = 3.0;
    clippedChain[0].params[QStringLiteral("_sylia.range.end")] = 8.0;
    filters.replaceActiveChainTransient(clippedChain);
    const QImage beforeRange = filters.processFrame(input, {2, 0.08, 25.0});
    const QImage insideRange = filters.processFrame(input, {3, 0.12, 25.0});
    filterObject[QStringLiteral("outsideRangeRgba")] = rgbaPixels(beforeRange);
    filterObject[QStringLiteral("clippedInsideRangeRgba")] = rgbaPixels(insideRange);
    filterObject[QStringLiteral("clippedWidth")] = insideRange.width();

    QList<VDFilterInstance> opacityChain = filters.getActiveChain();
    opacityChain[0].params.remove(QStringLiteral("_sylia.clip.left"));
    opacityChain[0].params.remove(QStringLiteral("_sylia.range.start"));
    opacityChain[0].params.remove(QStringLiteral("_sylia.range.end"));
    opacityChain[0].params[QStringLiteral("_sylia.opacity.count")] = 2.0;
    opacityChain[0].params[QStringLiteral("_sylia.opacity.0.x")] = 0.0;
    opacityChain[0].params[QStringLiteral("_sylia.opacity.0.y")] = 0.0;
    opacityChain[0].params[QStringLiteral("_sylia.opacity.0.linear")] = 1.0;
    opacityChain[0].params[QStringLiteral("_sylia.opacity.1.x")] = 10.0;
    opacityChain[0].params[QStringLiteral("_sylia.opacity.1.y")] = 1.0;
    opacityChain[0].params[QStringLiteral("_sylia.opacity.1.linear")] = 1.0;
    filters.replaceActiveChainTransient(opacityChain);
    filterObject[QStringLiteral("halfOpacityRgba")] = rgbaPixels(
        filters.processFrame(input, {5, 0.2, 25.0}));
    contract[QStringLiteral("filters")] = filterObject;

    VDAudioFilterInstance gain =
        VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::Gain);
    gain.params[QStringLiteral("decibels")] = 20.0 * std::log10(2.0);
    VDQtAudioFilterProcessor audioProcessor;
    audioProcessor.configure({gain}, 48000, 1);
    qint16 samples[] = {1000, -1000, 20000, -20000};
    audioProcessor.processInt16(reinterpret_cast<char *>(samples),
                                sizeof(samples));
    QJsonArray audioSamples;
    for (qint16 sample : samples) audioSamples.append(sample);
    contract[QStringLiteral("audioGainSamples")] = audioSamples;
    return contract;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QString error;
    const QJsonObject actual = buildContract(&error);
    if (actual.isEmpty()) {
        std::cerr << error.toStdString() << '\n';
        return 1;
    }
    const QJsonDocument actualDocument(actual);
    if (application.arguments().contains(QStringLiteral("--emit"))) {
        std::cout << actualDocument.toJson(QJsonDocument::Indented).toStdString();
        return 0;
    }
    QString referencePath = QStringLiteral(VDQT_PARITY_REFERENCE_PATH);
    const int referenceOption = application.arguments().indexOf(
        QStringLiteral("--reference"));
    if (referenceOption >= 0
        && referenceOption + 1 < application.arguments().size()) {
        referencePath = application.arguments().at(referenceOption + 1);
    }
    QFile referenceFile(referencePath);
    if (!referenceFile.open(QIODevice::ReadOnly)) {
        std::cerr << "could not open parity reference: "
                  << referenceFile.errorString().toStdString() << '\n';
        return 1;
    }
    QJsonParseError parseError;
    const QJsonDocument reference = QJsonDocument::fromJson(
        referenceFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !reference.isObject()) {
        std::cerr << "invalid parity reference: "
                  << parseError.errorString().toStdString() << '\n';
        return 1;
    }
    if (reference.object() != actual) {
        std::cerr << "VirtualDub parity contract differs from the reference.\n"
                  << "Run parity_contract_tests --emit to inspect the native result.\n";
        return 1;
    }
    return 0;
}
