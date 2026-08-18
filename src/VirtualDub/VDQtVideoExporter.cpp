#include "VDQtVideoExporter.h"
#include "VDQtCodecSettings.h"
#include "VDQtCodecEngine.h"
#include "VDQtAudioPlayer.h"
#include <QProcess>
#include <QProgressDialog>
#include <QApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <cmath>
#include <cstdio>
#include <utility>
#include <sys/stat.h>
#include "VDQtDialogs.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}

namespace {

constexpr qint64 kMaxQueuedFfmpegBytes = 8 * 1024 * 1024;
constexpr int kProcessPollMs = 25;
constexpr int kMaxDiagnosticBytes = 1024 * 1024;

struct AudioStreamProbe {
    bool succeeded = false;
    bool hasAudio = false;
    int bestAudioStreamIndex = -1;
    double videoStartOffsetSeconds = 0.0;
    QString error;
};

AudioStreamProbe probeAudioStream(const QString& path) {
    AudioStreamProbe probe;
    AVFormatContext* formatContext = nullptr;
    const QByteArray encodedPath = QFile::encodeName(path);
    int result = avformat_open_input(&formatContext, encodedPath.constData(), nullptr, nullptr);
    if (result < 0) {
        char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, errorBuffer, sizeof errorBuffer);
        probe.error = QString::fromUtf8(errorBuffer);
        return probe;
    }

    result = avformat_find_stream_info(formatContext, nullptr);
    if (result < 0) {
        char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, errorBuffer, sizeof errorBuffer);
        probe.error = QString::fromUtf8(errorBuffer);
        avformat_close_input(&formatContext);
        return probe;
    }

    double containerStartSeconds = std::numeric_limits<double>::quiet_NaN();
    if (formatContext->start_time != AV_NOPTS_VALUE)
        containerStartSeconds = static_cast<double>(formatContext->start_time) / AV_TIME_BASE;

    double earliestStreamStartSeconds = std::numeric_limits<double>::infinity();
    double videoStartSeconds = std::numeric_limits<double>::quiet_NaN();
    const int primaryVideoStream = av_find_best_stream(
        formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    for (unsigned int i = 0; i < formatContext->nb_streams; ++i) {
        const AVStream* stream = formatContext->streams[i];
        if (stream->start_time != AV_NOPTS_VALUE) {
            const double startSeconds = static_cast<double>(stream->start_time)
                                      * av_q2d(stream->time_base);
            earliestStreamStartSeconds = std::min(earliestStreamStartSeconds, startSeconds);
            if (static_cast<int>(i) == primaryVideoStream) {
                videoStartSeconds = startSeconds;
            }
        }
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            probe.hasAudio = true;
            if (probe.bestAudioStreamIndex < 0)
                probe.bestAudioStreamIndex = static_cast<int>(i);
        }
    }
    const int preferredAudioStream = av_find_best_stream(
        formatContext, AVMEDIA_TYPE_AUDIO, -1, primaryVideoStream, nullptr, 0);
    if (preferredAudioStream >= 0)
        probe.bestAudioStreamIndex = preferredAudioStream;
    if (!std::isfinite(containerStartSeconds)
        && std::isfinite(earliestStreamStartSeconds)) {
        containerStartSeconds = earliestStreamStartSeconds;
    }
    if (std::isfinite(videoStartSeconds) && std::isfinite(containerStartSeconds)) {
        probe.videoStartOffsetSeconds = videoStartSeconds - containerStartSeconds;
    }
    probe.succeeded = true;
    avformat_close_input(&formatContext);
    return probe;
}

VDAudioCodecParams configuredAudioParams()
{
    VDAudioCodecParams params = VDQtCodecEngine::instance().getAudioParams();
    if (!params.codecId.trimmed().isEmpty()) return params;

    const VDAudioCodecConfig fallback = VDQtCodecSettings::instance().getAudioConfig();
    params.codecId = fallback.codecId;
    params.rateMode = fallback.rateControlMode;
    params.bitrateKbps = fallback.bitrateKbps;
    params.vbrQuality = fallback.vbrQuality;
    params.sampleRate = fallback.sampleRate;
    params.channels = fallback.channels;
    return params;
}

void appendBounded(QByteArray& destination, const QByteArray& data) {
    if (data.isEmpty()) return;
    destination += data;
    if (destination.size() > kMaxDiagnosticBytes)
        destination.remove(0, destination.size() - kMaxDiagnosticBytes);
}

void drainProcessOutput(QProcess& process, QByteArray& diagnostics) {
    appendBounded(diagnostics, process.readAllStandardError());
    // FFmpeg normally has no useful stdout in this pipeline, but it must still
    // be consumed so that a child can never block on a full pipe.
    process.readAllStandardOutput();
}

void stopProcess(QProcess& process) {
    if (process.state() == QProcess::NotRunning) return;
    process.closeWriteChannel();
    process.terminate();
    if (!process.waitForFinished(1500)) {
        process.kill();
        process.waitForFinished(3000);
    }
}

bool waitForProcess(QProcess& process, QProgressDialog& progress,
                    QByteArray& diagnostics, bool& cancelled) {
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(kProcessPollMs);
        drainProcessOutput(process, diagnostics);
        QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
        if (progress.wasCanceled()) {
            cancelled = true;
            stopProcess(process);
            drainProcessOutput(process, diagnostics);
            return false;
        }
    }
    drainProcessOutput(process, diagnostics);
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool writeFrame(QProcess& process, const QImage& image,
                QProgressDialog& progress, QByteArray& diagnostics,
                bool& cancelled) {
    for (int y = 0; y < image.height(); ++y) {
        const char* scanline = reinterpret_cast<const char*>(image.constScanLine(y));
        const int bytesPerPixel = image.depth() / 8;
        if (bytesPerPixel != 3 && bytesPerPixel != 4 && bytesPerPixel != 8)
            return false;
        qint64 remaining = static_cast<qint64>(image.width()) * bytesPerPixel;
        while (remaining > 0) {
            while (process.bytesToWrite() >= kMaxQueuedFfmpegBytes) {
                if (process.state() == QProcess::NotRunning) {
                    drainProcessOutput(process, diagnostics);
                    return false;
                }
                process.waitForBytesWritten(kProcessPollMs);
                drainProcessOutput(process, diagnostics);
                QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
                if (progress.wasCanceled()) {
                    cancelled = true;
                    return false;
                }
            }

            const qint64 accepted = process.write(scanline, remaining);
            if (accepted <= 0) {
                drainProcessOutput(process, diagnostics);
                return false;
            }
            scanline += accepted;
            remaining -= accepted;
        }
    }
    drainProcessOutput(process, diagnostics);
    return true;
}

bool validOutputFile(const QString& path) {
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.size() > 0;
}

void removePartialOutput(const QString& path) {
    if (!path.isEmpty() && QFileInfo::exists(path))
        QFile::remove(path);
}

bool pathsReferToSameFile(const QString& firstPath, const QString& secondPath) {
    const QFileInfo first(firstPath);
    const QFileInfo second(secondPath);
    if (first.absoluteFilePath() == second.absoluteFilePath()) return true;

    struct stat firstStatus = {};
    struct stat secondStatus = {};
    const QByteArray firstName = QFile::encodeName(first.absoluteFilePath());
    const QByteArray secondName = QFile::encodeName(second.absoluteFilePath());
    return ::stat(firstName.constData(), &firstStatus) == 0
        && ::stat(secondName.constData(), &secondStatus) == 0
        && firstStatus.st_dev == secondStatus.st_dev
        && firstStatus.st_ino == secondStatus.st_ino;
}

QString stagedOutputTemplate(const QString& outputPath) {
    const QFileInfo target(outputPath);
    QString baseName = target.completeBaseName();
    if (baseName.isEmpty()) baseName = QStringLiteral("video");
    QString pattern = target.dir().filePath(QStringLiteral(".%1.XXXXXX").arg(baseName));
    const QString suffix = target.completeSuffix();
    if (!suffix.isEmpty()) pattern += QLatin1Char('.') + suffix;
    return pattern;
}

bool replaceWithStagedFile(const QString& stagedPath, const QString& outputPath) {
    if (!validOutputFile(stagedPath)) return false;
    const QFileInfo existing(outputPath);
    const QFile::Permissions permissions = existing.exists()
        ? existing.permissions()
        : QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
    QFile::setPermissions(stagedPath, permissions);

    const QByteArray stagedName = QFile::encodeName(stagedPath);
    const QByteArray outputName = QFile::encodeName(outputPath);
    return std::rename(stagedName.constData(), outputName.constData()) == 0;
}

} // namespace

VDQtVideoExporter::VDQtVideoExporter() {}

VDQtVideoExporter::~VDQtVideoExporter() {}

bool VDQtVideoExporter::exportVideo(const ExportOptions& options,
                                    VDQtVideoDecoder *activeDecoder,
                                    VDQtAudioPlayer *audioPlayer,
                                    QWidget *parentWidget,
                                    std::function<void(int frameIndex, const QImage &rawFrame, const QImage &filteredFrame)> frameCallback) {
    if (options.inputPath.isEmpty() || options.outputPath.isEmpty()) return false;
    if (pathsReferToSameFile(options.inputPath, options.outputPath)) {
        if (parentWidget) {
            QMessageBox::critical(parentWidget, "Unsafe Output Path",
                                  "The output file is the currently loaded source. Choose a different path.");
        }
        return false;
    }
    if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) {
        if (parentWidget) {
            QMessageBox::critical(parentWidget, "FFmpeg Not Available",
                                  "The ffmpeg executable was not found in PATH.");
        }
        return false;
    }

    // Pre-flight check: Video encoder availability
    if (options.videoMode != VideoMode_DirectStreamCopy) {
        VDVideoCodecParams vParams = VDQtCodecEngine::instance().getVideoParams();
        QString err;
        if (!VDQtCodecEngine::instance().checkVideoEncoderAvailable(vParams.codecId, &err)) {
            if (parentWidget) QMessageBox::critical(parentWidget, "Video Encoder Not Available", err);
            return false;
        }
    }

    VDQtVideoDecoder localDecoder;
    VDQtVideoDecoder &decoder = (activeDecoder && activeDecoder->isOpen()) ? *activeDecoder : localDecoder;
    if (!decoder.isOpen()) {
        if (!decoder.openFile(options.inputPath)) {
            return false;
        }
    }
    const QString loadedSourcePath = decoder.getFilePath();
    const bool scriptBackedInput = decoder.isAvsNative()
        || options.inputPath.endsWith(QStringLiteral(".avs"), Qt::CaseInsensitive)
        || options.inputPath.endsWith(QStringLiteral(".vpy"), Qt::CaseInsensitive)
        || loadedSourcePath.endsWith(QStringLiteral(".avs"), Qt::CaseInsensitive)
        || loadedSourcePath.endsWith(QStringLiteral(".vpy"), Qt::CaseInsensitive);
    if (scriptBackedInput) {
        const QStringList scriptSources = VDQtVideoDecoder::parseScriptSources(options.inputPath);
        for (const QString& sourcePath : scriptSources) {
            if (pathsReferToSameFile(sourcePath, options.outputPath)) {
                if (parentWidget) {
                    QMessageBox::critical(
                        parentWidget, "Unsafe Output Path",
                        QString("The output aliases media referenced by the AviSynth script:\n%1\n"
                                "Choose a different destination.").arg(sourcePath));
                }
                return false;
            }
        }
        const QFileInfo existingOutput(options.outputPath);
        if (existingOutput.exists() || existingOutput.isSymLink()) {
            if (parentWidget) {
                QMessageBox::critical(
                    parentWidget, "Unsafe Script Output Path",
                    "An existing destination cannot be replaced while exporting an AviSynth script, "
                    "because scripts can compute source paths dynamically. Choose a new output path.");
            }
            return false;
        }
    }

    AudioStreamProbe sourceAudioProbe;
    if (!decoder.isAvsNative()) {
        sourceAudioProbe = probeAudioStream(options.inputPath);
        if (!sourceAudioProbe.succeeded) {
            if (parentWidget) {
                QMessageBox::critical(parentWidget, "Audio Stream Probe Failed",
                                      QString("The source audio streams could not be inspected safely:\n%1")
                                          .arg(sourceAudioProbe.error));
            }
            return false;
        }
    }
    const bool sourceHasAudio = decoder.isAvsNative()
        ? audioPlayer && audioPlayer->hasAudio()
        : sourceAudioProbe.hasAudio;

    if (sourceHasAudio && options.audioMode != AudioMode_DirectStreamCopy
        && (!audioPlayer || !audioPlayer->hasAudio())) {
        if (parentWidget) {
            QMessageBox::critical(parentWidget, "Audio Decoder Not Available",
                                  "The source contains audio, but it could not be opened for full processing. "
                                  "The export was stopped to avoid silently producing a video-only file.");
        }
        return false;
    }

    // Pre-flight check: Audio encoder availability
    if (sourceHasAudio && options.audioMode != AudioMode_DirectStreamCopy) {
        VDAudioCodecParams aParams = VDQtCodecEngine::instance().getAudioParams();
        QString audioCodec = aParams.codecId.isEmpty()
            ? VDQtCodecSettings::instance().getAudioConfig().codecId
            : aParams.codecId;
        QString err;
        if (!VDQtCodecEngine::instance().checkAudioEncoderAvailable(audioCodec, &err)) {
            if (parentWidget) QMessageBox::critical(parentWidget, "Audio Encoder Not Available", err);
            return false;
        }
    }

    int totalFrames = decoder.getFrameCount();
    bool indexedForExport = false;
    const bool explicitFrameRange = options.endFrame >= 0
                                 && options.endFrame >= options.startFrame;
    const bool needsDecodedIndex = !decoder.isAvsNative()
        && (options.videoMode != VideoMode_DirectStreamCopy || explicitFrameRange);
    if ((options.videoMode != VideoMode_DirectStreamCopy && !decoder.isFrameCountExact())
        || needsDecodedIndex) {
        QProgressDialog indexingProgress(
            "Indexing source frames for an exact export range...", "Cancel",
            0, totalFrames > 0 ? totalFrames : 0, parentWidget);
        indexingProgress.setWindowModality(Qt::WindowModal);
        indexingProgress.setMinimumDuration(0);

        const VDQtVideoDecoder::VDScanResult scan = decoder.scanVideoStream(
            [&indexingProgress, totalFrames](int current, int reportedTotal) {
                if (totalFrames > 0) {
                    const int maximum = std::max(totalFrames, reportedTotal);
                    indexingProgress.setRange(0, maximum);
                    indexingProgress.setValue(std::min(current, maximum));
                } else {
                    indexingProgress.setRange(0, 0);
                    indexingProgress.setLabelText(
                        QString("Indexing source frames... %1 decoded").arg(current));
                }
                QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
                return !indexingProgress.wasCanceled();
            });
        indexingProgress.close();
        if (scan.cancelled) return false;
        if (!scan.errorMessage.isEmpty()) {
            if (parentWidget)
                QMessageBox::critical(parentWidget, "Export Error", scan.errorMessage);
            return false;
        }
        totalFrames = decoder.getFrameCount();
        indexedForExport = true;
    }
    if (options.videoMode == VideoMode_DirectStreamCopy && totalFrames <= 0) {
        // A full compressed stream can be copied without knowing its decoded
        // frame count. A synthetic one-frame range keeps the selection math
        // neutral and produces no -ss/-t arguments below.
        totalFrames = 1;
    }
    if (totalFrames <= 0) {
        if (parentWidget)
            QMessageBox::critical(parentWidget, "Export Error", "The source has no decodable video frames.");
        return false;
    }
    if (explicitFrameRange
        && (options.startFrame < 0 || options.startFrame >= totalFrames)) {
        if (parentWidget) {
            QMessageBox::critical(
                parentWidget, "Export Range Error",
                QString("The requested selection starts at frame %1, but the source contains only %2 frame(s).")
                    .arg(options.startFrame)
                    .arg(totalFrames));
        }
        return false;
    }
    const int startFrame = explicitFrameRange ? options.startFrame : 0;
    const int endFrame = explicitFrameRange
        ? std::min(options.endFrame, totalFrames - 1)
        : totalFrames - 1;
    int step = std::max(1, options.decimateFactor);
    const double sourceFps = decoder.getFps() > 0.0 ? decoder.getFps() : 29.97;
    const int selectedSourceFrames = endFrame - startFrame + 1;
    double sourceStartSeconds = static_cast<double>(startFrame) / sourceFps;
    double sourceDurationSeconds = static_cast<double>(selectedSourceFrames) / sourceFps;
    bool useTimestampFrameMapping = false;
    bool variableFrameTiming = false;
    double shortestFrameDuration = std::numeric_limits<double>::infinity();
    double longestFrameDuration = 0.0;
    const bool hasFrameSelection = startFrame > 0 || endFrame < totalFrames - 1;
    if (indexedForExport || hasFrameSelection || options.convertFpsPreserveDuration) {
        const double firstTimestamp = decoder.getFrameTimestampSeconds(startFrame);
        const double lastTimestamp = decoder.getFrameTimestampSeconds(endFrame);
        const double lastDuration = decoder.getFrameDurationSeconds(endFrame);
        if (std::isfinite(firstTimestamp)) sourceStartSeconds = firstTimestamp;
        if (std::isfinite(firstTimestamp) && std::isfinite(lastTimestamp)
            && std::isfinite(lastDuration) && lastTimestamp + lastDuration > firstTimestamp) {
            sourceDurationSeconds = lastTimestamp + lastDuration - firstTimestamp;
        }
    }
    bool presentationTimestampsUsable = selectedSourceFrames > 0;
    if (indexedForExport || hasFrameSelection || options.convertFpsPreserveDuration) {
        double previousTimestamp = -std::numeric_limits<double>::infinity();
        for (int frameIndex = startFrame; frameIndex <= endFrame; ++frameIndex) {
            const double timestamp = decoder.getFrameTimestampSeconds(frameIndex);
            if (!std::isfinite(timestamp) || timestamp + 1e-9 < previousTimestamp) {
                presentationTimestampsUsable = false;
                break;
            }
            if (std::isfinite(previousTimestamp)) {
                const double timestampDelta = timestamp - previousTimestamp;
                if (timestampDelta > 1e-9) {
                    shortestFrameDuration = std::min(shortestFrameDuration, timestampDelta);
                    longestFrameDuration = std::max(longestFrameDuration, timestampDelta);
                }
            }
            const double frameDuration = decoder.getFrameDurationSeconds(frameIndex);
            if (std::isfinite(frameDuration) && frameDuration > 1e-9) {
                shortestFrameDuration = std::min(shortestFrameDuration, frameDuration);
                longestFrameDuration = std::max(longestFrameDuration, frameDuration);
            }
            previousTimestamp = timestamp;
            if (frameIndex == endFrame) break;
        }
    }
    if (!std::isfinite(shortestFrameDuration) || longestFrameDuration <= 0.0)
        presentationTimestampsUsable = false;
    if (presentationTimestampsUsable) {
        variableFrameTiming = longestFrameDuration
            > shortestFrameDuration * 1.01 + 1e-6;
    }
    const bool preserveNativeVfr = variableFrameTiming
        && presentationTimestampsUsable
        && !options.convertFpsPreserveDuration
        && options.customFps <= 0.0;
    if (options.convertFpsPreserveDuration && options.customFps > 0.0)
        useTimestampFrameMapping = presentationTimestampsUsable;

    double fps = sourceFps;
    int framesToExport = (selectedSourceFrames + step - 1) / step;
    if (options.convertFpsPreserveDuration && options.customFps > 0.0) {
        fps = options.customFps;
        const long double requestedFrames = static_cast<long double>(sourceDurationSeconds) * fps;
        if (!std::isfinite(requestedFrames)
            || requestedFrames > std::numeric_limits<int>::max()) {
            if (parentWidget)
                QMessageBox::critical(parentWidget, "Export Error", "The requested frame-rate conversion is too large.");
            return false;
        }
        framesToExport = std::max(1, static_cast<int>(std::llround(requestedFrames)));
        step = 1;
    } else if (options.customFps > 0.0) {
        fps = options.customFps;
    } else if (preserveNativeVfr) {
        // The VFR path stages one lossless image per processed source frame and
        // gives FFmpeg an explicit duration for each image. `fps` remains an
        // informational/codec fallback value; it no longer determines PTS.
        fps = static_cast<double>(framesToExport) / sourceDurationSeconds;
    } else if ((indexedForExport || hasFrameSelection)
               && std::isfinite(sourceDurationSeconds) && sourceDurationSeconds > 0.0) {
        // The stdin rawvideo pipe is necessarily CFR. For a VFR selection,
        // choose the segment's exact average rate so its encoded boundary and
        // processed-audio range still land on the selected PTS boundary.
        fps = static_cast<double>(framesToExport) / sourceDurationSeconds;
    } else if (step > 1) {
        fps = sourceFps / step;
    }
    if (!std::isfinite(fps) || fps <= 0.0) fps = 29.97;

    QTemporaryFile stagedOutput(stagedOutputTemplate(options.outputPath));
    stagedOutput.setAutoRemove(true);
    if (!stagedOutput.open()) {
        if (parentWidget) {
            QMessageBox::critical(parentWidget, "Export Error",
                                  "A temporary output could not be created in the destination directory.");
        }
        return false;
    }
    const QString processOutputPath = stagedOutput.fileName();
    stagedOutput.close();

    // 0. Direct Stream Copy Mode (For media files / containers)
    if (options.videoMode == VideoMode_DirectStreamCopy && decoder.isAvsNative()) {
        if (parentWidget) {
            QMessageBox::critical(parentWidget, "Unsupported Direct Copy Operation",
                                  "AviSynth output consists of decoded frames and cannot be direct-stream-copied. "
                                  "Choose a recompress mode.");
        }
        return false;
    }
    if (options.videoMode == VideoMode_DirectStreamCopy && !decoder.isAvsNative()) {
        if (options.decimateFactor > 1 || options.customFps > 0.0) {
            if (parentWidget) {
                QMessageBox::critical(parentWidget, "Unsupported Direct Copy Operation",
                    "Direct stream copy cannot decimate frames or change frame rate. "
                    "Choose a recompress mode for this operation.");
            }
            return false;
        }
        QProcess ffmpeg;
        QByteArray diagnostics;
        bool cancelled = false;
        QStringList args;
        args << "-y";

        if (hasFrameSelection) {
            if (parentWidget) {
                const auto answer = QMessageBox::warning(
                    parentWidget,
                    "Keyframe-Aligned Direct Copy",
                    "Compressed direct-stream-copy cuts are keyframe aligned and may begin before "
                    "the selected frame. Use a recompress mode for an exact frame boundary.",
                    QMessageBox::Ok | QMessageBox::Cancel,
                    QMessageBox::Cancel);
                if (answer != QMessageBox::Ok)
                    return false;
            }
            // A copied video stream necessarily retains packets back to the
            // preceding keyframe. If audio is transcoded, FFmpeg's default
            // accurate seek would discard that same preroll only from audio,
            // leaving silence/desynchronization until the requested marker.
            // Preserve preroll consistently across both streams.
            if (options.audioMode != AudioMode_DirectStreamCopy)
                args << "-noaccurate_seek";
            const double inputStartSeconds = std::max(
                0.0, sourceAudioProbe.videoStartOffsetSeconds + sourceStartSeconds);
            args << "-ss" << QString::number(inputStartSeconds, 'f', 9);
            args << "-i" << options.inputPath;
            args << "-t" << QString::number(sourceDurationSeconds, 'f', 6);
        } else {
            args << "-i" << options.inputPath;
        }

        args << "-map" << "0:v:0";
        args << "-c:v" << "copy";

        if (sourceHasAudio) {
            args << "-map" << QString("0:%1").arg(sourceAudioProbe.bestAudioStreamIndex);
            if (options.audioMode == AudioMode_DirectStreamCopy) {
                args << "-c:a" << "copy";
            } else {
                args << VDQtCodecEngine::buildFfmpegAudioEncodeArguments(
                    configuredAudioParams());
            }
        }

        if (options.fastStart || options.containerType.contains("faststart")) {
            args << "-movflags" << "+faststart";
        }

        QString container = options.containerType.toLower();
        if (container == "webm" || options.outputPath.endsWith(".webm", Qt::CaseInsensitive)) args << "-f" << "webm";
        else if (container == "nut" || options.outputPath.endsWith(".nut", Qt::CaseInsensitive)) args << "-f" << "nut";
        else if (container.startsWith("mov") || options.outputPath.endsWith(".mov", Qt::CaseInsensitive)) args << "-f" << "mov";
        else if (container.startsWith("mp4") || options.outputPath.endsWith(".mp4", Qt::CaseInsensitive)) args << "-f" << "mp4";
        else if (container == "mkv" || options.outputPath.endsWith(".mkv", Qt::CaseInsensitive)) args << "-f" << "matroska";
        else if (container.startsWith("avi") || options.outputPath.endsWith(".avi", Qt::CaseInsensitive)) args << "-f" << "avi";

        args << processOutputPath;

        qDebug() << "[Exporter] Direct Stream Copy ffmpeg args:" << args.join(" ");

        QProgressDialog progress("Direct stream copy in progress...", "Cancel", 0, 100, parentWidget);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(30);

        ffmpeg.start("ffmpeg", args);
        if (!ffmpeg.waitForStarted(3000)) {
            removePartialOutput(processOutputPath);
            return false;
        }

        const bool processOk = waitForProcess(ffmpeg, progress, diagnostics, cancelled)
                            && validOutputFile(processOutputPath);
        const bool ok = processOk && replaceWithStagedFile(processOutputPath, options.outputPath);
        progress.setValue(100);
        if (!ok) {
            removePartialOutput(processOutputPath);
            if (!cancelled && parentWidget) {
                QMessageBox::critical(parentWidget, "Direct Copy Failed",
                                      processOk
                                          ? QStringLiteral("The completed output could not be committed to its destination.")
                                          : QString::fromUtf8(diagnostics).trimmed());
            }
        }
        return ok;
    }

    // Determine output resolution (reflecting any Resize filter if Full Processing Mode)
    QImage sampleFrame = decoder.getFrameImage(startFrame);
    if (sampleFrame.isNull()) {
        return false;
    }

    bool applyFilters = (options.videoMode == VideoMode_FullProcessing);
    int outW = sampleFrame.width();
    int outH = sampleFrame.height();
    int inputFramesToProcess = framesToExport;
    double sourceSelectionOutputFps = fps;
    int filterFramesPerInput = 1;
    if (applyFilters) {
        QImage filteredSample = VDQtFilterSystem::instance().processFrame(sampleFrame);
        if (filteredSample.isNull()) {
            if (parentWidget) QMessageBox::critical(parentWidget, "Filter Error", "The filter chain rejected the first frame.");
            return false;
        }
        outW = filteredSample.width();
        outH = filteredSample.height();

        const VDFilterTimingInfo timing = VDQtFilterSystem::instance().getTimingInfo();
        if (!timing.sequenceSupported || timing.outputFramesPerInput <= 0) {
            if (parentWidget) QMessageBox::critical(parentWidget, "Filter Error", "The configured temporal filter chain is not supported.");
            return false;
        }
        filterFramesPerInput = timing.outputFramesPerInput;
        if (filterFramesPerInput > 1) {
            fps *= filterFramesPerInput;
            framesToExport *= filterFramesPerInput;
        }
    }
    const double outputDurationSeconds = preserveNativeVfr
        ? sourceDurationSeconds
        : static_cast<double>(framesToExport) / fps;

    // Fetch user-configured codec settings
    VDVideoCodecParams vParams = VDQtCodecEngine::instance().getVideoParams();
    const VDAudioCodecParams aParams = configuredAudioParams();

    // 1. Audio Source Configuration
    QString audioSrcMedia;
    if (options.audioMode == AudioMode_DirectStreamCopy) {
        if (!decoder.isAvsNative() && sourceHasAudio) {
            audioSrcMedia = options.inputPath;
        }
        // An AviSynth clip may trim, delay, amplify, replace, or synthesize
        // audio. Copying a filename guessed from its script would bypass those
        // operations, so native clips are streamed through a temporary PCM file.
    }

    bool isDirectCopyMediaAudio = !audioSrcMedia.isEmpty();

    QTemporaryDir temporaryDirectory;
    QString tempAudioPath;
    if (!isDirectCopyMediaAudio && audioPlayer && audioPlayer->hasAudio()) {
        if (!temporaryDirectory.isValid()) {
            qWarning() << "[Exporter] Unable to create a secure temporary directory.";
            return false;
        }
        tempAudioPath = temporaryDirectory.filePath("audio.wav");

        int sampleRate = audioPlayer->getSampleRate();
        if (sampleRate <= 0) sampleRate = 48000;

        const int64_t startSample = static_cast<int64_t>(std::llround(sourceStartSeconds * sampleRate));
        const int64_t sampleCount = static_cast<int64_t>(std::llround(outputDurationSeconds * sampleRate));

        QProgressDialog audioProgress(
            "Preparing processed audio...", "Cancel", 0, 100, parentWidget);
        audioProgress.setWindowModality(Qt::WindowModal);
        audioProgress.setMinimumDuration(0);
        const bool audioPrepared = audioPlayer->exportAudioToFile(
            tempAudioPath, startSample, sampleCount,
            [&audioProgress](int current, int total) {
                audioProgress.setRange(0, std::max(1, total));
                audioProgress.setValue(std::clamp(current, 0, std::max(1, total)));
                QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
                return !audioProgress.wasCanceled();
            });
        const bool audioCancelled = audioProgress.wasCanceled();
        audioProgress.close();

        if (audioPrepared) {
            qDebug() << "[Exporter] Successfully prepared audio stream for export (samples:" << startSample << "count:" << sampleCount << "):" << tempAudioPath;
        } else {
            if (!audioCancelled && parentWidget) {
                QMessageBox::critical(parentWidget, "Audio Export Error",
                                      "The requested processed audio range could not be decoded. "
                                      "The video export was stopped to avoid silently dropping audio.");
            }
            return false;
        }
    }

    // 2. FFmpeg input arguments (ALL inputs before encoding flags)
    QProcess ffmpeg;
    QStringList args;

    QString rawInputPixelFormat = QStringLiteral("rgb24");
    QImage::Format rawInputImageFormat = QImage::Format_RGB888;
    if (sampleFrame.depth() > 32) {
        rawInputPixelFormat = QStringLiteral("rgba64le");
        rawInputImageFormat = QImage::Format_RGBA64;
    } else if (decoder.sourceHasAlpha()) {
        rawInputPixelFormat = QStringLiteral("rgba");
        rawInputImageFormat = QImage::Format_RGBA8888;
    }

    QTemporaryDir vfrFrameDirectory(
        QDir::tempPath() + QStringLiteral("/virtualdub-vfr-XXXXXX"));
    QString vfrManifestPath;
    if (preserveNativeVfr) {
        if (!vfrFrameDirectory.isValid()) {
            if (parentWidget)
                QMessageBox::critical(parentWidget, "Export Error",
                                      "Temporary storage for variable-frame-rate frames could not be created.");
            return false;
        }

        QProgressDialog stagingProgress(
            "Preparing timestamped variable-frame-rate frames...", "Cancel",
            0, std::max(1, framesToExport), parentWidget);
        stagingProgress.setWindowModality(Qt::WindowModal);
        stagingProgress.setMinimumDuration(0);

        QByteArray manifest("ffconcat version 1.0\n");
        QString lastFrameName;
        int stagedFrames = 0;
        bool stagingFailed = false;
        QString stagingError;

        for (int64_t sourceFrame64 = startFrame;
             sourceFrame64 <= endFrame;
             sourceFrame64 += step) {
            const int sourceFrame = static_cast<int>(sourceFrame64);
            QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
            if (stagingProgress.wasCanceled()) {
                stagingError = QStringLiteral("Variable-frame-rate export was cancelled.");
                stagingFailed = true;
                break;
            }

            QImage rawFrame = decoder.getFrameImage(sourceFrame);
            if (rawFrame.isNull()) {
                stagingError = QString("Failed to decode source frame %1.").arg(sourceFrame);
                stagingFailed = true;
                break;
            }

            QList<QImage> filteredFrames;
            if (applyFilters) {
                if (!VDQtFilterSystem::instance().processFrameSequence(rawFrame, filteredFrames)
                    || filteredFrames.size() != filterFramesPerInput) {
                    stagingError = QString("The temporal filter chain failed at source frame %1.")
                                       .arg(sourceFrame);
                    stagingFailed = true;
                    break;
                }
            } else {
                filteredFrames.append(rawFrame);
            }

            const double frameStart = decoder.getFrameTimestampSeconds(sourceFrame);
            const int nextSourceFrame = sourceFrame + step;
            const double frameEnd = nextSourceFrame <= endFrame
                ? decoder.getFrameTimestampSeconds(nextSourceFrame)
                : sourceStartSeconds + sourceDurationSeconds;
            if (!std::isfinite(frameStart) || !std::isfinite(frameEnd)
                || frameEnd <= frameStart) {
                stagingError = QString("Source frame %1 has an invalid presentation duration.")
                                   .arg(sourceFrame);
                stagingFailed = true;
                break;
            }
            const double phaseDuration = (frameEnd - frameStart) / filteredFrames.size();
            if (!std::isfinite(phaseDuration) || phaseDuration < 0.000001) {
                stagingError = QString("Source frame %1 has a presentation phase shorter than one microsecond.")
                                   .arg(sourceFrame);
                stagingFailed = true;
                break;
            }

            for (const QImage& filteredFrame : std::as_const(filteredFrames)) {
                if (filteredFrame.isNull()
                    || filteredFrame.width() != outW
                    || filteredFrame.height() != outH) {
                    stagingError = QString("Filter output dimensions changed at frame %1.")
                                       .arg(sourceFrame);
                    stagingFailed = true;
                    break;
                }

                const QString frameName = QString("frame_%1.png")
                                              .arg(stagedFrames, 8, 10, QLatin1Char('0'));
                const QString framePath = vfrFrameDirectory.filePath(frameName);
                if (!filteredFrame.save(framePath, "PNG")) {
                    stagingError = QString("Could not stage processed frame %1.").arg(sourceFrame);
                    stagingFailed = true;
                    break;
                }

                manifest += "file '" + frameName.toUtf8() + "'\n";
                // A microsecond image-demuxer time base preserves source PTS.
                // A repeated final image below supplies the end timestamp for
                // the preceding frame; its one-microsecond sentinel duration is
                // visually inert but allows muxers to retain that final dwell.
                manifest += "option framerate 1000000\n";
                manifest += "duration "
                         + QString::number(phaseDuration, 'f', 12).toLatin1()
                         + '\n';
                lastFrameName = frameName;
                ++stagedFrames;

                if (frameCallback)
                    frameCallback(sourceFrame, rawFrame, filteredFrame);
                stagingProgress.setValue(stagedFrames);
            }
            if (stagingFailed) break;
        }

        if (!stagingFailed && stagedFrames != framesToExport) {
            stagingError = QString("Prepared %1 frames, but %2 were expected.")
                               .arg(stagedFrames).arg(framesToExport);
            stagingFailed = true;
        }
        if (!stagingFailed) {
            manifest += "file '" + lastFrameName.toUtf8() + "'\n";
            manifest += "option framerate 1000000\n";
            vfrManifestPath = vfrFrameDirectory.filePath(QStringLiteral("timeline.ffconcat"));
            QFile manifestFile(vfrManifestPath);
            if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || manifestFile.write(manifest) != manifest.size()) {
                stagingError = QStringLiteral("The variable-frame-rate timeline could not be written.");
                stagingFailed = true;
            }
        }
        stagingProgress.close();
        if (stagingFailed) {
            qWarning() << "[Exporter]" << stagingError;
            if (parentWidget && !stagingProgress.wasCanceled())
                QMessageBox::critical(parentWidget, "Export Error", stagingError);
            return false;
        }
    }

    args << "-y";
    if (preserveNativeVfr) {
        args << "-f" << "concat"
             << "-safe" << "0"
             << "-i" << vfrManifestPath;
    } else {
        args << "-f" << "rawvideo"
             << "-pix_fmt" << rawInputPixelFormat
             << "-s" << QString("%1x%2").arg(outW).arg(outH)
             << "-r" << QString::number(fps, 'f', 12)
             << "-i" << "-"; // input 0: raw video stream from stdin
    }

    bool hasAudioInput = false;
    if (isDirectCopyMediaAudio) {
        hasAudioInput = true;
        // This is a second input whose time origin is the container, while the
        // editor timeline is normalized to the first video timestamp. Seek by
        // both offsets so leading pre-video audio is not muxed at video t=0.
        const double audioInputStart = std::max(
            0.0, sourceAudioProbe.videoStartOffsetSeconds + sourceStartSeconds);
        if (audioInputStart > 1e-9)
            args << "-ss" << QString::number(audioInputStart, 'f', 9);
        args << "-i" << audioSrcMedia;
    } else if (!tempAudioPath.isEmpty() && QFile::exists(tempAudioPath)) {
        hasAudioInput = true;
        args << "-i" << tempAudioPath;
    }

    // 3. Stream mappings
    args << "-map" << "0:v:0";
    if (hasAudioInput) {
        if (isDirectCopyMediaAudio)
            args << "-map" << QString("1:%1").arg(sourceAudioProbe.bestAudioStreamIndex);
        else
            args << "-map" << "1:a:0";
    }

    // 4. Video Codec & Options
    QString container = options.containerType.toLower();
    QString outPixFmt = vParams.pixFmt.toLower();
    if (options.videoMode == VideoMode_DirectStreamCopy) {
        args << "-c:v" << "rawvideo";
        outPixFmt = "rgb24";
    } else if (vParams.codecId == "(Uncompressed)" || vParams.codecId.isEmpty()) {
        args << "-c:v" << "rawvideo";
        if (decoder.getSourceBitDepth() > 8)
            outPixFmt = decoder.sourceHasAlpha() ? "rgba64le" : "rgb48le";
        else
            outPixFmt = decoder.sourceHasAlpha() ? "rgba" : "bgr24";
        if (container == "mkv" || container == "matroska") {
            args << "-allow_raw_vfw" << "1";
        }
    } else if (vParams.codecId == "libx264_10bit") {
        args << "-c:v" << "libx264";
        outPixFmt = "yuv420p10le";
    } else if (vParams.codecId == "libx265_lossless") {
        args << "-c:v" << "libx265" << "-x265-params" << "lossless=1";
        outPixFmt = "yuv420p";
    } else {
        args << "-c:v" << vParams.codecId;
    }

    if (options.videoMode != VideoMode_DirectStreamCopy) {
        if (vParams.codecId == "prores_ks") {
            args << "-profile:v" << QString::number(vParams.proresProfile);
            if (!vParams.proresVendor.isEmpty()) {
                args << "-vendor" << vParams.proresVendor;
            }
            if (vParams.proresProfile >= 4) {
                outPixFmt = decoder.sourceHasAlpha() ? "yuva444p10le" : "yuv444p10le";
            } else {
                outPixFmt = "yuv422p10le";
            }
        } else if (vParams.codecId == "ffv1") {
            args << "-level" << QString::number(vParams.ffv1Version);
            args << "-coder" << QString::number(vParams.ffv1Coder);
            args << "-slices" << QString::number(vParams.ffv1Slices);
        } else if (vParams.codecId == "huffyuv") {
            args << "-pred" << QString::number(vParams.huffyuvPredictor);
            if (outPixFmt.isEmpty()) outPixFmt = "yuv422p";
        } else if (vParams.codecId == "cfhd") {
            args << "-quality" << QString::number(vParams.cineformQuality);
            outPixFmt = "yuv422p10le";
        } else if (vParams.codecId == "libx264" || vParams.codecId == "libx265") {
            if (vParams.rateMode == "crf") {
                args << "-crf" << QString::number(vParams.crf);
            } else {
                args << "-b:v" << QString("%1k").arg(vParams.targetBitrateKbps);
            }
            if (!vParams.preset.isEmpty()) args << "-preset" << vParams.preset;
            if (!vParams.tune.isEmpty() && vParams.tune != "none") args << "-tune" << vParams.tune;
        } else if (vParams.codecId == "libvpx" || vParams.codecId == "libvpx-vp9" || vParams.codecId == "libsvtav1") {
            args << "-crf" << QString::number(vParams.crf);
            args << "-b:v" << QString("%1k").arg(vParams.targetBitrateKbps);
        }

        if (vParams.keyframeInterval > 0 && vParams.keyframeInterval <= 10000) {
            args << "-g" << QString::number(vParams.keyframeInterval);
        }
        if (vParams.bFrames > 0 && vParams.bFrames <= 16) {
            args << "-bf" << QString::number(vParams.bFrames);
        }
        if (!vParams.colorMatrix.isEmpty() && vParams.colorMatrix != "auto" && vParams.colorMatrix != "none") {
            args << "-colorspace" << vParams.colorMatrix
                 << "-color_primaries" << vParams.colorMatrix
                 << "-color_trc" << vParams.colorMatrix;
        }
    }

    if (!outPixFmt.isEmpty()) {
        const QByteArray pixelFormatName = outPixFmt.toUtf8();
        const AVPixelFormat outputFormat = av_get_pix_fmt(pixelFormatName.constData());
        const AVPixFmtDescriptor *outputDescriptor = av_pix_fmt_desc_get(outputFormat);
        int outputBitDepth = 0;
        bool outputHasAlpha = false;
        if (outputDescriptor) {
            for (int component = 0; component < outputDescriptor->nb_components; ++component) {
                outputBitDepth = std::max(
                    outputBitDepth,
                    static_cast<int>(outputDescriptor->comp[component].depth));
            }
            outputHasAlpha = (outputDescriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
        }
        if (!outputDescriptor
            || (decoder.getSourceBitDepth() > 8
                && outputBitDepth < decoder.getSourceBitDepth())
            || (decoder.sourceHasAlpha() && !outputHasAlpha)) {
            if (parentWidget) {
                QMessageBox::critical(
                    parentWidget,
                    "Video Precision Error",
                    QString("The selected output pixel format '%1' cannot preserve the source's "
                            "%2-bit%3 video data. Choose a matching high-bit-depth/alpha-capable "
                            "format or use direct stream copy.")
                        .arg(outPixFmt)
                        .arg(decoder.getSourceBitDepth())
                        .arg(decoder.sourceHasAlpha() ? QStringLiteral(" alpha") : QString()));
            }
            return false;
        }
        args << "-pix_fmt" << outPixFmt;
    }
    if (preserveNativeVfr)
        args << "-fps_mode" << "vfr";

    // 5. Audio Codec & Options
    if (hasAudioInput) {
        if (options.audioMode == AudioMode_DirectStreamCopy) {
            // Native AviSynth audio is staged in a WAV that already carries
            // its true PCM integer/float depth. Stream-copy that codec too;
            // forcing pcm_s16le here silently quantized 24/32-bit and float
            // clips. Incompatible destination containers now fail visibly.
            args << "-c:a" << "copy";
        } else {
            args << VDQtCodecEngine::buildFfmpegAudioEncodeArguments(aParams);
        }

        // Bound the mux to the generated video duration. Using -shortest here
        // would let a slightly shorter audio stream truncate valid video frames.
        const double muxDuration = outputDurationSeconds
                                 + (preserveNativeVfr ? 0.000001 : 0.0);
        args << "-t" << QString::number(muxDuration, 'f', 9);
    }

    // 6. Container Format & Output Path
    if (options.fastStart || options.containerType.contains("faststart")) {
        args << "-movflags" << "+faststart";
    }

    if (container == "webm" || options.outputPath.endsWith(".webm", Qt::CaseInsensitive)) {
        args << "-f" << "webm";
    } else if (container == "nut" || options.outputPath.endsWith(".nut", Qt::CaseInsensitive)) {
        args << "-f" << "nut";
    } else if (container.startsWith("mov") || options.outputPath.endsWith(".mov", Qt::CaseInsensitive)) {
        args << "-f" << "mov";
    } else if (container.startsWith("mp4") || options.outputPath.endsWith(".mp4", Qt::CaseInsensitive)) {
        args << "-f" << "mp4";
    } else if (container == "mkv" || options.outputPath.endsWith(".mkv", Qt::CaseInsensitive)) {
        args << "-f" << "matroska";
    } else if (container.startsWith("avi") || options.outputPath.endsWith(".avi", Qt::CaseInsensitive)) {
        args << "-f" << "avi";
    }

    args << processOutputPath;

    qDebug() << "[Exporter] Launching ffmpeg with args:" << args.join(" ");

    QByteArray diagnostics;
    bool cancelled = false;
    bool writeFailed = false;
    ffmpeg.start("ffmpeg", args);
    if (!ffmpeg.waitForStarted(3000)) {
        qWarning() << "[Exporter] Failed to start ffmpeg process.";
        removePartialOutput(processOutputPath);
        return false;
    }

    QProgressDialog progress("Exporting processed video...", "Cancel", 0, 100, parentWidget);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QElapsedTimer timer;
    timer.start();

    int doneCount = preserveNativeVfr ? framesToExport : 0;
    for (int inputIndex = 0;
         !preserveNativeVfr && inputIndex < inputFramesToProcess;
         ++inputIndex) {
        if (progress.wasCanceled()) {
            cancelled = true;
            break;
        }

        int f = startFrame;
        if (useTimestampFrameMapping) {
            const double requestedTimestamp = sourceStartSeconds
                                            + static_cast<double>(inputIndex) / sourceSelectionOutputFps;
            int lo = startFrame;
            int hi = endFrame;
            while (lo < hi) {
                const int mid = lo + (hi - lo + 1) / 2;
                const double timestamp = decoder.getFrameTimestampSeconds(mid);
                if (std::isfinite(timestamp) && timestamp <= requestedTimestamp + 1e-9)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            f = lo;
        } else if (options.convertFpsPreserveDuration && options.customFps > 0.0) {
            // Timestamp-less elementary streams cannot be searched by PTS.
            // Fall back to CFR index resampling instead of repeatedly choosing
            // frame zero when every timestamp query returns NaN.
            const double sourceOffset = static_cast<double>(inputIndex)
                                      * sourceFps / sourceSelectionOutputFps;
            f = startFrame + static_cast<int>(std::floor(sourceOffset + 1e-9));
        } else {
            f += inputIndex * step;
        }
        f = std::clamp(f, startFrame, endFrame);

        QImage rawFrame = decoder.getFrameImage(f);
        if (rawFrame.isNull()) {
            appendBounded(diagnostics, QString("Failed to decode source frame %1.\n").arg(f).toUtf8());
            writeFailed = true;
            break;
        }

        QList<QImage> filteredFrames;
        if (applyFilters) {
            if (!VDQtFilterSystem::instance().processFrameSequence(rawFrame, filteredFrames)
                || filteredFrames.size() != filterFramesPerInput) {
                appendBounded(diagnostics, QString("The temporal filter chain failed at source frame %1.\n").arg(f).toUtf8());
                writeFailed = true;
                break;
            }
        } else {
            filteredFrames.append(rawFrame);
        }

        for (QImage filtered : filteredFrames) {
            if (filtered.isNull() || filtered.width() != outW || filtered.height() != outH) {
                appendBounded(diagnostics,
                    QString("Filter output dimensions changed at frame %1; expected %2x%3, got %4x%5.\n")
                        .arg(f).arg(outW).arg(outH).arg(filtered.width()).arg(filtered.height()).toUtf8());
                writeFailed = true;
                break;
            }
            if (filtered.format() != rawInputImageFormat)
                filtered = filtered.convertToFormat(rawInputImageFormat);

            if (!writeFrame(ffmpeg, filtered, progress, diagnostics, cancelled)) {
                writeFailed = !cancelled;
                break;
            }

            if (frameCallback)
                frameCallback(f, rawFrame, filtered);
            ++doneCount;
        }
        if (cancelled || writeFailed)
            break;

        int pct = static_cast<int>(95.0 * doneCount / framesToExport);
        progress.setValue(pct);

        double elapsedSec = timer.elapsed() / 1000.0;
        double currentFps = (elapsedSec > 0) ? (doneCount / elapsedSec) : 0;
        double remainingSec = (currentFps > 0) ? ((framesToExport - doneCount) / currentFps) : 0;

        progress.setLabelText(QString("Exporting frame %1 of %2 (%3%)\nSpeed: %4 fps | ETA: %5s")
            .arg(doneCount)
            .arg(framesToExport)
            .arg(static_cast<int>(100.0 * doneCount / framesToExport))
            .arg(currentFps, 0, 'f', 1)
            .arg(static_cast<int>(remainingSec)));

        QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
    }

    bool processOk = false;
    if (preserveNativeVfr) {
        progress.setLabelText("Encoding timestamped variable-frame-rate video...");
        progress.setValue(50);
        QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
        processOk = waitForProcess(ffmpeg, progress, diagnostics, cancelled);
    } else if (cancelled || writeFailed) {
        stopProcess(ffmpeg);
        drainProcessOutput(ffmpeg, diagnostics);
    } else {
        ffmpeg.closeWriteChannel();
        progress.setLabelText("Finalizing video stream and container metadata...");
        progress.setValue(96);
        QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
        processOk = waitForProcess(ffmpeg, progress, diagnostics, cancelled);
    }

    progress.setValue(100);
    QApplication::processEvents();
    progress.close();

    const bool encoded = !cancelled && !writeFailed && processOk
                      && doneCount == framesToExport && validOutputFile(processOutputPath);
    const bool success = encoded && replaceWithStagedFile(processOutputPath, options.outputPath);
    if (!success) {
        removePartialOutput(processOutputPath);
        QString errOutput = QString::fromUtf8(diagnostics);
        if (encoded)
            errOutput = "The completed output could not be committed to its destination.";
        if (writeFailed && errOutput.trimmed().isEmpty())
            errOutput = "The FFmpeg input pipe closed before all frames were written.";
        qWarning() << "[Exporter] FFmpeg export error:" << errOutput;
        VDLogWindow::instance(parentWidget)->appendLog(QString("[Export Error] %1").arg(errOutput));
        if (parentWidget && !cancelled) {
            QString userMsg = "Video export failed.\n\n";
            if (options.audioMode == AudioMode_DirectStreamCopy && isDirectCopyMediaAudio) {
                userMsg += "Note: In Audio -> Direct stream copy mode, the destination container must support the source audio codec natively (for example, standard AVI cannot encapsulate Opus audio).\n\n"
                           "To resolve:\n"
                           "1. Switch Audio to 'Full processing mode' to re-encode.\n"
                           "2. Or export to MKV/MP4/MOV container that supports this audio codec.\n\nDetails:\n";
            }
            userMsg += errOutput.trimmed();
            QMessageBox::critical(parentWidget, "Export Error", userMsg);
        }
    }

    return success;
}
