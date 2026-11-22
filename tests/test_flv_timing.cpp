// Header-only Spark navigation must agree with the decoder's displayed pictures,
// particularly startup disposable pictures and malformed tag-chain fallback.
#include "VirtualDub/VDQtFlvTiming.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
QByteArray integer(quint32 value, int size) {
    QByteArray data(size, '\0');
    for (int i = 0; i < size; ++i) data[i] = char(value >> (8 * (size - i - 1)));
    return data;
}
QByteArray picture(unsigned type) {
    QByteArray data(7, '\0');
    int position = 0;
    const auto bits = [&](unsigned value, int count) {
        for (int bit = count - 1; bit >= 0; --bit, ++position)
            if ((value >> bit) & 1) data[position / 8] |= char(1 << (7 - position % 8));
    };
    bits(1, 17); bits(0, 5); bits(0, 8); bits(5, 3); // 320x240 Spark.
    bits(type, 2); bits(0, 1); bits(1, 5); bits(0, 1);
    return QByteArray(1, char(type == 0 ? 0x12 : type == 1 ? 0x22 : 0x32)) + data;
}
QByteArray tag(int type, quint32 timestamp, const QByteArray& body) {
    return integer(type, 1) + integer(body.size(), 3) + integer(timestamp & 0xffffff, 3)
        + integer(timestamp >> 24, 1) + QByteArray(3, '\0') + body + integer(11 + body.size(), 4);
}
QByteArray header() { return QByteArray::fromHex("464c5601050000000900000000"); }
bool expect(bool value, const char *message) {
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const auto path = directory.filePath("headers.flv");
    const auto parse = [&](const QByteArray& bytes, QVector<VDQtFlvFrameTiming> *out, int width = 320) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
        file.close();
        return VDQtReadFlvTiming(path, width, 240, out);
    };
    const auto valid = header() + tag(18, 0, "metadata") + tag(9, 35, picture(0))
        + tag(8, 40, "audio") + tag(9, 197, picture(2)) + tag(9, 480, picture(1))
        + tag(9, 534, picture(2));
    QVector<VDQtFlvFrameTiming> frames;
    if (!expect(parse(valid, &frames) && frames.size() == 3 && frames[0].timestamp == 35
        && frames[1].timestamp == 480 && frames[2].timestamp == 534
        && frames[0].keyFrame && !frames[1].keyFrame && frames[1].seekPosition > frames[0].seekPosition,
        "startup disposable picture is excluded; later disposable pictures retain their ordinals")) return 1;
    if (!expect(parse(header() + tag(9, 0, picture(0)) + tag(9, 10, picture(2))
            + tag(9, 20, picture(3)) + tag(9, 30, picture(0)) + tag(9, 40, picture(2)), &frames)
        && frames.size() == 3 && frames[1].timestamp == 30 && frames[2].timestamp == 40,
        "all startup disposable pictures are excluded until the second reference")) return 1;
    if (!expect(parse(header() + tag(9, 0x01000001, picture(0)), &frames)
        && frames.size() == 1 && frames.first().timestamp == 0x01000001,
        "extended timestamp byte retains millisecond timing")) return 1;
    const auto rejected = [&](const QByteArray& bytes, const char *message, int width = 320) {
        QVector<VDQtFlvFrameTiming> sentinel{{17, 23, true}};
        return expect(!parse(bytes, &sentinel, width) && sentinel.size() == 1
            && sentinel[0].timestamp == 17 && sentinel[0].seekPosition == 23, message);
    };
    if (!rejected(valid, "changing picture dimensions retain fallback", 321)
        || !rejected(header() + tag(9, 0, picture(1)), "the source must begin with a reference key picture")
        || !rejected(header() + tag(9, 0, picture(0)) + tag(9, 0, picture(1)), "duplicate timestamps retain fallback")
        || !rejected(header() + tag(9, 2, picture(0)) + tag(9, 1, picture(1)), "backward/wrapped clocks retain fallback")
        || !rejected(header() + tag(0x29, 0, picture(0)), "filtered/encrypted tags retain fallback")
        || !rejected(header() + tag(9, 0, QByteArray::fromHex("1700000000")), "AVC remains outside the narrow Spark reader")
        || !rejected(header() + tag(9, 0, "x"), "short picture prefixes are rejected")) return 1;
    auto damaged = valid;
    damaged[12] = '\1';
    if (!rejected(damaged, "PreviousTagSize links must agree")) return 1;
    damaged = header() + tag(9, 0, picture(0));
    damaged[14] = char(255);
    if (!rejected(damaged, "oversized tags cannot cross the file boundary")) return 1;
    for (int size : {0, 8, 12, int(valid.size() - 1), int(valid.size() - 4)})
        if (!rejected(valid.left(size), "truncated tags/tails leave output unchanged")) return 1;
    return 0;
}
