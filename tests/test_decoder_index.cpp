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
    return check(decoder.getSeekCount() == 0 && decoder.getIndexLookupWorkCount() <= frames * 2,
                 "sequential indexing does not search the entire previous prefix");
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
    // An approximately labeled sparse picture must not fill the next prefix
    // slot and then be mistaken for verified presentation-order knowledge.
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
    bool observedDuplicate = false;
    for (int frame = 0; frame < frames; ++frame) {
        if (!check(matchesPicture(decoder.getFrameImage(frame, true), frame),
                   "each duplicate-PTS ordinal matches its independently decoded picture")) return false;
        if (frame > 0 && decoder.getFrameTimestampSeconds(frame)
            == decoder.getFrameTimestampSeconds(frame - 1)) observedDuplicate = true;
    }
    return check(observedDuplicate, "the fixture really has duplicate presentation timestamps");
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    VDQtTestFixtures fixtures;
    const bool scaling = sequentialScaling(fixtures);
    const bool duplicates = duplicateTimestamps(fixtures);
    if (!scaling || !duplicates) {
        if (!fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
        return 1;
    }
    return 0;
}
