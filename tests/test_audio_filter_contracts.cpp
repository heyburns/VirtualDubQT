// Sample-level regressions for the actual pull device, not a physical sound
// card. ffmpeg(1) independently evaluates the graph used by offline exports.
#include "VirtualDub/VDQtAudioFilterSystem.h"
#include "support/VDQtTestFixtures.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QElapsedTimer>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <limits>
#include <algorithm>
#include <iostream>

namespace {
class FragmentSource final : public QIODevice {
public:
    explicit FragmentSource(const QByteArray& bytes) : bytes(bytes) { open(ReadOnly); }
    bool isSequential() const override { return true; }
    bool atEnd() const override { return position == bytes.size() && QIODevice::bytesAvailable() == 0; }
protected:
    qint64 readData(char *data, qint64 maximum) override {
        const auto count = std::min({maximum, qint64{3}, qint64(bytes.size() - position)});
        if (count > 0) std::memcpy(data, bytes.constData() + position, count);
        position += count;
        return count;
    }
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    QByteArray bytes;
    qsizetype position = 0;
};
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
QByteArray fileBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QByteArray pull(VDQtAudioFilterDevice& device) {
    QByteArray result;
    for (int attempt = 0; attempt < 100000 && !device.atEnd(); ++attempt) {
        const auto block = device.read(4096);
        result += block;
    }
    return result;
}
bool sameSamples(const QByteArray& left, const QByteArray& right, int tolerance = 1) {
    if (!check(left.size() == right.size(), "live/export sample counts and EOF tails agree")) {
        std::cerr << left.size() << " vs " << right.size() << '\n'; return false;
    }
    for (qsizetype sample = 0; sample < left.size() / 2; ++sample) {
        const int a = qFromLittleEndian<qint16>(left.constData() + sample * 2);
        const int b = qFromLittleEndian<qint16>(right.constData() + sample * 2);
        if (std::abs(a - b) > tolerance) {
            std::cerr << "Sample " << sample << ": " << a << " vs " << b << '\n';
            return check(false, "live/export effects agree sample by sample");
        }
    }
    return true;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    VDQtTestFixtures fixtures;
    QByteArray pcm(48000 * 2 * 2, '\0');
    for (int frame = 0; frame < 48000; ++frame) {
        qToLittleEndian<qint16>(frame == 0 ? 30000 :
            static_cast<qint16>(5000 * std::sin(frame * 0.07)), pcm.data() + frame * 4);
        qToLittleEndian<qint16>(frame == 0 ? -20000 :
            static_cast<qint16>(3500 * std::cos(frame * 0.05)), pcm.data() + frame * 4 + 2);
    }
    const QString input = fixtures.directory.filePath("input.s16le");
    if (!fixtures.writeText(input, pcm)) return 1;
    VDQtAudioFilterSystem system;
    for (auto type : {VDAudioFilterType::LowPass, VDAudioFilterType::HighPass,
                     VDAudioFilterType::Gain, VDAudioFilterType::ChannelMix,
                     VDAudioFilterType::CenterCut, VDAudioFilterType::CenterMix,
                     VDAudioFilterType::Chorus, VDAudioFilterType::PitchShift,
                     VDAudioFilterType::TimeStretch, VDAudioFilterType::Resample}) {
        auto filter = system.createFilter(type);
        if (type == VDAudioFilterType::PitchShift) filter.params["semitones"] = 3.0;
        if (type == VDAudioFilterType::TimeStretch) filter.params["factor"] = 1.5;
        if (type == VDAudioFilterType::Resample) filter.params["sampleRate"] = 44100;
        system.replaceActiveChain({filter});
        const QString output = fixtures.directory.filePath("reference.s16le");
        if (!fixtures.ffmpeg({"-f", "s16le", "-ar", "48000", "-ac", "2", "-i", input,
                              "-af", system.ffmpegFilterGraph(48000), "-ar", "48000", "-ac", "2",
                              "-f", "s16le", output})) {
            std::cerr << fixtures.error.toStdString() << '\n'; return 1;
        }
        QBuffer source(&pcm);
        source.open(QIODevice::ReadOnly);
        VDQtAudioFilterDevice device(&source, 48000, 2);
        device.setFilterChain({filter});
        const auto actual = pull(device);
        if (!sameSamples(actual, fileBytes(output))) {
            std::cerr << "Filter type " << static_cast<int>(type) << '\n'; return 1;
        }
        auto identity = system.createFilter(VDAudioFilterType::Resample);
        identity.params["sampleRate"] = 48000;
        source.seek(0);
        device.setFilterChain({filter, identity});
        if (!sameSamples(pull(device), actual)) return 1;
        // Rebuilding after a seek must flush old graph delay/history and Qt's
        // QIODevice read-ahead buffer, not prepend samples from the old cursor.
        source.seek(0);
        device.resetProcessor();
        if (!sameSamples(pull(device), actual)) return 1;
        source.seek(0);
        device.resetProcessor();
        device.read(37); // Leave read-ahead samples behind in QIODevice.
        source.seek(0);
        device.resetProcessor();
        if (!sameSamples(pull(device), actual)) return 1;
    }
    auto lowpass = system.createFilter(VDAudioFilterType::LowPass);
    system.replaceActiveChain({lowpass});
    QBuffer source(&pcm);
    source.open(QIODevice::ReadOnly);
    VDQtAudioFilterDevice reference(&source, 48000, 2);
    reference.setFilterChain({lowpass});
    const auto expected = pull(reference);
    FragmentSource fragmented(pcm);
    VDQtAudioFilterDevice fragmentDevice(&fragmented, 48000, 2);
    fragmentDevice.setFilterChain({lowpass});
    if (!sameSamples(pull(fragmentDevice), expected)) return 1;
    // Sound-card fallbacks still filter BEFORE channel/rate/precision conversion.
    for (const auto format : {QAudioFormat::UInt8, QAudioFormat::Int16,
                             QAudioFormat::Int32, QAudioFormat::Float}) {
        QAudioFormat output;
        output.setSampleRate(32000);
        output.setChannelCount(1);
        output.setSampleFormat(format);
        const QString rawFormat = format == QAudioFormat::UInt8 ? "u8"
            : format == QAudioFormat::Int16 ? "s16le" : format == QAudioFormat::Int32 ? "s32le" : "f32le";
        const QString path = fixtures.directory.filePath("reference." + rawFormat);
        if (!fixtures.ffmpeg({"-f", "s16le", "-ar", "48000", "-ac", "2", "-i", input,
                              "-af", system.ffmpegFilterGraph(48000), "-ar", "32000", "-ac", "1",
                              "-f", rawFormat, path})) return 1;
        source.seek(0);
        VDQtAudioFilterDevice converted(&source, 48000, 2, output);
        if (!check(converted.setFilterChain({lowpass}), "device-boundary conversion initializes")) return 1;
        if (!check(pull(converted) == fileBytes(path), "device fallback retains effects in all supported sample formats")) return 1;
        source.seek(0);
        converted.setFilterChain({});
        if (!check(!pull(converted).isEmpty(), "conversion without effects remains available")) return 1;
    }
    source.seek(0);
    VDQtAudioFilterDevice invalid(&source, 48000, 2);
    auto bad = lowpass;
    bad.params["cutoffHz"] = std::numeric_limits<double>::quiet_NaN();
    if (!check(!invalid.setFilterChain({bad}) && !invalid.error().isEmpty()
               && invalid.read(100).isEmpty() && source.pos() == 0,
               "invalid filters fail visibly without unfiltered fallback")) return 1;
    auto pitch = system.createFilter(VDAudioFilterType::PitchShift);
    pitch.params["semitones"] = 1e100;
    QString error;
    system.replaceActiveChain({pitch});
    if (!check(system.ffmpegFilterGraph(48000, &error).isEmpty() && !error.isEmpty(),
               "exponential audio parameters are rejected before arithmetic")) return 1;
    QByteArray incomplete = pcm.left(7);
    QBuffer shortSource(&incomplete);
    shortSource.open(QIODevice::ReadOnly);
    VDQtAudioFilterDevice shortDevice(&shortSource, 48000, 2);
    shortDevice.setFilterChain({lowpass});
    pull(shortDevice);
    if (!check(!shortDevice.error().isEmpty(), "an incomplete final input sample is reported")) return 1;
    auto gain = system.createFilter(VDAudioFilterType::Gain);
    gain.params["_sylia.config.2"] = 1234567890123.0;
    if (!check(VDQtValidateAudioFilters({gain}, &error),
               "unused legacy 64-bit configuration fields retain compatibility")) return 1;
    if (app.arguments().contains("--benchmark")) {
        auto chorus = system.createFilter(VDAudioFilterType::Chorus);
        auto stretch = system.createFilter(VDAudioFilterType::TimeStretch);
        stretch.params["factor"] = 1.5;
        pitch.params["semitones"] = 3;
        QList<qint64> reads;
        for (int run = 0; run < 10; ++run) {
            source.seek(0);
            VDQtAudioFilterDevice benchmark(&source, 48000, 2);
            benchmark.setFilterChain({lowpass, chorus, pitch, stretch});
            while (!benchmark.atEnd()) {
                QElapsedTimer timer;
                timer.start();
                benchmark.read(4096);
                reads.append(timer.nsecsElapsed());
            }
            if (!check(benchmark.error().isEmpty(), "benchmark chain drains normally")) return 1;
        }
        std::sort(reads.begin(), reads.end());
        std::cout << "S16 stereo, lowpass+chorus+pitch+stretch, 4096-byte pulls: p99 "
                  << reads.at((reads.size() - 1) * 99 / 100) / 1e6 << " ms, worst "
                  << reads.last() / 1e6 << " ms (buffered source, no physical sink)\n";
    }
    return 0;
}
