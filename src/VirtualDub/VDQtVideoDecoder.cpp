#include "VDQtVideoDecoder.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QFont>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace {

std::atomic<int> gFrameCacheBudgetMiB{64};
std::atomic<int> gDecoderThreadCount{0};

QString avErrorString(int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

QString avOperationError(const QString& operation, int errorCode) {
    return QStringLiteral("%1: %2 (%3)")
        .arg(operation, avErrorString(errorCode))
        .arg(errorCode);
}

bool isUsableFrameRate(AVRational rate) {
    if (rate.num <= 0 || rate.den <= 0) return false;
    const double value = av_q2d(rate);
    return std::isfinite(value) && value > 0.0 && value < 100000.0;
}

int boundedFrameCount(int64_t count) {
    if (count <= 0) return 0;
    return static_cast<int>(std::min<int64_t>(count, std::numeric_limits<int>::max()));
}

QMutex& aviSynthWorkingDirectoryMutex() {
    static QMutex mutex;
    return mutex;
}

class ScopedCurrentDirectory {
public:
    explicit ScopedCurrentDirectory(const QString& directory)
        : mLocker(&aviSynthWorkingDirectoryMutex()),
          mOriginalDirectory(QDir::currentPath()),
          mChanged(QDir::setCurrent(directory)) {
    }

    ~ScopedCurrentDirectory() {
        if (mChanged && !QDir::setCurrent(mOriginalDirectory)) {
            qWarning() << "[VDQtVideoDecoder] Failed to restore process working directory to"
                       << mOriginalDirectory;
        }
    }

    bool isValid() const { return mChanged; }

private:
    QMutexLocker<QMutex> mLocker;
    QString mOriginalDirectory;
    bool mChanged;
};

bool isScriptDependencyFile(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("avs") || suffix == QStringLiteral("avsi")
        || suffix == QStringLiteral("vpy") || suffix == QStringLiteral("py")
        || suffix == QStringLiteral("ffconcat");
}

QStringList resolveScriptPathLiteral(const QString& literal, const QDir& scriptDirectory) {
    QString path = literal.trimmed();
    if (path.isEmpty() || path.size() > 4096) return {};

    // Handle the path spellings normally used by AviSynth and Python scripts.
    path.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (path.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive))
        path.remove(0, 7);

    QString absolutePath = QFileInfo(path).isAbsolute()
        ? QDir::cleanPath(path)
        : QDir::cleanPath(scriptDirectory.absoluteFilePath(path));

    // ImageSource and similar filters commonly use printf/hash patterns. Expand
    // them conservatively so every existing source frame is protected.
    QString wildcardPath = absolutePath;
    wildcardPath.replace(
        QRegularExpression(QStringLiteral("%[-+0 #]*\\d*(?:\\.\\d+)?[diu]")),
        QStringLiteral("*"));
    wildcardPath.replace(QRegularExpression(QStringLiteral("#+")), QStringLiteral("*"));

    const bool hasWildcard = wildcardPath.contains(QLatin1Char('*'))
                          || wildcardPath.contains(QLatin1Char('?'))
                          || wildcardPath.contains(QLatin1Char('['));
    if (!hasWildcard) {
        const QFileInfo info(absolutePath);
        if (!info.exists() && !info.isSymLink()) return {};
        if (info.isDir()) return {};
        return { info.absoluteFilePath() };
    }

    const int separator = wildcardPath.lastIndexOf(QLatin1Char('/'));
    const QString directoryPath = separator >= 0
        ? (separator == 0 ? QStringLiteral("/") : wildcardPath.left(separator))
        : scriptDirectory.absolutePath();
    const QString namePattern = separator >= 0
        ? wildcardPath.mid(separator + 1)
        : wildcardPath;
    if (namePattern.isEmpty()) return {};

    QStringList matches;
    const QFileInfoList entries = QDir(directoryPath).entryInfoList(
        { namePattern }, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    matches.reserve(entries.size());
    for (const QFileInfo& entry : entries)
        matches.append(entry.absoluteFilePath());
    return matches;
}

bool looksLikePathLiteral(const QString& literal) {
    const QString value = literal.trimmed();
    if (value.contains(QLatin1Char('/')) || value.contains(QLatin1Char('\\'))
        || value.contains(QLatin1Char('*')) || value.contains(QLatin1Char('#'))
        || value.contains(QRegularExpression(QStringLiteral("%[-+0 #]*\\d*(?:\\.\\d+)?[diu]")))) {
        return true;
    }
    static const QRegularExpression extensionRegex(QStringLiteral(
        R"(\.(?:avs|avsi|vpy|py|avi|mp4|m4v|mkv|mov|webm|nut|ts|m2ts|mpg|mpeg|vob|wav|flac|mp3|aac|m4a|ogg|opus|png|jpe?g|bmp|tiff?|webp)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return extensionRegex.match(value).hasMatch();
}

void collectConcatDependencies(
    const QString& manifestPath,
    VDQtVideoDecoder::ScriptDependencyReport& report) {
    const QFileInfo manifestInfo(manifestPath);
    QFile manifest(manifestInfo.absoluteFilePath());
    if (!manifest.open(QIODevice::ReadOnly | QIODevice::Text)) {
        report.complete = false;
        report.unresolvedPathLiterals.append(manifestInfo.absoluteFilePath());
        report.diagnostics.append(QStringLiteral("Cannot read concat manifest: %1")
                                      .arg(manifestInfo.absoluteFilePath()));
        return;
    }
    const QString content = QString::fromUtf8(manifest.readAll());
    static const QRegularExpression fileLine(
        QStringLiteral(R"(^\s*file\s+(.+?)\s*$)"),
        QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator lines = fileLine.globalMatch(content);
    int fileCount = 0;
    while (lines.hasNext()) {
        ++fileCount;
        const QString token = lines.next().captured(1);
        QString decoded;
        decoded.reserve(token.size());
        bool escaped = false;
        QChar quote;
        for (const QChar character : token) {
            if (escaped) {
                decoded += character;
                escaped = false;
            } else if (character == QLatin1Char('\\')) {
                escaped = true;
            } else if (quote.isNull()
                       && (character == QLatin1Char('\'')
                           || character == QLatin1Char('"'))) {
                quote = character;
            } else if (!quote.isNull() && character == quote) {
                quote = QChar();
            } else {
                decoded += character;
            }
        }
        if (escaped || !quote.isNull()) {
            report.complete = false;
            report.diagnostics.append(QStringLiteral("Malformed file path in concat manifest."));
            continue;
        }
        const QString absolutePath = QFileInfo(decoded).isAbsolute()
            ? QDir::cleanPath(decoded)
            : QDir::cleanPath(manifestInfo.dir().absoluteFilePath(decoded));
        const QFileInfo dependency(absolutePath);
        if (!dependency.exists() && !dependency.isSymLink()) {
            report.complete = false;
            report.unresolvedPathLiterals.append(absolutePath);
        } else if (!dependency.isDir()) {
            report.resolvedPaths.append(dependency.absoluteFilePath());
        }
    }
    if (fileCount == 0) {
        report.complete = false;
        report.diagnostics.append(QStringLiteral("The concat manifest contains no file entries."));
    }
}

void collectScriptDependencies(const QString& scriptPath,
                               QSet<QString>& visitedScripts,
                               VDQtVideoDecoder::ScriptDependencyReport& report,
                               int depth) {
    if (depth > 32) {
        report.complete = false;
        report.diagnostics.append(QStringLiteral("Script import depth exceeds 32 levels."));
        return;
    }
    const QFileInfo scriptInfo(scriptPath);
    const QString absoluteScriptPath = scriptInfo.absoluteFilePath();
    const QString visitKey = scriptInfo.canonicalFilePath().isEmpty()
        ? absoluteScriptPath
        : scriptInfo.canonicalFilePath();
    if (visitedScripts.contains(visitKey)) return;
    visitedScripts.insert(visitKey);

    QFile scriptFile(absoluteScriptPath);
    if (!scriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        report.complete = false;
        report.unresolvedPathLiterals.append(absoluteScriptPath);
        report.diagnostics.append(QStringLiteral("Cannot read script dependency: %1")
                                      .arg(absoluteScriptPath));
        return;
    }
    const QString content = QString::fromUtf8(scriptFile.readAll());
    scriptFile.close();

    // Extract every existing path literal, rather than trying to maintain a
    // fragile whitelist of source-filter names. This covers variables, named
    // arguments, plugin source filters, Import(), and ordinary VapourSynth
    // syntax. Script dependencies are followed recursively.
    static const QRegularExpression literalRegex(QStringLiteral(
        R"vdq("((?:\\.|[^"\\\r\n])*)"|'((?:\\.|[^'\\\r\n])*)')vdq"));
    QRegularExpressionMatchIterator matches = literalRegex.globalMatch(content);
    const QDir scriptDirectory = scriptInfo.dir();
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString literal = match.captured(1).isNull()
            ? match.captured(2)
            : match.captured(1);
        const QStringList resolved = resolveScriptPathLiteral(literal, scriptDirectory);
        if (resolved.isEmpty() && looksLikePathLiteral(literal)) {
            report.complete = false;
            const QString unresolved = QFileInfo(literal).isAbsolute()
                ? QDir::cleanPath(literal)
                : QDir::cleanPath(scriptDirectory.absoluteFilePath(literal));
            if (!report.unresolvedPathLiterals.contains(unresolved))
                report.unresolvedPathLiterals.append(unresolved);
        }
        for (const QString& dependency : resolved) {
            if (!report.resolvedPaths.contains(dependency))
                report.resolvedPaths.append(dependency);
            if (isScriptDependencyFile(dependency))
                collectScriptDependencies(dependency, visitedScripts, report, depth + 1);
        }
    }

    // A dependency audit can only be called complete for the deliberately
    // narrow literal-source subset. Anything capable of constructing a path
    // at runtime keeps the conservative existing-destination guard enabled.
    static const QRegularExpression dynamicPathRegex(QStringLiteral(
        R"((?:\+\s*[A-Za-z_]|[A-Za-z_]\s*\+|\b(?:eval|exec|getenv|environ|glob|format)\b|\$\{|\{[^}\r\n]*\}))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression sourceCallRegex(QStringLiteral(
        R"(\b(?:AVISource|OpenDMLSource|DirectShowSource|FFVideoSource|FFAudioSource|LWLibavVideoSource|LWLibavAudioSource|ImageSource|ImageReader|Import|Source)\s*\(\s*(?:[A-Za-z_]\w*\s*=\s*)?([^,\)\r\n]+))"),
        QRegularExpression::CaseInsensitiveOption);
    if (dynamicPathRegex.match(content).hasMatch()) {
        report.complete = false;
        report.diagnostics.append(QStringLiteral("The script contains runtime path construction."));
    }
    QRegularExpressionMatchIterator sourceCalls = sourceCallRegex.globalMatch(content);
    while (sourceCalls.hasNext()) {
        const QString argument = sourceCalls.next().captured(1).trimmed();
        if (!argument.startsWith(QLatin1Char('"')) && !argument.startsWith(QLatin1Char('\''))) {
            report.complete = false;
            report.diagnostics.append(
                QStringLiteral("A source path is supplied through a variable or expression: %1")
                    .arg(argument.left(160)));
        }
    }
    static const QSet<QString> auditedCalls = {
        QStringLiteral("avisource"), QStringLiteral("opendmlsource"),
        QStringLiteral("directshowsource"), QStringLiteral("ffvideosource"),
        QStringLiteral("ffaudiosource"), QStringLiteral("lwlibavvideosource"),
        QStringLiteral("lwlibavaudiosource"), QStringLiteral("imagesource"),
        QStringLiteral("imagereader"), QStringLiteral("import"),
        QStringLiteral("source")
    };
    static const QRegularExpression functionCallRegex(QStringLiteral(
        R"(\b([A-Za-z_]\w*)\s*\()"));
    QRegularExpressionMatchIterator functionCalls = functionCallRegex.globalMatch(content);
    while (functionCalls.hasNext()) {
        const QString functionName = functionCalls.next().captured(1).toLower();
        if (!auditedCalls.contains(functionName)) {
            report.complete = false;
            report.diagnostics.append(
                QStringLiteral("Dependency behavior of script function '%1' cannot be proven.")
                    .arg(functionName));
        }
    }
}

} // namespace

qsizetype VDQtVideoDecoder::getFrameCacheBudgetKiB() {
    return static_cast<qsizetype>(
        gFrameCacheBudgetMiB.load(std::memory_order_relaxed)) * 1024;
}

void VDQtVideoDecoder::setFrameCacheBudgetMiB(int budgetMiB) {
    gFrameCacheBudgetMiB.store(
        std::clamp(budgetMiB, 16, 1024), std::memory_order_relaxed);
}

int VDQtVideoDecoder::getDecoderThreadCount() {
    return gDecoderThreadCount.load(std::memory_order_relaxed);
}

void VDQtVideoDecoder::setDecoderThreadCount(int threadCount) {
    gDecoderThreadCount.store(
        std::clamp(threadCount, 0, 64), std::memory_order_relaxed);
}

void VDQtVideoDecoder::applyFrameCacheBudget() {
    mFrameCache.setMaxCost(getFrameCacheBudgetKiB());
}

VDQtVideoDecoder::VDQtVideoDecoder()
    : mIsOpen(false),
      mWidth(0),
      mHeight(0),
      mFrameCount(0),
      mFrameCountStatus(FrameCountStatus::Unknown),
      mFps(0.0),
      mVideoStreamIndex(-1),
      mDuration(AV_NOPTS_VALUE),
      mFormatCtx(nullptr),
      mCodecCtx(nullptr),
      mSwsCtx(nullptr),
      mFrame(nullptr),
      mFrameRGB(nullptr),
      mPacket(nullptr),
      mBuffer(nullptr),
      mCurrentFrameIndex(-1),
      mNextDecodeFrameIndex(0),
      mStreamStartTimestamp(AV_NOPTS_VALUE),
      mPendingSeekTargetTimestamp(AV_NOPTS_VALUE),
      mPacketPending(false),
      mDemuxEof(false),
      mDrainSent(false),
      mLastDecodeReachedEof(false),
      mDiscardUntilKeyFrame(false),
      mSeekCount(0),
      mDecodedFrameCount(0),
      mSwsSourceFormat(AV_PIX_FMT_NONE),
      mSwsDestinationFormat(AV_PIX_FMT_NONE),
      mOutputPixelFormat(AV_PIX_FMT_RGB24),
      mOutputImageFormat(QImage::Format_RGB888),
      mSwsSourceWidth(0),
      mSwsSourceHeight(0),
      mSourceBitDepth(8),
      mSourceHasAlpha(false),
      mErrorMode(0) {
    mFrameCache.setMaxCost(getFrameCacheBudgetKiB());
}

VDQtVideoDecoder::~VDQtVideoDecoder() {
    close();
}

QString VDQtVideoDecoder::parseScriptSource(const QString& scriptPath) {
    const QStringList sources = parseScriptSources(scriptPath);
    return sources.isEmpty() ? QString() : sources.first();
}

QStringList VDQtVideoDecoder::parseScriptSources(const QString& scriptPath) {
    return auditScriptDependencies(scriptPath).resolvedPaths;
}

VDQtVideoDecoder::ScriptDependencyReport
VDQtVideoDecoder::auditScriptDependencies(const QString& scriptPath) {
    ScriptDependencyReport report;
    report.complete = true;
    const QFileInfo info(scriptPath);
    if (!isScriptDependencyFile(scriptPath) || (!info.exists() && !info.isSymLink())) {
        report.complete = false;
        report.unresolvedPathLiterals.append(info.absoluteFilePath());
        report.diagnostics.append(QStringLiteral("The script file does not exist or has an unsupported extension."));
        return report;
    }
    if (info.suffix().compare(QStringLiteral("ffconcat"), Qt::CaseInsensitive) == 0) {
        collectConcatDependencies(scriptPath, report);
    } else {
        QSet<QString> visitedScripts;
        collectScriptDependencies(scriptPath, visitedScripts, report, 0);
    }
    report.resolvedPaths.removeDuplicates();
    report.unresolvedPathLiterals.removeDuplicates();
    report.diagnostics.removeDuplicates();
    return report;
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
    if (!frame) {
        mLastError = QStringLiteral("AviSynth failed to return frame %1.").arg(frameIndex);
        return QImage();
    }

    int w = mAvsVi->width;
    int h = mAvsVi->height;

    QImage img(w, h, mOutputImageFormat);
    if (img.isNull()) {
        mLastError = QStringLiteral("Could not allocate a %1x%2 image for AviSynth frame %3.")
                         .arg(w)
                         .arg(h)
                         .arg(frameIndex);
        avs_release_video_frame(frame);
        return QImage();
    }

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
    } else if (avs_is_rgb64(mAvsVi) || avs_is_rgb48(mAvsVi)
               || avs_is_rgb32(mAvsVi) || avs_is_rgb24(mAvsVi)) {
        if (avs_is_rgb64(mAvsVi)) srcFmt = AV_PIX_FMT_BGRA64LE;
        else if (avs_is_rgb48(mAvsVi)) srcFmt = AV_PIX_FMT_BGR48LE;
        else if (avs_is_rgb32(mAvsVi)) srcFmt = AV_PIX_FMT_BGRA;
        else srcFmt = AV_PIX_FMT_BGR24;
        const uint8_t *base = avs_get_read_ptr_p(frame, 0);
        const int pitch = avs_get_pitch_p(frame, 0);
        // AviSynth's packed RGB24/RGB32 convention is bottom-up even though
        // planar YUV and YUY2 are top-down. Present the visual top scanline to
        // swscale and walk toward the visual bottom with a negative stride.
        if (base && pitch > 0) {
            srcSlice[0] = base + static_cast<ptrdiff_t>(h - 1) * pitch;
            srcStride[0] = -pitch;
        }
    }

    if (srcFmt != AV_PIX_FMT_NONE && srcSlice[0]) {
        const bool contextMatches = mSwsCtx
            && mSwsSourceFormat == srcFmt
            && mSwsDestinationFormat == mOutputPixelFormat
            && mSwsSourceWidth == w
            && mSwsSourceHeight == h;
        if ((contextMatches || setupSwsContext(srcFmt, w, h, mOutputPixelFormat)) && mSwsCtx) {
            uint8_t *dstSlice[4] = { img.bits(), nullptr, nullptr, nullptr };
            int dstStride[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
            const int scaledRows = sws_scale(mSwsCtx, srcSlice, srcStride, 0, h, dstSlice, dstStride);
            avs_release_video_frame(frame);
            if (scaledRows == h) return img;

            mLastError = QStringLiteral("Pixel conversion failed for AviSynth frame %1.").arg(frameIndex);
            return QImage();
        }
    }

    if (mLastError.isEmpty()) {
        mLastError = QStringLiteral("Unsupported AviSynth pixel format for frame %1.").arg(frameIndex);
    }
    avs_release_video_frame(frame);
    return QImage();
}

bool VDQtVideoDecoder::setupSwsContext(AVPixelFormat sourceFormat,
                                       int sourceWidth,
                                       int sourceHeight,
                                       AVPixelFormat destinationFormat) {
    if (sourceFormat == AV_PIX_FMT_NONE && mCodecCtx) sourceFormat = mCodecCtx->pix_fmt;
    if (sourceWidth <= 0) sourceWidth = mWidth;
    if (sourceHeight <= 0) sourceHeight = mHeight;

    if (sourceFormat == AV_PIX_FMT_NONE || destinationFormat == AV_PIX_FMT_NONE
        || sourceWidth <= 0 || sourceHeight <= 0) {
        mLastError = QStringLiteral("Cannot initialize pixel conversion without a valid source format and dimensions.");
        return false;
    }

    SwsContext *newContext = sws_getContext(
        sourceWidth, sourceHeight, sourceFormat,
        sourceWidth, sourceHeight, destinationFormat,
        destinationFormat == AV_PIX_FMT_BGRA ? SWS_POINT : SWS_FAST_BILINEAR,
        nullptr, nullptr, nullptr
    );

    if (!newContext) {
        const char *formatName = av_get_pix_fmt_name(sourceFormat);
        mLastError = QStringLiteral("Could not create pixel conversion context for %1 at %2x%3.")
                         .arg(formatName ? QString::fromUtf8(formatName) : QStringLiteral("unknown format"))
                         .arg(sourceWidth)
                         .arg(sourceHeight);
        return false;
    }

    int srcRange = 0;
    if (mComponentRangeMode == 1) {
        srcRange = 0; // Limited (16-235)
    } else if (mComponentRangeMode == 2) {
        srcRange = 1; // Full (0-255)
    } else {
        if (mCodecCtx && mCodecCtx->color_range == AVCOL_RANGE_JPEG) srcRange = 1;
        else srcRange = 0;
    }

    const int *invTable = nullptr;
    if (mColorSpaceMode == 1) {
        invTable = sws_getCoefficients(SWS_CS_ITU601);
    } else if (mColorSpaceMode == 2) {
        invTable = sws_getCoefficients(SWS_CS_ITU709);
    } else {
        if (sourceWidth >= 1280 || sourceHeight >= 720) {
            invTable = sws_getCoefficients(SWS_CS_ITU709);
        } else {
            invTable = sws_getCoefficients(SWS_CS_ITU601);
        }
    }

    const int *table = sws_getCoefficients(SWS_CS_DEFAULT);
    const int colorResult = sws_setColorspaceDetails(
        newContext,
        invTable, srcRange,
        table, 1, // destination full range RGB
        0, 1 << 16, 1 << 16
    );

    if (colorResult < 0) {
        mLastError = avOperationError(QStringLiteral("Could not configure pixel conversion colorspace"), colorResult);
        sws_freeContext(newContext);
        return false;
    }

    if (mSwsCtx) sws_freeContext(mSwsCtx);
    mSwsCtx = newContext;
    mSwsSourceFormat = sourceFormat;
    mSwsDestinationFormat = destinationFormat;
    mSwsSourceWidth = sourceWidth;
    mSwsSourceHeight = sourceHeight;
    return true;
}

void VDQtVideoDecoder::setDecompressionConfig(const QString &formatName, int colorSpace, int componentRange) {
    mForcedFormatName = formatName;
    mColorSpaceMode = colorSpace;
    mComponentRangeMode = componentRange;

    clearCache();
    if (mIsAvsNative) {
        if (mSwsCtx) sws_freeContext(mSwsCtx);
        mSwsCtx = nullptr;
        mSwsSourceFormat = AV_PIX_FMT_NONE;
        mSwsDestinationFormat = AV_PIX_FMT_NONE;
        mSwsSourceWidth = 0;
        mSwsSourceHeight = 0;
    } else if (mCodecCtx && mCodecCtx->pix_fmt != AV_PIX_FMT_NONE) {
        setupSwsContext(mCodecCtx->pix_fmt, mWidth, mHeight, mOutputPixelFormat);
    }
}

bool VDQtVideoDecoder::openFile(const QString& filePath) {
    close();
    mLastError.clear();

    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    const bool isAvs = absolutePath.endsWith(QStringLiteral(".avs"), Qt::CaseInsensitive);

    if (isAvs) {
        qDebug() << "[VDQtVideoDecoder] Attempting native AviSynth+ script evaluation:" << absolutePath;

        AVS_ScriptEnvironment *newEnvironment = avs_create_script_environment(AVISYNTH_INTERFACE_VERSION);
        AVS_Clip *newClip = nullptr;
        const AVS_VideoInfo *newVideoInfo = nullptr;

        if (!newEnvironment) {
            mLastError = QStringLiteral("Failed to create the AviSynth+ script environment.");
            return false;
        }

        {
            // AviSynth resolves some source-filter paths against the process CWD. Serialize
            // this narrow compatibility window and restore the original directory on every
            // return path.
            ScopedCurrentDirectory workingDirectory(QFileInfo(absolutePath).absolutePath());
            if (!workingDirectory.isValid()) {
                mLastError = QStringLiteral("Could not enter the AviSynth script directory: %1")
                                 .arg(QFileInfo(absolutePath).absolutePath());
            } else {
                const QByteArray pathBytes = absolutePath.toUtf8();
                const AVS_Value argument = avs_new_value_string(pathBytes.constData());
                AVS_Value result = avs_invoke(newEnvironment, "Import", argument, nullptr);

                if (avs_is_error(result)) {
                    mLastError = QString::fromUtf8(avs_as_string(result));
                } else if (!avs_is_clip(result)) {
                    mLastError = QStringLiteral("AviSynth Import did not return a video clip.");
                } else {
                    newClip = avs_take_clip(result, newEnvironment);
                    if (newClip) newVideoInfo = avs_get_video_info(newClip);
                }
                avs_release_value(result);
            }
        }

        if (!newClip || !newVideoInfo || newVideoInfo->width <= 0 || newVideoInfo->height <= 0
            || newVideoInfo->num_frames <= 0
            || av_image_check_size(static_cast<unsigned>(newVideoInfo->width),
                                   static_cast<unsigned>(newVideoInfo->height), 0, nullptr) < 0) {
            if (mLastError.isEmpty()) {
                const char *environmentError = avs_get_error(newEnvironment);
                mLastError = environmentError
                    ? QString::fromUtf8(environmentError)
                    : QStringLiteral("AviSynth returned invalid video dimensions or no frames.");
            }
            if (newClip) avs_release_clip(newClip);
            avs_delete_script_environment(newEnvironment);
            qWarning() << "[VDQtVideoDecoder] AviSynth+ script evaluation failed:" << mLastError;
            return false;
        }

        mAvsEnv = newEnvironment;
        mAvsClip = newClip;
        mAvsVi = newVideoInfo;
        mFilePath = absolutePath;
        mWidth = newVideoInfo->width;
        mHeight = newVideoInfo->height;
        mFrameCount = boundedFrameCount(newVideoInfo->num_frames);
        mFrameCountStatus = FrameCountStatus::Exact;
        mFps = newVideoInfo->fps_denominator > 0
            ? static_cast<double>(newVideoInfo->fps_numerator) / newVideoInfo->fps_denominator
            : 0.0;
        mIsOpen = true;
        mIsAvsNative = true;
        mSourceBitDepth = std::max(8, avs_bits_per_component(newVideoInfo));
        mSourceHasAlpha = avs_is_yuva(newVideoInfo) || avs_is_planar_rgba(newVideoInfo)
                       || avs_is_rgb32(newVideoInfo) || avs_is_rgb64(newVideoInfo);
        if (mSourceBitDepth > 8) {
            mOutputPixelFormat = AV_PIX_FMT_RGBA64LE;
            mOutputImageFormat = QImage::Format_RGBA64;
        } else if (mSourceHasAlpha) {
            mOutputPixelFormat = AV_PIX_FMT_RGBA;
            mOutputImageFormat = QImage::Format_RGBA8888;
        } else {
            mOutputPixelFormat = AV_PIX_FMT_RGB24;
            mOutputImageFormat = QImage::Format_RGB888;
        }
        mCurrentFrameIndex = -1;
        mNextDecodeFrameIndex = 0;
        mFrameCache.setMaxCost(getFrameCacheBudgetKiB());

        qDebug() << "[VDQtVideoDecoder] Native AviSynth+ script evaluation succeeded:"
                 << absolutePath << mWidth << "x" << mHeight << "@" << mFps << "fps,"
                 << mFrameCount << "frames.";
        return true;
    }

    AVFormatContext *newFormatContext = nullptr;
    AVCodecContext *newCodecContext = nullptr;
    AVFrame *newFrame = nullptr;
    AVFrame *newRgbFrame = nullptr;
    AVPacket *newPacket = nullptr;
    uint8_t *newBuffer = nullptr;

    auto releaseLocals = [&]() {
        if (newBuffer) av_freep(&newBuffer);
        if (newRgbFrame) av_frame_free(&newRgbFrame);
        if (newFrame) av_frame_free(&newFrame);
        if (newPacket) av_packet_free(&newPacket);
        if (newCodecContext) avcodec_free_context(&newCodecContext);
        if (newFormatContext) avformat_close_input(&newFormatContext);
    };

    const QByteArray pathBytes = absolutePath.toUtf8();
    const bool isConcatManifest = absolutePath.endsWith(
        QStringLiteral(".ffconcat"), Qt::CaseInsensitive);
    const bool isVapourSynthScript = absolutePath.endsWith(
        QStringLiteral(".vpy"), Qt::CaseInsensitive);
    const AVInputFormat *inputFormat = isConcatManifest
        ? av_find_input_format("concat")
        : (isVapourSynthScript ? av_find_input_format("vapoursynth") : nullptr);
    if (isVapourSynthScript && !inputFormat) {
        mLastError = QStringLiteral(
            "This FFmpeg build does not provide the VapourSynth input module. "
            "Install an FFmpeg build with VapourSynth support to open .vpy scripts.");
        releaseLocals();
        return false;
    }
    AVDictionary *inputOptions = nullptr;
    if (isConcatManifest) av_dict_set(&inputOptions, "safe", "0", 0);
    int error = avformat_open_input(
        &newFormatContext, pathBytes.constData(), inputFormat, &inputOptions);
    av_dict_free(&inputOptions);
    if (error < 0) {
        mLastError = avOperationError(QStringLiteral("Could not open %1").arg(absolutePath), error);
        releaseLocals();
        return false;
    }

    error = avformat_find_stream_info(newFormatContext, nullptr);
    if (error < 0) {
        mLastError = avOperationError(QStringLiteral("Could not read stream information"), error);
        releaseLocals();
        return false;
    }

    const AVCodec *codec = nullptr;
    const int streamIndex = av_find_best_stream(
        newFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (streamIndex < 0) {
        mLastError = avOperationError(QStringLiteral("No decodable video stream was found"), streamIndex);
        releaseLocals();
        return false;
    }

    AVStream *videoStream = newFormatContext->streams[streamIndex];
    if (!codec) codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        mLastError = QStringLiteral("No decoder is available for codec %1.")
                         .arg(QString::fromUtf8(avcodec_get_name(videoStream->codecpar->codec_id)));
        releaseLocals();
        return false;
    }

    newCodecContext = avcodec_alloc_context3(codec);
    if (!newCodecContext) {
        mLastError = QStringLiteral("Could not allocate the video decoder context.");
        releaseLocals();
        return false;
    }

    error = avcodec_parameters_to_context(newCodecContext, videoStream->codecpar);
    if (error < 0) {
        mLastError = avOperationError(QStringLiteral("Could not copy video stream parameters"), error);
        releaseLocals();
        return false;
    }
    newCodecContext->pkt_timebase = videoStream->time_base;
    newCodecContext->thread_count = getDecoderThreadCount();
    newCodecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    // Apply the selected policy before opening the codec. setErrorMode() also
    // updates an already-open context for subsequent frames.
    switch (mErrorMode) {
    case 1:
        newCodecContext->err_recognition = 0;
        newCodecContext->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
        newCodecContext->flags &= ~AV_CODEC_FLAG_OUTPUT_CORRUPT;
        break;
    case 2:
        newCodecContext->err_recognition = AV_EF_IGNORE_ERR;
        newCodecContext->error_concealment = 0;
        newCodecContext->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
        break;
    default:
        newCodecContext->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_BUFFER;
        newCodecContext->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
        newCodecContext->flags &= ~AV_CODEC_FLAG_OUTPUT_CORRUPT;
        break;
    }

    error = avcodec_open2(newCodecContext, codec, nullptr);
    if (error < 0) {
        mLastError = avOperationError(
            QStringLiteral("Could not open video decoder %1").arg(QString::fromUtf8(codec->name)), error);
        releaseLocals();
        return false;
    }

    const int width = newCodecContext->width;
    const int height = newCodecContext->height;
    error = av_image_check_size(static_cast<unsigned>(std::max(width, 0)),
                                static_cast<unsigned>(std::max(height, 0)), 0, nullptr);
    if (width <= 0 || height <= 0 || error < 0) {
        mLastError = error < 0
            ? avOperationError(QStringLiteral("Invalid video dimensions %1x%2").arg(width).arg(height), error)
            : QStringLiteral("Invalid video dimensions %1x%2.").arg(width).arg(height);
        releaseLocals();
        return false;
    }

    newFrame = av_frame_alloc();
    newRgbFrame = av_frame_alloc();
    newPacket = av_packet_alloc();
    if (!newFrame || !newRgbFrame || !newPacket) {
        mLastError = QStringLiteral("Could not allocate FFmpeg frame or packet storage.");
        releaseLocals();
        return false;
    }

    int sourceBitDepth = 8;
    bool sourceHasAlpha = false;
    if (const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(newCodecContext->pix_fmt)) {
        for (int component = 0; component < descriptor->nb_components; ++component) {
            sourceBitDepth = std::max(
                sourceBitDepth, static_cast<int>(descriptor->comp[component].depth));
        }
        sourceHasAlpha = (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
    }
    const AVPixelFormat outputPixelFormat = sourceBitDepth > 8
        ? AV_PIX_FMT_RGBA64LE
        : sourceHasAlpha ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24;
    const QImage::Format outputImageFormat = sourceBitDepth > 8
        ? QImage::Format_RGBA64
        : sourceHasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;

    const int bufferSize = av_image_get_buffer_size(outputPixelFormat, width, height, 1);
    if (bufferSize <= 0) {
        mLastError = avOperationError(QStringLiteral("Could not calculate RGB frame buffer size"), bufferSize);
        releaseLocals();
        return false;
    }

    newBuffer = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(bufferSize)));
    if (!newBuffer) {
        mLastError = QStringLiteral("Could not allocate %1 bytes for RGB frame conversion.").arg(bufferSize);
        releaseLocals();
        return false;
    }

    error = av_image_fill_arrays(newRgbFrame->data, newRgbFrame->linesize, newBuffer,
                                 outputPixelFormat, width, height, 1);
    if (error < 0) {
        mLastError = avOperationError(QStringLiteral("Could not initialize RGB frame storage"), error);
        releaseLocals();
        return false;
    }

    AVRational frameRate = av_guess_frame_rate(newFormatContext, videoStream, nullptr);
    if (!isUsableFrameRate(frameRate)) frameRate = videoStream->avg_frame_rate;
    if (!isUsableFrameRate(frameRate)) frameRate = videoStream->r_frame_rate;
    if (!isUsableFrameRate(frameRate)) frameRate = newCodecContext->framerate;

    int frameCount = 0;
    FrameCountStatus frameCountStatus = FrameCountStatus::Unknown;
    if (videoStream->nb_frames > 0) {
        frameCount = boundedFrameCount(videoStream->nb_frames);
        // Demuxer metadata is frequently estimated or stale (notably in AVI
        // and remuxed VFR files). Treat it as a navigation hint until decoder
        // EOF validates the presentation-order count; otherwise an
        // underreported value hard-clamps valid tail frames.
        frameCountStatus = frameCount > 0
            ? FrameCountStatus::Estimated
            : FrameCountStatus::Unknown;
    } else if (isUsableFrameRate(frameRate) && videoStream->duration != AV_NOPTS_VALUE
               && videoStream->duration > 0) {
        const int64_t estimate = av_rescale_q_rnd(
            videoStream->duration, videoStream->time_base, av_inv_q(frameRate),
            static_cast<AVRounding>(AV_ROUND_UP | AV_ROUND_PASS_MINMAX));
        frameCount = boundedFrameCount(estimate);
        frameCountStatus = frameCount > 0 ? FrameCountStatus::Estimated : FrameCountStatus::Unknown;
    } else if (isUsableFrameRate(frameRate) && newFormatContext->duration != AV_NOPTS_VALUE
               && newFormatContext->duration > 0) {
        const int64_t estimate = av_rescale_q_rnd(
            newFormatContext->duration, AV_TIME_BASE_Q, av_inv_q(frameRate),
            static_cast<AVRounding>(AV_ROUND_UP | AV_ROUND_PASS_MINMAX));
        frameCount = boundedFrameCount(estimate);
        frameCountStatus = frameCount > 0 ? FrameCountStatus::Estimated : FrameCountStatus::Unknown;
    }

    int64_t streamStartTimestamp = videoStream->start_time;
    if (streamStartTimestamp == AV_NOPTS_VALUE && newFormatContext->start_time != AV_NOPTS_VALUE) {
        streamStartTimestamp = av_rescale_q(
            newFormatContext->start_time, AV_TIME_BASE_Q, videoStream->time_base);
    }
    if (streamStartTimestamp == AV_NOPTS_VALUE) streamStartTimestamp = 0;

    // Commit only after every allocation and initialization above has succeeded.
    mFormatCtx = newFormatContext;
    mCodecCtx = newCodecContext;
    mFrame = newFrame;
    mFrameRGB = newRgbFrame;
    mPacket = newPacket;
    mBuffer = newBuffer;
    newFormatContext = nullptr;
    newCodecContext = nullptr;
    newFrame = nullptr;
    newRgbFrame = nullptr;
    newPacket = nullptr;
    newBuffer = nullptr;

    mFilePath = absolutePath;
    mVideoStreamIndex = streamIndex;
    mWidth = width;
    mHeight = height;
    mFps = isUsableFrameRate(frameRate) ? av_q2d(frameRate) : 0.0;
    mFrameCount = frameCount;
    mFrameCountStatus = frameCountStatus;
    mDuration = videoStream->duration;
    if (mDuration == AV_NOPTS_VALUE && mFormatCtx->duration != AV_NOPTS_VALUE) {
        mDuration = av_rescale_q(mFormatCtx->duration, AV_TIME_BASE_Q, videoStream->time_base);
    }
    mStreamStartTimestamp = streamStartTimestamp;
    mSourceBitDepth = sourceBitDepth;
    mSourceHasAlpha = sourceHasAlpha;
    mOutputPixelFormat = outputPixelFormat;
    mOutputImageFormat = outputImageFormat;
    mPendingSeekTargetTimestamp = AV_NOPTS_VALUE;
    mCurrentFrameIndex = -1;
    mNextDecodeFrameIndex = 0;
    mPacketPending = false;
    mDemuxEof = false;
    mDrainSent = false;
    mLastDecodeReachedEof = false;
    mDiscardUntilKeyFrame = false;
    mSeekCount = 0;
    mDecodedFrameCount = 0;
    mFrameCache.setMaxCost(getFrameCacheBudgetKiB());

    if (mCodecCtx->pix_fmt != AV_PIX_FMT_NONE
        && !setupSwsContext(mCodecCtx->pix_fmt, mWidth, mHeight, mOutputPixelFormat)) {
        const QString conversionError = mLastError;
        close();
        mLastError = conversionError;
        return false;
    }

    mIsOpen = true;
    qDebug() << "[VDQtVideoDecoder] Opened file:" << absolutePath
             << mWidth << "x" << mHeight << "@" << mFps << "fps,"
             << mFrameCount
             << (mFrameCountStatus == FrameCountStatus::Exact ? "exact frames."
                 : mFrameCountStatus == FrameCountStatus::Estimated ? "estimated frames."
                 : "frames unknown.");
    return true;
}

void VDQtVideoDecoder::clearCache() {
    mFrameCache.clear();
}

void VDQtVideoDecoder::cacheFrame(int frameIndex, const QImage& image) {
    if (frameIndex < 0 || image.isNull()) return;

    const qsizetype byteCount = image.sizeInBytes();
    // Divide before adding so even a theoretical qsizetype-sized image cannot
    // overflow while rounding up. A non-null QImage should never report zero,
    // but charge the minimum unit if a backend does.
    const qsizetype costKiB = byteCount > 0
        ? 1 + ((byteCount - 1) / 1024)
        : 1;
    if (costKiB <= 0 || costKiB > mFrameCache.maxCost()) {
        // QCache would delete an oversized inserted object. Skip allocation and
        // remove an older entry for this key explicitly instead.
        mFrameCache.remove(frameIndex);
        return;
    }

    mFrameCache.insert(frameIndex, new QImage(image), costKiB);
}

void VDQtVideoDecoder::close() {
    clearCache();
    mFrameIndex.clear();

    if (mAvsClip) {
        avs_release_clip(mAvsClip);
        mAvsClip = nullptr;
        mAvsVi = nullptr;
    }

    if (mAvsEnv) {
        avs_delete_script_environment(mAvsEnv);
        mAvsEnv = nullptr;
    }

    if (mSwsCtx) {
        sws_freeContext(mSwsCtx);
        mSwsCtx = nullptr;
    }
    mSwsSourceFormat = AV_PIX_FMT_NONE;
    mSwsDestinationFormat = AV_PIX_FMT_NONE;
    mSwsSourceWidth = 0;
    mSwsSourceHeight = 0;

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

    if (mPacket) {
        av_packet_free(&mPacket);
        mPacket = nullptr;
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
    mFilePath.clear();
    mIsSyntheticScript = false;
    mIsAvsNative = false;
    mWidth = 0;
    mHeight = 0;
    mFrameCount = 0;
    mFrameCountStatus = FrameCountStatus::Unknown;
    mFps = 0.0;
    mVideoStreamIndex = -1;
    mDuration = AV_NOPTS_VALUE;
    mSourceBitDepth = 8;
    mSourceHasAlpha = false;
    mOutputPixelFormat = AV_PIX_FMT_RGB24;
    mOutputImageFormat = QImage::Format_RGB888;
    mCurrentFrameIndex = -1;
    mNextDecodeFrameIndex = 0;
    mStreamStartTimestamp = AV_NOPTS_VALUE;
    mPendingSeekTargetTimestamp = AV_NOPTS_VALUE;
    mPacketPending = false;
    mDemuxEof = false;
    mDrainSent = false;
    mLastDecodeReachedEof = false;
    mDiscardUntilKeyFrame = false;
    mSeekCount = 0;
    mDecodedFrameCount = 0;
}

bool VDQtVideoDecoder::ensureConversionResources(const AVFrame *sourceFrame) {
    if (!sourceFrame) {
        mLastError = QStringLiteral("Decoder returned an empty video frame.");
        return false;
    }

    const int sourceWidth = sourceFrame->width > 0 ? sourceFrame->width : mWidth;
    const int sourceHeight = sourceFrame->height > 0 ? sourceFrame->height : mHeight;
    const AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(sourceFrame->format);
    const int sizeCheck = av_image_check_size(
        static_cast<unsigned>(std::max(sourceWidth, 0)),
        static_cast<unsigned>(std::max(sourceHeight, 0)), 0, nullptr);
    if (sourceWidth <= 0 || sourceHeight <= 0 || sourceFormat == AV_PIX_FMT_NONE || sizeCheck < 0) {
        mLastError = QStringLiteral("Decoder returned invalid frame dimensions or pixel format.");
        return false;
    }

    int detectedBitDepth = 8;
    bool detectedAlpha = false;
    if (const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(sourceFormat)) {
        for (int component = 0; component < descriptor->nb_components; ++component) {
            detectedBitDepth = std::max(
                detectedBitDepth, static_cast<int>(descriptor->comp[component].depth));
        }
        detectedAlpha = (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
    }

    const int sourceBitDepth = std::max(mSourceBitDepth, detectedBitDepth);
    const bool sourceHasAlpha = mSourceHasAlpha || detectedAlpha;
    const AVPixelFormat outputPixelFormat = sourceBitDepth > 8
        ? AV_PIX_FMT_RGBA64LE
        : sourceHasAlpha ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24;
    const QImage::Format outputImageFormat = sourceBitDepth > 8
        ? QImage::Format_RGBA64
        : sourceHasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    const bool outputFormatMatches = outputPixelFormat == mOutputPixelFormat;
    const bool storageMatches = outputFormatMatches && mFrameRGB && mBuffer
        && sourceWidth == mWidth && sourceHeight == mHeight;
    const bool contextMatches = mSwsCtx
        && mSwsSourceFormat == sourceFormat
        && mSwsDestinationFormat == outputPixelFormat
        && mSwsSourceWidth == sourceWidth
        && mSwsSourceHeight == sourceHeight;
    if (storageMatches && contextMatches) {
        mSourceBitDepth = sourceBitDepth;
        mSourceHasAlpha = sourceHasAlpha;
        return true;
    }

    AVFrame *replacementFrame = nullptr;
    uint8_t *replacementBuffer = nullptr;
    if (!storageMatches) {
        replacementFrame = av_frame_alloc();
        if (!replacementFrame) {
            mLastError = QStringLiteral("Could not allocate RGB storage for a resized video frame.");
            return false;
        }

        const int bufferSize = av_image_get_buffer_size(
            outputPixelFormat, sourceWidth, sourceHeight, 1);
        if (bufferSize <= 0) {
            mLastError = avOperationError(QStringLiteral("Could not size resized RGB frame storage"), bufferSize);
            av_frame_free(&replacementFrame);
            return false;
        }

        replacementBuffer = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(bufferSize)));
        if (!replacementBuffer) {
            mLastError = QStringLiteral("Could not allocate %1 bytes for a resized RGB frame.").arg(bufferSize);
            av_frame_free(&replacementFrame);
            return false;
        }

        const int fillResult = av_image_fill_arrays(
            replacementFrame->data, replacementFrame->linesize, replacementBuffer,
            outputPixelFormat, sourceWidth, sourceHeight, 1);
        if (fillResult < 0) {
            mLastError = avOperationError(QStringLiteral("Could not initialize resized RGB frame storage"), fillResult);
            av_free(replacementBuffer);
            av_frame_free(&replacementFrame);
            return false;
        }
    }

    if (!setupSwsContext(sourceFormat, sourceWidth, sourceHeight, outputPixelFormat)) {
        if (replacementBuffer) av_free(replacementBuffer);
        if (replacementFrame) av_frame_free(&replacementFrame);
        return false;
    }

    if (!storageMatches) {
        if (mBuffer) av_free(mBuffer);
        if (mFrameRGB) av_frame_free(&mFrameRGB);
        mBuffer = replacementBuffer;
        mFrameRGB = replacementFrame;
        mWidth = sourceWidth;
        mHeight = sourceHeight;
    }
    mSourceBitDepth = sourceBitDepth;
    mSourceHasAlpha = sourceHasAlpha;
    mOutputPixelFormat = outputPixelFormat;
    mOutputImageFormat = outputImageFormat;
    return true;
}

bool VDQtVideoDecoder::resetDecoderToStart() {
    if (!mFormatCtx || !mCodecCtx || mVideoStreamIndex < 0 || !mPacket) return false;

    const int64_t timestamp = mStreamStartTimestamp == AV_NOPTS_VALUE ? 0 : mStreamStartTimestamp;
    int result = avformat_seek_file(
        mFormatCtx, mVideoStreamIndex, std::numeric_limits<int64_t>::min(),
        timestamp, timestamp, AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        result = av_seek_frame(mFormatCtx, mVideoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
    }
    if (result < 0) {
        // Elementary-stream demuxers often cannot seek after find_stream_info()
        // has consumed probe data. Reopen only the demuxer (without probing it a
        // second time), retaining the decoder and presentation index.
        AVFormatContext *replacementContext = nullptr;
        const QByteArray pathBytes = mFilePath.toUtf8();
        const bool isConcatManifest = mFilePath.endsWith(
            QStringLiteral(".ffconcat"), Qt::CaseInsensitive);
        const bool isVapourSynthScript = mFilePath.endsWith(
            QStringLiteral(".vpy"), Qt::CaseInsensitive);
        const AVInputFormat *inputFormat = isConcatManifest
            ? av_find_input_format("concat")
            : (isVapourSynthScript ? av_find_input_format("vapoursynth") : nullptr);
        AVDictionary *inputOptions = nullptr;
        if (isConcatManifest) av_dict_set(&inputOptions, "safe", "0", 0);
        const int reopenResult = pathBytes.isEmpty()
            ? AVERROR(EINVAL)
            : avformat_open_input(
                  &replacementContext, pathBytes.constData(), inputFormat, &inputOptions);
        av_dict_free(&inputOptions);
        const bool replacementValid = reopenResult >= 0
            && replacementContext
            && mVideoStreamIndex >= 0
            && mVideoStreamIndex < static_cast<int>(replacementContext->nb_streams)
            && replacementContext->streams[mVideoStreamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        if (replacementValid) {
            avformat_close_input(&mFormatCtx);
            mFormatCtx = replacementContext;
            mCodecCtx->pkt_timebase = mFormatCtx->streams[mVideoStreamIndex]->time_base;
            qWarning() << "[VDQtVideoDecoder] Demuxer is not seekable; reopened it at stream start.";
        } else {
            if (replacementContext) avformat_close_input(&replacementContext);
            mLastError = avOperationError(
                QStringLiteral("Could not seek or reopen the beginning of the video stream"),
                reopenResult < 0 ? reopenResult : result);
            return false;
        }
    }

    avcodec_flush_buffers(mCodecCtx);
    av_frame_unref(mFrame);
    av_packet_unref(mPacket);
    mPacketPending = false;
    mDemuxEof = false;
    mDrainSent = false;
    mLastDecodeReachedEof = false;
    mDiscardUntilKeyFrame = false;
    mCurrentFrameIndex = -1;
    mNextDecodeFrameIndex = 0;
    mPendingSeekTargetTimestamp = AV_NOPTS_VALUE;
    return true;
}

bool VDQtVideoDecoder::seekToFrame(int frameIndex) {
    if (!mFormatCtx || !mCodecCtx || mVideoStreamIndex < 0 || !mPacket) return false;
    if (frameIndex <= 0) return resetDecoderToStart();

    int64_t targetTimestamp = AV_NOPTS_VALUE;
    int anchorIndex = -1;

    if (!mFrameIndex.isEmpty()) {
        const int indexedCount = boundedFrameCount(mFrameIndex.size());
        int candidate = std::min(frameIndex, indexedCount - 1);
        while (candidate > 0 && (!mFrameIndex[candidate].keyFrame || mFrameIndex[candidate].timestamp == AV_NOPTS_VALUE)) {
            --candidate;
        }
        // Only use the anchor if it is a known keyframe close to the target
        if (candidate > 0 && mFrameIndex[candidate].timestamp != AV_NOPTS_VALUE) {
            anchorIndex = candidate;
            targetTimestamp = mFrameIndex[candidate].timestamp;
        }
    }

    if (targetTimestamp == AV_NOPTS_VALUE) {
        if (mFps > 0.0) {
            const AVRational timeBase = mFormatCtx->streams[mVideoStreamIndex]->time_base;
            const double targetSec = frameIndex / mFps;
            targetTimestamp = static_cast<int64_t>(targetSec / av_q2d(timeBase));
            if (mStreamStartTimestamp != AV_NOPTS_VALUE) {
                targetTimestamp += mStreamStartTimestamp;
            }
        } else {
            targetTimestamp = 0;
        }
    }

    int result = av_seek_frame(mFormatCtx, mVideoStreamIndex, targetTimestamp, AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        result = avformat_seek_file(
            mFormatCtx, mVideoStreamIndex, 0, targetTimestamp, targetTimestamp, AVSEEK_FLAG_BACKWARD);
    }
    if (result < 0) {
        return resetDecoderToStart();
    }

    avcodec_flush_buffers(mCodecCtx);
    av_frame_unref(mFrame);
    av_packet_unref(mPacket);
    mPacketPending = false;
    mDemuxEof = false;
    mDrainSent = false;
    mLastDecodeReachedEof = false;
    mDiscardUntilKeyFrame = false;
    mCurrentFrameIndex = (anchorIndex >= 0) ? (anchorIndex - 1) : -1;
    mNextDecodeFrameIndex = (anchorIndex >= 0) ? anchorIndex : frameIndex;
    mPendingSeekTargetTimestamp = (anchorIndex >= 0) ? AV_NOPTS_VALUE : targetTimestamp;
    ++mSeekCount;
    return true;
}

bool VDQtVideoDecoder::decodeNextFrame(int *decodeErrors) {
    if (!mCodecCtx || !mFormatCtx || !mFrame || !mPacket) return false;

    auto recordDecodeError = [&](const QString& operation, int error) {
        if (decodeErrors) ++*decodeErrors;
        mLastError = avOperationError(operation, error);
        qWarning() << "[VDQtVideoDecoder]" << mLastError;
    };

    for (;;) {
        const int receiveResult = avcodec_receive_frame(mCodecCtx, mFrame);
        if (receiveResult == 0) {
            mLastDecodeReachedEof = false;
            ++mDecodedFrameCount;
            return true;
        }
        if (receiveResult == AVERROR_EOF) {
            mLastDecodeReachedEof = true;
            updateFrameCountAtEndOfStream();
            return false;
        }
        if (receiveResult != AVERROR(EAGAIN)) {
            recordDecodeError(QStringLiteral("Video decoder could not produce a frame"), receiveResult);
            avcodec_flush_buffers(mCodecCtx);
            mPacketPending = false;
            av_packet_unref(mPacket);
            if (mErrorMode == 1) mDiscardUntilKeyFrame = true;
        }

        if (mPacketPending) {
            const int sendResult = avcodec_send_packet(mCodecCtx, mPacket);
            if (sendResult == AVERROR(EAGAIN)) {
                // The packet is deliberately retained. receive_frame() will be called
                // again before another input packet is read.
                continue;
            }

            mPacketPending = false;
            av_packet_unref(mPacket);
            if (sendResult == 0) continue;
            if (sendResult == AVERROR_EOF) {
                mDemuxEof = true;
                mDrainSent = true;
                continue;
            }

            recordDecodeError(QStringLiteral("Video decoder rejected a packet"), sendResult);
            if (mErrorMode == 1) {
                avcodec_flush_buffers(mCodecCtx);
                mDiscardUntilKeyFrame = true;
            }
            continue;
        }

        if (mDemuxEof) {
            if (mDrainSent) {
                // A successfully submitted drain packet must eventually produce frames
                // or EOF. Treat an unexpected EAGAIN as EOF instead of spinning forever.
                mLastDecodeReachedEof = true;
                updateFrameCountAtEndOfStream();
                return false;
            }

            const int drainResult = avcodec_send_packet(mCodecCtx, nullptr);
            if (drainResult == AVERROR(EAGAIN)) continue;
            if (drainResult == 0) {
                mDrainSent = true;
                continue;
            }
            if (drainResult == AVERROR_EOF) {
                mDrainSent = true;
                mLastDecodeReachedEof = true;
                updateFrameCountAtEndOfStream();
                return false;
            }

            recordDecodeError(QStringLiteral("Video decoder could not be drained"), drainResult);
            mLastDecodeReachedEof = true;
            updateFrameCountAtEndOfStream();
            return false;
        }

        const int readResult = av_read_frame(mFormatCtx, mPacket);
        if (readResult < 0) {
            if (readResult != AVERROR_EOF) {
                recordDecodeError(QStringLiteral("Could not read the next media packet"), readResult);
            }
            av_packet_unref(mPacket);
            mDemuxEof = true;
            continue;
        }

        if (mPacket->stream_index != mVideoStreamIndex) {
            av_packet_unref(mPacket);
            continue;
        }

        if (mDiscardUntilKeyFrame && !(mPacket->flags & AV_PKT_FLAG_KEY)) {
            av_packet_unref(mPacket);
            continue;
        }
        if (mDiscardUntilKeyFrame) {
            avcodec_flush_buffers(mCodecCtx);
            mDiscardUntilKeyFrame = false;
        }

        mPacketPending = true;
    }
}

int VDQtVideoDecoder::findIndexedFrameByTimestamp(int64_t timestamp, int hint) const {
    if (timestamp == AV_NOPTS_VALUE || mFrameIndex.isEmpty()) return -1;

    const int indexedCount = boundedFrameCount(mFrameIndex.size());
    hint = std::clamp(hint, 0, indexedCount - 1);
    for (int distance = 0; distance < indexedCount; ++distance) {
        const int after = hint + distance;
        if (after < indexedCount && mFrameIndex[after].timestamp == timestamp) return after;
        const int before = hint - distance;
        if (distance && before >= 0 && mFrameIndex[before].timestamp == timestamp) return before;
    }
    return -1;
}

int VDQtVideoDecoder::registerDecodedFrame() {
    int64_t timestamp = mFrame->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE) timestamp = mFrame->pts;

    int frameIndex = mNextDecodeFrameIndex;
    const int indexedMatch = findIndexedFrameByTimestamp(timestamp, mNextDecodeFrameIndex);
    if (indexedMatch >= 0) frameIndex = indexedMatch;

    const int indexedCount = boundedFrameCount(mFrameIndex.size());

    FrameIndexEntry entry;
    entry.timestamp = timestamp;
    entry.duration = mFrame->duration;
    entry.keyFrame = (mFrame->flags & AV_FRAME_FLAG_KEY) != 0;

    if (frameIndex == indexedCount) {
        mFrameIndex.append(entry);
    } else if (frameIndex >= 0 && frameIndex < indexedCount) {
        FrameIndexEntry& indexedEntry = mFrameIndex[frameIndex];
        if (indexedEntry.timestamp == AV_NOPTS_VALUE) indexedEntry.timestamp = entry.timestamp;
        if (indexedEntry.duration <= 0) indexedEntry.duration = entry.duration;
        indexedEntry.keyFrame = indexedEntry.keyFrame || entry.keyFrame;
    }

    mCurrentFrameIndex = frameIndex;
    mNextDecodeFrameIndex = frameIndex + 1;
    return frameIndex;
}

QImage VDQtVideoDecoder::convertDecodedFrameToImage() {
    if (!ensureConversionResources(mFrame)) return QImage();

    sws_scale(
        mSwsCtx,
        const_cast<const uint8_t *const *>(mFrame->data),
        mFrame->linesize,
        0,
        mFrame->height > 0 ? mFrame->height : mHeight,
        mFrameRGB->data,
        mFrameRGB->linesize);

    const QImage frameView(
        mFrameRGB->data[0], mWidth, mHeight, mFrameRGB->linesize[0], mOutputImageFormat);
    return frameView.copy();
}

void VDQtVideoDecoder::updateFrameCountAtEndOfStream() {
    if (mFrameIndex.isEmpty()) return;
    mFrameCount = boundedFrameCount(mFrameIndex.size());
    mFrameCountStatus = FrameCountStatus::Exact;
}

QImage VDQtVideoDecoder::getFrameImage(int frameIndex, bool preserveSequentialDecode) {
    if (!mIsOpen || frameIndex < 0) return QImage();

    if (mFrameCountStatus == FrameCountStatus::Exact
        && (mFrameCount <= 0 || frameIndex >= mFrameCount)) {
        return QImage();
    }

    if (QImage *cached = mFrameCache.object(frameIndex)) return *cached;

    if (mIsAvsNative) {
        QImage image = renderAvsFrame(frameIndex);
        if (!image.isNull()) cacheFrame(frameIndex, image);
        return image;
    }

    if (mIsSyntheticScript) {
        return generateSyntheticFrame(frameIndex);
    }

    if (!mFormatCtx || !mCodecCtx || !mFrame || !mFrameRGB || mVideoStreamIndex < 0) {
        return QImage();
    }

    const bool sequentialRequest = !mLastDecodeReachedEof
        && (frameIndex == mCurrentFrameIndex + 1
            || (preserveSequentialDecode && frameIndex > mCurrentFrameIndex));
    if (!sequentialRequest && !seekToFrame(frameIndex)) return QImage();

    for (;;) {
        if (!decodeNextFrame()) return QImage();

        int64_t timestamp = mFrame->best_effort_timestamp;
        if (timestamp == AV_NOPTS_VALUE) timestamp = mFrame->pts;
        if (mPendingSeekTargetTimestamp != AV_NOPTS_VALUE
            && timestamp != AV_NOPTS_VALUE
            && timestamp < mPendingSeekTargetTimestamp) {
            // av_seek_frame() lands on the preceding keyframe. Decode the
            // dependency chain without allocating/converting throwaway images.
            continue;
        }
        mPendingSeekTargetTimestamp = AV_NOPTS_VALUE;

        const int decodedIndex = registerDecodedFrame();
        if (decodedIndex < frameIndex) continue;

        QImage image = convertDecodedFrameToImage();
        if (image.isNull()) return QImage();
        cacheFrame(decodedIndex, image);
        return image;
    }
}

bool VDQtVideoDecoder::isKeyFrame(int frameIndex) {
    if (!mIsOpen || frameIndex < 0) return false;
    if (mIsAvsNative || mIsSyntheticScript) {
        return mFrameCountStatus != FrameCountStatus::Exact || frameIndex < mFrameCount;
    }
    if (frameIndex >= 0 && frameIndex < mFrameIndex.size()) {
        return mFrameIndex[frameIndex].keyFrame;
    }
    return false;
}

int VDQtVideoDecoder::getPreviousKeyFrame(int frameIndex) {
    if (!mIsOpen) return -1;
    if (mIsAvsNative || mIsSyntheticScript) return std::max(0, frameIndex - 1);

    int candidate = std::min(frameIndex - 1, boundedFrameCount(mFrameIndex.size()) - 1);
    for (; candidate >= 0; --candidate) {
        if (mFrameIndex[candidate].keyFrame) return candidate;
    }
    int step = std::max(1, static_cast<int>(std::round(mFps > 0 ? mFps : 30.0)));
    return std::max(0, frameIndex - step);
}

int VDQtVideoDecoder::getNextKeyFrame(int frameIndex) {
    if (!mIsOpen) return -1;
    if (mIsAvsNative || mIsSyntheticScript) {
        if (mFrameCountStatus == FrameCountStatus::Exact && mFrameCount > 0) {
            return std::min(frameIndex + 1, mFrameCount - 1);
        }
        return frameIndex + 1;
    }

    int candidate = std::max(0, frameIndex + 1);
    while (candidate < boundedFrameCount(mFrameIndex.size())) {
        if (mFrameIndex[candidate].keyFrame) return candidate;
        ++candidate;
    }

    int step = std::max(1, static_cast<int>(std::round(mFps > 0 ? mFps : 30.0)));
    int maxFrame = mFrameCount > 0 ? mFrameCount - 1 : (frameIndex + step);
    return std::min(maxFrame, frameIndex + step);
}

double VDQtVideoDecoder::getFrameTimestampSeconds(int frameIndex) {
    const double unavailable = std::numeric_limits<double>::quiet_NaN();
    if (!mIsOpen || frameIndex < 0) return unavailable;
    if (mIsAvsNative || mIsSyntheticScript) {
        return mFps > 0.0 ? frameIndex / mFps : unavailable;
    }

    if (frameIndex >= 0 && frameIndex < mFrameIndex.size()) {
        const FrameIndexEntry& entry = mFrameIndex[frameIndex];
        if (entry.timestamp != AV_NOPTS_VALUE && mVideoStreamIndex >= 0 && mFormatCtx) {
            int64_t origin = mStreamStartTimestamp;
            if (!mFrameIndex.isEmpty() && mFrameIndex.front().timestamp != AV_NOPTS_VALUE) {
                origin = mFrameIndex.front().timestamp;
            }
            if (origin == AV_NOPTS_VALUE) origin = 0;
            const AVRational timeBase = mFormatCtx->streams[mVideoStreamIndex]->time_base;
            return static_cast<double>(entry.timestamp - origin) * av_q2d(timeBase);
        }
    }

    return mFps > 0.0 ? (frameIndex / mFps) : unavailable;
}

double VDQtVideoDecoder::getFrameDurationSeconds(int frameIndex) {
    const double unavailable = std::numeric_limits<double>::quiet_NaN();
    if (!mIsOpen || frameIndex < 0) return unavailable;
    if (mIsAvsNative || mIsSyntheticScript) return mFps > 0.0 ? 1.0 / mFps : unavailable;

    if (frameIndex >= 0 && frameIndex < mFrameIndex.size() && mVideoStreamIndex >= 0 && mFormatCtx) {
        const AVRational timeBase = mFormatCtx->streams[mVideoStreamIndex]->time_base;
        const FrameIndexEntry& entry = mFrameIndex[frameIndex];

        // AVFrame::duration can reflect decode-order packet spacing rather than
        // presentation-order dwell time on reordered VFR streams. Adjacent PTS
        // values are authoritative for every frame except the final one.
        if (entry.timestamp != AV_NOPTS_VALUE && frameIndex + 1 < mFrameIndex.size()) {
            const int64_t nextTimestamp = mFrameIndex[frameIndex + 1].timestamp;
            if (nextTimestamp != AV_NOPTS_VALUE && nextTimestamp > entry.timestamp) {
                return static_cast<double>(nextTimestamp - entry.timestamp)
                     * av_q2d(timeBase);
            }
        }

        // The final presentation duration is the stream end minus its PTS.
        // Demuxers commonly report this boundary correctly even when the last
        // decoded AVFrame inherited the preceding packet's duration.
        const int64_t streamDuration =
            mFormatCtx->streams[mVideoStreamIndex]->duration;
        if (entry.timestamp != AV_NOPTS_VALUE && frameIndex + 1 == mFrameIndex.size()
            && streamDuration != AV_NOPTS_VALUE && streamDuration > 0) {
            int64_t origin = mStreamStartTimestamp;
            if (!mFrameIndex.isEmpty() && mFrameIndex.front().timestamp != AV_NOPTS_VALUE)
                origin = mFrameIndex.front().timestamp;
            if (origin == AV_NOPTS_VALUE) origin = 0;
            if (origin <= std::numeric_limits<int64_t>::max() - streamDuration) {
                const int64_t streamEnd = origin + streamDuration;
                if (streamEnd > entry.timestamp) {
                    return static_cast<double>(streamEnd - entry.timestamp)
                         * av_q2d(timeBase);
                }
            }
        }

        if (entry.duration > 0) {
            return static_cast<double>(entry.duration) * av_q2d(timeBase);
        }
    }

    return mFps > 0.0 ? 1.0 / mFps : unavailable;
}

void VDQtVideoDecoder::applyErrorMode() {
    if (!mCodecCtx) return;

    if (mErrorMode == 1) {
        mCodecCtx->err_recognition = 0;
        mCodecCtx->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
        mCodecCtx->flags &= ~AV_CODEC_FLAG_OUTPUT_CORRUPT;
    } else if (mErrorMode == 2) {
        mCodecCtx->err_recognition = AV_EF_IGNORE_ERR;
        mCodecCtx->error_concealment = 0;
        mCodecCtx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
    } else {
        mCodecCtx->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_BUFFER;
        mCodecCtx->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
        mCodecCtx->flags &= ~AV_CODEC_FLAG_OUTPUT_CORRUPT;
    }
}

void VDQtVideoDecoder::setErrorMode(int errorMode) {
    mErrorMode = std::clamp(errorMode, 0, 2);
    mDiscardUntilKeyFrame = false;
    applyErrorMode();
}

VDQtVideoDecoder::VDScanResult VDQtVideoDecoder::scanVideoStream(std::function<bool(int currentFrame, int totalFrames)> progressCallback) {
    VDScanResult res;
    if (!mIsOpen) {
        res.errorMessage = "No video stream is currently loaded.";
        return res;
    }

    res.totalFrames = mFrameCount;

    if (mIsAvsNative && mAvsClip) {
        for (int i = 0; i < mFrameCount; ++i) {
            if (progressCallback && !progressCallback(i + 1, mFrameCount)) {
                res.cancelled = true;
                break;
            }

            AVS_VideoFrame *frame = avs_get_frame(mAvsClip, i);
            if (!frame) {
                res.badFrames++;
                res.maskedFrames++;
            } else {
                avs_release_video_frame(frame);
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

    if (!resetDecoderToStart()) {
        res.errorMessage = mLastError;
        return res;
    }

    // Rebuild a contiguous presentation-order index while scanning. This makes
    // the decoded total exact and includes frames emitted only during decoder drain.
    mFrameIndex.clear();
    mCurrentFrameIndex = -1;
    mNextDecodeFrameIndex = 0;
    clearCache();

    int decodedCount = 0;
    for (;;) {
        int decodeErrors = 0;
        if (!decodeNextFrame(&decodeErrors)) {
            res.badFrames += decodeErrors;
            res.maskedFrames += decodeErrors;
            break;
        }

        res.badFrames += decodeErrors;
        res.maskedFrames += decodeErrors;
        const int decodedIndex = registerDecodedFrame();
        decodedCount = decodedIndex + 1;
        if (mFrame->flags & AV_FRAME_FLAG_KEY) ++res.keyFrames;
        if (mFrame->flags & AV_FRAME_FLAG_CORRUPT) {
            ++res.badFrames;
            ++res.maskedFrames;
        }

        const int progressTotal = mFrameCount > 0 ? mFrameCount : decodedCount;
        if (progressCallback && !progressCallback(decodedCount, progressTotal)) {
            res.cancelled = true;
            break;
        }
    }

    if (!res.cancelled) {
        updateFrameCountAtEndOfStream();
        res.totalFrames = mFrameCount;
    } else if (res.totalFrames < decodedCount) {
        res.totalFrames = decodedCount;
    }

    // Restore the interactive decoder to the beginning, retaining the timestamp index.
    if (!resetDecoderToStart() && res.errorMessage.isEmpty()) res.errorMessage = mLastError;
    clearCache();

    return res;
}
