#include <array>
#include <cmath>
#include <iostream>

#include "VDQtFilterSystem.h"
#include "VDQtFrameDecodeWorker.h"
#include "VDQtAudioPlayer.h"
#include "VDQtCodecEngine.h"
#include "VDQtCodecSettings.h"
#include "VDQtPositionControl.h"
#include "VDQtVideoDecoder.h"
#include "VDQtVideoExporter.h"
#include <vd2/system/atomic.h>
#include <vd2/system/binary.h>

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

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

bool runProcess(const QString& program,
                const QStringList& arguments,
                QByteArray *errorOutput = nullptr,
                QByteArray *standardOutput = nullptr) {
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    if (errorOutput) *errorOutput = process.readAllStandardError();
    if (standardOutput) *standardOutput = process.readAllStandardOutput();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool containsOptionValue(const QStringList& arguments,
                         const QString& option,
                         const QString& value) {
    const int optionIndex = arguments.indexOf(option);
    return optionIndex >= 0
        && optionIndex + 1 < arguments.size()
        && arguments[optionIndex + 1] == value;
}

} // namespace

int main(int argc, char **argv) {
    QTemporaryDir settingsDirectory;
    if (!require(settingsDirectory.isValid(), "temporary settings directory"))
        return 1;

    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    QApplication application(argc, argv);

    {
        VDQtPositionControlWidget positionControl;
        int dispatchCount = 0;
        int dispatchedPosition = -1;
        QObject::connect(&positionControl, &VDQtPositionControlWidget::positionChanged,
                         [&](int position) {
                             ++dispatchCount;
                             dispatchedPosition = position;
                         });
        positionControl.SetRange(0, 100, true);
        positionControl.SetPosition(12);

        // A programmatic move used to emit once directly and once again through
        // QSlider::valueChanged after the scrub debounce expired.
        QEventLoop settleLoop;
        QTimer::singleShot(40, &settleLoop, &QEventLoop::quit);
        settleLoop.exec();
        if (!require(dispatchCount == 1 && dispatchedPosition == 12,
                     "programmatic playhead movement dispatches exactly once"))
            return 1;
    }

    QImage source(8, 8, QImage::Format_RGB888);
    source.fill(QColor(16, 32, 64));

    {
        QImage highPrecision(4, 4, QImage::Format_RGBA64);
        highPrecision.fill(QColor::fromRgba64(1000, 2000, 3000, 4000));

        VDFilterInstance resize;
        resize.id = QStringLiteral("test-resize");
        resize.name = QStringLiteral("Resize");
        resize.type = VDFilterType::Resize;
        resize.enabled = true;
        resize.params.insert(QStringLiteral("width"), 8.0);
        resize.params.insert(QStringLiteral("height"), 8.0);

        VDQtFilterSystem geometricFilters;
        geometricFilters.replaceActiveChainTransient({ resize });
        const QImage resized = geometricFilters.processFrame(highPrecision);
        if (!require(!resized.isNull() && resized.depth() == 64,
                     "geometric filters retain high-bit-depth frame storage"))
            return 1;

        VDFilterInstance grayscale;
        grayscale.id = QStringLiteral("test-grayscale");
        grayscale.name = QStringLiteral("Grayscale");
        grayscale.type = VDFilterType::Grayscale;
        grayscale.enabled = true;
        geometricFilters.replaceActiveChainTransient({ grayscale });
        if (!require(geometricFilters.processFrame(highPrecision).isNull(),
                     "byte-oriented filters reject high-depth input instead of quantizing it"))
            return 1;

        QImage alphaImage(2, 2, QImage::Format_RGBA8888);
        alphaImage.fill(QColor(20, 80, 140, 73));
        const QImage grayscaleAlpha = geometricFilters.processFrame(alphaImage);
        if (!require(!grayscaleAlpha.isNull()
                     && grayscaleAlpha.hasAlphaChannel()
                     && grayscaleAlpha.pixelColor(0, 0).alpha() == 73,
                     "8-bit pixel filters preserve source alpha"))
            return 1;
    }

    {
        VDQtFilterSystem& filters = VDQtFilterSystem::instance();
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
        if (!require(VDQtFilterSystem::instance().getActiveChain().size() == 1,
                     "filter chain persists for the application session"))
            return 1;

        VDQtFilterSystem freshSession;
        if (!require(freshSession.getActiveChain().isEmpty(),
                     "a new filter session starts without the previous chain"))
            return 1;
        filters.clearFilters();
    }

    {
        VDQtCodecEngine& codecs = VDQtCodecEngine::instance();
        codecs.resetToDefaults();
        VDVideoCodecParams videoParams =
            VDQtCodecEngine::getDefaultVideoParamsForCodec(QStringLiteral("libx264"));
        videoParams.crf = 17;
        codecs.setVideoParams(videoParams);
        if (!require(VDQtCodecEngine::instance().getVideoParams().codecId == QStringLiteral("libx264")
                     && VDQtCodecEngine::instance().getVideoParams().crf == 17,
                     "compression choices persist for the application session"))
            return 1;

        VDQtCodecEngine freshCodecSession;
        if (!require(freshCodecSession.getVideoParams().codecId == QStringLiteral("prores_ks"),
                     "a new compression session starts at defaults"))
            return 1;

        VDQtCodecSettings& codecSettings = VDQtCodecSettings::instance();
        codecSettings.resetToDefaults();
        VDAudioCodecConfig audioConfig = codecSettings.getAudioConfig();
        audioConfig.bitrateKbps = 256;
        codecSettings.setAudioConfig(audioConfig);
        if (!require(VDQtCodecSettings::instance().getAudioConfig().bitrateKbps == 256,
                     "audio compression choices persist for the application session"))
            return 1;

        VDAudioCodecParams directCopyAudio;
        directCopyAudio.codecId = QStringLiteral("aac");
        directCopyAudio.rateMode = QStringLiteral("cbr");
        directCopyAudio.bitrateKbps = 96;
        directCopyAudio.sampleRate = 44100;
        directCopyAudio.channels = 1;
        const QStringList directCopyAudioArgs =
            VDQtCodecEngine::buildFfmpegAudioEncodeArguments(directCopyAudio);
        if (!require(containsOptionValue(directCopyAudioArgs,
                                         QStringLiteral("-b:a"), QStringLiteral("96k"))
                     && containsOptionValue(directCopyAudioArgs,
                                            QStringLiteral("-ar"), QStringLiteral("44100"))
                     && containsOptionValue(directCopyAudioArgs,
                                            QStringLiteral("-ac"), QStringLiteral("1")),
                     "all configured direct-copy video audio options reach FFmpeg"))
            return 1;

        directCopyAudio.rateMode = QStringLiteral("vbr");
        directCopyAudio.vbrQuality = 5;
        const QStringList directCopyVbrArgs =
            VDQtCodecEngine::buildFfmpegAudioEncodeArguments(directCopyAudio);
        if (!require(containsOptionValue(directCopyVbrArgs,
                                         QStringLiteral("-q:a"), QStringLiteral("2.00"))
                     && !directCopyVbrArgs.contains(QStringLiteral("-b:a")),
                     "configured audio quality mode reaches FFmpeg without a CBR override"))
            return 1;

        directCopyAudio.codecId = QStringLiteral("uncompressed");
        directCopyAudio.bitDepth = 24;
        if (!require(VDQtCodecEngine::buildFfmpegAudioEncodeArguments(directCopyAudio)
                         .contains(QStringLiteral("pcm_s24le")),
                     "uncompressed audio honors configured bit depth"))
            return 1;

        VDQtCodecSettings freshSettingsSession;
        if (!require(freshSettingsSession.getAudioConfig().bitrateKbps == 192,
                     "a new audio settings session starts at defaults"))
            return 1;
        codecs.resetToDefaults();
        codecSettings.resetToDefaults();
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
        volatile int rawAtomic = 7;
        if (!require(VDAtomicInt::staticExchange(&rawAtomic, 11) == 7 && rawAtomic == 11,
                     "atomic exchange updates the pointed-to integer"))
            return 1;
        VDAtomicInt::staticIncrement(&rawAtomic);
        VDAtomicInt::staticDecrement(&rawAtomic);
        if (!require(rawAtomic == 11,
                     "atomic increment/decrement do not modify the pointer variable"))
            return 1;
        if (!require(VDAtomicInt::staticCompareExchange(&rawAtomic, 19, 11) == 11
                     && rawAtomic == 19,
                     "atomic compare/exchange targets the supplied storage"))
            return 1;

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
        const QString audioFixture = settingsDirectory.filePath(QStringLiteral("buffered_audio.m4a"));
        QByteArray ffmpegError;
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("sine=frequency=997:sample_rate=48000:duration=2"),
                           QStringLiteral("-c:a"), QStringLiteral("aac"),
                           QStringLiteral("-b:a"), QStringLiteral("192k"),
                           QStringLiteral("-y"), audioFixture },
                         &ffmpegError),
                     "create streaming-audio fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        QString audioBufferError;
        if (!require(VDQtRunAudioBufferRegression(audioFixture, &audioBufferError),
                     "bounded live-audio buffer primes, pulls, and rebuilds after seek")) {
            std::cerr << audioBufferError.toStdString() << '\n';
            return 1;
        }
        audioBufferError.clear();
        if (!require(VDQtRunAudioDecodeAheadDeadlineRegression(audioFixture, &audioBufferError),
                     "codec work stays off the real-time audio pull path")) {
            std::cerr << audioBufferError.toStdString() << '\n';
            return 1;
        }

        const QString jitterFixture = settingsDirectory.filePath(QStringLiteral("subsample_jitter.nut"));
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("sine=frequency=997:sample_rate=48000:duration=2"),
                           QStringLiteral("-af"),
                           QStringLiteral("volume=0.8,asetnsamples=n=1024,asetpts=PTS+mod(floor(N/1024)\\,2)/(48000*TB)"),
                           QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
                           QStringLiteral("-f"), QStringLiteral("nut"),
                           QStringLiteral("-y"), jitterFixture },
                         &ffmpegError),
                     "create sub-millisecond timestamp-jitter fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }
        audioBufferError.clear();
        if (!require(VDQtRunAudioBufferRegression(jitterFixture, &audioBufferError),
                     "sub-millisecond timestamp jitter does not create PCM clicks")) {
            std::cerr << audioBufferError.toStdString() << '\n';
            return 1;
        }

        const QString durationJitterFixture =
            settingsDirectory.filePath(QStringLiteral("aac_duration_jitter.m4a"));
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("sine=frequency=997:sample_rate=48000:duration=2"),
                           QStringLiteral("-af"),
                           QStringLiteral("volume=0.8,asetnsamples=n=1024,asetpts=PTS+(if(eq(mod(floor(N/1024)\\,3)\\,1)\\,-32\\,if(eq(mod(floor(N/1024)\\,3)\\,2)\\,24\\,0)))/(48000*TB)"),
                           QStringLiteral("-c:a"), QStringLiteral("aac"),
                           QStringLiteral("-b:a"), QStringLiteral("192k"),
                           QStringLiteral("-y"), durationJitterFixture },
                         &ffmpegError),
                     "create AAC duration-jitter fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }
        audioBufferError.clear();
        if (!require(VDQtRunAudioBufferRegression(durationJitterFixture, &audioBufferError),
                     "variable AAC packet durations do not cut continuous playback PCM")) {
            std::cerr << audioBufferError.toStdString() << '\n';
            return 1;
        }

        const QString exportedWav =
            settingsDirectory.filePath(QStringLiteral("aac_duration_jitter_export.wav"));
        VDQtAudioPlayer audioExporter;
        if (!require(audioExporter.openFile(durationJitterFixture),
                     "open AAC duration-jitter fixture for full-processing export") ||
            !require(audioExporter.exportAudioToFile(exportedWav),
                     "export continuous AAC PCM without packet-duration edits")) {
            return 1;
        }

        const QString sourcePcm =
            settingsDirectory.filePath(QStringLiteral("aac_duration_jitter_source.pcm"));
        const QString exportedPcm =
            settingsDirectory.filePath(QStringLiteral("aac_duration_jitter_export.pcm"));
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-i"), durationJitterFixture,
                           QStringLiteral("-map"), QStringLiteral("0:a:0"),
                           QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
                           QStringLiteral("-f"), QStringLiteral("s16le"),
                           QStringLiteral("-y"), sourcePcm },
                         &ffmpegError),
                     "decode AAC duration-jitter reference PCM") ||
            !require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-i"), exportedWav,
                           QStringLiteral("-map"), QStringLiteral("0:a:0"),
                           QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
                           QStringLiteral("-f"), QStringLiteral("s16le"),
                           QStringLiteral("-y"), exportedPcm },
                         &ffmpegError),
                     "decode exported AAC PCM for comparison")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }
        QFile sourcePcmFile(sourcePcm);
        QFile exportedPcmFile(exportedPcm);
        if (!require(sourcePcmFile.open(QIODevice::ReadOnly) &&
                     exportedPcmFile.open(QIODevice::ReadOnly) &&
                     sourcePcmFile.readAll() == exportedPcmFile.readAll(),
                     "full-processing audio export matches continuous decoder PCM exactly")) {
            return 1;
        }

        const QString gapFixture = settingsDirectory.filePath(QStringLiteral("real_audio_gap.nut"));
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("sine=frequency=997:sample_rate=48000:duration=2"),
                           QStringLiteral("-af"),
                           QStringLiteral("volume=0.8,asetnsamples=n=1024,asetpts=PTS+if(gte(N\\,1024)\\,480/(48000*TB)\\,0)"),
                           QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
                           QStringLiteral("-f"), QStringLiteral("nut"),
                           QStringLiteral("-y"), gapFixture },
                         &ffmpegError),
                     "create real audio-gap fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }
        audioBufferError.clear();
        if (!require(VDQtRunAudioGapRegression(gapFixture, 1024, 480, &audioBufferError),
                     "real audio gaps remain present after timestamp jitter smoothing")) {
            std::cerr << audioBufferError.toStdString() << '\n';
            return 1;
        }

        const QString lateSelectionFixture =
            settingsDirectory.filePath(QStringLiteral("late_selection.m4a"));
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("sine=frequency=733:sample_rate=48000:duration=12"),
                           QStringLiteral("-c:a"), QStringLiteral("aac"),
                           QStringLiteral("-b:a"), QStringLiteral("160k"),
                           QStringLiteral("-y"), lateSelectionFixture },
                         &ffmpegError),
                     "create late-selection audio fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        VDQtAudioPlayer lateSelectionExporter;
        const QString lateSelectionOutput =
            settingsDirectory.filePath(QStringLiteral("late_selection.wav"));
        constexpr int64_t lateStartSample = 8 * 48000;
        constexpr int64_t lateSampleCount = 48000;
        if (!require(lateSelectionExporter.openFile(lateSelectionFixture)
                     && lateSelectionExporter.exportAudioToFile(
                         lateSelectionOutput, lateStartSample, lateSampleCount),
                     "export a sample-accurate late audio selection"))
            return 1;
        if (!require(lateSelectionExporter.lastExportUsedSeekForTesting()
                     && lateSelectionExporter.lastExportDecodedSamplesForTesting() < 4 * 48000,
                     "late audio selections seek with bounded decoder preroll"))
            return 1;

        const QString fullReferenceWav =
            settingsDirectory.filePath(QStringLiteral("late_selection_full_reference.wav"));
        VDQtAudioPlayer fullReferenceExporter;
        if (!require(fullReferenceExporter.openFile(lateSelectionFixture)
                     && fullReferenceExporter.exportAudioToFile(fullReferenceWav),
                     "decode full reference for late audio selection"))
            return 1;

        const QString lateSelectionPcm =
            settingsDirectory.filePath(QStringLiteral("late_selection.pcm"));
        const QString referenceSelectionPcm =
            settingsDirectory.filePath(QStringLiteral("late_selection_reference.pcm"));
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-i"), lateSelectionOutput,
                           QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
                           QStringLiteral("-f"), QStringLiteral("s16le"),
                           QStringLiteral("-y"), lateSelectionPcm },
                         &ffmpegError)
                     && runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-i"), fullReferenceWav,
                           QStringLiteral("-ss"), QStringLiteral("8"),
                           QStringLiteral("-t"), QStringLiteral("1"),
                           QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
                           QStringLiteral("-f"), QStringLiteral("s16le"),
                           QStringLiteral("-y"), referenceSelectionPcm },
                         &ffmpegError),
                     "render comparable late-selection PCM")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }
        QFile latePcmFile(lateSelectionPcm);
        QFile referencePcmFile(referenceSelectionPcm);
        if (!require(latePcmFile.open(QIODevice::ReadOnly)
                     && referencePcmFile.open(QIODevice::ReadOnly)
                     && latePcmFile.readAll() == referencePcmFile.readAll(),
                     "seeked late audio export is sample-identical to full decode"))
            return 1;
    }

    {
        const QString fixturePath = settingsDirectory.filePath(QStringLiteral("nonzero_bframes.mp4"));
        QByteArray ffmpegError;
        const bool fixtureCreated = runProcess(
            QStringLiteral("ffmpeg"),
            { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
              QStringLiteral("-f"), QStringLiteral("lavfi"),
              QStringLiteral("-i"), QStringLiteral("testsrc2=size=96x64:rate=10:duration=1.2"),
              QStringLiteral("-vf"), QStringLiteral("setpts=PTS+5/TB"),
              QStringLiteral("-c:v"), QStringLiteral("libx264"),
              QStringLiteral("-bf"), QStringLiteral("3"),
              QStringLiteral("-g"), QStringLiteral("30"),
              QStringLiteral("-an"), QStringLiteral("-copyts"),
              QStringLiteral("-avoid_negative_ts"), QStringLiteral("disabled"),
              QStringLiteral("-y"), fixturePath },
            &ffmpegError);
        if (!require(fixtureCreated, "create non-zero-start B-frame fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        VDQtVideoDecoder decoder;
        if (!require(decoder.openFile(fixturePath), "open non-zero-start B-frame fixture"))
            return 1;
        decoder.resetPerformanceCounters();
        for (int frameIndex = 0; frameIndex < 12; ++frameIndex) {
            if (!require(!decoder.getFrameImage(frameIndex).isNull(),
                         "sequential decoder returns every delayed frame"))
                return 1;
        }
        if (!require(decoder.getSeekCount() == 0,
                     "sequential playback does not seek between frames"))
            return 1;
        if (!require(decoder.getFrameImage(12).isNull()
                     && decoder.isFrameCountExact()
                     && decoder.getFrameCount() == 12,
                     "interactive decoder drains delayed tail frames and validates EOF"))
            return 1;
        if (!require(std::abs(decoder.getFrameTimestampSeconds(0)) < 0.0001
                     && std::abs(decoder.getFrameTimestampSeconds(11) - 1.1) < 0.0001,
                     "non-zero stream timestamps are normalized without changing frame ordinals"))
            return 1;

        decoder.clearCache();
        decoder.resetPerformanceCounters();
        if (!require(!decoder.getFrameImage(5).isNull(), "random indexed seek returns target frame"))
            return 1;
        for (int frameIndex = 6; frameIndex <= 10; ++frameIndex) {
            if (!require(!decoder.getFrameImage(frameIndex).isNull(),
                         "sequential decode continues after indexed seek"))
                return 1;
        }
        if (!require(decoder.getSeekCount() == 1,
                     "one random jump followed by playback performs one seek"))
            return 1;

        QThread decodeThread;
        auto *worker = new VDQtFrameDecodeWorker;
        worker->moveToThread(&decodeThread);
        QObject::connect(&decodeThread, &QThread::finished, worker, &QObject::deleteLater);
        decodeThread.start();

        bool workerOpened = false;
        QMetaObject::invokeMethod(
            worker,
            [&]() {
                workerOpened = worker->openSource(
                    fixturePath, QStringLiteral("Autoselect"), 0, 0, 0);
            },
            Qt::BlockingQueuedConnection);
        if (!require(workerOpened, "open playback worker fixture"))
            return 1;

        quint64 receivedGeneration = 0;
        int receivedFrame = -1;
        quint64 receivedSeekCount = 0;
        int readyCount = 0;
        QEventLoop workerLoop;
        QObject::connect(
            worker, &VDQtFrameDecodeWorker::frameReady, &workerLoop,
            [&](int frameIndex, quint64 generation, const QImage&, const QImage&,
                bool, double, int, int, quint64 seekCount, quint64) {
                ++readyCount;
                receivedFrame = frameIndex;
                receivedGeneration = generation;
                receivedSeekCount = seekCount;
                workerLoop.quit();
            });

        worker->requestFrame(0, 1, true, false);
        QTimer::singleShot(5000, &workerLoop, &QEventLoop::quit);
        workerLoop.exec();
        if (!require(receivedGeneration == 1 && receivedFrame == 0,
                     "playback worker returns its initial frame"))
            return 1;

        // Both writes occur before the worker event is serviced. Only the last
        // target should be decoded/presented, and sequential catch-up must not seek.
        worker->requestFrame(4, 2, true, false);
        worker->requestFrame(9, 3, true, false);
        QTimer::singleShot(5000, &workerLoop, &QEventLoop::quit);
        workerLoop.exec();
        if (!require(receivedGeneration == 3 && receivedFrame == 9 && readyCount == 2,
                     "playback worker coalesces obsolete frame requests"))
            return 1;
        if (!require(receivedSeekCount == 0,
                     "dropped presentation frames preserve sequential decoding"))
            return 1;

        QMetaObject::invokeMethod(
            worker, [worker]() { worker->closeSource(); }, Qt::BlockingQueuedConnection);
        decodeThread.quit();
        decodeThread.wait();
    }

    {
        const QString fixturePath = settingsDirectory.filePath(QStringLiteral("ten_bit_ffv1.mkv"));
        QByteArray ffmpegError;
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("testsrc2=size=64x48:rate=1:duration=1"),
                           QStringLiteral("-vf"), QStringLiteral("format=yuv420p10le"),
                           QStringLiteral("-c:v"), QStringLiteral("ffv1"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p10le"),
                           QStringLiteral("-y"), fixturePath },
                         &ffmpegError),
                     "create 10-bit FFV1 fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        VDQtVideoDecoder decoder;
        if (!require(decoder.openFile(fixturePath), "open 10-bit FFV1 fixture"))
            return 1;
        const QImage frame = decoder.getFrameImage(0);
        if (!require(decoder.getSourceBitDepth() == 10
                     && !decoder.sourceHasAlpha()
                     && !frame.isNull()
                     && frame.depth() == 64,
                     "10-bit source is retained in a 16-bit-per-channel image"))
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
        const QString scriptPath =
            settingsDirectory.filePath(QStringLiteral("buffered_audio.avs"));
        const QByteArray script =
            "BlankClip(length=100, width=32, height=16, fps=25, "
            "audio_rate=48000, channels=2, sample_type=\"16bit\")\n";
        if (!require(writeFile(scriptPath, script), "write AviSynth audio buffering script"))
            return 1;

        VDQtVideoDecoder decoder;
        if (!require(decoder.openFile(scriptPath), "open AviSynth audio buffering script")) {
            std::cerr << decoder.getLastError().toStdString() << '\n';
            return 1;
        }

        QString avsAudioError;
        if (!require(VDQtRunAvsAudioDecodeAheadDeadlineRegression(
                         decoder.getAvsClip(), decoder.getAvsVi(), &avsAudioError),
                     "AviSynth graph evaluation stays off the real-time audio pull path")) {
            std::cerr << avsAudioError.toStdString() << '\n';
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

    {
        const QString sourcePath =
            settingsDirectory.filePath(QStringLiteral("native_vfr_source.mp4"));
        const QString outputPath =
            settingsDirectory.filePath(QStringLiteral("native_vfr_output.mp4"));
        QByteArray ffmpegError;
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("testsrc2=size=64x48:rate=10:duration=0.3"),
                           QStringLiteral("-vf"),
                           QStringLiteral("setpts='if(eq(N,0),0,if(eq(N,1),1,20))/(10*TB)'"),
                           QStringLiteral("-fps_mode"), QStringLiteral("vfr"),
                           QStringLiteral("-c:v"), QStringLiteral("libx264"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                           QStringLiteral("-an"), QStringLiteral("-y"), sourcePath },
                         &ffmpegError),
                     "create native VFR export fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        VDQtCodecEngine& codecs = VDQtCodecEngine::instance();
        codecs.setVideoParams(
            VDQtCodecEngine::getDefaultVideoParamsForCodec(QStringLiteral("libx264")));
        VDQtVideoDecoder decoder;
        if (!require(decoder.openFile(sourcePath), "open native VFR export fixture"))
            return 1;
        const VDQtVideoDecoder::VDScanResult scan = decoder.scanVideoStream();
        if (!require(scan.errorMessage.isEmpty()
                     && std::abs(decoder.getFrameDurationSeconds(0) - 0.1) < 0.00001
                     && std::abs(decoder.getFrameDurationSeconds(1) - 1.9) < 0.00001
                     && std::abs(decoder.getFrameDurationSeconds(2) - 0.1) < 0.00001,
                     "VFR frame durations follow presentation order and stream end"))
            return 1;

        VDQtVideoExporter::ExportOptions options;
        options.inputPath = sourcePath;
        options.outputPath = outputPath;
        options.startFrame = 0;
        options.endFrame = -1;
        options.videoMode = VideoMode_NormalRecompress;
        options.audioMode = AudioMode_DirectStreamCopy;
        options.containerType = QStringLiteral("mp4");
        VDQtVideoExporter exporter;
        if (!require(exporter.exportVideo(options, &decoder),
                     "export native VFR presentation timestamps"))
            return 1;

        QByteArray timestampOutput;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                           QStringLiteral("-show_entries"),
                           QStringLiteral("frame=best_effort_timestamp_time"),
                           QStringLiteral("-of"), QStringLiteral("csv=p=0"), outputPath },
                         &ffmpegError, &timestampOutput),
                     "probe native VFR export timestamps")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }
        const QList<QByteArray> timestampLines =
            timestampOutput.trimmed().split('\n');
        if (!require(timestampLines.size() == 4
                     && std::abs(timestampLines[0].trimmed().toDouble() - 0.0) < 0.00001
                     && std::abs(timestampLines[1].trimmed().toDouble() - 0.1) < 0.00001
                     && std::abs(timestampLines[2].trimmed().toDouble() - 2.0) < 0.00001
                     && std::abs(timestampLines[3].trimmed().toDouble() - 2.1) < 0.00001,
                     "VFR export retains nonuniform frame presentation times")) {
            std::cerr << timestampOutput.constData() << '\n';
            return 1;
        }
        codecs.resetToDefaults();
    }

    {
        const QString sourcePath =
            settingsDirectory.filePath(QStringLiteral("direct_video_audio_source.mp4"));
        const QString outputPath =
            settingsDirectory.filePath(QStringLiteral("direct_video_processed_audio.mkv"));
        QByteArray ffmpegError;
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("testsrc2=size=64x48:rate=10:duration=2"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("sine=frequency=997:sample_rate=48000:duration=2"),
                           QStringLiteral("-c:v"), QStringLiteral("libx264"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                           QStringLiteral("-c:a"), QStringLiteral("aac"),
                           QStringLiteral("-shortest"), QStringLiteral("-y"), sourcePath },
                         &ffmpegError),
                     "create direct-video processed-audio fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        VDAudioCodecParams audioParams;
        audioParams.codecId = QStringLiteral("aac");
        audioParams.rateMode = QStringLiteral("cbr");
        audioParams.bitrateKbps = 96;
        audioParams.sampleRate = 44100;
        audioParams.channels = 1;
        VDQtCodecEngine::instance().setAudioParams(audioParams);

        VDQtVideoDecoder decoder;
        VDQtAudioPlayer audioPlayer;
        if (!require(decoder.openFile(sourcePath) && audioPlayer.openFile(sourcePath),
                     "open direct-video processed-audio fixture"))
            return 1;

        VDQtVideoExporter::ExportOptions options;
        options.inputPath = sourcePath;
        options.outputPath = outputPath;
        options.startFrame = 0;
        options.endFrame = -1;
        options.videoMode = VideoMode_DirectStreamCopy;
        options.audioMode = AudioMode_FullProcessing;
        options.containerType = QStringLiteral("mkv");
        VDQtVideoExporter exporter;
        if (!require(exporter.exportVideo(options, &decoder, &audioPlayer),
                     "export direct video with configured processed audio"))
            return 1;

        QByteArray audioProbeOutput;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-select_streams"), QStringLiteral("a:0"),
                           QStringLiteral("-show_entries"),
                           QStringLiteral("stream=codec_name,sample_rate,channels,bit_rate"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1"), outputPath },
                         &ffmpegError, &audioProbeOutput),
                     "probe configured processed audio output")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }
        if (!require(audioProbeOutput.contains("codec_name=aac")
                     && audioProbeOutput.contains("sample_rate=44100")
                     && audioProbeOutput.contains("channels=1"),
                     "direct-video export honors audio codec, rate, and channels")) {
            std::cerr << audioProbeOutput.constData() << '\n';
            return 1;
        }
        VDQtCodecEngine::instance().resetToDefaults();
    }

    std::cout << "stabilization tests passed\n";
    return 0;
}
