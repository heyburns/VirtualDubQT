#include "VDQtMatroskaTiming.h"

#include <QFile>
#include <QtEndian>
#include <climits>
#include <cstring>
#include <utility>

namespace {
constexpr quint32 kSegment = 0x18538067;
constexpr quint32 kInfo = 0x1549a966;
constexpr quint32 kTracks = 0x1654ae6b;
constexpr quint32 kCluster = 0x1f43b675;
constexpr quint32 kSimpleBlock = 0xa3;
constexpr quint32 kBlock = 0xa1;
constexpr quint32 kBlockGroup = 0xa0;
constexpr int kMaxFrames = 4 * 1024 * 1024;

struct Element {
    quint32 id = 0;
    qint64 start = 0;
    qint64 data = 0;
    qint64 end = 0;
    bool unknownSize = false;
};

bool segmentLevel(quint32 id) {
    return id == kCluster || id == kInfo || id == kTracks || id == 0x114d9b74
        || id == 0x1c53bb6b || id == 0x1941a469 || id == 0x1043a770 || id == 0x1254c367;
}

// A bounded EBML header reader, not a replacement video demuxer. In particular,
// a large binary element is skipped by its size, never read into a QByteArray.
// Matroska structure: https://www.matroska.org/technical/elements.html
class Reader {
public:
    QFile file;
    quint64 bytesRead = 0;
    quint64 headers = 0;

    bool bytes(qint64 at, void *out, qint64 count, qint64 end) {
        if (at < 0 || count < 0 || at > end || count > end - at
            || quint64(count) > 256 * 1024 * 1024
            || bytesRead > 256 * 1024 * 1024 - quint64(count)
            || (file.pos() != at && !file.seek(at))) return false;
        const qint64 got = file.read(static_cast<char*>(out), count);
        if (got > 0) bytesRead += quint64(got);
        return got == count;
    }
    bool vint(qint64 at, qint64 end, quint64 *value, int *length, bool id = false) {
        uchar first;
        if (!bytes(at, &first, 1, end) || first == 0) return false;
        int count = 1;
        uchar marker = 0x80;
        while (!(first & marker)) { marker >>= 1; ++count; }
        if (count > (id ? 4 : 8)) return false;
        uchar tail[7];
        if (!bytes(at + 1, tail, count - 1, end)) return false;
        quint64 result = id ? first : (first & (marker - 1));
        for (int byte = 0; byte < count - 1; ++byte) result = (result << 8) | tail[byte];
        *value = result;
        *length = count;
        return true;
    }
    bool element(qint64 at, qint64 end, Element *element) {
        quint64 id, size;
        int idLength, sizeLength;
        if (++headers > 32 * 1024 * 1024
            || !vint(at, end, &id, &idLength, true)
            || !vint(at + idLength, end, &size, &sizeLength)) return false;
        const qint64 data = at + idLength + sizeLength;
        const bool unknown = size == ((quint64(1) << (7 * sizeLength)) - 1);
        if (!unknown && size > quint64(end - data)) return false;
        *element = {quint32(id), at, data,
                    unknown ? end : data + qint64(size), unknown};
        return true;
    }
    bool uint(const Element& element, quint64 *value) {
        const qint64 size = element.end - element.data;
        uchar data[8];
        if (element.unknownSize || size > 8 || !bytes(element.data, data, size, element.end)) return false;
        quint64 result = 0;
        for (int byte = 0; byte < size; ++byte) result = (result << 8) | data[byte];
        *value = result;
        return true;
    }
    bool isOne(const Element& element) {
        uchar bytes[8];
        const qint64 size = element.end - element.data;
        if (element.unknownSize || (size != 4 && size != 8)
            || !this->bytes(element.data, bytes, size, element.end)) return false;
        if (size == 4) {
            const quint32 bits = qFromBigEndian<quint32>(bytes);
            float value;
            std::memcpy(&value, &bits, sizeof(value));
            return value == 1.0f;
        }
        const quint64 bits = qFromBigEndian<quint64>(bytes);
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return value == 1.0;
    }
};

class MapReader {
public:
    Reader reader;
    VDQtMatroskaTiming result;
    quint64 trackNumber = 0;
    quint64 scale = 1000000;
    quint64 defaultDuration = 0;
    qint64 referencePosition = -1;
    QByteArray expectedCodec;
    bool haveTrack = false;
    bool haveInfo = false;
    bool haveReference = false;
    int timeBaseNumerator = 0;
    int timeBaseDenominator = 0;

    bool info(const Element& parent) {
        if (haveInfo || parent.unknownSize) return false;
        haveInfo = true;
        bool haveScale = false;
        for (qint64 at = parent.data; at < parent.end;) {
            Element element;
            if (!reader.element(at, parent.end, &element) || element.unknownSize) return false;
            if (element.id == 0x2ad7b1) {
                if (haveScale || !reader.uint(element, &scale)) return false;
                haveScale = true;
            }
            at = element.end;
        }
        return scale > 0 && scale <= UINT_MAX
            && scale * quint64(timeBaseDenominator) == quint64(timeBaseNumerator) * 1000000000;
    }

    bool track(const Element& parent) {
        quint64 number = 0, type = 0, duration = 0;
        bool nativeScale = true, encoded = false;
        QByteArray codec;
        bool haveNumber = false, haveType = false, haveCodec = false;
        bool haveDuration = false, haveScale = false;
        for (qint64 at = parent.data; at < parent.end;) {
            Element element;
            if (!reader.element(at, parent.end, &element) || element.unknownSize) return false;
            if (element.id == 0xd7) {
                if (haveNumber || !reader.uint(element, &number)) return false;
                haveNumber = true;
            } else if (element.id == 0x83) {
                if (haveType || !reader.uint(element, &type)) return false;
                haveType = true;
            } else if (element.id == 0x86) {
                const qint64 size = element.end - element.data;
                if (haveCodec || size > 128) return false;
                codec.resize(size);
                if (!reader.bytes(element.data, codec.data(), size, element.end)) return false;
                haveCodec = true;
            } else if (element.id == 0x23e383) {
                if (haveDuration || !reader.uint(element, &duration)
                    || duration > quint64(INT64_MAX) / 256) return false;
                haveDuration = true;
            } else if (element.id == 0x23314f) {
                if (haveScale) return false;
                haveScale = true;
                nativeScale = reader.isOne(element);
            } else if (element.id == 0x6d80 || element.id == 0x537f) {
                // Encoded track data and deprecated timestamp offsets retain the
                // verified-decoding fallback rather than being reinterpreted.
                encoded = true;
            }
            at = element.end;
        }
        if (number != trackNumber) return true;
        if (haveTrack || !haveNumber || !haveType || type != 1 || codec != expectedCodec
            || !nativeScale || encoded) return false;
        haveTrack = true;
        defaultDuration = duration;
        return true;
    }

    bool tracks(const Element& parent) {
        if (parent.unknownSize) return false;
        int count = 0;
        for (qint64 at = parent.data; at < parent.end;) {
            Element element;
            if (!reader.element(at, parent.end, &element) || element.unknownSize) return false;
            if (element.id == 0xae && (++count > 256 || !track(element))) return false;
            at = element.end;
        }
        return true;
    }

    bool block(const Element& element, qint64 clusterTime, qint64 clusterPosition,
               bool groupKey, bool simple, quint64 duration, bool unsupported) {
        quint64 number;
        int length;
        if (!reader.vint(element.data, element.end, &number, &length)) return false;
        if (number != trackNumber) return true;
        uchar data[3];
        qint64 cursor = element.data + length;
        if (!haveTrack || !haveInfo || clusterTime < 0 || unsupported
            || !reader.bytes(cursor, data, 3, element.end)) return false;
        cursor += 3;
        const quint16 raw = qFromBigEndian<quint16>(data);
        const qint64 relative = raw <= INT16_MAX ? qint64(raw) : qint64(raw) - 65536;
        if (clusterTime < -relative || (relative > 0 && clusterTime > INT64_MAX - relative)) return false;
        const qint64 timestamp = clusterTime + relative;
        const bool key = simple ? (data[2] & 0x80) != 0 : groupKey;
        const int lacing = (data[2] & 6) >> 1;
        if (data[2] & 8) return false; // Invisible pictures need codec-specific accounting.
        int count = 1;
        if (lacing) {
            uchar laceCount;
            if (!reader.bytes(cursor++, &laceCount, 1, element.end)) return false;
            count += laceCount;
            if (count == 1) return false; // Degenerate lacing is handled by FFmpeg's fallback.
        }
        // Validate lace sizes without reading any compressed frame. A finite
        // lace-header budget also prevents a malicious Xiph-size run consuming
        // CPU proportional to a claimed gigabyte-sized picture.
        const qint64 laceStart = cursor;
        quint64 totalSizes = 0;
        qint64 previousSize = 0;
        for (int lace = 0; lace < count - 1 && lacing != 2; ++lace) {
            qint64 size = 0;
            if (lacing == 1) {
                uchar part;
                do {
                    if (cursor - laceStart >= 65536 || !reader.bytes(cursor++, &part, 1, element.end)) return false;
                    size += part;
                } while (part == 255);
            } else {
                quint64 value;
                int bytes;
                if (!reader.vint(cursor, element.end, &value, &bytes)) return false;
                cursor += bytes;
                if (lace == 0) size = qint64(value);
                else size = previousSize + qint64(value) - qint64((quint64(1) << (7 * bytes - 1)) - 1);
            }
            if (size <= 0 || size > INT_MAX) return false;
            totalSizes += quint64(size);
            previousSize = size;
        }
        const quint64 payload = quint64(element.end - cursor);
        if (lacing == 2) {
            if (payload == 0 || payload % count || payload / count > INT_MAX) return false;
        } else if (totalSizes >= payload || payload - totalSizes > INT_MAX) return false;
        if (!duration && defaultDuration) duration = defaultDuration * count / scale;
        if (duration > INT64_MAX || (count > 1 && duration == 0)
            || result.frames.size() > kMaxFrames - count) return false;
        if (element.data == referencePosition) {
            if (haveReference) return false;
            haveReference = true;
            result.referenceTimestamp = timestamp;
        }
        const auto offset = [duration, count](int frame) {
            return duration / count * frame + (duration % count) * frame / count;
        };
        for (int lace = 0; lace < count; ++lace) {
            const quint64 advance = offset(lace);
            if (advance > quint64(INT64_MAX - timestamp)) return false;
            const bool laceKey = key && lace == 0;
            result.frames.append({timestamp + qint64(advance), qint64(offset(lace + 1) - advance),
                                  clusterPosition, laceKey});
        }
        return true;
    }

    bool group(const Element& parent, qint64 clusterTime, qint64 clusterPosition) {
        Element picture;
        bool havePicture = false, haveDuration = false, key = true, unsupported = false;
        quint64 duration = 0;
        for (qint64 at = parent.data; at < parent.end;) {
            Element element;
            if (!reader.element(at, parent.end, &element) || element.unknownSize) return false;
            if (element.id == kBlock) {
                if (havePicture) return false;
                picture = element;
                havePicture = true;
            } else if (element.id == 0xfb) key = false;
            else if (element.id == 0x9b) {
                if (haveDuration || !reader.uint(element, &duration)) return false;
                haveDuration = true;
            } else if (element.id == 0xa4 || element.id == 0x75a2) unsupported = true;
            at = element.end;
        }
        return havePicture && block(picture, clusterTime, clusterPosition, key, false, duration, unsupported);
    }

    bool cluster(const Element& parent, qint64 *next) {
        qint64 time = -1;
        for (qint64 at = parent.data; at < parent.end;) {
            Element element;
            if (!reader.element(at, parent.end, &element)) return false;
            if (segmentLevel(element.id)) {
                if (!parent.unknownSize) return false;
                *next = at;
                return true;
            }
            if (element.unknownSize) return false;
            if (element.id == 0xe7) {
                quint64 value;
                if (time >= 0 || !reader.uint(element, &value) || value > INT64_MAX) return false;
                time = qint64(value);
            } else if (element.id == kSimpleBlock) {
                if (!block(element, time, parent.start, false, true, 0, false)) return false;
            } else if (element.id == kBlockGroup && !group(element, time, parent.start)) return false;
            at = element.end;
        }
        *next = parent.end;
        return true;
    }

    bool segment(const Element& parent) {
        for (qint64 at = parent.data; at < parent.end;) {
            Element element;
            if (!reader.element(at, parent.end, &element)) return false;
            qint64 next = element.end;
            if (element.id == kCluster) {
                if (!cluster(element, &next)) return false;
            } else if (element.unknownSize || element.id == kSegment || element.id == 0x1a45dfa3) return false;
            else if (element.id == kInfo && !info(element)) return false;
            else if (element.id == kTracks && !tracks(element)) return false;
            if (next <= at) return false;
            at = next;
        }
        return haveInfo && haveTrack && haveReference && !result.frames.isEmpty();
    }
};
} // namespace

bool VDQtReadMatroskaTiming(const QString& path, qint64 packetPosition,
                           const QByteArray& codecId, int timeBaseNumerator,
                           int timeBaseDenominator, VDQtMatroskaTiming *timing) {
    if (!timing || packetPosition < 0 || timeBaseNumerator <= 0 || timeBaseDenominator <= 0) return false;
    MapReader parser;
    parser.reader.file.setFileName(path);
    // QFile's default read-ahead would fetch compressed payload after every
    // tiny header read, turning sparse metadata traversal into near-full I/O.
    if (!parser.reader.file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) return false;
    const qint64 end = parser.reader.file.size();
    int trackBytes;
    if (!parser.reader.vint(packetPosition, end, &parser.trackNumber, &trackBytes)
        || parser.trackNumber == 0) return false;
    parser.referencePosition = packetPosition;
    parser.expectedCodec = codecId;
    parser.timeBaseNumerator = timeBaseNumerator;
    parser.timeBaseDenominator = timeBaseDenominator;
    bool haveSegment = false;
    for (qint64 at = 0; at < end;) {
        Element element;
        if (!parser.reader.element(at, end, &element)) return false;
        if (element.id == kSegment) {
            if (haveSegment || !parser.segment(element)) return false;
            haveSegment = true;
        } else if (element.unknownSize) return false;
        at = element.end;
    }
    if (!haveSegment) return false;
    parser.result.headerBytesRead = parser.reader.bytesRead;
    *timing = std::move(parser.result);
    return true;
}
