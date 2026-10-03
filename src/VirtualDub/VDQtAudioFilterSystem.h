#ifndef VDQTAUDIOFILTERSYSTEM_H
#define VDQTAUDIOFILTERSYSTEM_H

#include <QByteArray>
#include <QAudioFormat>
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

// Validate before constructing either live or offline graphs. Disabled stages
// are still checked when loading settings, but do not affect duration bounds.
bool VDQtValidateAudioFilters(const QList<VDAudioFilterInstance>& chain,
                             QString *errorMessage = nullptr);

// Pull-through adapter: decoders supply source-rate/channel packed S16. All
// effects use the offline export graph; final conversion belongs exclusively
// to the sound-device boundary. The no-effect, same-format path is transparent.
// Stop the sink before reconfiguration/seek, then reset to discard graph history
// AND QIODevice's own read-ahead bytes before restarting the consumer.
class VDQtAudioFilterDevice final : public QIODevice {
public:
    VDQtAudioFilterDevice(QIODevice *source,
                          int sampleRate,
                          int channels,
                          QObject *parent = nullptr);
    VDQtAudioFilterDevice(QIODevice *source, int sampleRate, int channels,
                          const QAudioFormat& outputFormat, QObject *parent = nullptr);
    ~VDQtAudioFilterDevice() override;

    bool setFilterChain(const QList<VDAudioFilterInstance>& chain);
    bool resetProcessor();
    QString error() const;
    bool isSequential() const override { return true; }
    bool atEnd() const override;
    qint64 bytesAvailable() const override;

protected:
    qint64 readData(char *data, qint64 maximumLength) override;
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    struct GraphProcessor;
    QIODevice *mSource = nullptr;
    int mSampleRate = 0;
    int mChannels = 0;
    QAudioFormat mOutputFormat;
    QString mConfigurationError;
    QList<VDAudioFilterInstance> mChain;
    std::unique_ptr<GraphProcessor> mGraphProcessor;
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

    QString ffmpegFilterGraph(int sourceSampleRate, QString *errorMessage = nullptr) const;

private:
    QList<VDAudioFilterInstance> mActiveChain;
};

#endif // VDQTAUDIOFILTERSYSTEM_H
