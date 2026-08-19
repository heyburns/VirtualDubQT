#ifndef VDQTFILTERSYSTEM_H
#define VDQTFILTERSYSTEM_H

#include <QString>
#include <QImage>
#include <QList>
#include <QMap>
#include <QHash>
#include <QByteArray>

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
    Count
};

struct VDFilterInstance {
    QString id;
    QString name;
    VDFilterType type;
    bool enabled;
    QMap<QString, double> params;
};

struct VDFilterTimingInfo {
    int outputFramesPerInput = 1;
    bool sequenceSupported = true;
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
    };
    QList<FilterInfo> getAvailableFilters() const;

    // Active Chain Management
    const QList<VDFilterInstance>& getActiveChain() const { return mActiveChain; }
    void addFilter(VDFilterType type);
    void removeFilter(int index);
    void moveFilterUp(int index);
    void moveFilterDown(int index);
    void clearFilters();
    void replaceActiveChain(const QList<VDFilterInstance>& chain);
    // Worker-local preview chains are independent snapshots of the session chain.
    void replaceActiveChainTransient(const QList<VDFilterInstance>& chain) { mActiveChain = chain; }
    void setFilterEnabled(int index, bool enabled);
    void updateFilterParams(int index, const QMap<QString, double>& params);

    // Frame Processing. processFrame() is the legacy one-frame path and
    // returns the first temporal phase of rate-changing filters. Exporters
    // and other rate-aware callers must use processFrameSequence().
    QImage processFrame(const QImage& inputFrame);
    bool processFrameSequence(const QImage& inputFrame, QList<QImage>& outputFrames);
    VDFilterTimingInfo getTimingInfo() const;

private:
    QImage processFrameForPhase(const QImage& inputFrame, quint64 bobPhaseMask);

    QList<VDFilterInstance> mActiveChain;
    QHash<QString, QByteArray> mSixAxisLutCache;
};

#endif // VDQTFILTERSYSTEM_H
