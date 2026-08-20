#include "VDQtFrameServer.h"

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
                std::atomic_bool& cancelled) {
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    for (int row = 0; row < rgb.height(); ++row) {
        const char *data = reinterpret_cast<const char *>(rgb.constScanLine(row));
        qint64 remaining = static_cast<qint64>(rgb.width()) * 3;
        while (remaining > 0) {
            if (cancelled.load(std::memory_order_relaxed)) return false;
            const qint64 accepted = process.write(data, remaining);
            if (accepted < 0) return false;
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
    mCancelRequested.store(true, std::memory_order_relaxed);
    if (mThread && mThread->isRunning()) mThread->wait();
}

bool VDQtFrameServer::isRunning() const {
    return mThread && mThread->isRunning();
}

void VDQtFrameServer::run(Config config) {
    QString error;
    VDQtVideoDecoder decoder;
    decoder.setDecompressionConfig(
        config.decompressionFormat, config.colorSpace, config.componentRange);
    decoder.setErrorMode(config.errorMode);
    if (!decoder.openFile(config.sourcePath)) {
        error = decoder.getLastError();
    }

    int endFrame = config.endFrame;
    if (error.isEmpty() && endFrame < 0) {
        const VDQtVideoDecoder::VDScanResult scan = decoder.scanVideoStream(
            [this](int, int) {
                return !mCancelRequested.load(std::memory_order_relaxed);
            });
        if (scan.cancelled) {
            error = QStringLiteral("Frame serving was cancelled.");
        } else if (!scan.errorMessage.isEmpty()) {
            error = scan.errorMessage;
        } else {
            endFrame = decoder.getFrameCount() - 1;
        }
    }
    VDQtTimeline timeline;
    if (error.isEmpty()) {
        if (!decoder.isFrameCountExact()) {
            const VDQtVideoDecoder::VDScanResult scan = decoder.scanVideoStream(
                [this](int, int) {
                    return !mCancelRequested.load(std::memory_order_relaxed);
                });
            if (scan.cancelled)
                error = QStringLiteral("Frame serving was cancelled.");
            else if (!scan.errorMessage.isEmpty())
                error = scan.errorMessage;
        }
        timeline.reset(decoder.getFrameCount(), true);
        if (error.isEmpty() && !config.timelineSegments.isEmpty()
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

    VDQtFilterSystem filters;
    filters.replaceActiveChainTransient(config.filters);
    const int outputPhases = std::max(
        1, filters.getTimingInfo().outputFramesPerInput);
    const double sourceFps = decoder.getFps() > 0.0 ? decoder.getFps() : 30.0;
    const double outputFps = sourceFps * outputPhases;

    QList<QImage> firstImages;
    if (error.isEmpty()) {
        const int sourceFrame = static_cast<int>(
            timeline.mapOutputToSource(config.startFrame));
        const QImage first = decoder.getFrameImage(sourceFrame);
        VDFilterFrameContext context;
        context.frameNumber = config.startFrame;
        context.timestampSeconds = decoder.getFrameTimestampSeconds(sourceFrame);
        context.frameRate = sourceFps;
        if (first.isNull() || !filters.processFrameSequence(first, firstImages, context)
            || firstImages.isEmpty() || firstImages.first().isNull()) {
            error = QStringLiteral("Could not prepare the first served frame.");
        }
    }

    QProcess ffmpeg;
    if (error.isEmpty()) {
        const QSize size = firstImages.first().size();
        QStringList arguments{
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("-pixel_format"), QStringLiteral("rgb24"),
            QStringLiteral("-video_size"), QString("%1x%2").arg(size.width()).arg(size.height()),
            QStringLiteral("-framerate"), QString::number(outputFps, 'f', 12),
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
                  << QStringLiteral("-pix_fmt") << QStringLiteral("rgb24");
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

    if (error.isEmpty()) Q_EMIT serverStarted(config.pipePath);
    QSize outputSize = firstImages.isEmpty() ? QSize() : firstImages.first().size();
    for (int frameIndex = config.startFrame;
         error.isEmpty() && frameIndex <= endFrame;
         ++frameIndex) {
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
            VDFilterFrameContext context;
            context.frameNumber = frameIndex;
            context.timestampSeconds = decoder.getFrameTimestampSeconds(sourceFrame);
            context.frameRate = sourceFps;
            if (frame.isNull() || !filters.processFrameSequence(frame, images, context)
                || images.isEmpty()) {
                error = QString("Could not decode frame %1.").arg(frameIndex);
                break;
            }
        }
        for (const QImage& image : images) {
            if (image.isNull() || image.size() != outputSize) {
                error = QStringLiteral(
                    "The filter chain changed dimensions while frame serving.");
                break;
            }
        }
        if (!error.isEmpty()) break;

        // NUT/rawvideo is CFR. Duplicate phases as needed so VFR timestamp
        // gaps retain their displayed duration instead of collapsing time.
        const int sourceFrame = static_cast<int>(
            timeline.mapOutputToSource(frameIndex));
        double duration = config.preserveEmptyFrames
            ? decoder.getFrameDurationSeconds(sourceFrame)
            : 1.0 / sourceFps;
        if (!std::isfinite(duration) || duration <= 0.0)
            duration = 1.0 / sourceFps;
        const int emittedFrames = std::max(
            static_cast<int>(images.size()),
            static_cast<int>(std::llround(duration * outputFps)));
        for (int outputIndex = 0; outputIndex < emittedFrames; ++outputIndex) {
            const int phase = std::min(
                static_cast<int>(images.size()) - 1,
                outputIndex * static_cast<int>(images.size()) / emittedFrames);
            if (!writeImage(ffmpeg, images.at(phase), mCancelRequested)) {
                error = mCancelRequested.load(std::memory_order_relaxed)
                    ? QStringLiteral("Frame serving was stopped.")
                    : processError(ffmpeg);
                break;
            }
        }
    }

    if (ffmpeg.state() != QProcess::NotRunning) {
        if (error.isEmpty()) {
            ffmpeg.closeWriteChannel();
            while (!ffmpeg.waitForFinished(100)) {
                if (mCancelRequested.load(std::memory_order_relaxed)) {
                    error = QStringLiteral("Frame serving was stopped.");
                    ffmpeg.kill();
                    ffmpeg.waitForFinished();
                    break;
                }
            }
            if (error.isEmpty() && ffmpeg.exitCode() != 0)
                error = processError(ffmpeg);
        } else {
            ffmpeg.kill();
            ffmpeg.waitForFinished();
        }
    }
    QFile::remove(config.pipePath);
    Q_EMIT serverFinished(error);
}
