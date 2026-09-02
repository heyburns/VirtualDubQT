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
// process; they are reported to the catalog with a useful diagnostic. Loaded
// modules and live filter instances are hidden behind Private so legacy ABI
// types and dlopen handles do not leak into the rest of the application.
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
    // Instance IDs correspond to entries in an active filter chain. Forgetting
    // them runs plug-in teardown and prevents configuration/state leaking into
    // a later chain that happens to use the same module.
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
