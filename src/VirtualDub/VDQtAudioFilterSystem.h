#ifndef VDQTAUDIOFILTERSYSTEM_H
#define VDQTAUDIOFILTERSYSTEM_H

#include <QByteArray>
#include <QIODevice>
#include <QList>
#include <QMap>
#include <QString>
#include <QVector>
#include <memory>

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
