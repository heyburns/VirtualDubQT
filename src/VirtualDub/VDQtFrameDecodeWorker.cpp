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
    mSharedAvsDecoder = nullptr;
    mDecoder.close();
    mDecoder.setDecompressionConfig(formatName, colorSpace, componentRange);
    mDecoder.setErrorMode(errorMode);
    return mDecoder.openFile(filePath);
}

bool VDQtFrameDecodeWorker::useSharedAvsSource(VDQtVideoDecoder *decoder) {
    Q_ASSERT(QThread::currentThread() == thread());
    mDecoder.close();
    if (!decoder || !decoder->isOpen() || !decoder->isAvsNative()) {
        mSharedAvsDecoder = nullptr;
        return false;
    }
    mSharedAvsDecoder = decoder;
    return true;
}

void VDQtFrameDecodeWorker::closeSource() {
    Q_ASSERT(QThread::currentThread() == thread());
    mSharedAvsDecoder = nullptr;
    mDecoder.close();
    QMutexLocker lock(&mRequestMutex);
    mRequestedFrame = -1;
}

QString VDQtFrameDecodeWorker::lastError() const {
    const VDQtVideoDecoder *decoder = activeDecoder();
    return decoder ? decoder->getLastError()
                   : QStringLiteral("No interactive decoder is open.");
}

VDQtVideoDecoder* VDQtFrameDecodeWorker::activeDecoder() {
    return mSharedAvsDecoder ? mSharedAvsDecoder : &mDecoder;
}

const VDQtVideoDecoder* VDQtFrameDecodeWorker::activeDecoder() const {
    return mSharedAvsDecoder ? mSharedAvsDecoder : &mDecoder;
}

void VDQtFrameDecodeWorker::setDecompressionConfig(const QString& formatName,
                                                    int colorSpace,
                                                    int componentRange) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!mSharedAvsDecoder)
        mDecoder.setDecompressionConfig(formatName, colorSpace, componentRange);
}

void VDQtFrameDecodeWorker::setErrorMode(int errorMode) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!mSharedAvsDecoder) mDecoder.setErrorMode(errorMode);
}

void VDQtFrameDecodeWorker::applyFrameCacheBudget() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!mSharedAvsDecoder) mDecoder.applyFrameCacheBudget();
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

        VDQtVideoDecoder *decoder = activeDecoder();
        if (!decoder || !decoder->isOpen()) {
            QMutexLocker lock(&mRequestMutex);
            mProcessScheduled = false;
            return;
        }

        QImage inputImage = decoder->getFrameImage(frameIndex, preserveSequentialDecode);
        QList<QImage> outputImages;
        if (!inputImage.isNull() && renderFilteredOutput) {
            VDFilterFrameContext context;
            context.frameNumber = frameIndex;
            context.timestampSeconds =
                decoder->getFrameTimestampSeconds(frameIndex);
            context.frameRate = decoder->getFps();
            if (!mFilters.processFrameSequence(inputImage, outputImages, context))
                outputImages.clear();
        }

        bool currentResult = false;
        {
            QMutexLocker lock(&mRequestMutex);
            currentResult = generation == mLatestGeneration;
        }

        if (currentResult) {
            const int status = static_cast<int>(decoder->getFrameCountStatus());
            if (!inputImage.isNull()) {
                Q_EMIT frameReady(
                    frameIndex,
                    generation,
                    inputImage,
                    outputImages,
                    decoder->isKeyFrame(frameIndex),
                    decoder->getFrameTimestampSeconds(frameIndex),
                    decoder->getFrameDurationSeconds(frameIndex),
                    decoder->getFrameCount(),
                    status,
                    decoder->getSeekCount(),
                    decoder->getDecodedFrameCount());
            } else {
                Q_EMIT frameUnavailable(
                    frameIndex,
                    generation,
                    decoder->getLastError(),
                    decoder->getFrameCount(),
                    status);
            }
        }
    }
}
