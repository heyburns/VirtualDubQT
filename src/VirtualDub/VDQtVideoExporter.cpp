#include "VDQtVideoExporter.h"
#include "VDQtCodecSettings.h"
#include "VDQtCodecEngine.h"
#include "VDQtAudioPlayer.h"
#include <QProcess>
#include <QProgressDialog>
#include <QApplication>
#include <QElapsedTimer>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QMessageBox>
#include "VDQtDialogs.h"
extern "C" {
#include <libavcodec/avcodec.h>
}

VDQtVideoExporter::VDQtVideoExporter() {}

VDQtVideoExporter::~VDQtVideoExporter() {}

bool VDQtVideoExporter::exportVideo(const ExportOptions& options,
                                    VDQtVideoDecoder *activeDecoder,
                                    VDQtAudioPlayer *audioPlayer,
                                    QWidget *parentWidget,
                                    std::function<void(int frameIndex, const QImage &rawFrame, const QImage &filteredFrame)> frameCallback) {
    if (options.inputPath.isEmpty() || options.outputPath.isEmpty()) return false;

    // Pre-flight check: Video encoder availability
    if (options.videoMode != VideoMode_DirectStreamCopy) {
        VDVideoCodecParams vParams = VDQtCodecEngine::instance().getVideoParams();
        QString err;
        if (!VDQtCodecEngine::instance().checkVideoEncoderAvailable(vParams.codecId, &err)) {
            if (parentWidget) QMessageBox::critical(parentWidget, "Video Encoder Not Available", err);
            return false;
        }
    }

    // Pre-flight check: Audio encoder availability
    if (audioPlayer && audioPlayer->hasAudio() && options.audioMode != AudioMode_DirectStreamCopy) {
        VDAudioCodecParams aParams = VDQtCodecEngine::instance().getAudioParams();
        QString audioCodec = aParams.codecId.isEmpty() ? VDQtCodecSettings::instance().getAudioConfig().codecId : aParams.codecId;
        QString err;
        if (!VDQtCodecEngine::instance().checkAudioEncoderAvailable(audioCodec, &err)) {
            if (parentWidget) QMessageBox::critical(parentWidget, "Audio Encoder Not Available", err);
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

    int totalFrames = decoder.getFrameCount();
    int startFrame = std::clamp(options.startFrame, 0, totalFrames - 1);
    int endFrame = (options.endFrame >= startFrame) ? std::clamp(options.endFrame, startFrame, totalFrames - 1) : (totalFrames - 1);
    int step = std::max(1, options.decimateFactor);
    int framesToExport = (endFrame - startFrame + step) / step;

    double fps = decoder.getFps();
    if (options.customFps > 0.0) {
        fps = options.customFps;
    } else if (step > 1 && fps > 0.0) {
        fps = fps / step;
    }
    if (fps <= 0.0) fps = 29.97;

    // 0. Direct Stream Copy Mode (For media files / containers)
    if (options.videoMode == VideoMode_DirectStreamCopy && !decoder.isAvsNative()) {
        QProcess ffmpeg;
        QStringList args;
        args << "-y";

        bool hasSelection = (startFrame > 0 || endFrame < totalFrames - 1);
        if (hasSelection) {
            double startTime = startFrame / fps;
            double duration = framesToExport / fps;
            args << "-ss" << QString::number(startTime, 'f', 4);
            args << "-i" << options.inputPath;
            args << "-t" << QString::number(duration, 'f', 4);
        } else {
            args << "-i" << options.inputPath;
        }

        args << "-map" << "0:v:0";
        args << "-c:v" << "copy";

        if (audioPlayer && audioPlayer->hasAudio()) {
            args << "-map" << "0:a:0?";
            if (options.audioMode == AudioMode_DirectStreamCopy) {
                args << "-c:a" << "copy";
            } else {
                VDAudioCodecParams aParams = VDQtCodecEngine::instance().getAudioParams();
                QString audioCodec = aParams.codecId.toLower();
                if (audioCodec == "aac" || audioCodec.isEmpty()) args << "-c:a" << "aac" << "-b:a" << "256k";
                else if (audioCodec == "libmp3lame" || audioCodec == "mp3") args << "-c:a" << "libmp3lame" << "-b:a" << "192k";
                else if (audioCodec == "libopus" || audioCodec == "opus") {
                    if (avcodec_find_encoder_by_name("libopus") != nullptr) args << "-c:a" << "libopus" << "-b:a" << "160k";
                    else args << "-c:a" << "opus" << "-strict" << "-2" << "-b:a" << "160k";
                }
                else if (audioCodec == "libvorbis" || audioCodec == "vorbis") args << "-c:a" << "libvorbis" << "-b:a" << "160k";
                else if (audioCodec == "flac") args << "-c:a" << "flac";
                else if (audioCodec == "ac3") args << "-c:a" << "ac3" << "-b:a" << "384k";
                else args << "-c:a" << "pcm_s16le";
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

        args << options.outputPath;

        qDebug() << "[Exporter] Direct Stream Copy ffmpeg args:" << args.join(" ");

        QProgressDialog progress("Direct stream copy in progress...", "Cancel", 0, 100, parentWidget);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(30);

        ffmpeg.start("ffmpeg", args);
        if (!ffmpeg.waitForStarted(3000)) {
            return false;
        }

        while (!ffmpeg.waitForFinished(100)) {
            if (progress.wasCanceled()) {
                ffmpeg.kill();
                return false;
            }
            QApplication::processEvents();
        }
        progress.setValue(100);
        return (ffmpeg.exitCode() == 0);
    }

    // Determine output resolution (reflecting any Resize filter if Full Processing Mode)
    QImage sampleFrame = decoder.getFrameImage(startFrame);
    if (sampleFrame.isNull()) {
        return false;
    }

    bool applyFilters = (options.videoMode == VideoMode_FullProcessing);
    int outW = sampleFrame.width();
    int outH = sampleFrame.height();
    if (applyFilters) {
        QImage filteredSample = VDQtFilterSystem::instance().processFrame(sampleFrame);
        outW = filteredSample.width();
        outH = filteredSample.height();
    }

    // Fetch user-configured codec settings
    VDVideoCodecParams vParams = VDQtCodecEngine::instance().getVideoParams();
    VDAudioCodecParams aParams = VDQtCodecEngine::instance().getAudioParams();

    // 1. Audio Source Configuration
    QString audioSrcMedia;
    if (options.audioMode == AudioMode_DirectStreamCopy) {
        if (!decoder.isAvsNative()) {
            audioSrcMedia = options.inputPath;
        } else {
            QString resolved = VDQtVideoDecoder::parseScriptSource(options.inputPath);
            if (!resolved.isEmpty() && QFile::exists(resolved)) {
                audioSrcMedia = resolved;
            }
        }
    }

    bool isDirectCopyMediaAudio = (!audioSrcMedia.isEmpty() && audioPlayer && audioPlayer->hasAudio());

    QString tempAudioPath;
    if (!isDirectCopyMediaAudio && audioPlayer && audioPlayer->hasAudio()) {
        tempAudioPath = QString("/tmp/vd_export_audio_%1_%2.wav").arg(QCoreApplication::applicationPid()).arg(QDateTime::currentMSecsSinceEpoch());
        
        int sampleRate = 48000;
        if (decoder.isAvsNative() && decoder.getAvsVi() && decoder.getAvsVi()->audio_samples_per_second > 0) {
            sampleRate = decoder.getAvsVi()->audio_samples_per_second;
        }
        if (sampleRate <= 0) sampleRate = 48000;

        int64_t startSample = (int64_t)std::round(startFrame * ((double)sampleRate / fps));
        int64_t sampleCount = (int64_t)std::round(framesToExport * ((double)sampleRate / fps));

        if (audioPlayer->exportAudioToFile(tempAudioPath, startSample, sampleCount)) {
            qDebug() << "[Exporter] Successfully prepared audio stream for export (samples:" << startSample << "count:" << sampleCount << "):" << tempAudioPath;
        } else {
            tempAudioPath.clear();
        }
    }

    // 2. FFmpeg input arguments (ALL inputs before encoding flags)
    QProcess ffmpeg;
    QStringList args;

    args << "-y"
         << "-f" << "rawvideo"
         << "-pix_fmt" << "rgb24"
         << "-s" << QString("%1x%2").arg(outW).arg(outH)
         << "-r" << QString::number(fps, 'f', 4)
         << "-i" << "-"; // input 0: raw video stream from stdin

    bool hasAudioInput = false;
    if (isDirectCopyMediaAudio) {
        hasAudioInput = true;
        bool hasSelection = (startFrame > 0 || endFrame < totalFrames - 1);
        if (hasSelection) {
            double startTime = (double)startFrame / fps;
            double duration = (double)framesToExport / fps;
            args << "-ss" << QString::number(startTime, 'f', 4)
                 << "-i" << audioSrcMedia
                 << "-t" << QString::number(duration, 'f', 4);
        } else {
            args << "-i" << audioSrcMedia;
        }
    } else if (!tempAudioPath.isEmpty() && QFile::exists(tempAudioPath)) {
        hasAudioInput = true;
        args << "-i" << tempAudioPath;
    }

    // 3. Stream mappings
    args << "-map" << "0:v:0";
    if (hasAudioInput) {
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
        outPixFmt = "bgr24";
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
                outPixFmt = "yuv444p10le";
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
        args << "-pix_fmt" << outPixFmt;
    }

    // 5. Audio Codec & Options
    if (hasAudioInput) {
        if (options.audioMode == AudioMode_DirectStreamCopy) {
            if (isDirectCopyMediaAudio) {
                args << "-c:a" << "copy";
            } else {
                args << "-c:a" << "pcm_s16le";
            }
        } else {
            QString audioCodec = aParams.codecId.toLower();
            if (audioCodec.isEmpty()) {
                VDAudioCodecConfig aCfg = VDQtCodecSettings::instance().getAudioConfig();
                audioCodec = aCfg.codecId.toLower();
                aParams.rateMode = aCfg.rateControlMode;
                aParams.vbrQuality = aCfg.vbrQuality;
                if (aParams.bitrateKbps <= 0) aParams.bitrateKbps = aCfg.bitrateKbps;
                if (aParams.sampleRate <= 0) aParams.sampleRate = aCfg.sampleRate;
                if (aParams.channels <= 0) aParams.channels = aCfg.channels;
            }

            bool isVbr = (aParams.rateMode.toLower() == "vbr");

            if (audioCodec == "aac" || audioCodec.contains("aac")) {
                args << "-c:a" << "aac";
                if (isVbr) {
                    // Map VBR quality slider (1..5) to FFmpeg native AAC -q:a (0.2 .. 2.0)
                    static const double aacQ[] = { 0.2, 0.5, 0.9, 1.4, 2.0 };
                    int qIdx = std::clamp(aParams.vbrQuality - 1, 0, 4);
                    args << "-q:a" << QString::number(aacQ[qIdx], 'f', 2);
                } else {
                    int br = (aParams.bitrateKbps > 0) ? aParams.bitrateKbps : 192;
                    args << "-b:a" << QString("%1k").arg(br);
                }
            } else if (audioCodec == "libmp3lame" || audioCodec == "mp3") {
                args << "-c:a" << "libmp3lame";
                if (isVbr) {
                    int vLevel = std::clamp(aParams.vbrQuality, 0, 9);
                    args << "-q:a" << QString::number(vLevel);
                } else {
                    int br = (aParams.bitrateKbps > 0) ? aParams.bitrateKbps : 192;
                    args << "-b:a" << QString("%1k").arg(br);
                }
            } else if (audioCodec == "libopus" || audioCodec == "opus") {
                if (avcodec_find_encoder_by_name("libopus") != nullptr) {
                    args << "-c:a" << "libopus";
                } else {
                    args << "-c:a" << "opus" << "-strict" << "-2";
                }
                int br = (aParams.bitrateKbps > 0) ? aParams.bitrateKbps : 160;
                args << "-b:a" << QString("%1k").arg(br);
                if (isVbr) {
                    args << "-vbr" << "on";
                } else {
                    args << "-vbr" << "off";
                }
            } else if (audioCodec == "libvorbis" || audioCodec == "vorbis") {
                args << "-c:a" << "libvorbis";
                if (isVbr) {
                    args << "-q:a" << QString::number(aParams.vbrQuality);
                } else {
                    int br = (aParams.bitrateKbps > 0) ? aParams.bitrateKbps : 160;
                    args << "-b:a" << QString("%1k").arg(br);
                }
            } else if (audioCodec == "flac") {
                args << "-c:a" << "flac";
                int compLevel = std::clamp(aParams.vbrQuality, 0, 8);
                if (compLevel == 0 && aParams.rateMode != "vbr") compLevel = 5;
                args << "-compression_level" << QString::number(compLevel);
            } else if (audioCodec == "ac3") {
                args << "-c:a" << "ac3";
                int br = (aParams.bitrateKbps > 0) ? aParams.bitrateKbps : 384;
                args << "-b:a" << QString("%1k").arg(br);
            } else if (audioCodec == "pcm_s16le" || audioCodec.contains("pcm") || audioCodec == "(uncompressed)") {
                args << "-c:a" << "pcm_s16le";
            } else {
                args << "-c:a" << audioCodec;
                if (aParams.bitrateKbps > 0) {
                    args << "-b:a" << QString("%1k").arg(aParams.bitrateKbps);
                }
            }

            if (audioCodec == "libopus" || audioCodec == "opus") {
                int opusRate = aParams.sampleRate;
                if (opusRate != 8000 && opusRate != 12000 && opusRate != 16000 && opusRate != 24000 && opusRate != 48000) {
                    opusRate = 48000;
                }
                args << "-ar" << QString::number(opusRate);
            } else if (audioCodec == "ac3") {
                int ac3Rate = aParams.sampleRate;
                if (ac3Rate != 48000 && ac3Rate != 44100 && ac3Rate != 32000) {
                    ac3Rate = 48000;
                }
                args << "-ar" << QString::number(ac3Rate);
            } else if (aParams.sampleRate > 0 && aParams.sampleRate <= 192000) {
                args << "-ar" << QString::number(aParams.sampleRate);
            }
            if (aParams.channels > 0 && aParams.channels <= 8) {
                args << "-ac" << QString::number(aParams.channels);
            }
        }

        args << "-shortest";
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

    args << options.outputPath;

    qDebug() << "[Exporter] Launching ffmpeg with args:" << args.join(" ");

    ffmpeg.start("ffmpeg", args);
    if (!ffmpeg.waitForStarted(3000)) {
        qWarning() << "[Exporter] Failed to start ffmpeg process.";
        if (!tempAudioPath.isEmpty() && QFile::exists(tempAudioPath)) {
            QFile::remove(tempAudioPath);
        }
        return false;
    }

    QProgressDialog progress("Exporting processed video...", "Cancel", 0, 100, parentWidget);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QElapsedTimer timer;
    timer.start();

    int doneCount = 0;
    for (int f = startFrame; f <= endFrame; f += step) {
        if (progress.wasCanceled()) {
            ffmpeg.kill();
            return false;
        }

        QImage rawFrame = decoder.getFrameImage(f);
        if (rawFrame.isNull()) continue;

        QImage filtered = applyFilters ? VDQtFilterSystem::instance().processFrame(rawFrame) : rawFrame;
        if (filtered.format() != QImage::Format_RGB888) {
            filtered = filtered.convertToFormat(QImage::Format_RGB888);
        }

        // Write scanlines to FFmpeg stdin pipe
        for (int y = 0; y < filtered.height(); ++y) {
            ffmpeg.write((const char*)filtered.constScanLine(y), filtered.width() * 3);
        }
        if (doneCount % 2 == 0) {
            ffmpeg.waitForBytesWritten(50);
        }

        if (frameCallback) {
            frameCallback(f, rawFrame, filtered);
        }

        doneCount++;
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

        QApplication::processEvents();
    }

    // Phase 2: Finalization & container muxing (active event processing loop to prevent UI freezing)
    ffmpeg.closeWriteChannel();
    progress.setLabelText("Finalizing video stream and container metadata...");
    progress.setValue(96);
    QApplication::processEvents();

    while (ffmpeg.state() != QProcess::NotRunning) {
        if (progress.wasCanceled()) {
            ffmpeg.kill();
            break;
        }
        ffmpeg.waitForFinished(50);
        QApplication::processEvents();
    }

    progress.setValue(100);
    QApplication::processEvents();
    progress.close();

    if (!tempAudioPath.isEmpty() && QFile::exists(tempAudioPath)) {
        QFile::remove(tempAudioPath);
    }

    if (ffmpeg.exitCode() != 0) {
        QString errOutput = QString::fromUtf8(ffmpeg.readAllStandardError());
        qWarning() << "[Exporter] FFmpeg export error:" << errOutput;
        VDLogWindow::instance(parentWidget)->appendLog(QString("[Export Error] %1").arg(errOutput));
        if (parentWidget) {
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

    return (ffmpeg.exitCode() == 0);
}
