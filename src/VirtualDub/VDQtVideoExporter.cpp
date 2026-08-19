#include "VDQtVideoExporter.h"
#include "VDQtCodecSettings.h"
#include "VDQtCodecEngine.h"
#include "VDQtAudioPlayer.h"
#include "VDQtSourceSafety.h"
#include <QProcess>
#include <QProgressDialog>
#include <QApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <cmath>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <memory>
#include <utility>
#include "VDQtDialogs.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {

constexpr qint64 kMaxQueuedFfmpegBytes = 8 * 1024 * 1024;
constexpr int kProcessPollMs = 25;
constexpr int kMaxDiagnosticBytes = 1024 * 1024;

struct AudioStreamProbe {
    bool succeeded = false;
    bool hasAudio = false;
    int bestAudioStreamIndex = -1;
    double videoStartOffsetSeconds = 0.0;
    QString error;
};

bool isConcatManifest(const QString& path) {
    return path.endsWith(QStringLiteral(".ffconcat"), Qt::CaseInsensitive);
}

bool isVapourSynthScript(const QString& path) {
    return path.endsWith(QStringLiteral(".vpy"), Qt::CaseInsensitive);
}

void appendInputFile(QStringList& arguments, const QString& path) {
    if (isConcatManifest(path)) arguments << "-safe" << "0";
    if (isVapourSynthScript(path)) arguments << "-f" << "vapoursynth";
    arguments << "-i" << path;
}

AudioStreamProbe probeAudioStream(const QString& path) {
    AudioStreamProbe probe;
    AVFormatContext* formatContext = nullptr;
    const QByteArray encodedPath = QFile::encodeName(path);
    const AVInputFormat *inputFormat = isConcatManifest(path)
        ? av_find_input_format("concat")
        : (isVapourSynthScript(path)
               ? av_find_input_format("vapoursynth") : nullptr);
    if (isVapourSynthScript(path) && !inputFormat) {
        probe.error = QStringLiteral(
            "This FFmpeg build does not provide the VapourSynth input module.");
        return probe;
    }
    AVDictionary *inputOptions = nullptr;
    if (isConcatManifest(path)) av_dict_set(&inputOptions, "safe", "0", 0);
    int result = avformat_open_input(
        &formatContext, encodedPath.constData(), inputFormat, &inputOptions);
    av_dict_free(&inputOptions);
    if (result < 0) {
        char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, errorBuffer, sizeof errorBuffer);
        probe.error = QString::fromUtf8(errorBuffer);
        return probe;
    }

    result = avformat_find_stream_info(formatContext, nullptr);
    if (result < 0) {
        char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, errorBuffer, sizeof errorBuffer);
        probe.error = QString::fromUtf8(errorBuffer);
        avformat_close_input(&formatContext);
        return probe;
    }

    double containerStartSeconds = std::numeric_limits<double>::quiet_NaN();
    if (formatContext->start_time != AV_NOPTS_VALUE)
        containerStartSeconds = static_cast<double>(formatContext->start_time) / AV_TIME_BASE;

    double earliestStreamStartSeconds = std::numeric_limits<double>::infinity();
    double videoStartSeconds = std::numeric_limits<double>::quiet_NaN();
    const int primaryVideoStream = av_find_best_stream(
        formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    for (unsigned int i = 0; i < formatContext->nb_streams; ++i) {
        const AVStream* stream = formatContext->streams[i];
        if (stream->start_time != AV_NOPTS_VALUE) {
            const double startSeconds = static_cast<double>(stream->start_time)
                                      * av_q2d(stream->time_base);
            earliestStreamStartSeconds = std::min(earliestStreamStartSeconds, startSeconds);
            if (static_cast<int>(i) == primaryVideoStream) {
                videoStartSeconds = startSeconds;
            }
        }
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            probe.hasAudio = true;
            if (probe.bestAudioStreamIndex < 0)
                probe.bestAudioStreamIndex = static_cast<int>(i);
        }
    }
    const int preferredAudioStream = av_find_best_stream(
        formatContext, AVMEDIA_TYPE_AUDIO, -1, primaryVideoStream, nullptr, 0);
    if (preferredAudioStream >= 0)
        probe.bestAudioStreamIndex = preferredAudioStream;
    if (!std::isfinite(containerStartSeconds)
        && std::isfinite(earliestStreamStartSeconds)) {
        containerStartSeconds = earliestStreamStartSeconds;
    }
    if (std::isfinite(videoStartSeconds) && std::isfinite(containerStartSeconds)) {
        probe.videoStartOffsetSeconds = videoStartSeconds - containerStartSeconds;
    }
    probe.succeeded = true;
    avformat_close_input(&formatContext);
    return probe;
}

VDAudioCodecParams configuredAudioParams()
{
    VDAudioCodecParams params = VDQtCodecEngine::instance().getAudioParams();
    if (!params.codecId.trimmed().isEmpty()) return params;

    const VDAudioCodecConfig fallback = VDQtCodecSettings::instance().getAudioConfig();
    return VDQtCodecEngine::audioParamsFromConfig(fallback);
}

bool appendVideoEncoderArguments(QStringList& args,
                                 const VDVideoCodecParams& params,
                                 int sourceBitDepth,
                                 bool sourceHasAlpha,
                                 bool preserveNativeVfr,
                                 const QString& container,
                                 QString *errorMessage,
                                 QString *resolvedPixelFormat = nullptr)
{
    QString outputPixelFormat = params.pixFmt.trimmed().toLower();
    const QString codecId = params.codecId.trimmed();
    if (codecId.compare(QStringLiteral("(Uncompressed)"), Qt::CaseInsensitive) == 0
        || codecId.compare(QStringLiteral("uncompressed"), Qt::CaseInsensitive) == 0
        || codecId.isEmpty()) {
        args << "-c:v" << "rawvideo";
        if (sourceBitDepth > 8)
            outputPixelFormat = sourceHasAlpha ? QStringLiteral("rgba64le")
                                               : QStringLiteral("rgb48le");
        else
            outputPixelFormat = sourceHasAlpha ? QStringLiteral("rgba")
                                               : QStringLiteral("bgr24");
        if (container == QStringLiteral("mkv")
            || container == QStringLiteral("matroska")) {
            args << "-allow_raw_vfw" << "1";
        }
    } else if (codecId == QStringLiteral("libx264_10bit")) {
        args << "-c:v" << "libx264";
        outputPixelFormat = QStringLiteral("yuv420p10le");
    } else if (codecId == QStringLiteral("libx265_lossless")) {
        args << "-c:v" << "libx265" << "-x265-params" << "lossless=1";
        outputPixelFormat = QStringLiteral("yuv420p");
    } else {
        args << "-c:v" << codecId;
    }

    if (codecId == QStringLiteral("prores_ks")) {
        args << "-profile:v" << QString::number(params.proresProfile);
        if (!params.proresVendor.isEmpty())
            args << "-vendor" << params.proresVendor;
        outputPixelFormat = params.proresProfile >= 4
            ? (sourceHasAlpha ? QStringLiteral("yuva444p10le")
                              : QStringLiteral("yuv444p10le"))
            : QStringLiteral("yuv422p10le");
    } else if (codecId == QStringLiteral("ffv1")) {
        args << "-level" << QString::number(params.ffv1Version)
             << "-coder" << QString::number(params.ffv1Coder)
             << "-slices" << QString::number(params.ffv1Slices);
    } else if (codecId == QStringLiteral("huffyuv")) {
        args << "-pred" << QString::number(params.huffyuvPredictor);
        if (outputPixelFormat.isEmpty())
            outputPixelFormat = QStringLiteral("yuv422p");
    } else if (codecId == QStringLiteral("cfhd")) {
        args << "-quality" << QString::number(params.cineformQuality);
        outputPixelFormat = QStringLiteral("yuv422p10le");
    } else if (codecId == QStringLiteral("libx264")
               || codecId == QStringLiteral("libx265")) {
        if (params.rateMode == QStringLiteral("crf"))
            args << "-crf" << QString::number(params.crf);
        else
            args << "-b:v" << QString("%1k").arg(params.targetBitrateKbps);
        if (!params.preset.isEmpty()) args << "-preset" << params.preset;
        if (!params.tune.isEmpty() && params.tune != QStringLiteral("none"))
            args << "-tune" << params.tune;
    } else if (codecId == QStringLiteral("libvpx")
               || codecId == QStringLiteral("libvpx-vp9")
               || codecId == QStringLiteral("libsvtav1")) {
        args << "-crf" << QString::number(params.crf)
             << "-b:v" << QString("%1k").arg(params.targetBitrateKbps);
    }

    if (params.keyframeInterval > 0 && params.keyframeInterval <= 10000)
        args << "-g" << QString::number(params.keyframeInterval);
    if (preserveNativeVfr && params.bFrames > 0)
        args << "-bf" << "0";
    else if (params.bFrames > 0 && params.bFrames <= 16)
        args << "-bf" << QString::number(params.bFrames);
    if (!params.colorMatrix.isEmpty()
        && params.colorMatrix != QStringLiteral("auto")
        && params.colorMatrix != QStringLiteral("none")) {
        args << "-colorspace" << params.colorMatrix
             << "-color_primaries" << params.colorMatrix
             << "-color_trc" << params.colorMatrix;
    }

    if (!outputPixelFormat.isEmpty()) {
        const QByteArray formatName = outputPixelFormat.toUtf8();
        const AVPixelFormat outputFormat = av_get_pix_fmt(formatName.constData());
        const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(outputFormat);
        int outputBitDepth = 0;
        bool outputHasAlpha = false;
        if (descriptor) {
            for (int component = 0; component < descriptor->nb_components; ++component) {
                outputBitDepth = std::max(
                    outputBitDepth,
                    static_cast<int>(descriptor->comp[component].depth));
            }
            outputHasAlpha = (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
        }
        const bool deliberatePaletteReduction = codecId == QStringLiteral("gif");
        if (!descriptor
            || (!deliberatePaletteReduction
                && ((sourceBitDepth > 8 && outputBitDepth < sourceBitDepth)
                    || (sourceHasAlpha && !outputHasAlpha)))) {
            if (errorMessage) {
                *errorMessage = QString(
                    "The selected output pixel format '%1' cannot preserve the source's "
                    "%2-bit%3 video data. Choose a matching high-bit-depth/alpha-capable "
                    "format or use direct stream copy.")
                    .arg(outputPixelFormat)
                    .arg(sourceBitDepth)
                    .arg(sourceHasAlpha ? QStringLiteral(" alpha") : QString());
            }
            return false;
        }
        args << "-pix_fmt" << outputPixelFormat;
    }
    if (resolvedPixelFormat)
        *resolvedPixelFormat = outputPixelFormat;
    return true;
}

void appendContainerArguments(QStringList& args,
                              const VDQtVideoExporter::ExportOptions& options)
{
    if (options.fastStart || options.containerType.contains(QStringLiteral("faststart")))
        args << "-movflags" << "+faststart";

    const QString container = options.containerType.toLower();
    if (container == QStringLiteral("webm")
        || options.outputPath.endsWith(QStringLiteral(".webm"), Qt::CaseInsensitive)) {
        args << "-f" << "webm";
    } else if (container == QStringLiteral("nut")
               || options.outputPath.endsWith(QStringLiteral(".nut"), Qt::CaseInsensitive)) {
        args << "-f" << "nut";
    } else if (container.startsWith(QStringLiteral("mov"))
               || options.outputPath.endsWith(QStringLiteral(".mov"), Qt::CaseInsensitive)) {
        args << "-f" << "mov";
    } else if (container.startsWith(QStringLiteral("mp4"))
               || options.outputPath.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) {
        args << "-f" << "mp4";
    } else if (container == QStringLiteral("mkv")
               || options.outputPath.endsWith(QStringLiteral(".mkv"), Qt::CaseInsensitive)) {
        args << "-f" << "matroska";
    } else if (container.startsWith(QStringLiteral("avi"))
               || options.outputPath.endsWith(QStringLiteral(".avi"), Qt::CaseInsensitive)) {
        args << "-f" << "avi";
    } else if (container == QStringLiteral("gif")
               || options.outputPath.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) {
        args << "-loop" << "0" << "-f" << "gif";
    }
}

void appendMetadataArguments(QStringList& args,
                             const QMap<QString, QString>& metadata) {
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it) {
        const QString key = it.key().trimmed();
        if (key.isEmpty() || key.size() > 128 || it.value().size() > 65536)
            continue;
        args << "-metadata" << QString("%1=%2").arg(key, it.value());
    }
}

void appendBounded(QByteArray& destination, const QByteArray& data) {
    if (data.isEmpty()) return;
    destination += data;
    if (destination.size() > kMaxDiagnosticBytes)
        destination.remove(0, destination.size() - kMaxDiagnosticBytes);
}

void drainProcessOutput(QProcess& process, QByteArray& diagnostics) {
    appendBounded(diagnostics, process.readAllStandardError());
    // FFmpeg normally has no useful stdout in this pipeline, but it must still
    // be consumed so that a child can never block on a full pipe.
    process.readAllStandardOutput();
}

void stopProcess(QProcess& process) {
    if (process.state() == QProcess::NotRunning) return;
    process.closeWriteChannel();
    process.terminate();
    if (!process.waitForFinished(1500)) {
        process.kill();
        process.waitForFinished(3000);
    }
}

bool waitForProcess(QProcess& process, QProgressDialog& progress,
                    QByteArray& diagnostics, bool& cancelled) {
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(kProcessPollMs);
        drainProcessOutput(process, diagnostics);
        QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
        if (progress.wasCanceled()) {
            cancelled = true;
            stopProcess(process);
            drainProcessOutput(process, diagnostics);
            return false;
        }
    }
    drainProcessOutput(process, diagnostics);
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool waitForProcessPair(QProcess& producer,
                        QProcess& consumer,
                        QProgressDialog& progress,
                        QByteArray& diagnostics,
                        bool& cancelled)
{
    while (producer.state() != QProcess::NotRunning
           || consumer.state() != QProcess::NotRunning) {
        if (producer.state() != QProcess::NotRunning)
            producer.waitForFinished(kProcessPollMs);
        if (consumer.state() != QProcess::NotRunning)
            consumer.waitForFinished(kProcessPollMs);
        drainProcessOutput(producer, diagnostics);
        drainProcessOutput(consumer, diagnostics);
        QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
        if (progress.wasCanceled()) {
            cancelled = true;
            stopProcess(producer);
            stopProcess(consumer);
            drainProcessOutput(producer, diagnostics);
            drainProcessOutput(consumer, diagnostics);
            return false;
        }
    }
    drainProcessOutput(producer, diagnostics);
    drainProcessOutput(consumer, diagnostics);
    return producer.exitStatus() == QProcess::NormalExit
        && producer.exitCode() == 0
        && consumer.exitStatus() == QProcess::NormalExit
        && consumer.exitCode() == 0;
}

bool writeFrame(QProcess& process, const QImage& image,
                QProgressDialog& progress, QByteArray& diagnostics,
                bool& cancelled) {
    for (int y = 0; y < image.height(); ++y) {
        const char* scanline = reinterpret_cast<const char*>(image.constScanLine(y));
        const int bytesPerPixel = image.depth() / 8;
        if (bytesPerPixel != 3 && bytesPerPixel != 4 && bytesPerPixel != 8)
            return false;
        qint64 remaining = static_cast<qint64>(image.width()) * bytesPerPixel;
        while (remaining > 0) {
            while (process.bytesToWrite() >= kMaxQueuedFfmpegBytes) {
                if (process.state() == QProcess::NotRunning) {
                    drainProcessOutput(process, diagnostics);
                    return false;
                }
                process.waitForBytesWritten(kProcessPollMs);
                drainProcessOutput(process, diagnostics);
                QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
                if (progress.wasCanceled()) {
                    cancelled = true;
                    return false;
                }
            }

            const qint64 accepted = process.write(scanline, remaining);
            if (accepted <= 0) {
                drainProcessOutput(process, diagnostics);
                return false;
            }
            scanline += accepted;
            remaining -= accepted;
        }
    }
    drainProcessOutput(process, diagnostics);
    return true;
}

class TimestampedNutWriter {
public:
    ~TimestampedNutWriter() { release(); }

    bool open(QProcess& process,
              QProgressDialog& progress,
              QByteArray& diagnostics,
              bool& cancelled,
              int width,
              int height,
              AVPixelFormat pixelFormat,
              AVRational averageFrameRate) {
        mProcess = &process;
        mProgress = &progress;
        mDiagnostics = &diagnostics;
        mCancelled = &cancelled;

        int result = avformat_alloc_output_context2(&mFormat, nullptr, "nut", nullptr);
        if (result < 0 || !mFormat)
            return fail(QStringLiteral("Could not initialize the timestamped NUT stream."), result);

        AVStream *stream = avformat_new_stream(mFormat, nullptr);
        if (!stream)
            return fail(QStringLiteral("Could not create the timestamped video stream."), AVERROR(ENOMEM));
        mStreamIndex = stream->index;
        stream->time_base = AVRational{1, 1000000};
        stream->avg_frame_rate = averageFrameRate;
        stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        stream->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
        stream->codecpar->format = pixelFormat;
        stream->codecpar->width = width;
        stream->codecpar->height = height;
        stream->codecpar->codec_tag = 0;

        constexpr int ioBufferSize = 64 * 1024;
        unsigned char *buffer = static_cast<unsigned char *>(av_malloc(ioBufferSize));
        if (!buffer)
            return fail(QStringLiteral("Could not allocate the timestamped stream buffer."), AVERROR(ENOMEM));
        mIo = avio_alloc_context(buffer, ioBufferSize, 1, this, nullptr,
                                 &TimestampedNutWriter::writePacket, nullptr);
        if (!mIo) {
            av_free(buffer);
            return fail(QStringLiteral("Could not create the timestamped stream pipe."), AVERROR(ENOMEM));
        }
        mFormat->pb = mIo;
        mFormat->flags |= AVFMT_FLAG_CUSTOM_IO;
        result = avformat_write_header(mFormat, nullptr);
        if (result < 0)
            return fail(QStringLiteral("Could not write the timestamped stream header."), result);
        mStreamTimeBase = stream->time_base;
        mHeaderWritten = true;
        return true;
    }

    bool writeImage(const QImage& image, qint64 ptsUs, qint64 durationUs) {
        if (!mFormat || !mHeaderWritten || image.isNull() || durationUs <= 0)
            return false;
        const int bytesPerPixel = image.depth() / 8;
        if (bytesPerPixel != 3 && bytesPerPixel != 4 && bytesPerPixel != 8)
            return fail(QStringLiteral("The timestamped pipe received an unsupported image format."), AVERROR(EINVAL));
        const qint64 rowBytes = static_cast<qint64>(image.width()) * bytesPerPixel;
        const qint64 packetBytes = rowBytes * image.height();
        if (packetBytes <= 0 || packetBytes > std::numeric_limits<int>::max())
            return fail(QStringLiteral("A timestamped video frame is too large."), AVERROR(EINVAL));

        AVPacket *packet = av_packet_alloc();
        if (!packet)
            return fail(QStringLiteral("Could not allocate a timestamped video packet."), AVERROR(ENOMEM));
        int result = av_new_packet(packet, static_cast<int>(packetBytes));
        if (result >= 0) {
            unsigned char *destination = packet->data;
            for (int y = 0; y < image.height(); ++y) {
                memcpy(destination, image.constScanLine(y), static_cast<size_t>(rowBytes));
                destination += rowBytes;
            }
            packet->stream_index = mStreamIndex;
            const AVRational microseconds{1, 1000000};
            packet->pts = av_rescale_q(ptsUs, microseconds, mStreamTimeBase);
            packet->dts = packet->pts;
            packet->duration = std::max<int64_t>(
                1, av_rescale_q(durationUs, microseconds, mStreamTimeBase));
            packet->pos = -1;
            packet->flags |= AV_PKT_FLAG_KEY;
            result = av_interleaved_write_frame(mFormat, packet);
        }
        av_packet_free(&packet);
        if (result < 0)
            return fail(QStringLiteral("Could not write a timestamped video frame."), result);
        return true;
    }

    bool finish() {
        if (!mFormat || !mHeaderWritten) return false;
        const int result = av_write_trailer(mFormat);
        mHeaderWritten = false;
        if (mIo) avio_flush(mIo);
        if (result < 0)
            return fail(QStringLiteral("Could not finalize the timestamped video stream."), result);
        release();
        return true;
    }

private:
    static int writePacket(void *opaque, const uint8_t *buffer, int size) {
        return static_cast<TimestampedNutWriter *>(opaque)->writeBytes(buffer, size);
    }

    int writeBytes(const uint8_t *buffer, int size) {
        if (!mProcess || !mProgress || !mDiagnostics || !mCancelled)
            return AVERROR(EIO);
        int remaining = size;
        const char *source = reinterpret_cast<const char *>(buffer);
        while (remaining > 0) {
            while (mProcess->bytesToWrite() >= kMaxQueuedFfmpegBytes) {
                if (mProcess->state() == QProcess::NotRunning) {
                    drainProcessOutput(*mProcess, *mDiagnostics);
                    return AVERROR(EPIPE);
                }
                mProcess->waitForBytesWritten(kProcessPollMs);
                drainProcessOutput(*mProcess, *mDiagnostics);
                QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
                if (mProgress->wasCanceled()) {
                    *mCancelled = true;
                    return AVERROR_EXIT;
                }
            }
            const qint64 written = mProcess->write(source, remaining);
            if (written <= 0) {
                drainProcessOutput(*mProcess, *mDiagnostics);
                return AVERROR(EPIPE);
            }
            source += written;
            remaining -= static_cast<int>(written);
        }
        drainProcessOutput(*mProcess, *mDiagnostics);
        return size;
    }

    bool fail(const QString& message, int errorCode) {
        QByteArray detail = message.toUtf8();
        if (errorCode < 0) {
            char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
            av_strerror(errorCode, errorBuffer, sizeof errorBuffer);
            detail += QByteArrayLiteral(" ") + errorBuffer;
        }
        detail += '\n';
        if (mDiagnostics) appendBounded(*mDiagnostics, detail);
        return false;
    }

    void release() {
        if (mFormat) mFormat->pb = nullptr;
        if (mIo) {
            av_freep(&mIo->buffer);
            avio_context_free(&mIo);
        }
        if (mFormat) avformat_free_context(mFormat);
        mFormat = nullptr;
        mStreamIndex = -1;
        mHeaderWritten = false;
    }

    QProcess *mProcess = nullptr;
    QProgressDialog *mProgress = nullptr;
    QByteArray *mDiagnostics = nullptr;
    bool *mCancelled = nullptr;
    AVFormatContext *mFormat = nullptr;
    AVIOContext *mIo = nullptr;
    int mStreamIndex = -1;
    AVRational mStreamTimeBase{1, 1000000};
    bool mHeaderWritten = false;
};

bool validOutputFile(const QString& path) {
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.size() > 0;
}

void removePartialOutput(const QString& path) {
    if (!path.isEmpty() && QFileInfo::exists(path))
        QFile::remove(path);
}

QString stagedOutputTemplate(const QString& outputPath) {
    const QFileInfo target(outputPath);
    QString baseName = target.completeBaseName();
    if (baseName.isEmpty()) baseName = QStringLiteral("video");
    QString pattern = target.dir().filePath(QStringLiteral(".%1.XXXXXX").arg(baseName));
    const QString suffix = target.completeSuffix();
    if (!suffix.isEmpty()) pattern += QLatin1Char('.') + suffix;
    return pattern;
}

bool replaceWithStagedFile(const QString& stagedPath, const QString& outputPath) {
    if (!validOutputFile(stagedPath)) return false;
    const QFileInfo existing(outputPath);
    const QFile::Permissions permissions = existing.exists()
        ? existing.permissions()
        : QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
    QFile::setPermissions(stagedPath, permissions);

    const QByteArray stagedName = QFile::encodeName(stagedPath);
    const QByteArray outputName = QFile::encodeName(outputPath);
    return std::rename(stagedName.constData(), outputName.constData()) == 0;
}

QString avErrorText(int errorCode) {
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, errorBuffer, sizeof errorBuffer);
    return QString::fromUtf8(errorBuffer);
}

bool validRawAlignment(int alignment) {
    return alignment >= 1 && alignment <= 64
        && (alignment & (alignment - 1)) == 0;
}

class RawFrameWriter {
public:
    ~RawFrameWriter() {
        if (mFrame) av_frame_free(&mFrame);
        if (mScaleContext) sws_freeContext(mScaleContext);
    }

    bool initialize(AVPixelFormat outputFormat,
                    int width,
                    int height,
                    int scanlineAlignment,
                    bool swapChromaPlanes,
                    bool bottomUp,
                    const QString& colorMatrix,
                    bool fullRange,
                    QString *errorMessage) {
        mDescriptor = av_pix_fmt_desc_get(outputFormat);
        mPlaneCount = av_pix_fmt_count_planes(outputFormat);
        if (!mDescriptor || mPlaneCount <= 0 || mPlaneCount > 4
            || (mDescriptor->flags & (AV_PIX_FMT_FLAG_HWACCEL
                                      | AV_PIX_FMT_FLAG_BITSTREAM
                                      | AV_PIX_FMT_FLAG_PAL))) {
            if (errorMessage)
                *errorMessage = QStringLiteral("The selected raw pixel format is not a writable software layout.");
            return false;
        }
        if (width <= 0 || height <= 0 || !validRawAlignment(scanlineAlignment)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("The raw frame dimensions or scanline alignment are invalid.");
            return false;
        }

        mFrame = av_frame_alloc();
        if (!mFrame) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Could not allocate a raw conversion frame.");
            return false;
        }
        mFrame->format = outputFormat;
        mFrame->width = width;
        mFrame->height = height;
        const int result = av_frame_get_buffer(mFrame, 64);
        if (result < 0) {
            if (errorMessage)
                *errorMessage = QString("Could not allocate the raw conversion buffer: %1")
                    .arg(avErrorText(result));
            return false;
        }

        if (av_image_fill_linesizes(mPlaneRowBytes, outputFormat, width) < 0) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Could not determine the raw pixel layout.");
            return false;
        }
        for (int plane = 0; plane < mPlaneCount; ++plane) {
            if (mPlaneRowBytes[plane] <= 0 || !mFrame->data[plane]) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("The raw pixel layout has an unsupported plane arrangement.");
                return false;
            }
            mPlaneHeights[plane] = planeHeight(plane, height);
            if (mPlaneHeights[plane] <= 0) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("The raw pixel layout has an invalid plane height.");
                return false;
            }
        }

        mWidth = width;
        mHeight = height;
        mOutputFormat = outputFormat;
        mAlignment = scanlineAlignment;
        mSwapChromaPlanes = swapChromaPlanes && hasSeparateChromaPlanes();
        mBottomUp = bottomUp;
        mColorMatrix = colorMatrix.toLower();
        mFullRange = fullRange;
        return true;
    }

    bool write(QFile& output, const QImage& inputImage, QString *errorMessage) {
        if (!mFrame || inputImage.isNull()
            || inputImage.width() != mWidth || inputImage.height() != mHeight) {
            if (errorMessage)
                *errorMessage = QStringLiteral("A filter changed the raw frame dimensions during export.");
            return false;
        }

        QImage normalized;
        AVPixelFormat inputFormat = AV_PIX_FMT_NONE;
        if (inputImage.depth() > 32) {
            normalized = inputImage.convertToFormat(QImage::Format_RGBA64);
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
            inputFormat = AV_PIX_FMT_RGBA64LE;
#else
            inputFormat = AV_PIX_FMT_RGBA64BE;
#endif
        } else if (inputImage.hasAlphaChannel()) {
            normalized = inputImage.convertToFormat(QImage::Format_RGBA8888);
            inputFormat = AV_PIX_FMT_RGBA;
        } else {
            normalized = inputImage.convertToFormat(QImage::Format_RGB888);
            inputFormat = AV_PIX_FMT_RGB24;
        }
        if (normalized.isNull()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Could not normalize a decoded frame for raw export.");
            return false;
        }
        if (normalized.bytesPerLine() > std::numeric_limits<int>::max()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("The decoded frame stride is too large for raw conversion.");
            return false;
        }

        mScaleContext = sws_getCachedContext(
            mScaleContext, mWidth, mHeight, inputFormat,
            mWidth, mHeight, mOutputFormat,
            SWS_BICUBIC, nullptr, nullptr, nullptr);
        if (!mScaleContext) {
            if (errorMessage)
                *errorMessage = QStringLiteral("The selected raw pixel conversion is not supported.");
            return false;
        }

        if (!(mDescriptor->flags & AV_PIX_FMT_FLAG_RGB)
            && mDescriptor->nb_components >= 3) {
            const int colorSpace = mColorMatrix == QStringLiteral("bt709")
                ? SWS_CS_ITU709 : SWS_CS_SMPTE170M;
            const int *coefficients = sws_getCoefficients(colorSpace);
            if (!coefficients
                || sws_setColorspaceDetails(
                       mScaleContext, coefficients, 1,
                       coefficients, mFullRange ? 1 : 0,
                       0, 1 << 16, 1 << 16) < 0) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("Could not configure the requested YUV matrix and range.");
                return false;
            }
        }

        const int writableResult = av_frame_make_writable(mFrame);
        if (writableResult < 0) {
            if (errorMessage)
                *errorMessage = QString("Could not reuse the raw conversion buffer: %1")
                    .arg(avErrorText(writableResult));
            return false;
        }
        const uint8_t *sourceData[4] = {
            normalized.constBits(), nullptr, nullptr, nullptr
        };
        const int sourceLinesize[4] = {
            static_cast<int>(normalized.bytesPerLine()), 0, 0, 0
        };
        const int convertedRows = sws_scale(
            mScaleContext, sourceData, sourceLinesize, 0, mHeight,
            mFrame->data, mFrame->linesize);
        if (convertedRows != mHeight) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Raw pixel conversion stopped before the frame was complete.");
            return false;
        }

        int planeOrder[4] = { 0, 1, 2, 3 };
        if (mSwapChromaPlanes) std::swap(planeOrder[1], planeOrder[2]);
        const QByteArray zeroPadding(64, '\0');
        for (int order = 0; order < mPlaneCount; ++order) {
            const int plane = planeOrder[order];
            const int rowBytes = mPlaneRowBytes[plane];
            const int alignedRowBytes = (rowBytes + mAlignment - 1)
                                      & ~(mAlignment - 1);
            const int paddingBytes = alignedRowBytes - rowBytes;
            for (int row = 0; row < mPlaneHeights[plane]; ++row) {
                const int sourceRow = mBottomUp
                    ? mPlaneHeights[plane] - 1 - row : row;
                const char *rowData = reinterpret_cast<const char *>(
                    mFrame->data[plane] + sourceRow * mFrame->linesize[plane]);
                if (output.write(rowData, rowBytes) != rowBytes
                    || (paddingBytes > 0
                        && output.write(zeroPadding.constData(), paddingBytes)
                            != paddingBytes)) {
                    if (errorMessage)
                        *errorMessage = output.errorString().isEmpty()
                            ? QStringLiteral("The raw output file could not be written completely.")
                            : output.errorString();
                    return false;
                }
            }
        }
        return true;
    }

private:
    int planeHeight(int plane, int frameHeight) const {
        if ((mDescriptor->flags & AV_PIX_FMT_FLAG_RGB)
            || mDescriptor->nb_components < 3)
            return frameHeight;
        const int uPlane = mDescriptor->comp[1].plane;
        const int vPlane = mDescriptor->comp[2].plane;
        return plane == uPlane || plane == vPlane
            ? AV_CEIL_RSHIFT(frameHeight, mDescriptor->log2_chroma_h)
            : frameHeight;
    }

    bool hasSeparateChromaPlanes() const {
        return !(mDescriptor->flags & AV_PIX_FMT_FLAG_RGB)
            && mDescriptor->nb_components >= 3
            && mDescriptor->comp[1].plane != mDescriptor->comp[0].plane
            && mDescriptor->comp[2].plane != mDescriptor->comp[0].plane
            && mDescriptor->comp[1].plane != mDescriptor->comp[2].plane;
    }

    SwsContext *mScaleContext = nullptr;
    AVFrame *mFrame = nullptr;
    const AVPixFmtDescriptor *mDescriptor = nullptr;
    AVPixelFormat mOutputFormat = AV_PIX_FMT_NONE;
    int mWidth = 0;
    int mHeight = 0;
    int mPlaneCount = 0;
    int mPlaneRowBytes[4] = {};
    int mPlaneHeights[4] = {};
    int mAlignment = 1;
    bool mSwapChromaPlanes = false;
    bool mBottomUp = false;
    QString mColorMatrix;
    bool mFullRange = false;
};

} // namespace

VDQtVideoExporter::VDQtVideoExporter() {}

VDQtVideoExporter::~VDQtVideoExporter() {}

bool VDQtVideoExporter::exportRawVideo(
    const RawExportOptions& options,
    VDQtVideoDecoder *activeDecoder,
    VDQtAudioPlayer *audioPlayer,
    QWidget *parentWidget,
    std::function<bool(int completedFrames, int totalFrames)> progressCallback) {
    const auto reportError = [parentWidget](const QString& message) {
        qWarning() << "[Raw Export]" << message;
        if (parentWidget)
            QMessageBox::critical(parentWidget, "Raw Video Export Error", message);
    };

    if (options.inputPath.isEmpty() || options.outputPath.isEmpty()) {
        reportError(QStringLiteral("The raw export source or destination path is empty."));
        return false;
    }
    if (!validRawAlignment(options.scanlineAlignment)) {
        reportError(QStringLiteral("Scanline alignment must be a power of two from 1 through 64 bytes."));
        return false;
    }
    const QByteArray pixelFormatName = options.pixelFormat.trimmed().toLower().toUtf8();
    const AVPixelFormat outputPixelFormat = av_get_pix_fmt(pixelFormatName.constData());
    if (outputPixelFormat == AV_PIX_FMT_NONE) {
        reportError(QString("Unknown raw pixel format: %1").arg(options.pixelFormat));
        return false;
    }

    VDQtVideoDecoder localDecoder;
    VDQtVideoDecoder& decoder = activeDecoder && activeDecoder->isOpen()
        ? *activeDecoder : localDecoder;
    if (!decoder.isOpen() && !decoder.openFile(options.inputPath)) {
        reportError(QStringLiteral("The source video could not be opened for raw export."));
        return false;
    }

    const QString loadedSourcePath = decoder.getFilePath();
    const QString scriptPath = VDQtSourceSafety::isScriptPath(loadedSourcePath)
        ? loadedSourcePath
        : (VDQtSourceSafety::isScriptPath(options.inputPath)
               ? options.inputPath : QString());
    QStringList directlyLoadedSources = { options.inputPath, loadedSourcePath };
    if (audioPlayer) directlyLoadedSources.append(audioPlayer->getSourcePath());
    const auto outputIsSafe = [&]() {
        return VDQtSourceSafety::evaluateOutputPath(
                   options.outputPath, directlyLoadedSources, scriptPath)
            .isSafe();
    };
    if (!outputIsSafe()) {
        reportError(QStringLiteral(
            "The destination aliases a loaded/script source, or an existing script-backed "
            "destination cannot be audited safely. Choose another path."));
        return false;
    }

    int totalFrames = decoder.getFrameCount();
    // Container nb_frames metadata can be exact-looking but underreported, and
    // it does not prove that a VFR timestamp index exists. Native AviSynth has
    // an authoritative clip length; regular media must be drained once before
    // raw range and frame-rate conversion decisions are made.
    if (!decoder.isAvsNative()) {
        QProgressDialog indexingProgress(
            "Indexing source frames for raw export...", "Cancel",
            0, totalFrames > 0 ? totalFrames : 0, parentWidget);
        indexingProgress.setWindowModality(Qt::WindowModal);
        indexingProgress.setMinimumDuration(0);
        const int initialEstimate = totalFrames;
        const VDQtVideoDecoder::VDScanResult scan = decoder.scanVideoStream(
            [&indexingProgress, initialEstimate](int current, int reportedTotal) {
                if (initialEstimate > 0) {
                    const int maximum = std::max(initialEstimate, reportedTotal);
                    indexingProgress.setRange(0, maximum);
                    indexingProgress.setValue(std::min(current, maximum));
                } else {
                    indexingProgress.setRange(0, 0);
                    indexingProgress.setLabelText(
                        QString("Indexing source frames... %1 decoded").arg(current));
                }
                QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
                return !indexingProgress.wasCanceled();
            });
        const bool indexingCancelled = indexingProgress.wasCanceled();
        indexingProgress.close();
        if (scan.cancelled || indexingCancelled) return false;
        if (!scan.errorMessage.isEmpty()) {
            reportError(scan.errorMessage);
            return false;
        }
        totalFrames = decoder.getFrameCount();
    }
    if (totalFrames <= 0) {
        reportError(QStringLiteral("The source has no decodable video frames."));
        return false;
    }

    const bool explicitFrameRange = options.endFrame >= options.startFrame
                                 && options.endFrame >= 0;
    if (explicitFrameRange
        && (options.startFrame < 0 || options.startFrame >= totalFrames)) {
        reportError(QString("The selection starts at frame %1, but the source contains only %2 frame(s).")
                        .arg(options.startFrame)
                        .arg(totalFrames));
        return false;
    }
    const int startFrame = explicitFrameRange ? options.startFrame : 0;
    const int endFrame = explicitFrameRange
        ? std::min(options.endFrame, totalFrames - 1) : totalFrames - 1;
    const int selectedSourceFrames = endFrame - startFrame + 1;
    int step = std::max(1, options.decimateFactor);
    const double sourceFps = decoder.getFps() > 0.0
        ? decoder.getFps() : 29.97;
    double sourceStartSeconds = static_cast<double>(startFrame) / sourceFps;
    double sourceDurationSeconds = static_cast<double>(selectedSourceFrames) / sourceFps;
    const double firstTimestamp = decoder.getFrameTimestampSeconds(startFrame);
    const double lastTimestamp = decoder.getFrameTimestampSeconds(endFrame);
    const double lastDuration = decoder.getFrameDurationSeconds(endFrame);
    if (std::isfinite(firstTimestamp)) sourceStartSeconds = firstTimestamp;
    if (std::isfinite(firstTimestamp) && std::isfinite(lastTimestamp)
        && std::isfinite(lastDuration)
        && lastTimestamp + lastDuration > firstTimestamp) {
        sourceDurationSeconds = lastTimestamp + lastDuration - firstTimestamp;
    }

    bool timestampsUsable = std::isfinite(firstTimestamp);
    double previousTimestamp = -std::numeric_limits<double>::infinity();
    if (options.convertFpsPreserveDuration && options.customFps > 0.0) {
        for (int frame = startFrame; frame <= endFrame; ++frame) {
            const double timestamp = decoder.getFrameTimestampSeconds(frame);
            if (!std::isfinite(timestamp) || timestamp + 1e-9 < previousTimestamp) {
                timestampsUsable = false;
                break;
            }
            previousTimestamp = timestamp;
            if (frame == endFrame) break;
        }
    }

    int inputFramesToProcess = (selectedSourceFrames + step - 1) / step;
    double selectionOutputFps = sourceFps / step;
    if (options.convertFpsPreserveDuration && options.customFps > 0.0) {
        const long double requestedFrames =
            static_cast<long double>(sourceDurationSeconds) * options.customFps;
        if (!std::isfinite(requestedFrames)
            || requestedFrames > std::numeric_limits<int>::max()) {
            reportError(QStringLiteral("The requested raw frame-rate conversion is too large."));
            return false;
        }
        inputFramesToProcess = std::max(
            1, static_cast<int>(std::llround(requestedFrames)));
        selectionOutputFps = options.customFps;
        step = 1;
    }

    const VDFilterTimingInfo timing = VDQtFilterSystem::instance().getTimingInfo();
    if (!timing.sequenceSupported || timing.outputFramesPerInput <= 0
        || inputFramesToProcess
               > std::numeric_limits<int>::max() / timing.outputFramesPerInput) {
        reportError(QStringLiteral("The temporal filter chain produces an unsupported raw sequence size."));
        return false;
    }
    const int framesToExport = inputFramesToProcess * timing.outputFramesPerInput;

    QImage sampleFrame = decoder.getFrameImage(startFrame);
    QImage filteredSample = VDQtFilterSystem::instance().processFrame(sampleFrame);
    if (sampleFrame.isNull() || filteredSample.isNull()) {
        reportError(QStringLiteral("The first selected frame could not be decoded and filtered."));
        return false;
    }

    RawFrameWriter frameWriter;
    QString writeError;
    if (!frameWriter.initialize(
            outputPixelFormat, filteredSample.width(), filteredSample.height(),
            options.scanlineAlignment, options.swapChromaPlanes,
            options.bottomUp, options.colorMatrix, options.fullRange,
            &writeError)) {
        reportError(writeError);
        return false;
    }

    QTemporaryFile stagedOutput(stagedOutputTemplate(options.outputPath));
    stagedOutput.setAutoRemove(true);
    if (!stagedOutput.open()) {
        reportError(QStringLiteral(
            "A staging file could not be created beside the raw-video destination."));
        return false;
    }

    QProgressDialog progress(
        "Exporting raw video...", "Cancel", 0, framesToExport, parentWidget);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    QElapsedTimer timer;
    timer.start();

    bool cancelled = false;
    bool failed = false;
    int completedFrames = 0;
    for (int inputIndex = 0; inputIndex < inputFramesToProcess; ++inputIndex) {
        QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
        if (progress.wasCanceled()) {
            cancelled = true;
            break;
        }

        int sourceFrame = startFrame;
        if (options.convertFpsPreserveDuration && options.customFps > 0.0) {
            if (timestampsUsable) {
                const double requestedTimestamp = sourceStartSeconds
                    + static_cast<double>(inputIndex) / selectionOutputFps;
                int low = startFrame;
                int high = endFrame;
                while (low < high) {
                    const int middle = low + (high - low + 1) / 2;
                    const double timestamp = decoder.getFrameTimestampSeconds(middle);
                    if (std::isfinite(timestamp)
                        && timestamp <= requestedTimestamp + 1e-9)
                        low = middle;
                    else
                        high = middle - 1;
                }
                sourceFrame = low;
            } else {
                const double sourceOffset = static_cast<double>(inputIndex)
                    * sourceFps / selectionOutputFps;
                sourceFrame = startFrame
                    + static_cast<int>(std::floor(sourceOffset + 1e-9));
            }
        } else {
            sourceFrame += inputIndex * step;
        }
        sourceFrame = std::clamp(sourceFrame, startFrame, endFrame);

        const QImage rawFrame = decoder.getFrameImage(sourceFrame);
        QList<QImage> filteredFrames;
        if (rawFrame.isNull()
            || !VDQtFilterSystem::instance().processFrameSequence(
                rawFrame, filteredFrames)
            || filteredFrames.size() != timing.outputFramesPerInput) {
            writeError = QString("Frame %1 could not be decoded and filtered.")
                .arg(sourceFrame);
            failed = true;
            break;
        }

        for (const QImage& filteredFrame : filteredFrames) {
            if (!frameWriter.write(stagedOutput, filteredFrame, &writeError)) {
                failed = true;
                break;
            }
            ++completedFrames;
            progress.setValue(completedFrames);
            const double elapsedSeconds = timer.elapsed() / 1000.0;
            const double currentFps = elapsedSeconds > 0.0
                ? completedFrames / elapsedSeconds : 0.0;
            progress.setLabelText(
                QString("Exporting raw frame %1 of %2\nSpeed: %3 fps")
                    .arg(completedFrames)
                    .arg(framesToExport)
                    .arg(currentFps, 0, 'f', 1));
            QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
            if (progress.wasCanceled()
                || (progressCallback
                    && !progressCallback(completedFrames, framesToExport))) {
                cancelled = true;
                break;
            }
        }
        if (failed || cancelled) break;
    }

    if (!stagedOutput.flush()) {
        writeError = stagedOutput.errorString();
        failed = true;
    }
    const QString stagedPath = stagedOutput.fileName();
    stagedOutput.close();
    progress.close();

    if (cancelled) return false;
    if (failed || completedFrames != framesToExport) {
        reportError(writeError.isEmpty()
            ? QStringLiteral("Raw video export stopped before all frames were written.")
            : writeError);
        return false;
    }
    if (!outputIsSafe()) {
        reportError(QStringLiteral(
            "The destination became unsafe while the raw video was rendering; no existing file was changed."));
        return false;
    }
    if (!replaceWithStagedFile(stagedPath, options.outputPath)) {
        reportError(QStringLiteral(
            "The completed raw video could not be committed to its destination."));
        return false;
    }
    return true;
}

bool VDQtVideoExporter::exportVideo(const ExportOptions& options,
                                    VDQtVideoDecoder *activeDecoder,
                                    VDQtAudioPlayer *audioPlayer,
                                    QWidget *parentWidget,
                                    std::function<void(int frameIndex, const QImage &rawFrame, const QImage &filteredFrame)> frameCallback) {
    if (options.inputPath.isEmpty() || options.outputPath.isEmpty()) return false;
    int videoMode = options.videoMode;
    if (VDQtSourceSafety::pathsReferToSameFile(options.inputPath, options.outputPath)) {
        if (parentWidget) {
            QMessageBox::critical(parentWidget, "Unsafe Output Path",
                                  "The output file is the currently loaded source. Choose a different path.");
        }
        return false;
    }
    if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) {
        if (parentWidget) {
            QMessageBox::critical(parentWidget, "FFmpeg Not Available",
                                  "The ffmpeg executable was not found in PATH.");
        }
        return false;
    }

    VDVideoCodecParams selectedVideoParams =
        VDQtCodecEngine::instance().getVideoParams();
    if (!options.videoCodecOverride.trimmed().isEmpty()) {
        selectedVideoParams = VDQtCodecEngine::getDefaultVideoParamsForCodec(
            options.videoCodecOverride.trimmed());
        selectedVideoParams.codecId = options.videoCodecOverride.trimmed();
    }
    if (!options.videoPixelFormatOverride.trimmed().isEmpty())
        selectedVideoParams.pixFmt = options.videoPixelFormatOverride.trimmed();

    // Pre-flight check: Video encoder availability
    if (videoMode != VideoMode_DirectStreamCopy && !options.smartRendering) {
        QString err;
        if (!VDQtCodecEngine::instance().checkVideoEncoderAvailable(
                selectedVideoParams.codecId, &err)) {
            if (parentWidget) QMessageBox::critical(parentWidget, "Video Encoder Not Available", err);
            return false;
        }
    }

    VDQtVideoDecoder localDecoder;
    VDQtVideoDecoder &decoder = (activeDecoder && activeDecoder->isOpen()) ? *activeDecoder : localDecoder;
    if (!decoder.isOpen()) {
        if (!decoder.openFile(options.inputPath)) {
            return false;
        }
    }
    const QString loadedSourcePath = decoder.getFilePath();
    const QString scriptPath = VDQtSourceSafety::isScriptPath(loadedSourcePath)
        ? loadedSourcePath
        : (VDQtSourceSafety::isScriptPath(options.inputPath)
               ? options.inputPath : QString());
    QStringList directlyLoadedSources = { options.inputPath, loadedSourcePath };
    if (audioPlayer)
        directlyLoadedSources.append(audioPlayer->getSourcePath());
    const VDQtOutputSafetyReport outputSafety = VDQtSourceSafety::evaluateOutputPath(
        options.outputPath, directlyLoadedSources, scriptPath);
    if (!outputSafety.isSafe()) {
        if (parentWidget) {
            const bool aliases = outputSafety.issue
                == VDQtOutputSafetyIssue::AliasesLoadedSource;
            QMessageBox::critical(
                parentWidget,
                aliases ? "Unsafe Output Path" : "Unsafe Script Output Path",
                aliases
                    ? QString("The output aliases a loaded or script-referenced source:\n%1\n"
                              "Choose a different destination.")
                          .arg(outputSafety.aliasedPath)
                    : QStringLiteral(
                          "An existing destination cannot be replaced because the loaded script "
                          "contains unresolved or dynamically computed source paths. Choose a new output path."));
        }
        return false;
    }
    const auto outputStillSafe = [&]() {
        return VDQtSourceSafety::evaluateOutputPath(
                   options.outputPath, directlyLoadedSources, scriptPath)
            .isSafe();
    };

    AudioStreamProbe sourceAudioProbe;
    if (!decoder.isAvsNative()) {
        sourceAudioProbe = probeAudioStream(options.inputPath);
        if (!sourceAudioProbe.succeeded) {
            if (parentWidget) {
                QMessageBox::critical(parentWidget, "Audio Stream Probe Failed",
                                      QString("The source audio streams could not be inspected safely:\n%1")
                                          .arg(sourceAudioProbe.error));
            }
            return false;
        }
    }
    const bool sourceHasAudio = options.includeAudio
        && (decoder.isAvsNative()
            ? audioPlayer && audioPlayer->hasAudio()
            : sourceAudioProbe.hasAudio);

    if (sourceHasAudio && options.audioMode != AudioMode_DirectStreamCopy
        && (!audioPlayer || !audioPlayer->hasAudio())) {
        if (parentWidget) {
            QMessageBox::critical(parentWidget, "Audio Decoder Not Available",
                                  "The source contains audio, but it could not be opened for full processing. "
                                  "The export was stopped to avoid silently producing a video-only file.");
        }
        return false;
    }

    // Pre-flight check: Audio encoder availability
    if (sourceHasAudio && options.audioMode != AudioMode_DirectStreamCopy) {
        VDAudioCodecParams aParams = VDQtCodecEngine::instance().getAudioParams();
        QString audioCodec = aParams.codecId.isEmpty()
            ? VDQtCodecSettings::instance().getAudioConfig().codecId
            : aParams.codecId;
        QString err;
        if (!VDQtCodecEngine::instance().checkAudioEncoderAvailable(audioCodec, &err)) {
            if (parentWidget) QMessageBox::critical(parentWidget, "Audio Encoder Not Available", err);
            return false;
        }
    }

    int totalFrames = decoder.getFrameCount();
    bool indexedForExport = false;
    const bool explicitFrameRange = options.endFrame >= 0
                                 && options.endFrame >= options.startFrame;
    const bool needsDecodedIndex = !decoder.isAvsNative()
        && (videoMode != VideoMode_DirectStreamCopy || explicitFrameRange);
    if ((videoMode != VideoMode_DirectStreamCopy && !decoder.isFrameCountExact())
        || needsDecodedIndex) {
        QProgressDialog indexingProgress(
            "Indexing source frames for an exact export range...", "Cancel",
            0, totalFrames > 0 ? totalFrames : 0, parentWidget);
        indexingProgress.setWindowModality(Qt::WindowModal);
        indexingProgress.setMinimumDuration(0);

        const VDQtVideoDecoder::VDScanResult scan = decoder.scanVideoStream(
            [&indexingProgress, totalFrames](int current, int reportedTotal) {
                if (totalFrames > 0) {
                    const int maximum = std::max(totalFrames, reportedTotal);
                    indexingProgress.setRange(0, maximum);
                    indexingProgress.setValue(std::min(current, maximum));
                } else {
                    indexingProgress.setRange(0, 0);
                    indexingProgress.setLabelText(
                        QString("Indexing source frames... %1 decoded").arg(current));
                }
                QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
                return !indexingProgress.wasCanceled();
            });
        indexingProgress.close();
        if (scan.cancelled) return false;
        if (!scan.errorMessage.isEmpty()) {
            if (parentWidget)
                QMessageBox::critical(parentWidget, "Export Error", scan.errorMessage);
            return false;
        }
        totalFrames = decoder.getFrameCount();
        indexedForExport = true;
    }
    if (videoMode == VideoMode_DirectStreamCopy && totalFrames <= 0) {
        // A full compressed stream can be copied without knowing its decoded
        // frame count. A synthetic one-frame range keeps the selection math
        // neutral and produces no -ss/-t arguments below.
        totalFrames = 1;
    }
    if (totalFrames <= 0) {
        if (parentWidget)
            QMessageBox::critical(parentWidget, "Export Error", "The source has no decodable video frames.");
        return false;
    }
    if (explicitFrameRange
        && (options.startFrame < 0 || options.startFrame >= totalFrames)) {
        if (parentWidget) {
            QMessageBox::critical(
                parentWidget, "Export Range Error",
                QString("The requested selection starts at frame %1, but the source contains only %2 frame(s).")
                    .arg(options.startFrame)
                    .arg(totalFrames));
        }
        return false;
    }
    const int startFrame = explicitFrameRange ? options.startFrame : 0;
    const int endFrame = explicitFrameRange
        ? std::min(options.endFrame, totalFrames - 1)
        : totalFrames - 1;
    int step = std::max(1, options.decimateFactor);
    const double sourceFps = decoder.getFps() > 0.0 ? decoder.getFps() : 29.97;
    const int selectedSourceFrames = endFrame - startFrame + 1;
    double sourceStartSeconds = static_cast<double>(startFrame) / sourceFps;
    double sourceDurationSeconds = static_cast<double>(selectedSourceFrames) / sourceFps;
    bool useTimestampFrameMapping = false;
    bool variableFrameTiming = false;
    double shortestFrameDuration = std::numeric_limits<double>::infinity();
    double longestFrameDuration = 0.0;
    const bool hasFrameSelection = startFrame > 0 || endFrame < totalFrames - 1;
    if (options.smartRendering && videoMode != VideoMode_DirectStreamCopy) {
        const bool hasEnabledFilters = std::any_of(
            VDQtFilterSystem::instance().getActiveChain().cbegin(),
            VDQtFilterSystem::instance().getActiveChain().cend(),
            [](const VDFilterInstance& filter) { return filter.enabled; });
        const bool cleanTiming = options.customFps <= 0.0
            && !options.convertFpsPreserveDuration
            && options.decimateFactor <= 1
            && options.preserveEmptyFrames;
        const bool cleanAudio = options.audioMode == AudioMode_DirectStreamCopy;
        const bool startsAtRandomAccessPoint = decoder.isKeyFrame(startFrame);
        const bool endsAtRandomAccessPoint = endFrame == totalFrames - 1
            || decoder.isKeyFrame(endFrame + 1);
        if (!decoder.isAvsNative() && !hasEnabledFilters && cleanTiming
            && cleanAudio && startsAtRandomAccessPoint && endsAtRandomAccessPoint) {
            videoMode = VideoMode_DirectStreamCopy;
        }
    }
    if (options.smartRendering && videoMode != VideoMode_DirectStreamCopy) {
        QString error;
        if (!VDQtCodecEngine::instance().checkVideoEncoderAvailable(
                selectedVideoParams.codecId, &error)) {
            if (parentWidget)
                QMessageBox::critical(
                    parentWidget, "Video Encoder Not Available", error);
            return false;
        }
    }
    if (indexedForExport || hasFrameSelection || options.convertFpsPreserveDuration) {
        const double firstTimestamp = decoder.getFrameTimestampSeconds(startFrame);
        const double lastTimestamp = decoder.getFrameTimestampSeconds(endFrame);
        const double lastDuration = decoder.getFrameDurationSeconds(endFrame);
        if (std::isfinite(firstTimestamp)) sourceStartSeconds = firstTimestamp;
        if (std::isfinite(firstTimestamp) && std::isfinite(lastTimestamp)
            && std::isfinite(lastDuration) && lastTimestamp + lastDuration > firstTimestamp) {
            sourceDurationSeconds = lastTimestamp + lastDuration - firstTimestamp;
        }
    }
    bool presentationTimestampsUsable = selectedSourceFrames > 0;
    if (indexedForExport || hasFrameSelection || options.convertFpsPreserveDuration) {
        double previousTimestamp = -std::numeric_limits<double>::infinity();
        for (int frameIndex = startFrame; frameIndex <= endFrame; ++frameIndex) {
            const double timestamp = decoder.getFrameTimestampSeconds(frameIndex);
            if (!std::isfinite(timestamp) || timestamp + 1e-9 < previousTimestamp) {
                presentationTimestampsUsable = false;
                break;
            }
            if (std::isfinite(previousTimestamp)) {
                const double timestampDelta = timestamp - previousTimestamp;
                if (timestampDelta > 1e-9) {
                    shortestFrameDuration = std::min(shortestFrameDuration, timestampDelta);
                    longestFrameDuration = std::max(longestFrameDuration, timestampDelta);
                }
            }
            const double frameDuration = decoder.getFrameDurationSeconds(frameIndex);
            if (std::isfinite(frameDuration) && frameDuration > 1e-9) {
                shortestFrameDuration = std::min(shortestFrameDuration, frameDuration);
                longestFrameDuration = std::max(longestFrameDuration, frameDuration);
            }
            previousTimestamp = timestamp;
            if (frameIndex == endFrame) break;
        }
    }
    if (!std::isfinite(shortestFrameDuration) || longestFrameDuration <= 0.0)
        presentationTimestampsUsable = false;
    if (presentationTimestampsUsable) {
        variableFrameTiming = longestFrameDuration
            > shortestFrameDuration * 1.01 + 1e-6;
    }
    if (!options.preserveEmptyFrames && videoMode != VideoMode_DirectStreamCopy
        && sourceHasAudio && variableFrameTiming) {
        if (parentWidget) {
            QMessageBox::critical(
                parentWidget, "Cannot Collapse Video Gaps With Audio",
                "Collapsing empty-frame/timestamp gaps would require cutting matching "
                "sections out of the audio timeline. Disable audio for this export or "
                "preserve empty frames to keep A/V synchronization exact.");
        }
        return false;
    }
    if (!options.preserveEmptyFrames && videoMode != VideoMode_DirectStreamCopy) {
        // Empty/null video chunks and timestamp gaps display the previous
        // frame for longer. Collapsing them is a deliberate retiming request;
        // keep the selected start point but use nominal frame cadence.
        sourceDurationSeconds = static_cast<double>(selectedSourceFrames) / sourceFps;
        presentationTimestampsUsable = false;
        variableFrameTiming = false;
    }
    const bool preserveNativeVfr = variableFrameTiming
        && presentationTimestampsUsable
        && !options.convertFpsPreserveDuration
        && options.customFps <= 0.0;
    if (preserveNativeVfr
        && (options.containerType.startsWith(QStringLiteral("avi"), Qt::CaseInsensitive)
            || options.outputPath.endsWith(QStringLiteral(".avi"), Qt::CaseInsensitive))) {
        if (parentWidget) {
            QMessageBox::critical(
                parentWidget, "Unsupported VFR Container",
                "AVI cannot reliably represent this source's variable frame timing. "
                "Choose MKV, MP4, MOV, WebM, or NUT, or explicitly convert to a constant frame rate.");
        }
        return false;
    }
    if (options.convertFpsPreserveDuration && options.customFps > 0.0)
        useTimestampFrameMapping = presentationTimestampsUsable;

    double fps = sourceFps;
    int framesToExport = (selectedSourceFrames + step - 1) / step;
    if (options.convertFpsPreserveDuration && options.customFps > 0.0) {
        fps = options.customFps;
        const long double requestedFrames = static_cast<long double>(sourceDurationSeconds) * fps;
        if (!std::isfinite(requestedFrames)
            || requestedFrames > std::numeric_limits<int>::max()) {
            if (parentWidget)
                QMessageBox::critical(parentWidget, "Export Error", "The requested frame-rate conversion is too large.");
            return false;
        }
        framesToExport = std::max(1, static_cast<int>(std::llround(requestedFrames)));
        step = 1;
    } else if (options.customFps > 0.0) {
        fps = options.customFps;
    } else if (preserveNativeVfr) {
        // The timestamped NUT pipe gives every processed frame an explicit PTS
        // and duration. `fps` is informational only and does not determine PTS.
        fps = static_cast<double>(framesToExport) / sourceDurationSeconds;
    } else if ((indexedForExport || hasFrameSelection)
               && std::isfinite(sourceDurationSeconds) && sourceDurationSeconds > 0.0) {
        // The stdin rawvideo pipe is necessarily CFR. For a VFR selection,
        // choose the segment's exact average rate so its encoded boundary and
        // processed-audio range still land on the selected PTS boundary.
        fps = static_cast<double>(framesToExport) / sourceDurationSeconds;
    } else if (step > 1) {
        fps = sourceFps / step;
    }
    if (!std::isfinite(fps) || fps <= 0.0) fps = 29.97;

    QTemporaryFile stagedOutput(stagedOutputTemplate(options.outputPath));
    stagedOutput.setAutoRemove(true);
    if (!stagedOutput.open()) {
        if (parentWidget) {
            QMessageBox::critical(parentWidget, "Export Error",
                                  "A temporary output could not be created in the destination directory.");
        }
        return false;
    }
    const QString processOutputPath = stagedOutput.fileName();
    stagedOutput.close();

    // Fast Recompress keeps decoded video in FFmpeg's native pixel formats.
    // Unlike Normal/Full Processing, frames never cross the QImage/RGB boundary
    // and the Qt filter chain is deliberately bypassed.
    if (videoMode == VideoMode_FastRecompress) {
        QProcess videoDecoderProcess;
        QProcess ffmpeg;
        QByteArray diagnostics;
        bool cancelled = false;

        const QFileInfo inputInfo(options.inputPath);
        const QString inputPath = inputInfo.absoluteFilePath();
        if (VDQtSourceSafety::isScriptPath(inputPath)) {
            videoDecoderProcess.setWorkingDirectory(inputInfo.absolutePath());
            ffmpeg.setWorkingDirectory(inputInfo.absolutePath());
        }

        double inputStartSeconds = sourceStartSeconds;
        if (!decoder.isAvsNative())
            inputStartSeconds += sourceAudioProbe.videoStartOffsetSeconds;
        inputStartSeconds = std::max(0.0, inputStartSeconds);
        const double outputDurationSeconds = preserveNativeVfr
            ? sourceDurationSeconds
            : static_cast<double>(framesToExport) / fps;

        QStringList videoFilters;
        // Input seeking establishes the first selected frame. An explicit frame
        // bound prevents a retimed/slowed output from consuming frames beyond
        // the editor's exclusive out marker.
        videoFilters << QString("trim=start_frame=0:end_frame=%1")
                            .arg(selectedSourceFrames);
        if (step > 1)
            videoFilters << QString("select=not(mod(n\\,%1))").arg(step);

        if (options.convertFpsPreserveDuration && options.customFps > 0.0) {
            videoFilters << QStringLiteral("setpts=PTS-STARTPTS")
                         << QString("fps=fps=%1:round=near")
                                .arg(QString::number(fps, 'f', 12));
        } else if (options.customFps > 0.0) {
            videoFilters << QString("setpts=N/(%1*TB)")
                                .arg(QString::number(fps, 'f', 12));
        } else {
            videoFilters << QStringLiteral("setpts=PTS-STARTPTS");
        }

        QStringList encoderArgs;
        encoderArgs << "-nostdin" << "-y"
                    << "-f" << "nut" << "-i" << "-";
        if (sourceHasAudio) {
            if (inputStartSeconds > 1e-9)
                encoderArgs << "-ss" << QString::number(inputStartSeconds, 'f', 9);
            appendInputFile(encoderArgs, inputPath);
        }
        encoderArgs << "-map" << "0:v:0";
        if (sourceHasAudio) {
            if (decoder.isAvsNative())
                encoderArgs << "-map" << "1:a:0";
            else
                encoderArgs << "-map"
                            << QString("1:%1").arg(sourceAudioProbe.bestAudioStreamIndex);
        }

        QString videoEncodingError;
        QString fastPixelFormat;
        if (!appendVideoEncoderArguments(
                encoderArgs, selectedVideoParams, decoder.getSourceBitDepth(),
                decoder.sourceHasAlpha(), preserveNativeVfr,
                options.containerType.toLower(), &videoEncodingError,
                &fastPixelFormat)) {
            if (parentWidget)
                QMessageBox::critical(parentWidget, "Video Precision Error", videoEncodingError);
            removePartialOutput(processOutputPath);
            return false;
        }
        if (fastPixelFormat.isEmpty()) {
            fastPixelFormat = decoder.getSourceBitDepth() > 8
                ? QStringLiteral("yuv420p10le")
                : (decoder.sourceHasAlpha() ? QStringLiteral("rgba")
                                            : QStringLiteral("yuv420p"));
            encoderArgs << "-pix_fmt" << fastPixelFormat;
        }

        if (options.convertFpsPreserveDuration || options.customFps > 0.0) {
            encoderArgs << "-r" << QString::number(fps, 'f', 12)
                        << "-fps_mode" << "cfr";
        } else {
            encoderArgs << "-fps_mode" << "vfr";
            if (preserveNativeVfr) {
                encoderArgs << "-enc_time_base:v" << "1:1000000"
                            << "-avoid_negative_ts" << "disabled";
            }
        }

        if (sourceHasAudio) {
            if (options.audioMode == AudioMode_DirectStreamCopy)
                encoderArgs << "-c:a" << "copy";
            else
                encoderArgs << VDQtCodecEngine::buildFfmpegAudioEncodeArguments(
                    configuredAudioParams());
        }

        encoderArgs << "-t" << QString::number(outputDurationSeconds, 'f', 9);
        appendMetadataArguments(encoderArgs, options.metadata);
        appendContainerArguments(encoderArgs, options);
        encoderArgs << processOutputPath;

        QStringList decoderArgs;
        decoderArgs << "-nostdin" << "-hide_banner" << "-loglevel" << "error";
        if (inputStartSeconds > 1e-9)
            decoderArgs << "-ss" << QString::number(inputStartSeconds, 'f', 9);
        appendInputFile(decoderArgs, inputPath);
        decoderArgs << "-map" << "0:v:0"
                    << "-vf" << videoFilters.join(QLatin1Char(','))
                    << "-an"
                    << "-c:v" << "rawvideo"
                    << "-pix_fmt" << fastPixelFormat;
        if (options.convertFpsPreserveDuration || options.customFps > 0.0) {
            decoderArgs << "-r" << QString::number(fps, 'f', 12)
                        << "-fps_mode" << "cfr";
        } else {
            decoderArgs << "-fps_mode" << "vfr"
                        << "-enc_time_base:v" << "1:1000000"
                        << "-avoid_negative_ts" << "disabled";
        }
        decoderArgs << "-t" << QString::number(outputDurationSeconds, 'f', 9)
                    << "-f" << "nut" << "-";

        qDebug() << "[Exporter] Fast Recompress decoder args:"
                 << decoderArgs.join(" ");
        qDebug() << "[Exporter] Fast Recompress encoder args:"
                 << encoderArgs.join(" ");

        QProgressDialog progress(
            "Fast recompressing video in native pixel formats...", "Cancel",
            0, 0, parentWidget);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);

        // QProcess connects the producer's stdout directly to the encoder's
        // stdin, so planar frames never accumulate in application memory.
        videoDecoderProcess.setStandardOutputProcess(&ffmpeg);
        ffmpeg.start("ffmpeg", encoderArgs);
        if (!ffmpeg.waitForStarted(3000)) {
            removePartialOutput(processOutputPath);
            if (parentWidget)
                QMessageBox::critical(parentWidget, "Fast Recompress Failed",
                                      "The FFmpeg encoder process could not be started.");
            return false;
        }
        videoDecoderProcess.start("ffmpeg", decoderArgs);
        if (!videoDecoderProcess.waitForStarted(3000)) {
            stopProcess(ffmpeg);
            removePartialOutput(processOutputPath);
            if (parentWidget)
                QMessageBox::critical(parentWidget, "Fast Recompress Failed",
                                      "The native-format FFmpeg decoder process could not be started.");
            return false;
        }

        const bool processOk = waitForProcessPair(
            videoDecoderProcess, ffmpeg, progress, diagnostics, cancelled)
            && validOutputFile(processOutputPath);
        const bool success = processOk && outputStillSafe()
                          && replaceWithStagedFile(processOutputPath, options.outputPath);
        progress.setRange(0, 100);
        progress.setValue(100);
        progress.close();
        if (!success) {
            removePartialOutput(processOutputPath);
            if (!cancelled && parentWidget) {
                QString message = processOk
                    ? QStringLiteral(
                          "The completed output could not be committed to its destination.")
                    : QString::fromUtf8(diagnostics).trimmed();
                if (message.isEmpty())
                    message = QStringLiteral("FFmpeg did not produce a valid output file.");
                if (options.audioMode == AudioMode_DirectStreamCopy && sourceHasAudio) {
                    message.prepend(
                        "The destination container must support the source audio codec when "
                        "Audio > Direct stream copy is selected.\n\n");
                }
                QMessageBox::critical(parentWidget, "Fast Recompress Failed", message);
            }
        }
        return success;
    }

    // 0. Direct Stream Copy Mode (For media files / containers)
    if (videoMode == VideoMode_DirectStreamCopy && decoder.isAvsNative()) {
        if (parentWidget) {
            QMessageBox::critical(parentWidget, "Unsupported Direct Copy Operation",
                                  "AviSynth output consists of decoded frames and cannot be direct-stream-copied. "
                                  "Choose a recompress mode.");
        }
        return false;
    }
    if (videoMode == VideoMode_DirectStreamCopy && !decoder.isAvsNative()) {
        if (options.decimateFactor > 1 || options.customFps > 0.0) {
            if (parentWidget) {
                QMessageBox::critical(parentWidget, "Unsupported Direct Copy Operation",
                    "Direct stream copy cannot decimate frames or change frame rate. "
                    "Choose a recompress mode for this operation.");
            }
            return false;
        }
        QProcess ffmpeg;
        QByteArray diagnostics;
        bool cancelled = false;
        QStringList args;
        args << "-y";

        if (hasFrameSelection) {
            if (parentWidget && !options.smartRendering) {
                const auto answer = QMessageBox::warning(
                    parentWidget,
                    "Keyframe-Aligned Direct Copy",
                    "Compressed direct-stream-copy cuts are keyframe aligned and may begin before "
                    "the selected frame. Use a recompress mode for an exact frame boundary.",
                    QMessageBox::Ok | QMessageBox::Cancel,
                    QMessageBox::Cancel);
                if (answer != QMessageBox::Ok)
                    return false;
            }
            // A copied video stream necessarily retains packets back to the
            // preceding keyframe. If audio is transcoded, FFmpeg's default
            // accurate seek would discard that same preroll only from audio,
            // leaving silence/desynchronization until the requested marker.
            // Preserve preroll consistently across both streams.
            if (options.audioMode != AudioMode_DirectStreamCopy)
                args << "-noaccurate_seek";
            const double inputStartSeconds = std::max(
                0.0, sourceAudioProbe.videoStartOffsetSeconds + sourceStartSeconds);
            args << "-ss" << QString::number(inputStartSeconds, 'f', 9);
            appendInputFile(args, options.inputPath);
            args << "-t" << QString::number(sourceDurationSeconds, 'f', 6);
        } else {
            appendInputFile(args, options.inputPath);
        }

        args << "-map" << "0:v:0";
        args << "-c:v" << "copy";

        if (sourceHasAudio) {
            args << "-map" << QString("0:%1").arg(sourceAudioProbe.bestAudioStreamIndex);
            if (options.audioMode == AudioMode_DirectStreamCopy) {
                args << "-c:a" << "copy";
            } else {
                args << VDQtCodecEngine::buildFfmpegAudioEncodeArguments(
                    configuredAudioParams());
            }
        }

        if (options.fastStart || options.containerType.contains("faststart")) {
            args << "-movflags" << "+faststart";
        }

        QString container = options.containerType.toLower();
        if (container == "webm" || options.outputPath.endsWith(".webm", Qt::CaseInsensitive)) args << "-f" << "webm";
        else if (container == "nut" || options.outputPath.endsWith(".nut", Qt::CaseInsensitive)) args << "-f" << "nut";
        else if (container.startsWith("mov") || options.outputPath.endsWith(".mov", Qt::CaseInsensitive)) args << "-f" << "mov";
        else if (container.startsWith("mp4") || options.outputPath.endsWith(".mp4", Qt::CaseInsensitive)) args << "-f" << "mp4";
        else if (container == "mkv" || options.outputPath.endsWith(".mkv", Qt::CaseInsensitive)) args << "-f" << "matroska";
        else if (container.startsWith("avi") || options.outputPath.endsWith(".avi", Qt::CaseInsensitive)) args << "-f" << "avi";

        appendMetadataArguments(args, options.metadata);
        args << processOutputPath;

        qDebug() << "[Exporter] Direct Stream Copy ffmpeg args:" << args.join(" ");

        QProgressDialog progress("Direct stream copy in progress...", "Cancel", 0, 100, parentWidget);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(30);

        ffmpeg.start("ffmpeg", args);
        if (!ffmpeg.waitForStarted(3000)) {
            removePartialOutput(processOutputPath);
            return false;
        }

        const bool processOk = waitForProcess(ffmpeg, progress, diagnostics, cancelled)
                            && validOutputFile(processOutputPath);
        const bool ok = processOk && outputStillSafe()
                     && replaceWithStagedFile(processOutputPath, options.outputPath);
        progress.setValue(100);
        if (!ok) {
            removePartialOutput(processOutputPath);
            if (!cancelled && parentWidget) {
                QMessageBox::critical(parentWidget, "Direct Copy Failed",
                                      processOk
                                          ? QStringLiteral("The completed output could not be committed to its destination.")
                                          : QString::fromUtf8(diagnostics).trimmed());
            }
        }
        return ok;
    }

    // Determine output resolution (reflecting any Resize filter if Full Processing Mode)
    QImage sampleFrame = decoder.getFrameImage(startFrame);
    if (sampleFrame.isNull()) {
        return false;
    }

    bool applyFilters = (videoMode == VideoMode_FullProcessing);
    int outW = sampleFrame.width();
    int outH = sampleFrame.height();
    int inputFramesToProcess = framesToExport;
    double sourceSelectionOutputFps = fps;
    int filterFramesPerInput = 1;
    if (applyFilters) {
        QImage filteredSample = VDQtFilterSystem::instance().processFrame(sampleFrame);
        if (filteredSample.isNull()) {
            if (parentWidget) QMessageBox::critical(parentWidget, "Filter Error", "The filter chain rejected the first frame.");
            return false;
        }
        outW = filteredSample.width();
        outH = filteredSample.height();

        const VDFilterTimingInfo timing = VDQtFilterSystem::instance().getTimingInfo();
        if (!timing.sequenceSupported || timing.outputFramesPerInput <= 0) {
            if (parentWidget) QMessageBox::critical(parentWidget, "Filter Error", "The configured temporal filter chain is not supported.");
            return false;
        }
        filterFramesPerInput = timing.outputFramesPerInput;
        if (filterFramesPerInput > 1) {
            fps *= filterFramesPerInput;
            framesToExport *= filterFramesPerInput;
        }
    }
    const double outputDurationSeconds = preserveNativeVfr
        ? sourceDurationSeconds
        : static_cast<double>(framesToExport) / fps;

    // Fetch user-configured codec settings
    const VDVideoCodecParams& vParams = selectedVideoParams;
    const VDAudioCodecParams aParams = configuredAudioParams();

    // 1. Audio Source Configuration
    QString audioSrcMedia;
    if (options.audioMode == AudioMode_DirectStreamCopy) {
        if (!decoder.isAvsNative() && sourceHasAudio) {
            audioSrcMedia = options.inputPath;
        }
        // An AviSynth clip may trim, delay, amplify, replace, or synthesize
        // audio. Copying a filename guessed from its script would bypass those
        // operations, so native clips are streamed through a temporary PCM file.
    }

    bool isDirectCopyMediaAudio = !audioSrcMedia.isEmpty();

    QTemporaryDir temporaryDirectory;
    QString tempAudioPath;
    if (!isDirectCopyMediaAudio && audioPlayer && audioPlayer->hasAudio()) {
        if (!temporaryDirectory.isValid()) {
            qWarning() << "[Exporter] Unable to create a secure temporary directory.";
            return false;
        }
        tempAudioPath = temporaryDirectory.filePath("audio.wav");

        int sampleRate = audioPlayer->getSampleRate();
        if (sampleRate <= 0) sampleRate = 48000;

        const int64_t startSample = static_cast<int64_t>(std::llround(sourceStartSeconds * sampleRate));
        const int64_t sampleCount = static_cast<int64_t>(std::llround(outputDurationSeconds * sampleRate));

        QProgressDialog audioProgress(
            "Preparing processed audio...", "Cancel", 0, 100, parentWidget);
        audioProgress.setWindowModality(Qt::WindowModal);
        audioProgress.setMinimumDuration(0);
        const bool audioPrepared = audioPlayer->exportAudioToFile(
            tempAudioPath, startSample, sampleCount,
            [&audioProgress](int current, int total) {
                audioProgress.setRange(0, std::max(1, total));
                audioProgress.setValue(std::clamp(current, 0, std::max(1, total)));
                QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
                return !audioProgress.wasCanceled();
            });
        const bool audioCancelled = audioProgress.wasCanceled();
        audioProgress.close();

        if (audioPrepared) {
            qDebug() << "[Exporter] Successfully prepared audio stream for export (samples:" << startSample << "count:" << sampleCount << "):" << tempAudioPath;
        } else {
            if (!audioCancelled && parentWidget) {
                QMessageBox::critical(parentWidget, "Audio Export Error",
                                      "The requested processed audio range could not be decoded. "
                                      "The video export was stopped to avoid silently dropping audio.");
            }
            return false;
        }
    }

    // 2. FFmpeg input arguments (ALL inputs before encoding flags)
    QProcess ffmpeg;
    QStringList args;

    QString rawInputPixelFormat = QStringLiteral("rgb24");
    QImage::Format rawInputImageFormat = QImage::Format_RGB888;
    if (sampleFrame.depth() > 32) {
        rawInputPixelFormat = QStringLiteral("rgba64le");
        rawInputImageFormat = QImage::Format_RGBA64;
    } else if (decoder.sourceHasAlpha()) {
        rawInputPixelFormat = QStringLiteral("rgba");
        rawInputImageFormat = QImage::Format_RGBA8888;
    }

    args << "-y";
    if (preserveNativeVfr) {
        args << "-f" << "nut" << "-i" << "-";
    } else {
        args << "-f" << "rawvideo"
             << "-pix_fmt" << rawInputPixelFormat
             << "-s" << QString("%1x%2").arg(outW).arg(outH)
             << "-r" << QString::number(fps, 'f', 12)
             << "-i" << "-"; // input 0: raw video stream from stdin
    }

    bool hasAudioInput = false;
    if (isDirectCopyMediaAudio) {
        hasAudioInput = true;
        // This is a second input whose time origin is the container, while the
        // editor timeline is normalized to the first video timestamp. Seek by
        // both offsets so leading pre-video audio is not muxed at video t=0.
        const double audioInputStart = std::max(
            0.0, sourceAudioProbe.videoStartOffsetSeconds + sourceStartSeconds);
        if (audioInputStart > 1e-9)
            args << "-ss" << QString::number(audioInputStart, 'f', 9);
        appendInputFile(args, audioSrcMedia);
    } else if (!tempAudioPath.isEmpty() && QFile::exists(tempAudioPath)) {
        hasAudioInput = true;
        args << "-i" << tempAudioPath;
    }

    // 3. Stream mappings
    args << "-map" << "0:v:0";
    if (hasAudioInput) {
        if (isDirectCopyMediaAudio)
            args << "-map" << QString("1:%1").arg(sourceAudioProbe.bestAudioStreamIndex);
        else
            args << "-map" << "1:a:0";
    }

    // 4. Video Codec & Options
    const QString container = options.containerType.toLower();
    QString videoEncodingError;
    if (!appendVideoEncoderArguments(
            args, vParams, decoder.getSourceBitDepth(), decoder.sourceHasAlpha(),
            preserveNativeVfr, container, &videoEncodingError)) {
        if (parentWidget)
            QMessageBox::critical(parentWidget, "Video Precision Error", videoEncodingError);
        return false;
    }
    if (preserveNativeVfr) {
        args << "-fps_mode" << "vfr"
             << "-enc_time_base:v" << "1:1000000"
             << "-avoid_negative_ts" << "disabled";
    }

    // 5. Audio Codec & Options
    if (hasAudioInput) {
        if (options.audioMode == AudioMode_DirectStreamCopy) {
            // Native AviSynth audio is staged in a WAV that already carries
            // its true PCM integer/float depth. Stream-copy that codec too;
            // forcing pcm_s16le here silently quantized 24/32-bit and float
            // clips. Incompatible destination containers now fail visibly.
            args << "-c:a" << "copy";
        } else {
            args << VDQtCodecEngine::buildFfmpegAudioEncodeArguments(aParams);
        }

        // Bound the mux to the generated video duration. Using -shortest here
        // would let a slightly shorter audio stream truncate valid video frames.
        const double muxDuration = outputDurationSeconds;
        args << "-t" << QString::number(muxDuration, 'f', 9);
    }

    // 6. Container Format & Output Path
    appendMetadataArguments(args, options.metadata);
    appendContainerArguments(args, options);

    args << processOutputPath;

    qDebug() << "[Exporter] Launching ffmpeg with args:" << args.join(" ");

    QByteArray diagnostics;
    bool cancelled = false;
    bool writeFailed = false;
    ffmpeg.start("ffmpeg", args);
    if (!ffmpeg.waitForStarted(3000)) {
        qWarning() << "[Exporter] Failed to start ffmpeg process.";
        removePartialOutput(processOutputPath);
        return false;
    }

    QProgressDialog progress("Exporting processed video...", "Cancel", 0, 100, parentWidget);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QElapsedTimer timer;
    timer.start();

    TimestampedNutWriter timestampedWriter;
    if (preserveNativeVfr) {
        const AVPixelFormat inputPixelFormat =
            av_get_pix_fmt(rawInputPixelFormat.toUtf8().constData());
        const int finalSourceFrame = startFrame
            + std::max(0, inputFramesToProcess - 1) * step;
        const double finalSourceTimestamp =
            decoder.getFrameTimestampSeconds(std::min(finalSourceFrame, endFrame));
        const double finalPhaseDuration =
            (sourceStartSeconds + sourceDurationSeconds - finalSourceTimestamp)
            / std::max(1, filterFramesPerInput);
        const AVRational terminalFrameRate =
            std::isfinite(finalPhaseDuration) && finalPhaseDuration > 0.0
                ? av_d2q(1.0 / finalPhaseDuration, 1000000)
                : av_d2q(sourceFps * filterFramesPerInput, 1000000);
        if (inputPixelFormat == AV_PIX_FMT_NONE
            || !timestampedWriter.open(ffmpeg, progress, diagnostics, cancelled,
                                       outW, outH, inputPixelFormat,
                                       terminalFrameRate)) {
            stopProcess(ffmpeg);
            removePartialOutput(processOutputPath);
            return false;
        }
    }

    int doneCount = 0;
    for (int inputIndex = 0; inputIndex < inputFramesToProcess; ++inputIndex) {
        if (progress.wasCanceled()) {
            cancelled = true;
            break;
        }

        int f = startFrame;
        if (useTimestampFrameMapping) {
            const double requestedTimestamp = sourceStartSeconds
                                            + static_cast<double>(inputIndex) / sourceSelectionOutputFps;
            int lo = startFrame;
            int hi = endFrame;
            while (lo < hi) {
                const int mid = lo + (hi - lo + 1) / 2;
                const double timestamp = decoder.getFrameTimestampSeconds(mid);
                if (std::isfinite(timestamp) && timestamp <= requestedTimestamp + 1e-9)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            f = lo;
        } else if (options.convertFpsPreserveDuration && options.customFps > 0.0) {
            // Timestamp-less elementary streams cannot be searched by PTS.
            // Fall back to CFR index resampling instead of repeatedly choosing
            // frame zero when every timestamp query returns NaN.
            const double sourceOffset = static_cast<double>(inputIndex)
                                      * sourceFps / sourceSelectionOutputFps;
            f = startFrame + static_cast<int>(std::floor(sourceOffset + 1e-9));
        } else {
            f += inputIndex * step;
        }
        f = std::clamp(f, startFrame, endFrame);

        QImage rawFrame = decoder.getFrameImage(f);
        if (rawFrame.isNull()) {
            appendBounded(diagnostics, QString("Failed to decode source frame %1.\n").arg(f).toUtf8());
            writeFailed = true;
            break;
        }

        QList<QImage> filteredFrames;
        if (applyFilters) {
            if (!VDQtFilterSystem::instance().processFrameSequence(rawFrame, filteredFrames)
                || filteredFrames.size() != filterFramesPerInput) {
                appendBounded(diagnostics, QString("The temporal filter chain failed at source frame %1.\n").arg(f).toUtf8());
                writeFailed = true;
                break;
            }
        } else {
            filteredFrames.append(rawFrame);
        }

        double vfrFrameStart = 0.0;
        double vfrFrameEnd = 0.0;
        if (preserveNativeVfr) {
            vfrFrameStart = decoder.getFrameTimestampSeconds(f);
            const int nextSourceFrame = f + step;
            vfrFrameEnd = nextSourceFrame <= endFrame
                ? decoder.getFrameTimestampSeconds(nextSourceFrame)
                : sourceStartSeconds + sourceDurationSeconds;
            if (!std::isfinite(vfrFrameStart) || !std::isfinite(vfrFrameEnd)
                || vfrFrameEnd <= vfrFrameStart) {
                appendBounded(diagnostics,
                    QString("Source frame %1 has an invalid presentation duration.\n")
                        .arg(f).toUtf8());
                writeFailed = true;
                break;
            }
        }

        for (int phase = 0; phase < filteredFrames.size(); ++phase) {
            QImage filtered = filteredFrames.at(phase);
            if (filtered.isNull() || filtered.width() != outW || filtered.height() != outH) {
                appendBounded(diagnostics,
                    QString("Filter output dimensions changed at frame %1; expected %2x%3, got %4x%5.\n")
                        .arg(f).arg(outW).arg(outH).arg(filtered.width()).arg(filtered.height()).toUtf8());
                writeFailed = true;
                break;
            }
            if (filtered.format() != rawInputImageFormat)
                filtered = filtered.convertToFormat(rawInputImageFormat);

            bool frameWritten = false;
            if (preserveNativeVfr) {
                const double phaseStart = vfrFrameStart
                    + (vfrFrameEnd - vfrFrameStart)
                        * static_cast<double>(phase) / filteredFrames.size();
                const double phaseEnd = vfrFrameStart
                    + (vfrFrameEnd - vfrFrameStart)
                        * static_cast<double>(phase + 1) / filteredFrames.size();
                const qint64 ptsUs = std::llround(
                    (phaseStart - sourceStartSeconds) * 1000000.0);
                const qint64 endUs = std::llround(
                    (phaseEnd - sourceStartSeconds) * 1000000.0);
                frameWritten = endUs > ptsUs
                    && timestampedWriter.writeImage(filtered, ptsUs, endUs - ptsUs);
            } else {
                frameWritten = writeFrame(
                    ffmpeg, filtered, progress, diagnostics, cancelled);
            }
            if (!frameWritten) {
                writeFailed = !cancelled;
                break;
            }

            if (frameCallback)
                frameCallback(f, rawFrame, filtered);
            ++doneCount;
        }
        if (cancelled || writeFailed)
            break;

        int pct = static_cast<int>(95.0 * doneCount / framesToExport);
        progress.setValue(pct);

        double elapsedSec = timer.elapsed() / 1000.0;
        double currentFps = (elapsedSec > 0) ? (doneCount / elapsedSec) : 0;
        double remainingSec = (currentFps > 0) ? ((framesToExport - doneCount) / currentFps) : 0;

        progress.setLabelText(QString("Exporting frame %1 of %2 (%3%)\nSpeed: %4 fps | ETA: %5s")
            .arg(doneCount)
            .arg(framesToExport)
            .arg(static_cast<int>(100.0 * doneCount / framesToExport))
            .arg(currentFps, 0, 'f', 1)
            .arg(static_cast<int>(remainingSec)));

        QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
    }

    bool processOk = false;
    if (cancelled || writeFailed) {
        stopProcess(ffmpeg);
        drainProcessOutput(ffmpeg, diagnostics);
    } else {
        if (preserveNativeVfr && !timestampedWriter.finish()) {
            writeFailed = true;
            stopProcess(ffmpeg);
            drainProcessOutput(ffmpeg, diagnostics);
        }
    }
    if (!cancelled && !writeFailed) {
        ffmpeg.closeWriteChannel();
        progress.setLabelText("Finalizing video stream and container metadata...");
        progress.setValue(96);
        QApplication::processEvents(QEventLoop::AllEvents, kProcessPollMs);
        processOk = waitForProcess(ffmpeg, progress, diagnostics, cancelled);
    }

    progress.setValue(100);
    QApplication::processEvents();
    progress.close();

    const bool encoded = !cancelled && !writeFailed && processOk
                      && doneCount == framesToExport && validOutputFile(processOutputPath);
    const bool success = encoded && outputStillSafe()
                      && replaceWithStagedFile(processOutputPath, options.outputPath);
    if (!success) {
        removePartialOutput(processOutputPath);
        QString errOutput = QString::fromUtf8(diagnostics);
        if (encoded)
            errOutput = "The completed output could not be committed to its destination.";
        if (writeFailed && errOutput.trimmed().isEmpty())
            errOutput = "The FFmpeg input pipe closed before all frames were written.";
        qWarning() << "[Exporter] FFmpeg export error:" << errOutput;
        VDLogWindow::instance(parentWidget)->appendLog(QString("[Export Error] %1").arg(errOutput));
        if (parentWidget && !cancelled) {
            QString userMsg = "Video export failed.\n\n";
            if (options.audioMode == AudioMode_DirectStreamCopy && isDirectCopyMediaAudio) {
                userMsg += "Note: In Audio -> Direct stream copy mode, the destination container must support the source audio codec natively (for example, standard AVI cannot encapsulate Opus audio).\n\n"
                           "To resolve:\n"
                           "1. Switch Audio to 'Full processing mode' to re-encode.\n"
                           "2. Or export to MKV/MP4/MOV container that supports this audio codec.\n\nDetails:\n";
            }
            userMsg += errOutput.trimmed();
            QMessageBox::critical(parentWidget, "Export Error", userMsg);
        }
    }

    return success;
}
