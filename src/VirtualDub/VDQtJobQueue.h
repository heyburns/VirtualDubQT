#ifndef VDQTJOBQUEUE_H
#define VDQTJOBQUEUE_H

#include "VDQtProjectFile.h"

#include <QObject>
#include <QTimer>

class VDQtJobQueue : public QObject {
    Q_OBJECT
public:
    explicit VDQtJobQueue(QObject *parent = nullptr);
    ~VDQtJobQueue() override;

    int count() const { return static_cast<int>(mJobs.size()); }
    bool isEmpty() const { return mJobs.isEmpty(); }
    const VDQtJobState *jobAt(int index) const;
    VDQtJobState *jobAt(int index);
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

    void setAutosavePath(const QString& path);
    QString autosavePath() const { return mAutosavePath; }
    bool loadAutosave(QString *errorMessage = nullptr);
    bool flush(QString *errorMessage = nullptr) const;

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

private:
    static void normalizeNewJob(VDQtJobState *job);
    void scheduleAutosave();

    QList<VDQtJobState> mJobs;
    QString mAutosavePath;
    QTimer mAutosaveTimer;
    bool mRunning = false;
    bool mAutoRun = false;
    int mCurrentIndex = -1;
};

#endif
