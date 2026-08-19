#include <array>
#include <cmath>
#include <iostream>

#include "VDQtFilterSystem.h"
#include "VDQtFrameDecodeWorker.h"
#include "VDQtFrameServer.h"
#include "VDQtAudioPlayer.h"
#include "VDQtCodecEngine.h"
#include "VDQtCodecSettings.h"
#include "VDQtPositionControl.h"
#include "VDQtProjectFile.h"
#include "VDQtVideoDecoder.h"
#include "VDQtVideoExporter.h"
#include "VDQtSourceSafety.h"
#include "VDQtTimeline.h"
#include <vd2/system/atomic.h>
#include <vd2/system/binary.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
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
        VDQtAudioFilterSystem& audioFilters = VDQtAudioFilterSystem::instance();
        VDAudioFilterInstance gain = audioFilters.createFilter(
            VDAudioFilterType::Gain);
        gain.params[QStringLiteral("decibels")] = 6.0;
        VDQtAudioFilterProcessor processor;
        processor.configure({gain}, 48000, 2);
        std::array<qint16, 4> samples{1000, -1000, 2000, -2000};
        processor.processInt16(reinterpret_cast<char *>(samples.data()),
                               sizeof(samples));
        if (!require(samples[0] >= 1994 && samples[0] <= 1996
                        && samples[1] <= -1994 && samples[1] >= -1996,
                     "live audio filter processor applies gain to interleaved PCM"))
            return 1;

        VDAudioFilterInstance pitch = audioFilters.createFilter(
            VDAudioFilterType::PitchShift);
        pitch.params[QStringLiteral("semitones")] = 1.5;
        VDAudioFilterInstance chorus = audioFilters.createFilter(
            VDAudioFilterType::Chorus);
        audioFilters.replaceActiveChain({gain, pitch, chorus});
        const QString graph = audioFilters.ffmpegFilterGraph(48000);
        QByteArray filterError;
        if (!require(!graph.contains(QStringLiteral("sample_rate"))
                        && runProcess(
                            QStringLiteral("ffmpeg"),
                            {QStringLiteral("-hide_banner"),
                             QStringLiteral("-loglevel"), QStringLiteral("error"),
                             QStringLiteral("-f"), QStringLiteral("lavfi"),
                             QStringLiteral("-i"),
                             QStringLiteral("sine=frequency=440:duration=0.05:sample_rate=48000"),
                             QStringLiteral("-af"), graph,
                             QStringLiteral("-f"), QStringLiteral("null"),
                             QStringLiteral("-")},
                            &filterError),
                     "audio export filter graph is accepted by FFmpeg")) {
            std::cerr << filterError.constData() << '\n';
            return 1;
        }
        audioFilters.clear();
    }

    {
        VDQtTimeline timeline;
        timeline.reset(100, true);
        if (!require(timeline.frameCount() == 100 && timeline.isIdentity(),
                     "timeline starts as an identity mapping")
            || !require(timeline.mapOutputToSource(0) == 0
                        && timeline.mapOutputToSource(99) == 99
                        && timeline.mapOutputToSource(100) == -1,
                        "identity timeline maps only valid output frames"))
            return 1;

        QString timelineError;
        const QList<VDQtTimelineSegment> copied =
            timeline.copyRange(10, 20, &timelineError);
        if (!require(copied.size() == 1
                        && copied.first().sourceStartFrame == 10
                        && copied.first().frameCount == 10,
                     "timeline copies half-open frame ranges")
            || !require(timeline.deleteRange(10, 20, &timelineError),
                        "timeline deletes a frame range")
            || !require(timeline.frameCount() == 90
                        && timeline.mapOutputToSource(9) == 9
                        && timeline.mapOutputToSource(10) == 20,
                        "timeline deletion joins source mappings without copying frames")
            || !require(timeline.insert(30, copied, &timelineError),
                        "timeline inserts copied segments")
            || !require(timeline.frameCount() == 100
                        && timeline.mapOutputToSource(30) == 10
                        && timeline.mapOutputToSource(39) == 19
                        && timeline.mapOutputToSource(40) == 40,
                        "timeline paste preserves source frame identity")
            || !require(timeline.cropToRange(25, 45, &timelineError),
                        "timeline crops to a selection")
            || !require(timeline.frameCount() == 20
                        && timeline.mapOutputToSource(5) == 10,
                        "timeline crop retains composed mapping")
            || !require(timeline.canUndo() && timeline.undo()
                        && timeline.frameCount() == 100
                        && timeline.canRedo() && timeline.redo()
                        && timeline.frameCount() == 20,
                        "timeline undo and redo restore normalized edit lists")
            || !require(timeline.resetEdits(&timelineError)
                        && timeline.isIdentity()
                        && timeline.frameCount() == 100,
                        "timeline reset restores the complete source")) {
            std::cerr << timelineError.toStdString() << '\n';
            return 1;
        }

        VDQtTimeline estimated;
        estimated.reset(40, false);
        estimated.setSourceFrameCount(75, true);
        if (!require(estimated.isIdentity() && estimated.frameCount() == 75,
                     "unmodified estimated timelines grow to an exact source length"))
            return 1;
    }

    {
        const QString sourcePath = settingsDirectory.filePath(
            QStringLiteral("edited_timeline_source.nut"));
        const QString outputPath = settingsDirectory.filePath(
            QStringLiteral("edited_timeline_output.nut"));
        QByteArray ffmpegError;
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                          QStringLiteral("error"), QStringLiteral("-f"),
                          QStringLiteral("lavfi"), QStringLiteral("-i"),
                          QStringLiteral("testsrc=size=32x24:rate=6:duration=1"),
                          QStringLiteral("-vf"), QStringLiteral("format=rgb24"),
                          QStringLiteral("-c:v"), QStringLiteral("rawvideo"),
                          QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
                          QStringLiteral("-f"), QStringLiteral("nut"),
                          QStringLiteral("-y"), sourcePath},
                         &ffmpegError),
                     "create edited timeline render fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        VDQtVideoDecoder sourceDecoder;
        if (!require(sourceDecoder.openFile(sourcePath),
                     "open edited timeline render fixture"))
            return 1;
        const auto sourceScan = sourceDecoder.scanVideoStream();
        if (!require(sourceScan.errorMessage.isEmpty()
                        && sourceDecoder.getFrameCount() == 6,
                     "index edited timeline render fixture"))
            return 1;
        const QList<int> expectedSourceFrames = {0, 1, 4, 5};
        QList<QImage> expectedImages;
        for (const int frame : expectedSourceFrames)
            expectedImages.append(sourceDecoder.getFrameImage(frame));

        VDVideoCodecParams rawVideoParams;
        rawVideoParams.codecId = QStringLiteral("rawvideo");
        rawVideoParams.pixFmt = QStringLiteral("rgb24");
        VDQtCodecEngine::instance().setVideoParams(rawVideoParams);
        VDQtVideoExporter::ExportOptions options;
        options.inputPath = sourcePath;
        options.outputPath = outputPath;
        options.videoMode = VideoMode_NormalRecompress;
        options.audioMode = AudioMode_DirectStreamCopy;
        options.includeAudio = false;
        options.containerType = QStringLiteral("nut");
        options.timelineSegments = {{0, 2}, {4, 2}};
        VDQtVideoExporter exporter;
        if (!require(exporter.exportVideo(options, &sourceDecoder, nullptr),
                     "render a non-contiguous edited timeline"))
            return 1;

        VDQtVideoDecoder outputDecoder;
        if (!require(outputDecoder.openFile(outputPath),
                     "open edited timeline render output"))
            return 1;
        const auto outputScan = outputDecoder.scanVideoStream();
        if (!require(outputScan.errorMessage.isEmpty()
                        && outputDecoder.getFrameCount() == 4,
                     "edited timeline render contains only composed frames"))
            return 1;
        for (int index = 0; index < expectedImages.size(); ++index) {
            if (!require(outputDecoder.getFrameImage(index)
                             .convertToFormat(QImage::Format_RGB888)
                         == expectedImages.at(index)
                                .convertToFormat(QImage::Format_RGB888),
                         "edited timeline render preserves source frame ordering"))
                return 1;
        }
        VDQtCodecEngine::instance().resetToDefaults();
    }

    {
        VDQtProcessingState state;
        state.videoMode = VideoMode_FastRecompress;
        state.audioMode = AudioMode_FullProcessing;
        state.smartRendering = true;
        state.preserveEmptyFrames = false;
        state.frameRate.sourceMode = 1;
        state.frameRate.customSourceFps = 24000.0 / 1001.0;
        state.frameRate.convMode = 3;
        state.frameRate.decimateN = 5;
        state.decompression.formatName = QStringLiteral("RGB24");
        state.decompression.colorSpace = 2;
        state.decompression.componentRange = 1;
        state.decoderErrorMode.errorMode = 1;
        state.rawVideo.pixelFormat = QStringLiteral("yuv422p10le");
        state.rawVideo.scanlineAlignment = 16;
        state.rawVideo.swapChromaPlanes = false;
        state.rawVideo.bottomUp = true;
        state.rawVideo.colorMatrix = QStringLiteral("bt709");
        state.rawVideo.fullRange = true;
        state.videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec(
            QStringLiteral("ffv1"));
        state.audioCodec.codecId = QStringLiteral("flac");
        state.audioCodec.sampleRate = 96000;
        state.audioCodec.channels = 6;
        VDFilterInstance filter;
        filter.id = QStringLiteral("roundtrip-filter");
        filter.name = QStringLiteral("Resize / Rescale");
        filter.type = VDFilterType::Resize;
        filter.enabled = true;
        filter.params.insert(QStringLiteral("width"), 1280.0);
        filter.params.insert(QStringLiteral("height"), 720.0);
        state.filters.append(filter);
        VDAudioFilterInstance audioFilter;
        audioFilter.id = QStringLiteral("roundtrip-audio-filter");
        audioFilter.name = QStringLiteral("gain");
        audioFilter.type = VDAudioFilterType::Gain;
        audioFilter.enabled = true;
        audioFilter.params.insert(QStringLiteral("decibels"), -3.0);
        state.audioFilters.append(audioFilter);
        state.textMetadata.insert(QStringLiteral("title"), QStringLiteral("Round trip"));

        const QString settingsPath = settingsDirectory.filePath(
            QStringLiteral("processing.vdqsettings"));
        QString stateError;
        if (!require(VDQtProjectFile::saveProcessingSettings(
                         settingsPath, state, &stateError),
                     "save processing settings snapshot")) {
            std::cerr << stateError.toStdString() << '\n';
            return 1;
        }
        VDQtProcessingState loaded;
        if (!require(VDQtProjectFile::loadProcessingSettings(
                         settingsPath, &loaded, &stateError),
                     "load processing settings snapshot")
            || !require(loaded.videoMode == VideoMode_FastRecompress
                        && loaded.audioMode == AudioMode_FullProcessing
                        && loaded.smartRendering
                        && !loaded.preserveEmptyFrames
                        && loaded.frameRate.decimateN == 5
                        && std::abs(loaded.frameRate.customSourceFps
                                    - 24000.0 / 1001.0) < 1e-9
                        && loaded.rawVideo.pixelFormat == QStringLiteral("yuv422p10le")
                        && loaded.rawVideo.scanlineAlignment == 16
                        && loaded.videoCodec.codecId == QStringLiteral("ffv1")
                        && loaded.audioCodec.codecId == QStringLiteral("flac")
                        && loaded.filters.size() == 1
                        && loaded.audioFilters.size() == 1
                        && loaded.audioFilters.first().params.value(
                               QStringLiteral("decibels")) == -3.0
                        && loaded.filters.first().params.value(QStringLiteral("width")) == 1280.0
                        && loaded.textMetadata.value(QStringLiteral("title"))
                               == QStringLiteral("Round trip"),
                        "processing snapshot preserves every session subsystem"))
            return 1;

        VDQtProjectState project;
        project.sourcePath = settingsDirectory.filePath(QStringLiteral("media/source.mkv"));
        project.sourcePaths = {
            project.sourcePath,
            settingsDirectory.filePath(QStringLiteral("media/source_2.mkv"))
        };
        project.imageSequenceFps = 23.976;
        project.audioSourcePath = settingsDirectory.filePath(
            QStringLiteral("media/commentary.flac"));
        project.audioStreamIndex = 2;
        project.position = 42;
        project.hasSelection = true;
        project.selectionStart = 10;
        project.selectionEnd = 50;
        project.sourceFrameCount = 100;
        project.timelineSegments = {{0, 10}, {20, 30}, {80, 20}};
        project.processing = state;
        const QString projectPath = settingsDirectory.filePath(
            QStringLiteral("roundtrip.vdqproject"));
        if (!require(VDQtProjectFile::saveProject(projectPath, project, &stateError),
                     "save project snapshot"))
            return 1;
        VDQtProjectState loadedProject;
        if (!require(VDQtProjectFile::loadProject(
                         projectPath, &loadedProject, &stateError),
                     "load project snapshot")
            || !require(loadedProject.sourcePath == QDir::cleanPath(project.sourcePath)
                        && loadedProject.sourcePaths.size() == 2
                        && loadedProject.sourcePaths.at(1)
                               == QDir::cleanPath(project.sourcePaths.at(1))
                        && std::abs(loadedProject.imageSequenceFps - 23.976) < 1e-9
                        && loadedProject.rawPixelFormat.isEmpty()
                        && loadedProject.audioSourcePath
                               == QDir::cleanPath(project.audioSourcePath)
                        && loadedProject.audioStreamIndex == 2
                        && !loadedProject.audioDisabled
                        && loadedProject.position == 42
                        && loadedProject.hasSelection
                        && loadedProject.sourceFrameCount == 100
                        && loadedProject.timelineSegments == project.timelineSegments
                        && loadedProject.selectionStart == 10
                        && loadedProject.selectionEnd == 50
                        && loadedProject.processing.filters.size() == 1
                        && loadedProject.processing.audioFilters.size() == 1,
                        "project snapshot preserves relative source and timeline state"))
            return 1;

        VDQtProjectState rawProject = project;
        rawProject.sourcePaths = {project.sourcePath};
        rawProject.imageSequenceFps = 0.0;
        rawProject.rawPixelFormat = QStringLiteral("rgb24");
        rawProject.rawWidth = 640;
        rawProject.rawHeight = 360;
        rawProject.rawFrameRate = 24000.0 / 1001.0;
        rawProject.rawByteOffset = 128;
        const QString rawProjectPath = settingsDirectory.filePath(
            QStringLiteral("raw_roundtrip.vdqproject"));
        VDQtProjectState loadedRawProject;
        if (!require(VDQtProjectFile::saveProject(
                         rawProjectPath, rawProject, &stateError),
                     "save raw-input project snapshot")
            || !require(VDQtProjectFile::loadProject(
                            rawProjectPath, &loadedRawProject, &stateError),
                        "load raw-input project snapshot")
            || !require(loadedRawProject.sourcePaths.size() == 1
                        && loadedRawProject.rawPixelFormat == QStringLiteral("rgb24")
                        && loadedRawProject.rawWidth == 640
                        && loadedRawProject.rawHeight == 360
                        && std::abs(loadedRawProject.rawFrameRate
                                    - 24000.0 / 1001.0) < 1e-9
                        && loadedRawProject.rawByteOffset == 128,
                        "raw-input project preserves demux parameters"))
            return 1;

        VDQtJobState queuedJob;
        queuedJob.sourcePaths = project.sourcePaths;
        queuedJob.imageSequenceFps = project.imageSequenceFps;
        queuedJob.audioSourcePath = project.audioSourcePath;
        queuedJob.audioStreamIndex = project.audioStreamIndex;
        queuedJob.options.inputPath = project.sourcePaths.first();
        queuedJob.options.outputPath =
            settingsDirectory.filePath(QStringLiteral("exports/output.mkv"));
        queuedJob.options.startFrame = 10;
        queuedJob.options.endFrame = 49;
        queuedJob.options.videoMode = VideoMode_FastRecompress;
        queuedJob.options.audioMode = AudioMode_FullProcessing;
        queuedJob.options.smartRendering = true;
        queuedJob.options.preserveEmptyFrames = false;
        queuedJob.options.containerType = QStringLiteral("mkv");
        queuedJob.processing = state;
        VDQtJobState rawQueuedJob = queuedJob;
        rawQueuedJob.sourcePaths = {project.sourcePath};
        rawQueuedJob.imageSequenceFps = 0.0;
        rawQueuedJob.rawPixelFormat = QStringLiteral("rgb24");
        rawQueuedJob.rawWidth = 640;
        rawQueuedJob.rawHeight = 360;
        rawQueuedJob.rawFrameRate = 24000.0 / 1001.0;
        rawQueuedJob.rawByteOffset = 4096;
        rawQueuedJob.options.inputPath = project.sourcePath;
        rawQueuedJob.options.outputPath =
            settingsDirectory.filePath(QStringLiteral("exports/raw_output.mkv"));
        const QString jobPath = settingsDirectory.filePath(
            QStringLiteral("queue.vdqjobs"));
        if (!require(VDQtProjectFile::saveJobQueue(
                         jobPath, { queuedJob, rawQueuedJob }, &stateError),
                     "save executable job script"))
            return 1;
        QList<VDQtJobState> loadedJobs;
        if (!require(VDQtProjectFile::loadJobQueue(
                         jobPath, &loadedJobs, &stateError),
                     "load executable job script")
            || !require(loadedJobs.size() == 2
                        && loadedJobs.first().sourcePaths.size() == 2
                        && loadedJobs.first().audioSourcePath
                               == QDir::cleanPath(project.audioSourcePath)
                        && loadedJobs.first().audioStreamIndex == 2
                        && std::abs(loadedJobs.first().imageSequenceFps - 23.976) < 1e-9
                        && loadedJobs.first().options.startFrame == 10
                        && loadedJobs.first().options.endFrame == 49
                        && loadedJobs.first().options.smartRendering
                        && !loadedJobs.first().options.preserveEmptyFrames
                        && loadedJobs.first().processing.filters.size() == 1
                        && loadedJobs.first().processing.audioFilters.size() == 1
                        && loadedJobs.at(1).sourcePaths.size() == 1
                        && loadedJobs.at(1).rawPixelFormat == QStringLiteral("rgb24")
                        && loadedJobs.at(1).rawWidth == 640
                        && loadedJobs.at(1).rawHeight == 360
                        && std::abs(loadedJobs.at(1).rawFrameRate
                                    - 24000.0 / 1001.0) < 1e-9
                        && loadedJobs.at(1).rawByteOffset == 4096,
                        "job script round-trips media sources, raw input, export range, and processing"))
            return 1;

        const QString malformedPath = settingsDirectory.filePath(
            QStringLiteral("malformed.vdqsettings"));
        if (!require(writeFile(
                         malformedPath,
                         QByteArray("{\"kind\":\"VirtualDubQTProcessingSettings\","
                                    "\"version\":1,\"processing\":{\"videoMode\":99}}")),
                     "write malformed processing snapshot")
            || !require(!VDQtProjectFile::loadProcessingSettings(
                            malformedPath, &loaded, &stateError),
                        "reject invalid processing snapshot before applying state"))
            return 1;
    }

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

    VDQtVideoDecoder::setFrameCacheBudgetMiB(32);
    VDQtVideoDecoder::setDecoderThreadCount(4);
    if (!require(VDQtVideoDecoder::getFrameCacheBudgetKiB() == 32 * 1024
                 && VDQtVideoDecoder::getDecoderThreadCount() == 4,
                 "session preferences control decoder cache and thread defaults"))
        return 1;
    VDQtVideoDecoder::setFrameCacheBudgetMiB(64);
    VDQtVideoDecoder::setDecoderThreadCount(0);

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
        const QImage grayscaleHigh = geometricFilters.processFrame(highPrecision);
        const QRgba64 grayscalePixel = reinterpret_cast<const QRgba64 *>(
            grayscaleHigh.constScanLine(0))[0];
        if (!require(!grayscaleHigh.isNull() && grayscaleHigh.depth() == 64
                     && grayscalePixel.red() == grayscalePixel.green()
                     && grayscalePixel.green() == grayscalePixel.blue()
                     && grayscalePixel.alpha() == 4000,
                     "grayscale processes high-depth input and preserves alpha"))
            return 1;

        const std::array<VDFilterType, 5> highDepthPixelFilters = {
            VDFilterType::SixAxis,
            VDFilterType::BrightnessContrast,
            VDFilterType::Blur,
            VDFilterType::Sharpen,
            VDFilterType::BobDoubler
        };
        for (VDFilterType type : highDepthPixelFilters) {
            VDQtFilterSystem highDepthFilter;
            highDepthFilter.addFilter(type);
            QList<QImage> outputs;
            if (!require(highDepthFilter.processFrameSequence(highPrecision, outputs)
                         && !outputs.isEmpty(),
                         "existing pixel filter accepts high-depth input"))
                return 1;
            for (const QImage& output : outputs) {
                const QRgba64 pixel = reinterpret_cast<const QRgba64 *>(
                    output.constScanLine(0))[0];
                if (!require(!output.isNull() && output.depth() == 64
                             && pixel.red() == 1000
                             && pixel.green() == 2000
                             && pixel.blue() == 3000
                             && pixel.alpha() == 4000,
                             "identity high-depth filter is bit-exact and preserves alpha"))
                    return 1;
            }
        }

        QImage highDepthImpulse(3, 3, QImage::Format_RGBA64);
        highDepthImpulse.fill(QColor::fromRgba64(10000, 12000, 14000, 4321));
        reinterpret_cast<QRgba64 *>(highDepthImpulse.scanLine(1))[1] =
            QRgba64::fromRgba64(30000, 26000, 22000, 4321);
        const struct {
            VDFilterType type;
            QMap<QString, double> params;
        } nonIdentityCases[] = {
            { VDFilterType::SixAxis,
              { { QStringLiteral("intensity"), 0.8 },
                { QStringLiteral("red"), 1.4 } } },
            { VDFilterType::BrightnessContrast,
              { { QStringLiteral("bright"), 10.0 },
                { QStringLiteral("cont"), 20.0 } } },
            { VDFilterType::Blur,
              { { QStringLiteral("width"), 1.0 },
                { QStringLiteral("power"), 1.0 } } },
            { VDFilterType::Sharpen,
              { { QStringLiteral("amount"), 12.0 } } }
        };
        for (const auto& testCase : nonIdentityCases) {
            VDQtFilterSystem highDepthFilter;
            highDepthFilter.addFilter(testCase.type);
            QMap<QString, double> params =
                highDepthFilter.getActiveChain().first().params;
            for (auto it = testCase.params.cbegin(); it != testCase.params.cend(); ++it)
                params.insert(it.key(), it.value());
            highDepthFilter.updateFilterParams(0, params);
            const QImage output = highDepthFilter.processFrame(highDepthImpulse);
            const QRgba64 original = reinterpret_cast<const QRgba64 *>(
                highDepthImpulse.constScanLine(1))[1];
            const QRgba64 filtered = reinterpret_cast<const QRgba64 *>(
                output.constScanLine(1))[1];
            if (!require(!output.isNull() && output.depth() == 64
                         && filtered.alpha() == 4321
                         && (filtered.red() != original.red()
                             || filtered.green() != original.green()
                             || filtered.blue() != original.blue()),
                         "non-identity high-depth filter performs 16-bit arithmetic"))
                return 1;
        }

        const std::array<VDFilterType, 12> expandedFilters = {
            VDFilterType::Deinterlace, VDFilterType::Emboss,
            VDFilterType::FieldSwap, VDFilterType::HSVAdjust,
            VDFilterType::Levels, VDFilterType::Threshold,
            VDFilterType::Posterize, VDFilterType::Gamma,
            VDFilterType::Smoother, VDFilterType::Crop,
            VDFilterType::ChromaShift, VDFilterType::Pixelate
        };
        for (VDFilterType type : expandedFilters) {
            VDQtFilterSystem expanded;
            expanded.addFilter(type);
            const QImage output = expanded.processFrame(highDepthImpulse);
            const quint16 outputAlpha = output.isNull() || output.depth() != 64
                ? 0 : reinterpret_cast<const QRgba64 *>(
                    output.constScanLine(0))[0].alpha();
            if (output.isNull() || output.depth() != 64
                || output.width() <= 0 || output.height() <= 0
                || outputAlpha != 4321) {
                std::cerr << "expanded filter type " << static_cast<int>(type)
                          << " produced " << output.width() << 'x' << output.height()
                          << " depth " << output.depth() << " alpha "
                          << outputAlpha
                          << '\n';
            }
            if (!require(!output.isNull() && output.depth() == 64
                         && output.width() > 0 && output.height() > 0
                         && outputAlpha == 4321,
                         "expanded built-in filter handles high-depth RGBA input"))
                return 1;
        }

        VDQtFilterSystem cropFilter;
        cropFilter.addFilter(VDFilterType::Crop);
        QMap<QString, double> cropParams =
            cropFilter.getActiveChain().first().params;
        cropParams[QStringLiteral("left")] = 1.0;
        cropParams[QStringLiteral("top")] = 1.0;
        cropFilter.updateFilterParams(0, cropParams);
        const QImage cropped = cropFilter.processFrame(highDepthImpulse);
        if (!require(cropped.size() == QSize(2, 2),
                     "crop filter updates output geometry"))
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

        VDAudioCodecConfig fdkConfig;
        fdkConfig.codecId = QStringLiteral("libfdk_aac");
        fdkConfig.rateControlMode = QStringLiteral("vbr");
        fdkConfig.vbrQuality = 4;
        fdkConfig.sampleRate = 0;
        fdkConfig.channels = 0;
        const VDAudioCodecParams converted =
            VDQtCodecEngine::audioParamsFromConfig(fdkConfig, 44100, 6);
        const QStringList convertedArgs =
            VDQtCodecEngine::buildFfmpegAudioEncodeArguments(converted);
        if (!require(containsOptionValue(convertedArgs,
                                         QStringLiteral("-c:a"), QStringLiteral("libfdk_aac"))
                     && containsOptionValue(convertedArgs,
                                            QStringLiteral("-vbr"), QStringLiteral("4"))
                     && !convertedArgs.contains(QStringLiteral("-q:a"))
                     && containsOptionValue(convertedArgs,
                                            QStringLiteral("-ar"), QStringLiteral("44100"))
                     && containsOptionValue(convertedArgs,
                                            QStringLiteral("-ac"), QStringLiteral("6")),
                     "legacy audio settings use the canonical FFmpeg mapping"))
            return 1;

        const struct {
            const char *codec;
            const char *rateMode;
            int quality;
            int bitrate;
            int sampleRate;
            const char *expectedOption;
            const char *expectedValue;
        } audioArgumentCases[] = {
            { "libmp3lame", "vbr", 2, 192, 44100, "-q:a", "2" },
            { "libvorbis", "vbr", 6, 160, 48000, "-q:a", "6" },
            { "flac", "vbr", 8, 0, 96000, "-compression_level", "8" },
            { "ac3", "cbr", 0, 384, 12345, "-ar", "48000" },
            { "libopus", "cbr", 0, 128, 44100, "-ar", "48000" },
            { "pcm_s32le", "cbr", 0, 0, 48000, "-c:a", "pcm_s32le" }
        };
        for (const auto& testCase : audioArgumentCases) {
            VDAudioCodecParams params;
            params.codecId = QString::fromLatin1(testCase.codec);
            params.rateMode = QString::fromLatin1(testCase.rateMode);
            params.vbrQuality = testCase.quality;
            params.bitrateKbps = testCase.bitrate;
            params.sampleRate = testCase.sampleRate;
            params.channels = 2;
            const QStringList arguments =
                VDQtCodecEngine::buildFfmpegAudioEncodeArguments(params);
            if (!require(containsOptionValue(
                             arguments,
                             QString::fromLatin1(testCase.expectedOption),
                             QString::fromLatin1(testCase.expectedValue)),
                         "canonical audio argument table covers supported codec policy"))
                return 1;
        }

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

        const QString literalScript =
            settingsDirectory.filePath(QStringLiteral("literal-only.avs"));
        const QString unrelatedOutput =
            settingsDirectory.filePath(QStringLiteral("existing-output.mkv"));
        if (!require(writeFile(literalScript,
                               QByteArray("AVISource(\"dependency-one.avi\")\n")),
                     "write literal-only dependency script")
            || !require(writeFile(unrelatedOutput, QByteArray("sentinel")),
                        "write unrelated existing output"))
            return 1;
        const VDQtVideoDecoder::ScriptDependencyReport literalAudit =
            VDQtVideoDecoder::auditScriptDependencies(literalScript);
        if (!require(literalAudit.complete
                     && literalAudit.resolvedPaths.contains(
                         QFileInfo(firstSource).absoluteFilePath()),
                     "literal-only script dependency audit is complete")
            || !require(VDQtSourceSafety::evaluateOutputPath(
                            unrelatedOutput, { literalScript }, literalScript).isSafe(),
                        "complete script audit permits an unrelated existing destination")
            || !require(!VDQtSourceSafety::evaluateOutputPath(
                             firstSource, { literalScript }, literalScript).isSafe(),
                        "script dependency aliases remain protected"))
            return 1;

        const QString dynamicScript =
            settingsDirectory.filePath(QStringLiteral("dynamic-path.avs"));
        if (!require(writeFile(dynamicScript,
                               QByteArray("base=\"dependency-\"\n"
                                          "name=\"one.avi\"\n"
                                          "AVISource(base+name)\n")),
                     "write dynamic dependency script"))
            return 1;
        const VDQtVideoDecoder::ScriptDependencyReport dynamicAudit =
            VDQtVideoDecoder::auditScriptDependencies(dynamicScript);
        if (!require(!dynamicAudit.complete,
                     "computed script source is reported as incomplete")
            || !require(!VDQtSourceSafety::evaluateOutputPath(
                             unrelatedOutput, { dynamicScript }, dynamicScript).isSafe(),
                        "incomplete script audit refuses an existing destination")
            || !require(!VDQtSourceSafety::evaluateOutputPath(
                             unrelatedOutput,
                             {literalScript, dynamicScript}).isSafe(),
                        "every script in a multi-source session is audited"))
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

        VDVideoCodecParams fastParams =
            VDQtCodecEngine::getDefaultVideoParamsForCodec(QStringLiteral("ffv1"));
        fastParams.pixFmt = QStringLiteral("yuv420p");
        fastParams.ffv1Slices = 4;
        VDQtCodecEngine::instance().setVideoParams(fastParams);
        const QString fastSelectionPath =
            settingsDirectory.filePath(QStringLiteral("nonzero_bframes_fast_selection.mkv"));
        VDQtVideoExporter::ExportOptions options;
        options.inputPath = fixturePath;
        options.outputPath = fastSelectionPath;
        options.startFrame = 5;
        options.endFrame = 8;
        options.videoMode = VideoMode_FastRecompress;
        options.containerType = QStringLiteral("mkv");
        VDQtVideoExporter exporter;
        if (!require(exporter.exportVideo(options, &decoder),
                     "fast recompress exact B-frame selection"))
            return 1;

        QByteArray selectedReference;
        QByteArray selectedOutput;
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-i"), fixturePath,
                           QStringLiteral("-vf"), QStringLiteral("select=eq(n\\,5)"),
                           QStringLiteral("-frames:v"), QStringLiteral("1"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
                           QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-") },
                         &ffmpegError, &selectedReference),
                     "decode fast selection source reference")
            || !require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-i"), fastSelectionPath,
                           QStringLiteral("-frames:v"), QStringLiteral("1"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
                           QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-") },
                         &ffmpegError, &selectedOutput),
                     "decode fast selection first output frame")
            || !require(!selectedReference.isEmpty()
                        && selectedReference == selectedOutput,
                        "fast recompress begins on the exact selected B-frame"))
            return 1;

        QByteArray frameCountOutput;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-count_frames"),
                           QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                           QStringLiteral("-show_entries"), QStringLiteral("stream=nb_read_frames"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
                           fastSelectionPath },
                         &ffmpegError, &frameCountOutput),
                     "probe fast B-frame selection count")
            || !require(frameCountOutput.trimmed() == QByteArray("4"),
                        "fast recompress honors the exclusive four-frame selection"))
            return 1;
        VDQtCodecEngine::instance().resetToDefaults();
    }

    {
        const QString fixturePath =
            settingsDirectory.filePath(QStringLiteral("raw_export_source.mkv"));
        QByteArray ffmpegError;
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("testsrc=size=5x4:rate=3:duration=1"),
                           QStringLiteral("-vf"), QStringLiteral("format=yuv444p"),
                           QStringLiteral("-c:v"), QStringLiteral("ffv1"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("yuv444p"),
                           QStringLiteral("-an"), QStringLiteral("-y"), fixturePath },
                         &ffmpegError),
                     "create raw-video export fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        VDQtVideoDecoder decoder;
        if (!require(decoder.openFile(fixturePath), "open raw-video export fixture"))
            return 1;

        VDQtVideoExporter exporter;
        VDQtVideoExporter::RawExportOptions options;
        options.inputPath = fixturePath;
        options.startFrame = 1;
        options.endFrame = 1;
        options.pixelFormat = QStringLiteral("rgb24");
        options.scanlineAlignment = 8;
        options.bottomUp = true;

        VDQtFilterSystem::instance().clearFilters();
        VDQtFilterSystem::instance().addFilter(VDFilterType::InvertColor);
        const QString rgbOutput =
            settingsDirectory.filePath(QStringLiteral("raw_selected_filtered.rgb"));
        options.outputPath = rgbOutput;
        if (!require(exporter.exportRawVideo(options, &decoder),
                     "export selected filtered bottom-up aligned RGB frame"))
            return 1;

        const QImage expectedImage = VDQtFilterSystem::instance()
            .processFrame(decoder.getFrameImage(1))
            .convertToFormat(QImage::Format_RGB888);
        QByteArray expectedRgb;
        for (int row = expectedImage.height() - 1; row >= 0; --row) {
            expectedRgb.append(
                reinterpret_cast<const char *>(expectedImage.constScanLine(row)),
                expectedImage.width() * 3);
            expectedRgb.append('\0');
        }
        QFile rgbFile(rgbOutput);
        if (!require(rgbFile.open(QIODevice::ReadOnly), "read raw RGB output"))
            return 1;
        const QByteArray actualRgb = rgbFile.readAll();
        if (!require(actualRgb == expectedRgb,
                     "raw RGB bytes honor selection, filters, alignment, and bottom-up orientation"))
            return 1;

        VDQtFilterSystem::instance().clearFilters();
        options.pixelFormat = QStringLiteral("yuv444p");
        options.scanlineAlignment = 8;
        options.bottomUp = false;
        options.swapChromaPlanes = false;
        const QString yuvOutput =
            settingsDirectory.filePath(QStringLiteral("raw_yuv_order.yuv"));
        options.outputPath = yuvOutput;
        if (!require(exporter.exportRawVideo(options, &decoder),
                     "export aligned YUV raw frame"))
            return 1;
        options.swapChromaPlanes = true;
        const QString yvuOutput =
            settingsDirectory.filePath(QStringLiteral("raw_yvu_order.yuv"));
        options.outputPath = yvuOutput;
        if (!require(exporter.exportRawVideo(options, &decoder),
                     "export swapped-chroma raw frame"))
            return 1;

        QFile yuvFile(yuvOutput);
        QFile yvuFile(yvuOutput);
        if (!require(yuvFile.open(QIODevice::ReadOnly)
                     && yvuFile.open(QIODevice::ReadOnly),
                     "read planar raw outputs"))
            return 1;
        const QByteArray yuvBytes = yuvFile.readAll();
        const QByteArray yvuBytes = yvuFile.readAll();
        constexpr int alignedPlaneBytes = 8 * 4;
        if (!require(yuvBytes.size() == alignedPlaneBytes * 3
                     && yvuBytes.size() == alignedPlaneBytes * 3
                     && yuvBytes.mid(0, alignedPlaneBytes)
                            == yvuBytes.mid(0, alignedPlaneBytes)
                     && yuvBytes.mid(alignedPlaneBytes, alignedPlaneBytes)
                            == yvuBytes.mid(alignedPlaneBytes * 2, alignedPlaneBytes)
                     && yuvBytes.mid(alignedPlaneBytes * 2, alignedPlaneBytes)
                            == yvuBytes.mid(alignedPlaneBytes, alignedPlaneBytes),
                     "raw planar export pads scanlines and swaps complete chroma planes"))
            return 1;

        options.pixelFormat = QStringLiteral("rgb24");
        options.scanlineAlignment = 1;
        options.swapChromaPlanes = false;
        options.startFrame = 0;
        options.endFrame = 2;
        options.convertFpsPreserveDuration = true;
        options.customFps = 6.0;
        const QString convertedOutput =
            settingsDirectory.filePath(QStringLiteral("raw_converted_6fps.rgb"));
        options.outputPath = convertedOutput;
        if (!require(exporter.exportRawVideo(options, &decoder),
                     "export raw frame-rate converted sequence")
            || !require(QFileInfo(convertedOutput).size() == 5 * 4 * 3 * 6,
                        "raw frame-rate conversion emits the duration-preserving frame count"))
            return 1;

        options.convertFpsPreserveDuration = false;
        options.customFps = 0.0;
        options.decimateFactor = 2;
        const QString decimatedOutput =
            settingsDirectory.filePath(QStringLiteral("raw_decimated.rgb"));
        options.outputPath = decimatedOutput;
        if (!require(exporter.exportRawVideo(options, &decoder),
                     "export decimated raw sequence")
            || !require(QFileInfo(decimatedOutput).size() == 5 * 4 * 3 * 2,
                        "raw decimation emits the expected two frames"))
            return 1;

        options.decimateFactor = 1;
        const QString cancelledOutput =
            settingsDirectory.filePath(QStringLiteral("raw_cancelled.rgb"));
        const QByteArray sentinel("existing raw destination");
        if (!require(writeFile(cancelledOutput, sentinel),
                     "create raw cancellation destination sentinel"))
            return 1;
        options.outputPath = cancelledOutput;
        if (!require(!exporter.exportRawVideo(
                         options, &decoder, nullptr, nullptr,
                         [](int completed, int) { return completed < 1; }),
                     "cancel raw export through progress callback"))
            return 1;
        QFile cancelledFile(cancelledOutput);
        if (!require(cancelledFile.open(QIODevice::ReadOnly)
                     && cancelledFile.readAll() == sentinel,
                     "cancelled raw export preserves an existing destination"))
            return 1;

        QFile sourceBefore(fixturePath);
        if (!require(sourceBefore.open(QIODevice::ReadOnly),
                     "read source before raw alias rejection"))
            return 1;
        const QByteArray sourceContents = sourceBefore.readAll();
        sourceBefore.close();
        options.outputPath = fixturePath;
        if (!require(!exporter.exportRawVideo(options, &decoder),
                     "reject raw output that aliases the loaded source"))
            return 1;
        QFile sourceAfter(fixturePath);
        if (!require(sourceAfter.open(QIODevice::ReadOnly)
                     && sourceAfter.readAll() == sourceContents,
                     "raw source-alias rejection leaves the source byte-exact"))
            return 1;

        VDQtVideoExporter::ExportOptions gifOptions;
        gifOptions.inputPath = fixturePath;
        gifOptions.outputPath =
            settingsDirectory.filePath(QStringLiteral("selected_animation.gif"));
        gifOptions.startFrame = 0;
        gifOptions.endFrame = 1;
        gifOptions.videoMode = VideoMode_FullProcessing;
        gifOptions.includeAudio = false;
        gifOptions.videoCodecOverride = QStringLiteral("gif");
        gifOptions.videoPixelFormatOverride = QStringLiteral("rgb8");
        gifOptions.containerType = QStringLiteral("gif");
        if (!require(exporter.exportVideo(gifOptions, &decoder),
                     "export selected animated GIF"))
            return 1;
        QByteArray gifProbe;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-count_frames"),
                           QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                           QStringLiteral("-show_entries"),
                           QStringLiteral("stream=codec_name,nb_read_frames"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1"),
                           gifOptions.outputPath },
                         &ffmpegError, &gifProbe),
                     "probe animated GIF output")
            || !require(gifProbe.contains("codec_name=gif")
                        && gifProbe.contains("nb_read_frames=2"),
                        "animated GIF honors the exact selected frame count")) {
            std::cerr << gifProbe.constData() << '\n';
            return 1;
        }

        VDQtVideoExporter::ExportOptions metadataOptions;
        metadataOptions.inputPath = fixturePath;
        metadataOptions.outputPath =
            settingsDirectory.filePath(QStringLiteral("metadata_output.mkv"));
        metadataOptions.startFrame = 0;
        metadataOptions.endFrame = 0;
        metadataOptions.videoMode = VideoMode_FullProcessing;
        metadataOptions.includeAudio = false;
        metadataOptions.videoCodecOverride = QStringLiteral("ffv1");
        metadataOptions.videoPixelFormatOverride = QStringLiteral("yuv444p");
        metadataOptions.containerType = QStringLiteral("mkv");
        metadataOptions.metadata.insert(
            QStringLiteral("title"), QStringLiteral("Metadata round trip"));
        if (!require(exporter.exportVideo(metadataOptions, &decoder),
                     "export container text metadata"))
            return 1;
        QByteArray metadataProbe;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-show_entries"),
                           QStringLiteral("format_tags=title"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
                           metadataOptions.outputPath },
                         &ffmpegError, &metadataProbe),
                     "probe exported text metadata")
            || !require(metadataProbe.trimmed() == QByteArray("Metadata round trip"),
                        "video export preserves configured text metadata")) {
            std::cerr << metadataProbe.constData() << '\n';
            return 1;
        }
    }

    {
        const QString firstSegment =
            settingsDirectory.filePath(QStringLiteral("append_first.mkv"));
        const QString secondSegment =
            settingsDirectory.filePath(QStringLiteral("append_second.mkv"));
        const QString videoOnlySegment =
            settingsDirectory.filePath(QStringLiteral("external_audio_video_only.mkv"));
        QByteArray ffmpegError;
        const QStringList commonArguments = {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("lavfi")
        };
        QStringList firstArguments = commonArguments;
        firstArguments
            << QStringLiteral("-i") << QStringLiteral("color=red:size=64x48:rate=5:duration=0.4")
            << QStringLiteral("-f") << QStringLiteral("lavfi")
            << QStringLiteral("-i") << QStringLiteral("sine=frequency=440:sample_rate=48000:duration=0.4")
            << QStringLiteral("-c:v") << QStringLiteral("ffv1")
            << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
            << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le")
            << QStringLiteral("-shortest") << QStringLiteral("-y") << firstSegment;
        QStringList secondArguments = commonArguments;
        secondArguments
            << QStringLiteral("-i") << QStringLiteral("color=blue:size=64x48:rate=5:duration=0.4")
            << QStringLiteral("-f") << QStringLiteral("lavfi")
            << QStringLiteral("-i") << QStringLiteral("sine=frequency=880:sample_rate=48000:duration=0.4")
            << QStringLiteral("-c:v") << QStringLiteral("ffv1")
            << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
            << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le")
            << QStringLiteral("-shortest") << QStringLiteral("-y") << secondSegment;
        QStringList videoOnlyArguments = commonArguments;
        videoOnlyArguments
            << QStringLiteral("-i")
            << QStringLiteral("color=green:size=64x48:rate=5:duration=0.4")
            << QStringLiteral("-c:v") << QStringLiteral("ffv1")
            << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
            << QStringLiteral("-an") << QStringLiteral("-y") << videoOnlySegment;
        if (!require(runProcess(QStringLiteral("ffmpeg"), firstArguments, &ffmpegError)
                     && runProcess(QStringLiteral("ffmpeg"), secondArguments, &ffmpegError)
                     && runProcess(QStringLiteral("ffmpeg"), videoOnlyArguments, &ffmpegError),
                     "create concat-compatible append fixtures")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        const QString manifest =
            settingsDirectory.filePath(QStringLiteral("append_timeline.ffconcat"));
        const QByteArray manifestContents =
            QByteArray("ffconcat version 1.0\nfile ")
            + firstSegment.toUtf8() + QByteArray("\nfile ")
            + secondSegment.toUtf8() + QByteArray("\n");
        if (!require(writeFile(manifest, manifestContents),
                     "write appended timeline manifest"))
            return 1;

        VDQtVideoDecoder decoder;
        if (!require(decoder.openFile(manifest),
                     "open appended timeline through concat demuxer"))
            return 1;
        const VDQtVideoDecoder::VDScanResult scan = decoder.scanVideoStream();
        if (!require(scan.errorMessage.isEmpty() && !scan.cancelled
                     && decoder.isFrameCountExact() && decoder.getFrameCount() == 4,
                     "appended timeline exposes all segment frames"))
            return 1;
        const QImage firstFrame = decoder.getFrameImage(0);
        const QImage lastFrame = decoder.getFrameImage(3);
        if (!require(!firstFrame.isNull() && !lastFrame.isNull()
                     && firstFrame.pixelColor(10, 10).red()
                            > firstFrame.pixelColor(10, 10).blue()
                     && lastFrame.pixelColor(10, 10).blue()
                            > lastFrame.pixelColor(10, 10).red(),
                     "appended timeline preserves segment order"))
            return 1;

        const VDQtOutputSafetyReport aliasReport =
            VDQtSourceSafety::evaluateOutputPath(firstSegment, { manifest }, manifest);
        if (!require(!aliasReport.isSafe()
                     && aliasReport.issue == VDQtOutputSafetyIssue::AliasesLoadedSource,
                     "concat dependency audit protects every appended source"))
            return 1;

        VDQtVideoExporter exporter;
        VDQtVideoExporter::ExportOptions options;
        options.inputPath = manifest;
        options.outputPath =
            settingsDirectory.filePath(QStringLiteral("append_direct_copy.mkv"));
        options.videoMode = VideoMode_DirectStreamCopy;
        options.audioMode = AudioMode_DirectStreamCopy;
        options.containerType = QStringLiteral("mkv");
        if (!require(exporter.exportVideo(options, &decoder),
                     "direct-copy appended video and audio timeline"))
            return 1;

        QByteArray videoFrames;
        QByteArray audioStreams;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-count_frames"), QStringLiteral("-select_streams"),
                           QStringLiteral("v:0"), QStringLiteral("-show_entries"),
                           QStringLiteral("stream=nb_read_frames"), QStringLiteral("-of"),
                           QStringLiteral("default=nw=1:nk=1"), options.outputPath },
                         &ffmpegError, &videoFrames)
                     && runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-select_streams"), QStringLiteral("a"),
                           QStringLiteral("-show_entries"), QStringLiteral("stream=index"),
                           QStringLiteral("-of"), QStringLiteral("csv=p=0"), options.outputPath },
                         &ffmpegError, &audioStreams),
                     "probe appended direct-copy output")
            || !require(videoFrames.trimmed() == QByteArray("4")
                        && !audioStreams.trimmed().isEmpty(),
                        "appended output retains every frame and its audio stream")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        VDQtVideoDecoder externalAudioDecoder;
        VDQtAudioPlayer externalAudioPlayer;
        if (!require(externalAudioDecoder.openFile(videoOnlySegment)
                     && externalAudioPlayer.openFile(secondSegment)
                     && externalAudioPlayer.hasAudio(),
                     "open independent video and external-audio fixtures"))
            return 1;
        VDQtAudioFilterSystem::instance().clear();
        VDAudioFilterInstance externalGain =
            VDQtAudioFilterSystem::instance().createFilter(
                VDAudioFilterType::Gain);
        externalGain.params[QStringLiteral("decibels")] = -1.0;
        VDQtAudioFilterSystem::instance().replaceActiveChain({externalGain});
        VDAudioCodecParams externalAudioParams;
        externalAudioParams.codecId = QStringLiteral("pcm_s16le");
        externalAudioParams.sampleRate = 48000;
        externalAudioParams.channels = 1;
        externalAudioParams.bitDepth = 16;
        VDQtCodecEngine::instance().setAudioParams(externalAudioParams);
        VDQtVideoExporter::ExportOptions externalAudioOptions;
        externalAudioOptions.inputPath = videoOnlySegment;
        externalAudioOptions.outputPath = settingsDirectory.filePath(
            QStringLiteral("direct_video_external_audio.mkv"));
        externalAudioOptions.videoMode = VideoMode_DirectStreamCopy;
        externalAudioOptions.audioMode = AudioMode_FullProcessing;
        externalAudioOptions.containerType = QStringLiteral("mkv");
        if (!require(exporter.exportVideo(
                         externalAudioOptions, &externalAudioDecoder,
                         &externalAudioPlayer),
                     "direct-copy video with processed external audio"))
            return 1;
        QByteArray externalStreams;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         {QStringLiteral("-v"), QStringLiteral("error"),
                          QStringLiteral("-show_entries"),
                          QStringLiteral("stream=codec_type"),
                          QStringLiteral("-of"), QStringLiteral("csv=p=0"),
                          externalAudioOptions.outputPath},
                         &ffmpegError, &externalStreams)
                     && externalStreams.contains("video")
                     && externalStreams.contains("audio"),
                     "external audio remains present beside direct-copy video"))
            return 1;
        externalAudioOptions.videoMode = VideoMode_FastRecompress;
        externalAudioOptions.outputPath = settingsDirectory.filePath(
            QStringLiteral("fast_video_external_audio.mkv"));
        if (!require(exporter.exportVideo(
                         externalAudioOptions, &externalAudioDecoder,
                         &externalAudioPlayer),
                     "fast recompress uses selected processed external audio"))
            return 1;
        externalStreams.clear();
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         {QStringLiteral("-v"), QStringLiteral("error"),
                          QStringLiteral("-show_entries"),
                          QStringLiteral("stream=codec_type"),
                          QStringLiteral("-of"), QStringLiteral("csv=p=0"),
                          externalAudioOptions.outputPath},
                         &ffmpegError, &externalStreams)
                     && externalStreams.contains("video")
                     && externalStreams.contains("audio"),
                     "fast recompress retains selected external audio"))
            return 1;
        VDQtAudioFilterSystem::instance().clear();

        VDQtFilterSystem::instance().clearFilters();
        VDQtVideoDecoder smartDecoder;
        if (!require(smartDecoder.openFile(firstSegment),
                     "open smart-render fixture"))
            return 1;
        VDQtVideoExporter::ExportOptions smartOptions;
        smartOptions.inputPath = firstSegment;
        smartOptions.outputPath =
            settingsDirectory.filePath(QStringLiteral("smart_copy.mkv"));
        smartOptions.videoMode = VideoMode_FullProcessing;
        smartOptions.audioMode = AudioMode_DirectStreamCopy;
        smartOptions.smartRendering = true;
        smartOptions.containerType = QStringLiteral("mkv");
        if (!require(exporter.exportVideo(smartOptions, &smartDecoder),
                     "smart-render a clean random-access range"))
            return 1;
        QByteArray smartCodec;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                           QStringLiteral("-show_entries"), QStringLiteral("stream=codec_name"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
                           smartOptions.outputPath },
                         &ffmpegError, &smartCodec),
                     "probe smart-render codec")
            || !require(smartCodec.trimmed() == QByteArray("ffv1"),
                        "smart rendering copies a clean GOP-aligned stream"))
            return 1;

        const QString servedPipe =
            settingsDirectory.filePath(QStringLiteral("filtered_frames.nutpipe"));
        const QString servedOutput =
            settingsDirectory.filePath(QStringLiteral("filtered_frames.mkv"));
        VDQtFrameServer frameServer;
        VDQtFrameServer::Config serverConfig;
        serverConfig.sourcePath = firstSegment;
        serverConfig.pipePath = servedPipe;
        serverConfig.audioPath = firstSegment;
        serverConfig.startFrame = 0;
        serverConfig.endFrame = 1;
        VDFilterInstance invert;
        invert.id = QStringLiteral("frame-server-invert");
        invert.name = QStringLiteral("Invert Color");
        invert.type = VDFilterType::InvertColor;
        invert.enabled = true;
        serverConfig.filters.append(invert);
        QString serverError;
        if (!require(frameServer.start(serverConfig, &serverError),
                     "start filtered NUT FIFO frame server")) {
            std::cerr << serverError.toStdString() << '\n';
            return 1;
        }
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                           QStringLiteral("error"), QStringLiteral("-i"), servedPipe,
                           QStringLiteral("-c:v"), QStringLiteral("ffv1"),
                           QStringLiteral("-c:a"), QStringLiteral("copy"),
                           QStringLiteral("-y"), servedOutput },
                         &ffmpegError),
                     "consume frame-server FIFO into a media file")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }
        QElapsedTimer serverWait;
        serverWait.start();
        while (frameServer.isRunning() && serverWait.elapsed() < 5000) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        if (!require(!frameServer.isRunning() && !QFileInfo::exists(servedPipe),
                     "frame server completes and removes its FIFO"))
            return 1;
        QByteArray servedFrames;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-count_frames"), QStringLiteral("-select_streams"),
                           QStringLiteral("v:0"), QStringLiteral("-show_entries"),
                           QStringLiteral("stream=nb_read_frames"), QStringLiteral("-of"),
                           QStringLiteral("default=nw=1:nk=1"), servedOutput },
                         &ffmpegError, &servedFrames),
                     "probe consumed frame-server stream")
            || !require(servedFrames.trimmed() == QByteArray("2"),
                        "frame server honors its exact selected range"))
            return 1;
        QByteArray servedStreams;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         {QStringLiteral("-v"), QStringLiteral("error"),
                          QStringLiteral("-show_entries"),
                          QStringLiteral("stream=codec_type"),
                          QStringLiteral("-of"), QStringLiteral("csv=p=0"),
                          servedOutput}, &ffmpegError, &servedStreams)
                     && servedStreams.contains("video")
                     && servedStreams.contains("audio"),
                     "frame server carries prepared audio with filtered video"))
            return 1;
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

        VDQtFilterSystem::instance().clearFilters();
        VDQtVideoExporter exporter;
        VDQtVideoExporter::RawExportOptions rawOptions;
        rawOptions.inputPath = fixturePath;
        rawOptions.outputPath =
            settingsDirectory.filePath(QStringLiteral("ten_bit_raw.yuv"));
        rawOptions.pixelFormat = QStringLiteral("yuv420p10le");
        rawOptions.scanlineAlignment = 16;
        rawOptions.swapChromaPlanes = false;
        if (!require(exporter.exportRawVideo(rawOptions, &decoder),
                     "export 10-bit planar raw video")
            || !require(QFileInfo(rawOptions.outputPath).size()
                            == (64 * 48 + 2 * 32 * 24) * 2,
                        "10-bit planar raw export uses two bytes per component"))
            return 1;

        VDQtFilterSystem::instance().addFilter(VDFilterType::Grayscale);
        VDQtCodecEngine::instance().setVideoParams(
            VDQtCodecEngine::getDefaultVideoParamsForCodec(
                QStringLiteral("libx264_10bit")));
        const QString filteredOutput =
            settingsDirectory.filePath(QStringLiteral("ten_bit_filtered.mkv"));
        VDQtVideoExporter::ExportOptions options;
        options.inputPath = fixturePath;
        options.outputPath = filteredOutput;
        options.videoMode = VideoMode_FullProcessing;
        options.audioMode = AudioMode_DirectStreamCopy;
        options.containerType = QStringLiteral("mkv");
        if (!require(exporter.exportVideo(options, &decoder),
                     "export filtered 10-bit video"))
            return 1;
        QByteArray pixelFormatOutput;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                           QStringLiteral("-show_entries"), QStringLiteral("stream=pix_fmt"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
                           filteredOutput },
                         &ffmpegError, &pixelFormatOutput),
                     "probe filtered 10-bit export")
            || !require(pixelFormatOutput.trimmed() == QByteArray("yuv420p10le"),
                        "filtered export retains 10-bit output precision"))
            return 1;

        // Fast recompress must keep FFmpeg-native planar video out of the
        // QImage/filter path. FFV1 makes the bypass observable byte-for-byte:
        // the active grayscale filter would otherwise change this color frame.
        VDVideoCodecParams fastParams =
            VDQtCodecEngine::getDefaultVideoParamsForCodec(QStringLiteral("ffv1"));
        fastParams.pixFmt = QStringLiteral("yuv420p10le");
        fastParams.ffv1Slices = 4;
        VDQtCodecEngine::instance().setVideoParams(fastParams);
        const QString fastOutput =
            settingsDirectory.filePath(QStringLiteral("ten_bit_fast_recompress.mkv"));
        options.outputPath = fastOutput;
        options.videoMode = VideoMode_FastRecompress;
        int fastFrameCallbacks = 0;
        if (!require(exporter.exportVideo(
                         options, &decoder, nullptr, nullptr,
                         [&fastFrameCallbacks](int, const QImage&, const QImage&) {
                             ++fastFrameCallbacks;
                         }),
                     "fast recompress 10-bit native-planar video")
            || !require(fastFrameCallbacks == 0,
                        "fast recompress bypasses the QImage frame callback path"))
            return 1;

        QByteArray sourceRgb;
        QByteArray fastRgb;
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-i"), fixturePath,
                           QStringLiteral("-frames:v"), QStringLiteral("1"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
                           QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-") },
                         &ffmpegError, &sourceRgb),
                     "decode source reference for fast recompress")
            || !require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-i"), fastOutput,
                           QStringLiteral("-frames:v"), QStringLiteral("1"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
                           QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-") },
                         &ffmpegError, &fastRgb),
                     "decode fast recompress result")
            || !require(!sourceRgb.isEmpty() && sourceRgb == fastRgb,
                        "fast recompress bypasses the active grayscale filter without RGB loss"))
            return 1;

        pixelFormatOutput.clear();
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                           QStringLiteral("-show_entries"), QStringLiteral("stream=pix_fmt"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
                           fastOutput },
                         &ffmpegError, &pixelFormatOutput),
                     "probe fast recompress pixel format")
            || !require(pixelFormatOutput.trimmed() == QByteArray("yuv420p10le"),
                        "fast recompress retains native 10-bit planar precision"))
            return 1;
        VDQtFilterSystem::instance().clearFilters();
        VDQtCodecEngine::instance().resetToDefaults();
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
        const QString scriptPath =
            settingsDirectory.filePath(QStringLiteral("fast_recompress_planar.avs"));
        const QString outputPath =
            settingsDirectory.filePath(QStringLiteral("fast_recompress_planar.mkv"));
        const QByteArray script =
            "BlankClip(length=5, width=64, height=48, fps=25, "
            "pixel_type=\"YUV420P10\", audio_rate=48000, channels=1, "
            "sample_type=\"16bit\")\n";
        if (!require(writeFile(scriptPath, script),
                     "write AviSynth fast recompress script"))
            return 1;

        VDQtVideoDecoder decoder;
        VDQtAudioPlayer audioPlayer;
        if (!require(decoder.openFile(scriptPath),
                     "open AviSynth fast recompress script")) {
            std::cerr << decoder.getLastError().toStdString() << '\n';
            return 1;
        }
        if (!require(audioPlayer.openAvsClip(decoder.getAvsClip(), decoder.getAvsVi()),
                     "open AviSynth audio for fast recompress"))
            return 1;

        VDVideoCodecParams videoParams =
            VDQtCodecEngine::getDefaultVideoParamsForCodec(QStringLiteral("ffv1"));
        videoParams.pixFmt = QStringLiteral("yuv420p10le");
        videoParams.ffv1Slices = 4;
        VDQtCodecEngine::instance().setVideoParams(videoParams);
        VDQtVideoExporter::ExportOptions options;
        options.inputPath = scriptPath;
        options.outputPath = outputPath;
        options.videoMode = VideoMode_FastRecompress;
        options.audioMode = AudioMode_DirectStreamCopy;
        options.containerType = QStringLiteral("mkv");
        VDQtVideoExporter exporter;
        if (!require(exporter.exportVideo(options, &decoder, &audioPlayer),
                     "fast recompress native AviSynth planar video and audio"))
            return 1;

        QByteArray ffmpegError;
        QByteArray probeOutput;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-count_frames"),
                           QStringLiteral("-show_entries"),
                           QStringLiteral("stream=codec_type,codec_name,pix_fmt,nb_read_frames"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1"), outputPath },
                         &ffmpegError, &probeOutput),
                     "probe AviSynth fast recompress output")
            || !require(probeOutput.contains("codec_type=video")
                        && probeOutput.contains("codec_name=ffv1")
                        && probeOutput.contains("pix_fmt=yuv420p10le")
                        && probeOutput.contains("nb_read_frames=5")
                        && probeOutput.contains("codec_type=audio")
                        && probeOutput.contains("codec_name=pcm_s16le"),
                        "AviSynth fast recompress retains planar depth and script audio")) {
            std::cerr << probeOutput.constData() << '\n';
            return 1;
        }
        VDQtCodecEngine::instance().resetToDefaults();
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
        if (!require(timestampLines.size() == 3
                     && std::abs(timestampLines[0].trimmed().toDouble() - 0.0) < 0.00001
                     && std::abs(timestampLines[1].trimmed().toDouble() - 0.1) < 0.00001
                     && std::abs(timestampLines[2].trimmed().toDouble() - 2.0) < 0.00001,
                     "VFR export retains nonuniform frame presentation times without a sentinel frame")) {
            std::cerr << timestampOutput.constData() << '\n';
            return 1;
        }
        QByteArray durationOutput;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"), outputPath },
                         &ffmpegError, &durationOutput),
                     "probe native VFR export duration"))
            return 1;
        if (!require(std::abs(durationOutput.trimmed().toDouble() - 2.1) < 0.001,
                     "VFR final-frame duration is retained without an endpoint frame")) {
            std::cerr << "duration=" << durationOutput.constData() << '\n';
            return 1;
        }

        options.outputPath = settingsDirectory.filePath(
            QStringLiteral("collapsed_empty_frames.mp4"));
        options.preserveEmptyFrames = false;
        if (!require(exporter.exportVideo(options, &decoder),
                     "collapse null-frame/timestamp gaps on request"))
            return 1;
        QByteArray collapsedProbe;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-count_frames"), QStringLiteral("-select_streams"),
                           QStringLiteral("v:0"), QStringLiteral("-show_entries"),
                           QStringLiteral("stream=nb_read_frames:format=duration"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1"),
                           options.outputPath },
                         &ffmpegError, &collapsedProbe),
                     "probe collapsed empty-frame timeline")
            || !require(collapsedProbe.contains("nb_read_frames=3")
                        && collapsedProbe.contains("duration=0.300"),
                        "empty-frame toggle collapses dwell gaps to nominal cadence")) {
            std::cerr << collapsedProbe.constData() << '\n';
            return 1;
        }
        options.preserveEmptyFrames = true;

        const struct {
            const char *extension;
            const char *container;
            const char *codec;
        } vfrContainers[] = {
            { "mkv", "mkv", "libx264" },
            { "mov", "mov", "libx264" },
            { "nut", "nut", "libx264" },
            { "webm", "webm", "libvpx-vp9" }
        };
        for (const auto& target : vfrContainers) {
            codecs.setVideoParams(VDQtCodecEngine::getDefaultVideoParamsForCodec(
                QString::fromLatin1(target.codec)));
            options.outputPath = settingsDirectory.filePath(
                QStringLiteral("native_vfr_output.%1")
                    .arg(QString::fromLatin1(target.extension)));
            options.containerType = QString::fromLatin1(target.container);
            if (!require(exporter.exportVideo(options, &decoder),
                         "export timestamped VFR container matrix"))
                return 1;
            QByteArray matrixProbe;
            const bool isNut = QString::fromLatin1(target.extension)
                == QStringLiteral("nut");
            if (!require(runProcess(
                             QStringLiteral("ffprobe"),
                             { QStringLiteral("-v"), QStringLiteral("error"),
                               QStringLiteral("-count_frames"),
                               QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                               QStringLiteral("-show_entries"),
                               QStringLiteral("stream=nb_read_frames:format=duration"),
                               QStringLiteral("-of"), QStringLiteral("default=nw=1"),
                               options.outputPath },
                             &ffmpegError, &matrixProbe),
                         "probe timestamped VFR container matrix")
                || !require(matrixProbe.contains("nb_read_frames=3")
                            && matrixProbe.contains(isNut
                                ? QByteArray("duration=2.000")
                                : QByteArray("duration=2.100")),
                            "VFR containers retain frame count and final duration")) {
                std::cerr << matrixProbe.constData() << '\n';
                QByteArray detailedProbe;
                runProcess(QStringLiteral("ffprobe"),
                           { QStringLiteral("-v"), QStringLiteral("error"),
                             QStringLiteral("-show_entries"),
                             QStringLiteral("stream=time_base,r_frame_rate,avg_frame_rate,duration:frame=best_effort_timestamp_time,pkt_duration_time:packet=pts_time,duration_time"),
                             QStringLiteral("-of"), QStringLiteral("default=nw=1"),
                             options.outputPath },
                           &ffmpegError, &detailedProbe);
                std::cerr << detailedProbe.constData() << '\n';
                return 1;
            }
        }

        const struct {
            const char *name;
            int startFrame;
            int endFrame;
            int decimate;
            bool bob;
            int videoMode;
            double customFps;
            bool convertFps;
            int expectedFrames;
            const char *expectedDuration;
        } vfrVariants[] = {
            { "selection", 1, 2, 1, false, VideoMode_NormalRecompress, 0.0, false, 2, "2.000" },
            { "decimation", 0, -1, 2, false, VideoMode_NormalRecompress, 0.0, false, 2, "2.100" },
            { "bob", 0, -1, 1, true, VideoMode_FullProcessing, 0.0, false, 6, "2.100" },
            { "fast", 0, -1, 1, false, VideoMode_FastRecompress, 0.0, false, 3, "2.100" },
            { "fast_selection", 1, 2, 1, false, VideoMode_FastRecompress, 0.0, false, 2, "2.000" },
            { "fast_decimation", 0, -1, 2, false, VideoMode_FastRecompress, 0.0, false, 2, "2.100" },
            { "fast_cfr_conversion", 0, -1, 1, false, VideoMode_FastRecompress, 10.0, true, 21, "2.100" }
        };
        codecs.setVideoParams(VDQtCodecEngine::getDefaultVideoParamsForCodec(
            QStringLiteral("libx264")));
        for (const auto& variant : vfrVariants) {
            VDQtFilterSystem::instance().clearFilters();
            if (variant.bob)
                VDQtFilterSystem::instance().addFilter(VDFilterType::BobDoubler);
            options.outputPath = settingsDirectory.filePath(
                QStringLiteral("native_vfr_%1.mp4")
                    .arg(QString::fromLatin1(variant.name)));
            options.containerType = QStringLiteral("mp4");
            options.startFrame = variant.startFrame;
            options.endFrame = variant.endFrame;
            options.decimateFactor = variant.decimate;
            options.videoMode = variant.videoMode;
            options.customFps = variant.customFps;
            options.convertFpsPreserveDuration = variant.convertFps;
            if (!require(exporter.exportVideo(options, &decoder),
                         "export VFR selection/decimation/temporal-filter variant"))
                return 1;
            QByteArray variantProbe;
            if (!require(runProcess(
                             QStringLiteral("ffprobe"),
                             { QStringLiteral("-v"), QStringLiteral("error"),
                               QStringLiteral("-count_frames"),
                               QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                               QStringLiteral("-show_entries"),
                               QStringLiteral("stream=nb_read_frames:format=duration"),
                               QStringLiteral("-of"), QStringLiteral("default=nw=1"),
                               options.outputPath },
                             &ffmpegError, &variantProbe),
                         "probe VFR export variant")
                || !require(variantProbe.contains(
                                QByteArray("nb_read_frames=")
                                    + QByteArray::number(variant.expectedFrames))
                            && variantProbe.contains(
                                QByteArray("duration=") + variant.expectedDuration),
                            "VFR variants retain selected timing and output phases")) {
                std::cerr << variantProbe.constData() << '\n';
                QByteArray detailedVariantProbe;
                runProcess(QStringLiteral("ffprobe"),
                           { QStringLiteral("-v"), QStringLiteral("error"),
                             QStringLiteral("-show_entries"),
                             QStringLiteral("frame=best_effort_timestamp_time,pkt_duration_time:packet=pts_time,dts_time,duration_time:stream=r_frame_rate,avg_frame_rate,time_base,duration"),
                             QStringLiteral("-of"), QStringLiteral("default=nw=1"),
                             options.outputPath },
                           &ffmpegError, &detailedVariantProbe);
                std::cerr << detailedVariantProbe.constData() << '\n';
                return 1;
            }
        }
        VDQtFilterSystem::instance().clearFilters();
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

        VDQtCodecEngine::instance().setVideoParams(
            VDQtCodecEngine::getDefaultVideoParamsForCodec(QStringLiteral("libx264")));
        const QString fastOutput =
            settingsDirectory.filePath(QStringLiteral("fast_recompress_selected_audio.mkv"));
        options.outputPath = fastOutput;
        options.startFrame = 5;
        options.endFrame = 14;
        options.videoMode = VideoMode_FastRecompress;
        if (!require(exporter.exportVideo(options, &decoder, &audioPlayer),
                     "fast recompress exact selection with processed audio"))
            return 1;

        QByteArray fastProbeOutput;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-count_frames"),
                           QStringLiteral("-show_entries"),
                           QStringLiteral("stream=index,codec_type,codec_name,pix_fmt,sample_rate,channels,nb_read_frames:format=duration"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1"), fastOutput },
                         &ffmpegError, &fastProbeOutput),
                     "probe selected fast recompress output")
            || !require(fastProbeOutput.contains("codec_type=video")
                        && fastProbeOutput.contains("codec_name=h264")
                        && fastProbeOutput.contains("nb_read_frames=10")
                        && fastProbeOutput.contains("codec_type=audio")
                        && fastProbeOutput.contains("codec_name=aac")
                        && fastProbeOutput.contains("sample_rate=44100")
                        && fastProbeOutput.contains("channels=1"),
                        "fast recompress retains selected frames, duration, and audio settings")) {
            std::cerr << fastProbeOutput.constData() << '\n';
            return 1;
        }
        double fastContainerDuration = -1.0;
        for (const QByteArray& line : fastProbeOutput.split('\n')) {
            if (line.startsWith("duration="))
                fastContainerDuration = line.mid(sizeof("duration=") - 1).toDouble();
        }
        // AAC's final coded frame can extend the mux duration by up to one
        // 1024-sample packet even though the video selection is exactly 1.0s.
        if (!require(fastContainerDuration >= 1.0 && fastContainerDuration <= 1.03,
                     "fast recompress bounds processed audio to one codec frame past video")) {
            std::cerr << fastProbeOutput.constData() << '\n';
            return 1;
        }

        const QString fastCopyAudioOutput =
            settingsDirectory.filePath(QStringLiteral("fast_recompress_copied_audio.mkv"));
        options.outputPath = fastCopyAudioOutput;
        options.audioMode = AudioMode_DirectStreamCopy;
        if (!require(exporter.exportVideo(options, &decoder, &audioPlayer),
                     "fast recompress exact selection with copied compressed audio"))
            return 1;
        QByteArray fastCopyProbe;
        if (!require(runProcess(
                         QStringLiteral("ffprobe"),
                         { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-count_frames"),
                           QStringLiteral("-show_entries"),
                           QStringLiteral("stream=codec_type,codec_name,nb_read_frames"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1"),
                           fastCopyAudioOutput },
                         &ffmpegError, &fastCopyProbe),
                     "probe fast recompress copied-audio output")
            || !require(fastCopyProbe.contains("codec_type=video")
                        && fastCopyProbe.contains("codec_name=h264")
                        && fastCopyProbe.contains("nb_read_frames=10")
                        && fastCopyProbe.contains("codec_type=audio")
                        && fastCopyProbe.contains("codec_name=aac"),
                        "fast recompress supports compressed audio stream copy")) {
            std::cerr << fastCopyProbe.constData() << '\n';
            return 1;
        }
        VDQtCodecEngine::instance().resetToDefaults();
    }

    {
        const QString sourcePath =
            settingsDirectory.filePath(QStringLiteral("fast_offset_source.mkv"));
        const QString outputPath =
            settingsDirectory.filePath(QStringLiteral("fast_offset_output.mkv"));
        QByteArray ffmpegError;
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"), QStringLiteral("testsrc2=size=64x48:rate=10:duration=2"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"),
                           QStringLiteral("-i"),
                           QStringLiteral("aevalsrc=if(lt(t\\,2)\\,0\\,0.5*sin(2*PI*440*t)):s=48000:d=4"),
                           QStringLiteral("-filter_complex"), QStringLiteral("[0:v]setpts=PTS+2/TB[v]"),
                           QStringLiteral("-map"), QStringLiteral("[v]"),
                           QStringLiteral("-map"), QStringLiteral("1:a:0"),
                           QStringLiteral("-c:v"), QStringLiteral("libx264"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                           QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
                           QStringLiteral("-copyts"),
                           QStringLiteral("-avoid_negative_ts"), QStringLiteral("disabled"),
                           QStringLiteral("-y"), sourcePath },
                         &ffmpegError),
                     "create delayed-video fast recompress fixture")) {
            std::cerr << ffmpegError.constData() << '\n';
            return 1;
        }

        VDQtVideoDecoder decoder;
        VDQtAudioPlayer audioPlayer;
        if (!require(decoder.openFile(sourcePath) && audioPlayer.openFile(sourcePath),
                     "open delayed-video fast recompress fixture"))
            return 1;

        VDVideoCodecParams videoParams =
            VDQtCodecEngine::getDefaultVideoParamsForCodec(QStringLiteral("ffv1"));
        videoParams.pixFmt = QStringLiteral("yuv420p");
        videoParams.ffv1Slices = 4;
        VDQtCodecEngine::instance().setVideoParams(videoParams);
        VDAudioCodecParams audioParams;
        audioParams.codecId = QStringLiteral("pcm_s16le");
        audioParams.sampleRate = 48000;
        audioParams.channels = 1;
        audioParams.bitDepth = 16;
        VDQtCodecEngine::instance().setAudioParams(audioParams);

        VDQtVideoExporter::ExportOptions options;
        options.inputPath = sourcePath;
        options.outputPath = outputPath;
        options.videoMode = VideoMode_FastRecompress;
        options.audioMode = AudioMode_FullProcessing;
        options.containerType = QStringLiteral("mkv");
        VDQtVideoExporter exporter;
        if (!require(exporter.exportVideo(options, &decoder, &audioPlayer),
                     "fast recompress aligns audio to a delayed video origin"))
            return 1;

        QByteArray leadingAudio;
        if (!require(runProcess(
                         QStringLiteral("ffmpeg"),
                         { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-i"), outputPath,
                           QStringLiteral("-map"), QStringLiteral("0:a:0"),
                           QStringLiteral("-t"), QStringLiteral("0.1"),
                           QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
                           QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("-") },
                         &ffmpegError, &leadingAudio),
                     "decode leading fast recompress audio"))
            return 1;
        const qsizetype nonZeroBytes = std::count_if(
            leadingAudio.cbegin(), leadingAudio.cend(),
            [](char value) { return value != 0; });
        if (!require(leadingAudio.size() >= 9000
                     && nonZeroBytes > leadingAudio.size() / 3,
                     "fast recompress removes pre-video silence using the common media origin"))
            return 1;
        VDQtCodecEngine::instance().resetToDefaults();
    }

    std::cout << "stabilization tests passed\n";
    return 0;
}
