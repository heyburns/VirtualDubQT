#ifndef VDQTFRAMESERVER_H
#define VDQTFRAMESERVER_H

#include "VDQtFilterSystem.h"
#include "VDQtTimeline.h"

#include <QObject>
#include <QThread>
#include <QString>
#include <atomic>

// Linux-native frame server. It renders the requested edit/filter graph into a
// NUT stream written to a named pipe, which FFmpeg and other Linux tools can
// consume. It is intentionally not the Windows VirtualDub frameserver protocol.
// The render runs on mThread; start/stop and signals remain on the UI object.
class VDQtFrameServer : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString sourcePath;             // Video/script/concat input.
        QString pipePath;               // FIFO created for the NUT stream.
        QString audioPath;              // Optional independent audio input.
        int startFrame = 0;
        int endFrame = -1;
        QString decompressionFormat = QStringLiteral("Autoselect");
        int colorSpace = 0;
        int componentRange = 0;
        int errorMode = 0;
        QList<VDFilterInstance> filters;
        bool preserveEmptyFrames = true;
        QList<VDQtTimelineSegment> timelineSegments; // Empty means identity.
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
