#ifndef VDQTFRAMESERVER_H
#define VDQTFRAMESERVER_H

#include "VDQtFilterSystem.h"

#include <QObject>
#include <QThread>
#include <QString>
#include <atomic>

class VDQtFrameServer : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString sourcePath;
        QString pipePath;
        int startFrame = 0;
        int endFrame = -1;
        QString decompressionFormat = QStringLiteral("Autoselect");
        int colorSpace = 0;
        int componentRange = 0;
        int errorMode = 0;
        QList<VDFilterInstance> filters;
        bool preserveEmptyFrames = true;
    };

    explicit VDQtFrameServer(QObject *parent = nullptr);
    ~VDQtFrameServer() override;

    bool start(const Config& config, QString *errorMessage = nullptr);
    void stop();
    bool isRunning() const;

Q_SIGNALS:
    void serverStarted(const QString& pipePath);
    void serverFinished(const QString& errorMessage);

private:
    void run(Config config);

    QThread *mThread = nullptr;
    std::atomic_bool mCancelRequested{false};
};

#endif // VDQTFRAMESERVER_H
