#ifndef VDQTFRAMEDECODEWORKER_H
#define VDQTFRAMEDECODEWORKER_H

#include "VDQtFilterSystem.h"
#include "VDQtTimeline.h"
#include "VDQtVideoDecoder.h"

#include <QImage>
#include <QMutex>
#include <QObject>

// Owns the interactive decoder and preview filter pipeline on a dedicated
// thread. requestFrame() is deliberately thread-safe and latest-request-wins:
// slider motion cannot build an unbounded queue of obsolete random seeks.
class VDQtFrameDecodeWorker final : public QObject {
    Q_OBJECT

public:
    explicit VDQtFrameDecodeWorker(QObject *parent = nullptr);

    // Source and filter configuration calls are dispatched on the worker
    // thread. The request and cancel methods below are the cross-thread mailbox.
    bool openSource(const QString& filePath,
                    const QString& formatName,
                    int colorSpace,
                    int componentRange,
                    int errorMode,
                    const VDQtVideoDecoder::FrameIndexSnapshotPtr& navigation = {});
    bool useSharedAvsSource(VDQtVideoDecoder *decoder);
    void closeSource();
    QString lastError() const;
    void setDecompressionConfig(const QString& formatName, int colorSpace, int componentRange);
    void setErrorMode(int errorMode);
    void applyFrameCacheBudget();
    void setFilterChain(const QList<VDFilterInstance>& chain);
    bool adoptFrameIndexSnapshot(const VDQtVideoDecoder::FrameIndexSnapshotPtr& snapshot);

    // Update a single pending request and invalidate obsolete work by
    // generation. The caller can submit from the GUI thread.
    void requestFrame(int frameIndex,
                      quint64 generation,
                      bool preserveSequentialDecode,
                      bool renderFilteredOutput,
                      qint64 timelineFrame = -1,
                      const QList<VDQtTimelineSegment>& timelineSegments = {});
    void cancelPending(quint64 generation);

Q_SIGNALS:
    void frameIndexAvailable(quint64 generation,
                             const VDQtVideoDecoder::FrameIndexSnapshotPtr& snapshot);
    void frameReady(int frameIndex,
                    quint64 generation,
                    const QImage& inputImage,
                    const QList<QImage>& outputImages,
                    bool keyFrame,
                    double timestampSeconds,
                    double durationSeconds,
                    int frameCount,
                    int frameCountStatus,
                    quint64 seekCount,
                    quint64 decodedFrameCount,
                    const QString& filterError);
    void frameUnavailable(int frameIndex,
                          quint64 generation,
                          const QString& errorMessage,
                          int frameCount,
                          int frameCountStatus,
                          bool reachedEndOfStream);

private Q_SLOTS:
    void processPendingRequest();

private:
    // Protect request scheduling and generation flags only. Keep decoding and
    // filter execution outside this mutex.
    mutable QMutex mRequestMutex;
    int mRequestedFrame = -1;
    quint64 mRequestedGeneration = 0;
    quint64 mLatestGeneration = 0;
    bool mRequestedSequential = false;
    bool mRequestedFilteredOutput = false;
    qint64 mRequestedTimelineFrame = -1;
    QList<VDQtTimelineSegment> mRequestedTimelineSegments;
    bool mProcessScheduled = false;

    VDQtVideoDecoder mDecoder;
    // Non-owning. The main window closes the worker synchronously before the
    // authoritative decoder can be closed or destroyed.
    VDQtVideoDecoder *mSharedAvsDecoder = nullptr;
    VDQtFilterSystem mFilters;

    VDQtVideoDecoder* activeDecoder();
    const VDQtVideoDecoder* activeDecoder() const;
};

#endif // VDQTFRAMEDECODEWORKER_H
