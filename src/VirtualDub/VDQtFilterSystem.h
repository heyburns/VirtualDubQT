#ifndef VDQTFILTERSYSTEM_H
#define VDQTFILTERSYSTEM_H

#include <QString>
#include <QImage>
#include <QList>
#include <QMap>
#include <QHash>
#include <QByteArray>

#include <utility>

enum class VDFilterType {
    SixAxis,
    BobDoubler,
    Resize,
    Rotate,
    FlipHorizontal,
    FlipVertical,
    BrightnessContrast,
    Grayscale,
    InvertColor,
    Blur,
    Sharpen,
    Deinterlace,
    Emboss,
    FieldSwap,
    HSVAdjust,
    Levels,
    Threshold,
    Posterize,
    Gamma,
    Smoother,
    Crop,
    ChromaShift,
    Pixelate,
    Plugin,
    Fill,
    Canvas,
    Curves,
    ChromaSmoother,
    DrawText,
    DrawTime,
    FieldDelay,
    GammaCorrect,
    Interlace,
    Interpolate,
    InverseTelecine,
    MotionBlur,
    NullTransform,
    Perspective,
    Reduce2,
    Reduce2HQ,
    Rotate2,
    TemporalSmoother,
    Television,
    WarpResize,
    WarpSharp,
    Logo,
    ConvertFormat,
    Count
};

struct VDFilterInstance {
    QString id;
    QString name;
    VDFilterType type;
    bool enabled;
    QMap<QString, double> params;
    QMap<QString, QString> stringParams;
    QString pluginId;
    QByteArray pluginConfiguration;
};

struct VDFilterTimingInfo {
    int outputFramesPerInput = 1;
    bool sequenceSupported = true;
};

struct VDFilterFrameContext {
    qint64 frameNumber = -1;
    double timestampSeconds = -1.0;
    double frameRate = 0.0;
};

class VDQtFilterSystem {
public:
    VDQtFilterSystem();
    ~VDQtFilterSystem();

    static VDQtFilterSystem& instance();

    // Available catalog
    struct FilterInfo {
        VDFilterType type;
        QString name;
        QString description;
        QString pluginId;

        FilterInfo() = default;
        FilterInfo(VDFilterType filterType, QString filterName,
                   QString filterDescription, QString nativePluginId = {})
            : type(filterType), name(std::move(filterName)),
              description(std::move(filterDescription)),
              pluginId(std::move(nativePluginId)) {}
    };
    QList<FilterInfo> getAvailableFilters() const;

    // Active Chain Management
    const QList<VDFilterInstance>& getActiveChain() const { return mActiveChain; }
    void addFilter(VDFilterType type);
    bool addPluginFilter(const QString& pluginId);
    void removeFilter(int index);
    void moveFilterUp(int index);
    void moveFilterDown(int index);
    void clearFilters();
    void replaceActiveChain(const QList<VDFilterInstance>& chain);
    // Worker-local preview chains are independent snapshots of the session chain.
    void replaceActiveChainTransient(const QList<VDFilterInstance>& chain);
    void setFilterEnabled(int index, bool enabled);
    void updateFilterParams(int index, const QMap<QString, double>& params);
    void updateFilterStringParams(
        int index, const QMap<QString, QString>& stringParams);
    void resetRuntimeState();

    // Frame Processing. processFrame() is the legacy one-frame path and
    // returns the first temporal phase of rate-changing filters. Exporters
    // and other rate-aware callers must use processFrameSequence().
    QImage processFrame(const QImage& inputFrame);
    QImage processFrame(const QImage& inputFrame,
                        const VDFilterFrameContext& context);
    bool processFrameSequence(const QImage& inputFrame, QList<QImage>& outputFrames);
    bool processFrameSequence(const QImage& inputFrame,
                              QList<QImage>& outputFrames,
                              const VDFilterFrameContext& context);
    VDFilterTimingInfo getTimingInfo() const;

private:
    QImage processFrameForPhase(const QImage& inputFrame, quint64 bobPhaseMask,
                                const VDFilterFrameContext& context);

    struct TemporalState {
        qint64 lastFrameNumber = -1;
        QImage previousFrame;
        QList<QImage> history;
    };

    QList<VDFilterInstance> mActiveChain;
    QHash<QString, QByteArray> mSixAxisLutCache;
    QHash<QString, QImage> mAssetCache;
    QHash<QString, TemporalState> mTemporalStates;
};

#endif // VDQTFILTERSYSTEM_H
