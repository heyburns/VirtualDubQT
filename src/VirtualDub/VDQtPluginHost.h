#ifndef VDQTPLUGINHOST_H
#define VDQTPLUGINHOST_H

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

struct VDQtPluginFilterInfo {
    QString id;
    QString name;
    QString description;
    QString author;
    QString modulePath;
    int apiVersion = 0;
    bool hasNativeConfiguration = false;
};

// Host for Linux-native modules implementing VirtualDub's legacy VDX video
// filter entry points. Windows DLLs cannot be loaded into a native Linux
// process; they are reported to the catalog with a useful diagnostic.
class VDQtPluginHost {
public:
    static VDQtPluginHost& instance();

    QList<VDQtPluginFilterInfo> videoFilters();
    QString report();
    QStringList searchPaths() const;
    void reload();

    bool processVideoFilter(const QString& filterId,
                            const QString& instanceId,
                            const QByteArray& serializedConfiguration,
                            const QImage& input,
                            QImage *output,
                            QString *errorMessage = nullptr);
    void forgetInstance(const QString& instanceId);
    void forgetAllInstances();

private:
    VDQtPluginHost();
    ~VDQtPluginHost();
    VDQtPluginHost(const VDQtPluginHost&) = delete;
    VDQtPluginHost& operator=(const VDQtPluginHost&) = delete;

    class Private;
    std::unique_ptr<Private> d;
};

#endif // VDQTPLUGINHOST_H
