#include "VDQtMp4Timing.h"

#include <QFile>
#include <QtEndian>
#include <climits>
#include <utility>

namespace {
constexpr quint32 tag(char a, char b, char c, char d) {
    return (quint32(a) << 24) | (quint32(b) << 16) | (quint32(c) << 8) | quint32(d);
}

struct Box {
    qint64 data = 0;
    qint64 end = 0;
    quint32 type = 0;
};

// Walk box headers with explicit parent bounds. Large mdat/free boxes are
// skipped with seek(), so work depends on metadata size, not movie duration or
// compressed data size. The header budget also bounds malicious nesting lists.
class Reader {
public:
    QFile file;
    int headers = 0;

    bool bytes(qint64 at, char *out, qint64 count, qint64 end) {
        return at >= 0 && count >= 0 && at <= end && count <= end - at
            && file.seek(at) && file.read(out, count) == count;
    }
    bool u32(qint64 at, quint32 *value, qint64 end) {
        uchar data[4];
        if (!bytes(at, reinterpret_cast<char*>(data), 4, end)) return false;
        *value = qFromBigEndian<quint32>(data);
        return true;
    }
    QByteArray table(const Box& box) {
        if (box.end - box.data < 8 || !file.seek(box.data + 8)) return {};
        // All table callers validate the run count/size before allocating.
        return file.read(box.end - box.data - 8);
    }
    bool box(qint64 at, qint64 end, Box *out) {
        uchar data[16];
        if (++headers > 100000 || !bytes(at, reinterpret_cast<char*>(data), 8, end)) return false;
        quint64 size = qFromBigEndian<quint32>(data);
        qint64 header = 8;
        if (size == 1) {
            if (!bytes(at + 8, reinterpret_cast<char*>(data + 8), 8, end)) return false;
            size = qFromBigEndian<quint64>(data + 8);
            header = 16;
        } else if (size == 0) {
            size = static_cast<quint64>(end - at);
        }
        if (size < static_cast<quint64>(header) || size > static_cast<quint64>(end - at)) return false;
        *out = {at + header, at + static_cast<qint64>(size), qFromBigEndian<quint32>(data + 4)};
        return true;
    }
    bool child(const Box& parent, quint32 type, Box *out, bool *found) {
        *found = false;
        for (qint64 at = parent.data; at < parent.end;) {
            Box item;
            if (!box(at, parent.end, &item)) return false;
            if (item.type == type) {
                if (*found) return false; // Duplicate structural boxes are ambiguous.
                *out = item;
                *found = true;
            }
            at = item.end;
        }
        return true;
    }
    bool required(const Box& parent, quint32 type, Box *out) {
        bool found;
        return child(parent, type, out, &found) && found;
    }
};

bool readTrack(Reader& reader, const Box& track, int trackId, int timeScale,
               int samples, QVector<qint64> *offsets, bool *matches) {
    *matches = false;
    Box header;
    quint32 version, id;
    if (!reader.required(track, tag('t','k','h','d'), &header)
        || !reader.u32(header.data, &version, header.end) || (version >> 24) > 1
        || !reader.u32(header.data + ((version >> 24) ? 20 : 12), &id, header.end)) return false;
    if (id != static_cast<quint32>(trackId)) return true;
    *matches = true;

    // FFmpeg can expand/reorder sample tables for complex edit lists. A simple
    // leading empty edit followed by one normal-rate media edit is an affine
    // timing shift; anything more stays on the verified-decoding path.
    Box edits, list;
    bool found;
    if (!reader.child(track, tag('e','d','t','s'), &edits, &found)) return false;
    if (found) {
        quint32 count;
        if (!reader.required(edits, tag('e','l','s','t'), &list)
            || !reader.u32(list.data, &version, list.end) || (version >> 24) > 1
            || !reader.u32(list.data + 4, &count, list.end) || count > 2) return false;
        const qint64 stride = (version >> 24) ? 20 : 12;
        if (list.end - list.data - 8 != stride * count) return false;
        for (quint32 edit = 0; edit < count; ++edit) {
            const qint64 at = list.data + 8 + edit * stride;
            quint32 rate;
            if (!reader.u32(at + stride - 4, &rate, list.end) || rate != 0x00010000) return false;
            if (count == 2 && edit == 0) {
                char mediaTime[8];
                const int width = (version >> 24) ? 8 : 4;
                if (!reader.bytes(at + width, mediaTime, width, list.end)) return false;
                for (int byte = 0; byte < width; ++byte)
                    if (static_cast<uchar>(mediaTime[byte]) != 0xff) return false;
            }
        }
    }

    Box media, info, table, clock, sizes, durations, composition;
    quint32 scale, count;
    if (!reader.required(track, tag('m','d','i','a'), &media)
        || !reader.required(media, tag('m','d','h','d'), &clock)
        || !reader.u32(clock.data, &version, clock.end) || (version >> 24) > 1
        || !reader.u32(clock.data + ((version >> 24) ? 20 : 12), &scale, clock.end)
        || scale != static_cast<quint32>(timeScale)
        || !reader.required(media, tag('m','i','n','f'), &info)
        || !reader.required(info, tag('s','t','b','l'), &table)) return false;

    if (!reader.child(table, tag('s','t','s','z'), &sizes, &found)) return false;
    if (!found && !reader.required(table, tag('s','t','z','2'), &sizes)) return false;
    if (!reader.u32(sizes.data + 8, &count, sizes.end) || count != static_cast<quint32>(samples)) return false;

    if (!reader.required(table, tag('s','t','t','s'), &durations)
        || !reader.u32(durations.data + 4, &count, durations.end)
        || count > static_cast<quint32>(samples)
        || durations.end - durations.data - 8 != qint64(count) * 8) return false;
    quint64 total = 0;
    const QByteArray durationRuns = reader.table(durations);
    if (durationRuns.size() != qint64(count) * 8) return false;
    for (quint32 run = 0; run < count; ++run) {
        const auto *data = reinterpret_cast<const uchar*>(durationRuns.constData() + qint64(run) * 8);
        const quint32 length = qFromBigEndian<quint32>(data);
        const quint32 duration = qFromBigEndian<quint32>(data + 4);
        if (length == 0 || duration == 0 || duration > INT_MAX) return false;
        total += length;
        if (total > static_cast<quint64>(samples)) return false;
    }
    if (total != static_cast<quint64>(samples)) return false;

    if (!reader.child(table, tag('c','t','t','s'), &composition, &found)) return false;
    offsets->fill(0, samples);
    if (!found) return true;
    if (!reader.u32(composition.data, &version, composition.end) || (version >> 24) > 1
        || !reader.u32(composition.data + 4, &count, composition.end)
        || count > static_cast<quint32>(samples)
        || composition.end - composition.data - 8 != qint64(count) * 8) return false;
    int sample = 0;
    const QByteArray compositionRuns = reader.table(composition);
    if (compositionRuns.size() != qint64(count) * 8) return false;
    for (quint32 run = 0; run < count; ++run) {
        const auto *data = reinterpret_cast<const uchar*>(compositionRuns.constData() + qint64(run) * 8);
        const quint32 length = qFromBigEndian<quint32>(data);
        const quint32 rawOffset = qFromBigEndian<quint32>(data + 4);
        if (length == 0 || length > static_cast<quint32>(samples - sample)) return false;
        // Match the bundled MOV demuxer's signed CTTS interpretation, including
        // old files which incorrectly store negative offsets in version zero.
        const qint64 offset = rawOffset <= INT_MAX ? qint64(rawOffset)
            : qint64(rawOffset) - (qint64(1) << 32);
        if (offset <= -(qint64(1) << 28) || offset >= (qint64(1) << 28)) return false;
        for (quint32 entry = 0; entry < length; ++entry) (*offsets)[sample++] = offset;
    }
    return sample == samples;
}
} // namespace

bool VDQtReadMp4CompositionOffsets(const QString& path, int trackId, int timeScale,
                                  int sampleCount, QVector<qint64> *offsets) {
    // Bound auxiliary navigation storage independently of decoded-frame caches.
    // Very large or unconventional tables retain the ordinary safe fallback.
    if (!offsets || trackId <= 0 || timeScale <= 0 || sampleCount <= 0
        || sampleCount > 4 * 1024 * 1024) return false;
    Reader reader;
    reader.file.setFileName(path);
    if (!reader.file.open(QIODevice::ReadOnly)) return false;
    const Box root{0, reader.file.size(), 0};
    Box movie;
    bool found;
    if (!reader.child(root, tag('m','o','o','v'), &movie, &found) || !found) return false;
    Box fragments;
    if (!reader.child(movie, tag('m','v','e','x'), &fragments, &found) || found
        || !reader.child(root, tag('m','o','o','f'), &fragments, &found) || found) return false;
    bool matched = false;
    QVector<qint64> result;
    for (qint64 at = movie.data; at < movie.end;) {
        Box item;
        if (!reader.box(at, movie.end, &item)) return false;
        if (item.type == tag('t','r','a','k')) {
            bool matches;
            if (!readTrack(reader, item, trackId, timeScale, sampleCount, &result, &matches)) return false;
            if (matches && matched) return false;
            matched = matched || matches;
        }
        at = item.end;
    }
    if (!matched) return false;
    *offsets = std::move(result);
    return true;
}
