// AVI index acceleration must preserve displayed-frame identity, including
// reordered MPEG-4, and must reject incomplete indexes without hiding tail video.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtAviTiming.h"
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

bool recordAndAvixHeaders(VDQtTestFixtures& fixtures) {
    const auto chunk = [](const QByteArray& id, const QByteArray& data) {
        QByteArray size(4, '\0');
        qToLittleEndian<quint32>(data.size(), reinterpret_cast<uchar*>(size.data()));
        return id + size + data + (data.size() & 1 ? QByteArray(1, '\0') : QByteArray{});
    };
    const auto first = chunk("00db", "ODD");
    const auto second = chunk("00db", "NEXT");
    const QByteArray rec = chunk("RIFF", "AVI " + chunk("LIST", "movi" + chunk("LIST", "rec " + first + second)));
    const QByteArray avix = chunk("RIFF", "AVI " + chunk("LIST", "movi" + first))
        + chunk("RIFF", "AVIX" + chunk("LIST", "movi" + second));
    for (const auto& data : {rec, avix}) {
        const QString path = fixtures.directory.filePath("headers.avi");
        if (!fixtures.writeText(path, data)) return false;
        AVFormatContext *context = avformat_alloc_context();
        if (!context) return false;
        AVStream *stream = avformat_new_stream(context, nullptr);
        bool good = stream;
        if (stream) {
            stream->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
            stream->codecpar->format = AV_PIX_FMT_BGR0;
            stream->nb_frames = 2; stream->time_base = {1, 30};
            const int at0 = data.indexOf(first), at1 = data.indexOf(second);
            av_add_index_entry(stream, at0, 0, 3, 0, AVINDEX_KEYFRAME);
            av_add_index_entry(stream, at1, 1, 4, 0, AVINDEX_KEYFRAME);
            VDQtAviFrameOrder order;
            good = VDQtReadAviFrameOrder(path, stream, &order) && order.samples == QVector<int>{0, 1};
        }
        avformat_free_context(context);
        if (!check(good, "record lists, odd padding and multiple AVIX segments retain every picture")) return false;
    }
    return true;
}

bool navigation(VDQtTestFixtures& fixtures, const QString& name, const QString& rate, int bframes, bool audioFirst) {
    const auto input = fixtures.directory.filePath(name + ".avi");
    const auto oracle = fixtures.directory.filePath(name + ".framemd5");
    QStringList args;
    if (audioFirst) args << "-f" << "lavfi" << "-i" << "sine=sample_rate=48000:duration=110";
    args << "-f" << "lavfi" << "-i" << "testsrc2=size=96x64:rate=" + rate;
    if (audioFirst) args << "-map" << "0:a:0" << "-map" << "1:v:0" << "-c:a" << "pcm_s16le";
    args << "-frames:v" << "3000" << "-c:v" << "mpeg4" << "-bf" << QString::number(bframes)
         << "-g" << "30" << "-threads" << "1" << "-vtag" << "DX50";
    if (!audioFirst) args << "-an";
    if (!fixtures.ffmpeg(args + QStringList{input})
        || !fixtures.ffmpeg({"-i", input, "-map", "0:v:0", "-an", "-pix_fmt", "rgb24",
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
        && decoder.getDecodedFrameCount() <= 1, "AVI opens with exact navigation and at most one calibration picture")) return false;
    const auto snapshot = decoder.frameIndexSnapshot();
    decoder.resetPerformanceCounters();
    QElapsedTimer timer;
    timer.start();
    for (int frame : {2995, 1, 2999, 1500, 30, 0, 150, 29, 31}) {
        decoder.clearCache();
        if (!check(hash(decoder.getFrameImage(frame)) == expected.at(frame),
                   "AVI random seek matches an independent full decode")) return false;
    }
    std::cout << name.toStdString() << ": " << timer.elapsed() << " ms, "
              << decoder.getDecodedFrameCount() << " pictures for nine seeks\n";
    if (!check(decoder.getDecodedFrameCount() < 500 && !decoder.hasCompleteFrameIndex()
        && decoder.frameIndexSnapshot() == snapshot, "AVI seeks decode nearby dependencies and preserve the shared map")) return false;
    VDQtVideoDecoder consumer;
    if (!check(consumer.openFile(input, snapshot) && consumer.getDecodedFrameCount() == 0
        && hash(consumer.getFrameImage(2999)) == expected.at(2999),
        "preview adopts AVI presentation order and native seek positions without another header pass")) return false;
    const auto inspection = decoder.scanVideoStream();
    consumer.close();
    return check(inspection.totalFrames == 3000 && decoder.hasCompleteFrameIndex()
        && consumer.openFile(input, decoder.frameIndexSnapshot())
        && hash(consumer.getFrameImage(1500)) == expected.at(1500),
        "fresh AVI inspections and adopted decoded snapshots retain exact seeking");
}

bool rejectedMetadata(VDQtTestFixtures& fixtures) {
    const QString input = fixtures.directory.filePath("normal.avi");
    AVFormatContext *format = nullptr;
    const QByteArray path = input.toUtf8();
    if (avformat_open_input(&format, path.constData(), nullptr, nullptr) < 0) return false;
    struct Close { AVFormatContext*& value; ~Close() { avformat_close_input(&value); } } close{format};
    if (avformat_find_stream_info(format, nullptr) < 0) return false;
    const int video = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video < 0) return false;
    AVStream *stream = format->streams[video];
    VDQtAviFrameOrder order;
    if (!check(VDQtReadAviFrameOrder(input, stream, &order) && order.samples.size() == 3000,
               "unmodified AVI metadata establishes complete picture order")) return false;
    const auto rejected = [&](const QString& filePath, AVStream *candidate, const char *message) {
        VDQtAviFrameOrder sentinel;
        sentinel.samples = {7}; sentinel.hasBFrames = true;
        return check(!VDQtReadAviFrameOrder(filePath, candidate, &sentinel)
            && sentinel.samples == QVector<int>{7} && sentinel.hasBFrames, message);
    };
    const AVIndexEntry original = *avformat_index_get_entry(stream, 10);
    av_add_index_entry(stream, original.pos + 1, original.timestamp, original.size, 0, original.flags);
    if (!rejected(input, stream, "index offsets must identify actual movie chunk headers")) return false;
    av_add_index_entry(stream, original.pos, original.timestamp, original.size + 1, 0, original.flags);
    if (!rejected(input, stream, "index sizes must match movie chunk sizes")) return false;
    av_add_index_entry(stream, original.pos, original.timestamp, original.size, 0, original.flags);
    const int64_t declared = stream->nb_frames;
    stream->nb_frames = 4;
    if (!rejected(input, stream, "stale declared counts cannot truncate the indexed source")) return false;
    stream->nb_frames = declared;
    AVFormatContext *partial = avformat_alloc_context();
    if (!partial) return false;
    Close partialClose{partial};
    AVStream *shortStream = avformat_new_stream(partial, nullptr);
    if (!shortStream || avcodec_parameters_copy(shortStream->codecpar, stream->codecpar) < 0) return false;
    shortStream->time_base = stream->time_base; shortStream->nb_frames = 4;
    for (int i = 0; i < 4; ++i) {
        const auto *entry = avformat_index_get_entry(stream, i);
        av_add_index_entry(shortStream, entry->pos, entry->timestamp, entry->size, 0, entry->flags);
    }
    if (!rejected(input, shortStream, "matching stale count and partial index cannot hide unindexed tail chunks")) return false;
    QFile source(input);
    if (!source.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = source.readAll();
    const auto mutation = [&](QByteArray modified, const char *message) {
        const QString mutated = fixtures.directory.filePath("mutated.avi");
        return fixtures.writeText(mutated, modified) && rejected(mutated, stream, message);
    };
    const auto *first = avformat_index_get_entry(stream, 0);
    auto damaged = bytes;
    qToLittleEndian<quint32>(UINT32_MAX, reinterpret_cast<uchar*>(damaged.data() + first->pos + 4));
    if (!mutation(damaged, "oversized chunks cannot cross RIFF bounds")) return false;
    damaged = bytes; damaged[qsizetype(first->pos)] = '9';
    if (!mutation(damaged, "wrong-stream chunks cannot become selected video frames")) return false;
    const auto *next = avformat_index_get_entry(stream, 1);
    const QByteArray packet = bytes.mid(next->pos + 8, next->size);
    const int vop = packet.indexOf(QByteArray::fromHex("000001b6"));
    if (vop < 0) return false;
    // At 30 ticks/second, VOP type/unary/marker/time/marker consume ten bits.
    const qint64 codedBit = (next->pos + 8 + vop + 4) * 8 + 10;
    damaged = bytes;
    damaged[codedBit / 8] = char(uchar(damaged.at(codedBit / 8)) & ~(1 << (7 - codedBit % 8)));
    if (!mutation(damaged, "noncoded VOPs retain decoded accounting rather than invent pictures")) return false;
    damaged = bytes;
    for (int sample : {1, 2}) {
        const auto *entry = avformat_index_get_entry(stream, sample);
        const int offset = bytes.mid(entry->pos + 8, 512).indexOf(QByteArray::fromHex("000001b6"));
        if (offset < 0) return false;
        const qint64 timeBit = (entry->pos + 8 + offset + 4) * 8 + 4;
        const unsigned swapped = sample == 1 ? 2 : 1;
        for (int bit = 0; bit < 5; ++bit) {
            const qint64 at = timeBit + bit;
            const uchar mask = 1 << (7 - at % 8);
            damaged[qsizetype(at / 8)] = char((uchar(damaged.at(at / 8)) & ~mask)
                | ((swapped & (1 << (4 - bit))) ? mask : 0));
        }
    }
    if (!mutation(damaged, "codec clocks cannot reorder P pictures against their reference sequence")) return false;
    return mutation(bytes.left(bytes.size() - 1), "truncated RIFF data cannot become trusted navigation");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    VDQtVideoDecoder::setDecoderThreadCount(2);
    VDQtTestFixtures fixtures;
    const bool result = navigation(fixtures, "normal", "30", 0, false)
        && navigation(fixtures, "reordered", "30", 2, false)
        && navigation(fixtures, "fractional", "30000/1001", 3, true)
        && rejectedMetadata(fixtures) && recordAndAvixHeaders(fixtures);
    if (!result && !fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return result ? 0 : 1;
}
