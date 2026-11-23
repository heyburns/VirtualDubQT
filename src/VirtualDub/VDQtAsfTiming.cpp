#include "VDQtAsfTiming.h"

#include <QFile>
#include <QtEndian>
#include <climits>
#include <utility>

namespace {
const QByteArray headerGuid = QByteArray::fromHex("3026b2758e66cf11a6d900aa0062ce6c");
const QByteArray dataGuid = QByteArray::fromHex("3626b2758e66cf11a6d900aa0062ce6c");
const QByteArray fileGuid = QByteArray::fromHex("a1dcab8c47a9cf118ee400c00c205365");
const QByteArray streamGuid = QByteArray::fromHex("9107dcb7b7a9cf118ee600c00c205365");
const QByteArray videoGuid = QByteArray::fromHex("c0ef19bc4d5bcf11a8fd00805f5c442b");
const QByteArray extensionGuid = QByteArray::fromHex("b503bf5f2ea9cf118ee300c00c205365");
const QByteArray extendedStreamGuid = QByteArray::fromHex("cba5e61472c632438399a96952065b5a");
const QByteArray durationGuid = QByteArray::fromHex("5094bdc67f86074983a3c77921b733ad");
constexpr int maxFrames = 4 * 1024 * 1024;

// This is a bounded structural reader, not another demuxer. In particular, QFile
// buffering must remain disabled: its read-ahead would read the movie payload
// every time a small header is followed by a seek over compressed picture data.
class Reader {
public:
    QFile file;
    quint64 bytesRead = 0;
    bool read(qint64 at, qint64 count, qint64 end, char *result) {
        if (at < 0 || count < 0 || at > end || count > end - at
            || count > 1024 * 1024 || bytesRead + quint64(count) > 256 * 1024 * 1024
            || (file.pos() != at && !file.seek(at))) return false;
        const qint64 got = file.read(result, count);
        if (got > 0) bytesRead += quint64(got);
        return got == count;
    }
    bool bytes(qint64 at, qint64 count, qint64 end, QByteArray *result) {
        if (count < 0 || count > 1024 * 1024) return false;
        result->resize(count);
        return read(at, count, end, result->data());
    }
    bool uint(qint64& at, int size, qint64 end, quint32 *value) {
        char data[4];
        if (size < 0 || size > 4 || !read(at, size, end, data)) return false;
        quint32 result = 0;
        for (int i = 0; i < size; ++i) result |= quint32(uchar(data[i])) << (8 * i);
        at += size;
        *value = result;
        return true;
    }
    bool sized(qint64& at, quint32 type, qint64 end, quint32 *value, quint32 fallback = 0) {
        const int sizes[] = {0, 1, 2, 4};
        if (!(type & 3)) { *value = fallback; return true; }
        return uint(at, sizes[type & 3], end, value);
    }
    struct Object { QByteArray guid; qint64 data = 0, end = 0; };
    bool object(qint64 at, qint64 end, Object *out) {
        QByteArray data;
        if (!bytes(at, 24, end, &data)) return false;
        const quint64 size = qFromLittleEndian<quint64>(data.data() + 16);
        if (size < 24 || size > quint64(end - at)) return false;
        *out = {data.left(16), at + 24, at + qint64(size)};
        return true;
    }
};

// Resolve the selected stream and packet geometry from headers first.
// One ASF packet can contain parts of several video or audio objects, so
// build the frame map from complete selected objects, not packet count.
class MapReader {
public:
    Reader reader;
    QVector<VDQtAsfFrameTiming> frames;
    int streamId = 0;
    QByteArray fileId;
    quint64 packetCount = 0;
    quint32 packetSize = 0;
    int replicatedSize = 8;
    bool haveFile = false, haveStream = false, haveExtensions = false;
    quint64 objects = 0;

    bool properties(const Reader::Object& object) {
        QByteArray data;
        if (haveFile || object.end - object.data != 80
            || !reader.bytes(object.data, 80, object.end, &data)) return false;
        haveFile = true;
        fileId = data.left(16);
        packetCount = qFromLittleEndian<quint64>(data.data() + 32);
        packetSize = qFromLittleEndian<quint32>(data.data() + 68);
        return packetCount > 0 && packetCount <= 16 * 1024 * 1024
            && !(qFromLittleEndian<quint32>(data.data() + 64) & 1) // No broadcast/unknown length.
            && packetSize >= 20 && packetSize <= 1024 * 1024
            && packetSize == qFromLittleEndian<quint32>(data.data() + 72);
    }
    bool stream(const Reader::Object& object) {
        QByteArray data;
        if (!reader.bytes(object.data, 54, object.end, &data)) return false;
        const quint16 flags = qFromLittleEndian<quint16>(data.data() + 48);
        if ((flags & 127) != streamId) return true;
        const quint32 typeSize = qFromLittleEndian<quint32>(data.data() + 40);
        const quint32 correctionSize = qFromLittleEndian<quint32>(data.data() + 44);
        if (haveStream || (flags & 0x8000) || data.left(16) != videoGuid
            || quint64(typeSize) + correctionSize != quint64(object.end - object.data - 54)) return false;
        haveStream = true;
        return true;
    }
    bool extensions(const Reader::Object& object) {
        QByteArray data;
        if (!reader.bytes(object.data, 64, object.end, &data)) return false;
        if (qFromLittleEndian<quint16>(data.data() + 48) != streamId) return true;
        if (haveExtensions) return false;
        haveExtensions = true;
        const quint16 names = qFromLittleEndian<quint16>(data.data() + 60);
        const quint16 extensions = qFromLittleEndian<quint16>(data.data() + 62);
        qint64 at = object.data + 64;
        for (int i = 0; i < names; ++i) {
            if (!reader.bytes(at, 4, object.end, &data)) return false;
            const quint16 length = qFromLittleEndian<quint16>(data.data() + 2);
            at += 4;
            if (length > object.end - at) return false;
            at += length;
        }
        // Common WMV files add a two-byte picture duration. Other extensions,
        // especially the PTS override used by DVR-MS, need decoded verification.
        // Reject them rather than treating an object's decode time as its PTS.
        if (extensions > 1) return false;
        if (extensions) {
            if (!reader.bytes(at, 22, object.end, &data) || data.left(16) != durationGuid
                || qFromLittleEndian<quint16>(data.data() + 16) != 2
                || qFromLittleEndian<quint32>(data.data() + 18) != 0) return false;
            replicatedSize = 10;
            at += 22;
        }
        return at == object.end; // Embedded stream descriptors use the fallback.
    }
    // Visit direct header children and the one supported extension layer.
    // Unknown objects are skipped by their validated length.
    bool headers(qint64 at, qint64 end, int depth = 0, qint64 expected = -1) {
        quint32 count = 0;
        while (at < end) {
            Reader::Object object;
            if (++objects > 1024 * 1024 || !reader.object(at, end, &object)) return false;
            if (object.guid == fileGuid) { if (!properties(object)) return false; }
            else if (object.guid == streamGuid) { if (!stream(object)) return false; }
            else if (object.guid == extendedStreamGuid) { if (!extensions(object)) return false; }
            else if (object.guid == extensionGuid) {
                QByteArray data;
                if (depth || !reader.bytes(object.data, 22, object.end, &data)
                    || qFromLittleEndian<quint32>(data.data() + 18) != object.end - object.data - 22
                    || !headers(object.data + 22, object.end, depth + 1)) return false;
            }
            at = object.end;
            ++count;
        }
        return expected < 0 || expected == count;
    }
    // Check the complete packet span against the declared fixed size. Each
    // packet resets its local payload bounds, while a selected video object
    // can remain active across packet boundaries until the final fragment.
    bool packets(const Reader::Object& object) {
        QByteArray data;
        if (!reader.bytes(object.data, 26, object.end, &data) || data.left(16) != fileId
            || qFromLittleEndian<quint64>(data.data() + 16) != packetCount
            || quint64(object.end - object.data - 26) != packetCount * packetSize) return false;
        VDQtAsfFrameTiming active;
        quint32 used = 0, activeNumber = 0, activeNumberType = 0;
        for (quint64 packet = 0; packet < packetCount; ++packet) {
            const qint64 start = object.data + 26 + qint64(packet * packetSize);
            const qint64 end = start + packetSize;
            qint64 at = start;
            quint32 flags, property, length, padding, ignored;
            if (!reader.uint(at, 1, end, &flags)) return false;
            if (flags & 0x80) {
                if (flags != 0x82 || !reader.uint(at, 2, end, &ignored) || ignored
                    || !reader.uint(at, 1, end, &flags)) return false;
            }
            if (!reader.uint(at, 1, end, &property)
                || !reader.sized(at, flags >> 5, end, &length, packetSize)
                || !reader.sized(at, flags >> 1, end, &ignored)
                || !reader.sized(at, flags >> 3, end, &padding)
                || !length || length > packetSize || padding >= length
                || !reader.uint(at, 4, end, &ignored) || !reader.uint(at, 2, end, &ignored)) return false;
            const qint64 contentEnd = start + length - padding;
            quint32 payloadFlags = 0x81;
            if ((flags & 1) && !reader.uint(at, 1, contentEnd, &payloadFlags)) return false;
            const int count = flags & 1 ? payloadFlags & 63 : 1;
            if (!count || ((flags & 1) && !(payloadFlags >> 6))) return false;
            for (int payload = 0; payload < count; ++payload) {
                quint32 stream, number, offset, replicated, size;
                if (!reader.uint(at, 1, contentEnd, &stream)
                    || !reader.sized(at, property >> 4, contentEnd, &number)
                    || !reader.sized(at, property >> 2, contentEnd, &offset)
                    || !reader.sized(at, property, contentEnd, &replicated)
                    || replicated > quint64(contentEnd - at)) return false;
                const qint64 replicationAt = at;
                at += replicated;
                if (flags & 1) {
                    if (!reader.sized(at, payloadFlags >> 6, contentEnd, &size)) return false;
                } else size = quint32(contentEnd - at);
                if (at > contentEnd || size > quint64(contentEnd - at)) return false;
                if ((stream & 127) == quint32(streamId)) {
                    // Compressed multiple-object payloads, unusual replication
                    // extensions and incomplete/restarted fragments are ambiguous.
                    if (!size || (replicated && replicated != quint32(replicatedSize))) return false;
                    quint32 objectSize = 0, timestamp = 0;
                    qint64 position = replicationAt;
                    if (replicated && (!reader.uint(position, 4, at, &objectSize)
                        || !reader.uint(position, 4, at, &timestamp)
                        || !objectSize || objectSize >= (1 << 24) || timestamp > INT_MAX)) return false;
                    if (!offset) {
                        if (active.size || !replicated || frames.size() >= maxFrames) return false;
                        active = {timestamp, start, objectSize, bool(stream & 128)};
                        activeNumber = number; activeNumberType = (property >> 4) & 3; used = 0;
                    }
                    if (!active.size || number != activeNumber || ((property >> 4) & 3) != activeNumberType
                        || used != offset || size > active.size - used
                        || (replicated && (objectSize != active.size || timestamp != active.timestamp))) return false;
                    used += size;
                    if (used == active.size) {
                        if ((!frames.isEmpty() && active.timestamp <= frames.last().timestamp)
                            || (frames.isEmpty() && !active.keyFrame)) return false;
                        frames.append(active);
                        active = {};
                    }
                }
                at += size; // Skip compressed bytes, including all other tracks.
            }
            if (at != contentEnd) return false;
        }
        return !active.size && !frames.isEmpty();
    }
    bool read() {
        Reader::Object header;
        QByteArray data;
        const qint64 fileSize = reader.file.size();
        if (!reader.object(0, fileSize, &header) || header.guid != headerGuid
            || !reader.bytes(header.data, 6, header.end, &data)
            || uchar(data.at(4)) != 1 || uchar(data.at(5)) != 2
            || !headers(header.data + 6, header.end, 0, qFromLittleEndian<quint32>(data.data()))
            || !haveFile || !haveStream) return false;
        bool haveData = false;
        for (qint64 at = header.end; at < fileSize;) {
            Reader::Object object;
            if (++objects > 1024 * 1024 || !reader.object(at, fileSize, &object)) return false;
            if (object.guid == dataGuid) {
                if (haveData || !packets(object)) return false;
                haveData = true;
            }
            at = object.end;
        }
        return haveData;
    }
};
} // namespace

bool VDQtReadAsfTiming(const QString& path, int streamId, QVector<VDQtAsfFrameTiming> *output) {
    if (!output || streamId <= 0 || streamId > 127) return false;
    MapReader map;
    map.streamId = streamId;
    map.reader.file.setFileName(path);
    if (!map.reader.file.open(QIODevice::ReadOnly | QIODevice::Unbuffered) || !map.read()) return false;
    *output = std::move(map.frames);
    return true;
}
