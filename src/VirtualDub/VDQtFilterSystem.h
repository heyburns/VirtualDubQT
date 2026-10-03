#ifndef VDQTFILTERSYSTEM_H
#define VDQTFILTERSYSTEM_H

#include <QString>
#include <QImage>
#include <QList>
#include <QMap>
#include <QHash>
#include <QByteArray>

#include <utility>

// Built-in type IDs are serialized in projects/scripts; append new values
// before Count instead of reordering existing entries.
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

// Serializable configuration for one stage of the active filter chain.
// Runtime state and cached assets live in VDQtFilterSystem, never in this value.
struct VDFilterInstance {
    QString id;
    QString name;
    VDFilterType type = VDFilterType::NullTransform;
    bool enabled = true;
    QMap<QString, double> params;
    QMap<QString, QString> stringParams;
    QString pluginId;
    QByteArray pluginConfiguration;
};

// Describes how a chain changes time. Bob deinterlacing is currently the main
// rate-changing case and emits two output frames for each input frame.
struct VDFilterTimingInfo {
    int outputFramesPerInput = 1;
    bool sequenceSupported = true;
};

// frameNumber/time refer to the edited timeline at this stage, not the decoder
// ordinal. Source identity stays separate, especially for held/masked images.
// Output-phase fields are filled by rate-changing processing. Negative means
// the caller could not establish that piece of timing.
struct VDFilterFrameContext {
    qint64 frameNumber = -1;
    double timestampSeconds = -1.0;
    double frameRate = 0.0;
    qint64 sourceFrameNumber = -1;
    double sourceTimestampSeconds = -1.0;
    double inputDurationSeconds = 0.0;
    qint64 outputFrameNumber = -1;
    int outputPhase = 0;
    double outputTimestampSeconds = -1.0;
};

// Pipeline-local diagnostic: configuration identity is separate from the
// human-readable error, so callers need not parse a logged warning.
struct VDFilterProcessingError {
    QString filterId;
    QString filterName;
    QString message;
    QString text() const {
        return filterName.isEmpty() ? message : QString("%1: %2").arg(filterName, message);
    }
};

// Ordered video-filter pipeline used by preview and full-processing export.
// Each decoder worker owns a private instance/snapshot; instance() is the
// editable session chain. This separation prevents temporal history, plug-in
// instances, and mutable caches from being shared across threads.
//
// Frames enter as QImage. processFrameSequence() is the authoritative API
// because one source frame may produce multiple output phases. Temporal state
// is keyed by filter instance ID and reset on seeks/discontinuities.
class VDQtFilterSystem {
public:
    VDQtFilterSystem();
    ~VDQtFilterSystem();
    VDQtFilterSystem(const VDQtFilterSystem&) = delete;
    VDQtFilterSystem& operator=(const VDQtFilterSystem&) = delete;

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
    // Migrate legacy missing/duplicate identities once, before snapshots are
    // saved or copied to workers. Valid unique identities remain unchanged.
    static QList<VDFilterInstance> normalizeChainIds(QList<VDFilterInstance> chain);
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
    // Failure (including null input/allocation failure) clears outputFrames;
    // callers never receive an apparently successful partial/null sequence.
    bool processFrameSequence(const QImage& inputFrame, QList<QImage>& outputFrames);
    bool processFrameSequence(const QImage& inputFrame,
                              QList<QImage>& outputFrames,
                              const VDFilterFrameContext& context);
    VDFilterTimingInfo getTimingInfo() const;
    const VDFilterProcessingError& processingError() const { return mProcessingError; }
    QString lastError() const { return mProcessingError.text(); }

private:
    QImage failProcessing(const QString& message, const VDFilterInstance *filter = nullptr);
    VDFilterProcessingError mProcessingError;
    void forgetRuntimeInstances();
    QString runtimeInstanceId(const QString& filterId) const;
    // Serialized IDs describe configuration, not runtime ownership. Two
    // pipelines with the same chain must never share/clear native instances.
    QString mRuntimeNamespace;
    // One stage/phase only; the sequence driver reuses completed upstream work
    // and advances downstream history in chronological emitted-frame order.
    QImage processFilterForPhase(QImage inputFrame, quint64 phase,
                                const VDFilterFrameContext& context, int filterIndex);

    struct TemporalState {
        qint64 lastFrameNumber = -1;
        QImage previousFrame;
        QList<QImage> history;
    };

    QList<VDFilterInstance> mActiveChain;
    // Expensive derived data are cached by parameter/asset key and reused
    // across frames. They are intentionally local to this pipeline instance.
    QHash<QString, QByteArray> mSixAxisLutCache;
    QHash<QString, QImage> mAssetCache;
    QHash<QString, TemporalState> mTemporalStates;
};

#endif // VDQTFILTERSYSTEM_H
