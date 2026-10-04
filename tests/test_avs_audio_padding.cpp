// Native AviSynth audio may end before its video's timeline. Positive video
// requests retain their duration; source-EOF/nonpadding requests do not invent
// trailing audio. All scripts, outputs and settings belong to this test.
#include "VirtualDub/VDQtAudioExport.h"
#include "VirtualDub/VDQtAudioPlayer.h"
#include "VirtualDub/VDQtVideoExporter.h"
#include "VirtualDub/VDQtWaveform.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const QString& message) {
    if (!condition) std::cerr << "FAIL: " << message.toStdString() << '\n';
    return condition;
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QByteArray wavPayload(const QString& path) {
    const QByteArray bytes = read(path);
    if (bytes.size() < 12 || bytes.mid(0, 4) != "RIFF" || bytes.mid(8, 4) != "WAVE") return {};
    for (qsizetype position = 12; position + 8 <= bytes.size();) {
        const quint32 size = VDQtWaveformDetail::le32(bytes.constData() + position + 4);
        if (size > quint64(bytes.size() - position - 8)) return {};
        if (bytes.mid(position, 4) == "data") return bytes.mid(position + 8, size);
        position += 8 + size + (size & 1);
    }
    return {};
}
bool sameOffsetPcm(const QByteArray& first, const QByteArray& second, bool floating) {
    if (!floating) return first == second;
    if (first.size() != second.size() || first.size() % sizeof(float)) return false;
    // Tone may round an independently requested phase origin differently. This
    // comparison covers that fixture property only; source-prefix and silence
    // checks for the normal padded window remain byte-exact.
    for (qsizetype position = 0; position < first.size(); position += sizeof(float)) {
        float actual = 0, expected = 0;
        std::memcpy(&actual, first.constData() + position, sizeof actual);
        std::memcpy(&expected, second.constData() + position, sizeof expected);
        if (!std::isfinite(actual) || !std::isfinite(expected)
            || std::fabs(actual - expected) > 1.e-6F) return false;
    }
    return true;
}
QByteArray ffmpegPcm(const QString& path) {
    QProcess process;
    process.start("ffmpeg", {"-v", "error", "-nostdin", "-i", path,
        "-map", "0:a:0", "-c:a", "pcm_s16le", "-f", "s16le", "-"});
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        process.kill(); process.waitForFinished(3000); return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode()) {
        std::cerr << process.readAllStandardError().constData(); return {};
    }
    return process.readAllStandardOutput();
}
int videoFrameCount(const QString& path) {
    QProcess process;
    process.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-count_frames",
        "-show_entries", "stream=nb_read_frames", "-of", "json", path});
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        process.kill(); process.waitForFinished(3000); return -1;
    }
    const auto streams = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("streams").toArray();
    return streams.isEmpty() ? -1 : streams.first().toObject().value("nb_read_frames").toString().toInt();
}

struct SampleType {
    const char *name;
    const char *conversion;
    int bytes;
    bool floating;
};
bool nativeWindows(QTemporaryDir& directory, const SampleType& type, QString *script16,
                   QByteArray *reference16) {
    const QString stem = QString::fromLatin1(type.name);
    const QString script = directory.filePath(stem + ".avs");
    const QByteArray scriptBytes = QByteArray(
        "ClearAutoloadDirs()\n"
        "v=BlankClip(length=20,width=64,height=48,fps=10,pixel_type=\"RGB24\",audio_rate=0)\n"
        "a=Tone(length=1.0,frequency=440,samplerate=48000,channels=2).")
        + type.conversion + "()\nAudioDub(v,a)\n";
    if (!write(script, scriptBytes)) return false;
    // The player borrows the decoder-owned clip/VideoInfo, so destruction must
    // close the player first (reverse declaration order).
    VDQtVideoDecoder decoder;
    VDQtAudioPlayer audio(false);
    if (!check(decoder.openFile(script), stem + ": load built-in native clip")) {
        std::cerr << decoder.getLastError().toStdString() << '\n'; return false;
    }
    const AVS_VideoInfo *info = decoder.getAvsVi();
    if (!check(info && info->num_frames == 20 && info->num_audio_samples == 48000
        && avs_bytes_per_channel_sample(info) == type.bytes
        && audio.openAvsClip(decoder.getAvsClip(), info, decoder.getAvsAccessMutex()),
        stem + ": actual native video/audio lengths and sample format")) return false;
    const QList<VDAudioFilterInstance> noFilters;
    const QString eof = directory.filePath(stem + "-eof.wav");
    bool passed = check(audio.exportAudioToFile(eof, 0, -1, {}, &noFilters), stem + ": EOF export");
    const QByteArray source = wavPayload(eof);
    const qsizetype bytesPerFrame = 2 * type.bytes;
    passed &= check(source.size() == 48000 * bytesPerFrame, stem + ": EOF uses actual source length");
    const char silentByte = type.bytes == 1 ? char(0x80) : char(0);
    const QByteArray silence(48000 * bytesPerFrame, silentByte);

    const QString padded = directory.filePath(stem + "-padded.wav");
    passed &= check(audio.exportAudioToFile(padded, 0, 96000, {}, &noFilters)
        && wavPayload(padded) == source + silence, stem + ": positive range preserves real prefix and silent tail");
    VDQtWaveformData waveform;
    QString error;
    passed &= check(VDQtReadWaveformPeaks(padded, 16, &waveform, &error)
        && waveform.sampleFrames == 96000 && waveform.durationSeconds == 2
        && waveform.containerBits == type.bytes * 8 && waveform.floatingPoint == type.floating
        && waveform.channels == 2, stem + ": waveform reads requested edited clock and actual PCM precision");
    if (waveform.peaks.size() == 16) {
        for (int column = 8; column < 16; ++column)
            passed &= check(waveform.peaks[column] == 0, stem + ": silent waveform tail is actually silent");
    }
    const QString clamped = directory.filePath(stem + "-nonpadding.wav");
    passed &= check(audio.exportAudioToFile(clamped, 0, 96000, {}, &noFilters, false)
        && wavPayload(clamped) == source, stem + ": explicit nonpadding mode stops at EOF");
    const QString offset = directory.filePath(stem + "-offset.wav");
    const QString offsetReference = directory.filePath(stem + "-offset-reference.wav");
    passed &= check(audio.exportAudioToFile(offsetReference, 24000, 24000, {}, &noFilters),
        stem + ": extract actual offset reference");
    // Float generators may round a different phase origin by one ULP; compare
    // the same native read window rather than re-slicing another request.
    const QByteArray offsetPrefix = wavPayload(offsetReference);
    passed &= check(offsetPrefix.size() == 24000 * bytesPerFrame,
        stem + ": offset reference has its actual bounded length");
    passed &= check(audio.exportAudioToFile(offset, 24000, 72000, {}, &noFilters)
        && sameOffsetPcm(wavPayload(offset), offsetPrefix + silence, type.floating)
        && wavPayload(offset).right(silence.size()) == silence,
        stem + ": offset range clips native reads and preserves requested silent duration");
    const QString offsetUnpadded = directory.filePath(stem + "-offset-nonpadding.wav");
    passed &= check(audio.exportAudioToFile(offsetUnpadded, 24000, 72000, {}, &noFilters, false)
        && sameOffsetPcm(wavPayload(offsetUnpadded), offsetPrefix, type.floating),
        stem + ": offset nonpadding remains source bounded");
    const QString beyond = directory.filePath(stem + "-beyond.wav");
    const QByteArray marker("EXISTING_OUTPUT_MUST_SURVIVE");
    passed &= check(write(beyond, marker)
        && !audio.exportAudioToFile(beyond, 48000, 100, {}, &noFilters)
        && !audio.exportAudioToFile(beyond, 96000, 100, {}, &noFilters)
        && read(beyond) == marker, stem + ": start at/after EOF retains existing failure and atomic replacement");
    const QString cancelled = directory.filePath(stem + "-cancelled.wav");
    bool cancelledDuringTail = false;
    passed &= check(write(cancelled, marker)
        && !audio.exportAudioToFile(cancelled, 0, 96000,
            [&](int value, int total) {
                if (total > 0 && value * 100 / total >= 60) {
                    cancelledDuringTail = true; return false;
                }
                return true;
            }, &noFilters)
        && cancelledDuringTail && read(cancelled) == marker,
        stem + ": cancelling while padding preserves previous output");
    passed &= check(!audio.exportAudioToFile(beyond, 0, std::numeric_limits<int64_t>::max(), {}, &noFilters)
        && read(beyond) == marker, stem + ": unrepresentable PCM size fails without replacement");
    passed &= check(read(script) == scriptBytes, stem + ": generated script was not changed");
    if (type.bytes == 2) {
        *script16 = script;
        *reference16 = source;
    }
    return passed;
}

bool finalVideos(QTemporaryDir& directory, const QString& script, const QByteArray& nativePrefix) {
    VDQtVideoDecoder decoder;
    VDQtAudioPlayer audio(false);
    if (!decoder.openFile(script) || !audio.openAvsClip(decoder.getAvsClip(), decoder.getAvsVi(),
        decoder.getAvsAccessMutex())) return false;
    bool passed = true;
    // Exercise the same finite edited-range -> float WAV -> peak reader path
    // used by the video waveform, without opening a modal chart in this test.
    VDQtAudioExportRequest waveformRequest;
    waveformRequest.outputPath = directory.filePath("native-waveform.wav");
    waveformRequest.codec.codecId = "pcm_f32le";
    waveformRequest.codec.bitDepth = 32;
    QString error;
    QList<QPair<int64_t, int64_t>> finiteRanges;
    passed &= check(decoder.ensureFrameIndex().totalFrames == 20
        && VDQtAudioRangesForTimeline(decoder, {}, 0, 19, 48000, &finiteRanges, &error)
        && VDQtCapAudioSampleRanges(finiteRanges, 48000 * 10, &waveformRequest.sampleRanges, &error)
        && VDQtExportAudio(audio, waveformRequest, {}, &error),
        "native video waveform composes its finite edited timeline audio");
    VDQtWaveformData waveform;
    passed &= check(VDQtReadWaveformPeaks(waveformRequest.outputPath, 16, &waveform, &error)
        && waveform.floatingPoint && waveform.containerBits == 32
        && waveform.sampleFrames == 96000 && waveform.durationSeconds == 2,
        "native video waveform retains two-second edited duration");
    if (waveform.peaks.size() == 16)
        for (int column = 8; column < 16; ++column)
            passed &= check(waveform.peaks[column] == 0, "native video waveform renders a silent trailing second");
    waveformRequest.outputPath = directory.filePath("native-audio-only-waveform.wav");
    waveformRequest.padToRequestedLength = false;
    passed &= check(VDQtExportAudio(audio, waveformRequest, {}, &error)
        && VDQtReadWaveformPeaks(waveformRequest.outputPath, 16, &waveform, &error)
        && waveform.sampleFrames == 48000 && waveform.durationSeconds == 1,
        "native audio-only nonpadding waveform still stops at actual EOF");
    for (int mode : {VideoMode_FullProcessing, VideoMode_FastRecompress}) {
        VDQtVideoExporter::ExportOptions request;
        request.inputPath = script;
        request.outputPath = directory.filePath(QString("native-video-%1.mkv").arg(mode));
        request.videoMode = mode;
        request.audioMode = AudioMode_FullProcessing;
        request.containerType = "mkv";
        request.unattended = true;
        request.endFrame = 19;
        request.processing = VDQtVideoExporter::ProcessingSnapshot{};
        request.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
        request.processing->audioCodec.codecId = "pcm_s16le";
        request.processing->audioCodec.bitDepth = 16;
        VDQtVideoExporter exporter;
        int renderedCallbacks = 0;
        const bool ok = exporter.exportVideo(request, &decoder, &audio, nullptr,
            [&](int, const QImage&, const QImage&) { ++renderedCallbacks; });
        if (!ok) std::cerr << exporter.lastError().toStdString() << '\n';
        const QByteArray result = ok ? ffmpegPcm(request.outputPath) : QByteArray{};
        passed &= check(ok && videoFrameCount(request.outputPath) == 20
            && result == nativePrefix + QByteArray(48000 * 2 * 2, char(0)),
            QString("native final video mode%1 retains two-second audio with exact one-second prefix").arg(mode));
        passed &= check(mode == VideoMode_FullProcessing ? renderedCallbacks == 20 : renderedCallbacks == 0,
            QString("native mode%1 uses its actual rendered/Fast path").arg(mode));
    }
    return passed;
}
}

int main(int argc, char **argv) {
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", directory.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", directory.filePath("data").toUtf8());
    QApplication app(argc, argv);
    bool passed = true;
    QString script16;
    QByteArray reference16;
    for (const SampleType& type : {
        SampleType{"u8", "ConvertAudioTo8bit", 1, false},
        SampleType{"s16", "ConvertAudioTo16bit", 2, false},
        SampleType{"s24", "ConvertAudioTo24bit", 3, false},
        SampleType{"s32", "ConvertAudioTo32bit", 4, false},
        SampleType{"f32", "ConvertAudioToFloat", 4, true}})
        passed &= nativeWindows(directory, type, &script16, &reference16);
    if (!script16.isEmpty()) passed &= finalVideos(directory, script16, reference16);
    return passed ? 0 : 1;
}
