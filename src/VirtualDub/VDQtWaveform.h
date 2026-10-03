#ifndef VDQT_WAVEFORM_H
#define VDQT_WAVEFORM_H

#include <QString>
#include <QFile>
#include <QList>
#include <QPair>
#include <QVector>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>

// Waveform previews use the edited audio ranges, in timeline order. Cap their
// combined length, not the span between their first and last source positions:
// that span includes deleted audio and is meaningless after reordered edits.
// A sole count of -1 means source EOF; an unknown EOF is capped to the preview
// budget. Every input is validated, including ranges beyond that budget.
inline bool VDQtCapAudioSampleRanges(const QList<QPair<int64_t, int64_t>>& ranges,
                                    int64_t maximumSamples,
                                    QList<QPair<int64_t, int64_t>> *result,
                                    QString *error = nullptr,
                                    int64_t knownSourceSamples = -1) {
    const auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (!result || ranges.isEmpty() || maximumSamples <= 0 || knownSourceSamples < -1)
        return fail(QStringLiteral("The waveform sample range or preview limit is invalid."));
    constexpr int64_t ceiling = std::numeric_limits<int64_t>::max();
    for (const auto& range : ranges) {
        if (range.first < 0 || range.second == 0 || range.second < -1
            || (range.second == -1 && ranges.size() != 1)
            || (range.second > 0 && range.second > ceiling - range.first))
            return fail(QStringLiteral("A waveform sample range is invalid or overflows."));
        if (range.second == -1
            && (knownSourceSamples >= 0 ? range.first > knownSourceSamples
                                       : maximumSamples > ceiling - range.first))
            return fail(QStringLiteral("The waveform EOF range is outside the source sample domain."));
    }
    QList<QPair<int64_t, int64_t>> capped;
    int64_t remaining = maximumSamples;
    for (const auto& range : ranges) {
        if (!remaining) break;
        const int64_t count = range.second >= 0 ? range.second
            : knownSourceSamples >= 0 ? knownSourceSamples - range.first : maximumSamples;
        const int64_t take = std::min(count, remaining);
        if (take > 0) capped.append(qMakePair(range.first, take));
        remaining -= take;
    }
    if (capped.isEmpty())
        return fail(QStringLiteral("There are no audio samples in the waveform preview range."));
    *result = std::move(capped);
    if (error) error->clear();
    return true;
}

struct VDQtWaveformData {
    int sampleRate = 0;
    int channels = 0;
    int containerBits = 0;
    int validBits = 0;
    bool floatingPoint = false;
    qint64 sampleFrames = 0;
    double durationSeconds = 0;
    QVector<double> peaks;
};

namespace VDQtWaveformDetail {
inline quint16 le16(const char *p) {
    const auto *u = reinterpret_cast<const unsigned char *>(p);
    return quint16(u[0]) | (quint16(u[1]) << 8);
}
inline quint32 le32(const char *p) {
    const auto *u = reinterpret_cast<const unsigned char *>(p);
    return quint32(u[0]) | (quint32(u[1]) << 8) | (quint32(u[2]) << 16) | (quint32(u[3]) << 24);
}
inline quint64 le64(const char *p) {
    return quint64(le32(p)) | (quint64(le32(p + 4)) << 32);
}
inline bool readExactly(QFile& file, char *data, qint64 size) {
    qint64 read = 0;
    while (read < size) {
        const qint64 amount = file.read(data + read, size - read);
        if (amount <= 0) return false;
        read += amount;
    }
    return true;
}
inline qint64 columnBoundary(qint64 frames, int column, int columns) {
    // Avoid frames*column overflowing on large RF64 streams.
    return (frames / columns) * column + ((frames % columns) * column) / columns;
}
}

// The only size-dependent storage is a 32 KiB, frame-aligned input block and
// at most 4096 double peaks (32 KiB). Parse chunk headers by seeking, never by
// reading the complete WAV. The actual output format/rate/duration come from
// this WAV, since audio filters may change them after source extraction.
// Progress is called once per input block and may return false to cancel.
inline bool VDQtReadWaveformPeaks(
    const QString& path, int columns, VDQtWaveformData *result,
    QString *error = nullptr,
    const std::function<bool(qint64, qint64)>& progress = {}) {
    using namespace VDQtWaveformDetail;
    const auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (!result || columns < 1 || columns > 4096)
        return fail(QStringLiteral("The waveform display width is unsupported."));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
        return fail(QStringLiteral("The waveform WAV could not be opened: %1").arg(file.errorString()));
    const qint64 fileSize = file.size();
    const auto readAt = [&](qint64 offset, char *bytes, qint64 size) {
        return offset >= 0 && size >= 0 && offset <= fileSize && size <= fileSize - offset
            && file.seek(offset) && readExactly(file, bytes, size);
    };
    std::array<char, 40> header{};
    if (!readAt(0, header.data(), 12) || std::memcmp(header.data() + 8, "WAVE", 4) != 0)
        return fail(QStringLiteral("The waveform file is not a complete WAV stream."));
    const bool rf64 = std::memcmp(header.data(), "RF64", 4) == 0;
    if (!rf64 && std::memcmp(header.data(), "RIFF", 4) != 0)
        return fail(QStringLiteral("The waveform WAV container is unsupported (little-endian RIFF/RF64 required)."));
    const quint32 riffSize = le32(header.data() + 4);
    qint64 containerEnd = fileSize;
    if (!rf64 && riffSize != 0xffffffffU) {
        if (riffSize < 4 || qint64(riffSize) > fileSize - 8)
            return fail(QStringLiteral("The waveform WAV RIFF size is truncated or invalid."));
        containerEnd = 8 + qint64(riffSize);
    } else if (rf64 && riffSize != 0xffffffffU) {
        return fail(QStringLiteral("The waveform RF64 header is invalid."));
    }
    bool haveFormat = false;
    bool haveDs64 = false;
    quint64 rf64DataBytes = 0;
    quint64 rf64Frames = 0;
    qint64 dataOffset = -1;
    qint64 dataBytes = 0;
    int blockAlign = 0;
    VDQtWaveformData parsed;
    qint64 offset = 12;
    int chunkCount = 0;
    while (offset < containerEnd) {
        if (++chunkCount > 4096 || containerEnd - offset < 8
            || !readAt(offset, header.data(), 8))
            return fail(QStringLiteral("The waveform WAV chunk headers are truncated or excessive."));
        const bool isData = std::memcmp(header.data(), "data", 4) == 0;
        const bool isFormat = std::memcmp(header.data(), "fmt ", 4) == 0;
        const bool isDs64 = std::memcmp(header.data(), "ds64", 4) == 0;
        const quint32 size32 = le32(header.data() + 4);
        const qint64 payload = offset + 8;
        quint64 size = size32;
        if (size32 == 0xffffffffU) {
            if (!isData || (rf64 && !haveDs64))
                return fail(QStringLiteral("The waveform WAV sentinel chunk size is unsupported or missing RF64 size data."));
            size = rf64 ? rf64DataBytes : quint64(containerEnd - payload);
        }
        if (size > quint64(containerEnd - payload))
            return fail(QStringLiteral("The waveform WAV chunk extends past the file or RIFF boundary."));
        if (isDs64) {
            if (!rf64 || haveDs64 || size < 28 || !readAt(payload, header.data(), 28))
                return fail(QStringLiteral("The waveform RF64 size chunk is invalid."));
            const quint64 riff64 = le64(header.data());
            rf64DataBytes = le64(header.data() + 8);
            rf64Frames = le64(header.data() + 16);
            if (le32(header.data() + 24) != 0)
                return fail(QStringLiteral("Waveform RF64 chunk-size tables are unsupported."));
            if (riff64 < 4 || riff64 > quint64(fileSize - 8)
                || riff64 + 8 < quint64(payload) + size)
                return fail(QStringLiteral("The waveform RF64 stream size is truncated or invalid."));
            containerEnd = qint64(riff64 + 8);
            haveDs64 = true;
        } else if (isFormat) {
            if (haveFormat || size < 16 || !readAt(payload, header.data(), 16))
                return fail(QStringLiteral("The waveform WAV format chunk is missing, duplicated or truncated."));
            quint16 tag = le16(header.data());
            parsed.channels = le16(header.data() + 2);
            const quint32 rate = le32(header.data() + 4);
            const quint32 byteRate = le32(header.data() + 8);
            blockAlign = le16(header.data() + 12);
            parsed.containerBits = le16(header.data() + 14);
            parsed.validBits = parsed.containerBits;
            if (size != 16 && (size < 18 || !readAt(payload + 16, header.data() + 16, 2)
                              || quint64(le16(header.data() + 16)) + 18 > size))
                return fail(QStringLiteral("The waveform WAV format extension length is invalid."));
            if (tag == 0xfffe) {
                if (size < 40 || !readAt(payload, header.data(), 40))
                    return fail(QStringLiteral("The waveform extensible WAV format is truncated."));
                const quint16 extensionBytes = le16(header.data() + 16);
                if (extensionBytes < 22 || quint64(extensionBytes) + 18 > size)
                    return fail(QStringLiteral("The waveform extensible WAV extension length is invalid."));
                parsed.validBits = le16(header.data() + 18);
                static constexpr unsigned char guidTail[12] = {
                    0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71
                };
                const quint32 subformat = le32(header.data() + 24);
                if ((subformat != 1 && subformat != 3)
                    || std::memcmp(header.data() + 28, guidTail, sizeof(guidTail)) != 0)
                    return fail(QStringLiteral("The waveform extensible WAV subformat is unsupported."));
                tag = quint16(subformat);
            }
            parsed.floatingPoint = tag == 3;
            const bool supportedBits = parsed.floatingPoint
                ? parsed.containerBits == 32 || parsed.containerBits == 64
                : parsed.containerBits == 8 || parsed.containerBits == 16
                    || parsed.containerBits == 24 || parsed.containerBits == 32;
            if ((tag != 1 && tag != 3) || !supportedBits)
                return fail(QStringLiteral("The waveform WAV sample format is unsupported (PCM 8/16/24/32 or float 32/64 required)."));
            if (parsed.channels < 1 || parsed.channels > 64 || rate < 1
                || rate > quint32(std::numeric_limits<int>::max())
                || parsed.validBits < 1 || parsed.validBits > parsed.containerBits
                || (parsed.floatingPoint && parsed.validBits != parsed.containerBits)
                || blockAlign != parsed.channels * (parsed.containerBits / 8)
                || quint64(rate) * quint64(blockAlign) != byteRate)
                return fail(QStringLiteral("The waveform WAV rate, channels, precision or block layout is invalid."));
            parsed.sampleRate = int(rate);
            haveFormat = true;
        } else if (isData) {
            if (dataOffset >= 0)
                return fail(QStringLiteral("Waveform WAV streams with multiple data chunks are unsupported."));
            dataOffset = payload;
            dataBytes = qint64(size);
        }
        offset = payload + qint64(size);
        // This application's PCM writer leaves a final odd-length data chunk
        // unpadded. Accept that case, but require padding between chunks.
        if ((size & 1U) && offset < containerEnd) ++offset;
        else if ((size & 1U) && !isData)
            return fail(QStringLiteral("The waveform WAV chunk padding is truncated."));
    }
    if (!haveFormat || dataOffset < 0 || (rf64 && !haveDs64)
        || dataBytes < blockAlign || dataBytes % blockAlign != 0)
        return fail(QStringLiteral("The waveform WAV audio data is missing, empty or not frame-aligned."));
    parsed.sampleFrames = dataBytes / blockAlign;
    if (rf64 && (rf64DataBytes != quint64(dataBytes)
                 || (rf64Frames != 0 && rf64Frames != quint64(parsed.sampleFrames))))
        return fail(QStringLiteral("The waveform RF64 data size or sample count disagrees with the audio chunk."));
    parsed.durationSeconds = double(parsed.sampleFrames) / parsed.sampleRate;
    parsed.peaks.resize(columns);
    std::array<char, 32 * 1024> block;
    const qint64 blockCapacity = (qint64(block.size()) / blockAlign) * blockAlign;
    qint64 unreadBytes = dataBytes;
    qint64 bufferBytes = 0;
    qint64 bufferPosition = 0;
    qint64 currentFrame = -1;
    double currentPeak = 0;
    if (!file.seek(dataOffset))
        return fail(QStringLiteral("The waveform WAV data could not be read."));
    const auto nextFrame = [&]() {
        if (bufferPosition == bufferBytes) {
            if (progress && !progress(currentFrame + 1, parsed.sampleFrames)) {
                if (error) *error = QStringLiteral("Waveform reading was canceled.");
                return false;
            }
            bufferBytes = std::min(blockCapacity, unreadBytes);
            bufferPosition = 0;
            if (!bufferBytes || !readExactly(file, block.data(), bufferBytes)) {
                if (error) *error = QStringLiteral("The waveform WAV audio data is truncated or unreadable.");
                return false;
            }
            unreadBytes -= bufferBytes;
        }
        const char *sample = block.data() + bufferPosition;
        const int sampleBytes = parsed.containerBits / 8;
        currentPeak = 0;
        for (int channel = 0; channel < parsed.channels; ++channel, sample += sampleBytes) {
            double value = 0;
            if (parsed.floatingPoint) {
                if (parsed.containerBits == 32) {
                    const quint32 bits = le32(sample);
                    float floating;
                    std::memcpy(&floating, &bits, sizeof(floating));
                    value = floating;
                } else {
                    const quint64 bits = le64(sample);
                    std::memcpy(&value, &bits, sizeof(value));
                }
                if (!std::isfinite(value)) {
                    if (error) *error = QStringLiteral("The waveform WAV contains non-finite floating-point samples.");
                    return false;
                }
            } else if (parsed.containerBits == 8) {
                value = (int(static_cast<unsigned char>(*sample)) - 128) / 128.0;
            } else {
                quint64 raw = le16(sample);
                if (parsed.containerBits == 24)
                    raw |= quint64(static_cast<unsigned char>(sample[2])) << 16;
                else if (parsed.containerBits == 32) raw = le32(sample);
                const quint64 sign = quint64(1) << (parsed.containerBits - 1);
                const qint64 signedValue = qint64(raw) - (raw & sign ? qint64(sign << 1) : 0);
                // Extensible PCM valid bits are left-aligned in the container.
                value = double(signedValue) / double(sign);
            }
            currentPeak = std::max(currentPeak, std::min(1.0, std::abs(value)));
        }
        bufferPosition += blockAlign;
        ++currentFrame;
        return true;
    };
    for (int column = 0; column < columns; ++column) {
        const qint64 begin = columnBoundary(parsed.sampleFrames, column, columns);
        const qint64 end = std::max(begin + 1,
            columnBoundary(parsed.sampleFrames, column + 1, columns));
        // Narrow clips repeat their sample across neighboring display columns;
        // keep the previous frame's peak instead of seeking/re-reading it.
        double peak = currentFrame >= begin ? currentPeak : 0;
        while (currentFrame + 1 < end) {
            if (!nextFrame()) return false;
            if (currentFrame >= begin) peak = std::max(peak, currentPeak);
        }
        parsed.peaks[column] = peak;
    }
    if (progress && !progress(parsed.sampleFrames, parsed.sampleFrames))
        return fail(QStringLiteral("Waveform reading was canceled."));
    *result = std::move(parsed);
    if (error) error->clear();
    return true;
}

#endif
