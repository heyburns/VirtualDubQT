#include "VDQtFrameDecodeWorker.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

VDQtFrameDecodeWorker::VDQtFrameDecodeWorker(QObject *parent)
    : QObject(parent) {
}

bool VDQtFrameDecodeWorker::openSource(const QString& filePath,
                                       const QString& formatName,
                                       int colorSpace,
                                       int componentRange,
                                       int errorMode) {
    Q_ASSERT(QThread::currentThread() == thread());
    mDecoder.close();
    mDecoder.setDecompressionConfig(formatName, colorSpace, componentRange);
    mDecoder.setErrorMode(errorMode);
    return mDecoder.openFile(filePath);
}

void VDQtFrameDecodeWorker::closeSource() {
    Q_ASSERT(QThread::currentThread() == thread());
    mDecoder.close();
    QMutexLocker lock(&mRequestMutex);
    mRequestedFrame = -1;
}

void VDQtFrameDecodeWorker::setDecompressionConfig(const QString& formatName,
                                                    int colorSpace,
                                                    int componentRange) {
    Q_ASSERT(QThread::currentThread() == thread());
    mDecoder.setDecompressionConfig(formatName, colorSpace, componentRange);
}

void VDQtFrameDecodeWorker::setErrorMode(int errorMode) {
    Q_ASSERT(QThread::currentThread() == thread());
    mDecoder.setErrorMode(errorMode);
}

void VDQtFrameDecodeWorker::setFilterChain(const QList<VDFilterInstance>& chain) {
    Q_ASSERT(QThread::currentThread() == thread());
    mFilters.replaceActiveChainTransient(chain);
}

void VDQtFrameDecodeWorker::requestFrame(int frameIndex,
                                         quint64 generation,
                                         bool preserveSequentialDecode,
                                         bool renderFilteredOutput) {
    bool schedule = false;
    {
        QMutexLocker lock(&mRequestMutex);
        mRequestedFrame = frameIndex;
        mRequestedGeneration = generation;
        mLatestGeneration = generation;
        mRequestedSequential = preserveSequentialDecode;
        mRequestedFilteredOutput = renderFilteredOutput;
        if (!mProcessScheduled) {
            mProcessScheduled = true;
            schedule = true;
        }
    }

    if (schedule) {
        QMetaObject::invokeMethod(
            this, &VDQtFrameDecodeWorker::processPendingRequest, Qt::QueuedConnection);
    }
}

void VDQtFrameDecodeWorker::cancelPending(quint64 generation) {
    QMutexLocker lock(&mRequestMutex);
    mLatestGeneration = generation;
    mRequestedFrame = -1;
}

void VDQtFrameDecodeWorker::processPendingRequest() {
    Q_ASSERT(QThread::currentThread() == thread());

    for (;;) {
        int frameIndex = -1;
        quint64 generation = 0;
        bool preserveSequentialDecode = false;
        bool renderFilteredOutput = false;
        {
            QMutexLocker lock(&mRequestMutex);
            if (mRequestedFrame < 0) {
                mProcessScheduled = false;
                return;
            }
            frameIndex = mRequestedFrame;
            generation = mRequestedGeneration;
            preserveSequentialDecode = mRequestedSequential;
            renderFilteredOutput = mRequestedFilteredOutput;
            mRequestedFrame = -1;
        }

        QImage inputImage = mDecoder.getFrameImage(frameIndex, preserveSequentialDecode);
        QImage outputImage;
        if (!inputImage.isNull() && renderFilteredOutput)
            outputImage = mFilters.processFrame(inputImage);

        bool currentResult = false;
        {
            QMutexLocker lock(&mRequestMutex);
            currentResult = generation == mLatestGeneration;
        }

        if (currentResult) {
            const int status = static_cast<int>(mDecoder.getFrameCountStatus());
            if (!inputImage.isNull()) {
                Q_EMIT frameReady(
                    frameIndex,
                    generation,
                    inputImage,
                    outputImage,
                    mDecoder.isKeyFrame(frameIndex),
                    mDecoder.getFrameTimestampSeconds(frameIndex),
                    mDecoder.getFrameCount(),
                    status,
                    mDecoder.getSeekCount(),
                    mDecoder.getDecodedFrameCount());
            } else {
                Q_EMIT frameUnavailable(
                    frameIndex,
                    generation,
                    mDecoder.getLastError(),
                    mDecoder.getFrameCount(),
                    status);
            }
        }
    }
}
