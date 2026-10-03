// Local FIFO/NUT frame server. A worker thread decodes and filters frames, feeds
// raw video to an ffmpeg child process, and publishes the muxed NUT stream at the
// requested named pipe. Cancellation closes the pipeline and removes the FIFO.
#include "VDQtFrameServer.h"
#include "VDQtFilterFrameContext.h"
#include "VDQtTimingMath.h"

#include "VDQtVideoDecoder.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <sys/stat.h>

namespace {

QString processError(QProcess& process) {
    QString message = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (message.isEmpty()) message = process.errorString();
    return message;
}

bool writeImage(QProcess& process,
                const QImage& image,
                QImage::Format format,
                std::atomic_bool& cancelled) {
    // QProcess buffers stdin in memory. Bound that buffer so a slow FIFO reader
    // creates backpressure instead of letting a long serve consume all RAM.
    const QImage rgb = image.convertToFormat(format);
    if (rgb.isNull()) return false;
    const int bytesPerPixel = format == QImage::Format_RGBA64 ? 8
        : format == QImage::Format_RGBA8888 ? 4 : 3;
    for (int row = 0; row < rgb.height(); ++row) {
        const char *data = reinterpret_cast<const char *>(rgb.constScanLine(row));
        qint64 remaining = static_cast<qint64>(rgb.width()) * bytesPerPixel;
        while (remaining > 0) {
            if (cancelled.load(std::memory_order_relaxed)) return false;
            const qint64 accepted = process.write(data, remaining);
            if (accepted < 0) return false;
            if (!accepted) {
                if (!process.waitForBytesWritten(100) && process.state() == QProcess::NotRunning)
                    return false;
                continue;
            }
            data += accepted;
            remaining -= accepted;
            while (process.bytesToWrite() > 2 * 1024 * 1024) {
                if (cancelled.load(std::memory_order_relaxed)) return false;
                if (!process.waitForBytesWritten(100)
                    && process.state() == QProcess::NotRunning)
                    return false;
            }
        }
    }
    return true;
}

} // namespace

VDQtFrameServer::VDQtFrameServer(QObject *parent)
    : QObject(parent) {
}

VDQtFrameServer::~VDQtFrameServer() {
    stop();
    delete mThread;
}

bool VDQtFrameServer::start(const Config& config, QString *errorMessage) {
    // Validate and create the FIFO synchronously so callers either receive a
    // usable endpoint or an immediate error. Decoding starts only after this.
    if (isRunning()) {
        if (errorMessage) *errorMessage = QStringLiteral("A frame server is already running.");
        return false;
    }
    if (mThread) {
        mThread->wait();
        delete mThread;
        mThread = nullptr;
    }
    if (config.sourcePath.isEmpty() || config.pipePath.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("A source and FIFO path are required.");
        return false;
    }
    if (config.hasExplicitTimeline() && config.timelineSegments.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("The edited timeline contains no frames to serve.");
        return false;
    }
    if (!std::isfinite(config.customFps) || config.customFps < 0
        || config.decimateFactor < 1
        || (config.convertFpsPreserveDuration && config.customFps <= 0)) {
        if (errorMessage) *errorMessage = QStringLiteral("The frame-server rate settings are invalid.");
        return false;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("The ffmpeg executable was not found in PATH.");
        return false;
    }
    const QFileInfo pipeInfo(config.pipePath);
    if (pipeInfo.exists() || pipeInfo.isSymLink()) {
        if (errorMessage) *errorMessage = QStringLiteral("The frame-server path already exists.");
        return false;
    }
    const QByteArray encodedPipe = QFile::encodeName(pipeInfo.absoluteFilePath());
    if (::mkfifo(encodedPipe.constData(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) != 0) {
        if (errorMessage) {
            *errorMessage = QString("Could not create the frame-server FIFO: %1")
                .arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    Config absoluteConfig = config;
    absoluteConfig.sourcePath = QFileInfo(config.sourcePath).absoluteFilePath();
    absoluteConfig.pipePath = pipeInfo.absoluteFilePath();
    if (!config.audioPath.isEmpty())
        absoluteConfig.audioPath = QFileInfo(config.audioPath).absoluteFilePath();
    mCancelRequested.store(false, std::memory_order_relaxed);
    mThread = QThread::create([this, absoluteConfig]() { run(absoluteConfig); });
    mThread->start();
    return true;
}

void VDQtFrameServer::stop() {
    // run() polls this flag during scans, decode, and pipe writes. Waiting here
    // makes destruction safe and guarantees the worker no longer touches this.
    mCancelRequested.store(true, std::memory_order_relaxed);
    if (mThread && mThread->isRunning()) mThread->wait();
}

bool VDQtFrameServer::isRunning() const {
    return mThread && mThread->isRunning();
}

void VDQtFrameServer::run(Config config) {
    // Phase 1: prove ordinary-media count/timestamps once. Native AVS already
    // has an authoritative clip length and rational CFR clock; ensureFrameIndex
    // returns that proof without evaluating the entire (possibly heavy) graph.
    QString error;
    VDQtVideoDecoder decoder;
    decoder.setDecompressionConfig(
        config.decompressionFormat, config.colorSpace, config.componentRange);
    decoder.setErrorMode(config.errorMode);
    if (!decoder.openFile(config.sourcePath)) {
        error = decoder.getLastError();
    }

    int endFrame = config.endFrame;
    if (error.isEmpty()) {
        const VDQtVideoDecoder::VDScanResult scan = decoder.ensureFrameIndex(
            [this](int, int) {
                return !mCancelRequested.load(std::memory_order_relaxed);
            });
        if (scan.cancelled) {
            error = QStringLiteral("Frame serving was cancelled.");
        } else if (!scan.errorMessage.isEmpty()) {
            error = scan.errorMessage;
        }
    }
    VDQtTimeline timeline;
    if (error.isEmpty()) {
        timeline.reset(decoder.getFrameCount(), true);
        if (error.isEmpty() && config.hasExplicitTimeline()
            && !timeline.replaceSegments(config.timelineSegments, &error)) {
            if (error.isEmpty())
                error = QStringLiteral("The frame-server timeline is invalid.");
        }
        if (config.endFrame < 0)
            endFrame = static_cast<int>(timeline.frameCount() - 1);
    }
    if (error.isEmpty()
        && (config.startFrame < 0 || config.startFrame > endFrame
            || endFrame >= timeline.frameCount())) {
        error = QStringLiteral("The frame-server range is outside the decoded source.");
    }

    // Phase 2: instantiate an isolated filter graph and render one frame. The
    // probe establishes the fixed dimensions and the output phase multiplier
    // required to describe the raw input stream to ffmpeg.
    VDQtFilterSystem filters;
    filters.replaceActiveChainTransient(config.filters);
    const int outputPhases = std::max(
        1, filters.getTimingInfo().outputFramesPerInput);
    const double nativeFps = decoder.getFps() > 0.0 ? decoder.getFps() : 30.0;
    const double sourceFps = config.customFps > 0 && !config.convertFpsPreserveDuration
        ? config.customFps : nativeFps;
    const int step = config.convertFpsPreserveDuration ? 1 : config.decimateFactor;
    const double outputFps = (config.convertFpsPreserveDuration
        ? config.customFps : sourceFps / step) * outputPhases;
    if (error.isEmpty() && (!std::isfinite(outputFps) || outputFps <= 0))
        error = QStringLiteral("The frame-server output rate cannot be represented.");
    const QList<VDQtTimelineSegment> segments = timeline.isIdentity()
        ? QList<VDQtTimelineSegment>() : timeline.segments();

    QList<QImage> firstImages;
    if (error.isEmpty()) {
        const int sourceFrame = static_cast<int>(
            timeline.mapOutputToSource(config.startFrame));
        const QImage first = decoder.getFrameImage(sourceFrame);
        const VDFilterFrameContext context = VDQtFilterContextForFrame(
            decoder, segments, config.startFrame, sourceFps);
        if (first.isNull() || !filters.processFrameSequence(first, firstImages, context)
            || firstImages.isEmpty() || firstImages.first().isNull()) {
            error = QStringLiteral("Could not prepare the first served frame.");
            if (!filters.lastError().isEmpty()) error += '\n' + filters.lastError();
        }
    }

    // Phase 3: ffmpeg wraps RGB frames (and optional PCM audio) in NUT. Using a
    // real muxer supplies timestamps and stream metadata that raw FIFO bytes do
    // not carry, while keeping video processing inside this application.
    QProcess ffmpeg;
    QImage::Format imageFormat = QImage::Format_RGB888;
    QString pixelFormat = QStringLiteral("rgb24");
    if (!firstImages.isEmpty() && firstImages.first().depth() > 32) {
        imageFormat = QImage::Format_RGBA64;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
        pixelFormat = QStringLiteral("rgba64be");
#else
        pixelFormat = QStringLiteral("rgba64le");
#endif
    } else if (!firstImages.isEmpty() && firstImages.first().hasAlphaChannel()) {
        imageFormat = QImage::Format_RGBA8888;
        pixelFormat = QStringLiteral("rgba");
    }
    if (error.isEmpty()) {
        const QSize size = firstImages.first().size();
        QStringList arguments{
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("-pixel_format"), pixelFormat,
            QStringLiteral("-video_size"), QString("%1x%2").arg(size.width()).arg(size.height()),
            QStringLiteral("-framerate"), QString::number(outputFps, 'g', 17),
            QStringLiteral("-i"), QStringLiteral("pipe:0")
        };
        const bool hasAudio = !config.audioPath.isEmpty()
            && QFileInfo::exists(config.audioPath);
        if (hasAudio)
            arguments << QStringLiteral("-i") << config.audioPath;
        arguments << QStringLiteral("-map") << QStringLiteral("0:v:0");
        if (hasAudio)
            arguments << QStringLiteral("-map") << QStringLiteral("1:a:0");
        arguments << QStringLiteral("-c:v") << QStringLiteral("rawvideo")
                  << QStringLiteral("-pix_fmt") << pixelFormat
                  << QStringLiteral("-threads:v") << QStringLiteral("1");
        if (hasAudio) {
            arguments << QStringLiteral("-af") << QStringLiteral("apad")
                      << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le")
                      << QStringLiteral("-shortest");
        } else {
            arguments << QStringLiteral("-an");
        }
        arguments << QStringLiteral("-f") << QStringLiteral("nut")
                  << QStringLiteral("-flush_packets") << QStringLiteral("1")
                  << QStringLiteral("-y") << config.pipePath;
        ffmpeg.setProgram(QStringLiteral("ffmpeg"));
        ffmpeg.setArguments(arguments);
        ffmpeg.start(QIODevice::ReadWrite);
        if (!ffmpeg.waitForStarted(5000)) error = ffmpeg.errorString();
    }

    // Phase 4: decode/filter/write serially. Serial operation is intentional:
    // stateful filters and the decoder's sequential fast path depend on order.
    if (error.isEmpty()) Q_EMIT serverStarted(config.pipePath);
    QSize outputSize = firstImages.isEmpty() ? QSize() : firstImages.first().size();
    const int outputDepth = firstImages.isEmpty() ? 0 : firstImages.first().depth();
    const bool outputAlpha = !firstImages.isEmpty() && firstImages.first().hasAlphaChannel();
    long double elapsed = 0;
    int64_t emitted = 0;
    // Widen the loop/chunk boundary before adding a potentially maximal step.
    for (int64_t ordinal = config.startFrame;
         error.isEmpty() && ordinal <= endFrame;
         ordinal += step) {
        const int frameIndex = static_cast<int>(ordinal);
        if (mCancelRequested.load(std::memory_order_relaxed)) {
            error = QStringLiteral("Frame serving was stopped.");
            break;
        }
        QList<QImage> images;
        if (frameIndex == config.startFrame) {
            images = firstImages;
        } else {
            const int sourceFrame = static_cast<int>(
                timeline.mapOutputToSource(frameIndex));
            const QImage frame = decoder.getFrameImage(sourceFrame);
            const VDFilterFrameContext context = VDQtFilterContextForFrame(
                decoder, segments, frameIndex, sourceFps);
            if (frame.isNull() || !filters.processFrameSequence(frame, images, context)
                || images.isEmpty()) {
                error = QString("Could not decode frame %1.").arg(frameIndex);
                if (!filters.lastError().isEmpty()) error += '\n' + filters.lastError();
                break;
            }
        }
        if (images.size() != outputPhases) {
            error = QStringLiteral("The filter chain changed its output phase count while frame serving.");
            break;
        }
        for (const QImage& image : images) {
            if (image.isNull() || image.size() != outputSize
                || image.depth() != outputDepth || image.hasAlphaChannel() != outputAlpha) {
                error = QStringLiteral(
                    "The filter chain changed dimensions or pixel precision while frame serving.");
                break;
            }
        }
        if (!error.isEmpty()) break;

        // This server's documented wire format is CFR NUT, not exact VFR.
        // Sample a single cumulative clock: short phases may have no output
        // tick, long phases repeat, and masks retain their advancing edit time.
        const qint64 chunkEnd = std::min<int64_t>(int64_t(endFrame) + 1, ordinal + step);
        double duration = (chunkEnd - ordinal) / sourceFps;
        if (config.preserveEmptyFrames) {
            const auto begin = VDQtFilterContextForFrame(decoder, segments, ordinal, sourceFps);
            const auto end = VDQtFilterContextForFrame(decoder, segments, chunkEnd, sourceFps);
            const double actual = end.timestampSeconds - begin.timestampSeconds;
            if (std::isfinite(actual) && actual > 0) duration = actual;
        }
        if (!std::isfinite(duration) || duration <= 0) {
            error = QStringLiteral("The served frame duration cannot be represented.");
            break;
        }
        for (int phase = 0; phase < images.size(); ++phase) {
            const long double boundary = elapsed
                + static_cast<long double>(duration) * (phase + 1) / images.size();
            int64_t through = 0;
            if (!VDQtCfrBoundaryFrames(boundary, outputFps, &through) || through < emitted) {
                error = QStringLiteral("The frame-server clock exceeds the supported timestamp range.");
                break;
            }
            while (emitted < through) {
                if (!writeImage(ffmpeg, images.at(phase), imageFormat, mCancelRequested)) {
                    error = mCancelRequested.load(std::memory_order_relaxed)
                        ? QStringLiteral("Frame serving was stopped.")
                        : QStringLiteral("Could not write the served frame: %1").arg(processError(ffmpeg));
                    break;
                }
                ++emitted;
            }
            if (!error.isEmpty()) break;
        }
        elapsed += duration;
    }

    if (ffmpeg.state() != QProcess::NotRunning) {
        if (error.isEmpty()) {
            ffmpeg.closeWriteChannel();
            while (ffmpeg.state() != QProcess::NotRunning && !ffmpeg.waitForFinished(100)) {
                if (mCancelRequested.load(std::memory_order_relaxed)) {
                    error = QStringLiteral("Frame serving was stopped.");
                    ffmpeg.kill();
                    ffmpeg.waitForFinished();
                    break;
                }
            }
            if (error.isEmpty() && (ffmpeg.exitCode() != 0 || ffmpeg.exitStatus() != QProcess::NormalExit))
                error = processError(ffmpeg);
        } else {
            ffmpeg.kill();
            ffmpeg.waitForFinished();
        }
    }
    QFile::remove(config.pipePath);
    Q_EMIT serverFinished(error);
}
