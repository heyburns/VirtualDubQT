#include "VDQtScriptEngine.h"

#include <QCoreApplication>

#include <iostream>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    VDQtScriptProgram program;
    QString error;
    const QString script = QStringLiteral(
        "// VirtualDub project (Sylia script format)\n"
        "VirtualDub.Open(\"clip\\\\name.avi\", \"\", 0);\n"
        "VirtualDub.video.SetMode(3);\n"
        "declare width = 160 * 4;\n"
        "declare filter = VirtualDub.video.filters.Add(\"resize\");\n"
        "VirtualDub.video.filters.instance[filter].Config(width, 360, \"bicubic\");\n"
        "declare curve = VirtualDub.video.filters.instance[0].AddOpacityCurve();\n"
        "curve.AddPoint(0.0, 0.25, 1);\n"
        "curve.AddPoint(20.0, 1.0, 0);\n"
        "declare suffix = \".mkv\";\n"
        "VirtualDub.SaveAVI(\"out\" + suffix);\n");
    if (!VDQtScriptEngine::parseText(script, QStringLiteral("/tmp"),
                                     &program, &error)) {
        std::cerr << error.toStdString() << '\n';
        return 1;
    }
    if (program.commands.size() != 8
        || program.commands.at(0).name != QStringLiteral("Open")
        || program.commands.at(0).arguments.at(0).toString()
            != QStringLiteral("clip\\name.avi")
        || program.commands.at(1).arguments.at(0).toLongLong() != 3
        || program.commands.at(2).name != QStringLiteral("video.filters.Add")
        || program.commands.at(3).name
            != QStringLiteral("video.filters.instance[0].Config")
        || program.commands.at(3).arguments.at(0).toLongLong() != 640
        || program.commands.at(4).name
            != QStringLiteral("video.filters.instance[0].AddOpacityCurve")
        || program.commands.at(5).name
            != QStringLiteral("video.filters.instance[0].OpacityCurve.AddPoint")
        || program.commands.at(5).arguments.at(1).toDouble() != 0.25
        || program.commands.at(7).name != QStringLiteral("SaveAVI")
        || program.commands.at(7).arguments.first().toString()
            != QStringLiteral("out.mkv")) {
        std::cerr << "parsed command stream did not match\n";
        return 1;
    }
    if (VDQtScriptEngine::parseText(
            QStringLiteral("system(\"rm\");"), QStringLiteral("/tmp"),
            &program, &error)) {
        std::cerr << "non-VirtualDub statement was accepted\n";
        return 1;
    }
    if (VDQtScriptEngine::parseText(
            QStringLiteral("declare value = VirtualDub.video.SetMode(3);"),
            QStringLiteral("/tmp"), &program, &error)) {
        std::cerr << "an unsupported return-value assignment was accepted\n";
        return 1;
    }
    if (VDQtScriptEngine::parseText(
            QStringLiteral("curve.AddPoint(0, 1, 1);"),
            QStringLiteral("/tmp"), &program, &error)) {
        std::cerr << "an undeclared object alias was accepted\n";
        return 1;
    }
    if (VDQtScriptEngine::parseText(
            QStringLiteral("declare n = 9223372036854775807 + 1;"),
            QStringLiteral("/tmp"), &program, &error)) {
        std::cerr << "an overflowing integer expression was accepted\n";
        return 1;
    }

    const QString generatedProjectSurface = QStringLiteral(
        "VirtualDub.audio.SetSource(1, 0);\n"
        "VirtualDub.audio.SetMode(1);\n"
        "VirtualDub.audio.SetInterleave(1, 500, 1, 1, -20);\n"
        "VirtualDub.audio.SetClipMode(1, 1);\n"
        "VirtualDub.audio.SetEditMode(1);\n"
        "VirtualDub.audio.SetConversion(48000, 16, 2, 0, 1);\n"
        "VirtualDub.audio.SetVolume(192);\n"
        "VirtualDub.audio.SetCompressionWithHint(255, 48000, 2, 16, 24000, 4, \"aac\");\n"
        "VirtualDub.audio.SetCompData(4, \"AAAAAA==\");\n"
        "VirtualDub.audio.EnableFilterGraph(1);\n"
        "VirtualDub.video.SetInputFormat(7);\n"
        "VirtualDub.video.SetInputMatrix(2, 1);\n"
        "VirtualDub.video.SetOutputFormat(57);\n"
        "VirtualDub.video.SetOutputMatrix(2, 2);\n"
        "VirtualDub.video.SetOutputReference(1);\n"
        "VirtualDub.video.SetMode(3);\n"
        "VirtualDub.video.SetSmartRendering(1);\n"
        "VirtualDub.video.SetPreserveEmptyFrames(1);\n"
        "VirtualDub.video.SetFrameRate2(30000, 1001, 1);\n"
        "VirtualDub.video.SetTargetFrameRate(24000, 1001);\n"
        "VirtualDub.video.SetIVTC(0, 0, 0, 0);\n"
        "VirtualDub.video.SetCompression(0x34363248, 250, 10000, 750000);\n"
        "VirtualDub.video.SetCompData(4, \"AAAAAA==\");\n"
        "VirtualDub.SaveFormat(\"matroska\", \"mkv\");\n"
        "VirtualDub.SaveAudioFormat(\"wav\");\n"
        "VirtualDub.video.filters.BeginUpdate();\n"
        "VirtualDub.video.filters.Clear();\n"
        "declare vf = VirtualDub.video.filters.Add(\"resize\");\n"
        "VirtualDub.video.filters.instance[vf].Config(640, 360, \"bicubic\");\n"
        "VirtualDub.video.filters.instance[vf].SetClipping(2, 2, 2, 2);\n"
        "VirtualDub.video.filters.instance[vf].SetForceSingleFBEnabled(true);\n"
        "VirtualDub.video.filters.instance[vf].DataPrefix(\"resize0\");\n"
        "VirtualDub.video.filters.instance[vf].SetEnabled(true);\n"
        "VirtualDub.video.filters.instance[vf].SetOutputName(\"scaled\");\n"
        "VirtualDub.video.filters.instance[vf].SetOpacityClipping(1, 1, 1, 1);\n"
        "VirtualDub.video.filters.instance[vf].SetRangeFrames(4, 40);\n"
        "declare opacity = VirtualDub.video.filters.instance[vf].AddOpacityCurve();\n"
        "opacity.AddPoint(4, 0.0, 1);\n"
        "opacity.AddPoint(40, 1.0, 1);\n"
        "VirtualDub.video.filters.EndUpdate();\n"
        "VirtualDub.audio.filters.Clear();\n"
        "declare af = VirtualDub.audio.filters.Add(\"gain\");\n"
        "VirtualDub.audio.filters.instance[af].SetDouble(0, 5e-1);\n"
        "VirtualDub.audio.filters.instance[af].SetInt(1, 7 | 8);\n"
        "VirtualDub.audio.filters.instance[af].SetLong(2, 1234567890123);\n"
        "VirtualDub.audio.filters.instance[af].SetString(3, \"value\");\n"
        "VirtualDub.audio.filters.instance[af].SetRaw(4, 4, \"AAAAAA==\");\n"
        "VirtualDub.subset.Clear();\n"
        "VirtualDub.subset.AddRange(0, 10);\n"
        "VirtualDub.subset.AddMaskedRange(10, 5);\n"
        "VirtualDub.video.SetRangeFrames(0, 15);\n"
        "VirtualDub.video.SetZoomFrames(0, 15);\n"
        "VirtualDub.video.AddMarker(5);\n"
        "VirtualDub.project.ClearTextInfo();\n"
        "VirtualDub.project.AddTextInfo(\"INAM\", \"Generated project\");\n");
    if (!VDQtScriptEngine::parseText(
            generatedProjectSurface, QStringLiteral("/tmp"),
            &program, &error)
        || program.commands.size() < 50) {
        std::cerr << "official generated-project surface did not parse: "
                  << error.toStdString() << '\n';
        return 1;
    }
    return 0;
}
