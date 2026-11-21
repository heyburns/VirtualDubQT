// Presentation ordinals must retain duplicate PTS frames, and building a known
// sequential prefix must not search every older entry for every decoded frame.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtVideoDecoder.h"
#include "VirtualDub/VDQtFrameDecodeWorker.h"
#include "VirtualDub/VDQtMp4Timing.h"
#include "VirtualDub/VDQtAudioExport.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QtEndian>
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

bool mp4Navigation(VDQtTestFixtures& fixtures, const QString& name,
                   bool variableRate, const QStringList& extra = {}, const QString& codec = "libx264",
                   bool audioFirst = false) {
    const QString input = fixtures.directory.filePath(name + ".mp4");
    const int frames = variableRate ? 300 : 3000;
    QStringList arguments;
    if (audioFirst) arguments << "-f" << "lavfi" << "-i" << "sine=frequency=440:sample_rate=48000:duration=20";
    arguments << "-f" << "lavfi" << "-i" << "testsrc2=size=96x64:rate=30";
    if (audioFirst) arguments << "-map" << "0:a:0" << "-map" << "1:v:0" << "-c:a" << "aac";
    if (variableRate) arguments << "-vf"
        << "setpts='if(lt(N,150),N/(10*TB),(15+(N-150)/30)/TB)'";
    arguments << "-frames:v" << QString::number(frames) << "-fps_mode" << "passthrough"
        << "-c:v" << codec << "-g" << "30"
        << "-keyint_min" << "30" << "-sc_threshold" << "0" << "-bf" << "3"
        << "-threads" << "2";
    if (!audioFirst) arguments << "-an";
    if (codec == "libx264" || codec == "libx265") arguments << "-preset" << "ultrafast";
    if (!fixtures.ffmpeg(arguments + extra + QStringList{input})) return false;
    const QString referencePath = fixtures.directory.filePath(name + ".rgb");
    if (!fixtures.ffmpeg({"-i", input, "-fps_mode", "passthrough", "-pix_fmt", "rgb24",
                          "-c:v", "rawvideo", "-threads", "1", "-f", "rawvideo", "-an", referencePath})) return false;
    QFile referenceFile(referencePath);
    if (!referenceFile.open(QIODevice::ReadOnly)) return false;
    const QByteArray reference = referenceFile.readAll();
    if (!check(reference.size() == frames * 96 * 64 * 3,
               "independent MP4 reference contains all displayed pictures")) return false;
    VDQtVideoDecoder decoder;
    if (!check(decoder.openFile(input) && decoder.hasFrameNavigation()
               && !decoder.hasCompleteFrameIndex() && decoder.isFrameCountExact()
               && decoder.getFrameCount() == frames && decoder.getDecodedFrameCount() == 0,
               "MP4 opens with an exact metadata map and no decoded scan")) return false;
    const auto snapshot = decoder.frameIndexSnapshot();
    if (!check(snapshot && decoder.ensureFrameNavigation().totalFrames == frames
               && decoder.getDecodedFrameCount() == 0 && !decoder.hasCompleteFrameIndex(),
               "navigation metadata is distinct from a completed decoded inspection")) return false;
    QElapsedTimer timer;
    timer.start();
    for (int frame : {frames - 5, frames / 2, 1, frames - 1, 30, 31, 29, 0, 150}) {
        decoder.clearCache();
        const QImage image = decoder.getFrameImage(frame).convertToFormat(QImage::Format_RGB888);
        if (!check(image.size() == QSize(96, 64), "cold MP4 seeks return an image")) return false;
        for (int y = 0; y < 64; ++y) {
            if (!check(std::memcmp(image.constScanLine(y), reference.constData()
                    + (frame * 64 + y) * 96 * 3, 96 * 3) == 0,
                    "MP4 random seek matches independently decoded presentation ordinal")) return false;
        }
    }
    std::cout << name.toStdString() << ": " << timer.elapsed() << " ms, "
              << decoder.getDecodedFrameCount() << " pictures for nine random seeks\n";
    if (!check(decoder.getDecodedFrameCount() < 450 && !decoder.hasCompleteFrameIndex()
               && decoder.getIndexLookupWorkCount() < 1000 && decoder.frameIndexSnapshot() == snapshot,
               "first and subsequent MP4 seeks decode bounded dependency spans, not the movie")) return false;
    if (variableRate && !check(decoder.getFrameTimestampSeconds(150) == 15.0,
                              "container navigation retains exact VFR frame timing")) return false;
    if (variableRate) {
        const quint64 before = decoder.getDecodedFrameCount();
        QList<QPair<int64_t, int64_t>> ranges;
        QString error;
        if (!check(VDQtAudioRangesForTimeline(decoder, {}, 150, 152, 48000, &ranges, &error)
                   && ranges == QList<QPair<int64_t, int64_t>>{{720000, 4800}}
                   && decoder.getDecodedFrameCount() == before,
                   "VFR audio selection uses precise metadata boundaries without decoding video")) return false;
    }
    VDQtVideoDecoder consumer;
    if (!check(consumer.openFile(input, snapshot)
               && consumer.hasFrameNavigation() && !consumer.hasCompleteFrameIndex()
               && consumer.ensureFrameNavigation().totalFrames == frames
               && consumer.getDecodedFrameCount() == 0,
               "sharing metadata never upgrades it to decoded health proof")) return false;
    const auto cancelled = decoder.scanVideoStream([](int frame, int) { return frame < 3; });
    if (!check(cancelled.cancelled && decoder.hasFrameNavigation() && !decoder.hasCompleteFrameIndex()
               && decoder.frameIndexSnapshot() == snapshot,
               "cancelling a healthy inspection retains independent container navigation")) return false;
    decoder.clearCache();
    const auto inspection = decoder.scanVideoStream();
    return check(inspection.totalFrames == frames && inspection.errorMessage.isEmpty()
                 && decoder.hasCompleteFrameIndex() && decoder.adoptFrameIndexSnapshot(snapshot)
                 && decoder.hasCompleteFrameIndex(),
                 "explicit MP4 health inspection still decodes and verifies every frame");
}

bool mp4MetadataBounds(VDQtTestFixtures& fixtures) {
    QFile source(fixtures.directory.filePath("indexed-cfr.mp4"));
    if (!source.open(QIODevice::ReadOnly)) return false;
    const QByteArray original = source.readAll();
    const qsizetype ctts = original.indexOf("ctts");
    const qsizetype moov = original.indexOf("moov");
    if (ctts < 4 || moov < 4) return false;
    QVector<qint64> validOffsets;
    if (!check(VDQtReadMp4CompositionOffsets(source.fileName(), 1, 15360, 3000, &validOffsets)
               && validOffsets.size() == 3000, "metadata mutation tests begin with a valid matching track")) return false;
    const auto rejected = [&](const QByteArray& bytes, const char *message) {
        const QString path = fixtures.directory.filePath("invalid-metadata.mp4");
        QVector<qint64> offsets{123};
        return fixtures.writeText(path, bytes)
            && check(!VDQtReadMp4CompositionOffsets(path, 1, 15360, 3000, &offsets)
                     && offsets == QVector<qint64>{123}, message);
    };
    QByteArray bytes = original;
    bytes[ctts + 4] = 2;
    if (!rejected(bytes, "unknown CTTS version cannot become trusted navigation")) return false;
    bytes = original;
    qToBigEndian<quint32>(0x7fffffff, reinterpret_cast<uchar*>(bytes.data() + ctts + 12));
    if (!rejected(bytes, "composition run count cannot overrun the sample array")) return false;
    bytes = original;
    qToBigEndian<quint32>(0xffffffff, reinterpret_cast<uchar*>(bytes.data() + ctts - 4));
    if (!rejected(bytes, "nested boxes cannot exceed their parent or file bounds")) return false;
    bytes = original;
    qToBigEndian<quint32>(0xffffffff, reinterpret_cast<uchar*>(bytes.data() + ctts + 8));
    if (!rejected(bytes, "untrusted run counts cannot trigger oversized allocations")) return false;

    // Convert a real tail moov to its legal 64-bit-size form. Video payload
    // positions stay unchanged, so the decoder must retain fast exact access.
    bytes = original;
    const quint32 oldSize = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + moov - 4));
    QByteArray extended(8, '\0');
    qToBigEndian<quint64>(quint64(oldSize) + 8, reinterpret_cast<uchar*>(extended.data()));
    bytes.insert(moov + 4, extended);
    qToBigEndian<quint32>(1, reinterpret_cast<uchar*>(bytes.data() + moov - 4));
    const QString path = fixtures.directory.filePath("extended-moov.mp4");
    VDQtVideoDecoder decoder;
    return fixtures.writeText(path, bytes) && check(decoder.openFile(path) && decoder.hasFrameNavigation()
        && !decoder.getFrameImage(2999).isNull() && decoder.getDecodedFrameCount() < 90,
        "64-bit movie box sizes retain exact container navigation without reading payload prefixes");
}

bool fragmentedMp4Fallback(VDQtTestFixtures& fixtures) {
    const QString input = fixtures.directory.filePath("fragmented.mp4");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=96x64:rate=30",
        "-frames:v", "300", "-c:v", "libx264", "-preset", "ultrafast", "-g", "30",
        "-bf", "3", "-threads", "1", "-movflags", "+frag_keyframe+empty_moov", "-an", input})) return false;
    VDQtVideoDecoder reference, decoder;
    if (!reference.openFile(input) || reference.scanVideoStream().totalFrames != 300
        || !decoder.openFile(input)) return false;
    if (!check(!decoder.hasFrameNavigation(), "fragmented sample tables are not mistaken for a complete movie")) return false;
    return check(decoder.getFrameImage(150) == reference.getFrameImage(150)
        && decoder.hasCompleteFrameIndex() && decoder.getFrameCount() == 300,
        "fragmented MP4 retains the accurate decoded-index fallback");
}

bool intraMovNavigation(VDQtTestFixtures& fixtures) {
    const QString input = fixtures.directory.filePath("prores.mov");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=96x64:rate=25",
        "-frames:v", "32", "-c:v", "prores_ks", "-profile:v", "0", "-threads", "2", "-an", input})) return false;
    VDQtVideoDecoder reference, decoder;
    if (!reference.openFile(input) || reference.scanVideoStream().totalFrames != 32
        || !decoder.openFile(input)) return false;
    if (!check(decoder.hasFrameNavigation() && !decoder.hasCompleteFrameIndex()
               && decoder.getDecodedFrameCount() == 0,
               "intra MOV without a composition table opens without a decoded pass")) return false;
    for (int frame : {31, 2, 17, 0})
        if (!check(decoder.getFrameImage(frame) == reference.getFrameImage(frame),
                   "high-depth ProRes navigation preserves exact frame identity and pixels")) return false;
    return check(decoder.getDecodedFrameCount() < 16, "intra MOV seeks decode only the requested pictures");
}

bool queuedIndexDelivery(VDQtTestFixtures& fixtures) {
    const QString source = fixtures.directory.filePath("exact-vfr.mkv");
    VDQtVideoDecoder consumer;
    if (!consumer.openFile(source)) return false;
    QThread thread;
    auto *worker = new VDQtFrameDecodeWorker;
    worker->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    thread.start();
    bool opened = false;
    QMetaObject::invokeMethod(worker, [&] {
        opened = worker->openSource(source, "Autoselect", 0, 0, 0);
    }, Qt::BlockingQueuedConnection);
    bool delivered = false;
    QEventLoop loop;
    QObject::connect(worker, &VDQtFrameDecodeWorker::frameIndexAvailable, &loop,
        [&](quint64 generation, const VDQtVideoDecoder::FrameIndexSnapshotPtr& snapshot) {
            delivered = generation == 1 && consumer.adoptFrameIndexSnapshot(snapshot)
                && consumer.getDecodedFrameCount() == 0
                && consumer.getFrameTimestampSeconds(150) == 15.0;
            loop.quit();
        });
    if (opened) {
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        worker->requestFrame(150, 1, false, false);
        loop.exec();
    }
    QMetaObject::invokeMethod(worker, [worker] { worker->closeSource(); }, Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
    return check(opened && delivered,
                 "real worker queued delivery installs verified VFR timing without rescanning");
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    VDQtFrameDecodeWorker worker;
    if (!check(QMetaType::fromName("VDQtVideoDecoder::FrameIndexSnapshotPtr").isValid(),
               "worker snapshot alias is registered for queued GUI delivery")) return 1;
    VDQtTestFixtures fixtures;
    const bool scaling = sequentialScaling(fixtures);
    const bool duplicates = duplicateTimestamps(fixtures);
    const bool ordinals = exactVfrOrdinals(fixtures);
    const bool delivered = ordinals && queuedIndexDelivery(fixtures);
    const bool mp4 = mp4Navigation(fixtures, "indexed-cfr", false)
        && mp4Navigation(fixtures, "indexed-vfr", true)
        && mp4Navigation(fixtures, "signed-ctts-vfr", true, {"-movflags", "+negative_cts_offsets+faststart"})
        && mp4Navigation(fixtures, "open-gop-hevc", true,
            {"-x265-params", "pools=1:frame-threads=1:log-level=error:open-gop=1:keyint=30:min-keyint=30:scenecut=0"}, "libx265")
        && mp4Navigation(fixtures, "av1-vfr", true, {"-cpu-used", "8", "-crf", "40"}, "libaom-av1")
        && mp4Navigation(fixtures, "vp9-vfr", true, {"-deadline", "realtime", "-cpu-used", "8"}, "libvpx-vp9")
        && mp4Navigation(fixtures, "audio-first-vfr", true, {}, "libx264", true)
        && mp4MetadataBounds(fixtures) && fragmentedMp4Fallback(fixtures)
        && intraMovNavigation(fixtures);
    if (!scaling || !duplicates || !ordinals || !delivered || !mp4) {
        if (!fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
        return 1;
    }
    return 0;
}
