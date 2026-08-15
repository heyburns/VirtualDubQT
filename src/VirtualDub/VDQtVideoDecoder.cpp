#include "VDQtVideoDecoder.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QFont>
#include <QThread>
#include <algorithm>

VDQtVideoDecoder::VDQtVideoDecoder()
    : mIsOpen(false),
      mWidth(0),
      mHeight(0),
      mFrameCount(0),
      mFps(30.0),
      mVideoStreamIndex(-1),
      mDuration(0),
      mFormatCtx(nullptr),
      mCodecCtx(nullptr),
      mSwsCtx(nullptr),
      mFrame(nullptr),
      mFrameRGB(nullptr),
      mBuffer(nullptr),
      mCurrentFrameIndex(-1) {
}

VDQtVideoDecoder::~VDQtVideoDecoder() {
    close();
    if (mAvsEnv) {
        avs_delete_script_environment(mAvsEnv);
        mAvsEnv = nullptr;
    }
}

QString VDQtVideoDecoder::parseScriptSource(const QString& scriptPath) {
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();

    QTextStream in(&scriptFile);
    QString content = in.readAll();
    scriptFile.close();

    content.replace('\\', '/'); // Convert Windows backslashes

    QRegularExpression srcRegex("(?:AVISource|FFVideoSource|FFAudioSource|FFmpegSource2|LWLibavVideoSource|LWLibavAudioSource|DirectShowSource|SegmentedAVISource|OpenDMLSource|ImageSource|MovieSource)\\s*\\(\\s*[\"']([^\"']+)[\"']", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = srcRegex.match(content);

    if (match.hasMatch()) {
        QString refPath = match.captured(1);
        QFileInfo scriptInfo(scriptPath);
        QDir scriptDir = scriptInfo.dir();
        QString resolvedPath = scriptDir.filePath(refPath);

        if (!QFile::exists(resolvedPath) && QFile::exists(refPath)) {
            resolvedPath = refPath;
        }

        if (QFile::exists(resolvedPath)) {
            return resolvedPath;
        }
    }
    return QString();
}

QImage VDQtVideoDecoder::generateSyntheticFrame(int frameIndex) {
    QImage img(mWidth > 0 ? mWidth : 1920, mHeight > 0 ? mHeight : 1080, QImage::Format_RGB888);
    img.fill(QColor(24, 24, 32));

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);

    // Render SMPTE Color Bars
    int barWidth = img.width() / 7;
    QColor colors[7] = {
        Qt::white, Qt::yellow, Qt::cyan, Qt::green,
        Qt::magenta, Qt::red, Qt::blue
    };

    for (int i = 0; i < 7; i++) {
        painter.fillRect(i * barWidth, 0, barWidth, img.height() * 0.7, colors[i]);
    }

    // Text Overlay: AviSynth Script Generator Info
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPixelSize(28);
    font.setBold(true);
    painter.setFont(font);

    QString text = QString("AviSynth Script Preview | Frame %1 / %2 (%3 FPS)")
        .arg(frameIndex)
        .arg(mFrameCount)
        .arg(mFps, 0, 'f', 2);

    painter.drawText(img.rect().adjusted(20, static_cast<int>(img.height() * 0.7) + 20, -20, -20), Qt::AlignLeft | Qt::AlignTop, text);
    painter.end();

    return img;
}

QImage VDQtVideoDecoder::renderAvsFrame(int frameIndex) {
    if (!mAvsClip || !mAvsVi) return QImage();

    AVS_VideoFrame *frame = avs_get_frame(mAvsClip, frameIndex);
    if (!frame) return QImage();

    int w = mAvsVi->width;
    int h = mAvsVi->height;

    QImage img(w, h, QImage::Format_RGB32);

    AVPixelFormat srcFmt = AV_PIX_FMT_NONE;
    const uint8_t *srcSlice[4] = { nullptr };
    int srcStride[4] = { 0 };

    if (avs_is_yv12(mAvsVi)) {
        srcFmt = AV_PIX_FMT_YUV420P;
        srcSlice[0] = avs_get_read_ptr_p(frame, AVS_PLANAR_Y);
        srcStride[0] = avs_get_pitch_p(frame, AVS_PLANAR_Y);
        srcSlice[1] = avs_get_read_ptr_p(frame, AVS_PLANAR_U);
        srcStride[1] = avs_get_pitch_p(frame, AVS_PLANAR_U);
        srcSlice[2] = avs_get_read_ptr_p(frame, AVS_PLANAR_V);
        srcStride[2] = avs_get_pitch_p(frame, AVS_PLANAR_V);
    } else if (avs_is_yv16(mAvsVi)) {
        srcFmt = AV_PIX_FMT_YUV422P;
        srcSlice[0] = avs_get_read_ptr_p(frame, AVS_PLANAR_Y);
        srcStride[0] = avs_get_pitch_p(frame, AVS_PLANAR_Y);
        srcSlice[1] = avs_get_read_ptr_p(frame, AVS_PLANAR_U);
        srcStride[1] = avs_get_pitch_p(frame, AVS_PLANAR_U);
        srcSlice[2] = avs_get_read_ptr_p(frame, AVS_PLANAR_V);
        srcStride[2] = avs_get_pitch_p(frame, AVS_PLANAR_V);
    } else if (avs_is_yv24(mAvsVi)) {
        srcFmt = AV_PIX_FMT_YUV444P;
        srcSlice[0] = avs_get_read_ptr_p(frame, AVS_PLANAR_Y);
        srcStride[0] = avs_get_pitch_p(frame, AVS_PLANAR_Y);
        srcSlice[1] = avs_get_read_ptr_p(frame, AVS_PLANAR_U);
        srcStride[1] = avs_get_pitch_p(frame, AVS_PLANAR_U);
        srcSlice[2] = avs_get_read_ptr_p(frame, AVS_PLANAR_V);
        srcStride[2] = avs_get_pitch_p(frame, AVS_PLANAR_V);
    } else if (avs_is_yuy2(mAvsVi)) {
        srcFmt = AV_PIX_FMT_YUYV422;
        srcSlice[0] = avs_get_read_ptr_p(frame, 0);
        srcStride[0] = avs_get_pitch_p(frame, 0);
    } else if (avs_is_rgb32(mAvsVi)) {
        srcFmt = AV_PIX_FMT_BGRA;
        srcSlice[0] = avs_get_read_ptr_p(frame, 0);
        srcStride[0] = avs_get_pitch_p(frame, 0);
    } else if (avs_is_rgb24(mAvsVi)) {
        srcFmt = AV_PIX_FMT_BGR24;
        srcSlice[0] = avs_get_read_ptr_p(frame, 0);
        srcStride[0] = avs_get_pitch_p(frame, 0);
    }

    if (srcFmt != AV_PIX_FMT_NONE && srcSlice[0]) {
        if (!mSwsCtx) {
            mSwsCtx = sws_getContext(w, h, srcFmt, w, h, AV_PIX_FMT_BGRA, SWS_POINT, nullptr, nullptr, nullptr);
        }
        if (mSwsCtx) {
            uint8_t *dstSlice[4] = { img.bits(), nullptr, nullptr, nullptr };
            int dstStride[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
            sws_scale(mSwsCtx, srcSlice, srcStride, 0, h, dstSlice, dstStride);
            avs_release_video_frame(frame);
            return img;
        }
    }

    img.fill(Qt::black);
    avs_release_video_frame(frame);
    return img;
}

void VDQtVideoDecoder::setupSwsContext() {
    if (!mCodecCtx || mWidth <= 0 || mHeight <= 0) return;

    if (mSwsCtx) {
        sws_freeContext(mSwsCtx);
        mSwsCtx = nullptr;
    }

    mSwsCtx = sws_getContext(
        mWidth, mHeight, mCodecCtx->pix_fmt,
        mWidth, mHeight, AV_PIX_FMT_RGB24,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!mSwsCtx) return;

    int srcRange = 0;
    if (mComponentRangeMode == 1) {
        srcRange = 0; // Limited (16-235)
    } else if (mComponentRangeMode == 2) {
        srcRange = 1; // Full (0-255)
    } else {
        if (mCodecCtx->color_range == AVCOL_RANGE_JPEG) srcRange = 1;
        else srcRange = 0;
    }

    const int *invTable = nullptr;
    if (mColorSpaceMode == 1) {
        invTable = sws_getCoefficients(SWS_CS_ITU601);
    } else if (mColorSpaceMode == 2) {
        invTable = sws_getCoefficients(SWS_CS_ITU709);
    } else {
        if (mWidth >= 1280 || mHeight >= 720) {
            invTable = sws_getCoefficients(SWS_CS_ITU709);
        } else {
            invTable = sws_getCoefficients(SWS_CS_ITU601);
        }
    }

    const int *table = sws_getCoefficients(SWS_CS_DEFAULT);
    sws_setColorspaceDetails(
        mSwsCtx,
        invTable, srcRange,
        table, 1, // destination full range RGB
        0, 1 << 16, 1 << 16
    );
}

void VDQtVideoDecoder::setDecompressionConfig(const QString &formatName, int colorSpace, int componentRange) {
    mForcedFormatName = formatName;
    mColorSpaceMode = colorSpace;
    mComponentRangeMode = componentRange;

    clearCache();
    setupSwsContext();
}

bool VDQtVideoDecoder::openFile(const QString& filePath) {
    close();

    mFilePath = filePath;
    std::string pathStr = filePath.toStdString();

    AVInputFormat *inputFmt = nullptr;
    AVDictionary *formatOptions = nullptr;

    bool isAvs = filePath.endsWith(".avs", Qt::CaseInsensitive);

    if (isAvs) {
        qDebug() << "[VDQtVideoDecoder] Attempting native AviSynth+ script evaluation:" << filePath;
        QDir::setCurrent(QFileInfo(filePath).absolutePath());
        if (!mAvsEnv) {
            mAvsEnv = avs_create_script_environment(AVISYNTH_INTERFACE_VERSION);
        }
        if (mAvsEnv) {
            QByteArray pathBytes = filePath.toUtf8();
            AVS_Value val = avs_new_value_string(pathBytes.constData());
            AVS_Value res = avs_invoke(mAvsEnv, "Import", val, nullptr);
            qDebug() << "[VDQtVideoDecoder] avs_invoke Import returned: type=" << res.type
                     << "is_clip=" << avs_is_clip(res)
                     << "is_error=" << avs_is_error(res);

            if (!avs_is_error(res) && avs_is_clip(res)) {
                mAvsClip = avs_take_clip(res, mAvsEnv);
                mAvsVi = avs_get_video_info(mAvsClip);
                qDebug() << "[VDQtVideoDecoder] mAvsClip taken:" << (void*)mAvsClip << "mAvsVi:" << (void*)mAvsVi;

                if (mAvsVi && mAvsVi->width > 0 && mAvsVi->height > 0) {
                    mWidth = mAvsVi->width;
                    mHeight = mAvsVi->height;
                    mFrameCount = mAvsVi->num_frames;
                    mFps = (mAvsVi->fps_denominator > 0) ? (double)mAvsVi->fps_numerator / mAvsVi->fps_denominator : 29.97;
                    mIsOpen = true;
                    mIsAvsNative = true;
                    mCurrentFrameIndex = -1;
                    mFrameCache.setMaxCost(128);

                    qDebug() << "[VDQtVideoDecoder] Native AviSynth+ script evaluation SUCCESS:" << filePath
                             << mWidth << "x" << mHeight << "@" << mFps << "fps," << mFrameCount << "total frames.";
                    return true;
                }
            } else if (avs_is_error(res)) {
                mLastError = QString::fromUtf8(avs_as_string(res));
                qWarning() << "[VDQtVideoDecoder] AviSynth+ script evaluation error:" << mLastError;
                avs_release_value(res);
                close();
                return false;
            }
        }

        if (mLastError.isEmpty()) {
            mLastError = "Failed to initialize AviSynth+ script environment or load script.";
        }
        close();
        return false;
    }

    int err = avformat_open_input(&mFormatCtx, pathStr.c_str(), inputFmt, &formatOptions);
    if (formatOptions) {
        av_dict_free(&formatOptions);
    }

    if (err < 0) {
        char errBuf[256];
        av_strerror(err, errBuf, sizeof(errBuf));
        qWarning() << "[VDQtVideoDecoder] Could not open file or script:" << filePath << "Error:" << errBuf;
        close();
        return false;
    }

    if (avformat_find_stream_info(mFormatCtx, nullptr) < 0) {
        qWarning() << "[VDQtVideoDecoder] Could not find stream info:" << filePath;
        close();
        return false;
    }

    // Find first video stream
    for (unsigned int i = 0; i < mFormatCtx->nb_streams; i++) {
        if (mFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            mVideoStreamIndex = i;
            break;
        }
    }

    if (mVideoStreamIndex == -1) {
        qWarning() << "[VDQtVideoDecoder] No video stream found in:" << filePath;
        close();
        return false;
    }

    AVStream *videoStream = mFormatCtx->streams[mVideoStreamIndex];
    const AVCodec *codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        qWarning() << "[VDQtVideoDecoder] Unsupported codec for video stream.";
        close();
        return false;
    }

    mCodecCtx = avcodec_alloc_context3(codec);
    if (!mCodecCtx) {
        close();
        return false;
    }

    if (avcodec_parameters_to_context(mCodecCtx, videoStream->codecpar) < 0) {
        close();
        return false;
    }

    if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
        qWarning() << "[VDQtVideoDecoder] Could not open codec.";
        close();
        return false;
    }

    mWidth = mCodecCtx->width;
    mHeight = mCodecCtx->height;

    // Calculate FPS
    if (videoStream->avg_frame_rate.den > 0) {
        mFps = av_q2d(videoStream->avg_frame_rate);
    } else if (videoStream->r_frame_rate.den > 0) {
        mFps = av_q2d(videoStream->r_frame_rate);
    } else {
        mFps = 30.0;
    }

    // Calculate total frame count
    if (videoStream->nb_frames > 0) {
        mFrameCount = videoStream->nb_frames;
    } else if (mFormatCtx->duration != AV_NOPTS_VALUE) {
        double durationSec = mFormatCtx->duration / (double)AV_TIME_BASE;
        mFrameCount = static_cast<int>(durationSec * mFps);
    } else {
        mFrameCount = 1000;
    }

    if (mFrameCount <= 0) mFrameCount = 1;

    mFrame = av_frame_alloc();
    mFrameRGB = av_frame_alloc();

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, mWidth, mHeight, 1);
    mBuffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));

    av_image_fill_arrays(mFrameRGB->data, mFrameRGB->linesize, mBuffer, AV_PIX_FMT_RGB24, mWidth, mHeight, 1);

    setupSwsContext();

    mIsOpen = true;
    mCurrentFrameIndex = -1;
    mFrameCache.setMaxCost(128); // Cache up to 128 frames for instant scrubbing

    qDebug() << "[VDQtVideoDecoder] Opened file:" << filePath
             << mWidth << "x" << mHeight
             << "@" << mFps << "fps,"
             << mFrameCount << "total frames.";

    return true;
}

void VDQtVideoDecoder::clearCache() {
    mFrameCache.clear();
}

void VDQtVideoDecoder::close() {
    clearCache();

    if (mAvsClip) {
        avs_release_clip(mAvsClip);
        mAvsClip = nullptr;
        mAvsVi = nullptr;
    }

    if (mAvsEnv) {
        avs_delete_script_environment(mAvsEnv);
        mAvsEnv = nullptr;
        QThread::msleep(30);
    }

    if (mSwsCtx) {
        sws_freeContext(mSwsCtx);
        mSwsCtx = nullptr;
    }

    if (mBuffer) {
        av_free(mBuffer);
        mBuffer = nullptr;
    }

    if (mFrameRGB) {
        av_frame_free(&mFrameRGB);
        mFrameRGB = nullptr;
    }

    if (mFrame) {
        av_frame_free(&mFrame);
        mFrame = nullptr;
    }

    if (mCodecCtx) {
        avcodec_free_context(&mCodecCtx);
        mCodecCtx = nullptr;
    }

    if (mFormatCtx) {
        avformat_close_input(&mFormatCtx);
        mFormatCtx = nullptr;
    }

    mIsOpen = false;
    mIsSyntheticScript = false;
    mIsAvsNative = false;
    mWidth = 0;
    mHeight = 0;
    mFrameCount = 0;
    mVideoStreamIndex = -1;
    mCurrentFrameIndex = -1;
}

QImage VDQtVideoDecoder::getFrameImage(int frameIndex) {
    if (!mIsOpen || frameIndex < 0) return QImage();

    frameIndex = std::min(frameIndex, mFrameCount - 1);

    // Native AviSynth+ C-API script frame rendering
    if (mIsAvsNative) {
        if (QImage *cached = mFrameCache.object(frameIndex)) {
            return *cached;
        }
        QImage img = renderAvsFrame(frameIndex);
        if (!img.isNull()) {
            mFrameCache.insert(frameIndex, new QImage(img));
        }
        return img;
    }

    // Synthetic script (e.g. ColorBars, BlankClip, Version) generator preview
    if (mIsSyntheticScript) {
        return generateSyntheticFrame(frameIndex);
    }

    // Instant cache lookup for zero-latency timeline scrubbing
    if (QImage *cached = mFrameCache.object(frameIndex)) {
        return *cached;
    }

    AVStream *videoStream = mFormatCtx->streams[mVideoStreamIndex];

    // Calculate target timestamp (PTS) in stream time_base
    double targetSec = frameIndex / mFps;
    int64_t targetPts = static_cast<int64_t>(targetSec / av_q2d(videoStream->time_base));

    // If seeking backward or jumping non-sequentially, seek to keyframe first
    if (frameIndex != mCurrentFrameIndex + 1 || frameIndex == 0) {
        av_seek_frame(mFormatCtx, mVideoStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(mCodecCtx);
    }

    AVPacket packet;
    bool frameDecoded = false;
    QImage resultImage;

    while (av_read_frame(mFormatCtx, &packet) >= 0) {
        if (packet.stream_index == mVideoStreamIndex) {
            int ret = avcodec_send_packet(mCodecCtx, &packet);
            if (ret >= 0) {
                while (avcodec_receive_frame(mCodecCtx, mFrame) == 0) {
                    int64_t currentPts = mFrame->pts;
                    if (currentPts == AV_NOPTS_VALUE) {
                        currentPts = mFrame->best_effort_timestamp;
                    }

                    // Convert current frame PTS to frame index
                    int currentFrameIndex = -1;
                    if (currentPts != AV_NOPTS_VALUE) {
                        double currentSec = currentPts * av_q2d(videoStream->time_base);
                        currentFrameIndex = static_cast<int>(std::round(currentSec * mFps));
                    }

                    // Decode & convert to RGB
                    sws_scale(
                        mSwsCtx,
                        (const uint8_t *const *)mFrame->data,
                        mFrame->linesize,
                        0,
                        mHeight,
                        mFrameRGB->data,
                        mFrameRGB->linesize
                    );

                    int decodedIdx = (currentFrameIndex >= 0) ? currentFrameIndex : (mCurrentFrameIndex + 1);
                    mCurrentFrameIndex = decodedIdx;

                    QImage img(mBuffer, mWidth, mHeight, mFrameRGB->linesize[0], QImage::Format_RGB888);
                    QImage deepCopy = img.copy();

                    // Insert decoded frames into LRU cache as we encounter them
                    mFrameCache.insert(decodedIdx, new QImage(deepCopy));

                    if (decodedIdx >= frameIndex || currentPts >= targetPts || mCurrentFrameIndex == frameIndex) {
                        resultImage = deepCopy;
                        frameDecoded = true;
                        break;
                    }
                }
            }
        }
        av_packet_unref(&packet);
        if (frameDecoded) break;
    }

    return resultImage;
}

VDQtVideoDecoder::VDScanResult VDQtVideoDecoder::scanVideoStream(std::function<bool(int currentFrame, int totalFrames)> progressCallback) {
    VDScanResult res;
    if (!mIsOpen) {
        res.errorMessage = "No video stream is currently loaded.";
        return res;
    }

    res.totalFrames = mFrameCount;

    if (mIsAvsNative && mAvsClip) {
        bool lastValid = true;
        for (int i = 0; i < mFrameCount; ++i) {
            if (progressCallback && !progressCallback(i + 1, mFrameCount)) {
                res.cancelled = true;
                break;
            }

            AVS_VideoFrame *frame = avs_get_frame(mAvsClip, i);
            if (!frame) {
                res.badFrames++;
                res.maskedFrames++;
                lastValid = false;
            } else {
                avs_release_video_frame(frame);
                lastValid = true;
            }
        }
        return res;
    }

    if (mIsSyntheticScript) {
        for (int i = 0; i < mFrameCount; ++i) {
            if (progressCallback && !progressCallback(i + 1, mFrameCount)) {
                res.cancelled = true;
                break;
            }
        }
        return res;
    }

    if (!mFormatCtx || mVideoStreamIndex < 0 || !mCodecCtx) {
        res.errorMessage = "Codec context is not valid.";
        return res;
    }

    // Seek to beginning and flush decoder
    av_seek_frame(mFormatCtx, mVideoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(mCodecCtx);

    AVStream *videoStream = mFormatCtx->streams[mVideoStreamIndex];
    AVPacket packet;
    int decodedCount = 0;
    bool lastValid = true;

    while (av_read_frame(mFormatCtx, &packet) >= 0) {
        if (packet.stream_index == mVideoStreamIndex) {
            bool isKey = (packet.flags & AV_PKT_FLAG_KEY) != 0;
            if (isKey) res.keyFrames++;

            int sendRet = avcodec_send_packet(mCodecCtx, &packet);
            if (sendRet < 0) {
                res.badFrames++;
                res.maskedFrames++;
                lastValid = false;
            } else {
                while (avcodec_receive_frame(mCodecCtx, mFrame) == 0) {
                    decodedCount++;
                    if (progressCallback && !progressCallback(decodedCount, mFrameCount)) {
                        res.cancelled = true;
                        av_packet_unref(&packet);
                        goto scan_done;
                    }

                    if (!lastValid && !isKey) {
                        res.maskedFrames++;
                    } else {
                        lastValid = true;
                    }
                }
            }
        }
        av_packet_unref(&packet);
    }

    // Flush remaining frames from codec
    avcodec_send_packet(mCodecCtx, nullptr);
    while (avcodec_receive_frame(mCodecCtx, mFrame) == 0) {
        decodedCount++;
        if (progressCallback && !progressCallback(decodedCount, mFrameCount)) {
            res.cancelled = true;
            break;
        }
    }

scan_done:
    // Restore decoder state
    av_seek_frame(mFormatCtx, mVideoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(mCodecCtx);
    mCurrentFrameIndex = -1;
    clearCache();

    if (decodedCount > 0 && res.totalFrames < decodedCount) {
        res.totalFrames = decodedCount;
    }

    return res;
}
