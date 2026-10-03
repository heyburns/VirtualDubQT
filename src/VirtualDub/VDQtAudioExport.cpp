// Shared manual/script/job audio save pipeline. Extract source-precision PCM,
// concatenate edits before effects (continuous histories/tails), encode once,
// then commit. Callers retain dependency/overwrite approval at their boundary.
#include "VDQtAudioExport.h"
#include "VDQtAudioPlayer.h"
#include "VDQtVideoDecoder.h"
#include "VDQtSourceSafety.h"
#include "VDQtOutputTransaction.h"
#include "VDQtTimingMath.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
bool fail(QString *error, const QString& message) {
    if (error) *error = message;
    return false;
}
bool sampleBoundary(double seconds, int rate, qint64 *result) {
    return VDQtCheckedRoundedNonnegative(static_cast<long double>(seconds) * rate, result);
}
}

bool VDQtAudioRangesForTimeline(
    VDQtVideoDecoder& decoder, const QList<VDQtTimelineSegment>& edits,
    qint64 firstFrame, qint64 lastFrame, int sampleRate,
    QList<QPair<int64_t, int64_t>> *ranges, QString *errorMessage,
    const std::function<bool(int, int)>& progress) {
    if (!ranges || !decoder.isOpen() || sampleRate <= 0 || firstFrame < 0 || lastFrame < -1)
        return fail(errorMessage, QStringLiteral("Invalid audio timeline range."));
    ranges->clear();
    if (edits.isEmpty() && firstFrame == 0 && lastFrame == -1) {
        // A full soundtrack may be longer than its video; do not truncate it.
        ranges->append({0, -1});
        return true;
    }
    const auto scan = decoder.ensureFrameIndex(progress);
    if (scan.cancelled || !scan.errorMessage.isEmpty() || scan.totalFrames <= 0)
        return fail(errorMessage, scan.errorMessage.isEmpty()
            ? QStringLiteral("Audio timeline indexing failed or was cancelled.") : scan.errorMessage);
    VDQtTimeline timeline;
    timeline.reset(scan.totalFrames, true);
    if (!edits.isEmpty() && !timeline.replaceSegments(edits, errorMessage)) return false;
    const qint64 end = lastFrame == -1 ? timeline.frameCount()
        : lastFrame < std::numeric_limits<qint64>::max()
            ? std::min(lastFrame + 1, timeline.frameCount()) : timeline.frameCount();
    const auto selected = timeline.copyRange(firstFrame, end, errorMessage);
    if (selected.isEmpty()) return false;
    const double fps = decoder.getFps() > 0 ? decoder.getFps() : 29.97;
    for (const auto& segment : selected) {
        const int first = static_cast<int>(segment.sourceStartFrame);
        const int last = static_cast<int>(segment.sourceStartFrame + segment.frameCount - 1);
        double startSeconds = decoder.getFrameTimestampSeconds(first);
        double endSeconds = decoder.getFrameTimestampSeconds(last);
        double duration = decoder.getFrameDurationSeconds(last);
        if (!std::isfinite(startSeconds)) startSeconds = first / fps;
        if (!std::isfinite(endSeconds)) endSeconds = last / fps;
        if (!std::isfinite(duration) || duration <= 0) duration = 1.0 / fps;
        qint64 startSample = 0, endSample = 0;
        if (!sampleBoundary(startSeconds, sampleRate, &startSample)
            || !sampleBoundary(endSeconds + duration, sampleRate, &endSample)
            || endSample <= startSample)
            return fail(errorMessage, QStringLiteral("Audio sample boundaries are invalid or too large."));
        // Mask flags deliberately do not hold sound at the preceding picture.
        ranges->append({startSample, endSample - startSample});
    }
    return true;
}

bool VDQtPrepareAudioWav(VDQtAudioPlayer& player, const QString& outputPath,
    const QList<QPair<int64_t, int64_t>>& inputRanges,
    const QList<VDAudioFilterInstance>& inputFilters,
    const std::function<bool(int, int)>& progress, QString *errorMessage,
    bool padToRequestedLength) {
    const auto ranges = inputRanges;
    const auto filters = inputFilters;
    if (errorMessage) errorMessage->clear();
    if (!player.hasAudio() || outputPath.isEmpty() || ranges.isEmpty())
        return fail(errorMessage, QStringLiteral("No audio or source ranges are available to prepare."));
    if (!VDQtValidateAudioFilters(filters, errorMessage)) return false;
    qint64 total = 0;
    for (const auto& range : ranges) {
        if (range.first < 0 || (range.second <= 0 && !(range.second == -1 && ranges.size() == 1))
            || (range.second > 0 && (range.first > std::numeric_limits<qint64>::max() - range.second
                || total > std::numeric_limits<qint64>::max() - range.second)))
            return fail(errorMessage, QStringLiteral("Invalid or excessive audio sample range."));
        if (range.second > 0) total += range.second;
    }
    const bool prepared = ranges.size() == 1
        ? player.exportAudioToFile(outputPath, ranges.first().first, ranges.first().second,
                                  progress, &filters, padToRequestedLength)
        : player.exportAudioRangesToFile(outputPath, ranges, progress, &filters, padToRequestedLength);
    return prepared || fail(errorMessage,
        QStringLiteral("The requested audio ranges or effect chain could not be prepared."));
}

bool VDQtExportAudio(VDQtAudioPlayer& player, const VDQtAudioExportRequest& input,
                    const std::function<bool(int, int)>& progress, QString *errorMessage) {
    const VDQtAudioExportRequest request = input; // Before any event-pumping callback.
    if (!player.hasAudio() || request.outputPath.isEmpty() || request.sampleRanges.isEmpty())
        return fail(errorMessage, QStringLiteral("No audio or source ranges are available to export."));
    VDAudioCodecParams codec = request.codec;
    codec.codecId = codec.codecId.trimmed().toLower();
    if (codec.codecId.isEmpty() || codec.sampleRate < 0 || codec.sampleRate > 192000
        || (codec.sampleRate > 0 && codec.sampleRate < 8000)
        || codec.channels < 0 || codec.channels > 8
        || codec.bitrateKbps < 0 || codec.bitrateKbps > 1000000
        || (codec.rateMode != "cbr" && codec.rateMode != "vbr")
        || (codec.bitDepth != 8 && codec.bitDepth != 16 && codec.bitDepth != 24 && codec.bitDepth != 32))
        return fail(errorMessage, QStringLiteral("Invalid audio codec or conversion settings."));
    if (!VDQtValidateAudioFilters(request.filters, errorMessage)) return false;
    qint64 samples = 0;
    for (const auto& range : request.sampleRanges) {
        if (range.first < 0 || (range.second <= 0 && !(range.second == -1 && request.sampleRanges.size() == 1))
            || (range.second > 0 && range.first > std::numeric_limits<qint64>::max() - range.second)
            || (range.second > 0 && samples > std::numeric_limits<qint64>::max() - range.second))
            return fail(errorMessage, QStringLiteral("Invalid or excessive audio sample range."));
        samples += range.second > 0 ? range.second : std::max<qint64>(0, player.getTotalSamples() - range.first);
    }
    auto safety = VDQtSourceSafety::captureSources({player.getSourcePath()});
    if (!safety.evaluateOutputPath(request.outputPath).isSafe())
        return fail(errorMessage, QStringLiteral("The audio destination aliases its source."));

    QTemporaryDir directory;
    QTemporaryDir staging(QFileInfo(request.outputPath).dir().filePath(
        QStringLiteral(".virtualdub-audio-XXXXXX")));
    if (!directory.isValid() || !staging.isValid())
        return fail(errorMessage, QStringLiteral("Audio staging storage could not be created."));
    VDQtOutputTransaction transaction(staging, {request.outputPath});
    if (!transaction.inspect(errorMessage)) return false;
    if (!request.replaceExisting && !transaction.existingTargets().isEmpty())
        return fail(errorMessage, QStringLiteral("Audio destination replacement was not approved."));
    const QString stagedPath = staging.filePath(QFileInfo(request.outputPath).fileName());
    const QString wav = directory.filePath(QStringLiteral("source.wav"));
    const QList<VDAudioFilterInstance> noFilters;
    const auto extractionProgress = [&](int value, int maximum) {
        return !progress || progress(maximum > 0 ? static_cast<int>(
            std::clamp(70.0 * value / maximum, 0.0, 70.0)) : 0, 100);
    };
    if (!VDQtPrepareAudioWav(player, wav, request.sampleRanges, noFilters, extractionProgress,
                            errorMessage, request.padToRequestedLength)) return false;
    VDQtAudioFilterSystem filters;
    filters.replaceActiveChain(request.filters);
    const QString graph = filters.ffmpegFilterGraph(player.getSampleRate());
    // Compression-with-hint and SetConversion carry precision separately from
    // the PCM codec tag. Dialog requests arrive with the corresponding bit depth.
    if (codec.codecId == "pcm_s16le" || codec.codecId == "pcm_s24le" || codec.codecId == "pcm_s32le"
        || codec.codecId == "(uncompressed)" || codec.codecId == "uncompressed")
        codec.codecId = codec.bitDepth == 8 ? QStringLiteral("pcm_u8")
            : QString("pcm_s%1le").arg(codec.bitDepth);
    QStringList args{"-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", wav};
    if (!graph.isEmpty()) args << "-af" << graph;
    args << VDQtCodecEngine::buildFfmpegAudioEncodeArguments(codec);
    args << "-progress" << "pipe:1" << stagedPath;

    // Preserve the existing standalone LAME path for hosts without FFmpeg's
    // libmp3lame encoder. Effects are rendered before handing PCM to LAME.
    QString program = QStringLiteral("ffmpeg");
    const QString lame = QStandardPaths::findExecutable(QStringLiteral("lame"));
    const bool useLame = (codec.codecId == "mp3" || codec.codecId == "libmp3lame")
        && !avcodec_find_encoder_by_name("libmp3lame") && !lame.isEmpty();
    if (useLame) {
        QString lameInput = wav;
        if (!graph.isEmpty()) {
            lameInput = directory.filePath(QStringLiteral("filtered.wav"));
            QProcess effects;
            effects.start("ffmpeg", {"-nostdin", "-v", "error", "-y", "-i", wav,
                "-af", graph, "-c:a", "pcm_s16le", lameInput});
            if (!effects.waitForStarted(5000))
                return fail(errorMessage, QStringLiteral("Audio filter renderer could not start."));
            while (effects.state() != QProcess::NotRunning && !effects.waitForFinished(50)) {
                effects.readAllStandardError();
                if (progress && !progress(75, 100)) {
                    effects.kill(); effects.waitForFinished(3000);
                    return fail(errorMessage, QStringLiteral("Audio export was cancelled."));
                }
            }
            if (effects.exitStatus() != QProcess::NormalExit || effects.exitCode() != 0)
                return fail(errorMessage, QStringLiteral("Audio filter rendering failed."));
        }
        program = lame;
        args = {"--silent"};
        if (codec.rateMode.compare("vbr", Qt::CaseInsensitive) == 0)
            args << "-V" << QString::number(std::clamp(codec.vbrQuality, 0, 9));
        else args << "-b" << QString::number(codec.bitrateKbps > 0 ? codec.bitrateKbps : 192);
        if (codec.sampleRate > 0) args << "--resample" << QString::number(codec.sampleRate / 1000.0, 'f', 3);
        if (codec.channels == 1) args << "-m" << "m";
        else if (codec.channels == 2) args << "-m" << "j";
        else if (codec.channels > 2)
            return fail(errorMessage, QStringLiteral("MP3 supports at most two channels."));
        args << lameInput << stagedPath;
    }
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(5000))
        return fail(errorMessage, QStringLiteral("The audio encoder could not start."));
    QByteArray diagnostic, output;
    bool cancelled = false;
    while (process.state() != QProcess::NotRunning && !process.waitForFinished(50)) {
        diagnostic += process.readAllStandardError();
        diagnostic = diagnostic.right(64 * 1024);
        output += process.readAllStandardOutput();
        int percent = 75;
        for (;;) {
            const auto newline = output.indexOf('\n');
            if (newline < 0) break;
            const QByteArray line = output.left(newline).trimmed();
            output.remove(0, newline + 1);
            if (line.startsWith("out_time_us=") && samples > 0 && player.getSampleRate() > 0) {
                const long double fraction = static_cast<long double>(line.mid(12).toLongLong())
                    * player.getSampleRate() / (1000000.0L * samples);
                percent = 70 + static_cast<int>(std::clamp(fraction * 29, 0.0L, 29.0L));
            }
        }
        if (output.size() > 64 * 1024) output = output.right(64 * 1024);
        if (progress && !progress(percent, 100)) {
            cancelled = true;
            process.terminate();
            if (!process.waitForFinished(1000)) { process.kill(); process.waitForFinished(3000); }
            break;
        }
    }
    diagnostic += process.readAllStandardError();
    if (cancelled || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
        || QFileInfo(stagedPath).size() <= 0)
        return fail(errorMessage, cancelled ? QStringLiteral("Audio export was cancelled.")
            : QStringLiteral("Audio encoding failed: ") + QString::fromLocal8Bit(diagnostic.right(4096)));
    if (progress && !progress(100, 100))
        return fail(errorMessage, QStringLiteral("Audio export was cancelled."));
    safety.refresh();
    if (!safety.evaluateOutputPath(request.outputPath).isSafe())
        return fail(errorMessage, QStringLiteral("The audio destination became unsafe while encoding."));
    return transaction.commit({stagedPath}, request.replaceExisting, errorMessage);
}
