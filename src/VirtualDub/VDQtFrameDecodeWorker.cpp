// Dedicated interactive-preview worker. Requests may arrive from the GUI faster
// than decoding, so they are collapsed into one latest-request slot protected by
// mRequestMutex. Generation tokens keep late results from repainting stale UI.
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
    // Normal media gets a decoder owned exclusively by this worker. Closing it
    // here also ensures no codec context survives a main-window source change.
    mSharedAvsDecoder = nullptr;
    mDecoder.close();
    mDecoder.setDecompressionConfig(formatName, colorSpace, componentRange);
    mDecoder.setErrorMode(errorMode);
    return mDecoder.openFile(filePath);
}

bool VDQtFrameDecodeWorker::useSharedAvsSource(VDQtVideoDecoder *decoder) {
    Q_ASSERT(QThread::currentThread() == thread());
    // Native AviSynth sources cannot safely be opened as a second independent
    // graph by every preview consumer. In this mode MainWindow serializes use
    // of its decoder and guarantees that the borrowed pointer outlives us.
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
    // Source-owned temporal history and cached assets can be large. Preserve
    // the chain configuration, but release its runtime when the source closes.
    const auto chain = mFilters.getActiveChain();
    mFilters.clearFilters();
    mFilters.replaceActiveChainTransient(chain);
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

bool VDQtFrameDecodeWorker::adoptFrameIndexSnapshot(
    const VDQtVideoDecoder::FrameIndexSnapshotPtr& snapshot) {
    Q_ASSERT(QThread::currentThread() == thread());
    return !mSharedAvsDecoder && mDecoder.adoptFrameIndexSnapshot(snapshot);
}

void VDQtFrameDecodeWorker::requestFrame(int frameIndex,
                                         quint64 generation,
                                         bool preserveSequentialDecode,
                                         bool renderFilteredOutput) {
    // This entry point may be called directly by the GUI thread. It performs no
    // decode work: it replaces the one pending slot, then posts at most one
    // event to the worker thread. That is the key to responsive scrubbing.
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

    // Drain until no request remains. A request arriving during decode replaces
    // the pending slot and is picked up by the next iteration without growing a
    // potentially unbounded Qt event queue.
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

        // The mutex is intentionally released for decoding and filtering. The
        // GUI remains free to publish a newer request while this one is costly.
        VDQtVideoDecoder *decoder = activeDecoder();
        if (!decoder || !decoder->isOpen()) {
            QMutexLocker lock(&mRequestMutex);
            mProcessScheduled = false;
            return;
        }

        const auto stillRequested = [this, generation] {
            QMutexLocker lock(&mRequestMutex);
            return generation == mLatestGeneration;
        };
        QImage inputImage = decoder->getFrameImage(
            frameIndex, preserveSequentialDecode, stillRequested);
        QList<QImage> outputImages;
        if (!inputImage.isNull() && renderFilteredOutput && stillRequested()) {
            VDFilterFrameContext context;
            context.frameNumber = frameIndex;
            context.timestampSeconds =
                decoder->getFrameTimestampSeconds(frameIndex);
            context.frameRate = decoder->getFps();
            if (!mFilters.processFrameSequence(inputImage, outputImages, context))
                outputImages.clear();
        }

        // Packet/frame decoding is cancellable as newer generations arrive.
        // Check again after filtering: an individual plugin/filter call is not
        // preemptible, and no stale result may repaint or move the playhead.
        bool currentResult = false;
        {
            QMutexLocker lock(&mRequestMutex);
            currentResult = generation == mLatestGeneration;
        }

        if (currentResult) {
            // Complete snapshots are cached and immutable. Publishing a shared
            // pointer is cheap; partial growing prefixes are never copied here.
            if (const auto snapshot = decoder->frameIndexSnapshot())
                Q_EMIT frameIndexAvailable(generation, snapshot);
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
                    status,
                    decoder->reachedEndOfStream());
            }
        }
    }
}
