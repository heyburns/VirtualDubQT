#ifndef VDQTAUDIOFILTERSYSTEM_H
#define VDQTAUDIOFILTERSYSTEM_H

#include <QByteArray>
#include <QIODevice>
#include <QList>
#include <QMap>
#include <QString>
#include <QVector>
#include <memory>

// Audio filters are represented as plain data so the same chain can be used
// by live preview, project/job serialization, and FFmpeg export generation.
// Keep enum values stable: saved project files persist them numerically.
enum class VDAudioFilterType {
    Gain = 0,
    LowPass,
    HighPass,
    Resample,
    ChannelMix,
    PitchShift,
    TimeStretch,
    CenterCut,
    CenterMix,
    Chorus,
    Count
};

struct VDAudioFilterInstance {
    QString id;
    QString name;
    VDAudioFilterType type = VDAudioFilterType::Gain;
    bool enabled = true;
    QMap<QString, double> params;

    bool operator==(const VDAudioFilterInstance& other) const {
        return id == other.id && name == other.name && type == other.type
            && enabled == other.enabled && params == other.params;
    }
};

// Stateful, allocation-free processor for filters that can operate in place on
// signed 16-bit interleaved PCM. configure() builds one State per chain entry;
// reset() must be called after a seek so history from the old time position is
// never mixed into the new one.
class VDQtAudioFilterProcessor {
public:
    void configure(const QList<VDAudioFilterInstance>& chain,
                   int sampleRate,
                   int channels);
    void reset();
    void processInt16(char *data, qint64 bytes);

private:
    struct State {
        QVector<double> previousInput;
        QVector<double> previousOutput;
        QVector<qint16> delay;
        qint64 delayPosition = 0;
        double phase = 0.0;
    };

    QList<VDAudioFilterInstance> mChain;
    QVector<State> mStates;
    int mSampleRate = 0;
    int mChannels = 0;
};

// Pull-through adapter placed between an audio decoder QIODevice and
// QAudioSink. Fixed-rate filters use VDQtAudioFilterProcessor. Pitch/time
// filters are delegated to VariableRateProcessor because they can produce a
// different number of output samples than they consume.
class VDQtAudioFilterDevice final : public QIODevice {
public:
    VDQtAudioFilterDevice(QIODevice *source,
                          int sampleRate,
                          int channels,
                          QObject *parent = nullptr);
    ~VDQtAudioFilterDevice() override;

    void setFilterChain(const QList<VDAudioFilterInstance>& chain);
    void resetProcessor();
    bool isSequential() const override { return true; }
    bool atEnd() const override;
    qint64 bytesAvailable() const override;

protected:
    qint64 readData(char *data, qint64 maximumLength) override;
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    struct VariableRateProcessor;
    QIODevice *mSource = nullptr;
    int mSampleRate = 0;
    int mChannels = 0;
    QList<VDAudioFilterInstance> mChain;
    VDQtAudioFilterProcessor mProcessor;
    std::unique_ptr<VariableRateProcessor> mVariableProcessor;
};

// Session-wide catalog and editable audio-filter chain. This object stores
// configuration only; playback devices take snapshots so dialog edits cannot
// mutate a processor while the audio callback is using it. ffmpegFilterGraph()
// translates that same configuration for offline exports.
class VDQtAudioFilterSystem {
public:
    struct FilterInfo {
        VDAudioFilterType type;
        QString name;
        QString description;
    };

    static VDQtAudioFilterSystem& instance();
    QList<FilterInfo> availableFilters() const;
    VDAudioFilterInstance createFilter(VDAudioFilterType type) const;
    const QList<VDAudioFilterInstance>& activeChain() const { return mActiveChain; }
    void replaceActiveChain(const QList<VDAudioFilterInstance>& chain);
    void addFilter(VDAudioFilterType type);
    void removeFilter(int index);
    void moveFilter(int from, int to);
    void setEnabled(int index, bool enabled);
    void updateParams(int index, const QMap<QString, double>& params);
    void clear();
    bool hasEnabledFilters() const;

    QString ffmpegFilterGraph(int sourceSampleRate) const;

private:
    QList<VDAudioFilterInstance> mActiveChain;
};

#endif // VDQTAUDIOFILTERSYSTEM_H
