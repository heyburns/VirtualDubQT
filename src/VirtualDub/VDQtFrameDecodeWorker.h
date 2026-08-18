#ifndef VDQTFRAMEDECODEWORKER_H
#define VDQTFRAMEDECODEWORKER_H

#include "VDQtFilterSystem.h"
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

    bool openSource(const QString& filePath,
                    const QString& formatName,
                    int colorSpace,
                    int componentRange,
                    int errorMode);
    void closeSource();
    QString lastError() const { return mDecoder.getLastError(); }
    void setDecompressionConfig(const QString& formatName, int colorSpace, int componentRange);
    void setErrorMode(int errorMode);
    void setFilterChain(const QList<VDFilterInstance>& chain);

    void requestFrame(int frameIndex,
                      quint64 generation,
                      bool preserveSequentialDecode,
                      bool renderFilteredOutput);
    void cancelPending(quint64 generation);

Q_SIGNALS:
    void frameReady(int frameIndex,
                    quint64 generation,
                    const QImage& inputImage,
                    const QImage& outputImage,
                    bool keyFrame,
                    double timestampSeconds,
                    int frameCount,
                    int frameCountStatus,
                    quint64 seekCount,
                    quint64 decodedFrameCount);
    void frameUnavailable(int frameIndex,
                          quint64 generation,
                          const QString& errorMessage,
                          int frameCount,
                          int frameCountStatus);

private Q_SLOTS:
    void processPendingRequest();

private:
    mutable QMutex mRequestMutex;
    int mRequestedFrame = -1;
    quint64 mRequestedGeneration = 0;
    quint64 mLatestGeneration = 0;
    bool mRequestedSequential = false;
    bool mRequestedFilteredOutput = false;
    bool mProcessScheduled = false;

    VDQtVideoDecoder mDecoder;
    VDQtFilterSystem mFilters;
};

#endif // VDQTFRAMEDECODEWORKER_H
