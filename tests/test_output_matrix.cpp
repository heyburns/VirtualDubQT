// Actual imports and selected embedded-PCM outputs, not codec
// troubleshooting. All fixtures/settings/queues are owned temporary data.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtMainWindow.h"
#include "VirtualDub/VDQtAudioPlayer.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
bool check(bool condition, const QString& message) {
    if (!condition) std::cerr << "FAIL: " << message.toStdString() << '\n';
    else std::cout << "PASS: " << message.toStdString() << '\n';
    return condition;
}
QByteArray read(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QByteArray packed(const QImage& input) {
    QImage image = input.convertToFormat(QImage::Format_RGB888);
    QByteArray result;
    for (int row = 0; row < image.height(); ++row)
        result.append(reinterpret_cast<const char*>(image.constScanLine(row)), image.width() * 3);
    return result;
}
QList<double> pts(const QString& path) {
    QProcess process;
    process.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_frames",
        "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", path});
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
        process.kill(); process.waitForFinished(5000); return {};
    }
    if (process.exitCode() != 0) return {};
    QList<double> result;
    for (const auto& entry : QJsonDocument::fromJson(process.readAllStandardOutput()).object()["frames"].toArray()) {
        bool valid = false;
        double value = entry.toObject()["best_effort_timestamp_time"].toString().toDouble(&valid);
        if (!valid) return {};
        result.append(value);
    }
    return result;
}
double averageFrameRate(const QString& path) {
    QProcess process;
    process.start("ffprobe", {"-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=avg_frame_rate", "-of", "json", path});
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
        process.kill(); process.waitForFinished(5000); return 0;
    }
    const auto streams = QJsonDocument::fromJson(process.readAllStandardOutput()).object()["streams"].toArray();
    if (process.exitCode() != 0 || streams.isEmpty()) return 0;
    const auto rational = streams.first().toObject()["avg_frame_rate"].toString().split('/');
    if (rational.size() != 2) return 0;
    bool validNumerator = false, validDenominator = false;
    const double numerator = rational[0].toDouble(&validNumerator);
    const double denominator = rational[1].toDouble(&validDenominator);
    return validNumerator && validDenominator && numerator > 0 && denominator > 0
        ? numerator / denominator : 0;
}
bool queueJob(VDQtMainWindow& window, const VDQtJobState& job) {
    auto* queue = window.findChild<VDQtJobQueue*>();
    QString error;
    if (!queue || !queue->replaceJobs({job}, &error)) {
        std::cerr << "Queue admission: " << error.toStdString() << '\n'; return false;
    }
    if (!QMetaObject::invokeMethod(&window, "runPendingJobs", Qt::DirectConnection)) return false;
    auto* record = queue->jobAt(0);
    if (!record || record->status != VDQtJobStatus::Complete) {
        std::cerr << "Queue export: " << (record ? record->error.toStdString() : "missing record") << '\n';
        return false;
    }
    return true;
}
bool outputPixels(VDQtTestFixtures& fixtures, const QString& output,
                  const QByteArray& expected, const QList<double>& expectedPts) {
    const QString decoded = output + ".rgb";
    if (!fixtures.ffmpeg({"-i", output, "-map", "0:v:0", "-fps_mode", "passthrough",
        "-pix_fmt", "rgb24", "-c:v", "rawvideo", "-threads", "1", "-f", "rawvideo", decoded})) return false;
    const QByteArray actual = read(decoded);
    bool passed = check(actual == expected, output.section('/', -1) + ": selected count and every lossless RGB sample");
    if (actual != expected)
        std::cerr << "RGB expected=" << expected.size() << " actual=" << actual.size() << '\n';
    const auto times = pts(output);
    bool valid = times.size() == expectedPts.size();
    for (int index = 0; valid && index < times.size(); ++index)
        valid = std::abs(times[index] - expectedPts[index]) <= 0.0011;
    passed &= check(valid, output.section('/', -1) + ": independent count and rational presentation timestamps");
    if (!valid) {
        std::cerr << "PTS expected=";
        for (double time : expectedPts) std::cerr << time << ',';
        std::cerr << " actual="; for (double time : times) std::cerr << time << ',';
        std::cerr << '\n';
    }
    return passed;
}
VDQtJobState losslessJob(const QString& output, int mode) {
    VDQtJobState job;
    job.operation = VDQtJobOperation::VideoExport;
    job.audioDisabled = true;
    job.options.outputPath = output;
    job.options.containerType = "mkv";
    job.options.includeAudio = false;
    job.options.videoMode = mode;
    job.processing.videoMode = mode;
    job.processing.videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
    job.processing.videoCodec.pixFmt = "bgr0";
    job.processing.videoCodec.ffv1Slices = 4;
    job.options.startFrame = 1;
    job.options.endFrame = 4;
    return job;
}
bool importedSources(VDQtTestFixtures& fixtures) {
    constexpr int width = 18, height = 14, frames = 6;
    const double fps = 30000.0 / 1001;
    QByteArray all, expected;
    QStringList images;
    for (int frame = 0; frame < frames; ++frame) {
        QImage image = VDQtTestFixtures::patternedImage(width, height, QImage::Format_RGB888, frame * 31);
        const QByteArray bytes = packed(image);
        all += bytes;
        if (frame >= 1 && frame <= 4) expected += bytes;
        const QString path = fixtures.directory.filePath(QString("import-%1.png").arg(frame));
        if (!image.save(path)) return false;
        images.append(path);
    }
    const QString raw = fixtures.directory.filePath("input.rgb");
    const QByteArray header("raw-input-header!");
    if (!fixtures.writeText(raw, header + all)) return false;
    const auto sourceBytes = read(raw);
    QList<double> times; for (int frame = 0; frame < 4; ++frame) times.append(frame / fps);
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    bool passed = true;
    for (bool image : {false, true}) for (int mode : {VideoMode_FastRecompress, VideoMode_FullProcessing}) {
        const QString name = QString("%1-mode%2.mkv").arg(image ? "images" : "raw").arg(mode);
        auto job = losslessJob(fixtures.directory.filePath(name), mode);
        if (image) { job.sourcePaths = images; job.imageSequenceFps = fps; }
        else {
            job.sourcePaths = {raw}; job.rawPixelFormat = "rgb24";
            job.rawWidth = width; job.rawHeight = height;
            job.rawFrameRate = fps; job.rawByteOffset = header.size();
        }
        if (!queueJob(window, job)) { passed = check(false, name + ": real imported-source queue export") && passed; continue; }
        passed &= outputPixels(fixtures, job.options.outputPath, expected, times);
        passed &= check(read(raw) == sourceBytes, name + ": original raw input remains unchanged");
    }
    return passed;
}
void append16(QByteArray& bytes, quint16 value) { char out[2]; qToLittleEndian(value, out); bytes.append(out, 2); }
void append32(QByteArray& bytes, quint32 value) { char out[4]; qToLittleEndian(value, out); bytes.append(out, 4); }
QByteArray wave(const QByteArray& pcm, int rate) {
    QByteArray wav("RIFF"); append32(wav, 36 + pcm.size()); wav += "WAVEfmt "; append32(wav, 16);
    append16(wav, 1); append16(wav, 1); append32(wav, rate); append32(wav, rate * 2);
    append16(wav, 2); append16(wav, 16); wav += "data"; append32(wav, pcm.size()); wav += pcm;
    return wav;
}
bool embeddedPcm(VDQtTestFixtures& fixtures) {
    constexpr int width = 32, height = 24, rate = 48000, frames = 20;
    QByteArray rgb, expectedVideo;
    for (int frame = 0; frame < frames; ++frame) {
        const auto bytes = packed(VDQtTestFixtures::patternedImage(width, height, QImage::Format_RGB888, frame * 11));
        rgb += bytes;
        if (frame < 8) expectedVideo += bytes;
    }
    QByteArray pcm;
    for (int sample = 0; sample < rate * frames / 25; ++sample)
        append16(pcm, quint16(qint16((sample * 73 + (sample / 113) * 19) % 60001 - 30000)));
    const QString raw = fixtures.directory.filePath("avi-video.rgb");
    const QString audio = fixtures.directory.filePath("avi-audio.wav");
    const QString source = fixtures.directory.filePath("embedded-pcm.avi");
    if (!fixtures.writeText(raw, rgb) || !fixtures.writeText(audio, wave(pcm, rate))
        || !fixtures.ffmpeg({"-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "32x24",
            "-framerate", "25", "-i", raw, "-i", audio, "-c:v", "ffv1", "-g", "1", "-pix_fmt", "bgr0",
            "-threads", "1", "-c:a", "pcm_s16le", source})) return false;
    // Begin at source zero and end on both a video frame boundary and exact
    // PCM packet boundary. Arbitrary AVI audio-packet preroll is a separate
    // stream-copy granularity limit, not a sample-exact contract for this test.
    const QByteArray expectedAudio = pcm.left(8 * rate / 25 * 2);
    const auto unchanged = read(source);
    QList<double> times; for (int frame = 0; frame < 8; ++frame) times.append(frame / 25.0);
    VDQtVideoDecoder decoder; VDQtAudioPlayer player(false);
    if (!decoder.openFile(source) || !player.openFile(source) || !player.hasAudio()) return false;
    if (decoder.ensureFrameIndex().totalFrames != frames) return false;
    bool allKeyframes = true;
    for (int frame = 0; frame < frames; ++frame) allKeyframes &= decoder.isKeyFrame(frame);
    if (!check(allKeyframes, "every AVI source frame is independently verified keyframe")) return false;
    bool passed = true;
    for (int mode : {VideoMode_DirectStreamCopy, VideoMode_FastRecompress}) {
        VDQtVideoExporter exporter;
        VDQtVideoExporter::ExportOptions request;
        request.inputPath = source;
        request.outputPath = fixtures.directory.filePath(QString("avi-selected-mode%1.avi").arg(mode));
        request.containerType = "avi"; request.videoMode = mode; request.audioMode = AudioMode_DirectStreamCopy;
        request.startFrame = 0; request.endFrame = 7;
        request.includeAudio = true; request.unattended = true;
        request.processing = VDQtVideoExporter::ProcessingSnapshot{};
        request.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
        request.processing->videoCodec.pixFmt = "bgr0"; request.processing->videoCodec.ffv1Slices = 4;
        if (!exporter.exportVideo(request, &decoder, &player)) {
            passed = check(false, "AVI selected export: " + exporter.lastError()) && passed; continue;
        }
        passed &= outputPixels(fixtures, request.outputPath, expectedVideo, times);
        const QString decoded = request.outputPath + ".pcm";
        if (!fixtures.ffmpeg({"-i", request.outputPath, "-map", "0:a:0", "-f", "s16le", "-acodec", "pcm_s16le", decoded})) {
            passed = check(false, "AVI selected audio remains mapped: " + fixtures.error) && passed; continue;
        }
        const QByteArray actual = read(decoded);
        passed &= check(actual == expectedAudio, request.outputPath.section('/', -1) + ": selected PCM content and exact duration");
        if (actual != expectedAudio) {
            std::cerr << "PCM expected=" << expectedAudio.size() << " actual=" << actual.size()
                      << " expectedfirst=" << expectedAudio.left(12).toHex().toStdString()
                      << " actualfirst=" << actual.left(12).toHex().toStdString() << '\n';
            const qsizetype offset = pcm.indexOf(actual.left(64));
            std::cerr << "Actual starts at original source sample=" << offset / 2
                      << " expected=0\n";
        }
        passed &= check(read(source) == unchanged, "AVI source remains unchanged");
    }
    return passed;
}
bool editedVfrJoint(VDQtTestFixtures& fixtures) {
    constexpr int rate = 48000;
    QByteArray rgb, pcm;
    QList<QByteArray> sourceFrames, allSourceFrames;
    for (int frame = 0; frame < 24; ++frame) {
        const auto bytes = packed(VDQtTestFixtures::patternedImage(32, 24, QImage::Format_RGB888, frame * 17));
        rgb += bytes;
        allSourceFrames.append(bytes);
        if (frame < 12 || frame % 3 == 0) sourceFrames.append(bytes);
    }
    for (int sample = 0; sample < rate; ++sample)
        append16(pcm, quint16(qint16((sample * 31 + (sample / 127) * 17) % 50001 - 25000)));
    const QString raw = fixtures.directory.filePath("vfr-input.rgb");
    const QString audio = fixtures.directory.filePath("vfr-input.wav");
    const QString source = fixtures.directory.filePath("vfr-joint-source.mkv");
    if (!fixtures.writeText(raw, rgb) || !fixtures.writeText(audio, wave(pcm, rate))
        || !fixtures.ffmpeg({"-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "32x24",
            "-framerate", "24", "-i", raw, "-i", audio,
            "-vf", "select='if(lt(n,12),1,not(mod(n,3)))'", "-fps_mode", "vfr",
            "-c:v", "ffv1", "-g", "1", "-pix_fmt", "bgr0", "-threads", "1",
            "-c:a", "pcm_s16le", source})) return false;
    const auto sourcePts = pts(source);
    if (!check(sourcePts.size() == 16, "joint VFR fixture independently has sixteen pictures")) return false;
    const double nominalRate = averageFrameRate(source);
    VDQtVideoDecoder rateDecoder;
    if (!check(nominalRate > 0 && std::isfinite(nominalRate) && rateDecoder.openFile(source)
        && std::abs(rateDecoder.getFps() - nominalRate) < 1e-9,
        "independent source metadata verifies the nominal cadence used for explicit gap collapse")) return false;
    const QList<VDQtTimelineSegment> edits{{12, 2, false}, {0, 2, false}, {2, 2, true}, {14, 1, false}};
    const QList<int> outputOrdinals{12, 13, 0, 1, 2, 3, 14};
    const QList<int> pictureOrdinals{12, 13, 0, 1, 1, 1, 14};
    QByteArray expectedVideo, expectedAudio;
    QList<double> expectedPts;
    double clock = 0;
    for (int index = 0; index < outputOrdinals.size(); ++index) {
        const int ordinal = outputOrdinals[index];
        expectedVideo += sourceFrames[pictureOrdinals[index]];
        expectedPts.append(clock);
        clock += sourcePts[ordinal + 1] - sourcePts[ordinal];
    }
    QList<double> editedBoundaries = expectedPts;
    editedBoundaries.append(clock);
    // Existing edit-list exports deliberately use average CFR while preserving
    // the complete edited duration. Native per-frame VFR after reordering is a
    // documented parity gap, not an added feature in this regression. The
    // averaged duration must nevertheless use the underlying masked interval,
    // not the held picture's duration, and retain all edited audio samples.
    for (int index = 0; index < expectedPts.size(); ++index)
        expectedPts[index] = index * clock / expectedPts.size();
    // Video masks hold the preceding picture, not the soundtrack: the source
    // audio from the masked interval remains audible by the existing contract.
    for (const auto& segment : edits) {
        const qint64 first = std::llround(sourcePts[segment.sourceStartFrame] * rate);
        const qint64 end = std::llround(sourcePts[segment.sourceStartFrame + segment.frameCount] * rate);
        expectedAudio += pcm.mid(first * 2, (end - first) * 2);
    }
    auto job = losslessJob(fixtures.directory.filePath("vfr-joint-queue.mkv"), VideoMode_FullProcessing);
    job.sourcePaths = {source}; job.audioDisabled = false;
    job.options.includeAudio = true; job.options.audioMode = AudioMode_FullProcessing;
    job.processing.audioMode = AudioMode_FullProcessing;
    job.processing.audioCodec.codecId = "pcm_s16le";
    job.options.startFrame = 0; job.options.endFrame = 6;
    job.options.timelineExplicit = true; job.options.timelineSegments = edits;
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    if (!queueJob(window, job)) return false;
    bool passed = outputPixels(fixtures, job.options.outputPath, expectedVideo, expectedPts);
    const auto compareAudio = [&](const QString& output) {
        const QString decoded = output + ".pcm";
        if (!fixtures.ffmpeg({"-i", output, "-map", "0:a:0", "-f", "s16le", "-acodec", "pcm_s16le", decoded})) return false;
        const auto actual = read(decoded);
        if (actual != expectedAudio)
            std::cerr << "Joint audio expected=" << expectedAudio.size() << " actual=" << actual.size() << '\n';
        return check(actual == expectedAudio, output.section('/', -1) + ": reordered/masked VFR audio exact content and edited duration");
    };
    passed &= compareAudio(job.options.outputPath);
    if (!window.openVideoFile(source)) return false;
    VDQtCodecEngine::instance().setVideoParams(job.processing.videoCodec);
    VDQtCodecEngine::instance().setAudioParams(job.processing.audioCodec);
    const QString scriptOutput = fixtures.directory.filePath("vfr-joint-script.mkv");
    const QString script = QString(
        "VirtualDub.video.SetMode(3); VirtualDub.audio.SetSource(1); VirtualDub.audio.SetMode(1); "
        "VirtualDub.audio.SetCompression(); VirtualDub.SaveFormat(\"mkv\"); "
        "VirtualDub.subset.Clear(); VirtualDub.subset.AddRange(12,2); VirtualDub.subset.AddRange(0,2); "
        "VirtualDub.subset.AddMaskedRange(2,2); VirtualDub.subset.AddRange(14,1); "
        "VirtualDub.video.SetRangeFrames(0,7); VirtualDub.SaveAVI(\"%1\");").arg(scriptOutput);
    QString error;
    if (!window.runAutomationText(script, fixtures.directory.path(), &error)) {
        std::cerr << "Joint script: " << error.toStdString() << '\n'; return false;
    }
    passed &= outputPixels(fixtures, scriptOutput, expectedVideo, expectedPts);
    passed &= compareAudio(scriptOutput);
    // Explicit conversion samples the actual cumulative edited intervals,
    // unlike the default average-CFR export tested above. The oracle uses only
    // independently probed source PTS and original fixture pixels: it does not
    // call the production timeline/context/mapping helpers. Inspect every raw
    // and independently decoded rendered frame, not only callbacks or counts.
    enum class ConversionOutput { Raw, Full, FullCollapsed };
    for (const auto& selection : {QPair<int, int>{0, 6}, {1, 5}, {4, 5}}) {
        const int first = selection.first, last = selection.second;
        const double begin = editedBoundaries[first];
        for (auto output : {ConversionOutput::Raw, ConversionOutput::Full, ConversionOutput::FullCollapsed}) {
            const bool rawOutput = output == ConversionOutput::Raw;
            const bool preserveIntervals = output != ConversionOutput::FullCollapsed;
            const double duration = preserveIntervals
                ? editedBoundaries[last + 1] - begin : (last - first + 1) / nominalRate;
            const double targetRate = rawOutput ? 500 : 100;
            const qint64 count = std::llround(duration * targetRate);
            QByteArray convertedPixels;
            QList<double> convertedPts;
            for (qint64 index = 0; index < count; ++index) {
                const double time = begin + index / targetRate;
                int ordinal = first;
                if (preserveIntervals) {
                    while (ordinal < last && editedBoundaries[ordinal + 1] <= time + 1e-9)
                        ++ordinal;
                } else {
                    // Collapsing gaps deliberately ignores actual source PTS.
                    // Derive the selected cadence from independent metadata,
                    // then floor/restrict the nominal edited ordinal before
                    // applying the independently specified held-picture map.
                    ordinal = std::clamp(first + int(std::floor(index * nominalRate / targetRate)), first, last);
                }
                convertedPixels += sourceFrames[pictureOrdinals[ordinal]];
                convertedPts.append(index / targetRate);
            }
            const QString stem = QString("vfr-converted-%1-%2-%3")
                .arg(rawOutput ? "raw" : preserveIntervals ? "full" : "full-collapsed").arg(first).arg(last);
            VDQtVideoDecoder conversionDecoder;
            if (!conversionDecoder.openFile(source)) return false;
            VDQtVideoExporter exporter;
            if (rawOutput) {
                VDQtVideoExporter::RawExportOptions request;
                request.inputPath = source; request.outputPath = fixtures.directory.filePath(stem + ".rgb");
                request.pixelFormat = "rgb24"; request.scanlineAlignment = 1; request.swapChromaPlanes = false;
                request.startFrame = first; request.endFrame = last;
                request.customFps = targetRate; request.convertFpsPreserveDuration = true;
                request.timelineExplicit = true; request.timelineSegments = edits;
                request.unattended = true; request.processing = VDQtVideoExporter::ProcessingSnapshot{};
                if (!exporter.exportRawVideo(request, &conversionDecoder)) return false;
                const auto actual = read(request.outputPath);
                passed &= check(actual == convertedPixels,
                    stem + ": all converted pixels/count obey actual intervals, held masks and clipped selection");
                if (actual != convertedPixels)
                    std::cerr << "Raw expected frames=" << count << " actual=" << actual.size() / (32 * 24 * 3) << '\n';
            } else {
                VDQtVideoExporter::ExportOptions request;
                request.inputPath = source; request.outputPath = fixtures.directory.filePath(stem + ".mkv");
                request.videoMode = VideoMode_FullProcessing; request.containerType = "mkv";
                request.startFrame = first; request.endFrame = last;
                request.customFps = targetRate; request.convertFpsPreserveDuration = true;
                request.timelineExplicit = true; request.timelineSegments = edits;
                request.unattended = true; request.includeAudio = false;
                request.preserveEmptyFrames = preserveIntervals;
                request.processing = VDQtVideoExporter::ProcessingSnapshot{};
                request.processing->videoCodec = job.processing.videoCodec;
                if (!exporter.exportVideo(request, &conversionDecoder)) return false;
                passed &= outputPixels(fixtures, request.outputPath, convertedPixels, convertedPts);
            }
        }
    }
    // Collapsing a genuinely different source clock while retaining sound is
    // not an implemented synchronized retime operation. It must fail clearly
    // before publishing/replacing output, instead of silently cutting audio.
    VDQtAudioPlayer collapseAudio(false);
    VDQtVideoDecoder collapseDecoder;
    if (!collapseDecoder.openFile(source) || !collapseAudio.openFile(source)
        || !collapseAudio.hasAudio()) return false;
    for (bool uniformLongIntervals : {false, true}) {
        VDQtVideoExporter exporter;
        VDQtVideoExporter::ExportOptions request = job.options;
        request.inputPath = source;
        request.outputPath = fixtures.directory.filePath(
            uniformLongIntervals ? "collapse-with-audio-new.mkv" : "collapse-with-audio-marker.mkv");
        request.startFrame = 0; request.endFrame = uniformLongIntervals ? 1 : 6;
        request.preserveEmptyFrames = false; request.unattended = true;
        request.processing = VDQtVideoExporter::ProcessingSnapshot{};
        request.processing->videoCodec = job.processing.videoCodec;
        request.processing->audioCodec = job.processing.audioCodec;
        const QByteArray marker("existing audio/video destination must survive");
        if (!uniformLongIntervals && !fixtures.writeText(request.outputPath, marker)) return false;
        const bool exported = exporter.exportVideo(request, &collapseDecoder, &collapseAudio);
        const bool untouched = uniformLongIntervals ? !QFile::exists(request.outputPath)
            : read(request.outputPath) == marker;
        passed &= check(!exported && !exporter.wasCancelled() && untouched
            && exporter.lastError().contains("Collapsing") && exporter.lastError().contains("audio timeline"),
            uniformLongIntervals
                ? "uniform long-interval audio collapse fails clearly without creating output"
                : "mixed VFR audio collapse fails clearly without replacing existing output");
        if (exported || exporter.lastError().isEmpty())
            std::cerr << "Unsupported audio collapse returned success=" << exported
                      << " error=" << exporter.lastError().toStdString() << '\n';
    }

    // Do not broaden that refusal to ordinary edited CFR with sound. NUT keeps
    // the exact rational source clock, avoiding Matroska's millisecond rounding
    // being mistaken for genuine VFR. Independent metadata/PTS verify cadence.
    const QString cfrSource = fixtures.directory.filePath("cfr-edited-audio-source.nut");
    if (!fixtures.ffmpeg({"-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "32x24",
            "-framerate", "24", "-i", raw, "-i", audio, "-c:v", "ffv1", "-g", "1",
            "-pix_fmt", "bgr0", "-threads", "1", "-c:a", "pcm_s16le", cfrSource})) return false;
    const auto cfrTimes = pts(cfrSource);
    const double cfrRate = averageFrameRate(cfrSource);
    bool uniform = cfrTimes.size() == allSourceFrames.size() && cfrRate > 0 && std::isfinite(cfrRate);
    for (int index = 1; uniform && index < cfrTimes.size(); ++index)
        uniform = std::abs(cfrTimes[index] - cfrTimes[index - 1] - 1.0 / cfrRate) < 1e-6;
    if (!check(uniform, "CFR audio-collapse control has independently verified exact nominal cadence")) return false;
    VDQtVideoDecoder cfrDecoder;
    VDQtAudioPlayer cfrAudio(false);
    if (!cfrDecoder.openFile(cfrSource) || !cfrAudio.openFile(cfrSource) || !cfrAudio.hasAudio()) return false;
    VDQtVideoExporter exporter;
    VDQtVideoExporter::ExportOptions request = job.options;
    request.inputPath = cfrSource;
    request.outputPath = fixtures.directory.filePath("cfr-edited-audio-collapsed.mkv");
    request.preserveEmptyFrames = false; request.unattended = true;
    request.processing = VDQtVideoExporter::ProcessingSnapshot{};
    request.processing->videoCodec = job.processing.videoCodec;
    request.processing->audioCodec = job.processing.audioCodec;
    if (!exporter.exportVideo(request, &cfrDecoder, &cfrAudio)) {
        std::cerr << "Ordinary CFR collapse: " << exporter.lastError().toStdString() << '\n'; return false;
    }
    QByteArray cfrPixels, cfrSamples;
    QList<double> cfrOutputTimes;
    for (int index = 0; index < pictureOrdinals.size(); ++index) {
        cfrPixels += allSourceFrames[pictureOrdinals[index]];
        cfrOutputTimes.append(index / cfrRate);
    }
    for (const auto& segment : edits) {
        const qint64 first = std::llround(cfrTimes[segment.sourceStartFrame] * rate);
        const qint64 end = std::llround(cfrTimes[segment.sourceStartFrame + segment.frameCount] * rate);
        cfrSamples += pcm.mid(first * 2, (end - first) * 2);
    }
    passed &= outputPixels(fixtures, request.outputPath, cfrPixels, cfrOutputTimes);
    const QString cfrDecodedAudio = request.outputPath + ".pcm";
    if (!fixtures.ffmpeg({"-i", request.outputPath, "-map", "0:a:0", "-f", "s16le",
        "-acodec", "pcm_s16le", cfrDecodedAudio})) return false;
    passed &= check(read(cfrDecodedAudio) == cfrSamples,
        "ordinary edited CFR collapse with audio retains all intended samples and duration");
    return passed;
}
}
int main(int argc, char** argv) {
    VDQtTestFixtures fixtures;
    if (!fixtures.directory.isValid()) return 2;
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", fixtures.directory.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", fixtures.directory.filePath("data").toUtf8());
    QApplication application(argc, argv);
    bool passed = true;
    if (argc <= 1) {
        // CTest runs the complete small matrix. An optional case argument is
        // useful for isolated baseline/fixed diagnostics without larger suites.
        const bool imports = importedSources(fixtures);
        const bool avi = embeddedPcm(fixtures);
        const bool joint = editedVfrJoint(fixtures);
        passed = imports && avi && joint;
    } else {
        const QString scenario = QString::fromLocal8Bit(argv[1]);
        passed = scenario == "imports" ? importedSources(fixtures)
            : scenario == "vfr_joint" ? editedVfrJoint(fixtures)
            : scenario == "avi_pcm" ? embeddedPcm(fixtures)
            : check(false, "unknown output matrix scenario");
    }
    if (!passed) std::cerr << fixtures.error.toStdString() << '\n';
    return passed ? 0 : 1;
}
