// Generated-source regressions for effective audio-mode/source consistency.
// Conflicting saved Direct + selected-source states follow the editor's existing
// Full policy; ordinary Direct copies must ignore application audio effects.
#include "VirtualDub/VDQtAudioExport.h"
#include "VirtualDub/VDQtAudioPlayer.h"
#include "VirtualDub/VDQtVideoExporter.h"
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace {
bool check(bool ok, const QString& name) {
    if (!ok) std::cerr << "FAIL: " << name.toStdString() << '\n';
    return ok;
}
bool run(const QString& program, const QStringList& args, QByteArray *output = nullptr) {
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        process.kill(); process.waitForFinished(3000); return false;
    }
    if (output) *output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode()) {
        std::cerr << process.readAllStandardError().constData(); return false;
    }
    return true;
}
bool ffmpeg(const QStringList& args, QByteArray *output = nullptr) {
    return run("ffmpeg", QStringList{"-nostdin", "-hide_banner", "-v", "error", "-y"} + args, output);
}
QByteArray pcm(const QString& path) {
    QByteArray data;
    return ffmpeg({"-i", path, "-map", "0:a:0", "-c:a", "pcm_s16le", "-f", "s16le", "-"}, &data)
        ? data : QByteArray{};
}
QByteArray read(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool streams(const QString& path, int videos, int audios) {
    QByteArray json;
    if (!run("ffprobe", {"-v", "error", "-show_entries", "stream=codec_type", "-of", "json", path}, &json)) return false;
    int actualVideos = 0, actualAudios = 0;
    for (const QJsonValue& entry : QJsonDocument::fromJson(json).object().value("streams").toArray()) {
        const QString type = entry.toObject().value("codec_type").toString();
        actualVideos += type == "video";
        actualAudios += type == "audio";
    }
    return actualVideos == videos && actualAudios == audios;
}
bool closePcm(const QByteArray& actual, const QByteArray& expected, int tolerance = 0) {
    if (actual.isEmpty() || actual.size() != expected.size() || actual.size() % 2) return false;
    for (qsizetype offset = 0; offset < actual.size(); offset += 2) {
        qint16 a = 0, b = 0;
        std::memcpy(&a, actual.constData() + offset, 2);
        std::memcpy(&b, expected.constData() + offset, 2);
        if (std::abs(int(a) - int(b)) > tolerance) return false;
    }
    return true;
}
VDQtVideoExporter::ExportOptions optionsFor(QTemporaryDir& dir, const QString& source,
    const QString& name, int mode, const QList<VDAudioFilterInstance>& filters) {
    VDQtVideoExporter::ExportOptions options;
    options.inputPath = source;
    options.outputPath = dir.filePath(name + QString("-%1.mkv").arg(mode));
    options.containerType = "mkv";
    options.videoMode = mode;
    options.audioMode = AudioMode_DirectStreamCopy;
    options.endFrame = 19;
    options.unattended = true;
    options.processing = VDQtVideoExporter::ProcessingSnapshot{};
    options.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
    options.processing->audioCodec.codecId = "pcm_s16le";
    options.processing->audioCodec.bitDepth = 16;
    options.processing->audioFilters = filters;
    return options;
}
QByteArray referencePcm(QTemporaryDir& dir, VDQtAudioPlayer& audio,
    const QString& name, const QList<VDAudioFilterInstance>& filters,
    const QList<QPair<int64_t, int64_t>>& ranges = {{0, 96000}}) {
    VDQtAudioExportRequest request;
    request.outputPath = dir.filePath(name + "-reference.wav");
    request.sampleRanges = ranges;
    request.filters = filters;
    request.codec.codecId = "pcm_s16le";
    request.codec.bitDepth = 16;
    QString error;
    if (!VDQtExportAudio(audio, request, {}, &error)) {
        std::cerr << error.toStdString() << '\n'; return {};
    }
    return pcm(request.outputPath);
}
bool selectedCases(QTemporaryDir& dir, const QString& video, const QString& audioPath,
    int streamIndex, const QString& name, const QList<VDAudioFilterInstance>& filters) {
    VDQtVideoDecoder decoder;
    VDQtAudioPlayer audio(false);
    if (!decoder.openFile(video) || decoder.ensureFrameIndex().totalFrames != 20
        || !audio.openFile(audioPath, streamIndex) || !audio.hasAudio()) return false;
    const QByteArray sourceVideo = read(video), sourceAudio = read(audioPath);
    const QByteArray expected = referencePcm(dir, audio, name, filters);
    if (expected.isEmpty()) return false;
    bool passed = true;
    for (int mode : {VideoMode_DirectStreamCopy, VideoMode_FastRecompress,
            VideoMode_NormalRecompress, VideoMode_FullProcessing}) {
        auto options = optionsFor(dir, video, name, mode, filters);
        VDQtVideoExporter exporter;
        const bool exported = exporter.exportVideo(options, &decoder, &audio);
        if (!exported) std::cerr << name.toStdString() << " mode" << mode << ": "
            << exporter.lastError().toStdString() << '\n';
        VDQtVideoDecoder result;
        const bool valid = exported && streams(options.outputPath, 1, 1)
            && result.openFile(options.outputPath) && result.ensureFrameIndex().totalFrames == 20
            && closePcm(pcm(options.outputPath), expected);
        passed &= check(valid, name + QString(" mode%1 honors selected soundtrack using existing Full policy").arg(mode));
    }
    return check(read(video) == sourceVideo && read(audioPath) == sourceAudio,
        name + " preserves both original sources") && passed;
}
bool defaultDirect(QTemporaryDir& dir, const QString& source,
    const QList<VDAudioFilterInstance>& filters) {
    VDQtVideoDecoder decoder;
    VDQtAudioPlayer audio(false);
    if (!decoder.openFile(source) || !audio.openFile(source)) return false;
    const QByteArray expected = referencePcm(dir, audio, "default-direct", {});
    bool passed = true;
    for (int mode : {VideoMode_DirectStreamCopy, VideoMode_FastRecompress,
            VideoMode_NormalRecompress, VideoMode_FullProcessing}) {
        auto options = optionsFor(dir, source, "default-direct", mode, filters);
        VDQtVideoExporter exporter;
        const bool exported = exporter.exportVideo(options, &decoder, &audio);
        if (!exported) std::cerr << exporter.lastError().toStdString() << '\n';
        passed &= check(exported && streams(options.outputPath, 1, 1)
            && closePcm(pcm(options.outputPath), expected),
            QString("default embedded direct mode%1 retains untouched PCM despite configured Gain").arg(mode));
    }
    return passed;
}
bool smartAndUnsafe(QTemporaryDir& dir, const QString& source, const QString& external,
    const QList<VDAudioFilterInstance>& filters) {
    VDQtVideoDecoder decoder;
    VDQtAudioPlayer audio(false);
    if (!decoder.openFile(source) || !audio.openFile(external, 0)) return false;
    bool passed = true;
    auto selected = optionsFor(dir, source, "external-selection", VideoMode_DirectStreamCopy, filters);
    selected.startFrame = 3; selected.endFrame = 7;
    const QByteArray marker("PREVIOUS_DESTINATION");
    VDQtVideoExporter exporter;
    passed &= check(write(selected.outputPath, marker)
        && !exporter.exportVideo(selected, &decoder, &audio)
        && !exporter.wasCancelled() && !exporter.lastError().isEmpty()
        && read(selected.outputPath) == marker,
        "external direct-video selection still rejects keyframe/preroll mismatch without replacement");
    auto smart = optionsFor(dir, source, "external-smart-edits", VideoMode_DirectStreamCopy, filters);
    smart.smartRendering = true;
    smart.timelineExplicit = true;
    smart.timelineSegments = {{10, 5, false}, {0, 5, false}};
    smart.endFrame = 9;
    const QByteArray expected = referencePcm(dir, audio, "external-smart-edits", filters,
        {{48000, 24000}, {0, 24000}});
    int renderedFrames = 0;
    const bool exported = exporter.exportVideo(smart, &decoder, &audio, nullptr,
        [&](int, const QImage&, const QImage&) { ++renderedFrames; });
    if (!exported) std::cerr << exporter.lastError().toStdString() << '\n';
    VDQtVideoDecoder result;
    passed &= check(exported && renderedFrames == 10 && streams(smart.outputPath, 1, 1)
        && result.openFile(smart.outputPath) && result.ensureFrameIndex().totalFrames == 10
        && closePcm(pcm(smart.outputPath), expected),
        "external smart edited audio follows exact render fallback rather than native packet manifest");
    const QByteArray original = read(external);
    auto alias = optionsFor(dir, source, "external-alias", VideoMode_FullProcessing, filters);
    const QString symbolic = dir.filePath("external-symbolic.wav");
    const QString hard = dir.filePath("external-hard.wav");
    passed &= check(QFile::link(external, symbolic)
        && ::link(QFile::encodeName(external).constData(), QFile::encodeName(hard).constData()) == 0,
        "create selected source aliases");
    for (const QString& destination : {external, symbolic, hard}) {
        alias.outputPath = destination;
        passed &= check(!exporter.exportVideo(alias, &decoder, &audio)
            && read(external) == original, "selected audio source aliases remain protected");
    }
    auto cancelled = optionsFor(dir, source, "external-cancelled", VideoMode_FullProcessing, filters);
    int audioHeartbeats = 0, videoCallbacks = 0;
    passed &= check(write(cancelled.outputPath, marker)
        && !exporter.exportVideo(cancelled, &decoder, &audio, nullptr,
            [&](int, const QImage&, const QImage&) { ++videoCallbacks; },
            [&](int current, int total) {
                if (current == 100 && total == 1000) { ++audioHeartbeats; return false; }
                return true;
            })
        && exporter.wasCancelled() && audioHeartbeats == 1 && videoCallbacks == 0
        && read(cancelled.outputPath) == marker && read(external) == original,
        "normalized selected audio retains cancellable preparation and atomic replacement");
    auto twoPass = optionsFor(dir, source, "external-two-pass", VideoMode_FullProcessing, filters);
    twoPass.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("libx264");
    twoPass.processing->videoCodec.twoPass = true;
    twoPass.processing->videoCodec.rateMode = "bitrate";
    twoPass.processing->videoCodec.targetBitrateKbps = 300;
    twoPass.processing->videoCodec.preset = "ultrafast";
    twoPass.processing->videoCodec.profile = "main";
    const QByteArray fullExpected = referencePcm(dir, audio, "external-two-pass", filters);
    const bool encodedTwoPass = exporter.exportVideo(twoPass, &decoder, &audio);
    if (!encodedTwoPass) std::cerr << exporter.lastError().toStdString() << '\n';
    VDQtVideoDecoder encoded;
    passed &= check(encodedTwoPass && streams(twoPass.outputPath, 1, 1)
        && encoded.openFile(twoPass.outputPath) && encoded.ensureFrameIndex().totalFrames == 20
        && closePcm(pcm(twoPass.outputPath), fullExpected),
        "two-pass second pass uses selected processed audio while analysis remains video-only");
    return passed;
}
bool nativeDirect(QTemporaryDir& dir, const QList<VDAudioFilterInstance>& filters) {
    const QString script = dir.filePath("native-direct.avs");
    if (!write(script, "ClearAutoloadDirs()\n"
        "v=BlankClip(width=64,height=48,length=20,fps=10,pixel_type=\"YUV420P8\")\n"
        "a=Tone(length=2.0,frequency=440,samplerate=48000,channels=1,level=0.25).ConvertAudioTo16bit()\n"
        "AudioDub(v,a)\n")) return false;
    VDQtVideoDecoder decoder;
    VDQtAudioPlayer audio(false);
    if (!decoder.openFile(script) || !audio.openAvsClip(decoder.getAvsClip(), decoder.getAvsVi(),
        decoder.getAvsAccessMutex())) return false;
    const QByteArray expected = referencePcm(dir, audio, "native-direct", {});
    const QByteArray processed = referencePcm(dir, audio, "native-full", filters);
    bool passed = true;
    for (int mode : {VideoMode_FastRecompress, VideoMode_NormalRecompress, VideoMode_FullProcessing}) {
        for (bool full : {false, true}) {
            auto options = optionsFor(dir, script, full ? "native-full" : "native-direct", mode, filters);
            if (full) options.audioMode = AudioMode_FullProcessing;
            VDQtVideoExporter exporter;
            const bool exported = exporter.exportVideo(options, &decoder, &audio);
            if (!exported) std::cerr << exporter.lastError().toStdString() << '\n';
            // Tone phases requested in distinct AVS GetAudio blocks may round
            // at slightly different floating precision; two integer LSBs are
            // harmless here while the configured -6dB Gain is plainly distinct.
            passed &= check(exported && streams(options.outputPath, 1, 1)
                && closePcm(pcm(options.outputPath), full ? processed : expected, 2),
                QString("native mode%1 audio%2 obeys script graph vs Full UI effects").arg(mode).arg(full));
        }
        auto implicit = optionsFor(dir, script, "native-direct-implicit", mode, filters);
        VDQtVideoExporter exporter;
        const bool exported = exporter.exportVideo(implicit, &decoder);
        if (!exported) std::cerr << exporter.lastError().toStdString() << '\n';
        passed &= check(exported && streams(implicit.outputPath, 1, 1)
            && closePcm(pcm(implicit.outputPath), expected, 2),
            QString("native Direct mode%1 with no player borrows clip audio instead of dropping it").arg(mode));
        auto unavailable = optionsFor(dir, script, "native-direct-unavailable", mode, filters);
        const QByteArray marker("PREVIOUS_DESTINATION");
        VDQtAudioPlayer closed(false);
        // First use a new destination: a preexisting AVS target may be rejected
        // conservatively for unresolved dependencies before reaching this
        // availability guard, which must not masquerade as proof of the guard.
        passed &= check(!exporter.exportVideo(unavailable, &decoder, &closed)
            && !exporter.wasCancelled() && exporter.lastError().contains("selected audio source")
            && !QFileInfo::exists(unavailable.outputPath),
            QString("native Direct mode%1 does not replace an unusable explicitly supplied player").arg(mode));
        unavailable.outputPath = dir.filePath(QString("native-direct-old-%1.mkv").arg(mode));
        passed &= check(write(unavailable.outputPath, marker)
            && !exporter.exportVideo(unavailable, &decoder, &closed)
            && !exporter.wasCancelled() && !exporter.lastError().isEmpty()
            && read(unavailable.outputPath) == marker,
            QString("native Direct mode%1 with unusable provided player diagnoses without replacing target").arg(mode));
    }
    return passed;
}
bool videoOnlyOffset(QTemporaryDir& dir) {
    const QString source = dir.filePath("video-after-audio.mkv");
    if (!ffmpeg({"-itsoffset", "0.5", "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=10:duration=2",
        "-f", "lavfi", "-i", "aevalsrc=0.125:s=48000:d=2.5", "-map", "0:v:0", "-map", "1:a:0",
        "-threads", "1", "-c:v", "ffv1", "-g", "1", "-fps_mode:v", "passthrough",
        "-c:a", "pcm_s16le", source})) return false;
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(source) || decoder.ensureFrameIndex().totalFrames != 20) return false;
    bool passed = true;
    for (int mode : {VideoMode_DirectStreamCopy, VideoMode_FastRecompress}) {
        const QImage expectedFirst = decoder.getFrameImage(3).convertToFormat(QImage::Format_RGBA8888);
        const QImage expectedLast = decoder.getFrameImage(7).convertToFormat(QImage::Format_RGBA8888);
        auto options = optionsFor(dir, source, "video-only-offset", mode, {});
        options.startFrame = 3; options.endFrame = 7;
        options.includeAudio = false;
        VDQtVideoExporter exporter;
        const bool exported = exporter.exportVideo(options, &decoder);
        if (!exported) std::cerr << exporter.lastError().toStdString() << '\n';
        VDQtVideoDecoder encoded;
        passed &= check(exported && streams(options.outputPath, 1, 0)
            && encoded.openFile(options.outputPath) && encoded.ensureFrameIndex().totalFrames == 5
            && encoded.getFrameImage(0).convertToFormat(QImage::Format_RGBA8888) == expectedFirst
            && encoded.getFrameImage(4).convertToFormat(QImage::Format_RGBA8888) == expectedLast,
            QString("video-only mode%1 still seeks relative to first video rather than leading audio").arg(mode));
    }
    auto smart = optionsFor(dir, source, "video-only-offset-smart", VideoMode_DirectStreamCopy, {});
    smart.includeAudio = false;
    smart.smartRendering = true;
    smart.timelineExplicit = true;
    smart.timelineSegments = {{7, 3, false}, {0, 2, false}};
    smart.endFrame = 4;
    VDQtVideoExporter exporter;
    const bool exported = exporter.exportVideo(smart, &decoder);
    if (!exported) std::cerr << exporter.lastError().toStdString() << '\n';
    VDQtVideoDecoder encoded;
    const bool opened = exported && streams(smart.outputPath, 1, 0)
        && encoded.openFile(smart.outputPath) && encoded.ensureFrameIndex().totalFrames == 5;
    passed &= check(opened, "video-only smart packet timeline retains exactly five edited frames");
    if (opened) {
        const QList<int> expected{7, 8, 9, 0, 1};
        for (int frame = 0; frame < expected.size(); ++frame)
            passed &= check(encoded.getFrameImage(frame).convertToFormat(QImage::Format_RGBA8888)
                == decoder.getFrameImage(expected[frame]).convertToFormat(QImage::Format_RGBA8888),
                "video-only smart packet inpoints include native first-video offset");
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
    const QString silent = dir.filePath("silent.mkv"), embedded = dir.filePath("embedded.mkv");
    const QString external = dir.filePath("external.wav"), multiaudio = dir.filePath("external-three.mka");
    const QStringList video{"-f", "lavfi", "-i", "testsrc2=size=64x48:rate=10:duration=2"};
    if (!ffmpeg(video + QStringList{"-frames:v", "20", "-an", "-threads", "1", "-c:v", "ffv1", silent})
        || !ffmpeg(video + QStringList{
            "-f", "lavfi", "-i", "aevalsrc=0.125:s=48000:d=2",
            "-f", "lavfi", "-i", "aevalsrc=-0.25:s=48000:d=2",
            "-map", "0:v:0", "-map", "1:a:0", "-map", "2:a:0", "-frames:v", "20",
            "-t", "2", "-threads", "1", "-c:v", "ffv1", "-g", "1", "-c:a", "pcm_s16le",
            "-disposition:a:0", "default", "-disposition:a:1", "0", embedded})
        || !ffmpeg({"-f", "lavfi", "-i", "aevalsrc=if(lt(t\\,1)\\,0.5\\,-0.5):s=48000:d=2",
            "-c:a", "pcm_s16le", external})
        || !ffmpeg({"-f", "lavfi", "-i", "aevalsrc=0.125:s=48000:d=2",
            "-f", "lavfi", "-i", "aevalsrc=0.375:s=48000:d=2",
            "-f", "lavfi", "-i", "aevalsrc=-0.5:s=48000:d=2",
            "-map", "0:a:0", "-map", "1:a:0", "-map", "2:a:0", "-c:a", "pcm_s16le", multiaudio})) return 3;
    auto gain = VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::Gain);
    gain.params["decibels"] = -6;
    const QList<VDAudioFilterInstance> filters{gain};
    bool passed = selectedCases(dir, silent, external, 0, "silent-external-wav", filters);
    passed &= selectedCases(dir, embedded, external, 0, "embedded-external-wav", filters);
    passed &= selectedCases(dir, embedded, embedded, 2, "nondefault-embedded", filters);
    passed &= selectedCases(dir, embedded, multiaudio, 2, "external-stream2", filters);
    passed &= defaultDirect(dir, embedded, filters);
    passed &= smartAndUnsafe(dir, embedded, external, filters);
    passed &= nativeDirect(dir, filters);
    passed &= videoOnlyOffset(dir);
    return passed ? 0 : 1;
}
