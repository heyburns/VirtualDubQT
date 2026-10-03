// Presentation ordinals must retain duplicate PTS frames, and building a known
// sequential prefix must not search every older entry for every decoded frame.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtVideoDecoder.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <cstring>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool sequentialScaling(VDQtTestFixtures& fixtures) {
    const QString input = fixtures.directory.filePath("long-prefix.nut");
    constexpr int frames = 7200;
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=16x16:rate=60",
                          "-frames:v", QString::number(frames), "-c:v", "rawvideo",
                          "-threads", "1", "-an", input})) return false;
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(input)) return false;
    decoder.resetPerformanceCounters();
    QElapsedTimer timer;
    timer.start();
    for (int frame = 0; frame < frames; ++frame) {
        if (!check(!decoder.getFrameImage(frame, true).isNull(),
                   "sequential scaling source returns every frame")) return false;
    }
    std::cout << "7200-frame sequential traversal: " << timer.elapsed()
              << " ms, lookup work=" << decoder.getIndexLookupWorkCount() << '\n';
    if (!check(decoder.getSeekCount() == 0 && decoder.getIndexLookupWorkCount() <= frames * 2,
               "sequential indexing does not search the entire previous prefix")) return false;
    if (!check(decoder.getFrameImage(frames, true).isNull() && decoder.hasCompleteFrameIndex(),
               "only verified EOF marks the index complete")) return false;
    decoder.resetPerformanceCounters();
    const auto cached = decoder.ensureFrameIndex();
    if (!check(cached.totalFrames == frames && !cached.cancelled && cached.errorMessage.isEmpty()
               && decoder.getDecodedFrameCount() == 0,
               "ensuring a complete index does not decode the source again")) return false;
    const auto cancelled = decoder.ensureFrameIndex([](int, int) { return false; });
    if (!check(cancelled.cancelled && decoder.hasCompleteFrameIndex(),
               "cancelling a cached request preserves verified index knowledge")) return false;
    const auto partial = decoder.scanVideoStream([](int current, int) { return current < 7; });
    if (!check(partial.cancelled && !decoder.hasCompleteFrameIndex(),
               "a cancelled fresh health scan is not a complete reusable index")) return false;
    decoder.resetPerformanceCounters();
    if (!check(decoder.ensureFrameIndex().totalFrames == frames && decoder.hasCompleteFrameIndex()
               && decoder.getDecodedFrameCount() >= frames,
               "ensuring an incomplete scan rebuilds the complete index")) return false;
    decoder.setErrorMode(1);
    if (!check(!decoder.hasCompleteFrameIndex(),
               "changing corrupt-frame recovery invalidates ordinal knowledge")) return false;
    decoder.resetPerformanceCounters();
    if (!check(decoder.ensureFrameIndex().totalFrames == frames && decoder.hasCompleteFrameIndex()
               && decoder.getDecodedFrameCount() >= frames,
               "new error policy receives its own verified index")) return false;
    decoder.close();
    if (!check(!decoder.hasCompleteFrameIndex() && decoder.openFile(input)
               && !decoder.hasCompleteFrameIndex(),
               "closing/reopening even the same path cannot reuse stale session index state")) return false;
    int checks = 0;
    decoder.resetPerformanceCounters();
    if (!check(decoder.getFrameImage(6000, false, [&] { return ++checks < 100; }).isNull()
               && decoder.getDecodedFrameCount() < 200 && !decoder.hasCompleteFrameIndex()
               && !decoder.reachedEndOfStream(),
               "obsolete cold seek cancels indexing promptly without false EOF/completeness")) return false;
    decoder.resetPerformanceCounters();
    if (!check(decoder.ensureFrameIndex().totalFrames == frames
               && decoder.getDecodedFrameCount() < frames,
               "a replacement index request resumes verified work instead of restarting")) return false;
    if (!check(!decoder.getFrameImage(0).isNull(),
               "cancelled cold seek leaves the decoder reusable")) return false;
    decoder.clearCache();
    decoder.resetPerformanceCounters();
    for (int frame : {6000, 7199, 4200, 11}) {
        if (!check(!decoder.getFrameImage(frame).isNull(),
                   "verified long-source random seek succeeds")) return false;
        std::cout << "Indexed seek " << frame << ": cumulative decodes="
                  << decoder.getDecodedFrameCount() << '\n';
    }
    if (!check(decoder.getDecodedFrameCount() < 256,
               "repeated indexed all-keyframe seeks do not walk the entire prefix")) return false;
    decoder.clearCache();
    if (!check(!decoder.getFrameImage(0).isNull(), "prepare cancellable sequential traversal")) return false;
    checks = 0;
    decoder.resetPerformanceCounters();
    return check(decoder.getFrameImage(7000, true, [&] { return ++checks < 100; }).isNull()
                 && decoder.getDecodedFrameCount() < 200 && !decoder.reachedEndOfStream()
                 && !decoder.getFrameImage(0).isNull(),
                 "obsolete long sequential decode cancels between packets and recovers");
}

bool duplicateTimestamps(VDQtTestFixtures& fixtures) {
    const QString input = fixtures.directory.filePath("duplicate-pts.mkv");
    constexpr int frames = 48;
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=96x64:rate=24",
                          "-frames:v", QString::number(frames),
                          "-vf", "setpts=floor(N/2)/(24*TB)", "-fps_mode", "passthrough",
                          "-c:v", "ffv1", "-pix_fmt", "bgr0", "-threads", "1", "-an", input})) return false;
    const QString referencePath = fixtures.directory.filePath("duplicate-reference.rgb");
    if (!fixtures.ffmpeg({"-i", input, "-fps_mode", "passthrough", "-pix_fmt", "rgb24",
                          "-c:v", "rawvideo", "-threads", "1", "-f", "rawvideo",
                          "-an", referencePath})) return false;
    QFile referenceFile(referencePath);
    if (!referenceFile.open(QIODevice::ReadOnly)) return false;
    const QByteArray reference = referenceFile.readAll();
    if (!check(reference.size() == frames * 96 * 64 * 3,
               "independent FFmpeg decode retains all duplicate-PTS pictures")) return false;
    const auto matchesPicture = [&](const QImage& decoded, int frame) {
        const QImage image = decoded.convertToFormat(QImage::Format_RGB888);
        if (image.size() != QSize(96, 64)) return false;
        for (int y = 0; y < 64; ++y) {
            if (std::memcmp(image.constScanLine(y),
                            reference.constData() + (frame * 64 + y) * 96 * 3,
                            96 * 3) != 0) return false;
        }
        return true;
    };
    // Random seeks must preserve duplicate-PTS identity, not merely keep a
    // later sequential scan safe from approximately labeled observations.
    VDQtVideoDecoder sparse;
    if (!sparse.openFile(input) || sparse.getFrameImage(0).isNull()
        || sparse.getFrameImage(20).isNull() || sparse.getFrameImage(1).isNull()) return false;
    sparse.clearCache();
    for (int frame = 0; frame < frames; ++frame) {
        if (!check(matchesPicture(sparse.getFrameImage(frame, true), frame),
                   "sparse seeks cannot poison a subsequent verified traversal")) return false;
    }
    if (!check(sparse.getFrameImage(frames, true).isNull() && sparse.isFrameCountExact()
               && sparse.getFrameCount() == frames,
               "verified traversal after sparse observations retains the complete source")) return false;
    VDQtVideoDecoder decoder;
    if (!decoder.openFile(input)) return false;
    const auto scan = decoder.scanVideoStream();
    if (!check(scan.totalFrames == frames && decoder.isFrameCountExact()
               && scan.errorMessage.isEmpty(),
               "a full scan retains every distinct frame with duplicate timestamps")) return false;
    for (int frame : {20, 1, 47, 0, 32, 21}) {
        decoder.clearCache();
        if (!check(matchesPicture(decoder.getFrameImage(frame), frame),
                   "random duplicate-PTS seek returns the requested picture, not its PTS sibling")) return false;
    }
    decoder.clearCache();
    bool observedDuplicate = false;
    for (int frame = 0; frame < frames; ++frame) {
        if (!check(matchesPicture(decoder.getFrameImage(frame, true), frame),
                   "each duplicate-PTS ordinal matches its independently decoded picture")) return false;
        if (frame > 0 && decoder.getFrameTimestampSeconds(frame)
            == decoder.getFrameTimestampSeconds(frame - 1)) observedDuplicate = true;
    }
    return check(observedDuplicate, "the fixture really has duplicate presentation timestamps");
}

bool exactVfrOrdinals(VDQtTestFixtures& fixtures) {
    const QString input = fixtures.directory.filePath("exact-vfr.mkv");
    constexpr int frames = 300;
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=96x64:rate=30",
                          "-frames:v", QString::number(frames), "-vf",
                          "setpts='if(lt(N,150),N/(10*TB),(15+(N-150)/30)/TB)'",
                          "-fps_mode", "passthrough", "-c:v", "ffv1", "-pix_fmt", "bgr0",
                          "-threads", "1", "-an", input})) return false;
    VDQtVideoDecoder reference;
    if (!reference.openFile(input) || reference.scanVideoStream().totalFrames != frames) return false;
    const QImage expected = reference.getFrameImage(150);
    VDQtVideoDecoder fresh;
    if (!fresh.openFile(input) || fresh.getFrameImage(0).isNull()) return false;
    const QImage firstSeek = fresh.getFrameImage(150);
    if (!check(!expected.isNull() && firstSeek == expected,
               "a cold VFR ordinal seek matches its verified presentation frame")) {
        std::cerr << "Cold ordinal 150 timestamp=" << fresh.getFrameTimestampSeconds(150)
                  << ", verified=" << reference.getFrameTimestampSeconds(150) << '\n';
        return false;
    }
    if (!check(fresh.scanVideoStream().totalFrames == frames,
               "VFR health scan retains the complete source")) return false;
    if (!check(fresh.getFrameImage(150) == firstSeek,
               "indexing cannot change the picture assigned to an ordinal")) return false;
    const auto snapshot = fresh.frameIndexSnapshot();
    VDQtVideoDecoder consumer;
    if (!consumer.openFile(input)) return false;
    if (!check(!consumer.frameIndexSnapshot() && consumer.adoptFrameIndexSnapshot(snapshot)
               && consumer.ensureFrameIndex().totalFrames == frames
               && consumer.getDecodedFrameCount() == 0
               && consumer.getFrameTimestampSeconds(150) == reference.getFrameTimestampSeconds(150),
               "another decoder adopts exact VFR timing without a redundant scan")) return false;
    if (!check(consumer.getFrameImage(150) == firstSeek
               && consumer.frameIndexSnapshot() == snapshot,
               "revisiting an unchanged shared index keeps its cached immutable snapshot")) return false;
    consumer.setErrorMode(1);
    if (!check(!consumer.adoptFrameIndexSnapshot(snapshot),
               "index from a different corruption policy cannot be adopted")) return false;
    consumer.close();
    if (!consumer.openFile(fixtures.directory.filePath("duplicate-pts.mkv"))) return false;
    return check(!consumer.adoptFrameIndexSnapshot(snapshot),
                 "index from another source cannot be adopted");
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    VDQtTestFixtures fixtures;
    const bool scaling = sequentialScaling(fixtures);
    const bool duplicates = duplicateTimestamps(fixtures);
    const bool ordinals = exactVfrOrdinals(fixtures);
    if (!scaling || !duplicates || !ordinals) {
        if (!fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
        return 1;
    }
    return 0;
}
