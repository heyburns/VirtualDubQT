// Generated fixtures only: edited soundtracks must maintain effect state across
// cuts and must not quantize a high-precision intermediate to integer16/24 PCM.
#include <QString>
#include "VirtualDub/VDQtAudioExport.h"
#include "VirtualDub/VDQtAudioPlayer.h"
#include "VirtualDub/VDQtVideoExporter.h"
#include "VirtualDub/VDQtWaveform.h"
#include <QApplication>
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <unistd.h>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool run(const QStringList& args, QByteArray *output = nullptr) {
    QProcess process;
    process.start("ffmpeg", QStringList{"-hide_banner", "-nostdin", "-v", "error", "-y"} + args);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        process.kill(); process.waitForFinished(3000); return false;
    }
    if (output) *output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode()) {
        std::cerr << process.readAllStandardError().constData(); return false;
    }
    return true;
}
QByteArray pcm(const QString& path, const QString& format = "s16le") {
    QByteArray output;
    const QString codec = "pcm_" + format;
    if (!run({"-i", path, "-map", "0:a:0", "-c:a", codec, "-f", format, "-"}, &output)) return {};
    return output;
}
bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
// WAV integer32 and float32/64 fixtures include values below integer24's LSB.
bool precisionWav(const QString& path, int bits, bool floating) {
    constexpr int frames = 12000;
    const int bytes = bits / 8;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4); stream << quint32(36 + frames * bytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(floating ? 3 : 1) << quint16(1)
        << quint32(48000) << quint32(48000 * bytes) << quint16(bytes) << quint16(bits);
    stream.writeRawData("data", 4); stream << quint32(frames * bytes);
    for (int index = 0; index < frames; ++index) {
        if (!floating) stream << qint32(index % 8191 - 4095);
        else if (bits == 32) {
            const float value = float((index % 29 - 14) * 0x1p-27);
            quint32 encoded = 0; std::memcpy(&encoded, &value, sizeof encoded); stream << encoded;
        } else {
            const double value = (index % 29 - 14) * 0x1p-42 + 0x1p-50;
            quint64 encoded = 0; std::memcpy(&encoded, &value, sizeof encoded); stream << encoded;
        }
    }
    return stream.status() == QDataStream::Ok;
}

bool precisionCases(QTemporaryDir& dir) {
    bool passed = true;
    auto gain = VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::Gain);
    gain.params["decibels"] = 1.25;
    auto resample = VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::Resample);
    resample.params["sampleRate"] = 44100;
    const QList<QPair<int64_t, int64_t>> ranges{{0, 4000}, {8000, 4000}};
    for (const auto& spec : QList<QPair<int, bool>>{{32, false}, {32, true}, {64, true}}) {
        const QString stem = QString("precision_%1_%2").arg(spec.first).arg(spec.second);
        const QString source = dir.filePath(stem + ".wav");
        passed &= check(precisionWav(source, spec.first, spec.second), "write precision fixture");
        VDQtAudioPlayer audio(false);
        if (!check(audio.openFile(source), "open precision fixture")) return false;
        const QString raw = dir.filePath(stem + "_raw.wav");
        const QString identity = dir.filePath(stem + "_identity.wav");
        QString error;
        passed &= check(VDQtPrepareAudioWav(audio, identity, {{0, 12000}}, {}, {}, &error)
            && pcm(identity, "f64le") == pcm(source, "f64le"), "offline source precision survives extraction");
        passed &= check(VDQtPrepareAudioWav(audio, raw, ranges, {}, {}, &error), "join raw precision cuts");
        for (const QList<VDAudioFilterInstance>& filters :
                QList<QList<VDAudioFilterInstance>>{{gain}, {gain, resample}}) {
            const QString result = dir.filePath(stem + QString("_filtered_%1.wav").arg(filters.size()));
            const QString reference = dir.filePath(stem + QString("_reference_%1.wav").arg(filters.size()));
            VDQtAudioFilterSystem system; system.replaceActiveChain(filters);
            const QString graph = system.ffmpegFilterGraph(audio.getSampleRate());
            passed &= check(graph.contains(":precision=double"), "Gain retains double processing precision");
            passed &= check(run({"-i", raw, "-af", graph, "-c:a", "pcm_f64le", reference})
                && VDQtPrepareAudioWav(audio, result, ranges, filters, {}, &error), "render precision effects once");
            const QByteArray actual = pcm(result, "f64le"), expected = pcm(reference, "f64le");
            passed &= check(!actual.isEmpty() && actual == expected,
                "gain/resample output matches joined high-precision reference exactly");
            VDQtWaveformData data;
            passed &= check(VDQtReadWaveformPeaks(result, 16, &data, &error)
                && data.floatingPoint && data.containerBits == 64
                && data.sampleRate == (filters.size() > 1 ? 44100 : 48000),
                "processed intermediate retains floating precision and actual resampled rate");
        }
        const QByteArray sourceBefore = readFile(source);
        const QList<VDAudioFilterInstance> noFilters;
        passed &= check(!audio.exportAudioRangesToFile(source, ranges, {}, &noFilters)
            && readFile(source) == sourceBefore, "range export rejects replacing its source");
        const QString symbolic = dir.filePath(stem + "_symlink.wav");
        const QString hard = dir.filePath(stem + "_hardlink.wav");
        passed &= check(QFile::link(source, symbolic)
            && ::link(QFile::encodeName(source).constData(), QFile::encodeName(hard).constData()) == 0
            && !audio.exportAudioRangesToFile(symbolic, ranges, {}, &noFilters)
            && !audio.exportAudioRangesToFile(hard, ranges, {}, &noFilters)
            && readFile(source) == sourceBefore, "range export rejects symlink and hardlink source aliases");
        const QString late = dir.filePath(stem + "_late.wav");
        bool redirected = false;
        passed &= check(!audio.exportAudioRangesToFile(late, ranges,
            [&](int current, int total) {
                if (current == total && !redirected) redirected = QFile::link(source, late);
                return true;
            }, &noFilters) && redirected && QFileInfo(late).isSymLink()
            && readFile(source) == sourceBefore, "precommit recheck rejects a late-created source alias");
    }
    return passed;
}

bool liveGainFormats(const QByteArray& sourcePcm) {
    bool passed = true;
    auto gain = VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::Gain);
    gain.params["decibels"] = -6;
    for (auto type : {QAudioFormat::Int16, QAudioFormat::Int32, QAudioFormat::Float}) {
        QBuffer input; input.setData(sourcePcm); input.open(QIODevice::ReadOnly);
        QAudioFormat format; format.setSampleRate(48000); format.setChannelCount(1); format.setSampleFormat(type);
        VDQtAudioFilterDevice filter(&input, 48000, 1, format);
        passed &= check(filter.setFilterChain({gain}), "live double Gain graph configures declared sink format");
        QByteArray result;
        for (int pulls = 0; !filter.atEnd() && pulls < 2000; ++pulls) {
            char bytes[4096]; const qint64 count = filter.read(bytes, sizeof bytes);
            if (count < 0) break;
            result.append(bytes, count);
        }
        passed &= check(filter.error().isEmpty() && filter.atEnd()
            && result.size() == sourcePcm.size() / 2 * format.bytesPerFrame(),
            "live Gain output bytes match Int16/Int32/Float sink format exactly");
    }
    return passed;
}
}

int main(int argc, char **argv) {
    QTemporaryDir dir;
    if (!dir.isValid()) return 2;
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", dir.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", dir.filePath("data").toUtf8());
    QApplication app(argc, argv);
    bool passed = precisionCases(dir);
    const QString source = dir.filePath("source.mkv");
    if (!run({"-f", "lavfi", "-i", "testsrc2=size=64x48:rate=10",
        "-f", "lavfi", "-i", "aevalsrc=if(lt(t\\,1)\\,0.125\\,if(lt(t\\,2)\\,0.25\\,0.375)):s=48000:d=3",
        "-frames:v", "30", "-t", "3", "-c:v", "ffv1", "-c:a", "pcm_s16le", source})) return 3;
    VDQtVideoDecoder decoder; VDQtAudioPlayer audio(false);
    if (!decoder.openFile(source) || decoder.ensureFrameIndex().totalFrames != 30 || !audio.openFile(source)) return 4;
    const QList<VDQtTimelineSegment> edits{{0, 10, false}, {20, 10, false}};
    auto chorus = VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::Chorus);
    chorus.params["delayMs"] = 100; chorus.params["depthMs"] = 10; chorus.params["mix"] = 0.6;
    VDQtAudioExportRequest request;
    request.outputPath = dir.filePath("reference.wav"); request.filters = {chorus};
    request.codec.codecId = "pcm_s16le"; request.codec.bitDepth = 16;
    request.codec.sampleRate = request.codec.channels = 0;
    QString error;
    if (!VDQtAudioRangesForTimeline(decoder, edits, 0, 19, 48000, &request.sampleRanges, &error)
        || !VDQtExportAudio(audio, request, {}, &error)) return 5;
    const QByteArray expected = pcm(request.outputPath);
    passed &= liveGainFormats(pcm(source));
    const QString composed = dir.filePath("composed.wav");
    passed &= check(VDQtPrepareAudioWav(audio, composed, request.sampleRanges, {chorus}, {}, &error)
        && pcm(composed) == expected, "direct range preparation renders Chorus once including its sole tail");
    for (int mode : {VideoMode_FullProcessing, VideoMode_NormalRecompress, VideoMode_FastRecompress}) {
        VDQtVideoExporter::ExportOptions options;
        options.inputPath = source; options.outputPath = dir.filePath(QString("video_%1.mkv").arg(mode));
        options.containerType = "mkv"; options.videoMode = mode; options.audioMode = AudioMode_FullProcessing;
        options.unattended = true; options.timelineExplicit = true; options.timelineSegments = edits;
        options.startFrame = 0; options.endFrame = 19;
        options.processing = VDQtVideoExporter::ProcessingSnapshot{};
        options.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
        options.processing->audioCodec = request.codec; options.processing->audioFilters = request.filters;
        VDQtVideoExporter exporter;
        const bool exported = exporter.exportVideo(options, &decoder, &audio);
        if (!exported) std::cerr << exporter.lastError().toStdString() << '\n';
        const QByteArray actual = exported ? pcm(options.outputPath) : QByteArray{};
        passed &= check(exported && actual.size() == 2 * 48000 * 2
            && actual == expected.left(actual.size()),
            "edited video Chorus matches joined-once reference through final video duration");

        options.outputPath = dir.filePath(QString("cancel_%1.mkv").arg(mode));
        const QByteArray marker("PREEXISTING_OUTPUT");
        passed &= check(writeFile(options.outputPath, marker), "create cancellation target");
        int frameCallbacks = 0, audioHeartbeats = 0;
        const bool cancelled = !exporter.exportVideo(options, &decoder, &audio, nullptr,
            [&](int, const QImage&, const QImage&) { ++frameCallbacks; },
            [&](int current, int total) {
                if (total == 1000 && current == 100) { ++audioHeartbeats; return false; }
                return true;
            });
        passed &= check(cancelled && exporter.wasCancelled() && audioHeartbeats == 1
            && frameCallbacks == 0 && readFile(options.outputPath) == marker,
            "outer abort works during audio preparation without modifying output");
    }
    // Retiming without edits deliberately requests the output duration from the
    // source, rather than silently changing the established FPS contract. Gain
    // and resample preserve tiny float values through all three video paths.
    for (bool floating : {false, true}) {
        const QString preciseStem = floating ? "nonedited-float" : "nonedited-int32";
        const QString preciseSource = dir.filePath(preciseStem + ".wav");
        if (!precisionWav(preciseSource, 32, floating)) return 6;
        VDQtAudioPlayer preciseAudio(false);
        if (!preciseAudio.openFile(preciseSource)) return 7;
        auto gain = VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::Gain);
        gain.params["decibels"] = 1.25;
        auto resample = VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::Resample);
        resample.params["sampleRate"] = 44100;
        VDQtAudioExportRequest preciseRequest;
        preciseRequest.outputPath = dir.filePath(preciseStem + "-reference.wav");
        preciseRequest.sampleRanges = {{0, 12000}}; preciseRequest.filters = {gain, resample};
        preciseRequest.codec.codecId = floating ? "pcm_f64le" : "pcm_s32le"; preciseRequest.codec.bitDepth = 32;
        preciseRequest.codec.sampleRate = preciseRequest.codec.channels = 0;
        if (!VDQtExportAudio(preciseAudio, preciseRequest, {}, &error)) return 8;
        const QString preciseFormat = floating ? "f64le" : "s32le";
        const QByteArray preciseExpected = pcm(preciseRequest.outputPath, preciseFormat);
        for (int mode : {VideoMode_FullProcessing, VideoMode_NormalRecompress, VideoMode_FastRecompress}) {
            VDQtVideoExporter::ExportOptions options;
            options.inputPath = source; options.outputPath = dir.filePath(preciseStem + QString("_retimed_%1.mkv").arg(mode));
            options.containerType = "mkv"; options.videoMode = mode; options.audioMode = AudioMode_FullProcessing;
            options.unattended = true; options.startFrame = 0; options.endFrame = 4; options.customFps = 20;
            options.processing = VDQtVideoExporter::ProcessingSnapshot{};
            options.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
            options.processing->audioCodec = preciseRequest.codec; options.processing->audioFilters = preciseRequest.filters;
            VDQtVideoExporter exporter;
            const bool exported = exporter.exportVideo(options, &decoder, &preciseAudio);
            const QByteArray actual = exported ? pcm(options.outputPath, preciseFormat) : QByteArray{};
            passed &= check(exported && actual.size() == 11025 * (floating ? 8 : 4) && actual == preciseExpected,
                "nonedited Full/Normal/Fast gain/resample preserve precision and FPS output-duration contract");
            options.outputPath = dir.filePath(preciseStem + QString("_retimed-cancel_%1.mkv").arg(mode));
            const QByteArray retained("UNMODIFIED_RETIMED_OUTPUT"); writeFile(options.outputPath, retained);
            int calls = 0;
            passed &= check(!exporter.exportVideo(options, &decoder, &preciseAudio, nullptr, {},
                [&](int current, int total) {
                    if (current == 100 && total == 1000) { ++calls; return false; }
                    return true;
                }) && exporter.wasCancelled() && calls == 1 && readFile(options.outputPath) == retained,
                "nonedited Fast and render-path audio phases obey outer cancellation");
        }
    }
    const QString signed64 = dir.filePath("unsupported-signed64.nut");
    if (!run({"-f", "lavfi", "-i", "anullsrc=r=48000:cl=mono", "-t", "0.1",
        "-c:a", "pcm_s64le", signed64})) return 9;
    VDQtAudioPlayer signed64Audio(false);
    const QString rejected = dir.filePath("signed64-output.wav");
    const QByteArray retained64("KEEP_SIGNED64_DESTINATION"); writeFile(rejected, retained64);
    passed &= check(signed64Audio.openFile(signed64)
        && !VDQtPrepareAudioWav(signed64Audio, rejected, {{0, 4800}}, {}, {}, &error)
        && !error.isEmpty() && readFile(rejected) == retained64,
        "unsupported signed64 extraction fails instead of silently narrowing or replacing output");
    const QString invalid = dir.filePath("invalid.wav");
    const QByteArray marker("ORIGINAL_TARGET"); writeFile(invalid, marker);
    passed &= check(!VDQtPrepareAudioWav(audio, invalid,
        {{std::numeric_limits<qint64>::max() - 10, 20}, {0, 10}}, {chorus}, {}, &error)
        && !error.isEmpty() && readFile(invalid) == marker,
        "invalid edited range fails with error before replacing existing output");
    int callbacks = 0;
    passed &= check(!VDQtPrepareAudioWav(audio, invalid, request.sampleRanges, {chorus},
        [&](int, int) { ++callbacks; return false; }, &error)
        && callbacks == 1 && readFile(invalid) == marker, "audio preparation cancellation preserves output");
    return passed ? 0 : 1;
}
