// Deterministic presentation/storage policies plus real reordered video/audio
// exports. All files and configuration are owned disposable test fixtures.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtDisplayImageCache.h"
#include "VirtualDub/VDQtUiUpdateThrottle.h"
#include "VirtualDub/VDQtVideoExportStorage.h"
#include "VirtualDub/VDQtVideoExporter.h"
#include "VirtualDub/VDQtAudioPlayer.h"

#include <QApplication>
#include <QFileInfo>
#include <limits>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool presentationPolicies() {
    bool passed = true;
    VDQtUiUpdateThrottle throttle(100);
    int updates = 0;
    for (int ms = 0; ms < 1000; ++ms) updates += throttle.shouldUpdate(ms);
    passed &= check(updates == 10 && throttle.shouldUpdate(999, true)
        && !throttle.shouldUpdate(999) && throttle.shouldUpdate(1),
        "UI updates are bounded, final updates explicit and restarted clocks safe");
    QImage source = VDQtTestFixtures::patternedImage(19, 13, QImage::Format_RGBA8888, 0);
    VDQtDisplayImageCache cache;
    const QSize small(11, 7);
    const auto smooth = Qt::SmoothTransformation;
    const QImage first = cache.render(source, small, false, smooth);
    passed &= check(first == source.scaled(small, Qt::IgnoreAspectRatio, smooth)
        && cache.render(source, small, false, smooth).cacheKey() == first.cacheKey(),
        "unchanged paint inputs reuse exact existing Qt scaling results");
    QImage changed = source;
    changed.setPixelColor(0, 0, Qt::red);
    const QImage replacement = cache.render(changed, small, false, smooth);
    passed &= check(replacement.cacheKey() != first.cacheKey()
        && replacement == changed.scaled(small, Qt::IgnoreAspectRatio, smooth),
        "changed pixels invalidate the one-entry display cache");
    for (const auto mode : {Qt::FastTransformation, Qt::SmoothTransformation}) {
        const QImage alpha = cache.render(source, small, true, mode);
        passed &= check(alpha == source.convertToFormat(QImage::Format_Alpha8)
            .scaled(small, Qt::IgnoreAspectRatio, mode),
            "alpha and interpolation controls retain their previous exact semantics");
    }
    QImage high = VDQtTestFixtures::patternedImage(19, 13, QImage::Format_RGBA64, 1);
    passed &= check(cache.render(high, QSize(17, 9), false, smooth)
        == high.scaled(QSize(17, 9), Qt::IgnoreAspectRatio, smooth),
        "cached scaling does not reduce high-bit-depth image precision");
    cache.clear();
    const auto fresh = cache.render(source, small, false, smooth);
    cache.clear();
    passed &= check(cache.render(source, small, false, smooth).cacheKey() != fresh.cacheKey()
        && cache.render({}, small, false, smooth).isNull(), "Clear and null frames release cached presentation data");
    VDQtDisplayImageCache tiny(64);
    passed &= check(tiny.render(source, small, false, smooth).cacheKey()
        != tiny.render(source, small, false, smooth).cacheKey(),
        "oversized results bypass retention rather than pinning large zoom images");
    return passed;
}

VDAudioFilterInstance effect(VDAudioFilterType type, const QString& key, double value) {
    VDAudioFilterInstance result;
    result.type = type;
    result.params[key] = value;
    return result;
}
bool storagePolicies() {
    bool passed = true;
    qint64 plain = 0, edited = 0, processed = 0, disabled = 0;
    QString error;
    passed &= check(VDQtEstimatePcmTemporaryStorage(10, 48000, 2, {}, 1, &plain, &error)
        && plain >= 10 * 48000 * 2 * 4
        && VDQtEstimatePcmTemporaryStorage(10, 48000, 2, {}, 4, &edited, &error)
        && edited >= plain * 2, "PCM estimate includes retained edit segments and concatenated output");
    auto resample = effect(VDAudioFilterType::Resample, "sampleRate", 96000);
    auto slow = effect(VDAudioFilterType::TimeStretch, "factor", 0.5);
    passed &= check(VDQtEstimatePcmTemporaryStorage(10, 48000, 2, {resample, slow}, 4, &processed, &error)
        && processed > edited * 4, "resampling, time stretch and extraction copies contribute to disk preflight");
    resample.enabled = slow.enabled = false;
    passed &= check(VDQtEstimatePcmTemporaryStorage(10, 48000, 2, {resample, slow}, 1, &disabled, &error)
        && disabled == plain, "disabled audio effects do not inflate temporary-storage estimates");
    const auto pitch = effect(VDAudioFilterType::PitchShift, "semitones", 48);
    qint64 pitched = 0;
    passed &= check(VDQtEstimatePcmTemporaryStorage(10, 768000, 2, {pitch}, 1, &pitched, &error)
        && pitched > qint64(10) * 768000 * 2 * 4 * 16,
        "clamped pitch-shift rate accounts for its resulting duration expansion");
    qint64 sentinel = 123;
    passed &= check(!VDQtEstimatePcmTemporaryStorage(std::numeric_limits<double>::max(), 48000, 2, {}, 1, &sentinel, &error)
        && sentinel == 123 && !error.isEmpty()
        && !VDQtEstimatePcmTemporaryStorage(1, 48000, 2, {}, std::numeric_limits<qint64>::max(), &sentinel)
        && !VDQtEstimatePcmTemporaryStorage(1, 48000, 2,
            {effect(VDAudioFilterType::TimeStretch, "factor", std::numeric_limits<double>::quiet_NaN())}, 1, &sentinel),
        "nonfinite/overflow estimates fail without publishing a wrapped byte count");
    return passed;
}

bool reorderedExport(VDQtTestFixtures& fixtures) {
    const QString source = fixtures.directory.filePath("edited-source.mkv");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=32x24:rate=24",
        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
        "-frames:v", "8", "-t", "0.333333333", "-c:v", "ffv1", "-c:a", "pcm_s16le", source})) return false;
    VDQtVideoDecoder decoder;
    VDQtAudioPlayer audio(false);
    if (!decoder.openFile(source) || decoder.ensureFrameIndex().totalFrames != 8
        || !audio.openFile(source) || !audio.hasAudio()) return false;
    VDQtVideoExporter exporter;
    VDQtVideoExporter::ExportOptions request;
    request.inputPath = source;
    request.outputPath = fixtures.directory.filePath("reordered-export.mkv");
    request.containerType = "mkv";
    request.videoMode = VideoMode_FullProcessing;
    request.audioMode = AudioMode_FullProcessing;
    request.unattended = true;
    request.timelineExplicit = true;
    request.timelineSegments = {{4, 2, false}, {0, 2, false}, {2, 2, true}};
    request.startFrame = 1;
    request.endFrame = 4;
    request.processing = VDQtVideoExporter::ProcessingSnapshot{};
    request.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
    request.processing->videoCodec.ffv1Slices = 4;
    request.processing->audioCodec.codecId = "pcm_s16le";
    QList<int> positions;
    bool passed = check(exporter.exportVideo(request, &decoder, &audio, nullptr,
        [&](int timelinePosition, const QImage&, const QImage&) { positions.append(timelinePosition); })
        && positions == QList<int>{1, 2, 3, 4},
        "cut/reordered/masked export callbacks retain timeline positions and complete edited audio concat");
    request.outputPath = fixtures.directory.filePath("cancelled-export.mkv");
    request.includeAudio = false;
    positions.clear();
    passed &= check(!exporter.exportVideo(request, &decoder, nullptr, nullptr,
        [&](int position, const QImage&, const QImage&) { positions.append(position); },
        [](int done, int) { return done <= 100; })
        && exporter.wasCancelled() && positions == QList<int>{1}
        && !QFileInfo::exists(request.outputPath),
        "per-input abort callbacks remain prompt even when presentation updates are throttled");
    return passed;
}
}

int main(int argc, char **argv) {
    VDQtTestFixtures fixtures;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", fixtures.directory.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", fixtures.directory.filePath("data").toUtf8());
    QApplication app(argc, argv);
    const bool passed = presentationPolicies() & storagePolicies() & reorderedExport(fixtures);
    if (!passed && !fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return passed ? 0 : 1;
}
