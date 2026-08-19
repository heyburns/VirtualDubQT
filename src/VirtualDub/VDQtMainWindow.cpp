#include "VDQtMainWindow.h"
#include "VDQtSourceSafety.h"
#include <QApplication>
#include <QKeySequence>
#include <QScreen>
#include <QClipboard>
#include <QActionGroup>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QProgressDialog>
#include <QProcess>
#include <QWindow>
#include <QCursor>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSaveFile>
#include <QInputDialog>
#include <QTableWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace {

VDQtOutputSafetyReport loadedOutputSafety(const QString& outputPath,
                                          const VDQtVideoDecoder& decoder,
                                          const VDQtAudioPlayer& audioPlayer) {
    const QStringList protectedSources = {
        decoder.getFilePath(), audioPlayer.getSourcePath()
    };
    const QString scriptPath = decoder.getFilePath();
    return VDQtSourceSafety::evaluateOutputPath(
        outputPath, protectedSources,
        VDQtSourceSafety::isScriptPath(scriptPath) ? scriptPath : QString());
}

QString stagedOutputTemplate(const QString& outputPath) {
    const QFileInfo target(outputPath);
    QString baseName = target.completeBaseName();
    if (baseName.isEmpty()) baseName = QStringLiteral("audio");
    QString pattern = target.dir().filePath(QStringLiteral(".%1.XXXXXX").arg(baseName));
    const QString suffix = target.completeSuffix();
    if (!suffix.isEmpty()) pattern += QLatin1Char('.') + suffix;
    return pattern;
}

bool replaceWithStagedFile(const QString& stagedPath, const QString& outputPath) {
    if (QFileInfo(stagedPath).size() <= 0) return false;
    const QFileInfo existing(outputPath);
    const QFile::Permissions permissions = existing.exists()
        ? existing.permissions()
        : QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
    QFile::setPermissions(stagedPath, permissions);
    const QByteArray stagedName = QFile::encodeName(stagedPath);
    const QByteArray outputName = QFile::encodeName(outputPath);
    return std::rename(stagedName.constData(), outputName.constData()) == 0;
}

void clearLegacyPersistentProcessingSettings()
{
    // Processing choices are session state. Remove values written by older
    // builds while retaining unrelated application history such as Recent Files.
    QSettings applicationSettings("VirtualDub", "VirtualDub_Port");
    applicationSettings.remove(QStringLiteral("decoder"));
    applicationSettings.sync();

    QSettings filterSettings("VirtualDubPort", "FilterSettings");
    filterSettings.clear();
    filterSettings.sync();
}

struct SegmentStreamSignature {
    AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
    AVCodecID codec = AV_CODEC_ID_NONE;
    int width = 0;
    int height = 0;
    int sampleRate = 0;
    int channels = 0;
    QString channelLayout;
    AVRational timeBase{0, 1};
    QByteArray codecConfiguration;
};

struct SegmentSignature {
    QList<SegmentStreamSignature> streams;
};

bool probeSegmentSignature(const QString& path,
                           SegmentSignature *signature,
                           QString *errorMessage) {
    AVFormatContext *context = nullptr;
    const QByteArray encodedPath = QFile::encodeName(path);
    int result = avformat_open_input(&context, encodedPath.constData(), nullptr, nullptr);
    if (result >= 0) result = avformat_find_stream_info(context, nullptr);
    if (result < 0 || !context) {
        char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, buffer, sizeof buffer);
        if (errorMessage)
            *errorMessage = QString("Could not inspect segment %1: %2")
                .arg(path, QString::fromUtf8(buffer));
        if (context) avformat_close_input(&context);
        return false;
    }

    SegmentSignature value;
    for (unsigned int index = 0; index < context->nb_streams; ++index) {
        const AVStream *stream = context->streams[index];
        const AVCodecParameters *parameters = stream->codecpar;
        SegmentStreamSignature streamValue;
        streamValue.type = parameters->codec_type;
        streamValue.codec = parameters->codec_id;
        streamValue.width = parameters->width;
        streamValue.height = parameters->height;
        streamValue.sampleRate = parameters->sample_rate;
        streamValue.channels = parameters->ch_layout.nb_channels;
        char layoutBuffer[256] = {};
        if (parameters->ch_layout.nb_channels > 0
            && av_channel_layout_describe(
                   &parameters->ch_layout, layoutBuffer, sizeof layoutBuffer) >= 0) {
            streamValue.channelLayout = QString::fromUtf8(layoutBuffer);
        }
        streamValue.timeBase = stream->time_base;
        if (parameters->extradata && parameters->extradata_size > 0) {
            streamValue.codecConfiguration = QByteArray(
                reinterpret_cast<const char *>(parameters->extradata),
                parameters->extradata_size);
        }
        value.streams.append(streamValue);
    }
    avformat_close_input(&context);
    if (value.streams.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("The segment contains no media streams.");
        return false;
    }
    if (signature) *signature = value;
    return true;
}

bool compatibleSegments(const SegmentSignature& first,
                        const SegmentSignature& second,
                        QString *errorMessage) {
    if (first.streams.size() != second.streams.size()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Segments have a different number of media streams.");
        return false;
    }
    for (int index = 0; index < first.streams.size(); ++index) {
        const SegmentStreamSignature& a = first.streams.at(index);
        const SegmentStreamSignature& b = second.streams.at(index);
        const bool sameTimeBase = a.timeBase.num == b.timeBase.num
                               && a.timeBase.den == b.timeBase.den;
        if (a.type != b.type || a.codec != b.codec
            || a.width != b.width || a.height != b.height
            || a.sampleRate != b.sampleRate || a.channels != b.channels
            || a.channelLayout != b.channelLayout || !sameTimeBase
            || a.codecConfiguration != b.codecConfiguration) {
            if (errorMessage) {
                *errorMessage = QString(
                    "Stream %1 is not concat-compatible (codec, dimensions, time base, "
                    "audio layout, or codec configuration differs).")
                    .arg(index);
            }
            return false;
        }
    }
    return true;
}

QString escapedConcatPath(const QString& path) {
    QString escaped;
    escaped.reserve(path.size() * 2);
    for (const QChar character : path) {
        if (character == QLatin1Char('\n') || character == QLatin1Char('\r'))
            return QString();
        if (character.isSpace() || character == QLatin1Char('\\')
            || character == QLatin1Char('\'') || character == QLatin1Char('"')
            || character == QLatin1Char('#')) {
            escaped += QLatin1Char('\\');
        }
        escaped += character;
    }
    return escaped;
}

bool writeConcatManifest(const QString& path,
                         const QStringList& sources,
                         QString *errorMessage) {
    if (sources.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("The concatenated timeline is empty.");
        return false;
    }
    QByteArray contents("ffconcat version 1.0\n");
    for (const QString& source : sources) {
        const QString escaped = escapedConcatPath(QFileInfo(source).absoluteFilePath());
        if (escaped.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("A segment path contains a newline.");
            return false;
        }
        contents += "file ";
        contents += escaped.toUtf8();
        contents += '\n';
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(contents) != contents.size()
        || !output.commit()) {
        if (errorMessage) *errorMessage = output.errorString();
        return false;
    }
    return true;
}

} // namespace

VDQtMainWindow::VDQtMainWindow(QWidget *parent)
    : QMainWindow(parent) {
    clearLegacyPersistentProcessingSettings();
    mVideoDecoder.setDecompressionConfig(
        mDecompressionFormatConfig.formatName,
        mDecompressionFormatConfig.colorSpace,
        mDecompressionFormatConfig.componentRange);
    mVideoDecoder.setErrorMode(mDecoderErrorModeConfig.errorMode);

    setWindowTitle("VirtualDubQt v0.1");
    resize(1120, 780);
    setAcceptDrops(true);

    applyTheme();

    // Main layout with central widget
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Video display splitter (Input & Output dual view)
    mVideoSplitter = new QSplitter(Qt::Horizontal, this);
    mInputDisplay = new VDVideoDisplayWidget("Input Frame", this);
    mOutputDisplay = new VDVideoDisplayWidget("Output Frame (Filtered)", this);

    mVideoSplitter->addWidget(mInputDisplay);
    mVideoSplitter->addWidget(mOutputDisplay);
    mVideoSplitter->setSizes({560, 560});
    mainLayout->addWidget(mVideoSplitter, 1);

    // Bottom timeline & position control bar
    mPositionControl = new VDQtPositionControlWidget(this);
    mainLayout->addWidget(mPositionControl);

    setCentralWidget(centralWidget);

    createMenus();
    createStatusBar();

    mPlaybackTimer = new QTimer(this);
    connect(mPlaybackTimer, &QTimer::timeout, this, &VDQtMainWindow::onPlaybackTick);

    mFrameServer = new VDQtFrameServer(this);
    connect(mFrameServer, &VDQtFrameServer::serverStarted, this,
            [this](const QString& path) {
                statusBar()->showMessage(
                    QString("Frame server connected: %1").arg(path));
            });
    connect(mFrameServer, &VDQtFrameServer::serverFinished, this,
            [this](const QString& error) {
                if (error.isEmpty()) {
                    statusBar()->showMessage("Frame server completed");
                } else if (error != QStringLiteral("Frame serving was stopped.")) {
                    VDLogWindow::instance(this)->appendLog(
                        QString("[Frame server] %1").arg(error));
                    QMessageBox::warning(this, "Frame Server", error);
                } else {
                    statusBar()->showMessage("Frame server stopped");
                }
            });

    mFrameDecodeThread = new QThread(this);
    mFrameDecodeWorker = new VDQtFrameDecodeWorker;
    mFrameDecodeWorker->moveToThread(mFrameDecodeThread);
    connect(mFrameDecodeThread, &QThread::finished,
            mFrameDecodeWorker, &QObject::deleteLater);
    connect(mFrameDecodeWorker, &VDQtFrameDecodeWorker::frameReady,
            this, &VDQtMainWindow::onDecodedFrameReady);
    connect(mFrameDecodeWorker, &VDQtFrameDecodeWorker::frameUnavailable,
            this, &VDQtMainWindow::onDecodedFrameUnavailable);
    mFrameDecodeThread->start();

    connect(mPositionControl, &VDQtPositionControlWidget::positionChanged, this, &VDQtMainWindow::onPositionChanged);
    connect(mPositionControl, &VDQtPositionControlWidget::transportActionTriggered, this, &VDQtMainWindow::onTransportAction);
    connect(mPositionControl, &VDQtPositionControlWidget::userScrubStarted, this, [this]() {
        if (mPlaybackTimer->isActive()) {
            mPlaybackTimer->stop();
            mAudioPlayer.pause();
        }
    });

    VDLogWindow::instance(this)->appendLog("[Info] VirtualDub Native C++/Qt6 Linux Port initialized successfully.");
}

VDQtMainWindow::~VDQtMainWindow() {
    if (mPlaybackTimer) mPlaybackTimer->stop();
    if (mFrameServer) mFrameServer->stop();
    if (mFrameDecodeWorker) {
        mFrameDecodeWorker->cancelPending(++mFrameRequestGeneration);
        if (mFrameDecodeThread && mFrameDecodeThread->isRunning()) {
            QMetaObject::invokeMethod(
                mFrameDecodeWorker,
                [worker = mFrameDecodeWorker]() { worker->closeSource(); },
                Qt::BlockingQueuedConnection);
        }
    }
    if (mFrameDecodeThread) {
        mFrameDecodeThread->quit();
        mFrameDecodeThread->wait();
    }
    mFrameDecodeWorker = nullptr;
}

void VDQtMainWindow::applyTheme() {
    setStyleSheet(
        "QMainWindow { background-color: #16161c; color: #e0e0e0; }"
        "QMenuBar { background-color: #1f1f28; color: #d0d0dc; border-bottom: 1px solid #2d2d3c; font-weight: bold; }"
        "QMenuBar::item:selected { background-color: #00bcd4; color: #121216; border-radius: 4px; }"
        "QMenu { background-color: #1c1c26; color: #e0e0e0; border: 1px solid #333345; border-radius: 4px; padding: 4px; }"
        "QMenu::item:selected { background-color: #00bcd4; color: #121216; border-radius: 3px; }"
        "QStatusBar { background-color: #14141a; color: #00bcd4; font-family: monospace; font-weight: bold; border-top: 1px solid #282836; }"
        "QSplitter::handle { background-color: #2a2a38; width: 4px; }"
    );
}

void VDQtMainWindow::createMenus() {
    QMenuBar *bar = menuBar();

    // -------------------------------------------------------------------------
    // FILE MENU (Matching VirtualDub Screenshot)
    // -------------------------------------------------------------------------
    mFileMenu = bar->addMenu("&File");

    actFileOpen = mFileMenu->addAction("&Open video file...", QKeySequence::Open, this, &VDQtMainWindow::onFileOpen);
    actFileReopen = mFileMenu->addAction("&Reopen video file", QKeySequence(Qt::Key_F2), this, &VDQtMainWindow::onFileReopen);
    mFileMenu->addAction("Append video segment...", this, &VDQtMainWindow::onFileAppendSegment);
    actFileClose = mFileMenu->addAction("&Close video file", QKeySequence::Close, this, &VDQtMainWindow::onFileClose);
    mFileMenu->addAction("File &Information...", this, &VDQtMainWindow::onFileInformation);
    mFileMenu->addAction("Set text information...", this, &VDQtMainWindow::onFileSetTextInformation);
    mFileMenu->addSeparator();

    mFileMenu->addAction("Load Project...", this, &VDQtMainWindow::onFileLoadProject);
    mFileMenu->addAction("Save Project", QKeySequence::Save, this, &VDQtMainWindow::onFileSaveProject);
    mFileMenu->addAction("Save Project As...", QKeySequence::SaveAs, this, &VDQtMainWindow::onFileSaveProjectAs);
    mFileMenu->addSeparator();

    actFileSaveAVI = mFileMenu->addAction("Save video...", QKeySequence(Qt::Key_F7), this, &VDQtMainWindow::onFileSaveAVI);
    mFileMenu->addAction("&Save audio...", this, &VDQtMainWindow::onFileSaveAudio);
    mFileMenu->addAction("Run video analysis pass", this, &VDQtMainWindow::onFileRunAnalysisPass);

    QMenu *mExport = mFileMenu->addMenu("Export");
    mExport->addAction("Raw video...", this, &VDQtMainWindow::onFileExportRawVideo);
    mExport->addAction("Image sequence...", this, &VDQtMainWindow::onFileSaveImageSequence);
    mExport->addAction("Animated GIF...", this, &VDQtMainWindow::onFileExportAnimatedGIF);
    mFileMenu->addSeparator();

    mFileMenu->addAction("Load processing settings...", QKeySequence(Qt::CTRL | Qt::Key_L), this, &VDQtMainWindow::onFileLoadProcessingSettings);
    mFileMenu->addAction("Save processing settings...", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, &VDQtMainWindow::onFileSaveProcessingSettings);
    mFileMenu->addAction("Run script...", this, &VDQtMainWindow::onFileRunScript);
    mFileMenu->addAction("Batch wizard...", this, &VDQtMainWindow::onFileBatchWizard);
    mFileMenu->addAction("Job control...", this, &VDQtMainWindow::onFileJobControl);
    mFileMenu->addAction("Start frame server...", this, &VDQtMainWindow::onFileStartFrameServer);
    mFileMenu->addAction("Stop frame server", this, &VDQtMainWindow::onFileStopFrameServer);
    mFileMenu->addSeparator();

    mRecentSeparator = mFileMenu->addSeparator();
    for (int i = 0; i < 4; ++i) {
        QAction *action = new QAction(this);
        action->setVisible(false);
        connect(action, &QAction::triggered, this, &VDQtMainWindow::onOpenRecentFile);
        mFileMenu->insertAction(mRecentSeparator, action);
        mRecentFileActions.append(action);
    }

    actFileQuit = mFileMenu->addAction("&Quit", QKeySequence::Quit, this, &VDQtMainWindow::onFileQuit);

    updateRecentFilesMenu();

    // -------------------------------------------------------------------------
    // EDIT MENU
    // -------------------------------------------------------------------------
    QMenu *mEdit = bar->addMenu("&Edit");
    mEdit->addAction("Set selection &start", QKeySequence(Qt::Key_BracketLeft), this, &VDQtMainWindow::onEditSetSelectionStart);
    mEdit->addAction("Set selection &end", QKeySequence(Qt::Key_BracketRight), this, &VDQtMainWindow::onEditSetSelectionEnd);
    mEdit->addAction("Select &All", QKeySequence::SelectAll, this, &VDQtMainWindow::onEditSelectAll);

    // -------------------------------------------------------------------------
    // VIEW MENU
    // -------------------------------------------------------------------------
    QMenu *mView = bar->addMenu("&View");
    mView->addAction("&Dual View (Input & Output)", this, &VDQtMainWindow::onViewDualView);
    mView->addAction("&Input Video Only", this, &VDQtMainWindow::onViewInputOnly);
    mView->addAction("&Output Video Only", this, &VDQtMainWindow::onViewOutputOnly);
    mView->addSeparator();
    mView->addAction("&Auto Size Window to Video", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), this, &VDQtMainWindow::autoFitWindowToVideo);
    mView->addSeparator();
    mView->addAction("&Log Window...", this, &VDQtMainWindow::onViewLogWindow);

    // -------------------------------------------------------------------------
    // VIDEO MENU (Matching VirtualDub Screenshot)
    // -------------------------------------------------------------------------
    QMenu *mVideo = bar->addMenu("&Video");
    actVideoFilters = mVideo->addAction("&Filters...", QKeySequence(Qt::CTRL | Qt::Key_F), this, &VDQtMainWindow::onVideoFilters);
    mVideo->addAction("Frame &Rate...", QKeySequence(Qt::CTRL | Qt::Key_R), this, &VDQtMainWindow::onVideoFrameRate);
    mVideo->addAction("&Decode Format...", this, &VDQtMainWindow::onVideoDecodeFormat);
    actVideoCompression = mVideo->addAction("&Compression...", QKeySequence(Qt::CTRL | Qt::Key_P), this, &VDQtMainWindow::onVideoCompression);
    mVideo->addAction("&Select Range...", this, &VDQtMainWindow::onVideoSelectRange);

    mVideo->addSeparator();

    QActionGroup *grpVideoMode = new QActionGroup(this);
    grpVideoMode->setExclusive(true);

    actVideoDirectStream = mVideo->addAction("&Direct stream copy", this, &VDQtMainWindow::onVideoModeDirectStream);
    actVideoDirectStream->setCheckable(true);
    grpVideoMode->addAction(actVideoDirectStream);

    actVideoFastRecompress = mVideo->addAction("Fast recompress", this, &VDQtMainWindow::onVideoModeFastRecompress);
    actVideoFastRecompress->setCheckable(true);
    actVideoFastRecompress->setToolTip(
        "Recompress through FFmpeg's native pixel formats without RGB conversion or video filters.");
    grpVideoMode->addAction(actVideoFastRecompress);

    actVideoNormalRecompress = mVideo->addAction("Normal recompress", this, &VDQtMainWindow::onVideoModeNormalRecompress);
    actVideoNormalRecompress->setCheckable(true);
    grpVideoMode->addAction(actVideoNormalRecompress);

    actVideoFullProcessing = mVideo->addAction("&Full processing mode", this, &VDQtMainWindow::onVideoModeFullProcessing);
    actVideoFullProcessing->setCheckable(true);
    actVideoFullProcessing->setChecked(true);
    grpVideoMode->addAction(actVideoFullProcessing);

    actVideoSmartRendering = mVideo->addAction("Smart rendering");
    actVideoSmartRendering->setCheckable(true);
    actVideoSmartRendering->setToolTip(
        "Copy clean GOP-aligned ranges without re-encoding; use the selected "
        "recompression mode when an exact cut or processing requires it.");
    connect(actVideoSmartRendering, &QAction::toggled, this,
            [this](bool enabled) { mSmartRendering = enabled; });
    actVideoPreserveEmptyFrames = mVideo->addAction("Preserve empty frames");
    actVideoPreserveEmptyFrames->setCheckable(true);
    actVideoPreserveEmptyFrames->setChecked(true);
    actVideoPreserveEmptyFrames->setToolTip(
        "Retain null-frame/timestamp gaps as displayed dwell time during recompression. "
        "Direct stream copy always preserves the compressed timeline unchanged.");
    connect(actVideoPreserveEmptyFrames, &QAction::toggled, this,
            [this](bool enabled) { mPreserveEmptyFrames = enabled; });

    mVideo->addSeparator();

    mVideo->addAction("Copy source frame to clipboard", QKeySequence(Qt::CTRL | Qt::Key_1), this, &VDQtMainWindow::onVideoCopySourceFrame);
    mVideo->addAction("Copy output frame to clipboard", QKeySequence(Qt::CTRL | Qt::Key_2), this, &VDQtMainWindow::onVideoCopyOutputFrame);
    mVideo->addAction("Copy source frame number to clipboard", this, &VDQtMainWindow::onVideoCopySourceFrameNum);
    mVideo->addAction("Copy output frame number to clipboard", this, &VDQtMainWindow::onVideoCopyOutputFrameNum);
    mVideo->addAction("Scan video stream for errors....", this, &VDQtMainWindow::onVideoScanErrors);
    mVideo->addAction("&Error mode...", this, &VDQtMainWindow::onVideoErrorMode);

    // -------------------------------------------------------------------------
    // AUDIO MENU
    // -------------------------------------------------------------------------
    QMenu *mAudio = bar->addMenu("&Audio");
    actAudioDirectStream = mAudio->addAction("&Direct stream copy", this, &VDQtMainWindow::onAudioModeDirectStream);
    actAudioDirectStream->setCheckable(true);
    actAudioDirectStream->setChecked(true);

    actAudioFullProcessing = mAudio->addAction("&Full processing mode", this, &VDQtMainWindow::onAudioModeFullProcessing);
    actAudioFullProcessing->setCheckable(true);

    mAudio->addSeparator();
    actAudioCompression = mAudio->addAction("&Compression...", this, &VDQtMainWindow::onAudioCompression);

    QMenu *mOptions = bar->addMenu("&Options");
    mOptions->addAction("&Preferences...", this, &VDQtMainWindow::onOptionsPreferences);

    // -------------------------------------------------------------------------
    // HELP MENU
    // -------------------------------------------------------------------------
    QMenu *mHelp = bar->addMenu("&Help");
    mHelp->addAction("&About VirtualDub...", this, &VDQtMainWindow::onHelpAbout);
}

void VDQtMainWindow::createStatusBar() {
    statusBar()->showMessage("VirtualDub Ready | Pipeline: Idle | Codec: Uncompressed RGB24 | 1920x1080 @ 29.97 fps");
}

bool VDQtMainWindow::openVideoFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;

    onFileClose();

    if (mVideoDecoder.openFile(filePath)) {
        QString interactiveError;
        if (!openInteractiveDecoder(filePath, &interactiveError)) {
            mVideoDecoder.close();
            QMessageBox::critical(
                this, "VirtualDub Error",
                QString("Could not initialize interactive video decoding:\n%1")
                    .arg(interactiveError));
            return false;
        }

        if (mVideoDecoder.isAvsNative()) {
            // The native AviSynth clip is authoritative. In particular, do not
            // resurrect audio from a parsed source file when the script has
            // intentionally removed it (for example with KillAudio()).
            const AVS_VideoInfo *videoInfo = mVideoDecoder.getAvsVi();
            if (videoInfo && avs_has_audio(videoInfo)) {
                mAvsAudioDecoder.setDecompressionConfig(
                    mDecompressionFormatConfig.formatName,
                    mDecompressionFormatConfig.colorSpace,
                    mDecompressionFormatConfig.componentRange);
                mAvsAudioDecoder.setErrorMode(mDecoderErrorModeConfig.errorMode);
                if (mAvsAudioDecoder.openFile(filePath)) {
                    mAudioPlayer.openAvsClip(
                        mAvsAudioDecoder.getAvsClip(), mAvsAudioDecoder.getAvsVi());
                } else {
                    VDLogWindow::instance(this)->appendLog(
                        QString("[Audio] AviSynth audio graph could not be opened independently: %1")
                            .arg(mAvsAudioDecoder.getLastError()));
                }
            }
        } else {
            // A script successfully evaluated by the video decoder is its own
            // authoritative audio graph. Opening a guessed underlying media
            // file would resurrect KillAudio() output or bypass script edits.
            mAudioPlayer.openFile(filePath);
        }

        const bool concatenated = filePath.endsWith(
            QStringLiteral(".ffconcat"), Qt::CaseInsensitive);
        if (concatenated) {
            mTimelineSources =
                VDQtVideoDecoder::auditScriptDependencies(filePath).resolvedPaths;
        } else {
            mTimelineSources = { QFileInfo(filePath).absoluteFilePath() };
        }
        const QString displayName = concatenated
            ? QString("Concatenated timeline (%1 segments)").arg(mTimelineSources.size())
            : filePath;
        mInputDisplay->setLabelText(QString("Loaded: %1").arg(displayName));
        mOutputDisplay->setLabelText(QString("Filtered Output: %1").arg(displayName));
        mPositionControl->SetRange(0, std::max(0, mVideoDecoder.getFrameCount() - 1));
        mPositionControl->SetPosition(0);

        updateFrameDisplay(0);
        mPositionControl->SetFrameRate(mVideoDecoder.getFps());
        autoFitWindowToVideo();

        setWindowTitle(QString("VirtualDubQt v0.1 - [%1]").arg(
            concatenated ? displayName : QFileInfo(filePath).fileName()));

        VDLogWindow::instance(this)->appendLog(QString("[File] Opened video stream: %1 (%2x%3 @ %4 fps, %5 frames)")
            .arg(filePath)
            .arg(mVideoDecoder.getWidth())
            .arg(mVideoDecoder.getHeight())
            .arg(mVideoDecoder.getFps(), 0, 'f', 2)
            .arg(mVideoDecoder.getFrameCount()));

        if (!concatenated) addRecentFile(filePath);
        return true;
    } else {
        bool isScript = filePath.endsWith(".avs", Qt::CaseInsensitive) || filePath.endsWith(".vpy", Qt::CaseInsensitive);
        QString errorDetails = mVideoDecoder.getLastError();
        if (isScript && !errorDetails.isEmpty()) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle("VirtualDub Error");
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setText("Video script open failure:");
            msgBox.setInformativeText(errorDetails);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.exec();

            VDLogWindow::instance(this)->appendLog(QString("[Error] Video script open failure: %1").arg(errorDetails));
        } else {
            QString errMsg = QString("Could not open file:\n%1").arg(filePath);
            if (!errorDetails.isEmpty()) {
                errMsg += QString("\n\nDetails:\n%1").arg(errorDetails);
            }
            QMessageBox::critical(this, "VirtualDub Error", errMsg);
            VDLogWindow::instance(this)->appendLog(QString("[Error] Failed to open: %1 (%2)").arg(filePath, errorDetails));
        }
        return false;
    }
}

void VDQtMainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void VDQtMainWindow::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void VDQtMainWindow::dropEvent(QDropEvent *event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        QString localPath = urls.first().toLocalFile();
        if (!localPath.isEmpty()) {
            openVideoFile(localPath);
            event->acceptProposedAction();
        }
    }
}

void VDQtMainWindow::onFileOpen() {
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Open Video / Script File",
        QString(),
        "Video Scripts (*.avs *.AVS *.vpy *.VPY);;All Video & Media Files (*.avi *.mp4 *.mkv *.mov *.webm *.flv *.wmv *.avs *.AVS *.vpy *.VPY);;All Files (*)"
    );

    if (!fileName.isEmpty()) {
        openVideoFile(fileName);
    }
}

void VDQtMainWindow::onFileAppendSegment() {
    if (!mVideoDecoder.isOpen() || mTimelineSources.isEmpty()) {
        QMessageBox::warning(
            this, "Append Segment",
            "Open the first media segment before appending another segment.");
        return;
    }
    for (const QString& source : mTimelineSources) {
        const QString suffix = QFileInfo(source).suffix().toLower();
        if (suffix == QStringLiteral("avs") || suffix == QStringLiteral("vpy")
            || suffix == QStringLiteral("py") || suffix == QStringLiteral("avsi")) {
            QMessageBox::warning(
                this, "Append Segment",
                "Script-backed clips cannot be appended through the compressed-segment timeline. "
                "Render the scripts to compatible media files first.");
            return;
        }
    }

    const QStringList additions = QFileDialog::getOpenFileNames(
        this, "Append Video Segment", QFileInfo(mTimelineSources.last()).absolutePath(),
        "Video & Media Files (*.avi *.mp4 *.mkv *.mov *.webm *.flv *.wmv *.nut *.ts *.m2ts);;All Files (*)");
    if (additions.isEmpty()) return;

    SegmentSignature reference;
    QString error;
    if (!probeSegmentSignature(mTimelineSources.first(), &reference, &error)) {
        QMessageBox::critical(this, "Append Segment Error", error);
        return;
    }
    QStringList newTimeline = mTimelineSources;
    for (const QString& addition : additions) {
        SegmentSignature candidate;
        if (!probeSegmentSignature(addition, &candidate, &error)
            || !compatibleSegments(reference, candidate, &error)) {
            QMessageBox::critical(
                this, "Incompatible Segment",
                QString("The segment cannot be appended safely:\n%1\n\n%2")
                    .arg(addition, error));
            return;
        }
        newTimeline.append(QFileInfo(addition).absoluteFilePath());
    }

    if (!mTimelineTempDirectory.isValid()) {
        QMessageBox::critical(
            this, "Append Segment Error",
            "A temporary timeline directory could not be created.");
        return;
    }
    const QString manifestPath = mTimelineTempDirectory.filePath(
        QStringLiteral("timeline.ffconcat"));
    const QStringList oldTimeline = mTimelineSources;
    if (!writeConcatManifest(manifestPath, newTimeline, &error)) {
        QMessageBox::critical(this, "Append Segment Error", error);
        return;
    }

    VDQtVideoDecoder validationDecoder;
    validationDecoder.setDecompressionConfig(
        mDecompressionFormatConfig.formatName,
        mDecompressionFormatConfig.colorSpace,
        mDecompressionFormatConfig.componentRange);
    validationDecoder.setErrorMode(mDecoderErrorModeConfig.errorMode);
    if (!validationDecoder.openFile(manifestPath)) {
        writeConcatManifest(manifestPath, oldTimeline, nullptr);
        QMessageBox::critical(
            this, "Append Segment Error",
            QString("FFmpeg rejected the concatenated timeline:\n%1")
                .arg(validationDecoder.getLastError()));
        return;
    }
    validationDecoder.close();

    const VDQtProcessingState processing = captureProcessingState();
    const int oldPosition = mPositionControl->GetPosition();
    const qint64 oldSelectionStart = mPositionControl->GetSelectionStart();
    const qint64 oldSelectionEnd = mPositionControl->GetSelectionEnd();
    const bool hadSelection = mPositionControl->hasSelection();
    if (!openVideoFile(manifestPath)) {
        writeConcatManifest(manifestPath, oldTimeline, nullptr);
        const QString restorationSource = oldTimeline.size() > 1
            ? manifestPath : oldTimeline.value(0);
        if (!restorationSource.isEmpty() && openVideoFile(restorationSource)) {
            applyProcessingState(processing);
            mTimelineSources = oldTimeline;
            mPositionControl->SetPosition(std::min(
                oldPosition, std::max(0, mVideoDecoder.getFrameCount() - 1)));
            if (hadSelection)
                mPositionControl->SetSelection(oldSelectionStart, oldSelectionEnd);
        }
        return;
    }
    applyProcessingState(processing);
    mTimelineSources = newTimeline;
    mCurrentProjectPath.clear();
    mPositionControl->SetPosition(std::min(
        oldPosition, std::max(0, mVideoDecoder.getFrameCount() - 1)));
    if (hadSelection)
        mPositionControl->SetSelection(oldSelectionStart, oldSelectionEnd);
    statusBar()->showMessage(
        QString("Appended %1 segment(s); timeline now has %2 segments")
            .arg(additions.size()).arg(newTimeline.size()));
}

void VDQtMainWindow::onFileClose() {
    mPlaybackTimer->stop();
    if (mFrameServer) mFrameServer->stop();
    closeInteractiveDecoder();
    mAudioPlayer.close();
    mAvsAudioDecoder.close();
    QCoreApplication::processEvents();
    mVideoDecoder.close();
    mInputDisplay->clearDisplay();
    mOutputDisplay->clearDisplay();
    mPositionControl->SetRange(0, 0);
    mPositionControl->SetPosition(0);
    mPositionControl->SetSelection(0, 0);
    mTextMetadata.clear();
    mCurrentProjectPath.clear();
    mTimelineSources.clear();
    statusBar()->showMessage("No Video File Loaded");
    VDLogWindow::instance(this)->appendLog("[File] Closed current video session.");
}

void VDQtMainWindow::onFileInformation() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::information(this, "File Information", "No video stream loaded.");
        return;
    }
    QString info = QString("File: %1\n\nVideo stream:\n  Frame size: %2x%3\n  Frame rate: %4 fps\n  Total frames: %5\n  Color format: %6\n\nAudio stream:\n  Sample rate: %7 Hz\n  Channels: %8\n  Bits per sample: %9-bit\n  Layout: %10")
        .arg(mVideoDecoder.getFilePath())
        .arg(mVideoDecoder.getWidth())
        .arg(mVideoDecoder.getHeight())
        .arg(mVideoDecoder.getFps(), 0, 'f', 3)
        .arg(mVideoDecoder.getFrameCount())
        .arg(mVideoDecoder.getPixFormat())
        .arg(mAudioPlayer.getSampleRate())
        .arg(mAudioPlayer.getChannels())
        .arg(mAudioPlayer.getBitsPerSample())
        .arg(mAudioPlayer.getAudioLayoutString());

    QMessageBox::information(this, "File Information", info);
}

void VDQtMainWindow::onFileSetTextInformation() {
    QDialog dialog(this);
    dialog.setWindowTitle("Text Information");
    QFormLayout *form = new QFormLayout(&dialog);
    const struct {
        const char *key;
        const char *label;
    } fields[] = {
        { "title", "Title:" },
        { "artist", "Artist / Author:" },
        { "comment", "Comment:" },
        { "copyright", "Copyright:" },
        { "date", "Date:" }
    };
    QMap<QString, QLineEdit *> editors;
    for (const auto& field : fields) {
        auto *editor = new QLineEdit(
            mTextMetadata.value(QString::fromLatin1(field.key)), &dialog);
        editor->setMaxLength(65536);
        form->addRow(QString::fromLatin1(field.label), editor);
        editors.insert(QString::fromLatin1(field.key), editor);
    }
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    mTextMetadata.clear();
    for (auto it = editors.cbegin(); it != editors.cend(); ++it) {
        const QString text = it.value()->text().trimmed();
        if (!text.isEmpty()) mTextMetadata.insert(it.key(), text);
    }
    statusBar()->showMessage(
        QString("Text information updated (%1 field(s))").arg(mTextMetadata.size()));
}

VDQtProcessingState VDQtMainWindow::captureProcessingState() const {
    VDQtProcessingState state;
    state.videoMode = mVideoMode;
    state.audioMode = mAudioMode;
    state.smartRendering = mSmartRendering;
    state.preserveEmptyFrames = mPreserveEmptyFrames;
    state.frameRate = mFrameRateConfig;
    state.decompression = mDecompressionFormatConfig;
    state.decoderErrorMode = mDecoderErrorModeConfig;
    state.rawVideo = mRawVideoExportConfig;
    state.videoCodec = VDQtCodecEngine::instance().getVideoParams();
    state.audioCodec = VDQtCodecEngine::instance().getAudioParams();
    state.filters = VDQtFilterSystem::instance().getActiveChain();
    state.textMetadata = mTextMetadata;
    return state;
}

void VDQtMainWindow::applyProcessingState(const VDQtProcessingState& state) {
    mVideoMode = state.videoMode;
    mAudioMode = state.audioMode;
    mSmartRendering = state.smartRendering;
    mPreserveEmptyFrames = state.preserveEmptyFrames;
    mFrameRateConfig = state.frameRate;
    mDecompressionFormatConfig = state.decompression;
    mDecoderErrorModeConfig = state.decoderErrorMode;
    mRawVideoExportConfig = state.rawVideo;
    mTextMetadata = state.textMetadata;

    actVideoDirectStream->setChecked(mVideoMode == VideoMode_DirectStreamCopy);
    actVideoFastRecompress->setChecked(mVideoMode == VideoMode_FastRecompress);
    actVideoNormalRecompress->setChecked(mVideoMode == VideoMode_NormalRecompress);
    actVideoFullProcessing->setChecked(mVideoMode == VideoMode_FullProcessing);
    actAudioDirectStream->setChecked(mAudioMode == AudioMode_DirectStreamCopy);
    actAudioFullProcessing->setChecked(mAudioMode == AudioMode_FullProcessing);
    actVideoSmartRendering->setChecked(mSmartRendering);
    actVideoPreserveEmptyFrames->setChecked(mPreserveEmptyFrames);

    VDQtCodecEngine::instance().setVideoParams(state.videoCodec);
    VDQtCodecEngine::instance().setAudioParams(state.audioCodec);
    VDAudioCodecConfig audioConfig;
    audioConfig.codecId = state.audioCodec.codecId;
    audioConfig.codecName = state.audioCodec.codecId;
    audioConfig.rateControlMode = state.audioCodec.rateMode;
    audioConfig.bitrateKbps = state.audioCodec.bitrateKbps;
    audioConfig.vbrQuality = state.audioCodec.vbrQuality;
    audioConfig.sampleRate = state.audioCodec.sampleRate;
    audioConfig.channels = state.audioCodec.channels;
    VDQtCodecSettings::instance().setAudioConfig(audioConfig);

    VDQtFilterSystem::instance().replaceActiveChain(state.filters);
    mVideoDecoder.setDecompressionConfig(
        mDecompressionFormatConfig.formatName,
        mDecompressionFormatConfig.colorSpace,
        mDecompressionFormatConfig.componentRange);
    mVideoDecoder.setErrorMode(mDecoderErrorModeConfig.errorMode);
    mAvsAudioDecoder.setDecompressionConfig(
        mDecompressionFormatConfig.formatName,
        mDecompressionFormatConfig.colorSpace,
        mDecompressionFormatConfig.componentRange);
    mAvsAudioDecoder.setErrorMode(mDecoderErrorModeConfig.errorMode);
    if (mFrameDecodeWorker && mFrameDecodeThread
        && mFrameDecodeThread->isRunning()) {
        QMetaObject::invokeMethod(
            mFrameDecodeWorker,
            [this]() {
                mFrameDecodeWorker->setDecompressionConfig(
                    mDecompressionFormatConfig.formatName,
                    mDecompressionFormatConfig.colorSpace,
                    mDecompressionFormatConfig.componentRange);
                mFrameDecodeWorker->setErrorMode(
                    mDecoderErrorModeConfig.errorMode);
            },
            Qt::BlockingQueuedConnection);
    }
    syncInteractiveFilterChain();
    if (mVideoDecoder.isOpen())
        updateFrameDisplay(mPositionControl->GetPosition());
}

VDQtVideoExporter::ExportOptions VDQtMainWindow::currentExportOptions(
    const QString& outputPath,
    const QString& containerType,
    bool fastStart,
    bool fullSourceRange) const {
    VDQtVideoExporter::ExportOptions options;
    options.inputPath = mVideoDecoder.getFilePath();
    options.outputPath = outputPath;
    if (!fullSourceRange && mPositionControl->hasSelection()) {
        options.startFrame = std::max(
            0, static_cast<int>(mPositionControl->GetSelectionStart()));
        options.endFrame = std::max(
            options.startFrame,
            static_cast<int>(mPositionControl->GetSelectionEnd() - 1));
    } else {
        options.startFrame = 0;
        options.endFrame = -1;
    }
    if (mFrameRateConfig.sourceMode == 1) {
        options.customFps = mFrameRateConfig.customSourceFps;
    } else if (mFrameRateConfig.convMode == 4) {
        options.customFps = mFrameRateConfig.convertFps;
        options.convertFpsPreserveDuration = true;
    }
    if (mFrameRateConfig.convMode == 1) options.decimateFactor = 2;
    else if (mFrameRateConfig.convMode == 2) options.decimateFactor = 3;
    else if (mFrameRateConfig.convMode == 3)
        options.decimateFactor = std::max(1, mFrameRateConfig.decimateN);
    options.videoMode = mVideoMode;
    options.audioMode = mAudioMode;
    options.containerType = containerType;
    options.fastStart = fastStart;
    options.metadata = mTextMetadata;
    options.smartRendering = mSmartRendering;
    options.preserveEmptyFrames = mPreserveEmptyFrames;
    return options;
}

QString VDQtMainWindow::primarySessionSourcePath() const {
    return mTimelineSources.isEmpty()
        ? mVideoDecoder.getFilePath() : mTimelineSources.first();
}

void VDQtMainWindow::onFileLoadProject() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Load Project", QString(),
        "VirtualDubQT Project (*.vdqproject);;All Files (*)");
    if (path.isEmpty()) return;
    loadProjectFile(path);
}

bool VDQtMainWindow::loadProjectFile(const QString& path) {
    VDQtProjectState project;
    QString error;
    if (!VDQtProjectFile::loadProject(path, &project, &error)) {
        QMessageBox::critical(this, "Load Project Error", error);
        return false;
    }

    QString sourceToOpen = project.sourcePath;
    if (project.sourcePaths.size() > 1) {
        SegmentSignature reference;
        if (!probeSegmentSignature(project.sourcePaths.first(), &reference, &error)) {
            QMessageBox::critical(this, "Load Project Error", error);
            return false;
        }
        for (int index = 1; index < project.sourcePaths.size(); ++index) {
            SegmentSignature candidate;
            if (!probeSegmentSignature(project.sourcePaths.at(index), &candidate, &error)
                || !compatibleSegments(reference, candidate, &error)) {
                QMessageBox::critical(
                    this, "Load Project Error",
                    QString("Project segment %1 is no longer concat-compatible:\n%2\n\n%3")
                        .arg(index + 1)
                        .arg(project.sourcePaths.at(index), error));
                return false;
            }
        }
        if (!mTimelineTempDirectory.isValid()) {
            QMessageBox::critical(
                this, "Load Project Error",
                "A temporary timeline directory could not be created.");
            return false;
        }
        sourceToOpen = mTimelineTempDirectory.filePath(
            QString("project_%1.ffconcat").arg(
                QUuid::createUuid().toString(QUuid::Id128)));
        if (!writeConcatManifest(sourceToOpen, project.sourcePaths, &error)) {
            QMessageBox::critical(this, "Load Project Error", error);
            return false;
        }
    }

    VDQtVideoDecoder validationDecoder;
    validationDecoder.setDecompressionConfig(
        project.processing.decompression.formatName,
        project.processing.decompression.colorSpace,
        project.processing.decompression.componentRange);
    validationDecoder.setErrorMode(project.processing.decoderErrorMode.errorMode);
    if (!validationDecoder.openFile(sourceToOpen)) {
        QMessageBox::critical(
            this, "Load Project Error",
            QString("The project source could not be opened:\n%1\n\n%2")
                .arg(sourceToOpen, validationDecoder.getLastError()));
        return false;
    }
    validationDecoder.close();

    applyProcessingState(project.processing);
    if (!openVideoFile(sourceToOpen)) return false;
    applyProcessingState(project.processing);
    mTimelineSources = project.sourcePaths;
    mCurrentProjectPath = QFileInfo(path).absoluteFilePath();

    const qint64 requestedLast = std::max(
        project.position,
        project.hasSelection ? project.selectionEnd - 1 : 0);
    if (!mVideoDecoder.isFrameCountExact()
        && requestedLast >= mVideoDecoder.getFrameCount()) {
        if (!ensureExactFrameRange(QStringLiteral("project timeline"))) return false;
    }
    const int frameCount = mVideoDecoder.getFrameCount();
    if (frameCount > 0) {
        mPositionControl->SetRange(0, frameCount - 1);
        if (project.hasSelection && project.selectionStart < frameCount) {
            mPositionControl->SetSelection(
                project.selectionStart,
                std::min<qint64>(project.selectionEnd, frameCount));
        }
        mPositionControl->SetPosition(
            static_cast<int>(std::min<qint64>(project.position, frameCount - 1)));
    }
    statusBar()->showMessage(
        QString("Project loaded: %1").arg(QFileInfo(path).fileName()));
    return true;
}

void VDQtMainWindow::onFileSaveProject() {
    if (mCurrentProjectPath.isEmpty()) {
        onFileSaveProjectAs();
        return;
    }
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, "Save Project", "Open a source before saving a project.");
        return;
    }
    const VDQtOutputSafetyReport projectSafety = loadedOutputSafety(
        mCurrentProjectPath, mVideoDecoder, mAudioPlayer);
    if (projectSafety.issue == VDQtOutputSafetyIssue::AliasesLoadedSource) {
        QMessageBox::critical(
            this, "Unsafe Project Path",
            "The project path aliases a loaded or script-referenced media source. "
            "Choose a different project path.");
        return;
    }
    VDQtProjectState project;
    project.sourcePaths = mTimelineSources;
    if (project.sourcePaths.isEmpty())
        project.sourcePaths = { mVideoDecoder.getFilePath() };
    project.sourcePath = project.sourcePaths.first();
    project.position = mPositionControl->GetPosition();
    project.hasSelection = mPositionControl->hasSelection();
    project.selectionStart = mPositionControl->GetSelectionStart();
    project.selectionEnd = mPositionControl->GetSelectionEnd();
    project.processing = captureProcessingState();
    QString error;
    if (!VDQtProjectFile::saveProject(mCurrentProjectPath, project, &error)) {
        QMessageBox::critical(this, "Save Project Error", error);
        return;
    }
    statusBar()->showMessage(
        QString("Project saved: %1").arg(QFileInfo(mCurrentProjectPath).fileName()));
}

void VDQtMainWindow::onFileSaveProjectAs() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, "Save Project", "Open a source before saving a project.");
        return;
    }
    QString suggested = mCurrentProjectPath;
    if (suggested.isEmpty()) {
        const QFileInfo source(primarySessionSourcePath());
        suggested = source.dir().filePath(
            source.completeBaseName() + QStringLiteral(".vdqproject"));
    }
    QString path = QFileDialog::getSaveFileName(
        this, "Save Project As", suggested,
        "VirtualDubQT Project (*.vdqproject);;All Files (*)");
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".vdqproject");
    const VDQtOutputSafetyReport projectSafety =
        loadedOutputSafety(path, mVideoDecoder, mAudioPlayer);
    if (!projectSafety.isSafe()) {
        QMessageBox::critical(
            this, "Unsafe Project Path",
            "The project path aliases a loaded or script-referenced media source, or "
            "cannot be safely audited. Choose a different project path.");
        return;
    }
    mCurrentProjectPath = QFileInfo(path).absoluteFilePath();
    onFileSaveProject();
}

void VDQtMainWindow::onFileLoadProcessingSettings() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Load Processing Settings", QString(),
        "VirtualDubQT Processing Settings (*.vdqsettings);;All Files (*)");
    if (path.isEmpty()) return;
    VDQtProcessingState state;
    QString error;
    if (!VDQtProjectFile::loadProcessingSettings(path, &state, &error)) {
        QMessageBox::critical(this, "Load Processing Settings Error", error);
        return;
    }
    applyProcessingState(state);
    statusBar()->showMessage(
        QString("Processing settings loaded: %1").arg(QFileInfo(path).fileName()));
}

void VDQtMainWindow::onFileSaveProcessingSettings() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Processing Settings", QStringLiteral("processing.vdqsettings"),
        "VirtualDubQT Processing Settings (*.vdqsettings);;All Files (*)");
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".vdqsettings");
    if (mVideoDecoder.isOpen()) {
        const VDQtOutputSafetyReport settingsSafety =
            loadedOutputSafety(path, mVideoDecoder, mAudioPlayer);
        if (!settingsSafety.isSafe()) {
            QMessageBox::critical(
                this, "Unsafe Settings Path",
                "The settings path aliases a loaded or script-referenced media source, or "
                "cannot be safely audited. Choose a different path.");
            return;
        }
    }
    QString error;
    if (!VDQtProjectFile::saveProcessingSettings(
            path, captureProcessingState(), &error)) {
        QMessageBox::critical(this, "Save Processing Settings Error", error);
        return;
    }
    statusBar()->showMessage(
        QString("Processing settings saved: %1").arg(QFileInfo(path).fileName()));
}

void VDQtMainWindow::onFileSaveAudio() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, "Save audio", "No video/audio source has been loaded to save.");
        return;
    }

    // Export callbacks pump the Qt event loop. Quiesce playback first so the
    // shared decoder/AviSynth clip cannot be sought or pulled concurrently.
    mPlaybackTimer->stop();
    mAudioPlayer.stop();

    if (!mAudioPlayer.hasAudio()) {
        if (mVideoDecoder.isAvsNative() && mVideoDecoder.getAvsClip() && mVideoDecoder.getAvsVi()) {
            mAudioPlayer.openAvsClip(mVideoDecoder.getAvsClip(), mVideoDecoder.getAvsVi());
        } else {
            QString srcFile = mVideoDecoder.getFilePath();
            mAudioPlayer.openFile(srcFile);
        }
    }

    if (!mAudioPlayer.hasAudio()) {
        QMessageBox::warning(this, "Save audio", "The currently opened file has no audio stream to save.");
        return;
    }

    QFileInfo srcInfo(primarySessionSourcePath());
    QString defaultDir = srcInfo.dir().absolutePath();
    QString baseName = srcInfo.baseName().isEmpty() ? "test" : srcInfo.baseName();
    QString defaultName = baseName + ".wav";

    QString compStr = mAudioPlayer.getAudioCompressionString();
    QString layoutStr = mAudioPlayer.getAudioLayoutString();

    VDSaveAudioDialog dlg(defaultDir, defaultName, compStr, layoutStr, this);
    if (dlg.exec() == QDialog::Accepted) {
        QString outPath = dlg.getSelectedFilePath();
        VDAudioCodecConfig audioCfg = dlg.getAudioConfig();

        const VDQtOutputSafetyReport audioSafety =
            loadedOutputSafety(outPath, mVideoDecoder, mAudioPlayer);
        if (audioSafety.issue == VDQtOutputSafetyIssue::AliasesLoadedSource) {
            QMessageBox::critical(this, "Unsafe Output Path",
                                  "The output file is a currently loaded source. Choose a different path.");
            return;
        }

        const QFileInfo audioTarget(outPath);
        if (audioSafety.issue
            == VDQtOutputSafetyIssue::ExistingDestinationWithIncompleteScriptAudit) {
            QMessageBox::critical(
                this, "Unsafe Script Output Path",
                "An existing destination cannot be replaced while an AviSynth script is loaded, "
                "because scripts can compute source paths dynamically. Choose a new output path.");
            return;
        }
        if (audioTarget.exists() || audioTarget.isSymLink()) {
            const auto answer = QMessageBox::warning(
                this, "Replace Existing Audio File?",
                QString("The destination already exists:\n%1\n\nReplace it after the new file is encoded successfully?")
                    .arg(outPath),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) return;
        }

        if (mPositionControl->hasSelection()
            && !ensureExactFrameRange(QStringLiteral("audio selection range"))) {
            return;
        }

        QTemporaryFile stagedOutput(stagedOutputTemplate(outPath));
        stagedOutput.setAutoRemove(true);
        if (!stagedOutput.open()) {
            QMessageBox::critical(this, "Save Audio Error",
                                  "A temporary output could not be created in the destination directory.");
            return;
        }
        const QString workingOutputPath = stagedOutput.fileName();
        stagedOutput.close();

        QProgressDialog progress("Exporting audio stream...", "Cancel", 0, 100, this);
        progress.setWindowTitle("Audio Export (Full Processing)");
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);

        double fps = mVideoDecoder.getFps();
        if (fps <= 0) fps = 29.97;
        int sampleRate = mAudioPlayer.getSampleRate();
        if (sampleRate <= 0) sampleRate = 48000;

        int64_t startSample = 0;
        int64_t sampleCount = -1;
        if (mPositionControl->hasSelection()) {
            const qint64 requestedStart = mPositionControl->GetSelectionStart();
            const qint64 requestedEndExclusive = mPositionControl->GetSelectionEnd();
            const int exactFrameCount = mVideoDecoder.getFrameCount();
            if (requestedStart < 0 || requestedStart >= exactFrameCount
                || requestedEndExclusive <= requestedStart) {
                QMessageBox::critical(
                    this, "Audio Export Range Error",
                    QString("The requested selection starts at frame %1, but the source contains only %2 frame(s).")
                        .arg(requestedStart)
                        .arg(exactFrameCount));
                return;
            }
            const int startFrame = static_cast<int>(requestedStart);
            const int endFrame = static_cast<int>(std::min<qint64>(
                requestedEndExclusive - 1, exactFrameCount - 1));

            double startSeconds = mVideoDecoder.getFrameTimestampSeconds(startFrame);
            if (!std::isfinite(startSeconds)) startSeconds = startFrame / fps;
            double durationSeconds = (endFrame - startFrame + 1) / fps;
            const double lastTimestamp = mVideoDecoder.getFrameTimestampSeconds(endFrame);
            const double lastDuration = mVideoDecoder.getFrameDurationSeconds(endFrame);
            if (std::isfinite(lastTimestamp) && std::isfinite(lastDuration)
                && lastTimestamp + lastDuration > startSeconds) {
                durationSeconds = lastTimestamp + lastDuration - startSeconds;
            }

            startSample = static_cast<int64_t>(std::llround(startSeconds * sampleRate));
            sampleCount = static_cast<int64_t>(std::llround(durationSeconds * sampleRate));
        }

        bool ok = false;
        QString errorMsg;

        QString audioCodec = audioCfg.codecId.toLower();

        // 1. Direct Uncompressed PCM Export if no resampling/channel change requested and saving as WAV
        if ((audioCodec == "pcm_s16le" || audioCodec == "(uncompressed)" || audioCodec.isEmpty()) &&
            audioCfg.sampleRate == 0 && audioCfg.channels == 0 && outPath.endsWith(".wav", Qt::CaseInsensitive)) {
            ok = mAudioPlayer.exportAudioToFile(workingOutputPath, startSample, sampleCount, [&progress](int cur, int) -> bool {
                progress.setValue(cur);
                progress.setLabelText(QString("Exporting uncompressed PCM audio...\n%1% complete").arg(cur));
                QCoreApplication::processEvents();
                return !progress.wasCanceled();
            });
        } else {
            // 2. Full Processing Encoding with selected codec, bitrate, sample rate, and channels
            QTemporaryDir tempDir;
            if (!tempDir.isValid()) {
                QMessageBox::critical(this, "Save Audio Error", "Unable to create a secure temporary directory.");
                return;
            }
            QString tempWav = tempDir.filePath("audio.wav");
            bool extractOk = mAudioPlayer.exportAudioToFile(tempWav, startSample, sampleCount, [&progress](int cur, int) -> bool {
                int pct = cur / 2; // 0..50%
                progress.setValue(pct);
                progress.setLabelText(QString("Extracting audio stream...\n%1% complete").arg(pct * 2));
                QCoreApplication::processEvents();
                return !progress.wasCanceled();
            });

            if (extractOk) {
                QProcess ffmpeg;
                QStringList args;
                bool isVbr = (audioCfg.rateControlMode.toLower() == "vbr");
                const QString lameExecutable = QStandardPaths::findExecutable("lame");

                if ((audioCodec == "libmp3lame" || audioCodec == "mp3")
                    && !lameExecutable.isEmpty()) {
                    // Use native LAME MP3 encoder
                    QProcess lameProc;
                    QStringList lameArgs;
                    if (isVbr) {
                        int vQuality = std::clamp(audioCfg.vbrQuality, 0, 9);
                        lameArgs << "-V" << QString::number(vQuality);
                    } else {
                        int br = (audioCfg.bitrateKbps > 0) ? audioCfg.bitrateKbps : 192;
                        lameArgs << "-b" << QString::number(br);
                    }
                    if (audioCfg.sampleRate > 0) {
                        lameArgs << "--resample" << QString::number(audioCfg.sampleRate / 1000.0, 'f', 1);
                    }
                    if (audioCfg.channels == 1) {
                        lameArgs << "-m" << "m";
                    } else if (audioCfg.channels == 2) {
                        lameArgs << "-m" << "j";
                    }
                    lameArgs << tempWav << workingOutputPath;

                    // LAME may prompt instead of replacing an existing file.
                    // The randomized path remains safely scoped to the target directory.
                    QFile::remove(workingOutputPath);
                    lameProc.start(lameExecutable, lameArgs);
                    if (lameProc.waitForStarted(3000)) {
                        static const QRegularExpression re("\\(\\s*(\\d+)%\\)");
                        QByteArray errBuf;
                        bool cancelled = false;
                        while (!lameProc.waitForFinished(30)) {
                            if (progress.wasCanceled()) {
                                cancelled = true;
                                lameProc.terminate();
                                if (!lameProc.waitForFinished(1000)) {
                                    lameProc.kill();
                                    lameProc.waitForFinished(3000);
                                }
                                break;
                            }
                            errBuf += lameProc.readAllStandardError();
                            lameProc.readAllStandardOutput();
                            auto match = re.match(QString::fromUtf8(errBuf));
                            if (match.hasMatch()) {
                                int rawPct = match.captured(1).toInt();
                                int overallPct = 50 + std::clamp((rawPct * 50) / 100, 0, 49);
                                progress.setValue(overallPct);
                                progress.setLabelText(QString("Encoding MP3 audio stream...\n%1% complete").arg(rawPct));
                            }
                            if (errBuf.size() > 4096) errBuf = errBuf.right(1024);
                            QCoreApplication::processEvents();
                        }
                        errBuf += lameProc.readAllStandardError();
                        lameProc.readAllStandardOutput();
                        if (!cancelled && lameProc.exitStatus() == QProcess::NormalExit
                            && lameProc.exitCode() == 0 && QFileInfo(workingOutputPath).size() > 0) {
                            ok = true;
                            progress.setValue(100);
                            progress.setLabelText("Encoding MP3 audio stream...\n100% complete");
                        } else {
                            QFile::remove(workingOutputPath);
                            errorMsg = cancelled ? "Audio export was cancelled."
                                                 : QString("LAME MP3 encoding failed:\n%1").arg(QString::fromUtf8(errBuf));
                        }
                        QCoreApplication::processEvents();
                    } else {
                        errorMsg = "Failed to launch LAME MP3 encoder.";
                    }
                } else {
                    args << "-y" << "-i" << tempWav;
                    const VDAudioCodecParams encodeParams =
                        VDQtCodecEngine::audioParamsFromConfig(
                            audioCfg, sampleRate, mAudioPlayer.getChannels());
                    args << VDQtCodecEngine::buildFfmpegAudioEncodeArguments(encodeParams);

                    const int64_t progressSamples = sampleCount > 0
                        ? sampleCount
                        : std::max<int64_t>(1, mAudioPlayer.getTotalSamples() - startSample);
                    const int64_t totalDurationUs = sampleRate > 0
                        ? std::max<int64_t>(1, static_cast<int64_t>(
                              static_cast<long double>(progressSamples) * 1000000.0L / sampleRate))
                        : 1000000LL;
                    args << "-progress" << "pipe:1";
                    args << workingOutputPath;

                    ffmpeg.start("ffmpeg", args);
                    if (ffmpeg.waitForStarted(3000)) {
                        QByteArray outBuf;
                        QByteArray errBuf;
                        bool cancelled = false;
                        while (!ffmpeg.waitForFinished(30)) {
                            if (progress.wasCanceled()) {
                                cancelled = true;
                                ffmpeg.terminate();
                                if (!ffmpeg.waitForFinished(1000)) {
                                    ffmpeg.kill();
                                    ffmpeg.waitForFinished(3000);
                                }
                                break;
                            }
                            outBuf += ffmpeg.readAllStandardOutput();
                            errBuf += ffmpeg.readAllStandardError();
                            if (errBuf.size() > 1024 * 1024)
                                errBuf = errBuf.right(1024 * 1024);
                            int lastNl;
                            while ((lastNl = outBuf.indexOf('\n')) >= 0) {
                                QByteArray line = outBuf.left(lastNl).trimmed();
                                outBuf.remove(0, lastNl + 1);
                                if (line.startsWith("out_time_us=")) {
                                    qint64 outUs = line.mid(12).trimmed().toLongLong();
                                    int encPct = (int)((outUs * 100LL) / totalDurationUs);
                                    int overallPct = 50 + std::clamp((encPct * 50) / 100, 0, 49);
                                    progress.setValue(overallPct);
                                    progress.setLabelText(QString("Encoding %1 audio stream...\n%2% complete").arg(audioCfg.codecName).arg(std::clamp(encPct, 0, 100)));
                                }
                            }
                            QCoreApplication::processEvents();
                        }
                        progress.setValue(100);
                        progress.setLabelText(QString("Encoding %1 audio stream...\n100% complete").arg(audioCfg.codecName));
                        QCoreApplication::processEvents();
                        errBuf += ffmpeg.readAllStandardError();
                        if (!cancelled && ffmpeg.exitStatus() == QProcess::NormalExit && ffmpeg.exitCode() == 0 && QFileInfo(workingOutputPath).size() > 0) {
                            ok = true;
                        } else {
                            QFile::remove(workingOutputPath);
                            errorMsg = cancelled ? "Audio export was cancelled."
                                                 : QString("FFmpeg audio encoding failed:\n%1").arg(QString::fromUtf8(errBuf));
                        }
                    } else {
                        errorMsg = "Failed to launch FFmpeg audio encoder.";
                    }
                }
            } else {
                errorMsg = "Failed to extract uncompressed PCM audio from source.";
            }
        }

        progress.reset();
        progress.close();
        QCoreApplication::processEvents();

        if (ok) {
            const VDQtOutputSafetyReport commitSafety =
                loadedOutputSafety(outPath, mVideoDecoder, mAudioPlayer);
            if (!commitSafety.isSafe()) {
                ok = false;
                errorMsg = "The destination became unsafe while audio was encoding; the existing file was not changed.";
            } else if (!replaceWithStagedFile(workingOutputPath, outPath)) {
                ok = false;
                errorMsg = "The completed audio output could not be committed to its destination.";
            }
        }
        if (!ok) QFile::remove(workingOutputPath);

        if (ok) {
            VDLogWindow::instance(this)->appendLog(QString("[Audio] Successfully exported audio: %1 (%2 bytes, codec: %3)").arg(outPath).arg(QFileInfo(outPath).size()).arg(audioCfg.codecName));
            statusBar()->showMessage(QString("Audio saved to %1").arg(QFileInfo(outPath).fileName()));
            QMessageBox::information(this, "Save Audio", QString("Audio stream saved successfully to:\n%1\n\nCodec: %2\nFile size: %3 KB")
                .arg(outPath)
                .arg(audioCfg.codecName)
                .arg(QFileInfo(outPath).size() / 1024));
        } else {
            VDLogWindow::instance(this)->appendLog(QString("[Audio] Error exporting audio to: %1 (%2)").arg(outPath, errorMsg));
            if (errorMsg.isEmpty()) {
                errorMsg = QString("Failed to export audio to:\n%1\n\nPlease check write permissions or disk space.").arg(outPath);
            }
            QMessageBox::critical(this, "Save Audio Error", errorMsg);
        }
    }
}

void VDQtMainWindow::onFileRunAnalysisPass() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::information(this, "Video Analysis", "No video stream loaded to analyze.");
        return;
    }
    onVideoScanErrors();
}

void VDQtMainWindow::onFileRunScript() {
    QString fileName = QFileDialog::getOpenFileName(
        this, "Run Script", QString(),
        "Video Scripts (*.avs *.AVS *.vpy *.VPY);;VirtualDubQT Projects (*.vdqproject);;VirtualDubQT Job Scripts (*.vdqjobs);;VirtualDubQT Processing Settings (*.vdqsettings);;All Files (*)");
    if (!fileName.isEmpty()) {
        if (fileName.endsWith(".avs", Qt::CaseInsensitive)
            || fileName.endsWith(".vpy", Qt::CaseInsensitive)) {
            openVideoFile(fileName);
        } else if (fileName.endsWith(".vdqproject", Qt::CaseInsensitive)) {
            loadProjectFile(fileName);
        } else if (fileName.endsWith(".vdqjobs", Qt::CaseInsensitive)) {
            QList<VDQtJobState> loadedJobs;
            QString error;
            if (!VDQtProjectFile::loadJobQueue(fileName, &loadedJobs, &error)) {
                QMessageBox::critical(this, "Run Job Script Error", error);
                return;
            }
            if (mVideoJobs.size() + loadedJobs.size() > 1000) {
                QMessageBox::critical(
                    this, "Run Job Script Error",
                    "Loading this script would exceed the 1000-job session limit.");
                return;
            }
            for (const VDQtJobState& loaded : loadedJobs) {
                QueuedVideoJob job;
                job.sourcePaths = loaded.sourcePaths;
                job.options = loaded.options;
                job.processing = loaded.processing;
                mVideoJobs.append(job);
            }
            onFileJobControl();
        } else if (fileName.endsWith(".vdqsettings", Qt::CaseInsensitive)) {
            VDQtProcessingState state;
            QString error;
            if (!VDQtProjectFile::loadProcessingSettings(fileName, &state, &error)) {
                QMessageBox::critical(this, "Run Script Error", error);
                return;
            }
            applyProcessingState(state);
            statusBar()->showMessage(
                QString("Processing script applied: %1").arg(QFileInfo(fileName).fileName()));
        } else {
            QMessageBox::warning(this, "Unsupported Script",
                                 "This script format is not supported. Use AviSynth, VapourSynth, "
                                 "a .vdqproject file, a .vdqjobs file, or a .vdqsettings file.");
        }
    }
}

void VDQtMainWindow::onFileBatchWizard() {
    const QStringList inputs = QFileDialog::getOpenFileNames(
        this, "Batch Wizard - Select Sources", QString(),
        "Video, Media, and Scripts (*.avi *.mp4 *.mkv *.mov *.webm *.flv *.wmv *.nut *.avs *.AVS *.vpy *.VPY);;All Files (*)");
    if (inputs.isEmpty()) return;
    if (mVideoJobs.size() + inputs.size() > 1000) {
        QMessageBox::warning(
            this, "Batch Wizard", "The session job queue is limited to 1000 entries.");
        return;
    }
    const QString outputDirectory = QFileDialog::getExistingDirectory(
        this, "Batch Wizard - Select Output Directory",
        QFileInfo(inputs.first()).absolutePath());
    if (outputDirectory.isEmpty()) return;

    const QStringList formatNames = {
        QStringLiteral("Matroska (*.mkv)"),
        QStringLiteral("MP4 +faststart (*.mp4)"),
        QStringLiteral("QuickTime / MOV (*.mov)"),
        QStringLiteral("WebM (*.webm)"),
        QStringLiteral("AVI (*.avi)"),
        QStringLiteral("NUT (*.nut)")
    };
    bool accepted = false;
    const QString selectedFormat = QInputDialog::getItem(
        this, "Batch Wizard", "Output container:", formatNames, 0, false, &accepted);
    if (!accepted) return;
    const int formatIndex = formatNames.indexOf(selectedFormat);
    const QStringList containerTypes = {
        QStringLiteral("mkv"), QStringLiteral("mp4_faststart"),
        QStringLiteral("mov"), QStringLiteral("webm"),
        QStringLiteral("avi"), QStringLiteral("nut")
    };
    const QStringList extensions = {
        QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("mov"),
        QStringLiteral("webm"), QStringLiteral("avi"), QStringLiteral("nut")
    };
    const QString containerType = containerTypes.at(formatIndex);
    const QString extension = extensions.at(formatIndex);
    const bool fastStart = containerType == QStringLiteral("mp4_faststart");

    QSet<QString> reservedOutputs;
    int added = 0;
    for (const QString& input : inputs) {
        const QFileInfo inputInfo(input);
        QString baseName = inputInfo.completeBaseName();
        if (baseName.isEmpty()) baseName = QStringLiteral("output");
        QString output = QDir(outputDirectory).filePath(
            baseName + QLatin1Char('.') + extension);
        int duplicate = 2;
        while (reservedOutputs.contains(QFileInfo(output).absoluteFilePath())
               || VDQtSourceSafety::pathsReferToSameFile(input, output)) {
            output = QDir(outputDirectory).filePath(
                QString("%1_%2.%3").arg(baseName).arg(duplicate++).arg(extension));
        }
        reservedOutputs.insert(QFileInfo(output).absoluteFilePath());

        QueuedVideoJob job;
        job.sourcePaths = { inputInfo.absoluteFilePath() };
        job.options = currentExportOptions(
            output, containerType, fastStart, true);
        job.options.inputPath = job.sourcePaths.first();
        job.processing = captureProcessingState();
        mVideoJobs.append(job);
        ++added;
    }
    statusBar()->showMessage(
        QString("Queued %1 batch export job(s)").arg(added));
    QMessageBox::information(
        this, "Batch Jobs Queued",
        QString("%1 job(s) were added. Open File > Job control to review or run them.")
            .arg(added));
}

void VDQtMainWindow::onFileJobControl() {
    QDialog dialog(this);
    dialog.setWindowTitle("Job Control");
    dialog.resize(900, 460);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(&dialog);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels(
        { "Status", "Source", "Destination", "Error" });
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table);

    auto statusText = [](QueuedVideoJob::Status status) {
        switch (status) {
        case QueuedVideoJob::Pending: return QStringLiteral("Pending");
        case QueuedVideoJob::Running: return QStringLiteral("Running");
        case QueuedVideoJob::Complete: return QStringLiteral("Complete");
        case QueuedVideoJob::Failed: return QStringLiteral("Failed");
        case QueuedVideoJob::Cancelled: return QStringLiteral("Cancelled");
        }
        return QStringLiteral("Unknown");
    };
    auto refresh = [&]() {
        table->setRowCount(mVideoJobs.size());
        for (int row = 0; row < mVideoJobs.size(); ++row) {
            const QueuedVideoJob& job = mVideoJobs.at(row);
            const QString source = job.sourcePaths.size() > 1
                ? QString("%1 (+%2 segments)")
                      .arg(job.sourcePaths.first())
                      .arg(job.sourcePaths.size() - 1)
                : job.sourcePaths.value(0);
            table->setItem(row, 0, new QTableWidgetItem(statusText(job.status)));
            table->setItem(row, 1, new QTableWidgetItem(source));
            table->setItem(row, 2, new QTableWidgetItem(job.options.outputPath));
            table->setItem(row, 3, new QTableWidgetItem(job.error));
        }
    };
    refresh();

    QHBoxLayout *buttons = new QHBoxLayout();
    auto *runButton = new QPushButton("Run pending", &dialog);
    auto *retryButton = new QPushButton("Retry failed", &dialog);
    auto *loadButton = new QPushButton("Load queue...", &dialog);
    auto *saveButton = new QPushButton("Save queue...", &dialog);
    auto *removeButton = new QPushButton("Remove selected", &dialog);
    auto *clearButton = new QPushButton("Clear completed", &dialog);
    auto *closeButton = new QPushButton("Close", &dialog);
    buttons->addWidget(runButton);
    buttons->addWidget(retryButton);
    buttons->addWidget(loadButton);
    buttons->addWidget(saveButton);
    buttons->addWidget(removeButton);
    buttons->addWidget(clearButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(saveButton, &QPushButton::clicked, &dialog, [&]() {
        QString path = QFileDialog::getSaveFileName(
            &dialog, "Save Job Queue", QString(),
            "VirtualDubQT Job Scripts (*.vdqjobs);;All Files (*)");
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".vdqjobs");
        if (mVideoDecoder.isOpen()
            && !loadedOutputSafety(path, mVideoDecoder, mAudioPlayer).isSafe()) {
            QMessageBox::critical(
                &dialog, "Unsafe Job Script Path",
                "The job script path may alias a source in the current session.");
            return;
        }
        for (const QueuedVideoJob& queued : mVideoJobs) {
            VDQtOutputSafetyReport safety =
                VDQtSourceSafety::evaluateOutputPath(path, queued.sourcePaths);
            for (const QString& source : queued.sourcePaths) {
                if (VDQtSourceSafety::isScriptPath(source)) {
                    safety = VDQtSourceSafety::evaluateOutputPath(
                        path, queued.sourcePaths, source);
                    if (!safety.isSafe()) break;
                }
            }
            if (!safety.isSafe()) {
                QMessageBox::critical(
                    &dialog, "Unsafe Job Script Path",
                    "The job script path aliases, or cannot be distinguished from, "
                    "one of the queued media sources.");
                return;
            }
        }
        QList<VDQtJobState> jobs;
        for (const QueuedVideoJob& queued : mVideoJobs) {
            VDQtJobState job;
            job.sourcePaths = queued.sourcePaths;
            job.options = queued.options;
            job.processing = queued.processing;
            jobs.append(job);
        }
        QString error;
        if (!VDQtProjectFile::saveJobQueue(path, jobs, &error))
            QMessageBox::critical(&dialog, "Save Job Queue Error", error);
    });
    connect(loadButton, &QPushButton::clicked, &dialog, [&]() {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, "Load Job Queue", QString(),
            "VirtualDubQT Job Scripts (*.vdqjobs);;All Files (*)");
        if (path.isEmpty()) return;
        QList<VDQtJobState> loadedJobs;
        QString error;
        if (!VDQtProjectFile::loadJobQueue(path, &loadedJobs, &error)) {
            QMessageBox::critical(&dialog, "Load Job Queue Error", error);
            return;
        }
        if (mVideoJobs.size() + loadedJobs.size() > 1000) {
            QMessageBox::critical(
                &dialog, "Load Job Queue Error",
                "Appending this file would exceed the 1000-job session limit.");
            return;
        }
        for (const VDQtJobState& loaded : loadedJobs) {
            QueuedVideoJob job;
            job.sourcePaths = loaded.sourcePaths;
            job.options = loaded.options;
            job.processing = loaded.processing;
            mVideoJobs.append(job);
        }
        refresh();
    });
    connect(removeButton, &QPushButton::clicked, &dialog, [&]() {
        QList<int> rows;
        for (const QModelIndex& index : table->selectionModel()->selectedRows())
            rows.append(index.row());
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows) {
            if (row >= 0 && row < mVideoJobs.size()
                && mVideoJobs.at(row).status != QueuedVideoJob::Running)
                mVideoJobs.removeAt(row);
        }
        refresh();
    });
    connect(clearButton, &QPushButton::clicked, &dialog, [&]() {
        for (int row = mVideoJobs.size() - 1; row >= 0; --row) {
            if (mVideoJobs.at(row).status == QueuedVideoJob::Complete)
                mVideoJobs.removeAt(row);
        }
        refresh();
    });
    connect(retryButton, &QPushButton::clicked, &dialog, [&]() {
        for (QueuedVideoJob& job : mVideoJobs) {
            if (job.status == QueuedVideoJob::Failed
                || job.status == QueuedVideoJob::Cancelled) {
                job.status = QueuedVideoJob::Pending;
                job.error.clear();
            }
        }
        refresh();
    });

    connect(runButton, &QPushButton::clicked, &dialog, [&]() {
        if (mIsExporting) return;
        mPlaybackTimer->stop();
        mAudioPlayer.stop();
        const VDQtProcessingState originalProcessing = captureProcessingState();
        runButton->setEnabled(false);
        retryButton->setEnabled(false);
        loadButton->setEnabled(false);
        saveButton->setEnabled(false);
        removeButton->setEnabled(false);
        clearButton->setEnabled(false);
        closeButton->setEnabled(false);
        mIsExporting = true;

        for (int row = 0; row < mVideoJobs.size(); ++row) {
            QueuedVideoJob& job = mVideoJobs[row];
            if (job.status != QueuedVideoJob::Pending) continue;
            if (QFileInfo(job.options.outputPath).exists()
                || QFileInfo(job.options.outputPath).isSymLink()) {
                const QMessageBox::StandardButton answer = QMessageBox::warning(
                    &dialog, "Replace Existing Job Output?",
                    QString("The queued destination exists:\n%1\n\nReplace it only after the new output completes?")
                        .arg(job.options.outputPath),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                    QMessageBox::No);
                if (answer == QMessageBox::Cancel) {
                    job.status = QueuedVideoJob::Cancelled;
                    job.error = QStringLiteral("Queue stopped by user.");
                    refresh();
                    break;
                }
                if (answer != QMessageBox::Yes) {
                    job.status = QueuedVideoJob::Cancelled;
                    job.error = QStringLiteral("Existing destination was not replaced.");
                    refresh();
                    continue;
                }
            }

            job.status = QueuedVideoJob::Running;
            job.error.clear();
            refresh();
            table->scrollToItem(table->item(row, 0));
            QCoreApplication::processEvents();

            applyProcessingState(job.processing);
            QTemporaryDir timelineDirectory;
            QString inputPath = job.sourcePaths.value(0);
            QString preparationError;
            if (job.sourcePaths.size() > 1) {
                if (!timelineDirectory.isValid()) {
                    preparationError = QStringLiteral(
                        "Could not create a temporary concatenated timeline.");
                } else {
                    inputPath = timelineDirectory.filePath(
                        QStringLiteral("job.ffconcat"));
                    if (!writeConcatManifest(
                            inputPath, job.sourcePaths, &preparationError))
                        inputPath.clear();
                }
            }

            VDQtVideoDecoder decoder;
            decoder.setDecompressionConfig(
                job.processing.decompression.formatName,
                job.processing.decompression.colorSpace,
                job.processing.decompression.componentRange);
            decoder.setErrorMode(job.processing.decoderErrorMode.errorMode);
            VDQtVideoDecoder avsAudioDecoder;
            VDQtAudioPlayer audioPlayer;
            bool prepared = preparationError.isEmpty() && !inputPath.isEmpty()
                && decoder.openFile(inputPath);
            if (!prepared && preparationError.isEmpty())
                preparationError = decoder.getLastError();
            if (prepared && decoder.isAvsNative()) {
                const AVS_VideoInfo *videoInfo = decoder.getAvsVi();
                if (videoInfo && avs_has_audio(videoInfo)) {
                    avsAudioDecoder.setDecompressionConfig(
                        job.processing.decompression.formatName,
                        job.processing.decompression.colorSpace,
                        job.processing.decompression.componentRange);
                    avsAudioDecoder.setErrorMode(
                        job.processing.decoderErrorMode.errorMode);
                    prepared = avsAudioDecoder.openFile(inputPath)
                        && audioPlayer.openAvsClip(
                            avsAudioDecoder.getAvsClip(), avsAudioDecoder.getAvsVi());
                    if (!prepared)
                        preparationError = QStringLiteral(
                            "Could not open the queued AviSynth audio graph.");
                }
            } else if (prepared) {
                // A missing/undecodable audio stream remains acceptable for
                // direct copy and video-only sources; the exporter probes and
                // enforces full-processing audio requirements independently.
                audioPlayer.openFile(inputPath);
            }

            bool ok = false;
            if (prepared) {
                VDQtVideoExporter exporter;
                VDQtVideoExporter::ExportOptions options = job.options;
                options.inputPath = inputPath;
                ok = exporter.exportVideo(
                    options, &decoder, &audioPlayer, &dialog);
            }
            if (ok) {
                job.status = QueuedVideoJob::Complete;
            } else {
                job.status = QueuedVideoJob::Failed;
                job.error = preparationError.isEmpty()
                    ? QStringLiteral("Export failed or was cancelled.")
                    : preparationError;
            }
            refresh();
            QCoreApplication::processEvents();
        }

        applyProcessingState(originalProcessing);
        mIsExporting = false;
        runButton->setEnabled(true);
        retryButton->setEnabled(true);
        loadButton->setEnabled(true);
        saveButton->setEnabled(true);
        removeButton->setEnabled(true);
        clearButton->setEnabled(true);
        closeButton->setEnabled(true);
        statusBar()->showMessage(QString("Job queue run finished"));
    });

    dialog.exec();
}

void VDQtMainWindow::onFileStartFrameServer() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(
            this, "Frame Server", "Open a video or script before starting a frame server.");
        return;
    }
    if (mFrameServer && mFrameServer->isRunning()) {
        QMessageBox::information(
            this, "Frame Server", "A frame server is already running.");
        return;
    }
    QString baseName = QFileInfo(primarySessionSourcePath()).completeBaseName();
    if (baseName.isEmpty()) baseName = QStringLiteral("virtualdubqt");
    const QString suggested = QDir(QStandardPaths::writableLocation(
        QStandardPaths::TempLocation)).filePath(baseName + QStringLiteral(".nutpipe"));
    const QString pipePath = QFileDialog::getSaveFileName(
        this, "Create Frame-Server FIFO", suggested,
        "NUT frame-server FIFO (*.nutpipe);;All Files (*)");
    if (pipePath.isEmpty()) return;
    const VDQtOutputSafetyReport safety =
        loadedOutputSafety(pipePath, mVideoDecoder, mAudioPlayer);
    if (!safety.isSafe()) {
        QMessageBox::critical(
            this, "Unsafe Frame-Server Path",
            "The FIFO path aliases a loaded or script-referenced source, or cannot be "
            "safely audited. Choose a new, non-existing path.");
        return;
    }
    if (QFileInfo(pipePath).exists() || QFileInfo(pipePath).isSymLink()) {
        QMessageBox::critical(
            this, "Frame-Server Path Exists",
            "Frame serving never replaces an existing filesystem entry. Choose a new path.");
        return;
    }

    VDQtFrameServer::Config config;
    config.sourcePath = mVideoDecoder.getFilePath();
    config.pipePath = pipePath;
    if (mPositionControl->hasSelection()) {
        config.startFrame = std::max(
            0, static_cast<int>(mPositionControl->GetSelectionStart()));
        config.endFrame = std::max(
            config.startFrame,
            static_cast<int>(mPositionControl->GetSelectionEnd() - 1));
    }
    config.decompressionFormat = mDecompressionFormatConfig.formatName;
    config.colorSpace = mDecompressionFormatConfig.colorSpace;
    config.componentRange = mDecompressionFormatConfig.componentRange;
    config.errorMode = mDecoderErrorModeConfig.errorMode;
    config.filters = VDQtFilterSystem::instance().getActiveChain();
    config.preserveEmptyFrames = mPreserveEmptyFrames;
    QString error;
    if (!mFrameServer->start(config, &error)) {
        QMessageBox::critical(this, "Frame Server Error", error);
        return;
    }
    QMessageBox::information(
        this, "Frame Server Started",
        QString("The selected, filtered video is available as an RGB NUT stream at:\n%1\n\n"
                "Open that FIFO in the receiving application. It is removed automatically when "
                "the stream completes or you choose Stop frame server.")
            .arg(QFileInfo(pipePath).absoluteFilePath()));
    statusBar()->showMessage(
        QString("Frame server waiting for a reader: %1").arg(pipePath));
}

void VDQtMainWindow::onFileStopFrameServer() {
    if (!mFrameServer || !mFrameServer->isRunning()) {
        statusBar()->showMessage("No frame server is running");
        return;
    }
    mFrameServer->stop();
    statusBar()->showMessage("Frame server stopped");
}

void VDQtMainWindow::closeEvent(QCloseEvent *event) {
    QMainWindow::closeEvent(event);
}

#include "VDQtVideoExporter.h"

void VDQtMainWindow::onFileSaveAVI() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, "No Video Loaded", "Please open a video or AviSynth script first.");
        return;
    }

    QFileInfo srcInfo(primarySessionSourcePath());
    QString defaultDir = srcInfo.dir().absolutePath();
    QString baseName = srcInfo.completeBaseName();
    if (baseName.isEmpty()) baseName = "output";

    VDSaveVideoDialog dlg(mVideoMode, mAudioMode, defaultDir, baseName, this);
    if (dlg.exec() == QDialog::Accepted) {
        QString savePath = dlg.getSelectedFilePath();
        if (savePath.isEmpty()) return;

        const VDQtOutputSafetyReport videoSafety =
            loadedOutputSafety(savePath, mVideoDecoder, mAudioPlayer);
        if (videoSafety.issue == VDQtOutputSafetyIssue::AliasesLoadedSource) {
            QMessageBox::critical(this, "Unsafe Output Path",
                                  "The output file is a currently loaded source. Choose a different path.");
            return;
        }
        const QFileInfo videoTarget(savePath);
        if (videoSafety.issue
            == VDQtOutputSafetyIssue::ExistingDestinationWithIncompleteScriptAudit) {
            QMessageBox::critical(
                this, "Unsafe Script Output Path",
                "An existing destination cannot be replaced while an AviSynth script is loaded, "
                "because scripts can compute source paths dynamically. Choose a new output path.");
            return;
        }
        if (videoTarget.exists() || videoTarget.isSymLink()) {
            const auto answer = QMessageBox::warning(
                this, "Replace Existing Video File?",
                QString("The destination already exists:\n%1\n\nReplace it after the new file is encoded successfully?")
                    .arg(savePath),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) return;
        }

        VDQtVideoExporter::ExportOptions opts = currentExportOptions(
            savePath, dlg.getSelectedContainerType(), dlg.isFastStartEnabled());
        if (dlg.addToJobQueue()) {
            if (mVideoJobs.size() >= 1000) {
                QMessageBox::warning(
                    this, "Job Queue Full",
                    "The session job queue is limited to 1000 entries.");
                return;
            }
            QueuedVideoJob job;
            job.sourcePaths = mTimelineSources;
            if (job.sourcePaths.isEmpty())
                job.sourcePaths = { mVideoDecoder.getFilePath() };
            job.options = opts;
            job.processing = captureProcessingState();
            mVideoJobs.append(job);
            statusBar()->showMessage(
                QString("Queued video export: %1").arg(QFileInfo(savePath).fileName()));
            QMessageBox::information(
                this, "Job Queued",
                "The export was added to the session queue. Use File > Job control to run it.");
            return;
        }

        // The save dialog may have been open while playback was active.
        mPlaybackTimer->stop();
        mAudioPlayer.stop();

        VDLogWindow::instance(this)->appendLog(QString("[Export] Exporting video to %1...").arg(savePath));

        VDQtVideoExporter exporter;

        mIsExporting = true;
        auto frameCallback = [this, opts](int frameIndex, const QImage &rawFrame, const QImage &filteredFrame) {
            if (opts.videoMode == VideoMode_NormalRecompress) {
                // Normal Recompress: live preview in INPUT pane ONLY
                mInputDisplay->setFrameImage(rawFrame);
                mPositionControl->SetPositionSilent(frameIndex);
                QCoreApplication::processEvents();
            } else if (opts.videoMode == VideoMode_FullProcessing) {
                // Full Processing: live preview in BOTH Input and Output panes
                mInputDisplay->setFrameImage(rawFrame);
                mOutputDisplay->setFrameImage(filteredFrame);
                mPositionControl->SetPositionSilent(frameIndex);
                QCoreApplication::processEvents();
            }
        };

        bool ok = exporter.exportVideo(opts, &mVideoDecoder, &mAudioPlayer, this, frameCallback);
        mIsExporting = false;

        if (ok) {
            VDLogWindow::instance(this)->appendLog(QString("[Export] Video export successfully completed: %1").arg(savePath));
            QMessageBox::information(this, "Export Complete", QString("Video file successfully exported to:\n%1").arg(savePath));
        } else {
            VDLogWindow::instance(this)->appendLog(QString("[Export] Video export failed or was cancelled."));
            QMessageBox::warning(this, "Export Failed", "Video export failed or was cancelled.");
        }
    }
}

void VDQtMainWindow::onFileExportRawVideo() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(
            this, "No Video Loaded",
            "Please open a video or AviSynth script first.");
        return;
    }

    const QFileInfo sourceInfo(primarySessionSourcePath());
    VDRawVideoExportDialog dialog(
        mRawVideoExportConfig,
        sourceInfo.dir().absolutePath(),
        sourceInfo.completeBaseName(),
        this);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString outputPath = dialog.getSelectedFilePath();
    if (outputPath.isEmpty()) return;

    mPlaybackTimer->stop();
    mAudioPlayer.stop();

    const VDQtOutputSafetyReport safety =
        loadedOutputSafety(outputPath, mVideoDecoder, mAudioPlayer);
    if (safety.issue == VDQtOutputSafetyIssue::AliasesLoadedSource) {
        QMessageBox::critical(
            this, "Unsafe Raw Output Path",
            "The raw output aliases a loaded or script-referenced source. Choose a different path.");
        return;
    }
    if (safety.issue
        == VDQtOutputSafetyIssue::ExistingDestinationWithIncompleteScriptAudit) {
        QMessageBox::critical(
            this, "Unsafe Script Output Path",
            "An existing destination cannot be replaced while the loaded script contains "
            "unresolved or dynamic source paths. Choose a new output path.");
        return;
    }
    const QFileInfo outputInfo(outputPath);
    if (outputInfo.exists() || outputInfo.isSymLink()) {
        const auto answer = QMessageBox::warning(
            this, "Replace Existing Raw Video?",
            QString("The destination already exists:\n%1\n\n"
                    "Replace it only after the complete raw stream has rendered successfully?")
                .arg(outputPath),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }

    mRawVideoExportConfig = dialog.getConfig();
    VDQtVideoExporter::RawExportOptions options;
    options.inputPath = mVideoDecoder.getFilePath();
    options.outputPath = outputPath;
    if (mPositionControl->hasSelection()) {
        options.startFrame = std::max(
            0, static_cast<int>(mPositionControl->GetSelectionStart()));
        options.endFrame = std::max(
            options.startFrame,
            static_cast<int>(mPositionControl->GetSelectionEnd() - 1));
    } else {
        options.startFrame = 0;
        options.endFrame = -1;
    }

    if (mFrameRateConfig.sourceMode == 1) {
        options.customFps = mFrameRateConfig.customSourceFps;
    } else if (mFrameRateConfig.convMode == 4) {
        options.customFps = mFrameRateConfig.convertFps;
        options.convertFpsPreserveDuration = true;
    }
    if (mFrameRateConfig.convMode == 1)
        options.decimateFactor = 2;
    else if (mFrameRateConfig.convMode == 2)
        options.decimateFactor = 3;
    else if (mFrameRateConfig.convMode == 3)
        options.decimateFactor = std::max(1, mFrameRateConfig.decimateN);

    options.pixelFormat = mRawVideoExportConfig.pixelFormat;
    options.scanlineAlignment = mRawVideoExportConfig.scanlineAlignment;
    options.swapChromaPlanes = mRawVideoExportConfig.swapChromaPlanes;
    options.bottomUp = mRawVideoExportConfig.bottomUp;
    options.colorMatrix = mRawVideoExportConfig.colorMatrix;
    options.fullRange = mRawVideoExportConfig.fullRange;

    VDLogWindow::instance(this)->appendLog(
        QString("[Export] Rendering raw video to %1 (%2, alignment %3)...")
            .arg(outputPath, options.pixelFormat)
            .arg(options.scanlineAlignment));
    mIsExporting = true;
    VDQtVideoExporter exporter;
    const bool success = exporter.exportRawVideo(
        options, &mVideoDecoder, &mAudioPlayer, this);
    mIsExporting = false;

    if (success) {
        VDLogWindow::instance(this)->appendLog(
            QString("[Export] Raw video completed: %1 (%2 bytes)")
                .arg(outputPath)
                .arg(QFileInfo(outputPath).size()));
        statusBar()->showMessage(
            QString("Raw video saved to %1").arg(QFileInfo(outputPath).fileName()));
        QMessageBox::information(
            this, "Raw Video Export Complete",
            QString("Raw video was exported successfully to:\n%1\n\n"
                    "Pixel format: %2\nFile size: %3 bytes")
                .arg(outputPath, options.pixelFormat.toUpper())
                .arg(QFileInfo(outputPath).size()));
    } else {
        VDLogWindow::instance(this)->appendLog(
            QStringLiteral("[Export] Raw video export failed or was cancelled."));
    }
}

void VDQtMainWindow::onFileExportAnimatedGIF() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(
            this, "No Video Loaded",
            "Please open a video or AviSynth script first.");
        return;
    }

    const QFileInfo sourceInfo(primarySessionSourcePath());
    const QString suggestedPath = sourceInfo.dir().filePath(
        sourceInfo.completeBaseName() + QStringLiteral(".gif"));
    QString outputPath = QFileDialog::getSaveFileName(
        this, "Export Animated GIF", suggestedPath,
        "Animated GIF (*.gif);;All Files (*)");
    if (outputPath.isEmpty()) return;
    if (QFileInfo(outputPath).suffix().isEmpty()) outputPath += QStringLiteral(".gif");

    const VDQtOutputSafetyReport safety =
        loadedOutputSafety(outputPath, mVideoDecoder, mAudioPlayer);
    if (!safety.isSafe()) {
        QMessageBox::critical(
            this, "Unsafe GIF Output Path",
            "The GIF destination aliases a loaded/script source or cannot be audited safely. "
            "Choose another path.");
        return;
    }
    const QFileInfo target(outputPath);
    if (target.exists() || target.isSymLink()) {
        const auto answer = QMessageBox::warning(
            this, "Replace Existing GIF?",
            QString("The destination already exists:\n%1\n\n"
                    "Replace it after the animation has rendered successfully?")
                .arg(outputPath),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }

    mPlaybackTimer->stop();
    mAudioPlayer.stop();
    VDQtVideoExporter::ExportOptions options;
    options.inputPath = mVideoDecoder.getFilePath();
    options.outputPath = outputPath;
    if (mPositionControl->hasSelection()) {
        options.startFrame = std::max(
            0, static_cast<int>(mPositionControl->GetSelectionStart()));
        options.endFrame = std::max(
            options.startFrame,
            static_cast<int>(mPositionControl->GetSelectionEnd() - 1));
    } else {
        options.endFrame = -1;
    }
    if (mFrameRateConfig.sourceMode == 1) {
        options.customFps = mFrameRateConfig.customSourceFps;
    } else if (mFrameRateConfig.convMode == 4) {
        options.customFps = mFrameRateConfig.convertFps;
        options.convertFpsPreserveDuration = true;
    }
    if (mFrameRateConfig.convMode == 1)
        options.decimateFactor = 2;
    else if (mFrameRateConfig.convMode == 2)
        options.decimateFactor = 3;
    else if (mFrameRateConfig.convMode == 3)
        options.decimateFactor = std::max(1, mFrameRateConfig.decimateN);
    options.videoMode = VideoMode_FullProcessing;
    options.audioMode = AudioMode_DirectStreamCopy;
    options.includeAudio = false;
    options.videoCodecOverride = QStringLiteral("gif");
    options.videoPixelFormatOverride = QStringLiteral("rgb8");
    options.containerType = QStringLiteral("gif");
    options.metadata = mTextMetadata;

    mIsExporting = true;
    VDQtVideoExporter exporter;
    const bool success = exporter.exportVideo(
        options, &mVideoDecoder, nullptr, this,
        [this](int frameIndex, const QImage& raw, const QImage& filtered) {
            mInputDisplay->setFrameImage(raw);
            mOutputDisplay->setFrameImage(filtered);
            mPositionControl->SetPositionSilent(frameIndex);
        });
    mIsExporting = false;
    if (success) {
        statusBar()->showMessage(
            QString("Animated GIF saved to %1").arg(QFileInfo(outputPath).fileName()));
        QMessageBox::information(
            this, "GIF Export Complete",
            QString("Animated GIF exported successfully to:\n%1").arg(outputPath));
    } else {
        VDLogWindow::instance(this)->appendLog(
            QStringLiteral("[Export] Animated GIF export failed or was cancelled."));
    }
}

void VDQtMainWindow::onFileSaveImageSequence() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, "No Video Loaded", "Please open a video or AviSynth script first.");
        return;
    }

    mPlaybackTimer->stop();
    mAudioPlayer.stop();

    int totalFrames = mVideoDecoder.getFrameCount();
    if (!mVideoDecoder.isFrameCountExact()) {
        QProgressDialog indexingProgress(
            "Indexing source frames for image export...", "Cancel",
            0, totalFrames > 0 ? totalFrames : 0, this);
        indexingProgress.setWindowModality(Qt::WindowModal);
        indexingProgress.setMinimumDuration(0);
        const int initialEstimate = totalFrames;
        const VDQtVideoDecoder::VDScanResult scan = mVideoDecoder.scanVideoStream(
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
                QCoreApplication::processEvents();
                return !indexingProgress.wasCanceled();
            });
        indexingProgress.close();
        if (scan.cancelled) return;
        if (!scan.errorMessage.isEmpty()) {
            QMessageBox::critical(this, "Image Export Error", scan.errorMessage);
            return;
        }
        totalFrames = mVideoDecoder.getFrameCount();
        if (totalFrames > 0) mPositionControl->SetRange(0, totalFrames - 1);
    }
    if (totalFrames <= 0) {
        QMessageBox::warning(this, "Image Export Error", "The source has no decodable video frames.");
        return;
    }

    int startFrame = 0;
    int endFrame = totalFrames - 1;
    if (mPositionControl->hasSelection()) {
        const qint64 requestedStart = mPositionControl->GetSelectionStart();
        const qint64 requestedEndExclusive = mPositionControl->GetSelectionEnd();
        if (requestedStart < 0 || requestedStart >= totalFrames
            || requestedEndExclusive <= requestedStart) {
            QMessageBox::critical(
                this, "Image Export Range Error",
                QString("The requested selection starts at frame %1, but the source contains only %2 frame(s).")
                    .arg(requestedStart)
                    .arg(totalFrames));
            return;
        }
        startFrame = static_cast<int>(requestedStart);
        endFrame = static_cast<int>(std::min<qint64>(requestedEndExclusive - 1,
                                                     totalFrames - 1));
    }

    const QFileInfo sourceInfo(primarySessionSourcePath());
    QString defaultFile = sourceInfo.dir().filePath(
        sourceInfo.completeBaseName() + QStringLiteral("_frame.png"));
    QString filter = "PNG Images (*.png);;Windows Bitmap (*.bmp);;JPEG Images (*.jpg *.jpeg);;TIFF Images (*.tif *.tiff);;Targa Images (*.tga)";
    QString savePath = QFileDialog::getSaveFileName(this, "Save Image Sequence (Select Base Name and Format)", defaultFile, filter);
    if (savePath.isEmpty()) return;

    QFileInfo fi(savePath);
    QString dir = fi.absolutePath();
    QString baseName = fi.baseName();
    QString ext = fi.suffix().isEmpty() ? "png" : fi.suffix();

    const int sourceFramesToExport = endFrame - startFrame + 1;
    const VDFilterTimingInfo filterTiming = VDQtFilterSystem::instance().getTimingInfo();
    if (!filterTiming.sequenceSupported || filterTiming.outputFramesPerInput <= 0
        || sourceFramesToExport > std::numeric_limits<int>::max() / filterTiming.outputFramesPerInput) {
        QMessageBox::critical(this, "Image Export Error",
                              "The temporal filter chain produces an unsupported sequence size.");
        return;
    }
    const int framesToExport = sourceFramesToExport * filterTiming.outputFramesPerInput;
    QStringList targetPaths;
    targetPaths.reserve(framesToExport);
    QStringList existingTargets;
    for (int f = startFrame; f <= endFrame; ++f) {
        for (int phase = 0; phase < filterTiming.outputFramesPerInput; ++phase) {
            const qint64 outputNumber = static_cast<qint64>(f)
                                      * filterTiming.outputFramesPerInput + phase;
            const QString targetPath = QString("%1/%2_%3.%4")
                .arg(dir)
                .arg(baseName)
                .arg(outputNumber, 5, 10, QChar('0'))
                .arg(ext);
            const VDQtOutputSafetyReport imageSafety =
                loadedOutputSafety(targetPath, mVideoDecoder, mAudioPlayer);
            if (imageSafety.issue == VDQtOutputSafetyIssue::AliasesLoadedSource) {
                QMessageBox::critical(this, "Unsafe Image Sequence Path",
                                      QString("Generated image path aliases a loaded source:\n%1").arg(targetPath));
                return;
            }
            const QFileInfo targetInfo(targetPath);
            if (imageSafety.issue
                == VDQtOutputSafetyIssue::ExistingDestinationWithIncompleteScriptAudit) {
                QMessageBox::critical(
                    this, "Unsafe Script Image Path",
                    QString("An existing generated image cannot be replaced while an AviSynth script is loaded:\n%1\n"
                            "Choose a new image-sequence base name.").arg(targetPath));
                return;
            }
            if (targetInfo.exists() || targetInfo.isSymLink())
                existingTargets.append(targetPath);
            targetPaths.append(targetPath);
        }
    }

    if (!existingTargets.isEmpty()) {
        const auto answer = QMessageBox::warning(
            this, "Replace Existing Image Sequence?",
            QString("%1 generated image file(s) already exist. They will be replaced only after "
                    "the complete sequence has rendered successfully. Continue?")
                .arg(existingTargets.size()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }

    QTemporaryDir stagingDirectory(QDir(dir).filePath(QStringLiteral(".virtualdub-images-XXXXXX")));
    if (!stagingDirectory.isValid()) {
        QMessageBox::critical(this, "Image Export Error",
                              "A staging directory could not be created beside the destination sequence.");
        return;
    }

    QProgressDialog progress("Exporting Image Sequence...", "Cancel", 0, framesToExport, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QElapsedTimer timer;
    timer.start();

    int exportedCount = 0;
    QStringList stagedPaths;
    stagedPaths.reserve(framesToExport);
    bool renderFailed = false;
    for (int f = startFrame; f <= endFrame; ++f) {
        if (progress.wasCanceled()) break;

        QImage rawFrame = mVideoDecoder.getFrameImage(f);
        if (rawFrame.isNull()) {
            renderFailed = true;
            VDLogWindow::instance(this)->appendLog(
                QString("[Export] Failed to decode image-sequence frame %1").arg(f));
            break;
        }

        QList<QImage> filteredFrames;
        if (!VDQtFilterSystem::instance().processFrameSequence(rawFrame, filteredFrames)
            || filteredFrames.size() != filterTiming.outputFramesPerInput) {
            renderFailed = true;
            VDLogWindow::instance(this)->appendLog(
                QString("[Export] Temporal filter chain failed on image frame %1").arg(f));
            break;
        }

        for (const QImage& filtered : filteredFrames) {
            if (progress.wasCanceled()) break;
            const QString stagedPath = stagingDirectory.filePath(
                QString("frame_%1.%2").arg(exportedCount, 8, 10, QChar('0')).arg(ext));
            if (filtered.isNull() || !filtered.save(stagedPath)) {
                renderFailed = true;
                VDLogWindow::instance(this)->appendLog(
                    QString("[Export] Failed to stage image frame %1").arg(f));
                break;
            }
            stagedPaths.append(stagedPath);
            exportedCount++;

            progress.setValue(exportedCount);
            const double elapsedSec = timer.elapsed() / 1000.0;
            const double currentFps = elapsedSec > 0 ? exportedCount / elapsedSec : 0;
            progress.setLabelText(QString("Exporting frame %1 of %2 (%3%)\nSpeed: %4 fps")
                .arg(exportedCount)
                .arg(framesToExport)
                .arg(static_cast<int>(100.0 * exportedCount / framesToExport))
                .arg(currentFps, 0, 'f', 1));
            QApplication::processEvents();
        }
        if (progress.wasCanceled() || renderFailed) break;

    }

    if (progress.wasCanceled() || renderFailed || exportedCount != framesToExport) {
        if (renderFailed) {
            QMessageBox::critical(this, "Image Export Error",
                                  "The sequence could not be rendered completely. Existing files were not changed.");
        }
        return;
    }

    // Recheck aliases/collisions after rendering to close the user-visible
    // race window before any destination is changed.
    for (const QString& targetPath : targetPaths) {
        const VDQtOutputSafetyReport imageSafety =
            loadedOutputSafety(targetPath, mVideoDecoder, mAudioPlayer);
        if (imageSafety.issue == VDQtOutputSafetyIssue::AliasesLoadedSource) {
            QMessageBox::critical(this, "Unsafe Image Sequence Path",
                                  "A generated image path became an alias of a loaded source. Existing files were not changed.");
            return;
        }
        const QFileInfo targetInfo(targetPath);
        if ((targetInfo.exists() || targetInfo.isSymLink())
            && !existingTargets.contains(targetPath)) {
            QMessageBox::critical(this, "Image Export Collision",
                                  QString("A destination appeared while the sequence was rendering:\n%1\n"
                                          "Existing files were not changed.").arg(targetPath));
            return;
        }
        if (imageSafety.issue
            == VDQtOutputSafetyIssue::ExistingDestinationWithIncompleteScriptAudit) {
            QMessageBox::critical(
                this, "Unsafe Script Image Path",
                QString("A generated destination appeared while the script sequence was rendering:\n%1\n"
                        "No existing files were changed.").arg(targetPath));
            return;
        }
    }

    const QString backupDirectory = stagingDirectory.filePath(QStringLiteral("backups"));
    if (!QDir().mkpath(backupDirectory)) {
        QMessageBox::critical(this, "Image Export Error",
                              "Could not prepare transactional backups. Existing files were not changed.");
        return;
    }

    QVector<QPair<QString, QString>> backups;
    QStringList committedTargets;
    auto rollbackCommit = [&]() {
        bool restored = true;
        for (auto it = committedTargets.crbegin(); it != committedTargets.crend(); ++it)
            restored = QFile::remove(*it) && restored;
        for (auto it = backups.crbegin(); it != backups.crend(); ++it)
            restored = QFile::rename(it->second, it->first) && restored;
        return restored;
    };

    for (int i = 0; i < targetPaths.size(); ++i) {
        const QString& targetPath = targetPaths.at(i);
        const QFileInfo targetInfo(targetPath);
        if (!targetInfo.exists() && !targetInfo.isSymLink()) continue;

        QFile::setPermissions(stagedPaths.at(i), targetInfo.permissions());
        const QString backupPath = QDir(backupDirectory).filePath(QString::number(i));
        if (!QFile::rename(targetPath, backupPath)) {
            const bool restored = rollbackCommit();
            QMessageBox::critical(this, "Image Export Error",
                                  restored
                                      ? "Could not back up an existing image. No sequence files were changed."
                                      : "Could not back up an existing image, and automatic rollback was incomplete.");
            return;
        }
        backups.append(qMakePair(targetPath, backupPath));
    }

    for (int i = 0; i < targetPaths.size(); ++i) {
        if (!QFile::rename(stagedPaths.at(i), targetPaths.at(i))) {
            const bool restored = rollbackCommit();
            QMessageBox::critical(this, "Image Export Error",
                                  restored
                                      ? "Could not commit the completed sequence. Previous files were restored."
                                      : "Could not commit the completed sequence, and automatic rollback was incomplete.");
            return;
        }
        committedTargets.append(targetPaths.at(i));
    }

    VDLogWindow::instance(this)->appendLog(QString("[Export] Image sequence successfully exported: %1 frames to %2").arg(exportedCount).arg(dir));
    QMessageBox::information(this, "Export Complete", QString("Successfully exported %1 frames to:\n%2").arg(exportedCount).arg(dir));
}

void VDQtMainWindow::onFileQuit() {
    qApp->quit();
}

void VDQtMainWindow::onEditSetSelectionStart() {
    if (!ensureExactFrameRange(QStringLiteral("selection range"))) return;
    mPositionControl->SetSelection(mPositionControl->GetPosition(), mPositionControl->GetRangeEnd() + 1);
}

void VDQtMainWindow::onEditSetSelectionEnd() {
    qint64 start, end;
    mPositionControl->GetSelection(start, end);
    mPositionControl->SetSelection(start, mPositionControl->GetPosition());
}

void VDQtMainWindow::onEditSelectAll() {
    if (!ensureExactFrameRange(QStringLiteral("complete source range"))) return;
    mPositionControl->SetSelection(mPositionControl->GetRangeBegin(), mPositionControl->GetRangeEnd() + 1);
}

bool VDQtMainWindow::ensureExactFrameRange(const QString& operationLabel) {
    if (!mVideoDecoder.isOpen() || mVideoDecoder.isFrameCountExact())
        return true;

    mPlaybackTimer->stop();
    mAudioPlayer.stop();
    const int estimate = mVideoDecoder.getFrameCount();
    QProgressDialog progress(QString("Indexing the %1...").arg(operationLabel), "Cancel",
                             0, estimate > 0 ? estimate : 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    const VDQtVideoDecoder::VDScanResult scan = mVideoDecoder.scanVideoStream(
        [&progress, estimate, operationLabel](int current, int reportedTotal) {
            if (estimate > 0) {
                const int maximum = std::max(estimate, reportedTotal);
                progress.setRange(0, maximum);
                progress.setValue(std::min(current, maximum));
            } else {
                progress.setRange(0, 0);
                progress.setLabelText(
                    QString("Indexing the %1... %2 decoded").arg(operationLabel).arg(current));
            }
            QCoreApplication::processEvents();
            return !progress.wasCanceled();
        });
    progress.close();
    if (scan.cancelled) return false;
    if (!scan.errorMessage.isEmpty()) {
        QMessageBox::critical(this, "Frame Range Error", scan.errorMessage);
        return false;
    }
    const int exactCount = mVideoDecoder.getFrameCount();
    if (exactCount <= 0) {
        QMessageBox::warning(this, "Frame Range", "The source has no decodable video frames.");
        return false;
    }
    mPositionControl->SetRange(0, exactCount - 1);
    return true;
}

void VDQtMainWindow::autoFitWindowToVideo() {
    if (!mVideoDecoder.isOpen()) return;

    int videoW = mVideoDecoder.getWidth();
    int videoH = mVideoDecoder.getHeight();
    if (videoW <= 0 || videoH <= 0) return;

    // Determine visible panes
    bool showInput = mInputDisplay->isVisible();
    bool showOutput = mOutputDisplay->isVisible();
    if (!showInput && !showOutput) {
        showInput = true;
        mInputDisplay->setVisible(true);
    }
    int numPanes = (showInput && showOutput) ? 2 : 1;

    // Multi-Monitor Aware Target Screen Resolution:
    // 1. Check windowHandle screen (Wayland/X11 assigned surface)
    // 2. Check screen containing center of current window
    // 3. Check widget screen
    // 4. Check screen at cursor position
    // 5. Fallback to primary screen
    QScreen *targetScreen = nullptr;
    if (windowHandle() && windowHandle()->screen()) {
        targetScreen = windowHandle()->screen();
    }
    if (!targetScreen) {
        targetScreen = QGuiApplication::screenAt(geometry().center());
    }
    if (!targetScreen) {
        targetScreen = screen();
    }
    if (!targetScreen) {
        targetScreen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }

    QRect screenGeom = targetScreen ? targetScreen->availableGeometry() : QRect(0, 0, 1920, 1080);
    if (screenGeom.isEmpty()) {
        screenGeom = targetScreen ? targetScreen->geometry() : QRect(0, 0, 1920, 1080);
    }

    // Calculate non-video chrome height and width
    int menuH = menuBar() ? menuBar()->sizeHint().height() : 25;
    int posCtrlH = mPositionControl ? mPositionControl->sizeHint().height() : 110;
    int statusH = statusBar() ? statusBar()->sizeHint().height() : 22;
    int layoutMarginsH = 8;  // mainLayout top + bottom margins (4+4)
    int layoutSpacingH = 8;  // spacing between splitter and controls
    int verticalChrome = menuH + posCtrlH + statusH + layoutMarginsH + layoutSpacingH;

    int horizontalChrome = 8; // mainLayout left + right margins (4+4)
    if (numPanes == 2 && mVideoSplitter) {
        horizontalChrome += mVideoSplitter->handleWidth() + 4;
    }

    // 1. Get current top-left position of the window
    QPoint curTopLeft = pos();
    if (windowHandle()) {
        QPoint winPos = windowHandle()->position();
        if (!winPos.isNull()) curTopLeft = winPos;
    }

    // 2. Compute remaining space to the right and bottom from current top-left corner on THIS monitor
    int remainingRight = screenGeom.right() - curTopLeft.x();
    int remainingBottom = screenGeom.bottom() - curTopLeft.y();

    // If top-left is invalid or off-screen, fallback to monitor dimensions
    if (curTopLeft.x() < screenGeom.left() || curTopLeft.x() >= screenGeom.right() - 200 || remainingRight < 300) {
        remainingRight = screenGeom.width();
    }
    if (curTopLeft.y() < screenGeom.top() || curTopLeft.y() >= screenGeom.bottom() - 150 || remainingBottom < 200) {
        remainingBottom = screenGeom.height();
    }

    // 3. Allocate comfortable safety margins from the monitor borders (50px right margin, 40px bottom margin)
    int maxWindowW = std::max(320, remainingRight - 50);
    int maxWindowH = std::max(240, remainingBottom - 40);

    // Also clamp against 85% of total monitor dimensions for optimal visual comfort
    maxWindowW = std::min(maxWindowW, static_cast<int>(screenGeom.width() * 0.85));
    maxWindowH = std::min(maxWindowH, static_cast<int>(screenGeom.height() * 0.85));

    int maxVideoAreaW = std::max(128, maxWindowW - horizontalChrome);
    int maxVideoAreaH = std::max(96, maxWindowH - verticalChrome);

    // 4. Calculate optimal scale factor to fit exactly within this remaining space
    double scaleW = static_cast<double>(maxVideoAreaW) / (numPanes * videoW);
    double scaleH = static_cast<double>(maxVideoAreaH) / videoH;
    double scale = std::min(1.0, std::min(scaleW, scaleH));

    int paneW = std::max(64, static_cast<int>(std::round(videoW * scale)));
    int paneH = std::max(48, static_cast<int>(std::round(videoH * scale)));

    int targetW = (numPanes * paneW) + horizontalChrome;
    int targetH = paneH + verticalChrome;

    // Hard ceiling: target width and height must never cross the monitor boundary
    targetW = std::min(targetW, remainingRight - 30);
    targetH = std::min(targetH, remainingBottom - 30);

    // Apply splitter sizes
    if (numPanes == 2) {
        mVideoSplitter->setSizes({paneW, paneW});
    } else if (showInput) {
        mVideoSplitter->setSizes({paneW, 0});
    } else {
        mVideoSplitter->setSizes({0, paneW});
    }

    // Set screen on window handle (Wayland & X11 multi-monitor association)
    if (windowHandle() && targetScreen) {
        windowHandle()->setScreen(targetScreen);
    }

    // Resize window
    resize(targetW, targetH);

    // Compute target coordinates strictly enclosed inside this monitor's bounds:
    // [screenGeom.left(), screenGeom.left() + screenGeom.width()]
    int targetX = screenGeom.left() + (screenGeom.width() - targetW) / 2;
    int targetY = screenGeom.top() + (screenGeom.height() - targetH) / 2;

    int minX = screenGeom.left();
    int maxX = std::max(screenGeom.left(), screenGeom.right() - targetW + 1);
    int minY = screenGeom.top();
    int maxY = std::max(screenGeom.top(), screenGeom.bottom() - targetH + 1);

    targetX = std::clamp(targetX, minX, maxX);
    targetY = std::clamp(targetY, minY, maxY);

    move(targetX, targetY);

    // On Wayland with KDE Plasma / KWin, invoke compositor-side frame geometry adjustment
    if (QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive) ||
        !qgetenv("WAYLAND_DISPLAY").isEmpty()) {
        QString script = QString(
            "(function() {\n"
            "    var list = workspace.windowList ? workspace.windowList() : (workspace.windows ? workspace.windows() : []);\n"
            "    for (var i = 0; i < list.length; ++i) {\n"
            "        var w = list[i];\n"
            "        if (w && w.caption && w.caption.indexOf('VirtualDub') !== -1) {\n"
            "            var out = w.output ? w.output.geometry : (workspace.activeScreen ? workspace.activeScreen.geometry : null);\n"
            "            if (out) {\n"
            "                var tW = Math.min(%1, out.width - 40);\n"
            "                var tH = Math.min(%2, out.height - 40);\n"
            "                var newX = out.x + Math.max(0, Math.floor((out.width - tW) / 2));\n"
            "                var newY = out.y + Math.max(0, Math.floor((out.height - tH) / 2));\n"
            "                if (newX + tW > out.x + out.width) newX = out.x + out.width - tW;\n"
            "                if (newX < out.x) newX = out.x;\n"
            "                if (newY + tH > out.y + out.height) newY = out.y + out.height - tH;\n"
            "                if (newY < out.y) newY = out.y;\n"
            "                w.frameGeometry = { x: newX, y: newY, width: tW, height: tH };\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "})();\n"
        ).arg(targetW).arg(targetH);

        QTemporaryFile tempFile;
        if (tempFile.open()) {
            tempFile.write(script.toUtf8());
            tempFile.flush();
            QString scriptPath = tempFile.fileName();

            QDBusInterface kwinScripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
            if (kwinScripting.isValid()) {
                QDBusReply<int> reply = kwinScripting.call("loadDeclarativeScript", scriptPath);
                if (reply.isValid()) {
                    kwinScripting.call("start");
                }
            }
        }
    }

    QCoreApplication::processEvents();
}

void VDQtMainWindow::onViewDualView() {
    mInputDisplay->setVisible(true);
    mOutputDisplay->setVisible(true);
    autoFitWindowToVideo();
}

void VDQtMainWindow::onViewInputOnly() {
    mInputDisplay->setVisible(true);
    mOutputDisplay->setVisible(false);
    autoFitWindowToVideo();
}

void VDQtMainWindow::onViewOutputOnly() {
    mInputDisplay->setVisible(false);
    mOutputDisplay->setVisible(true);
    autoFitWindowToVideo();
}

void VDQtMainWindow::onViewLogWindow() {
    VDLogWindow::instance(this)->show();
    VDLogWindow::instance(this)->raise();
}

void VDQtMainWindow::onVideoModeDirectStream() {
    mVideoMode = VideoMode_DirectStreamCopy;
    actVideoDirectStream->setChecked(true);
    statusBar()->showMessage("Video Mode: Direct Stream Copy (Bypasses video codecs & filters)");
    VDLogWindow::instance(this)->appendLog("[Video] Mode set to Direct Stream Copy");
}

void VDQtMainWindow::onVideoModeFastRecompress() {
    mVideoMode = VideoMode_FastRecompress;
    actVideoFastRecompress->setChecked(true);
    statusBar()->showMessage(
        "Video Mode: Fast Recompress (Native pixel formats; video filters bypassed)");
    VDLogWindow::instance(this)->appendLog("[Video] Mode set to Fast Recompress");
}

void VDQtMainWindow::onVideoModeNormalRecompress() {
    mVideoMode = VideoMode_NormalRecompress;
    actVideoNormalRecompress->setChecked(true);
    statusBar()->showMessage("Video Mode: Normal Recompress (Bypasses video filters with configured RGB conversion)");
    VDLogWindow::instance(this)->appendLog("[Video] Mode set to Normal Recompress");
}

void VDQtMainWindow::onVideoModeFullProcessing() {
    mVideoMode = VideoMode_FullProcessing;
    actVideoFullProcessing->setChecked(true);
    statusBar()->showMessage("Video Mode: Full Processing Mode (All video filters active)");
    VDLogWindow::instance(this)->appendLog("[Video] Mode set to Full Processing Mode");
}

void VDQtMainWindow::onVideoDecodeFormat() {
    QString decoderName = "AVIFile/Avisynth input driver (internal)";
    QString actualFormat = "YUV420";
    if (mVideoDecoder.isOpen()) {
        decoderName = "FFmpeg / AviSynth Video Decoder (internal)";
        actualFormat = mVideoDecoder.getPixFormat();
        if (actualFormat.isEmpty()) actualFormat = "YUV420";
    }

    VDDecodeFormatDialog dlg(decoderName, actualFormat, mDecompressionFormatConfig, this);
    if (dlg.exec() == QDialog::Accepted) {
        mDecompressionFormatConfig = dlg.getConfig();
        mVideoDecoder.setDecompressionConfig(
            mDecompressionFormatConfig.formatName,
            mDecompressionFormatConfig.colorSpace,
            mDecompressionFormatConfig.componentRange
        );
        if (mVideoDecoder.isOpen()) {
            QMetaObject::invokeMethod(
                mFrameDecodeWorker,
                [this]() {
                    mFrameDecodeWorker->setDecompressionConfig(
                        mDecompressionFormatConfig.formatName,
                        mDecompressionFormatConfig.colorSpace,
                        mDecompressionFormatConfig.componentRange);
                },
                Qt::BlockingQueuedConnection);
            updateFrameDisplay(mPositionControl->GetPosition());
        }
        VDLogWindow::instance(this)->appendLog(QString("[Video] Session decompression format selected: %1 (Color space: %2, Component range: %3)")
            .arg(mDecompressionFormatConfig.formatName)
            .arg(mDecompressionFormatConfig.colorSpace == 1 ? "Rec. 601" : (mDecompressionFormatConfig.colorSpace == 2 ? "Rec. 709" : "No change"))
            .arg(mDecompressionFormatConfig.componentRange == 1 ? "Limited" : (mDecompressionFormatConfig.componentRange == 2 ? "Full" : "No change")));
        statusBar()->showMessage(QString("Decompression Format: %1").arg(mDecompressionFormatConfig.formatName));
    }
}

void VDQtMainWindow::onVideoSelectRange() {
    QMessageBox::information(this, "Select Range", QString("Current selection range:\nStart: Frame %1\nEnd: Frame %2 (exclusive)\nTotal frames: %3")
        .arg(mPositionControl->GetSelectionStart())
        .arg(mPositionControl->GetSelectionEnd())
        .arg(std::max<qint64>(0, mPositionControl->GetSelectionEnd() - mPositionControl->GetSelectionStart())));
}

void VDQtMainWindow::onVideoCopySourceFrame() {
    if (mVideoDecoder.isOpen()) {
        QImage frame = mVideoDecoder.getFrameImage(mPositionControl->GetPosition());
        if (!frame.isNull()) {
            QApplication::clipboard()->setImage(frame);
            statusBar()->showMessage(QString("Source frame %1 copied to clipboard").arg(mPositionControl->GetPosition()));
        }
    }
}

void VDQtMainWindow::onVideoCopyOutputFrame() {
    if (mVideoDecoder.isOpen()) {
        QImage frame = mVideoDecoder.getFrameImage(mPositionControl->GetPosition());
        if (!frame.isNull()) {
            QImage processed = VDQtFilterSystem::instance().processFrame(frame);
            QApplication::clipboard()->setImage(processed);
            statusBar()->showMessage(QString("Output frame %1 copied to clipboard").arg(mPositionControl->GetPosition()));
        }
    }
}

void VDQtMainWindow::onVideoCopySourceFrameNum() {
    int frame = mPositionControl->GetPosition();
    QApplication::clipboard()->setText(QString::number(frame));
    statusBar()->showMessage(QString("Source frame number %1 copied to clipboard").arg(frame));
}

void VDQtMainWindow::onVideoCopyOutputFrameNum() {
    int frame = mPositionControl->GetPosition();
    QApplication::clipboard()->setText(QString::number(frame));
    statusBar()->showMessage(QString("Output frame number %1 copied to clipboard").arg(frame));
}

void VDQtMainWindow::onVideoScanErrors() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::information(this, "Scan video stream", "No video source has been loaded to scan.");
        return;
    }

    int totalFrames = mVideoDecoder.getFrameCount();
    QProgressDialog progress("Scanning for unreadable frames...", "Cancel", 0, totalFrames, this);
    progress.setWindowTitle("Frame scan");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    VDLogWindow::instance(this)->appendLog(QString("[Scan] Beginning video stream scan (%1 frames)...").arg(totalFrames));

    VDQtVideoDecoder::VDScanResult result = mVideoDecoder.scanVideoStream([&progress](int currentFrame, int total) -> bool {
        progress.setValue(currentFrame);
        progress.setLabelText(QString("Scanning for unreadable frames...\nFrame %1 of %2").arg(currentFrame).arg(total));
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
    });

    progress.close();

    if (result.cancelled) {
        VDLogWindow::instance(this)->appendLog("[Scan] Video stream scan was cancelled by user.");
        statusBar()->showMessage("Video stream scan cancelled.");
        QMessageBox::information(this, "Frame scan", "Scan cancelled by user.");
        return;
    }

    int undecodable = result.maskedFrames - result.badFrames;
    if (undecodable < 0) undecodable = 0;

    QString statusMsg = QString("%1 frames masked (%2 frames bad, %3 frames good but undecodable)")
        .arg(result.maskedFrames)
        .arg(result.badFrames)
        .arg(undecodable);

    statusBar()->showMessage(statusMsg);
    VDLogWindow::instance(this)->appendLog(QString("[Scan] Complete: %1 (Keyframes: %2, Total frames: %3)")
        .arg(statusMsg)
        .arg(result.keyFrames)
        .arg(result.totalFrames));

    QString report = QString("Scan complete.\n\n%1 frames masked (%2 frames bad, %3 frames good but undecodable)\n\nKeyframes detected: %4\nTotal frames scanned: %5\nStatus: %6")
        .arg(result.maskedFrames)
        .arg(result.badFrames)
        .arg(undecodable)
        .arg(result.keyFrames)
        .arg(result.totalFrames)
        .arg(result.badFrames == 0 ? "Stream is clean (0 errors detected)." : "Stream contains damaged frames.");

    QMessageBox::information(this, "Scan video stream for errors", report);
}

void VDQtMainWindow::onVideoErrorMode() {
    VDDecoderErrorModeDialog dlg(mDecoderErrorModeConfig, this);
    if (dlg.exec() == QDialog::Accepted) {
        mDecoderErrorModeConfig = dlg.getConfig();
        mVideoDecoder.setErrorMode(mDecoderErrorModeConfig.errorMode);
        QMetaObject::invokeMethod(
            mFrameDecodeWorker,
            [this]() { mFrameDecodeWorker->setErrorMode(mDecoderErrorModeConfig.errorMode); },
            Qt::BlockingQueuedConnection);
        QString modeStr = "Report all errors";
        if (mDecoderErrorModeConfig.errorMode == 1) modeStr = "Conceal errors and resume decoding at next keyframe";
        else if (mDecoderErrorModeConfig.errorMode == 2) modeStr = "Decode even if the result may be garbled";

        VDLogWindow::instance(this)->appendLog(QString("[Video] Session decoder error mode set: %1")
            .arg(modeStr));
        statusBar()->showMessage(QString("Decoder error mode: %1").arg(modeStr));
    }
}

#include "VDQtCodecEngine.h"

void VDQtMainWindow::onVideoCompression() {
    VDVideoCompressionDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        VDVideoCodecParams params = VDQtCodecEngine::instance().getVideoParams();
        VDLogWindow::instance(this)->appendLog(QString("[Video] Selected compression codec: %1 (Mode: %2, CRF: %3, Bitrate: %4 kbps, Preset: %5, PixFmt: %6)")
            .arg(params.codecId)
            .arg(params.rateMode)
            .arg(params.crf)
            .arg(params.targetBitrateKbps)
            .arg(params.preset)
            .arg(params.pixFmt));
    }
}

void VDQtMainWindow::onVideoFilters() {
    VDQtFilterSystem& filterSystem = VDQtFilterSystem::instance();
    const QList<VDFilterInstance> originalChain = filterSystem.getActiveChain();
    int w = mVideoDecoder.isOpen() ? mVideoDecoder.getWidth() : 1920;
    int h = mVideoDecoder.isOpen() ? mVideoDecoder.getHeight() : 1080;
    QImage currentFrame;
    if (mVideoDecoder.isOpen()) {
        currentFrame = mVideoDecoder.getFrameImage(mPositionControl->GetPosition());
    }
    VDVideoFiltersDialog dlg(w, h, currentFrame, this);
    if (dlg.exec() != QDialog::Accepted) {
        filterSystem.replaceActiveChain(originalChain);
    }
    syncInteractiveFilterChain();
    updateFrameDisplay(mPositionControl->GetPosition());
}

void VDQtMainWindow::onVideoFrameRate() {
    double srcFps = mVideoDecoder.isOpen() ? mVideoDecoder.getFps() : 29.970;
    if (srcFps <= 0.0) srcFps = 29.970;
    double audioFps = srcFps;

    VDFrameRateDialog dlg(srcFps, audioFps, mFrameRateConfig, this);
    if (dlg.exec() == QDialog::Accepted) {
        mFrameRateConfig = dlg.getConfig();
        double newFps = dlg.getTargetFps();
        int decimate = dlg.getDecimateFactor();
        VDLogWindow::instance(this)->appendLog(QString("[Video] Frame rate configured: %1 fps (decimate factor: %2)")
            .arg(newFps, 0, 'f', 3)
            .arg(decimate));
        statusBar()->showMessage(QString("Frame Rate: %1 fps").arg(newFps, 0, 'f', 3));
    }
}

void VDQtMainWindow::onAudioModeDirectStream() {
    mAudioMode = AudioMode_DirectStreamCopy;
    actAudioDirectStream->setChecked(true);
    actAudioFullProcessing->setChecked(false);
    statusBar()->showMessage("Audio Mode: Direct Stream Copy");
    VDLogWindow::instance(this)->appendLog("[Audio] Mode set to Direct Stream Copy");
}

void VDQtMainWindow::onAudioModeFullProcessing() {
    mAudioMode = AudioMode_FullProcessing;
    actAudioDirectStream->setChecked(false);
    actAudioFullProcessing->setChecked(true);
    statusBar()->showMessage("Audio Mode: Full Processing Mode (Audio compression active)");
    VDLogWindow::instance(this)->appendLog("[Audio] Mode set to Full Processing Mode");
}

void VDQtMainWindow::onAudioCompression() {
    VDAudioCompressionDialog dlg(this);
    dlg.exec();
}

void VDQtMainWindow::onOptionsPreferences() {
    VDPreferencesDialog dialog(mPreferencesConfig, this);
    if (dialog.exec() != QDialog::Accepted) return;
    mPreferencesConfig = dialog.getConfig();
    VDQtVideoDecoder::setFrameCacheBudgetMiB(mPreferencesConfig.frameCacheMiB);
    VDQtVideoDecoder::setDecoderThreadCount(mPreferencesConfig.decoderThreads);
    mVideoDecoder.applyFrameCacheBudget();
    mAvsAudioDecoder.applyFrameCacheBudget();
    if (mFrameDecodeWorker && mFrameDecodeThread
        && mFrameDecodeThread->isRunning()) {
        QMetaObject::invokeMethod(
            mFrameDecodeWorker,
            [this]() { mFrameDecodeWorker->applyFrameCacheBudget(); },
            Qt::BlockingQueuedConnection);
    }
    if (mPlaybackTimer->isActive())
        mPlaybackTimer->setInterval(mPreferencesConfig.playbackTimerIntervalMs);
    statusBar()->showMessage(
        QString("Preferences applied: %1 MiB frame cache, %2 decoder threads, %3 ms clock")
            .arg(mPreferencesConfig.frameCacheMiB)
            .arg(mPreferencesConfig.decoderThreads == 0
                     ? QStringLiteral("automatic")
                     : QString::number(mPreferencesConfig.decoderThreads))
            .arg(mPreferencesConfig.playbackTimerIntervalMs));
}

void VDQtMainWindow::onHelpAbout() {
    VDAboutDialog dlg(this);
    dlg.exec();
}

void VDQtMainWindow::onPositionChanged(int frame) {
    if (mIsExporting) return;
    updateFrameDisplay(frame);
}

void VDQtMainWindow::seekAudioToVideoFrame(int frameIndex) {
    const double timestamp = mVideoDecoder.getFrameTimestampSeconds(frameIndex);
    if (std::isfinite(timestamp))
        mAudioPlayer.seekToTimeSeconds(timestamp);
    else
        mAudioPlayer.seekToFrame(frameIndex, mVideoDecoder.getFps());
}

void VDQtMainWindow::onTransportAction(int actionCode) {
    if (!mVideoDecoder.isOpen()) return;

    switch (actionCode) {
    case VDQT_PCN_STOP: // 0 - Stop
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        updateFrameDisplay(mPositionControl->GetPosition());
        break;

    case VDQT_PCN_PLAY: // 1 - Play Input Pane Only
        mPlaybackPreview = false;
        if (mPlaybackTimer->isActive()) {
            mPlaybackTimer->stop();
            mAudioPlayer.pause();
            updateFrameDisplay(mPositionControl->GetPosition());
        } else {
            mPlaybackStartFrame = mPositionControl->GetPosition();
            mPlaybackStartTimestamp = mVideoDecoder.getFrameTimestampSeconds(mPlaybackStartFrame);
            if (!std::isfinite(mPlaybackStartTimestamp))
                mPlaybackStartTimestamp = mVideoDecoder.getFps() > 0.0
                    ? mPlaybackStartFrame / mVideoDecoder.getFps()
                    : 0.0;
            seekAudioToVideoFrame(static_cast<int>(mPositionControl->GetPosition()));
            mAudioPlayer.play();
            mPlaybackElapsedTimer.restart();
            mPlaybackTimer->setTimerType(Qt::PreciseTimer);
            mPlaybackTimer->start(mPreferencesConfig.playbackTimerIntervalMs);
        }
        break;

    case VDQT_PCN_PLAYPREVIEW: // 10 - Play Preview (Both Input & Output Panes)
        mPlaybackPreview = true;
        if (mPlaybackTimer->isActive()) {
            mPlaybackTimer->stop();
            mAudioPlayer.pause();
            updateFrameDisplay(mPositionControl->GetPosition());
        } else {
            mPlaybackStartFrame = mPositionControl->GetPosition();
            mPlaybackStartTimestamp = mVideoDecoder.getFrameTimestampSeconds(mPlaybackStartFrame);
            if (!std::isfinite(mPlaybackStartTimestamp))
                mPlaybackStartTimestamp = mVideoDecoder.getFps() > 0.0
                    ? mPlaybackStartFrame / mVideoDecoder.getFps()
                    : 0.0;
            seekAudioToVideoFrame(static_cast<int>(mPositionControl->GetPosition()));
            mAudioPlayer.play();
            mPlaybackElapsedTimer.restart();
            mPlaybackTimer->setTimerType(Qt::PreciseTimer);
            mPlaybackTimer->start(mPreferencesConfig.playbackTimerIntervalMs);
        }
        break;

    case VDQT_PCN_START: // 4 - Jump to Start (|<)
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        mPositionControl->SetPosition(0);
        break;

    case VDQT_PCN_END: // 7 - Jump to End (>|)
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        if (!ensureExactFrameRange(QStringLiteral("end of stream"))) break;
        if (mVideoDecoder.getFrameCount() > 0) {
            int target = mVideoDecoder.getFrameCount() - 1;
            mPositionControl->SetRange(0, std::max(0, mVideoDecoder.getFrameCount() - 1));
            mPositionControl->SetPosition(target);
        } else {
            statusBar()->showMessage("The stream length is unknown; play or scan the stream to discover its end.");
        }
        break;

    case VDQT_PCN_BACKWARD: // 5 - Step Backward 1 frame (<)
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        mPositionControl->SetPosition(std::max<qint64>(0, mPositionControl->GetPosition() - 1));
        break;

    case VDQT_PCN_FORWARD: // 6 - Step Forward 1 frame (>)
    {
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        const int target = static_cast<int>(mPositionControl->GetPosition()) + 1;
        if (!mVideoDecoder.isFrameCountExact()) {
            const int rangeEnd = std::max(target, mVideoDecoder.getFrameCount() - 1);
            mPositionControl->SetRange(0, rangeEnd);
            mPositionControl->SetPosition(target);
        } else if (mVideoDecoder.getFrameCount() > 0) {
            mPositionControl->SetPosition(std::min(mVideoDecoder.getFrameCount() - 1, target));
        }
        break;
    }

    case VDQT_PCN_KEYPREV: // 8 - Previous Keyframe (<<K)
    {
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        const int target = mVideoDecoder.getPreviousKeyFrame(mPositionControl->GetPosition());
        if (target >= 0) mPositionControl->SetPosition(target);
        break;
    }

    case VDQT_PCN_KEYNEXT: // 9 - Next Keyframe (K>>)
    {
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        const int target = mVideoDecoder.getNextKeyFrame(mPositionControl->GetPosition());
        if (target >= 0) mPositionControl->SetPosition(target);
        break;
    }

    case VDQT_PCN_MARKIN: // 2 - Mark In ([)
    {
        qint64 position = mPositionControl->GetPosition();
        qint64 end = mPositionControl->GetSelectionEnd();
        if (end <= position) {
            if (!ensureExactFrameRange(QStringLiteral("selection range"))) break;
            position = std::min(position, mPositionControl->GetRangeEnd());
            end = mPositionControl->GetRangeEnd() + 1;
        }
        mPositionControl->SetSelection(position, end);
        break;
    }

    case VDQT_PCN_MARKOUT: // 3 - Mark Out (])
        mPositionControl->SetSelection(mPositionControl->GetSelectionStart(), mPositionControl->GetPosition());
        break;

    default:
        break;
    }
}

void VDQtMainWindow::onPlaybackTick() {
    if (!mVideoDecoder.isOpen()) {
        mPlaybackTimer->stop();
        mAudioPlayer.stop();
        return;
    }

    double fps = mVideoDecoder.getFps();
    if (fps <= 0.0) fps = 29.97;

    const double elapsedSeconds = mPlaybackElapsedTimer.elapsed() / 1000.0;
    int targetFrame = mPlaybackStartFrame + static_cast<int>(std::floor(elapsedSeconds * fps));

    if (targetFrame != mPositionControl->GetPosition()) {
        if (!mVideoDecoder.isFrameCountExact()
            && targetFrame >= mVideoDecoder.getFrameCount()) {
            mPositionControl->SetRange(
                0, std::max(targetFrame, mVideoDecoder.getFrameCount() - 1));
            mPositionControl->SetPosition(targetFrame);
            return;
        }
        if (mVideoDecoder.getFrameCount() > 0 && targetFrame >= mVideoDecoder.getFrameCount()) {
            mPlaybackTimer->stop();
            mAudioPlayer.stop();
            const int lastFrame = mVideoDecoder.getFrameCount() - 1;
            mPositionControl->SetRange(0, lastFrame);
            mPositionControl->SetPosition(lastFrame);
        } else {
            mPositionControl->SetPosition(targetFrame);
        }
    }
}

#include "VDQtFilterSystem.h"

void VDQtMainWindow::updateFrameDisplay(int frameIndex) {
    if (!mVideoDecoder.isOpen() || !mFrameDecodeWorker) return;

    const quint64 generation = ++mFrameRequestGeneration;
    const bool playing = mPlaybackTimer->isActive();
    mFrameDecodeWorker->requestFrame(
        frameIndex,
        generation,
        playing,
        !playing || mPlaybackPreview);
}

void VDQtMainWindow::onDecodedFrameReady(int frameIndex,
                                         quint64 generation,
                                         const QImage& inputImage,
                                         const QImage& outputImage,
                                         bool keyFrame,
                                         double timestampSeconds,
                                         int frameCount,
                                         int frameCountStatus,
                                         quint64 seekCount,
                                         quint64 decodedFrameCount) {
    Q_UNUSED(seekCount);
    Q_UNUSED(decodedFrameCount);
    if (generation != mFrameRequestGeneration || !mVideoDecoder.isOpen()) return;

    mPositionControl->SetCurrentFrameKey(keyFrame);
    mInputDisplay->setFrameImage(inputImage);
    if (!outputImage.isNull()) mOutputDisplay->setFrameImage(outputImage);

    if (frameCount > 0) {
        const bool exactFrameCount = frameCountStatus
            == static_cast<int>(VDQtVideoDecoder::FrameCountStatus::Exact);
        // Provisional metadata may underestimate a stream. Keep an expanded
        // interactive range while playback is discovering frames; shrinking
        // it would clamp the slider backwards and enqueue a spurious seek on
        // every frame beyond the estimate.
        const int rangeEnd = exactFrameCount
            ? frameCount - 1
            : std::max({static_cast<int>(mPositionControl->GetRangeEnd()),
                        frameCount - 1,
                        frameIndex});
        if (mPositionControl->GetRangeEnd() != rangeEnd)
            mPositionControl->SetRange(0, rangeEnd);
    }

    const double fps = mVideoDecoder.getFps();
    double timeSeconds = timestampSeconds;
    if (!std::isfinite(timeSeconds)) timeSeconds = (fps > 0) ? (frameIndex / fps) : 0;
        int hours = static_cast<int>(timeSeconds / 3600);
        int mins = static_cast<int>((timeSeconds - hours * 3600) / 60);
        int secs = static_cast<int>(timeSeconds) % 60;
        int msecs = static_cast<int>((timeSeconds - static_cast<int>(timeSeconds)) * 1000);

        QString timeStr = QString("%1:%2:%3.%4")
            .arg(hours, 2, 10, QChar('0'))
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'))
            .arg(msecs, 3, 10, QChar('0'));

    QString lastFrame = frameCount > 0 ? QString::number(frameCount - 1) : QStringLiteral("?");
    if (frameCountStatus == static_cast<int>(VDQtVideoDecoder::FrameCountStatus::Estimated))
        lastFrame.prepend(QLatin1Char('~'));
    statusBar()->showMessage(QString("Frame: %1 / %2  |  Time: %3  |  %4x%5 @ %6 fps")
        .arg(frameIndex)
        .arg(lastFrame)
        .arg(timeStr)
        .arg(inputImage.width())
        .arg(inputImage.height())
        .arg(fps, 0, 'f', 2));
}

void VDQtMainWindow::onDecodedFrameUnavailable(int frameIndex,
                                               quint64 generation,
                                               const QString& errorMessage,
                                               int frameCount,
                                               int frameCountStatus) {
    if (generation != mFrameRequestGeneration || !mVideoDecoder.isOpen()) return;

    const bool exact = frameCountStatus
        == static_cast<int>(VDQtVideoDecoder::FrameCountStatus::Exact);
    if (mPlaybackTimer->isActive() && exact) {
        mPlaybackTimer->stop();
        mAudioPlayer.stop();
        if (frameCount > 0) {
            const int lastFrame = frameCount - 1;
            mPositionControl->SetRange(0, lastFrame);
            if (mPositionControl->GetPosition() != lastFrame)
                mPositionControl->SetPosition(lastFrame);
        }
    } else if (!errorMessage.isEmpty()) {
        statusBar()->showMessage(
            QString("Unable to decode frame %1: %2").arg(frameIndex).arg(errorMessage));
    }
}

bool VDQtMainWindow::openInteractiveDecoder(const QString& filePath, QString *errorMessage) {
    if (!mFrameDecodeWorker || !mFrameDecodeThread || !mFrameDecodeThread->isRunning()) {
        if (errorMessage) *errorMessage = QStringLiteral("The frame decoding worker is unavailable.");
        return false;
    }

    bool opened = false;
    QString workerError;
    const QList<VDFilterInstance> chain = VDQtFilterSystem::instance().getActiveChain();
    QMetaObject::invokeMethod(
        mFrameDecodeWorker,
        [this, &opened, &workerError, filePath, chain]() {
            mFrameDecodeWorker->setFilterChain(chain);
            opened = mFrameDecodeWorker->openSource(
                filePath,
                mDecompressionFormatConfig.formatName,
                mDecompressionFormatConfig.colorSpace,
                mDecompressionFormatConfig.componentRange,
                mDecoderErrorModeConfig.errorMode);
            if (!opened) workerError = mFrameDecodeWorker->lastError();
        },
        Qt::BlockingQueuedConnection);
    if (errorMessage) *errorMessage = workerError;
    return opened;
}

void VDQtMainWindow::closeInteractiveDecoder() {
    if (!mFrameDecodeWorker) return;
    mFrameDecodeWorker->cancelPending(++mFrameRequestGeneration);
    if (mFrameDecodeThread && mFrameDecodeThread->isRunning()) {
        QMetaObject::invokeMethod(
            mFrameDecodeWorker,
            [worker = mFrameDecodeWorker]() { worker->closeSource(); },
            Qt::BlockingQueuedConnection);
    }
}

void VDQtMainWindow::syncInteractiveFilterChain() {
    if (!mFrameDecodeWorker || !mFrameDecodeThread || !mFrameDecodeThread->isRunning()) return;
    const QList<VDFilterInstance> chain = VDQtFilterSystem::instance().getActiveChain();
    QMetaObject::invokeMethod(
        mFrameDecodeWorker,
        [worker = mFrameDecodeWorker, chain]() { worker->setFilterChain(chain); },
        Qt::BlockingQueuedConnection);
}

void VDQtMainWindow::addRecentFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QSettings settings("VirtualDub", "VirtualDub_Port");
    QStringList files = settings.value("recentFiles").toStringList();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > 4) {
        files.removeLast();
    }
    settings.setValue("recentFiles", files);
    updateRecentFilesMenu();
}

void VDQtMainWindow::updateRecentFilesMenu() {
    QSettings settings("VirtualDub", "VirtualDub_Port");
    QStringList files = settings.value("recentFiles").toStringList();

    int numRecentFiles = std::min(static_cast<int>(files.size()), 4);
    if (actFileReopen) {
        actFileReopen->setEnabled(numRecentFiles > 0);
    }

    for (int i = 0; i < 4; ++i) {
        if (i < numRecentFiles && i < mRecentFileActions.size()) {
            QString path = files[i];
            QString text = QString("&%1 %2").arg(i + 1).arg(path);
            mRecentFileActions[i]->setText(text);
            mRecentFileActions[i]->setData(path);
            mRecentFileActions[i]->setVisible(true);
        } else if (i < mRecentFileActions.size()) {
            mRecentFileActions[i]->setVisible(false);
        }
    }
}

void VDQtMainWindow::onOpenRecentFile() {
    QAction *action = qobject_cast<QAction*>(sender());
    if (action) {
        QString filePath = action->data().toString();
        if (!filePath.isEmpty()) {
            openVideoFile(filePath);
        }
    }
}

void VDQtMainWindow::onFileReopen() {
    QSettings settings("VirtualDub", "VirtualDub_Port");
    QStringList files = settings.value("recentFiles").toStringList();
    if (!files.isEmpty()) {
        openVideoFile(files.first());
    }
}
