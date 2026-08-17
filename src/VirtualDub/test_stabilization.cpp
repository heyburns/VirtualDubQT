#include "VDQtFilterSystem.h"
#include "VDQtVideoDecoder.h"
#include <vd2/system/atomic.h>
#include <vd2/system/binary.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>
#include <array>
#include <cmath>

namespace {

bool require(bool condition, const char *message) {
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

bool isMostlyRed(const QColor& color) {
    return color.red() > 220 && color.green() < 35 && color.blue() < 35;
}

bool isMostlyBlue(const QColor& color) {
    return color.blue() > 220 && color.green() < 35 && color.red() < 35;
}

} // namespace

int main(int argc, char **argv) {
    QTemporaryDir settingsDirectory;
    if (!require(settingsDirectory.isValid(), "temporary settings directory"))
        return 1;

    // QSettings' native Unix backend uses XDG_CONFIG_HOME. Set it before
    // constructing QCoreApplication so this test never touches user settings.
    qputenv("XDG_CONFIG_HOME", settingsDirectory.path().toUtf8());
    QCoreApplication application(argc, argv);

    QImage source(8, 8, QImage::Format_RGB888);
    source.fill(QColor(16, 32, 64));

    {
        VDQtFilterSystem filters;
        filters.clearFilters();
        filters.addFilter(VDFilterType::BobDoubler);
        QList<QImage> phases;
        if (!require(filters.getTimingInfo().outputFramesPerInput == 2,
                     "bob filter advertises two output frames"))
            return 1;
        if (!require(filters.processFrameSequence(source, phases) && phases.size() == 2,
                     "bob filter emits two output phases"))
            return 1;
        if (!require(!phases[0].isNull() && !phases[1].isNull(),
                     "bob output phases are valid"))
            return 1;
    }

    {
        VDQtFilterSystem restored;
        if (!require(restored.getActiveChain().size() == 1,
                     "filter chain persists and reloads"))
            return 1;
        if (!require(restored.getActiveChain().first().type == VDFilterType::BobDoubler,
                     "persisted filter type is retained"))
            return 1;
        restored.clearFilters();
        restored.saveSettings();
    }

    {
        std::array<uint8, 24> storage{};
        void *unaligned = storage.data() + 1;
        constexpr double expectedDouble = -12345.6789012345;
        constexpr float expectedFloat = 98.125f;
        VDWriteUnalignedBED(unaligned, expectedDouble);
        VDWriteUnalignedBEF(storage.data() + 11, expectedFloat);
        if (!require(VDReadUnalignedBED(unaligned) == expectedDouble,
                     "unaligned big-endian double round-trip"))
            return 1;
        if (!require(VDReadUnalignedBEF(storage.data() + 11) == expectedFloat,
                     "unaligned big-endian float round-trip"))
            return 1;
    }

    {
        VDAtomicBool atomicBool(true);
        if (!require(atomicBool.xchg(false), "atomic bool exchange returns prior true value"))
            return 1;
        if (!require(!static_cast<bool>(atomicBool), "atomic bool exchange can store false"))
            return 1;
        if (!require(!atomicBool.xchg(true), "atomic bool exchange returns prior false value"))
            return 1;

        VDAtomicFloat atomicFloat(1.25f);
        if (!require(atomicFloat.xchg(-7.5f) == 1.25f,
                     "atomic float exchange returns the prior value"))
            return 1;
        if (!require(static_cast<float>(atomicFloat) == -7.5f,
                     "atomic float exchange stores the new value"))
            return 1;
    }

    {
        const QString firstSource = settingsDirectory.filePath(QStringLiteral("dependency-one.avi"));
        const QString secondSource = settingsDirectory.filePath(QStringLiteral("dependency-two.wav"));
        const QString patternedSource1 = settingsDirectory.filePath(QStringLiteral("sequence_001.png"));
        const QString patternedSource2 = settingsDirectory.filePath(QStringLiteral("sequence_002.png"));
        const QString nestedSource = settingsDirectory.filePath(QStringLiteral("nested-source.mkv"));
        const QString importedScript = settingsDirectory.filePath(QStringLiteral("sources.avsi"));
        const QString scriptPath = settingsDirectory.filePath(QStringLiteral("dependencies.avs"));
        if (!require(writeFile(firstSource, QByteArray("video")), "write first script dependency")
            || !require(writeFile(secondSource, QByteArray("audio")), "write second script dependency")
            || !require(writeFile(patternedSource1, QByteArray("image1")), "write first patterned dependency")
            || !require(writeFile(patternedSource2, QByteArray("image2")), "write second patterned dependency")
            || !require(writeFile(nestedSource, QByteArray("nested")), "write nested dependency")
            || !require(writeFile(importedScript,
                                  QByteArray("BestSource(source=\"nested-source.mkv\")\n")),
                        "write imported source script")
            || !require(writeFile(scriptPath,
                                  QByteArray("Import(\"sources.avsi\")\n"
                                             "video_path=\"dependency-one.avi\"\n"
                                             "v=AVISource(video_path)\n"
                                             "a=FFAudioSource(source=\"dependency-two.wav\")\n"
                                             "images=ImageSource(\"sequence_%03d.png\")\n"
                                             "AudioDub(v,a)\n")),
                        "write multiple-dependency AviSynth script"))
            return 1;
        const QStringList dependencies = VDQtVideoDecoder::parseScriptSources(scriptPath);
        if (!require(dependencies.contains(QFileInfo(firstSource).absoluteFilePath())
                     && dependencies.contains(QFileInfo(secondSource).absoluteFilePath())
                     && dependencies.contains(QFileInfo(patternedSource1).absoluteFilePath())
                     && dependencies.contains(QFileInfo(patternedSource2).absoluteFilePath())
                     && dependencies.contains(QFileInfo(importedScript).absoluteFilePath())
                     && dependencies.contains(QFileInfo(nestedSource).absoluteFilePath()),
                     "script variables, named paths, patterns, and imports are discovered"))
            return 1;

        const QString vapourSynthScript = settingsDirectory.filePath(QStringLiteral("dependencies.vpy"));
        if (!require(writeFile(vapourSynthScript,
                               QByteArray("source = 'dependency-one.avi'\n"
                                          "clip = core.ffms2.Source(source=source)\n")),
                     "write VapourSynth dependency script")
            || !require(VDQtVideoDecoder::parseScriptSources(vapourSynthScript)
                            .contains(QFileInfo(firstSource).absoluteFilePath()),
                        "VapourSynth path variables are discovered"))
            return 1;
    }

    {
        // Packed AviSynth RGB frames use Windows DIB orientation (bottom-up),
        // unlike YUV/YUY2. A vertically asymmetric clip catches accidental flips.
        const QString originalDirectory = QDir::currentPath();
        const struct {
            const char *pixelType;
            const char *fileName;
        } formats[] = {
            { "RGB32", "orientation_rgb32.avs" },
            { "RGB24", "orientation_rgb24.avs" },
        };

        for (const auto& format : formats) {
            const QString scriptPath = settingsDirectory.filePath(QString::fromLatin1(format.fileName));
            const QByteArray script = QByteArray("top = BlankClip(length=1, width=32, height=16, pixel_type=\"")
                + format.pixelType
                + "\", color=$FF0000)\n"
                  "bottom = BlankClip(length=1, width=32, height=16, pixel_type=\""
                + format.pixelType
                + "\", color=$0000FF)\n"
                  "StackVertical(top, bottom)\n";
            if (!require(writeFile(scriptPath, script), "write AviSynth RGB orientation script"))
                return 1;

            VDQtVideoDecoder decoder;
            if (!require(decoder.openFile(scriptPath), "open AviSynth RGB orientation script")) {
                std::cerr << decoder.getLastError().toStdString() << '\n';
                return 1;
            }

            const QImage frame = decoder.getFrameImage(0);
            if (!require(!frame.isNull(), "render AviSynth packed RGB frame"))
                return 1;
            if (!require(isMostlyRed(frame.pixelColor(16, 4)), "AviSynth packed RGB top remains red"))
                return 1;
            if (!require(isMostlyBlue(frame.pixelColor(16, 27)), "AviSynth packed RGB bottom remains blue"))
                return 1;
            if (!require(QDir::currentPath() == originalDirectory,
                         "AviSynth import restores process working directory"))
                return 1;
        }
    }

    {
        constexpr int generatedFrameCount = 80;
        const QString scriptPath = settingsDirectory.filePath(QStringLiteral("cache_budget.avs"));
        const QByteArray script =
            "BlankClip(length=80, width=512, height=512, pixel_type=\"RGB32\", color=$123456)\n";
        if (!require(writeFile(scriptPath, script), "write frame-cache budget script"))
            return 1;

        VDQtVideoDecoder decoder;
        if (!require(decoder.openFile(scriptPath), "open frame-cache budget script"))
            return 1;
        for (int frameIndex = 0; frameIndex < generatedFrameCount; ++frameIndex) {
            if (!require(!decoder.getFrameImage(frameIndex).isNull(), "render frame-cache budget frame"))
                return 1;
        }

        if (!require(decoder.getCachedFrameCostKiB() <= decoder.getFrameCacheBudgetKiB(),
                     "decoded frame cache stays within its memory budget"))
            return 1;
        if (!require(decoder.getCachedFrameCount() < generatedFrameCount,
                     "decoded frame cache evicts by image memory cost"))
            return 1;
        if (!require(decoder.getFrameImage(generatedFrameCount).isNull(),
                     "exact out-of-range frame requests fail instead of aliasing the final frame"))
            return 1;
        decoder.clearCache();
        if (!require(decoder.getCachedFrameCostKiB() == 0 && decoder.getCachedFrameCount() == 0,
                     "decoded frame cache releases all accounted memory"))
            return 1;
    }

    std::cout << "stabilization tests passed\n";
    return 0;
}
