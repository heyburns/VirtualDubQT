#include "VDQtAviTiming.h"
#include <QFile>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace {
constexpr int kMaxSamples = 4 * 1024 * 1024;
constexpr quint64 kMaxHeaderBytes = 256 * 1024 * 1024;
struct CodecCloser {
    void operator()(AVCodecContext *context) const { avcodec_free_context(&context); }
};

bool framePerChunk(AVCodecID codec) {
    switch (codec) {
    case AV_CODEC_ID_MPEG4: case AV_CODEC_ID_MJPEG: case AV_CODEC_ID_RAWVIDEO:
    case AV_CODEC_ID_FFV1: case AV_CODEC_ID_HUFFYUV:
        return true;
    default: return false;
    }
}

bool packedDivX(const QByteArray& prefix) {
    static const QByteArray userCode = QByteArray::fromHex("000001b2");
    static const QByteArray startCode = QByteArray::fromHex("000001");
    for (int at = prefix.indexOf(userCode); at >= 0;) {
        const int next = prefix.indexOf(startCode, at + 4);
        const QByteArray user = prefix.mid(at + 4, (next < 0 ? prefix.size() : next) - at - 4);
        if (user.startsWith("DivX") && user.endsWith('p')) return true;
        at = prefix.indexOf(userCode, at + 4);
    }
    return false;
}

int vopOffset(const QByteArray& prefix) {
    // A supported chunk contains one coded VOP. Packed DivX/Xvid packets and
    // header-only/no-picture chunks need decoded accounting instead.
    static const QByteArray vopCode = QByteArray::fromHex("000001b6");
    const int first = prefix.indexOf(vopCode);
    if (first < 0 || prefix.indexOf(vopCode, first + 4) >= 0 || packedDivX(prefix)) return -1;
    return first;
}

bool codedVop(const QByteArray& prefix, int start, int resolution, int pictureType) {
    // Header bits follow ISO MPEG-4's VOP type, modulo time base, marker,
    // time increment, marker, vop_coded. The parser supplies the VOL clock.
    // An NVOP can be a packed-B-frame filler; it is never a new picture here.
    int cursor = (start + 4) * 8;
    const auto take = [&](int count, unsigned *value) {
        if (count < 0 || count > 31 || cursor > prefix.size() * 8 - count) return false;
        unsigned result = 0;
        while (count--) {
            result = (result << 1) | ((uchar(prefix.at(cursor / 8)) >> (7 - cursor % 8)) & 1);
            ++cursor;
        }
        *value = result;
        return true;
    };
    unsigned value;
    if (resolution <= 0 || !take(2, &value) || int(value) + AV_PICTURE_TYPE_I != pictureType) return false;
    do { if (!take(1, &value)) return false; } while (value);
    if (!take(1, &value) || value != 1) return false;
    int bits = 1;
    for (unsigned maximum = unsigned(resolution - 1); maximum >>= 1;) ++bits;
    return take(bits, &value) && value < unsigned(resolution)
        && take(1, &value) && value == 1 && take(1, &value) && value == 1;
}

// Native index and declared count can both be stale. Check every movie chunk
// header, including OpenDML AVIX segments and record lists, without touching
// picture/audio payload. This prevents a truncated index hiding valid tail video.
class MovieHeaders {
public:
    QFile& file;
    AVStream *stream;
    int count;
    int matched = 0;
    quint64 bytesRead = 0;
    quint64 headers = 0;

    bool read(qint64 at, char *data, int size) {
        bytesRead += quint64(size);
        return bytesRead <= kMaxHeaderBytes && (file.pos() == at || file.seek(at))
            && file.read(data, size) == size;
    }
    bool selected(const char *id) const {
        return id[0] == '0' + stream->index / 10 && id[1] == '0' + stream->index % 10;
    }
    bool children(qint64 at, qint64 end, bool movie, int depth = 0) {
        if (depth > 16) return false;
        while (at < end) {
            char header[8];
            if (++headers > 32 * 1024 * 1024 || end - at < 8 || !read(at, header, 8)) return false;
            const quint32 length = qFromLittleEndian<quint32>(reinterpret_cast<uchar*>(header + 4));
            const qint64 data = at + 8;
            if (length > quint64(end - data)) return false;
            const qint64 next = data + length;
            if ((length & 1) && next == end) return false;
            if (std::memcmp(header, "LIST", 4) == 0) {
                char type[4];
                if (length < 4 || !read(data, type, 4)) return false;
                if ((!movie && std::memcmp(type, "movi", 4) == 0)
                    || (movie && std::memcmp(type, "rec ", 4) == 0)) {
                    if (!children(data + 4, next, true, depth + 1)) return false;
                } else if (movie) return false;
            } else if (movie && selected(header)) {
                if (header[2] != 'd' || (header[3] != 'b' && header[3] != 'c') || matched >= count) return false;
                const auto *entry = avformat_index_get_entry(stream, matched++);
                if (entry->pos != at || entry->size != int64_t(length)) return false;
            }
            at = next + (length & 1);
        }
        return at == end;
    }
    bool complete() {
        const qint64 size = file.size();
        bool first = true;
        for (qint64 at = 0; at < size;) {
            char header[12];
            if (size - at < 12 || !read(at, header, 12) || std::memcmp(header, "RIFF", 4)) return false;
            const quint32 length = qFromLittleEndian<quint32>(reinterpret_cast<uchar*>(header + 4));
            if (length < 4 || length > quint64(size - at - 8)
                || std::memcmp(header + 8, first ? "AVI " : "AVIX", 4)) return false;
            const qint64 end = at + 8 + length;
            if ((length & 1) && end == size) return false;
            if (!children(at + 12, end, false)) return false;
            at = end + (length & 1);
            first = false;
        }
        return !first && matched == count;
    }
};
} // namespace

bool VDQtReadAviFrameOrder(const QString& path, AVStream *stream, VDQtAviFrameOrder *output) {
    if (!output || !stream || !stream->codecpar || !framePerChunk(stream->codecpar->codec_id)
        || stream->index < 0 || stream->index > 99 || stream->time_base.num <= 0
        || stream->time_base.den <= 0 || stream->codecpar->format == AV_PIX_FMT_PAL8) return false;
    const int count = avformat_index_get_entries_count(stream);
    // Do not promote a partial index or stale declared length to exactness.
    // Empty/dropped samples produce gaps and deliberately retain the fallback.
    if (count <= 0 || count > kMaxSamples || stream->nb_frames != count) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) return false;
    const qint64 size = file.size();
    MovieHeaders movie{file, stream, count};
    if (!movie.complete()) return false;
    const bool mpeg4 = stream->codecpar->codec_id == AV_CODEC_ID_MPEG4;
    std::unique_ptr<AVCodecContext, CodecCloser> context;
    std::unique_ptr<AVCodecParserContext, decltype(&av_parser_close)> parser(nullptr, av_parser_close);
    if (mpeg4) {
        if (stream->codecpar->extradata_size > 65536) return false;
        if (packedDivX(QByteArray::fromRawData(reinterpret_cast<const char*>(stream->codecpar->extradata),
                                             stream->codecpar->extradata_size))) return false;
        context.reset(avcodec_alloc_context3(nullptr));
        parser.reset(av_parser_init(AV_CODEC_ID_MPEG4));
        if (!context || !parser || avcodec_parameters_to_context(context.get(), stream->codecpar) < 0) return false;
        parser->flags = PARSER_FLAG_COMPLETE_FRAMES | PARSER_FLAG_USE_CODEC_TS;
    }
    struct TimedSample { int64_t timestamp; int sample; };
    QVector<TimedSample> timed;
    VDQtAviFrameOrder result;
    result.samples.reserve(count);
    if (mpeg4) timed.reserve(count);
    int pendingReference = -1, references = 0;
    qint64 previousEnd = 0;
    quint64 readBytes = movie.bytesRead;
    std::array<char, 512 + AV_INPUT_BUFFER_PADDING_SIZE> buffer{};
    for (int sample = 0; sample < count; ++sample) {
        const auto *entry = avformat_index_get_entry(stream, sample);
        if (!entry || entry->timestamp != sample || entry->pos < previousEnd || entry->size <= 0
            || entry->pos > size - 8 || entry->size > size - entry->pos - 8
            || (entry->flags & AVINDEX_DISCARD_FRAME) || (sample == 0 && !(entry->flags & AVINDEX_KEYFRAME))) return false;
        previousEnd = entry->pos + 8 + entry->size;
        if (!mpeg4) { result.samples.append(sample); continue; }
        if (!file.seek(entry->pos + 8)) return false;
        int length = std::min(32, entry->size);
        if (file.read(buffer.data(), length) != length) return false;
        readBytes += length;
        QByteArray prefix = QByteArray::fromRawData(buffer.data(), length);
        if (vopOffset(prefix) < 0 && length < std::min(512, entry->size)) {
            const int extended = std::min(512, entry->size);
            if (file.read(buffer.data() + length, extended - length) != extended - length) return false;
            readBytes += extended - length;
            length = extended;
            prefix = QByteArray::fromRawData(buffer.data(), length);
        }
        if (readBytes > kMaxHeaderBytes) return false;
        const int vop = vopOffset(prefix);
        if (vop < 0) return false;
        std::fill(buffer.begin() + length, buffer.end(), 0);
        parser->pts = AV_NOPTS_VALUE;
        parser->pict_type = AV_PICTURE_TYPE_NONE;
        uint8_t *parsed = nullptr;
        int parsedSize = 0;
        const int consumed = av_parser_parse2(parser.get(), context.get(), &parsed, &parsedSize,
            reinterpret_cast<const uint8_t*>(buffer.data()), length, AV_NOPTS_VALUE, AV_NOPTS_VALUE, entry->pos + 8);
        const int type = parser->pict_type;
        if (consumed != length || parsedSize != length || parser->pts == AV_NOPTS_VALUE
            || (type != AV_PICTURE_TYPE_I && type != AV_PICTURE_TYPE_P && type != AV_PICTURE_TYPE_B)
            || !codedVop(prefix, vop, context->framerate.num, type)
            || ((entry->flags & AVINDEX_KEYFRAME) != 0) != (type == AV_PICTURE_TYPE_I)) return false;
        result.hasBFrames |= type == AV_PICTURE_TYPE_B;
        timed.append({parser->pts, sample});
        if (type == AV_PICTURE_TYPE_B) {
            if (references < 2) return false;
            result.samples.append(sample);
        } else {
            if (pendingReference >= 0) result.samples.append(pendingReference);
            pendingReference = sample;
            ++references;
        }
    }
    if (mpeg4) {
        if (pendingReference < 0) return false;
        result.samples.append(pendingReference);
        // Only relative ordering is used. The parser's codec-clock units and
        // AVI's declared frame clock need not match (or have identical rounding).
        std::sort(timed.begin(), timed.end(), [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
        if (timed.first().sample != 0) return false;
        for (int frame = 0; frame < count; ++frame) {
            // Reference/B-picture emission order must independently agree with
            // the codec clock. Bad/reset clocks cannot reorder ordinary P video.
            if ((frame && timed.at(frame).timestamp == timed.at(frame - 1).timestamp)
                || timed.at(frame).sample != result.samples.at(frame)) return false;
        }
    }
    *output = std::move(result);
    return true;
}
