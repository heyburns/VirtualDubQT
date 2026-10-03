#ifndef VDQTJOBQUEUE_H
#define VDQTJOBQUEUE_H

#include "VDQtProjectFile.h"

#include <QObject>
#include <QTimer>
#include <memory>

class QLockFile;

// Authoritative in-memory job list plus debounced JSON autosave. Mutators are
// intentionally centralized here so the table model, runner, and recovery file
// observe consistent status transitions. Jobs execute serially in the main
// window; this class does not own worker threads or exporters.
class VDQtJobQueue : public QObject {
    Q_OBJECT
public:
    explicit VDQtJobQueue(QObject *parent = nullptr);
    ~VDQtJobQueue() override;

    int count() const { return static_cast<int>(mJobs.size()); }
    bool isEmpty() const { return mJobs.isEmpty(); }
    const VDQtJobState *jobAt(int index) const;
    QList<VDQtJobState> jobs() const { return mJobs; }

    bool addJobs(const QList<VDQtJobState>& jobs,
                 QString *errorMessage = nullptr);
    bool replaceJobs(const QList<VDQtJobState>& jobs,
                     QString *errorMessage = nullptr);
    bool appendFromFile(const QString& path, QString *errorMessage = nullptr);
    bool replaceFromFile(const QString& path, QString *errorMessage = nullptr);
    bool saveToFile(const QString& path, QString *errorMessage = nullptr) const;

    void removeRows(const QList<int>& rows);
    void clearAll();
    void clearCompleted();
    void retryFailed();
    void setAllPendingPostponed(bool postponed);
    bool moveJob(int from, int to);
    bool setJobName(int index, const QString& name);
    bool setJobStatus(int index, VDQtJobStatus status,
                      const QString& error = QString());
    bool setJobProgress(int index, double progress,
                        const QString& message = QString());
    bool appendJobLog(int index, const QString& message);
    bool setReplaceExisting(int index, bool enabled);

    void setRunning(bool running, int currentIndex = -1);
    bool isRunning() const { return mRunning; }
    int currentIndex() const { return mCurrentIndex; }
    int pendingCount() const;

    void setAutoRunEnabled(bool enabled);
    bool autoRunEnabled() const { return mAutoRun; }

    // Hold one ownership lock for the complete editor lifetime. A concurrent
    // editor gets a separate durable queue and recovery file, not a shared
    // writable copy of this instance's records.
    bool setAutosavePath(const QString& path, QString *errorMessage = nullptr);
    QString autosavePath() const { return mAutosavePath; }
    QString recoveryPath() const { return mRecoveryPath; }
    QString persistenceError() const { return mPersistenceError; }
    bool loadAutosave(QString *errorMessage = nullptr);
    bool flush(QString *errorMessage = nullptr);

    static bool validateJobs(const QList<VDQtJobState>& jobs,
                             QString *errorMessage = nullptr);
    static QString operationText(VDQtJobOperation operation);
    static QString statusText(VDQtJobStatus status);

Q_SIGNALS:
    void queueAboutToReset();
    void queueReset();
    void jobChanged(int row);
    void runningChanged(bool running, int currentIndex);
    void runRequested();
    void stopRequested();
    void abortRequested();
    void reloadRequested(int row);
    void batchWizardRequested();
    // Empty means a previously failed checkpoint has now succeeded. Failures
    // are sticky and deduplicated so encoder callbacks cannot flood the UI.
    void persistenceStatusChanged(const QString& error);

private:
    VDQtJobState *mutableJobAt(int index);
    static void normalizeNewJob(VDQtJobState *job);
    static void boundDiagnostics(QList<VDQtJobState> *jobs);
    bool validateAdmission(const QList<VDQtJobState>& jobs,
                           QString *errorMessage,
                           const QString& proposedPath = QString()) const;
    void reportPersistence(const QString& error);
    void scheduleAutosave();

    QList<VDQtJobState> mJobs;
    QString mAutosavePath;
    QString mRecoveryPath;
    QString mPersistenceError;
    std::unique_ptr<QLockFile> mAutosaveLock;
    bool mAutosaveProtected = false; // Never overwrite a queue that failed to load.
    QTimer mAutosaveTimer; // Coalesces bursts of progress/UI changes.
    bool mRunning = false;
    bool mAutoRun = false;
    int mCurrentIndex = -1;
};

#endif
