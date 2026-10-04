// Opt-in repeated-session measurements with real embedded/native audio. The
// short CTest run checks lifecycle/exports, never asserts a machine's speed.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtAudioExport.h"
#include "VirtualDub/VDQtVideoExporter.h"
#include "VirtualDub/VDQtAudioPlayer.h"
#include "VirtualDub/VDQtWaveform.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QThreadPool>
#include <array>
#include <algorithm>
#include <iostream>

namespace {
bool check(bool valid, const QString& error) {
    if (!valid) std::cerr << error.toStdString() << '\n';
    return valid;
}
QJsonObject resources() {
    QJsonObject result;
    QFile file("/proc/self/status");
    if (file.open(QIODevice::ReadOnly)) {
        for (const QByteArray& line : file.readAll().split('\n')) {
            const auto parts = line.simplified().split(' ');
            if (parts.size() < 2) continue;
            if (parts.first() == "VmRSS:") result["rssKiB"] = parts.at(1).toDouble();
            if (parts.first() == "Threads:") result["threads"] = parts.at(1).toInt();
        }
        file.close();
    }
    const QDir descriptors("/proc/self/fd");
    if (descriptors.exists()) result["fileDescriptors"] = descriptors.entryList(
        QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot).size();
    return result;
}
}

int main(int argc, char **argv) {
    VDQtTestFixtures fixtures;
    if (!fixtures.directory.isValid()) return 2;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", fixtures.directory.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", fixtures.directory.filePath("data").toUtf8());
    QApplication app(argc, argv);
    QThreadPool::globalInstance()->setMaxThreadCount(8);
    VDQtVideoDecoder::setDecoderThreadCount(2);
    const auto arguments = app.arguments();
    const bool smoke = arguments.contains("--smoke");
    const bool effects = arguments.contains("--effects");
    int cycles = smoke ? 2 : 12;
    const int cyclesIndex = arguments.indexOf("--cycles");
    if (cyclesIndex >= 0) {
        bool valid = false;
        if (cyclesIndex + 1 >= arguments.size()) return 2;
        cycles = arguments.at(cyclesIndex + 1).toInt(&valid);
        if (!valid || cycles < 1 || cycles > 100) return 2;
    }
    const int width = smoke ? 128 : 1920, height = smoke ? 96 : 1080;
    constexpr int frames = 96, sampleRate = 48000, channels = 2;
    const QString mp4 = fixtures.directory.filePath("embedded-audio.mp4");
    const QString avs = fixtures.directory.filePath("native-audio.avs");
    if (!check(fixtures.ffmpeg({"-f", "lavfi", "-i",
            QString("testsrc2=size=%1x%2:rate=24:duration=4").arg(width).arg(height),
            "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=4",
            "-c:v", "libx264", "-preset", "ultrafast", "-threads", "2",
            "-c:a", "aac", "-ac", "2", "-frames:v", "96", "-t", "4", mp4}), fixtures.error)
        || !check(fixtures.writeText(avs, QString(
            "ClearAutoloadDirs()\nv=BlankClip(length=96,width=%1,height=%2,fps=24,pixel_type=\"RGB24\",audio_rate=0)\n"
            "a=Tone(length=4.0,frequency=440,samplerate=48000,channels=2).ConvertAudioTo16bit()\n"
            "AudioDub(v,a)\n").arg(width).arg(height).toUtf8()), fixtures.error)) return 1;
    QList<VDAudioFilterInstance> filters;
    if (effects) {
        auto& catalog = VDQtAudioFilterSystem::instance();
        auto gain = catalog.createFilter(VDAudioFilterType::Gain);
        gain.params["decibels"] = -3;
        auto resample = catalog.createFilter(VDAudioFilterType::Resample);
        resample.params["sampleRate"] = 44100;
        auto chorus = catalog.createFilter(VDAudioFilterType::Chorus);
        chorus.params["delayMs"] = 50;
        filters = {gain, resample, chorus};
    }
    VDQtVideoDecoder decoder;
    VDQtAudioPlayer audio(false); // Destroy/close audio BEFORE its borrowed graph.
    VDQtVideoExporter exporter;
    const QList<VDAudioFilterInstance> noFilters;
    QJsonArray measurements;
    const QJsonObject baseline = resources();
    for (int cycle = 0; cycle < cycles; ++cycle) {
        QJsonObject sample{{"cycle", cycle + 1}, {"beforeOpen", resources()}};
        QElapsedTimer clock;
        clock.start();
        if (!check(decoder.openFile(mp4) && audio.openFile(mp4), "open embedded-audio MP4")) return 1;
        sample["mp4OpenMs"] = clock.nsecsElapsed() / 1e6;
        clock.restart();
        for (int frame : {0, 80, 3, 48, 95, 12})
            if (!check(!decoder.getFrameImage(frame).isNull(), "scrub MP4 trim points")) return 1;
        sample["mp4ScrubMs"] = clock.nsecsElapsed() / 1e6;
        // Opening the audio stream alone does not exercise its decoder. Read an
        // actual one-second window before switching to the borrowed AVS graph.
        const QString mp4Audio = fixtures.directory.filePath("mp4-audio.wav");
        VDQtWaveformData waveform;
        QString error;
        clock.restart();
        if (!check(audio.exportAudioToFile(mp4Audio, 0, sampleRate, {}, &noFilters, false)
                && VDQtReadWaveformPeaks(mp4Audio, 1, &waveform, &error)
                && waveform.sampleFrames == sampleRate && waveform.channels == channels
                && !waveform.peaks.isEmpty() && waveform.peaks.first() > 0,
                "decode actual embedded MP4 audio")) return 1;
        sample["mp4AudioExtractMs"] = clock.nsecsElapsed() / 1e6;
        if (!check(QFile::remove(mp4Audio), "remove owned MP4 audio")) return 1;
        audio.close();
        clock.restart();
        if (!check(decoder.openFile(avs) && audio.openAvsClip(decoder.getAvsClip(),
                decoder.getAvsVi(), decoder.getAvsAccessMutex()), "open borrowed native AVS audio")) return 1;
        sample["avsOpenMs"] = clock.nsecsElapsed() / 1e6;
        // Exercise the real native producer and live graph without a hardware
        // sink; the offline player above otherwise creates no producer thread.
        {
            clock.restart();
            AVSAudioDevice producer(decoder.getAvsClip(), decoder.getAvsVi(), decoder.getAvsAccessMutex());
            if (!check(producer.initialize(), producer.error())) return 1;
            VDQtAudioFilterDevice filtered(&producer, sampleRate, channels);
            if (!check(filtered.setFilterChain(filters), filtered.error())) return 1;
            sample["audioPrimeMs"] = clock.nsecsElapsed() / 1e6;
            clock.restart();
            std::array<char, 8192> block{};
            qint64 delivered = 0;
            while (!filtered.atEnd()) {
                const qint64 count = filtered.read(block.data(), block.size());
                if (!check(count >= 0 && filtered.error().isEmpty() && producer.error().isEmpty()
                        && clock.elapsed() < 30000, "native audio pull failed or exceeded deadline")) return 1;
                delivered += count;
                if (!count) QThread::msleep(1);
            }
            if (!check(delivered > 0 && (effects || delivered == qint64(4) * sampleRate * channels * 2),
                    "native audio byte delivery")) return 1;
            sample["audioPullMs"] = clock.nsecsElapsed() / 1e6;
            sample["audioBytes"] = double(delivered);
        }
        const QString prepared = fixtures.directory.filePath("prepared.wav");
        clock.restart();
        if (!check(VDQtPrepareAudioWav(audio, prepared, {{0, 4 * sampleRate}}, filters,
                {}, &error), error)) return 1;
        sample["audioPrepareMs"] = clock.nsecsElapsed() / 1e6;
        if (!check(VDQtReadWaveformPeaks(prepared, 1, &waveform, &error)
                && waveform.channels == channels && waveform.sampleFrames > 0,
                "prepared audio sample count")) return 1;
        const int64_t preparedSamples = waveform.sampleFrames;
        const int64_t videoAudioSamples = int64_t(4) * (effects ? 44100 : sampleRate);
        if (!check(preparedSamples >= videoAudioSamples,
                "prepared soundtrack spans the complete video clock")) return 1;
        sample["preparedAudioSamples"] = double(preparedSamples);
        if (!check(QFile::remove(prepared), "remove owned prepared audio")) return 1;
        VDQtVideoExporter::ExportOptions options;
        options.inputPath = avs;
        options.outputPath = fixtures.directory.filePath("prores.mov");
        options.containerType = "mov";
        options.endFrame = frames - 1;
        options.unattended = true;
        options.includeAudio = true;
        options.audioMode = AudioMode_FullProcessing;
        options.processing = VDQtVideoExporter::ProcessingSnapshot{};
        options.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("prores_ks");
        options.processing->audioCodec.codecId = "pcm_s16le";
        options.processing->audioFilters = filters;
        clock.restart();
        if (!check(exporter.exportVideo(options, &decoder, &audio), exporter.lastError())) return 1;
        const double exportMs = clock.nsecsElapsed() / 1e6;
        sample["exportMs"] = exportMs;
        sample["exportFps"] = frames * 1000 / std::max(0.001, exportMs);
        sample["afterExport"] = resources();
        sample["fixtureDiskBytes"] = double(fixtures.diskBytes());
        // Export timing excludes this independent output integrity scan.
        VDQtVideoDecoder output;
        VDQtAudioPlayer outputAudio(false);
        if (!check(output.openFile(options.outputPath)
                && output.ensureFrameIndex().totalFrames == frames
                && outputAudio.openFile(options.outputPath) && outputAudio.hasAudio()
                && outputAudio.getSampleRate() == (effects ? 44100 : sampleRate)
                && outputAudio.getChannels() == channels,
                "ProRes output frame count and processed audio stream")) return 1;
        const QString finalAudio = fixtures.directory.filePath("final-audio.wav");
        if (!check(outputAudio.exportAudioToFile(finalAudio, 0, -1, {}, &noFilters, false)
                && VDQtReadWaveformPeaks(finalAudio, 1, &waveform, &error)
                // Preparation retains the single effect tail; video export
                // deliberately trims it at the four-second video boundary.
                && waveform.sampleFrames == videoAudioSamples && waveform.channels == channels
                && !waveform.peaks.isEmpty() && waveform.peaks.first() > 0,
                "ProRes soundtrack contains the complete non-silent processed audio")) return 1;
        sample["finalAudioSamples"] = double(waveform.sampleFrames);
        if (!check(QFile::remove(finalAudio), "remove owned decoded final audio")) return 1;
        outputAudio.close();
        output.close();
        audio.close();
        decoder.close();
        if (!check(QFile::remove(options.outputPath), "remove owned ProRes output")) return 1;
        sample["afterClose"] = resources();
        sample["fixtureDiskAfterClose"] = double(fixtures.diskBytes());
        measurements.append(sample);
    }
    const QByteArray report = QJsonDocument(QJsonObject{
        {"fixture", "alternating synthetic MP4+AAC / core-only AVS+native audio / ProRes+PCM"},
        {"effects", effects}, {"hardwareSink", false}, {"width", width}, {"height", height},
        {"baseline", baseline}, {"temporaryAudioOutsideFixtureNotCounted", true},
        {"resourceMeasurements", "parent-process boundary snapshots; observational, not leak proof"},
        {"sourceCoverage", "synthetic core-only AVS; does not cover LSMASH or user plugin retention"},
        {"cycles", measurements}}).toJson();
    std::cout << report.constData();
    const int reportIndex = arguments.indexOf("--report");
    if (reportIndex >= 0) {
        if (reportIndex + 1 >= arguments.size()) return 2;
        QFile file(arguments.at(reportIndex + 1));
        if (!check(file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                && file.write(report) == report.size() && file.flush(), "write new benchmark report")) return 1;
    }
    return 0;
}
