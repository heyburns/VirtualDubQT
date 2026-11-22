#include "VDQtFlvTiming.h"

#include <QFile>
#include <QtEndian>
#include <algorithm>
#include <utility>

namespace {
quint32 uint24(const uchar *data) {
    return (quint32(data[0]) << 16) | (quint32(data[1]) << 8) | data[2];
}

struct Bits {
    const uchar *data;
    int end;
    int position = 0;
    bool read(int count, unsigned *value) {
        if (count < 0 || count > 32 || count > end - position) return false;
        unsigned result = 0;
        while (count--) {
            result = (result << 1) | ((data[position / 8] >> (7 - position % 8)) & 1);
            ++position;
        }
        *value = result;
        return true;
    }
};

bool picture(const uchar *data, int size, int width, int height, unsigned *type) {
    Bits bits{data, size * 8};
    unsigned start, version, ignored, format, w, h, quantizer;
    if (!bits.read(17, &start) || start != 1 || !bits.read(5, &version) || version > 1
        || !bits.read(8, &ignored) || !bits.read(3, &format)) return false;
    if (format <= 1) {
        if (!bits.read(format ? 16 : 8, &w) || !bits.read(format ? 16 : 8, &h)) return false;
    } else {
        constexpr unsigned widths[] = {352, 176, 128, 320, 160};
        constexpr unsigned heights[] = {288, 144, 96, 240, 120};
        if (format > 6) return false;
        w = widths[format - 2]; h = heights[format - 2];
    }
    return w == unsigned(width) && h == unsigned(height) && bits.read(2, type)
        && bits.read(1, &ignored) && bits.read(5, &quantizer) && quantizer;
}
} // namespace

bool VDQtReadFlvTiming(const QString& path, int width, int height,
                       QVector<VDQtFlvFrameTiming> *output) {
    if (!output || width <= 0 || height <= 0) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) return false;
    const qint64 end = file.size();
    quint64 bytesRead = 0;
    const auto read = [&](qint64 at, uchar *data, qint64 count) {
        if (at < 0 || count < 0 || at > end || count > end - at
            || bytesRead + quint64(count) > 256 * 1024 * 1024
            || (file.pos() != at && !file.seek(at))) return false;
        const qint64 got = file.read(reinterpret_cast<char*>(data), count);
        if (got > 0) bytesRead += quint64(got);
        return got == count;
    };
    uchar header[15];
    if (!read(0, header, 9) || header[0] != 'F' || header[1] != 'L' || header[2] != 'V'
        || header[3] != 1 || !(header[4] & 1) || (header[4] & ~5)) return false;
    qint64 at = qFromBigEndian<quint32>(header + 5);
    if (at < 9) return false;
    QVector<VDQtFlvFrameTiming> frames;
    quint32 previousSize = 0;
    quint64 tags = 0;
    qint64 previousTimestamp = -1;
    int references = 0;
    // FLV stores each picture's clock beside its tag. A full compressed-data
    // scan or the optional (and sometimes stale) AMF frame-count tag is not needed.
    while (at < end) {
        if (!read(at, header, 4) || qFromBigEndian<quint32>(header) != previousSize) return false;
        at += 4;
        if (at == end) {
            if (frames.isEmpty()) return false;
            *output = std::move(frames);
            return true;
        }
        if (++tags > 16 * 1024 * 1024 || !read(at, header, 11)
            || uint24(header + 8) != 0) return false;
        const quint32 size = uint24(header + 1);
        const qint64 timestamp = uint24(header + 4) | (quint32(header[7]) << 24);
        if (size > quint64(end - at - 11)) return false;
        if (header[0] == 9) {
            uchar prefix[12];
            const int count = std::min<quint32>(size, sizeof(prefix));
            if (!read(at + 11, prefix, count) || count < 2 || (prefix[0] & 15) != 2) return false;
            const unsigned tagType = prefix[0] >> 4;
            unsigned type;
            if (!picture(prefix + 1, count - 1, width, height, &type)
                || !((tagType == 1 && type == 0) || (tagType == 2 && type == 1)
                     || (tagType == 3 && type >= 2))
                || timestamp <= previousTimestamp || (previousTimestamp < 0 && type != 0)) return false;
            previousTimestamp = timestamp;
            // FFmpeg's Spark decoder emits the initial I picture immediately,
            // but cannot display disposable P pictures until a second reference
            // picture has established last_picture. Counting every video tag
            // would shift all following ordinals (including the user's sample).
            if (type < 2 || references >= 2) {
                if (frames.size() >= 4 * 1024 * 1024) return false;
                frames.append({timestamp, at, type == 0});
            }
            if (type < 2) references = std::min(2, references + 1);
        } else if (header[0] != 8 && header[0] != 18) {
            // Includes encrypted/filter tags and enhanced/multi-track formats.
            return false;
        }
        previousSize = size + 11;
        at += 11 + size; // Never read compressed video/audio or AMF bodies.
    }
    return false; // A missing final PreviousTagSize is a truncated file.
}
