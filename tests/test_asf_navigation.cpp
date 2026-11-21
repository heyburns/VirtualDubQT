// ASF acceleration must preserve exact frame identity through fragmented
// objects, sparse seeks, rewind and shared preview snapshots.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtAsfTiming.h"
#include "VirtualDub/VDQtVideoDecoder.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QtEndian>
#include <iostream>

namespace {
bool check(bool value, const char *message) {
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}
QByteArray hash(const QImage& picture) {
    const auto image = picture.convertToFormat(QImage::Format_RGB888);
    if (image.isNull()) return {};
    QCryptographicHash md5(QCryptographicHash::Md5);
    for (int y = 0; y < image.height(); ++y)
        md5.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 3));
    return md5.result().toHex();
}

// Tiny structural fixtures exercise fragment accounting without embedding a
// user's media or relying on an encoder to produce malformed ASF headers.
QByteArray integer(quint64 value, int size) {
    QByteArray data(size, '\0');
    for (int i = 0; i < size; ++i) data[i] = char(value >> (8 * i));
    return data;
}
QByteArray object(const char *guid, const QByteArray& body) {
    return QByteArray::fromHex(guid) + integer(24 + body.size(), 8) + body;
}
QByteArray packet(int number, int offset, int length, int stamp, bool key = true, bool replication = true) {
    // Standard ECC, fixed 96-byte packet, one payload, byte-sized object number
    // and fragment offset. The picture's total size is three bytes.
    const QByteArray header = QByteArray::fromHex("8200000855") + integer(96 - (replication ? 24 : 16) - length, 1)
        + integer(stamp, 4) + integer(40, 2) + integer(key ? 130 : 2, 1)
        + integer(number, 1) + integer(offset, 1) + integer(replication ? 8 : 0, 1)
        + (replication ? integer(3, 4) + integer(stamp, 4) : QByteArray{});
    return header + QByteArray(length, 'x') + QByteArray(96 - header.size() - length, '\0');
}
QByteArray structuralFile(const QByteArray& packets, int count = 3, bool encrypted = false) {
    const QByteArray id(16, 'i');
    QByteArray properties(80, '\0');
    properties.replace(0, 16, id);
    properties.replace(32, 8, integer(count, 8));
    properties.replace(64, 4, integer(2, 4));
    properties.replace(68, 4, integer(96, 4));
    properties.replace(72, 4, integer(96, 4));
    QByteArray stream(54, '\0');
    stream.replace(0, 16, QByteArray::fromHex("c0ef19bc4d5bcf11a8fd00805f5c442b"));
    stream.replace(48, 2, integer(encrypted ? 0x8002 : 2, 2));
    const auto header = object("3026b2758e66cf11a6d900aa0062ce6c", integer(2, 4) + QByteArray::fromHex("0102")
        + object("a1dcab8c47a9cf118ee400c00c205365", properties)
        + object("9107dcb7b7a9cf118ee600c00c205365", stream));
    return header + object("3626b2758e66cf11a6d900aa0062ce6c", id + integer(count, 8)
        + QByteArray::fromHex("0101") + packets);
}
bool structuralHeaders(VDQtTestFixtures& fixtures) {
    const QString path = fixtures.directory.filePath("headers.asf");
    const QByteArray fragments = packet(255, 0, 2, 1000) + packet(255, 2, 1, 1000) + packet(0, 0, 3, 1040);
    const QByteArray valid = structuralFile(fragments);
    QVector<VDQtAsfFrameTiming> timing;
    if (!fixtures.writeText(path, valid)
        || !check(VDQtReadAsfTiming(path, 2, &timing) && timing.size() == 2
            && timing[0].timestamp == 1000 && timing[1].timestamp == 1040
            && timing[1].seekPosition - timing[0].seekPosition == 192,
            "complete fragments and wrapping object numbers retain two pictures")) return false;
    QVector<VDQtAsfFrameTiming> inherited;
    if (!fixtures.writeText(path, structuralFile(packet(1, 0, 2, 1000)
            + packet(1, 2, 1, 1000, false, false), 2))
        || !check(VDQtReadAsfTiming(path, 2, &inherited) && inherited.size() == 1
            && inherited.first().keyFrame && inherited.first().timestamp == 1000,
            "continuations inherit object timing and the first fragment's key flag")) return false;
    const auto rejected = [&](const QByteArray& bytes, const char *message) {
        QVector<VDQtAsfFrameTiming> sentinel{{17, 23, 31, true}};
        return fixtures.writeText(path, bytes) && check(!VDQtReadAsfTiming(path, 2, &sentinel)
            && sentinel.size() == 1 && sentinel[0].timestamp == 17 && sentinel[0].seekPosition == 23,
            message);
    };
    if (!rejected(structuralFile(packet(255, 0, 2, 1000) + packet(255, 1, 1, 1000)
            + packet(0, 0, 3, 1040)), "overlapping/noncontiguous fragments are rejected")
        || !rejected(structuralFile(packet(255, 0, 2, 1000) + packet(255, 2, 1, 1001)
            + packet(0, 0, 3, 1040)), "fragment timestamps must agree")
        || !rejected(structuralFile(packet(255, 0, 2, 1000) + packet(254, 2, 1, 1000)
            + packet(0, 0, 3, 1040)), "fragments must identify the same media object")
        || !rejected(structuralFile(packet(255, 0, 2, 1000) + packet(0, 0, 3, 1040), 2),
            "a restarted object cannot hide an incomplete picture")
        || !rejected(structuralFile(packet(255, 0, 2, 1000), 1), "an incomplete final picture is rejected")
        || !rejected(structuralFile(packet(255, 0, 3, 1000) + packet(0, 0, 3, 1000), 2),
            "duplicate timestamps cannot establish exact ordinals")
        || !rejected(structuralFile(packet(255, 0, 3, 1000, false), 1), "the first picture must be a keyframe")
        || !rejected(structuralFile(fragments, 2), "packet counts cannot hide unindexed tail data")
        || !rejected(structuralFile(fragments, 3, true), "encrypted streams retain the decoded fallback")) return false;
    for (int cut : {1, 23, 29, 100, int(valid.size() - 1)})
        if (!rejected(valid.left(cut), "truncated object/packet bounds are rejected")) return false;
    auto damaged = valid;
    const auto firstPacket = timing.first().seekPosition;
    damaged.replace(firstPacket + 16, 4, integer(UINT32_MAX, 4));
    if (!rejected(damaged, "oversized media objects are rejected")) return false;
    damaged = valid;
    damaged[qsizetype(firstPacket + 15)] = '\1';
    if (!rejected(damaged, "compressed multi-object video payloads use decoded verification")) return false;
    damaged = valid;
    damaged[qsizetype(firstPacket + 5)] = char(255);
    if (!rejected(damaged, "padding cannot exceed the packet boundary")) return false;
    damaged = valid; damaged.replace(24, 4, integer(UINT32_MAX, 4));
    return rejected(damaged, "declared header counts must match actual objects");
}
bool navigation(VDQtTestFixtures& fixtures, const QString& name, const QString& rate, int variant, bool audioFirst) {
    const auto input = fixtures.directory.filePath(name + ".wmv");
    const auto oracle = fixtures.directory.filePath(name + ".framemd5");
    QStringList args;
    if (audioFirst) args << "-f" << "lavfi" << "-i" << "sine=sample_rate=48000:duration=110";
    args << "-f" << "lavfi" << "-i" << "testsrc2=size=96x64:rate=" + rate;
    if (audioFirst) args << "-map" << "0:a:0" << "-map" << "1:v:0" << "-c:a" << "wmav2";
    args << "-frames:v" << "3000" << "-c:v" << (name.startsWith("wmv1") ? "wmv1" : "wmv2")
         << "-g" << (variant == 5 ? "1" : "30") << "-threads" << "1";
    if (!audioFirst) args << "-an";
    if (variant == 1) args << "-vf" << "select='not(eq(mod(n,7),2))'" << "-fps_mode" << "vfr";
    if (variant == 3) args << "-output_ts_offset" << "4.5";
    if (!fixtures.ffmpeg(args + QStringList{input})) return false;
    // An absent optional Simple Index must retain accurate native seeking.
    if (variant == 2) {
        QFile file(input);
        if (!file.open(QIODevice::ReadWrite)) return false;
        const auto bytes = file.readAll();
        const auto guid = QByteArray::fromHex("90080033b1e5cf1189f400a0c90349cb");
        const auto at = bytes.lastIndexOf(guid);
        if (at < 0 || !file.resize(at)) return false;
    }
    if (!fixtures.ffmpeg({"-i", input, "-map", "0:v:0", "-an", "-pix_fmt", "rgb24",
                            "-fps_mode", "passthrough", "-c:v", "rawvideo", "-threads", "1",
                            "-f", "framemd5", oracle})) return false;
    QFile file(oracle);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QList<QByteArray> expected;
    for (const auto& line : file.readAll().split('\n'))
        if (!line.trimmed().isEmpty() && !line.startsWith('#')) expected.append(line.split(',').last().trimmed());
    if (expected.size() != 3000) return false;
    VDQtVideoDecoder decoder;
    if (!check(decoder.openFile(input) && decoder.hasFrameNavigation()
        && !decoder.hasCompleteFrameIndex() && decoder.isFrameCountExact() && decoder.getFrameCount() == 3000
        && decoder.getDecodedFrameCount() == 0, "ASF opens with exact navigation and no decoded pictures")) return false;
    const auto snapshot = decoder.frameIndexSnapshot();
    decoder.resetPerformanceCounters();
    QElapsedTimer timer;
    timer.start();
    for (int frame : {2995, 1, 2999, 1500, 30, 0, 150, 29, 31}) {
        decoder.clearCache();
        if (!check(hash(decoder.getFrameImage(frame)) == expected.at(frame),
                   "ASF random seek matches an independent full decode")) return false;
    }
    std::cout << name.toStdString() << ": " << timer.elapsed() << " ms, "
              << decoder.getDecodedFrameCount() << " pictures for nine seeks\n";
    if (!check(decoder.getDecodedFrameCount() < 500 && !decoder.hasCompleteFrameIndex()
        && decoder.frameIndexSnapshot() == snapshot, "ASF seeks decode nearby dependencies and preserve the shared map")) return false;
    VDQtVideoDecoder consumer;
    if (!check(consumer.openFile(input, snapshot) && consumer.getDecodedFrameCount() == 0
        && hash(consumer.getFrameImage(2999)) == expected.at(2999),
        "preview adopts ASF presentation order and native seek positions without another header pass")) return false;
    const auto inspection = decoder.scanVideoStream();
    consumer.close();
    return check(inspection.totalFrames == 3000 && decoder.hasCompleteFrameIndex()
        && consumer.openFile(input, decoder.frameIndexSnapshot())
        && hash(consumer.getFrameImage(1500)) == expected.at(1500),
        "fresh ASF inspections and adopted decoded snapshots retain exact seeking");
}

bool fallback(VDQtTestFixtures& fixtures) {
    const auto normal = fixtures.directory.filePath("wmv2.wmv");
    QFile file(normal);
    if (!file.open(QIODevice::ReadOnly)) return false;
    auto bytes = file.readAll();
    const auto at = bytes.indexOf(QByteArray::fromHex("a1dcab8c47a9cf118ee400c00c205365"));
    if (at < 0) return false;
    // Native FFmpeg ignores this stale declared count. Our structural reader
    // rejects its disagreement with Data Object length rather than hiding tail
    // frames; normal decoded indexing must remain usable afterward.
    bytes.replace(at + 24 + 32, 8, integer(1, 8));
    const auto damaged = fixtures.directory.filePath("stale-count.wmv");
    if (!fixtures.writeText(damaged, bytes)) return false;
    VDQtVideoDecoder decoder, reference;
    return check(decoder.openFile(damaged) && !decoder.hasFrameNavigation()
        && decoder.ensureFrameIndex().totalFrames == 3000 && reference.openFile(normal)
        && hash(decoder.getFrameImage(2999)) == hash(reference.getFrameImage(2999))
        && hash(decoder.getFrameImage(0)) == hash(reference.getFrameImage(0)),
        "rejected ASF metadata retains complete decoded fallback and exact tail/rewind frames");
}

} // namespace
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    VDQtVideoDecoder::setDecoderThreadCount(2);
    VDQtTestFixtures fixtures;
    const bool result = structuralHeaders(fixtures)
        && navigation(fixtures, "wmv2", "30", 0, false)
        && navigation(fixtures, "wmv1", "30000/1001", 0, true)
        && navigation(fixtures, "variable", "30", 1, false)
        && navigation(fixtures, "no-index", "30", 2, false)
        && navigation(fixtures, "offset", "30", 3, false)
        && navigation(fixtures, "intra", "30", 5, false)
        && fallback(fixtures);
    if (!result && !fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return result ? 0 : 1;
}
