// Small structural fixtures isolate EBML bounds/lacing from codec behavior.
// Real HEVC/H.264 image identity and decoder performance live in decoder_index_tests.
#include "VirtualDub/VDQtMatroskaTiming.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>

namespace {
bool check(bool value, const char *message) {
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}
QByteArray number(quint64 value, int length = 0) {
    if (!length) { length = 1; while (length < 8 && value >> (8 * length)) ++length; }
    QByteArray bytes(length, '\0');
    for (int i = length - 1; i >= 0; --i) { bytes[i] = char(value & 255); value >>= 8; }
    return bytes;
}
QByteArray vint(quint64 value) {
    int length = 1;
    while (length < 8 && value >= (quint64(1) << (7 * length)) - 1) ++length;
    return number(value | (quint64(1) << (7 * length)), length);
}
QByteArray element(quint32 id, const QByteArray& payload, bool unknown = false) {
    return number(id) + (unknown ? QByteArray::fromHex("01ffffffffffffff") : vint(payload.size())) + payload;
}
QByteArray uint(quint32 id, quint64 value) { return element(id, number(value)); }
QByteArray track(quint64 id, const QByteArray& codec, quint64 type, quint64 duration = 41708333,
                 const QByteArray& extra = {}) {
    return element(0xae, uint(0xd7, id) + uint(0x83, type) + element(0x86, codec)
        + uint(0x23e383, duration) + extra);
}
QByteArray block(quint64 track, int relative, uchar flags, const QByteArray& payload) {
    return vint(track) + number(quint16(relative), 2) + QByteArray(1, char(flags)) + payload;
}
QByteArray cluster(quint64 time, const QByteArray& blocks, bool unknown = false) {
    return element(0x1f43b675, uint(0xe7, time) + blocks, unknown);
}
QByteArray document(const QByteArray& tracks, const QByteArray& clusters, bool unknown = false) {
    return element(0x1a45dfa3, element(0x4282, "matroska"))
        + element(0x18538067, element(0x1549a966, uint(0x2ad7b1, 1000000))
            + element(0x1654ae6b, tracks) + clusters, unknown);
}
bool read(QTemporaryDir& directory, const QByteArray& bytes, const QByteArray& reference,
          VDQtMatroskaTiming *timing, int denominator = 1000) {
    const QString path = directory.filePath("headers.mkv");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
    file.close();
    const qsizetype at = bytes.indexOf(reference);
    return at >= 0 && VDQtReadMatroskaTiming(path, at, "V_MPEGH/ISO/HEVC", 1, denominator, timing);
}
bool structuralCases(QTemporaryDir& directory) {
    const auto first = block(300, 0, 0x80, "FIRST-PICTURE");
    const auto next = block(300, 42, 0x80, "SECOND-PICTURE");
    const auto grouped = block(300, 84, 0, "THIRD-PICTURE");
    const auto video = track(300, "V_MPEGH/ISO/HEVC", 1);
    const auto audio = track(1, "A_AAC", 2);
    const auto group = element(0xa0, uint(0x9b, 43) + element(0xa1, grouped) + uint(0xfb, 1));
    const auto bytes = document(audio + video, cluster(0, element(0xa3, block(1, 0, 0x80, "AUDIO"))
        + element(0xa3, first) + element(0xa3, next) + group));
    VDQtMatroskaTiming timing;
    if (!check(read(directory, bytes, first, &timing) && timing.frames.size() == 3
               && timing.frames.at(0).timestamp == 0 && timing.frames.at(1).timestamp == 42
               && timing.frames.at(2).timestamp == 84 && timing.frames.at(2).duration == 43
               && timing.frames.at(0).keyFrame && !timing.frames.at(2).keyFrame,
               "audio-first tracks, long track VINTs and BlockGroups retain exact timing/key flags")) return false;
    const qint64 clusterPosition = bytes.indexOf(QByteArray::fromHex("1f43b675"));
    if (!check(timing.frames.at(0).seekPosition == clusterPosition,
               "seek positions identify cluster IDs rather than compressed payloads")) return false;

    const auto tail = block(300, 0, 0x80, "LAST-PICTURE");
    const auto streaming = document(video, cluster(0, element(0xa3, first), true)
        + cluster(1000, element(0xa3, tail), true), true);
    if (!check(read(directory, streaming, first, &timing) && timing.frames.size() == 2
               && timing.frames.at(1).timestamp == 1000,
               "unknown-size clusters end at the next segment-level element")) return false;

    const auto large = block(300, 0, 0x80, QByteArray(2 * 1024 * 1024, 'x'));
    if (!check(read(directory, document(video, cluster(0, element(0xa3, large))), large, &timing)
               && timing.frames.size() == 1 && timing.headerBytesRead < 2048,
               "a megabyte-sized compressed picture is skipped without reading its payload")) return false;
    // Requested byte counts alone miss QFile's automatic read-ahead. On Linux,
    // also measure actual read() bytes for separated headers, independent of
    // page-cache warmth. This caught almost movie-sized I/O on short HEVC blocks.
    const auto readBytes = []() -> qint64 {
        QFile stats("/proc/self/io");
        if (!stats.open(QIODevice::ReadOnly)) return -1;
        for (const auto& line : stats.readAll().split('\n'))
            if (line.startsWith("rchar:")) return line.mid(6).trimmed().toLongLong();
        return -1;
    };
    QByteArray separated;
    for (int frame = 0; frame < 32; ++frame)
        separated += element(0xa3, block(300, frame * 42, 0x80, QByteArray(8192, 'x')));
    const auto headerOnly = document(video, cluster(0, element(0xa3, first) + separated));
    const qint64 before = readBytes();
    const bool mapped = read(directory, headerOnly, first, &timing);
    const qint64 after = readBytes();
    if (!check(mapped && timing.frames.size() == 33
               && (before < 0 || after < 0 || after - before < 65536),
               "header traversal does not prefetch compressed payload into QFile's buffer")) return false;
    return true;
}

bool lacingCases(QTemporaryDir& directory) {
    const auto video = track(1, "V_MPEGH/ISO/HEVC", 1);
    for (int mode : {1, 2, 3}) {
        QByteArray sizes;
        if (mode == 1) sizes = QByteArray::fromHex("0202");
        if (mode == 3) sizes = QByteArray::fromHex("82bf"); // size=2, signed delta=0.
        const auto first = block(1, 0, uchar(0x80 | (mode << 1)), QByteArray(1, '\2') + sizes + "ABCDEF");
        VDQtMatroskaTiming timing;
        if (!check(read(directory, document(video, cluster(0, element(0xa3, first))), first, &timing)
                   && timing.frames.size() == 3 && timing.frames.at(0).timestamp == 0
                   && timing.frames.at(1).timestamp == 41 && timing.frames.at(2).timestamp == 83
                   && timing.frames.at(0).duration == 41 && timing.frames.at(1).duration == 42
                   && timing.frames.at(0).keyFrame && !timing.frames.at(1).keyFrame,
                   "fixed, Xiph and EBML laces use the demuxer's integer duration distribution")) return false;
    }
    const auto first = block(1, 0, 0x86, QByteArray::fromHex("0283be") + "ABCDEF");
    VDQtMatroskaTiming timing;
    return check(read(directory, document(video, cluster(0, element(0xa3, first))), first, &timing)
        && timing.frames.size() == 3, "signed EBML lace-size deltas preserve three distinct pictures");
}

bool rejectedCases(QTemporaryDir& directory) {
    const auto first = block(1, 0, 0x80, "FIRST-PICTURE");
    const auto normalTrack = track(1, "V_MPEGH/ISO/HEVC", 1);
    const auto clusters = cluster(0, element(0xa3, first));
    const auto rejected = [&](const QByteArray& data, const QByteArray& reference, const char *message) {
        VDQtMatroskaTiming timing;
        timing.referenceTimestamp = 123;
        timing.frames.append({123, 1, -1, false});
        return check(!read(directory, data, reference, &timing) && timing.frames.size() == 1
                     && timing.frames.at(0).timestamp == 123 && timing.referenceTimestamp == 123, message);
    };
    const auto valid = document(normalTrack, clusters);
    if (!rejected(valid.left(valid.size() - 1), first.left(first.size() - 1),
                  "truncated elements fail transactionally")) return false;
    auto overflow = valid;
    const qsizetype at = overflow.indexOf(first);
    overflow[at - 1] = '\x7f';
    if (!rejected(overflow, first, "block sizes cannot extend beyond parent bounds")) return false;
    if (!rejected(document(normalTrack + normalTrack, clusters), first,
                  "duplicate selected tracks cannot become trusted navigation")) return false;
    if (!rejected(document(track(1, "V_MPEGH/ISO/HEVC", 1, 41708333,
                                  uint(0x23e383, 33333333)), clusters), first,
                  "conflicting default durations cannot become trusted timing")) return false;
    if (!rejected(document(track(1, "V_MPEGH/ISO/HEVC", 1, 41708333,
                                  element(0x23314f, QByteArray::fromHex("40000000"))), clusters), first,
                  "unsupported track timestamp scaling uses the decoded fallback")) return false;
    if (!rejected(document(track(1, "V_MPEGH/ISO/HEVC", 1, 41708333, element(0x6d80, {})), clusters), first,
                  "encoded track content cannot be mistaken for ordinary frame blocks")) return false;
    if (!rejected(document(track(1, "V_MPEGH/ISO/HEVC", 1, UINT64_MAX), clusters), first,
                  "duration multiplication cannot overflow")) return false;
    const auto badLace = block(1, 0, 0x86, QByteArray::fromHex("028100") + "ABCDEF");
    if (!rejected(document(normalTrack, cluster(0, element(0xa3, badLace))), badLace,
                  "invalid EBML lace integers cannot overrun payloads")) return false;
    const auto invisible = block(1, 0, 0x88, "INVISIBLE");
    if (!rejected(document(normalTrack, cluster(0, element(0xa3, invisible))), invisible,
                  "invisible pictures require codec-specific decoded accounting")) return false;
    VDQtMatroskaTiming timing;
    return check(!read(directory, valid, first, &timing, 90000),
                 "container and selected-stream clocks must agree");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    return directory.isValid() && structuralCases(directory) && lacingCases(directory)
        && rejectedCases(directory) ? 0 : 1;
}
