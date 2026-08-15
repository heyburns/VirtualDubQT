#ifndef VDQTFILTERSYSTEM_H
#define VDQTFILTERSYSTEM_H

#include <QString>
#include <QImage>
#include <QList>
#include <QMap>
#include <QSettings>

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
    Sharpen
};

struct VDFilterInstance {
    QString id;
    QString name;
    VDFilterType type;
    bool enabled;
    QMap<QString, double> params;
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
    void setFilterEnabled(int index, bool enabled);
    void updateFilterParams(int index, const QMap<QString, double>& params);

    // Frame Processing
    QImage processFrame(const QImage& inputFrame);

    // Persistence across sessions
    void saveSettings();
    void loadSettings();

private:
    QList<VDFilterInstance> mActiveChain;
};

#endif // VDQTFILTERSYSTEM_H
