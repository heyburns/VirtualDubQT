// Disposable WAV/range fixtures exercise the waveform helper without widgets,
// FFmpeg, user media, settings, shared targets or the application's event loop.
#include "VirtualDub/VDQtWaveform.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>

namespace {
using Ranges = QList<QPair<int64_t, int64_t>>;
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
void append16(QByteArray& bytes, quint16 value) {
    bytes.append(char(value));
    bytes.append(char(value >> 8));
}
void append32(QByteArray& bytes, quint32 value) {
    append16(bytes, quint16(value));
    append16(bytes, quint16(value >> 16));
}
void append64(QByteArray& bytes, quint64 value) {
    append32(bytes, quint32(value));
    append32(bytes, quint32(value >> 32));
}
void put16(QByteArray& bytes, qsizetype offset, quint16 value) {
    bytes[offset] = char(value);
    bytes[offset + 1] = char(value >> 8);
}
void put32(QByteArray& bytes, qsizetype offset, quint32 value) {
    put16(bytes, offset, quint16(value));
    put16(bytes, offset + 2, quint16(value >> 16));
}
void put64(QByteArray& bytes, qsizetype offset, quint64 value) {
    put32(bytes, offset, quint32(value));
    put32(bytes, offset + 4, quint32(value >> 32));
}
QByteArray chunk(const char *id, const QByteArray& payload, bool pad = true,
                 quint32 declaredSize = 0xfffffffeU) {
    QByteArray bytes(id, 4);
    append32(bytes, declaredSize == 0xfffffffeU ? quint32(payload.size()) : declaredSize);
    bytes += payload;
    if (pad && (payload.size() & 1)) bytes += '\0';
    return bytes;
}
QByteArray riff(const QByteArray& chunks, bool sentinelSize = false) {
    QByteArray bytes("RIFF", 4);
    append32(bytes, sentinelSize ? 0xffffffffU : quint32(chunks.size() + 4));
    bytes += "WAVE";
    bytes += chunks;
    return bytes;
}
QByteArray format(int bits, int channels = 1, int rate = 48000,
                  bool floating = false, int validBits = -1, bool extensible = false) {
    QByteArray bytes;
    append16(bytes, extensible ? 0xfffe : floating ? 3 : 1);
    append16(bytes, quint16(channels));
    append32(bytes, quint32(rate));
    append32(bytes, quint32(rate * channels * (bits / 8)));
    append16(bytes, quint16(channels * (bits / 8)));
    append16(bytes, quint16(bits));
    if (extensible) {
        append16(bytes, 22);
        append16(bytes, quint16(validBits < 0 ? bits : validBits));
        append32(bytes, 0);
        append32(bytes, floating ? 3 : 1);
        static constexpr char tail[12] = {0, 0, 0x10, 0, char(0x80), 0, 0, char(0xaa), 0, 0x38, char(0x9b), 0x71};
        bytes.append(tail, sizeof(tail));
    }
    return bytes;
}
QByteArray samples(const QList<double>& values, int bits, bool floating = false) {
    QByteArray bytes;
    for (double value : values) {
        if (floating && bits == 32) {
            const float sample = float(value);
            quint32 raw;
            std::memcpy(&raw, &sample, sizeof(raw));
            append32(bytes, raw);
        } else if (floating && bits == 64) {
            quint64 raw;
            std::memcpy(&raw, &value, sizeof(raw));
            append64(bytes, raw);
        } else if (bits == 8) {
            bytes.append(char(std::clamp(int(std::llround(value * 128)) + 128, 0, 255)));
        } else {
            const qint64 sign = qint64(1) << (bits - 1);
            const qint64 raw = std::clamp<qint64>(std::llround(value * sign), -sign, sign - 1);
            for (int byte = 0; byte < bits / 8; ++byte)
                bytes.append(char(quint64(raw) >> (byte * 8)));
        }
    }
    return bytes;
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray wav(const QByteArray& fmt, const QByteArray& data) {
    return riff(chunk("fmt ", fmt) + chunk("data", data));
}
bool readFixture(const QString& path, const QByteArray& bytes, int columns,
                 VDQtWaveformData *result, QString *error = nullptr) {
    return write(path, bytes) && VDQtReadWaveformPeaks(path, columns, result, error);
}
bool near(double first, double second) { return std::abs(first - second) < 1e-7; }

bool sampleRanges() {
    QString error;
    Ranges output;
    bool passed = check(VDQtCapAudioSampleRanges({{0, 25}, {100, 25}}, 40, &output, &error)
        && output == Ranges({{0, 25}, {100, 15}}),
        "ordered cut ranges exclude deleted audio and truncate the final included range");
    passed &= check(VDQtCapAudioSampleRanges({{100, 25}, {0, 25}, {100, 25}}, 60, &output, &error)
        && output == Ranges({{100, 25}, {0, 25}, {100, 10}}),
        "reordered/repeated timeline segments retain their source order");
    passed &= check(VDQtCapAudioSampleRanges({{8, -1}}, 100, &output, &error, 12)
        && output == Ranges({{8, 4}}) && error.isEmpty(), "known EOF limits the remaining source audio");
    passed &= check(VDQtCapAudioSampleRanges({{8, -1}}, 100, &output, &error)
        && output == Ranges({{8, 100}}), "unknown EOF is bounded by the waveform preview budget");
    constexpr int64_t maximum = std::numeric_limits<int64_t>::max();
    passed &= check(VDQtCapAudioSampleRanges({{0, maximum}, {0, maximum}}, maximum, &output)
        && output == Ranges({{0, maximum}}), "aggregate range sums cannot overflow");
    const Ranges unchanged = output;
    for (const Ranges& input : QList<Ranges>{ {}, {{0, 0}}, {{-1, 1}}, {{0, -2}},
             {{0, 5}, {maximum, 1}}, {{0, 5}, {0, -1}}, {{maximum, -1}} }) {
        passed &= check(!VDQtCapAudioSampleRanges(input, 5, &output, &error)
            && output == unchanged && !error.isEmpty(),
            "invalid ranges fail atomically, even after the preview cap was already reached");
    }
    passed &= check(!VDQtCapAudioSampleRanges({{0, 1}}, 0, &output, &error)
        && !VDQtCapAudioSampleRanges({{12, -1}}, 5, &output, &error, 12)
        && !VDQtCapAudioSampleRanges({{13, -1}}, 5, &output, &error, 12)
        && !VDQtCapAudioSampleRanges({{0, 1}}, 5, nullptr, &error)
        && !VDQtCapAudioSampleRanges({{0, 1}}, 5, &output, &error, -2),
        "empty EOF, invalid limits and invalid destinations are diagnosed");
    return passed;
}

bool supportedSamples(const QString& path) {
    bool passed = true;
    QString error;
    VDQtWaveformData result;
    for (int bits : {8, 16, 24, 32}) {
        const auto bytes = wav(format(bits, 2, 32000), samples({-1, 0, 0.5, -0.25, 0, -0.25, 0, 0}, bits));
        passed &= check(readFixture(path, bytes, 4, &result, &error)
            && result.sampleRate == 32000 && result.channels == 2
            && result.containerBits == bits && result.validBits == bits
            && !result.floatingPoint && result.sampleFrames == 4
            && near(result.durationSeconds, 4.0 / 32000)
            && result.peaks == QVector<double>({1, 0.5, 0.25, 0}),
            "PCM 8/16/24/32 stereo samples normalize and report the WAV's real rate/precision");
    }
    for (int bits : {32, 64}) {
        passed &= check(readFixture(path, wav(format(bits, 2, 96000, true, bits, true),
            samples({-1.5, 0, 0.5, -0.25, 0, -0.25, 0, 0}, bits, true)), 4, &result, &error)
            && result.sampleRate == 96000 && result.floatingPoint
            && result.containerBits == bits && result.validBits == bits
            && result.peaks == QVector<double>({1, 0.5, 0.25, 0})
            && near(result.durationSeconds, 4.0 / 96000),
            "extensible float32/64 is decoded, clipped for display, and timed from the output WAV");
    }
    passed &= check(readFixture(path, wav(format(24, 1, 44100, false, 20, true),
        samples({-1, 0.5}, 24)), 2, &result, &error)
        && result.validBits == 20 && result.containerBits == 24
        && result.peaks == QVector<double>({1, 0.5}),
        "left-aligned extensible integer valid bits preserve normalized amplitude and precision");
    passed &= check(readFixture(path, wav(format(16), samples({0.25, -0.75}, 16)), 5, &result)
        && result.peaks == QVector<double>({0.25, 0.25, 0.25, 0.75, 0.75}),
        "short clips repeat their frame peak without rereading or out-of-range access");
    passed &= check(readFixture(path, wav(format(16), samples({0.125, 0.25, 0.5, 0.375, 0.625, 0.75, -1}, 16)),
        3, &result) && result.peaks == QVector<double>({0.25, 0.5, 1}),
        "uneven waveform columns include every sample frame exactly once");
    QList<double> wide;
    for (int i = 0; i < 64; ++i) wide.append(i == 63 ? -0.75 : 0);
    passed &= check(readFixture(path, wav(format(64, 64, 48000, true, 64, true),
        samples(wide, 64, true)), 1, &result) && result.channels == 64 && near(result.peaks[0], 0.75),
        "maximum supported channel count scans all channels of a complete frame");
    return passed;
}

bool containers(const QString& path) {
    bool passed = true;
    VDQtWaveformData result;
    const QByteArray fmt = format(16);
    const QByteArray data = samples({0.5, -0.25, -1}, 16);
    QString error;
    passed &= check(readFixture(path, riff(chunk("JUNK", "x") + chunk("fmt ", fmt)
        + chunk("data", data) + chunk("LIST", "abc")), 3, &result, &error)
        && result.peaks == QVector<double>({0.5, 0.25, 1}),
        "odd metadata padding and chunks after audio are parsed by bounded seeks");
    passed &= check(readFixture(path, riff(chunk("data", data) + chunk("fmt ", fmt)),
        3, &result), "format chunks after the audio are supported without materializing audio");
    passed &= check(readFixture(path, riff(chunk("fmt ", fmt)
        + chunk("data", data, false, 0xffffffffU), true), 3, &result)
        && result.sampleFrames == 3, "RIFF data and container sentinels use the finite file extent");
    passed &= check(readFixture(path, riff(chunk("fmt ", format(8))
        + chunk("data", samples({-0.5}, 8), false)), 1, &result)
        && result.sampleFrames == 1 && near(result.peaks[0], 0.5),
        "the application's final unpadded odd PCM8 data chunk remains readable");
    QByteArray ds64(28, '\0');
    QByteArray chunks = chunk("ds64", ds64) + chunk("fmt ", fmt)
        + chunk("data", data, true, 0xffffffffU);
    QByteArray rf64("RF64", 4);
    append32(rf64, 0xffffffffU);
    rf64 += "WAVE";
    rf64 += chunks;
    put64(rf64, 20, quint64(rf64.size() - 8));
    put64(rf64, 28, quint64(data.size()));
    put64(rf64, 36, 3);
    passed &= check(readFixture(path, rf64, 3, &result, &error)
        && result.sampleFrames == 3 && result.peaks == QVector<double>({0.5, 0.25, 1}),
        "RF64 ds64 sentinel sizes and exact sample counts are supported");
    QByteArray invalid = rf64;
    put64(invalid, 20, std::numeric_limits<quint64>::max());
    passed &= check(!readFixture(path, invalid, 3, &result, &error), "overflowing RF64 RIFF sizes are rejected");
    invalid = rf64;
    put64(invalid, 28, std::numeric_limits<quint64>::max());
    passed &= check(!readFixture(path, invalid, 3, &result, &error), "overflowing RF64 data sizes are rejected");
    invalid = rf64;
    put64(invalid, 36, 4);
    passed &= check(!readFixture(path, invalid, 3, &result, &error), "contradictory RF64 sample counts are rejected");
    invalid = rf64;
    put32(invalid, 44, 1);
    passed &= check(!readFixture(path, invalid, 3, &result, &error)
        && error.contains("unsupported"), "RF64 size tables receive an explicit unsupported diagnostic");
    return passed;
}

bool malformedStreams(const QString& path) {
    const QByteArray fmt = format(16);
    const QByteArray data = samples({0.5, -0.25}, 16);
    QList<QByteArray> invalid;
    invalid << QByteArray("RIFF", 4) << QByteArray("not a wav stream at all");
    QByteArray bytes = wav(fmt, data);
    put32(bytes, 4, quint32(bytes.size() + 20));
    invalid << bytes;
    bytes = wav(fmt, data);
    bytes.replace(0, 4, "RIFX");
    invalid << bytes;
    bytes.replace(0, 4, "RF64");
    put32(bytes, 4, 0xffffffffU);
    invalid << bytes;
    invalid << wav(fmt, "x") << wav(fmt, {})
        << riff(chunk("fmt ", fmt) + chunk("data", data, true, 100))
        << riff(chunk("fmt ", fmt) + chunk("data", data) + QByteArray("bad"))
        << riff(chunk("fmt ", fmt) + chunk("fmt ", fmt) + chunk("data", data))
        << riff(chunk("fmt ", fmt) + chunk("data", data) + chunk("data", data))
        << riff(chunk("JUNK", {}, false, 0xffffffffU) + chunk("fmt ", fmt) + chunk("data", data))
        << riff(chunk("JUNK", {}, false, 0xfffffff0U) + chunk("fmt ", fmt) + chunk("data", data))
        << riff(chunk("fmt ", fmt.left(15)) + chunk("data", data));
    for (int field : {0, 2, 4, 8, 12, 14}) {
        QByteArray wrong = fmt;
        if (field == 0) put16(wrong, field, 6);
        if (field == 2) put16(wrong, field, 65);
        if (field == 4 || field == 8) put32(wrong, field, 0);
        if (field == 12) put16(wrong, field, 1);
        if (field == 14) put16(wrong, field, 64);
        invalid << wav(wrong, data);
    }
    QByteArray wrong = fmt;
    wrong += '\0';
    invalid << wav(wrong, data);
    for (int field : {16, 18, 24, 39}) {
        wrong = format(16, 1, 48000, false, 16, true);
        if (field == 16) put16(wrong, field, 21);
        if (field == 18) put16(wrong, field, 17);
        if (field == 24) put32(wrong, field, 0x10001);
        if (field == 39) wrong[field] = char(0x72);
        invalid << wav(wrong, data);
    }
    wrong = format(16, 1, 48000, false, 0, true);
    invalid << wav(wrong, data);
    wrong = format(16, 1, 48000, false, 16, true);
    put16(wrong, 16, 23);
    invalid << wav(wrong, data) << wav(wrong.left(39), data);
    invalid << wav(format(32, 1, 48000, true, 16, true), samples({0.5}, 32, true));
    for (int bits : {32, 64}) {
        for (double value : {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
            invalid << wav(format(bits, 1, 48000, true), samples({0.25, value}, bits, true));
    }
    VDQtWaveformData unchanged;
    unchanged.sampleRate = 17;
    unchanged.peaks = {0.123};
    QString error;
    bool passed = true;
    for (const QByteArray& fixture : invalid) {
        VDQtWaveformData result = unchanged;
        passed &= check(!readFixture(path, fixture, 1100, &result, &error)
            && !error.isEmpty() && result.sampleRate == 17 && result.peaks == unchanged.peaks,
            "malformed/unsupported/non-finite WAVs fail with a diagnostic and no partial output");
    }
    passed &= check(!VDQtReadWaveformPeaks(path, 0, &unchanged, &error)
        && !VDQtReadWaveformPeaks(path, 4097, &unchanged, &error)
        && !VDQtReadWaveformPeaks(path, 100, nullptr, &error)
        && !VDQtReadWaveformPeaks(path + ".missing", 100, &unchanged, &error),
        "invalid display bounds, destinations and missing files fail explicitly");
    return passed;
}

bool streaming(const QString& path) {
    constexpr int frames = 1024 * 1024;
    constexpr int bytes = frames * 2;
    QByteArray header("RIFF", 4);
    append32(header, bytes + 36);
    header += "WAVE";
    header += chunk("fmt ", format(16));
    header += chunk("data", {}, false, bytes);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(header) != header.size()) return false;
    QByteArray block(32 * 1024, '\0');
    for (qsizetype i = 0; i < block.size(); i += 2) put16(block, i, 0x8000);
    for (int i = 0; i < bytes / block.size(); ++i)
        if (file.write(block) != block.size()) return false;
    file.close();
    VDQtWaveformData result;
    QString error;
    int callbacks = 0;
    qint64 previous = -1;
    bool ordered = true;
    bool passed = check(VDQtReadWaveformPeaks(path, 1100, &result, &error,
        [&](qint64 current, qint64 total) {
            ++callbacks;
            ordered &= total == frames && current >= previous && current <= total;
            previous = current;
            return true;
        }) && callbacks == bytes / (32 * 1024) + 1 && ordered && previous == frames
        && result.sampleFrames == frames && result.peaks.size() == 1100
        && std::all_of(result.peaks.cbegin(), result.peaks.cend(), [](double p) { return p == 1; }),
        "multi-MiB WAVs stream in bounded blocks, retain only display peaks, and report ordered progress");
    result.sampleRate = 17;
    result.peaks = {0.123};
    callbacks = 0;
    passed &= check(!VDQtReadWaveformPeaks(path, 1100, &result, &error,
        [&](qint64, qint64) { return ++callbacks < 2; })
        && error.contains("canceled") && result.sampleRate == 17
        && result.peaks == QVector<double>({0.123}),
        "streaming cancellation preserves the caller's prior waveform");
    passed &= check(VDQtWaveformDetail::columnBoundary(std::numeric_limits<qint64>::max(),
        4096, 4096) == std::numeric_limits<qint64>::max(),
        "column boundaries remain exact without multiplying a giant frame count");
    return passed;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QString path = temporary.filePath("waveform.wav");
    bool passed = sampleRanges();
    passed &= supportedSamples(path);
    passed &= containers(path);
    passed &= malformedStreams(path);
    passed &= streaming(path);
    return passed ? 0 : 1;
}
