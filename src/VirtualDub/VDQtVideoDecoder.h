#ifndef VDQTVIDEODECODER_H
#define VDQTVIDEODECODER_H

#include "VDQtSourceDependencies.h"

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QImage>
#include <QCache>
#include <QHash>
#include <QMutex>
#include <QVector>
#include <functional>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <avisynth/avisynth_c.h>
}

// Random-access video decoder shared by preview, export, analysis, and frame
// serving. Ordinary media uses libavformat/libavcodec; .avs files are evaluated
// directly through AviSynth+ so video and audio can share one script graph.
//
// FFmpeg decoding is presentation-order aware: mFrameIndex records timestamps
// and keyframes as frames are discovered, seeks restart from a safe timestamp,
// and dependent frames are decoded forward. getFrameImage() returns a detached
// QImage cached by frame number. The class is internally guarded by a recursive
// mutex because native AVS audio/video may enter it from separate workers.
class VDQtVideoDecoder {
public:
    // Container frame counts are often estimates. Callers that require an exact
    // end frame must scan to EOF or use ensureExactFrameRange() in the UI.
    enum class FrameCountStatus {
        Exact,
        Estimated,
        Unknown
    };

    VDQtVideoDecoder();
    ~VDQtVideoDecoder();

    // Transactional: an unsuccessful open leaves the object closed and frees
    // all partially constructed FFmpeg/AviSynth resources.
    bool openFile(const QString& filePath);
    void close();

    bool isOpen() const { return mIsOpen; }
    QString getFilePath() const { return mFilePath; }
    int getFrameCount() const { return mFrameCount; }
    FrameCountStatus getFrameCountStatus() const { return mFrameCountStatus; }
    bool isFrameCountExact() const { return mFrameCountStatus == FrameCountStatus::Exact; }
    bool hasCompleteFrameIndex() const {
        return mIsOpen && (mIsAvsNative || mFrameIndexComplete);
    }
    // EOF describes the last decode attempt, not proof of the entire source's
    // length. A sparse timestamp seek can reach EOF with only a prefix indexed.
    bool reachedEndOfStream() const { return mLastDecodeReachedEof; }
    bool isKeyFrame(int frameIndex);
    int getPreviousKeyFrame(int frameIndex);
    int getNextKeyFrame(int frameIndex);
    // Returns NaN when timing cannot be established. Values are relative to the
    // beginning of the video stream, not the container's absolute timestamp.
    double getFrameTimestampSeconds(int frameIndex);
    double getFrameDurationSeconds(int frameIndex);
    double getFps() const { return mFps; }
    int getWidth() const { return mWidth; }
    int getHeight() const { return mHeight; }
    int getSourceBitDepth() const { return mSourceBitDepth; }
    bool sourceHasAlpha() const { return mSourceHasAlpha; }
    bool isAvsNative() const { return mIsAvsNative; }
    AVS_Clip* getAvsClip() const { return mAvsClip; }
    const AVS_VideoInfo* getAvsVi() const { return mAvsVi; }
    // All consumers of the native AviSynth clip share this lock. A number of
    // third-party filters do not support simultaneous GetFrame/GetAudio calls,
    // and evaluating the script a second time is even less safe for plugins
    // with process-global state.
    QRecursiveMutex* getAvsAccessMutex() { return &mAvsAccessMutex; }
    QString getPixFormat() const {
        if (mIsAvsNative && mAvsVi) {
            if (avs_is_yv12(mAvsVi)) return "YV12";
            if (avs_is_yv16(mAvsVi)) return "YV16";
            if (avs_is_yv24(mAvsVi)) return "YV24";
            if (avs_is_yuy2(mAvsVi)) return "YUY2";
            if (avs_is_rgb32(mAvsVi)) return "RGBA32";
            if (avs_is_rgb24(mAvsVi)) return "RGB24";
            return "YUV420";
        }
        if (mCodecCtx) {
            const char* name = av_get_pix_fmt_name(mCodecCtx->pix_fmt);
            if (name) return QString::fromUtf8(name).toUpper();
        }
        return "YUV420";
    }

    // preserveSequentialDecode is used by playback: a late presentation may
    // skip image conversion, but dependency frames are still decoded in order
    // instead of turning every dropped display frame into a random seek.
    // shouldContinue is checked between packets/frames. It must not enter this
    // decoder or pump GUI events; false abandons an obsolete worker request.
    // An individual demux read or third-party AVS call cannot be preempted.
    QImage getFrameImage(int frameIndex, bool preserveSequentialDecode = false,
                         std::function<bool()> shouldContinue = nullptr);
    void clearCache();
    qsizetype getCachedFrameCount() const { return mFrameCache.size(); }
    qsizetype getCachedFrameCostKiB() const { return mFrameCache.totalCost(); }
    quint64 getSeekCount() const { return mSeekCount; }
    quint64 getDecodedFrameCount() const { return mDecodedFrameCount; }
    quint64 getIndexLookupWorkCount() const { return mIndexLookupWorkCount; }
    void resetPerformanceCounters() {
        mSeekCount = 0; mDecodedFrameCount = 0; mIndexLookupWorkCount = 0;
    }
    static qsizetype getFrameCacheBudgetKiB();
    static void setFrameCacheBudgetMiB(int budgetMiB);
    static int getDecoderThreadCount();
    static void setDecoderThreadCount(int threadCount);
    void applyFrameCacheBudget();
    using ScriptDependencyReport = VDQtScriptDependencyReport;
    QString getInputFormatName() const;
    static ScriptDependencyReport auditScriptDependencies(const QString& scriptPath);
    static QString parseScriptSource(const QString& scriptPath);
    static QStringList parseScriptSources(const QString& scriptPath);

    struct VDScanResult {
        int totalFrames = 0;
        int badFrames = 0;
        int maskedFrames = 0;
        int keyFrames = 0;
        bool cancelled = false;
        QString errorMessage;
    };

    VDScanResult scanVideoStream(std::function<bool(int currentFrame, int totalFrames)> progressCallback = nullptr,
                                std::function<bool()> shouldContinue = nullptr);
    // Navigation/export need the verified index, not a fresh health analysis.
    // Reuse a complete source-owned index; cached results report length only.
    // scanVideoStream() remains the explicit, always-fresh error scan.
    VDScanResult ensureFrameIndex(std::function<bool(int currentFrame, int totalFrames)> progressCallback = nullptr,
                                 std::function<bool()> shouldContinue = nullptr);

    void setDecompressionConfig(const QString &formatName, int colorSpace, int componentRange);
    QString getForcedFormatName() const { return mForcedFormatName; }
    int getColorSpaceMode() const { return mColorSpaceMode; }
    int getComponentRangeMode() const { return mComponentRangeMode; }
    void setErrorMode(int errorMode);
    int getErrorMode() const { return mErrorMode; }

    QString getLastError() const { return mLastError; }

private:
    // One presentation-order observation. Entries are gradually refined as
    // decoding reaches parts of files whose container metadata was incomplete.
    struct FrameIndexEntry {
        int64_t timestamp = AV_NOPTS_VALUE;
        int64_t duration = 0;
        bool keyFrame = false;
    };

    bool setupSwsContext(AVPixelFormat sourceFormat = AV_PIX_FMT_NONE,
                         int sourceWidth = 0,
                         int sourceHeight = 0,
                         AVPixelFormat destinationFormat = AV_PIX_FMT_RGB24);
    bool ensureConversionResources(const AVFrame *sourceFrame);
    bool seekToFrame(int frameIndex);
    bool resetDecoderToStart();
    bool decodeNextFrame(int *decodeErrors = nullptr,
                         const std::function<bool()>& shouldContinue = nullptr);
    QImage convertDecodedFrameToImage();
    int registerDecodedFrame();
    int findIndexedFrameByTimestamp(int64_t timestamp, int hint, bool requireUnique = false);
    void registerIndexedTimestamp(int64_t timestamp, int frameIndex);
    void updateFrameCountAtEndOfStream();
    void applyErrorMode();
    void cacheFrame(int frameIndex, const QImage& image);

    // Public source metadata and last diagnostic.
    bool mIsOpen;
    QString mFilePath;
    QString mLastError;
    int mWidth;
    int mHeight;
    int mFrameCount;
    FrameCountStatus mFrameCountStatus;
    double mFps;
    int mVideoStreamIndex;
    int64_t mDuration;

    // FFmpeg ownership. Frames/packets belong exclusively to this decoder.
    AVFormatContext *mFormatCtx;
    AVCodecContext *mCodecCtx;
    SwsContext *mSwsCtx;
    AVFrame *mFrame;
    AVFrame *mFrameRGB;
    AVPacket *mPacket;
    // Owns the allocation backing mFrameRGB. mFrameRGB->data[0] is aligned
    // within this allocation and intentionally has padded rows and tail room.
    uint8_t *mBuffer;

    // Sequential decode/seek state. A packet may remain pending while the codec
    // emits multiple frames, hence mPacketPending is distinct from demux EOF.
    int mCurrentFrameIndex;
    int mNextDecodeFrameIndex;
    int64_t mStreamStartTimestamp;
    int64_t mPendingSeekTargetTimestamp;
    bool mPacketPending;
    bool mDemuxEof;
    bool mDrainSent;
    bool mLastDecodeReachedEof;
    bool mDecodeCancelled = false;
    bool mIndexTraversalContiguous;
    bool mDiscardUntilKeyFrame;
    quint64 mSeekCount;
    quint64 mDecodedFrameCount;
    quint64 mIndexLookupWorkCount = 0;
    // swscale configuration and destination image format.
    AVPixelFormat mSwsSourceFormat;
    AVPixelFormat mSwsDestinationFormat;
    AVPixelFormat mOutputPixelFormat;
    QImage::Format mOutputImageFormat;
    int mSwsSourceWidth;
    int mSwsSourceHeight;
    int mSourceBitDepth;
    bool mSourceHasAlpha;
    int mErrorMode;
    bool mIsAvsNative = false;

    QString mForcedFormatName = "Autoselect";
    int mColorSpaceMode = 0; // 0: No change, 1: Rec.601, 2: Rec.709
    int mComponentRangeMode = 0; // 0: No change, 1: Limited, 2: Full

    // Native AviSynth backend. mAvsClip is released before mAvsEnv in close().
    AVS_ScriptEnvironment *mAvsEnv = nullptr;
    AVS_Clip *mAvsClip = nullptr;
    const AVS_VideoInfo *mAvsVi = nullptr;
    QRecursiveMutex mAvsAccessMutex;
    // swscale's SIMD packed-RGB converters may store a complete vector at the
    // end of the final scanline. Native AviSynth frames are converted through
    // this aligned, explicitly tail-padded buffer before being copied to
    // QImage; ordinary FFmpeg frames use the same layout through mBuffer.
    QByteArray mAvsConversionBuffer;

    // QCache cost is KiB, not entry count; this bounds memory for large frames.
    QCache<int, QImage> mFrameCache;
    QVector<FrameIndexEntry> mFrameIndex;
    bool mFrameIndexComplete = false;
    // Built only when a seek needs timestamp reconciliation. Sequential index
    // growth requires neither an all-prefix search nor this additional storage.
    // A timestamp can belong to several distinct presentation ordinals.
    QHash<qint64, QVector<int>> mFrameTimestampLookup;
    bool mFrameTimestampLookupReady = false;

    QImage renderAvsFrame(int frameIndex);
};

#endif // VDQTVIDEODECODER_H
