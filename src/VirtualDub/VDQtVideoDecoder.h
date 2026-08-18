#ifndef VDQTVIDEODECODER_H
#define VDQTVIDEODECODER_H

#include <QString>
#include <QStringList>
#include <QImage>
#include <QCache>
#include <QVector>
#include <functional>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <avisynth/avisynth_c.h>
}

class VDQtVideoDecoder {
public:
    enum class FrameCountStatus {
        Exact,
        Estimated,
        Unknown
    };

    VDQtVideoDecoder();
    ~VDQtVideoDecoder();

    bool openFile(const QString& filePath);
    void close();

    bool isOpen() const { return mIsOpen; }
    QString getFilePath() const { return mFilePath; }
    int getFrameCount() const { return mFrameCount; }
    FrameCountStatus getFrameCountStatus() const { return mFrameCountStatus; }
    bool isFrameCountExact() const { return mFrameCountStatus == FrameCountStatus::Exact; }
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
    QImage getFrameImage(int frameIndex, bool preserveSequentialDecode = false);
    void clearCache();
    qsizetype getCachedFrameCount() const { return mFrameCache.size(); }
    qsizetype getCachedFrameCostKiB() const { return mFrameCache.totalCost(); }
    quint64 getSeekCount() const { return mSeekCount; }
    quint64 getDecodedFrameCount() const { return mDecodedFrameCount; }
    void resetPerformanceCounters() { mSeekCount = 0; mDecodedFrameCount = 0; }
    static constexpr qsizetype getFrameCacheBudgetKiB() { return 64 * 1024; }
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

    VDScanResult scanVideoStream(std::function<bool(int currentFrame, int totalFrames)> progressCallback = nullptr);

    void setDecompressionConfig(const QString &formatName, int colorSpace, int componentRange);
    QString getForcedFormatName() const { return mForcedFormatName; }
    int getColorSpaceMode() const { return mColorSpaceMode; }
    int getComponentRangeMode() const { return mComponentRangeMode; }
    void setErrorMode(int errorMode);
    int getErrorMode() const { return mErrorMode; }

    QString getLastError() const { return mLastError; }

private:
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
    bool decodeNextFrame(int *decodeErrors = nullptr);
    QImage convertDecodedFrameToImage();
    int registerDecodedFrame();
    int findIndexedFrameByTimestamp(int64_t timestamp, int hint) const;
    void updateFrameCountAtEndOfStream();
    void applyErrorMode();
    void cacheFrame(int frameIndex, const QImage& image);

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

    AVFormatContext *mFormatCtx;
    AVCodecContext *mCodecCtx;
    SwsContext *mSwsCtx;
    AVFrame *mFrame;
    AVFrame *mFrameRGB;
    AVPacket *mPacket;
    uint8_t *mBuffer;

    int mCurrentFrameIndex;
    int mNextDecodeFrameIndex;
    int64_t mStreamStartTimestamp;
    int64_t mPendingSeekTargetTimestamp;
    bool mPacketPending;
    bool mDemuxEof;
    bool mDrainSent;
    bool mLastDecodeReachedEof;
    bool mDiscardUntilKeyFrame;
    quint64 mSeekCount;
    quint64 mDecodedFrameCount;
    AVPixelFormat mSwsSourceFormat;
    AVPixelFormat mSwsDestinationFormat;
    AVPixelFormat mOutputPixelFormat;
    QImage::Format mOutputImageFormat;
    int mSwsSourceWidth;
    int mSwsSourceHeight;
    int mSourceBitDepth;
    bool mSourceHasAlpha;
    int mErrorMode;
    bool mIsSyntheticScript = false;
    bool mIsAvsNative = false;

    QString mForcedFormatName = "Autoselect";
    int mColorSpaceMode = 0; // 0: No change, 1: Rec.601, 2: Rec.709
    int mComponentRangeMode = 0; // 0: No change, 1: Limited, 2: Full

    AVS_ScriptEnvironment *mAvsEnv = nullptr;
    AVS_Clip *mAvsClip = nullptr;
    const AVS_VideoInfo *mAvsVi = nullptr;

    QCache<int, QImage> mFrameCache;
    QVector<FrameIndexEntry> mFrameIndex;

    QImage generateSyntheticFrame(int frameIndex);
    QImage renderAvsFrame(int frameIndex);
};

#endif // VDQTVIDEODECODER_H
