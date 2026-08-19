#include "VDQtMainWindow.h"
#include "VDQtSourceSafety.h"
#include "VDQtBatchWizard.h"
#include "VDQtJobControl.h"
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
#include <QComboBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QSysInfo>
#include <QEventLoop>
#include <QCollator>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace {

VDQtOutputSafetyReport loadedOutputSafety(const QString& outputPath,
                                          const VDQtVideoDecoder& decoder,
                                          const VDQtAudioPlayer& audioPlayer,
                                          const QStringList& additionalSources = {}) {
    QStringList protectedSources = {
        decoder.getFilePath(), audioPlayer.getSourcePath()
    };
    protectedSources.append(additionalSources);
    protectedSources.removeDuplicates();
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

bool writeImageSequenceManifest(const QString& path,
                                const QStringList& sources,
                                double frameRate,
                                QString *errorMessage) {
    if (sources.isEmpty() || !std::isfinite(frameRate) || frameRate <= 0.0) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "An image sequence and a positive frame rate are required.");
        return false;
    }
    QByteArray contents("ffconcat version 1.0\n");
    for (const QString& source : sources) {
        const QString escaped = escapedConcatPath(QFileInfo(source).absoluteFilePath());
        if (escaped.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "An image path contains a newline.");
            return false;
        }
        contents += "file ";
        contents += escaped.toUtf8();
        contents += '\n';
        contents += "duration ";
        contents += QByteArray::number(1.0 / frameRate, 'f', 12);
        contents += '\n';
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(contents) != contents.size() || !output.commit()) {
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

    mJobQueue = new VDQtJobQueue(this);
    const QString queueDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    if (!queueDirectory.isEmpty()) {
        QDir().mkpath(queueDirectory);
        mJobQueue->setAutosavePath(
            QDir(queueDirectory).filePath(QStringLiteral("VirtualDub.vdqjobs")));
    }
    connect(mJobQueue, &VDQtJobQueue::runRequested,
            this, &VDQtMainWindow::runPendingJobs);
    connect(mJobQueue, &VDQtJobQueue::stopRequested,
            this, &VDQtMainWindow::stopJobQueue);
    connect(mJobQueue, &VDQtJobQueue::abortRequested,
            this, &VDQtMainWindow::abortCurrentJob);
    connect(mJobQueue, &VDQtJobQueue::reloadRequested,
            this, &VDQtMainWindow::reloadQueuedJob);
    connect(mJobQueue, &VDQtJobQueue::batchWizardRequested,
            this, &VDQtMainWindow::onFileBatchWizard);
    QString queueLoadError;
    if (!mJobQueue->loadAutosave(&queueLoadError) && !queueLoadError.isEmpty()) {
        VDLogWindow::instance(this)->appendLog(
            QStringLiteral("[Job queue] Could not restore the local queue: %1")
                .arg(queueLoadError));
    }

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
                if (!mFrameServerAudioPath.isEmpty()) {
                    QFile::remove(mFrameServerAudioPath);
                    mFrameServerAudioPath.clear();
                }
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
    connect(mPositionControl, &VDQtPositionControlWidget::selectionChanged,
            this, [this](qint64, qint64) { updateEditActions(); });
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
    if (mJobQueue) {
        QString queueSaveError;
        if (!mJobQueue->flush(&queueSaveError) && !queueSaveError.isEmpty())
            qWarning() << "[Job queue] Could not save the local queue:" << queueSaveError;
    }
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
    mFileMenu->addAction("Open image sequence...", this, &VDQtMainWindow::onFileOpenImageSequence);
    mFileMenu->addAction("Open raw video...", this, &VDQtMainWindow::onFileOpenRawVideo);
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
    actEditUndo = mEdit->addAction("&Undo", QKeySequence::Undo,
                                    this, &VDQtMainWindow::onEditUndo);
    actEditRedo = mEdit->addAction("&Redo", QKeySequence::Redo,
                                    this, &VDQtMainWindow::onEditRedo);
    mEdit->addSeparator();
    actEditCut = mEdit->addAction("Cu&t", QKeySequence::Cut,
                                   this, &VDQtMainWindow::onEditCut);
    actEditCopy = mEdit->addAction("&Copy", QKeySequence::Copy,
                                    this, &VDQtMainWindow::onEditCopy);
    actEditPaste = mEdit->addAction("&Paste", QKeySequence::Paste,
                                     this, &VDQtMainWindow::onEditPaste);
    actEditDelete = mEdit->addAction("&Delete", QKeySequence::Delete,
                                      this, &VDQtMainWindow::onEditDelete);
    actEditCrop = mEdit->addAction("Crop to selection",
                                    QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X),
                                    this, &VDQtMainWindow::onEditCropToSelection);
    mEdit->addSeparator();
    mEdit->addAction("Set selection &start", QKeySequence(Qt::Key_BracketLeft), this, &VDQtMainWindow::onEditSetSelectionStart);
    mEdit->addAction("Set selection &end", QKeySequence(Qt::Key_BracketRight), this, &VDQtMainWindow::onEditSetSelectionEnd);
    mEdit->addAction("Select &All", QKeySequence::SelectAll, this, &VDQtMainWindow::onEditSelectAll);
    actEditReset = mEdit->addAction("Reset timeline edits", this,
                                    &VDQtMainWindow::onEditResetTimeline);
    mEdit->addSeparator();
    mEdit->addAction("Previous scene change", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Left),
                     this, &VDQtMainWindow::onEditPreviousSceneChange);
    mEdit->addAction("Next scene change", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Right),
                     this, &VDQtMainWindow::onEditNextSceneChange);
    updateEditActions();

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
    actAudioSource = mAudio->addAction("&Source...", this, &VDQtMainWindow::onAudioSource);
    mAudio->addSeparator();
    actAudioDirectStream = mAudio->addAction("&Direct stream copy", this, &VDQtMainWindow::onAudioModeDirectStream);
    actAudioDirectStream->setCheckable(true);
    actAudioDirectStream->setChecked(true);

    actAudioFullProcessing = mAudio->addAction("&Full processing mode", this, &VDQtMainWindow::onAudioModeFullProcessing);
    actAudioFullProcessing->setCheckable(true);

    mAudio->addSeparator();
    actAudioCompression = mAudio->addAction("&Compression...", this, &VDQtMainWindow::onAudioCompression);
    actAudioFilters = mAudio->addAction("&Filters...", this, &VDQtMainWindow::onAudioFilters);

    QMenu *mOptions = bar->addMenu("&Options");
    mOptions->addAction("&Preferences...", this, &VDQtMainWindow::onOptionsPreferences);

    QMenu *mTools = bar->addMenu("&Tools");
    mTools->addAction("Backend and Plugin Catalog...", this,
                      &VDQtMainWindow::onToolsBackendCatalog);
    mTools->addAction("System Information...", this,
                      &VDQtMainWindow::onToolsSystemInformation);

    QMenu *mCapture = bar->addMenu("&Capture");
    mCapture->addAction("Capture Video...", this,
                        &VDQtMainWindow::onCaptureVideo);

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
        mAudioSourcePath.clear();
        mAudioStreamIndex = -1;
        mAudioDisabled = false;

        const bool concatenated = filePath.endsWith(
            QStringLiteral(".ffconcat"), Qt::CaseInsensitive);
        if (concatenated) {
            mTimelineSources =
                VDQtVideoDecoder::auditScriptDependencies(filePath).resolvedPaths;
        } else {
            mTimelineSources = { QFileInfo(filePath).absoluteFilePath() };
        }
        mTimeline.reset(
            mVideoDecoder.getFrameCount(), mVideoDecoder.isFrameCountExact());
        const QString displayName = concatenated
            ? QString("Concatenated timeline (%1 segments)").arg(mTimelineSources.size())
            : filePath;
        mInputDisplay->setLabelText(QString("Loaded: %1").arg(displayName));
        mOutputDisplay->setLabelText(QString("Filtered Output: %1").arg(displayName));
        mPositionControl->SetRange(
            0, std::max<qint64>(0, mTimeline.frameCount() - 1));
        mPositionControl->SetPosition(0);
        updateEditActions();

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

void VDQtMainWindow::onFileOpenImageSequence() {
    QStringList images = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Open Image Sequence"), QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.exr *.dpx *.tga);;All Files (*)"));
    if (images.isEmpty()) return;
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(images.begin(), images.end(),
              [&collator](const QString& left, const QString& right) {
                  return collator.compare(left, right) < 0;
              });
    bool accepted = false;
    const double frameRate = QInputDialog::getDouble(
        this, QStringLiteral("Image Sequence Frame Rate"),
        QStringLiteral("Frames per second:"), 25.0, 0.001, 1000.0, 3,
        &accepted);
    if (!accepted) return;
    if (!mTimelineTempDirectory.isValid()) {
        QMessageBox::critical(this, "Image Sequence Error",
                              "A temporary session directory is unavailable.");
        return;
    }
    const QString manifestPath = mTimelineTempDirectory.filePath(
        QString("images_%1.ffconcat").arg(
            QUuid::createUuid().toString(QUuid::Id128)));
    QString error;
    if (!writeImageSequenceManifest(manifestPath, images, frameRate, &error)) {
        QMessageBox::critical(this, "Image Sequence Error", error);
        return;
    }
    if (!openVideoFile(manifestPath)) return;
    mTimelineSources.clear();
    for (const QString& image : images)
        mTimelineSources.append(QFileInfo(image).absoluteFilePath());
    mImageSequenceFps = frameRate;
    mFrameRateConfig.sourceMode = 1;
    mFrameRateConfig.customSourceFps = frameRate;
    mPositionControl->SetFrameRate(frameRate);
    setWindowTitle(QString("VirtualDubQt v0.1 - [Image sequence: %1 files]")
                       .arg(images.size()));
    statusBar()->showMessage(
        QString("Opened %1 images at %2 fps")
            .arg(images.size()).arg(frameRate, 0, 'f', 3));
}

bool VDQtMainWindow::materializeRawVideo(
    const QString& sourcePath,
    const QString& pixelFormat,
    int width,
    int height,
    double frameRate,
    qint64 byteOffset,
    QString *outputPath,
    QString *errorMessage) {
    if (!QFileInfo::exists(sourcePath) || pixelFormat.isEmpty()
        || width <= 0 || height <= 0 || !std::isfinite(frameRate)
        || frameRate <= 0.0 || byteOffset < 0
        || !mTimelineTempDirectory.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The raw-video source parameters are invalid.");
        return false;
    }
    const QString materializedPath = mTimelineTempDirectory.filePath(
        QString("raw_%1.nut").arg(
            QUuid::createUuid().toString(QUuid::Id128)));
    QStringList arguments{
        QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("error")
    };
    if (byteOffset > 0)
        arguments << QStringLiteral("-skip_initial_bytes")
                  << QString::number(byteOffset);
    arguments << QStringLiteral("-f") << QStringLiteral("rawvideo")
              << QStringLiteral("-pixel_format") << pixelFormat
              << QStringLiteral("-video_size")
              << QString("%1x%2").arg(width).arg(height)
              << QStringLiteral("-framerate")
              << QString::number(frameRate, 'f', 12)
              << QStringLiteral("-i") << QFileInfo(sourcePath).absoluteFilePath()
              << QStringLiteral("-map") << QStringLiteral("0:v:0")
              << QStringLiteral("-c:v") << QStringLiteral("copy")
              << QStringLiteral("-f") << QStringLiteral("nut")
              << QStringLiteral("-y") << materializedPath;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral("ffmpeg"), arguments);
    if (!process.waitForStarted(5000)) {
        if (errorMessage) *errorMessage = process.errorString();
        return false;
    }
    QProgressDialog progress(
        QStringLiteral("Indexing raw video without recompression..."),
        QStringLiteral("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QByteArray diagnostics;
    bool cancelled = false;
    while (!process.waitForFinished(50)) {
        diagnostics += process.readAll();
        if (diagnostics.size() > 64 * 1024)
            diagnostics.remove(0, diagnostics.size() - 64 * 1024);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress.wasCanceled()) {
            cancelled = true;
            process.terminate();
            if (!process.waitForFinished(1000)) {
                process.kill();
                process.waitForFinished(1000);
            }
            break;
        }
    }
    diagnostics += process.readAll();
    progress.close();
    const bool success = !cancelled
        && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0
        && QFileInfo(materializedPath).size() > 0;
    if (!success) {
        QFile::remove(materializedPath);
        if (errorMessage && !cancelled) {
            *errorMessage = QString::fromLocal8Bit(diagnostics.right(8192));
            if (errorMessage->isEmpty())
                *errorMessage = QStringLiteral("FFmpeg could not index the raw video.");
        }
        return false;
    }
    if (outputPath) *outputPath = materializedPath;
    if (errorMessage) errorMessage->clear();
    return true;
}

void VDQtMainWindow::onFileOpenRawVideo() {
    const QString sourcePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Raw Video"), QString(),
        QStringLiteral("Raw Video and Data (*.raw *.rgb *.rgba *.yuv *.nv12 *.gray);;All Files (*)"));
    if (sourcePath.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Raw Video Parameters"));
    auto *form = new QFormLayout(&dialog);
    auto *format = new QComboBox(&dialog);
    const struct { const char *label; const char *name; } formats[] = {
        {"YUV 4:2:0 planar, 8-bit", "yuv420p"},
        {"YUV 4:2:2 planar, 8-bit", "yuv422p"},
        {"YUV 4:4:4 planar, 8-bit", "yuv444p"},
        {"NV12 4:2:0", "nv12"}, {"YUYV 4:2:2", "yuyv422"},
        {"UYVY 4:2:2", "uyvy422"}, {"Grayscale 8-bit", "gray"},
        {"RGB24", "rgb24"}, {"BGR24", "bgr24"},
        {"RGBA 8-bit", "rgba"}, {"BGRA 8-bit", "bgra"},
        {"RGB48 little-endian", "rgb48le"},
        {"RGBA64 little-endian", "rgba64le"}
    };
    for (const auto& entry : formats)
        format->addItem(QString::fromLatin1(entry.label),
                        QString::fromLatin1(entry.name));
    auto *width = new QSpinBox(&dialog);
    width->setRange(1, 32768); width->setValue(1920);
    auto *height = new QSpinBox(&dialog);
    height->setRange(1, 32768); height->setValue(1080);
    auto *frameRate = new QDoubleSpinBox(&dialog);
    frameRate->setRange(0.001, 1000.0); frameRate->setDecimals(6);
    frameRate->setValue(30.0);
    auto *offset = new QLineEdit(QStringLiteral("0"), &dialog);
    form->addRow(QStringLiteral("Pixel format:"), format);
    form->addRow(QStringLiteral("Width:"), width);
    form->addRow(QStringLiteral("Height:"), height);
    form->addRow(QStringLiteral("Frame rate:"), frameRate);
    form->addRow(QStringLiteral("Header bytes to skip:"), offset);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    bool offsetValid = false;
    const qint64 byteOffset = offset->text().trimmed().toLongLong(&offsetValid);
    if (!offsetValid || byteOffset < 0) {
        QMessageBox::critical(this, "Raw Video Error",
                              "The header-byte offset is invalid.");
        return;
    }
    QString materializedPath;
    QString error;
    if (!materializeRawVideo(
            sourcePath, format->currentData().toString(), width->value(),
            height->value(), frameRate->value(), byteOffset,
            &materializedPath, &error)) {
        if (!error.isEmpty())
            QMessageBox::critical(this, "Raw Video Error", error);
        return;
    }
    if (!openVideoFile(materializedPath)) {
        QFile::remove(materializedPath);
        return;
    }
    mTimelineSources = {QFileInfo(sourcePath).absoluteFilePath()};
    mRawInputPixelFormat = format->currentData().toString();
    mRawInputWidth = width->value();
    mRawInputHeight = height->value();
    mRawInputFrameRate = frameRate->value();
    mRawInputByteOffset = byteOffset;
    mFrameRateConfig.sourceMode = 1;
    mFrameRateConfig.customSourceFps = mRawInputFrameRate;
    mPositionControl->SetFrameRate(mRawInputFrameRate);
    setWindowTitle(QString("VirtualDubQt v0.1 - [Raw: %1]")
                       .arg(QFileInfo(sourcePath).fileName()));
    statusBar()->showMessage(QString("Opened raw %1 %2x%3 at %4 fps")
        .arg(mRawInputPixelFormat).arg(mRawInputWidth).arg(mRawInputHeight)
        .arg(mRawInputFrameRate, 0, 'f', 3));
}

void VDQtMainWindow::onFileAppendSegment() {
    if (!mVideoDecoder.isOpen() || mTimelineSources.isEmpty()) {
        QMessageBox::warning(
            this, "Append Segment",
            "Open the first media segment before appending another segment.");
        return;
    }
    if (!ensureExactFrameRange(QStringLiteral("timeline before append"))) return;
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
    const QList<VDQtTimelineSegment> oldEditSegments = mTimeline.segments();
    const qint64 oldSourceFrameCount = mTimeline.sourceFrameCount();
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
            if (mVideoDecoder.isFrameCountExact()) {
                mTimeline.reset(mVideoDecoder.getFrameCount(), true);
                mTimeline.replaceSegments(oldEditSegments, nullptr);
            }
            mPositionControl->SetPosition(std::min(
                oldPosition, std::max(0, mVideoDecoder.getFrameCount() - 1)));
            if (hadSelection)
                mPositionControl->SetSelection(oldSelectionStart, oldSelectionEnd);
        }
        return;
    }
    applyProcessingState(processing);
    mTimelineSources = newTimeline;
    if (!ensureExactFrameRange(QStringLiteral("appended timeline"))) return;
    QList<VDQtTimelineSegment> appendedEditSegments = oldEditSegments;
    const qint64 appendedFrames =
        mTimeline.sourceFrameCount() - oldSourceFrameCount;
    if (appendedFrames > 0)
        appendedEditSegments.append({oldSourceFrameCount, appendedFrames});
    if (!mTimeline.replaceSegments(appendedEditSegments, &error)) {
        QMessageBox::critical(this, "Append Segment Error", error);
        return;
    }
    mCurrentProjectPath.clear();
    mPositionControl->SetPosition(std::min(
        oldPosition, std::max(0, static_cast<int>(mTimeline.frameCount()) - 1)));
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
    mImageSequenceFps = 0.0;
    mRawInputPixelFormat.clear();
    mRawInputWidth = 0;
    mRawInputHeight = 0;
    mRawInputFrameRate = 0.0;
    mRawInputByteOffset = 0;
    mTimeline.reset(0, true);
    mTimelineClipboard.clear();
    mAudioSourcePath.clear();
    mAudioStreamIndex = -1;
    mAudioDisabled = false;
    updateEditActions();
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
    state.audioFilters = VDQtAudioFilterSystem::instance().activeChain();
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
    VDQtAudioFilterSystem::instance().replaceActiveChain(state.audioFilters);
    mAudioPlayer.refreshAudioFilters();
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
    options.protectedSourcePaths = mTimelineSources;
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
    options.audioMode = (!mAudioSourcePath.isEmpty() || mAudioStreamIndex >= 0)
        ? AudioMode_FullProcessing : mAudioMode;
    options.containerType = containerType;
    options.fastStart = fastStart;
    options.metadata = mTextMetadata;
    options.smartRendering = mSmartRendering;
    options.preserveEmptyFrames = mPreserveEmptyFrames;
    options.includeAudio = !mAudioDisabled;
    if (mTimeline.isModified()) options.timelineSegments = mTimeline.segments();
    return options;
}

VDQtJobState VDQtMainWindow::currentJobTemplate() const {
    VDQtJobState job;
    job.sourcePaths = mTimelineSources;
    if (job.sourcePaths.isEmpty() && mVideoDecoder.isOpen())
        job.sourcePaths = {mVideoDecoder.getFilePath()};
    job.imageSequenceFps = mImageSequenceFps;
    job.rawPixelFormat = mRawInputPixelFormat;
    job.rawWidth = mRawInputWidth;
    job.rawHeight = mRawInputHeight;
    job.rawFrameRate = mRawInputFrameRate;
    job.rawByteOffset = mRawInputByteOffset;
    job.audioSourcePath = mAudioSourcePath;
    job.audioStreamIndex = mAudioStreamIndex;
    job.audioDisabled = mAudioDisabled;
    job.options = currentExportOptions(
        QString(), QStringLiteral("mkv"), false, true);
    job.processing = captureProcessingState();
    return job;
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
        const bool manifestWritten = project.imageSequenceFps > 0.0
            ? writeImageSequenceManifest(sourceToOpen, project.sourcePaths,
                                         project.imageSequenceFps, &error)
            : writeConcatManifest(sourceToOpen, project.sourcePaths, &error);
        if (!manifestWritten) {
            QMessageBox::critical(this, "Load Project Error", error);
            return false;
        }
    }
    if (!project.rawPixelFormat.isEmpty()) {
        if (project.sourcePaths.size() != 1
            || !materializeRawVideo(
                project.sourcePath, project.rawPixelFormat,
                project.rawWidth, project.rawHeight, project.rawFrameRate,
                project.rawByteOffset, &sourceToOpen, &error)) {
            QMessageBox::critical(
                this, "Load Project Error",
                error.isEmpty()
                    ? QStringLiteral("The saved raw-video source could not be indexed.")
                    : error);
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
    if (!project.audioDisabled
        && (!project.audioSourcePath.isEmpty()
            || (project.audioStreamIndex >= 0
                && !validationDecoder.isAvsNative()))) {
        VDQtAudioPlayer validationAudio;
        const QString validationAudioPath = project.audioSourcePath.isEmpty()
            ? sourceToOpen : project.audioSourcePath;
        if (!QFileInfo::exists(validationAudioPath)
            || !validationAudio.openFile(
                validationAudioPath, project.audioStreamIndex)
            || !validationAudio.hasAudio()) {
            validationDecoder.close();
            QMessageBox::critical(
                this, "Load Project Error",
                QString("The project's selected audio source could not be opened:\n%1")
                    .arg(validationAudioPath));
            return false;
        }
    }
    validationDecoder.close();

    applyProcessingState(project.processing);
    if (!openVideoFile(sourceToOpen)) return false;
    applyProcessingState(project.processing);
    mTimelineSources = project.sourcePaths;
    mImageSequenceFps = project.imageSequenceFps;
    if (mImageSequenceFps > 0.0)
        mPositionControl->SetFrameRate(mImageSequenceFps);
    mRawInputPixelFormat = project.rawPixelFormat;
    mRawInputWidth = project.rawWidth;
    mRawInputHeight = project.rawHeight;
    mRawInputFrameRate = project.rawFrameRate;
    mRawInputByteOffset = project.rawByteOffset;
    if (mRawInputFrameRate > 0.0)
        mPositionControl->SetFrameRate(mRawInputFrameRate);
    mCurrentProjectPath = QFileInfo(path).absoluteFilePath();

    if (project.audioDisabled) {
        mAudioPlayer.close();
        mAvsAudioDecoder.close();
        mAudioDisabled = true;
        mAudioSourcePath.clear();
        mAudioStreamIndex = -1;
    } else if (!project.audioSourcePath.isEmpty()) {
        if (!QFileInfo::exists(project.audioSourcePath)
            || !mAudioPlayer.openFile(project.audioSourcePath,
                                      project.audioStreamIndex)) {
            QMessageBox::critical(
                this, "Load Project Error",
                QString("The project's external audio source could not be opened:\n%1")
                    .arg(project.audioSourcePath));
            return false;
        }
        mAudioSourcePath = project.audioSourcePath;
        mAudioStreamIndex = mAudioPlayer.getSelectedStreamIndex();
        mAudioDisabled = false;
        onAudioModeFullProcessing();
    } else if (project.audioStreamIndex >= 0 && !mVideoDecoder.isAvsNative()) {
        if (!mAudioPlayer.openFile(sourceToOpen, project.audioStreamIndex)) {
            QMessageBox::critical(
                this, "Load Project Error",
                QString("Audio stream %1 is no longer available in the project source.")
                    .arg(project.audioStreamIndex));
            return false;
        }
        mAudioStreamIndex = project.audioStreamIndex;
        mAudioDisabled = false;
        onAudioModeFullProcessing();
    }

    if (!project.timelineSegments.isEmpty()) {
        if (!ensureExactFrameRange(QStringLiteral("project edit list"))) return false;
        if (project.sourceFrameCount > 0
            && project.sourceFrameCount != mVideoDecoder.getFrameCount()) {
            QMessageBox::critical(
                this, "Load Project Error",
                QString("The source now contains %1 frames, but the saved edit list was "
                        "created for %2 frames. The project was not applied because its "
                        "frame references may no longer be valid.")
                    .arg(mVideoDecoder.getFrameCount())
                    .arg(project.sourceFrameCount));
            return false;
        }
        if (!mTimeline.replaceSegments(project.timelineSegments, &error)) {
            QMessageBox::critical(this, "Load Project Error", error);
            return false;
        }
    }

    const qint64 requestedLast = std::max(
        project.position,
        project.hasSelection ? project.selectionEnd - 1 : 0);
    if (!mVideoDecoder.isFrameCountExact()
        && requestedLast >= mVideoDecoder.getFrameCount()) {
        if (!ensureExactFrameRange(QStringLiteral("project timeline"))) return false;
    }
    const int frameCount = static_cast<int>(mTimeline.frameCount());
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
        mCurrentProjectPath, mVideoDecoder, mAudioPlayer, mTimelineSources);
    if (!projectSafety.isSafe()) {
        QMessageBox::critical(
            this, "Unsafe Project Path",
            "The project path aliases a loaded or script-referenced media source, or "
            "cannot be safely audited. Choose a different project path.");
        return;
    }
    VDQtProjectState project;
    project.sourcePaths = mTimelineSources;
    if (project.sourcePaths.isEmpty())
        project.sourcePaths = { mVideoDecoder.getFilePath() };
    project.sourcePath = project.sourcePaths.first();
    project.imageSequenceFps = mImageSequenceFps;
    project.rawPixelFormat = mRawInputPixelFormat;
    project.rawWidth = mRawInputWidth;
    project.rawHeight = mRawInputHeight;
    project.rawFrameRate = mRawInputFrameRate;
    project.rawByteOffset = mRawInputByteOffset;
    project.audioSourcePath = mAudioSourcePath;
    project.audioStreamIndex = mAudioStreamIndex;
    project.audioDisabled = mAudioDisabled;
    project.position = mPositionControl->GetPosition();
    project.hasSelection = mPositionControl->hasSelection();
    project.selectionStart = mPositionControl->GetSelectionStart();
    project.selectionEnd = mPositionControl->GetSelectionEnd();
    project.sourceFrameCount = mTimeline.sourceFrameCount();
    project.timelineSegments = mTimeline.segments();
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
        loadedOutputSafety(path, mVideoDecoder, mAudioPlayer, mTimelineSources);
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
            loadedOutputSafety(path, mVideoDecoder, mAudioPlayer, mTimelineSources);
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
            loadedOutputSafety(outPath, mVideoDecoder, mAudioPlayer, mTimelineSources);
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
        QList<VDQtTimelineSegment> audioEditSegments;
        if (mPositionControl->hasSelection()) {
            const qint64 requestedStart = mPositionControl->GetSelectionStart();
            const qint64 requestedEndExclusive = mPositionControl->GetSelectionEnd();
            const int exactFrameCount = static_cast<int>(mTimeline.frameCount());
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

            QString timelineError;
            if (mTimeline.isModified()) {
                audioEditSegments = mTimeline.copyRange(
                    startFrame, endFrame + 1, &timelineError);
                if (audioEditSegments.isEmpty()) {
                    QMessageBox::critical(
                        this, "Audio Export Range Error", timelineError);
                    return;
                }
            }

            const int firstSourceFrame = sourceFrameForTimelineFrame(startFrame);
            const int lastSourceFrame = sourceFrameForTimelineFrame(endFrame);
            double startSeconds =
                mVideoDecoder.getFrameTimestampSeconds(firstSourceFrame);
            if (!std::isfinite(startSeconds)) startSeconds = firstSourceFrame / fps;
            double durationSeconds = (endFrame - startFrame + 1) / fps;
            const double lastTimestamp =
                mVideoDecoder.getFrameTimestampSeconds(lastSourceFrame);
            const double lastDuration =
                mVideoDecoder.getFrameDurationSeconds(lastSourceFrame);
            if (std::isfinite(lastTimestamp) && std::isfinite(lastDuration)
                && lastTimestamp + lastDuration > startSeconds) {
                durationSeconds = lastTimestamp + lastDuration - startSeconds;
            }

            startSample = static_cast<int64_t>(std::llround(startSeconds * sampleRate));
            sampleCount = static_cast<int64_t>(std::llround(durationSeconds * sampleRate));
        } else if (mTimeline.isModified()) {
            audioEditSegments = mTimeline.segments();
        }

        const auto exportSelectedAudioToWav =
            [&](const QString& destination,
                std::function<bool(int, int)> progressCallback) -> bool {
            if (audioEditSegments.isEmpty()) {
                return mAudioPlayer.exportAudioToFile(
                    destination, startSample, sampleCount, progressCallback);
            }

            QTemporaryDir segmentDirectory;
            if (!segmentDirectory.isValid()) return false;
            QStringList segmentFiles;
            for (int index = 0; index < audioEditSegments.size(); ++index) {
                const VDQtTimelineSegment& segment = audioEditSegments.at(index);
                const int firstSource = static_cast<int>(segment.sourceStartFrame);
                const int lastSource = static_cast<int>(
                    segment.sourceStartFrame + segment.frameCount - 1);
                double startSeconds =
                    mVideoDecoder.getFrameTimestampSeconds(firstSource);
                if (!std::isfinite(startSeconds)) startSeconds = firstSource / fps;
                double durationSeconds = segment.frameCount / fps;
                const double lastTimestamp =
                    mVideoDecoder.getFrameTimestampSeconds(lastSource);
                const double lastDuration =
                    mVideoDecoder.getFrameDurationSeconds(lastSource);
                if (std::isfinite(lastTimestamp) && std::isfinite(lastDuration)
                    && lastTimestamp + lastDuration > startSeconds) {
                    durationSeconds = lastTimestamp + lastDuration - startSeconds;
                }
                const int64_t segmentStart = static_cast<int64_t>(
                    std::llround(startSeconds * sampleRate));
                const int64_t segmentSamples = std::max<int64_t>(
                    1, static_cast<int64_t>(
                        std::llround(durationSeconds * sampleRate)));
                const QString segmentPath = segmentDirectory.filePath(
                    QString("segment_%1.wav").arg(index, 6, 10, QLatin1Char('0')));
                const bool extracted = mAudioPlayer.exportAudioToFile(
                    segmentPath, segmentStart, segmentSamples,
                    [&, index](int current, int total) {
                        const double fraction = total > 0
                            ? static_cast<double>(current) / total : 0.0;
                        const int combinedCurrent = static_cast<int>(std::llround(
                            1000.0 * (index + fraction)
                            / audioEditSegments.size()));
                        return progressCallback
                            ? progressCallback(combinedCurrent, 1000) : true;
                    });
                if (!extracted) return false;
                segmentFiles.append(segmentPath);
            }
            if (segmentFiles.size() == 1) {
                QFile::remove(destination);
                return QFile::rename(segmentFiles.first(), destination);
            }

            const QString manifestPath =
                segmentDirectory.filePath(QStringLiteral("audio.ffconcat"));
            QSaveFile manifest(manifestPath);
            QByteArray contents("ffconcat version 1.0\n");
            for (const QString& segmentPath : segmentFiles) {
                contents += "file ";
                contents += QFileInfo(segmentPath).fileName().toUtf8();
                contents += '\n';
            }
            if (!manifest.open(QIODevice::WriteOnly)
                || manifest.write(contents) != contents.size()
                || !manifest.commit())
                return false;

            QProcess concat;
            concat.setWorkingDirectory(segmentDirectory.path());
            concat.start(
                QStringLiteral("ffmpeg"),
                {QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
                 QStringLiteral("-loglevel"), QStringLiteral("error"),
                 QStringLiteral("-f"), QStringLiteral("concat"),
                 QStringLiteral("-safe"), QStringLiteral("1"),
                 QStringLiteral("-i"), QFileInfo(manifestPath).fileName(),
                 QStringLiteral("-c:a"), QStringLiteral("copy"),
                 QStringLiteral("-y"), destination});
            if (!concat.waitForStarted(3000)) return false;
            while (!concat.waitForFinished(30)) {
                QCoreApplication::processEvents();
                if (progress.wasCanceled()) {
                    concat.kill();
                    concat.waitForFinished();
                    return false;
                }
            }
            return concat.exitStatus() == QProcess::NormalExit
                && concat.exitCode() == 0
                && QFileInfo(destination).size() > 0;
        };

        bool ok = false;
        QString errorMsg;

        QString audioCodec = audioCfg.codecId.toLower();

        // 1. Direct Uncompressed PCM Export if no resampling/channel change requested and saving as WAV
        if ((audioCodec == "pcm_s16le" || audioCodec == "(uncompressed)" || audioCodec.isEmpty()) &&
            audioCfg.sampleRate == 0 && audioCfg.channels == 0 && outPath.endsWith(".wav", Qt::CaseInsensitive)) {
            ok = exportSelectedAudioToWav(workingOutputPath, [&progress](int cur, int total) -> bool {
                const int pct = total > 0 ? std::clamp(cur * 100 / total, 0, 100) : 0;
                progress.setValue(pct);
                progress.setLabelText(QString("Exporting uncompressed PCM audio...\n%1% complete").arg(pct));
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
            bool extractOk = exportSelectedAudioToWav(tempWav, [&progress](int cur, int total) -> bool {
                int pct = total > 0 ? std::clamp(cur * 50 / total, 0, 50) : 0;
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
                loadedOutputSafety(outPath, mVideoDecoder, mAudioPlayer, mTimelineSources);
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
            QString error;
            if (!mJobQueue->appendFromFile(fileName, &error)) {
                QMessageBox::critical(this, "Run Job Script Error", error);
                return;
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
    VDQtBatchWizardDialog dialog(
        currentJobTemplate(), mJobQueue->jobs(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if (!mJobQueue->addJobs(dialog.jobs(), &error)) {
        QMessageBox::critical(this, QStringLiteral("Batch Wizard Error"), error);
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("Queued %1 batch job(s)").arg(dialog.jobs().size()));
    onFileJobControl();
}

void VDQtMainWindow::onFileJobControl() {
    if (!mJobControlWindow)
        mJobControlWindow = new VDQtJobControlWindow(mJobQueue, this);
    mJobControlWindow->showAndRaise();
}

void VDQtMainWindow::stopJobQueue() {
    if (!mJobQueue || !mJobQueue->isRunning()) return;
    mQueueStopRequested = true;
    statusBar()->showMessage(
        QStringLiteral("The job queue will stop after the current job."));
}

void VDQtMainWindow::abortCurrentJob() {
    if (!mJobQueue || !mJobQueue->isRunning()) return;
    mQueueAbortRequested = true;
    mQueueStopRequested = true;
    if (mActiveJobIndex >= 0)
        mJobQueue->setJobStatus(mActiveJobIndex, VDQtJobStatus::Aborting);

    // VideoExporter owns its progress dialogs while an export is active. The
    // dialogs are parented to Job Control, so cancelling them also terminates
    // the associated FFmpeg/decode loop without exposing process internals to
    // the queue model.
    const auto cancelProgressDialogs = [](QWidget *root) {
        if (!root) return;
        const QList<QProgressDialog *> dialogs =
            root->findChildren<QProgressDialog *>();
        for (QProgressDialog *dialog : dialogs) {
            if (dialog && dialog->isVisible()) dialog->cancel();
        }
    };
    cancelProgressDialogs(mJobControlWindow);
    cancelProgressDialogs(this);
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (QProgressDialog *dialog = qobject_cast<QProgressDialog *>(widget))
            if (dialog->isVisible()) dialog->cancel();
    }
    statusBar()->showMessage(QStringLiteral("Aborting the current job..."));
}

void VDQtMainWindow::runPendingJobs() {
    if (!mJobQueue || mJobQueue->isRunning() || mIsExporting
        || mJobQueue->pendingCount() <= 0)
        return;

    QString validationError;
    if (!VDQtJobQueue::validateJobs(mJobQueue->jobs(), &validationError)) {
        QMessageBox::critical(this, QStringLiteral("Unsafe Job Queue"),
                              validationError);
        return;
    }

    QList<int> unapprovedExisting;
    QStringList existingNames;
    for (int row = 0; row < mJobQueue->count(); ++row) {
        const VDQtJobState *job = mJobQueue->jobAt(row);
        if (!job || job->status != VDQtJobStatus::Pending
            || job->operation == VDQtJobOperation::VideoAnalysis
            || job->replaceExisting || job->options.outputPath.isEmpty())
            continue;
        const QFileInfo destination(job->options.outputPath);
        if (destination.exists() || destination.isSymLink()) {
            unapprovedExisting.append(row);
            existingNames.append(destination.absoluteFilePath());
        }
    }
    if (!unapprovedExisting.isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(
            mJobControlWindow ? static_cast<QWidget *>(mJobControlWindow) : this,
            QStringLiteral("Replace Existing Job Outputs?"),
            QStringLiteral(
                "%1 waiting job destination(s) already exist. If replacement is "
                "approved, each original remains untouched until its replacement "
                "has completed successfully.\n\n%2")
                .arg(unapprovedExisting.size())
                .arg(existingNames.mid(0, 10).join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) return;
        for (int row : unapprovedExisting) {
            if (answer == QMessageBox::Yes) {
                mJobQueue->setReplaceExisting(row, true);
            } else {
                mJobQueue->setJobStatus(
                    row, VDQtJobStatus::Cancelled,
                    QStringLiteral("The existing destination was not approved for replacement."));
            }
        }
    }
    if (mJobQueue->pendingCount() <= 0) return;

    mPlaybackTimer->stop();
    mAudioPlayer.stop();
    const VDQtProcessingState originalProcessing = captureProcessingState();
    mQueueStopRequested = false;
    mQueueAbortRequested = false;
    mIsExporting = true;
    mJobQueue->setRunning(true, -1);

    for (int row = 0; row < mJobQueue->count(); ++row) {
        const VDQtJobState *job = mJobQueue->jobAt(row);
        if (!job || job->status != VDQtJobStatus::Pending) continue;
        if (mQueueStopRequested) break;

        mActiveJobIndex = row;
        mQueueAbortRequested = false;
        mJobQueue->setRunning(true, row);
        mJobQueue->setJobStatus(row, VDQtJobStatus::Starting);
        mJobQueue->appendJobLog(
            row, QStringLiteral("[%1] Starting %2")
                     .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                          VDQtJobQueue::operationText(job->operation)));
        mJobQueue->setJobStatus(row, VDQtJobStatus::Running);
        QCoreApplication::processEvents();

        QString error;
        const bool succeeded = executeQueuedJob(row, &error);
        if (mQueueAbortRequested) {
            mJobQueue->setJobStatus(
                row, VDQtJobStatus::Cancelled,
                error.isEmpty() ? QStringLiteral("The job was aborted by the user.")
                                : error);
        } else if (succeeded) {
            mJobQueue->setJobStatus(row, VDQtJobStatus::Complete);
            mJobQueue->appendJobLog(
                row, QStringLiteral("[%1] Completed successfully")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
        } else {
            if (error.isEmpty()) error = QStringLiteral(
                "The operation failed. Review the application log for encoder diagnostics.");
            mJobQueue->setJobStatus(row, VDQtJobStatus::Failed, error);
            mJobQueue->appendJobLog(
                row, QStringLiteral("[%1] Failed: %2")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODate), error));
        }
        mJobQueue->flush(nullptr);
        QCoreApplication::processEvents();
        if (mQueueStopRequested) break;
    }

    mActiveJobIndex = -1;
    mJobQueue->setRunning(false);
    mIsExporting = false;
    mQueueAbortRequested = false;
    mQueueStopRequested = false;
    applyProcessingState(originalProcessing);
    mJobQueue->flush(nullptr);
    statusBar()->showMessage(QStringLiteral("Job queue run finished"));
    if (mCloseAfterQueueStops) {
        mCloseAfterQueueStops = false;
        QTimer::singleShot(0, this, &QWidget::close);
    }
}

bool VDQtMainWindow::executeQueuedJob(int row, QString *errorMessage) {
    const VDQtJobState *queuedJob = mJobQueue ? mJobQueue->jobAt(row) : nullptr;
    if (!queuedJob) {
        if (errorMessage) *errorMessage = QStringLiteral("The queued job no longer exists.");
        return false;
    }
    const VDQtJobState job = *queuedJob;
    if (job.sourcePaths.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("The job has no source file.");
        return false;
    }
    if (job.operation != VDQtJobOperation::VideoAnalysis) {
        const QFileInfo output(job.options.outputPath);
        if ((output.exists() || output.isSymLink()) && !job.replaceExisting) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "The destination exists and replacement was not approved.");
            return false;
        }
    }

    QStringList allQueueSources;
    for (const VDQtJobState& candidate : mJobQueue->jobs()) {
        allQueueSources.append(candidate.sourcePaths);
        if (!candidate.audioSourcePath.isEmpty())
            allQueueSources.append(candidate.audioSourcePath);
    }
    allQueueSources.removeDuplicates();
    const bool destinationExistedAtStart = !job.options.outputPath.isEmpty()
        && (QFileInfo(job.options.outputPath).exists()
            || QFileInfo(job.options.outputPath).isSymLink());
    if (!job.options.outputPath.isEmpty()) {
        const VDQtOutputSafetyReport safety = VDQtSourceSafety::evaluateOutputPath(
            job.options.outputPath, allQueueSources,
            VDQtSourceSafety::isScriptPath(job.sourcePaths.value(0))
                ? job.sourcePaths.value(0) : QString());
        if (!safety.isSafe()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "The destination aliases a queued source or cannot be safely audited.");
            return false;
        }
    }

    const auto prepareQueuedStage = [&](QString *stagePath) {
        if (!stagePath || job.options.outputPath.isEmpty()) return false;
        QTemporaryFile reservation(stagedOutputTemplate(job.options.outputPath));
        if (!reservation.open()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "A staging path could not be reserved beside the destination.");
            return false;
        }
        *stagePath = reservation.fileName();
        reservation.close();
        if (!QFile::remove(*stagePath)) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "The reserved staging path could not be prepared.");
            return false;
        }
        return true;
    };
    const auto commitQueuedStage = [&](const QString& stagePath) {
        const VDQtOutputSafetyReport safety = VDQtSourceSafety::evaluateOutputPath(
            job.options.outputPath, allQueueSources,
            VDQtSourceSafety::isScriptPath(job.sourcePaths.value(0))
                ? job.sourcePaths.value(0) : QString());
        if (!safety.isSafe()) {
            QFile::remove(stagePath);
            if (errorMessage) *errorMessage = QStringLiteral(
                "The destination became unsafe while the job was running; the staged result was discarded.");
            return false;
        }
        const QFileInfo currentDestination(job.options.outputPath);
        if ((currentDestination.exists() || currentDestination.isSymLink())
            && !destinationExistedAtStart && !job.replaceExisting) {
            QFile::remove(stagePath);
            if (errorMessage) *errorMessage = QStringLiteral(
                "A destination appeared while the job was running; it was not replaced.");
            return false;
        }
        if (!replaceWithStagedFile(stagePath, job.options.outputPath)) {
            QFile::remove(stagePath);
            if (errorMessage) *errorMessage = QStringLiteral(
                "The completed staged result could not be committed to its destination.");
            return false;
        }
        return true;
    };

    applyProcessingState(job.processing);
    QTemporaryDir timelineDirectory;
    QString inputPath = job.sourcePaths.first();
    if (job.sourcePaths.size() > 1) {
        if (!timelineDirectory.isValid()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "A temporary timeline directory could not be created.");
            return false;
        }
        inputPath = timelineDirectory.filePath(QStringLiteral("job.ffconcat"));
        const bool written = job.imageSequenceFps > 0.0
            ? writeImageSequenceManifest(inputPath, job.sourcePaths,
                                         job.imageSequenceFps, errorMessage)
            : writeConcatManifest(inputPath, job.sourcePaths, errorMessage);
        if (!written) return false;
    }
    if (!job.rawPixelFormat.isEmpty()) {
        QString materialized;
        if (!materializeRawVideo(
                job.sourcePaths.first(), job.rawPixelFormat, job.rawWidth,
                job.rawHeight, job.rawFrameRate, job.rawByteOffset,
                &materialized, errorMessage))
            return false;
        inputPath = materialized;
    }

    VDQtVideoDecoder decoder;
    decoder.setDecompressionConfig(
        job.processing.decompression.formatName,
        job.processing.decompression.colorSpace,
        job.processing.decompression.componentRange);
    decoder.setErrorMode(job.processing.decoderErrorMode.errorMode);
    if (!decoder.openFile(inputPath)) {
        if (errorMessage) {
            *errorMessage = decoder.getLastError().isEmpty()
                ? QStringLiteral("The video source could not be opened.")
                : decoder.getLastError();
        }
        return false;
    }

    VDQtVideoDecoder avsAudioDecoder;
    VDQtAudioPlayer audioPlayer;
    const bool needsAudio = job.operation == VDQtJobOperation::VideoExport
                         || job.operation == VDQtJobOperation::AudioExport;
    bool audioPrepared = !needsAudio || job.audioDisabled;
    if (needsAudio && !job.audioDisabled && !job.audioSourcePath.isEmpty()) {
        audioPrepared = audioPlayer.openFile(
            job.audioSourcePath, job.audioStreamIndex) && audioPlayer.hasAudio();
    } else if (needsAudio && !job.audioDisabled && job.audioStreamIndex >= 0
               && !decoder.isAvsNative()) {
        audioPrepared = audioPlayer.openFile(inputPath, job.audioStreamIndex)
                     && audioPlayer.hasAudio();
    } else if (needsAudio && !job.audioDisabled && decoder.isAvsNative()) {
        const AVS_VideoInfo *videoInfo = decoder.getAvsVi();
        audioPrepared = !videoInfo || !avs_has_audio(videoInfo);
        if (videoInfo && avs_has_audio(videoInfo)) {
            avsAudioDecoder.setDecompressionConfig(
                job.processing.decompression.formatName,
                job.processing.decompression.colorSpace,
                job.processing.decompression.componentRange);
            avsAudioDecoder.setErrorMode(job.processing.decoderErrorMode.errorMode);
            audioPrepared = avsAudioDecoder.openFile(inputPath)
                && audioPlayer.openAvsClip(
                    avsAudioDecoder.getAvsClip(), avsAudioDecoder.getAvsVi());
        }
    } else if (needsAudio && !job.audioDisabled) {
        audioPrepared = audioPlayer.openFile(inputPath) && audioPlayer.hasAudio();
    }

    const auto progress = [this, row](int completed, int total) {
        const double fraction = total > 0
            ? std::clamp(static_cast<double>(completed) / total, 0.0, 1.0)
            : 0.0;
        mJobQueue->setJobProgress(row, fraction);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        return !mQueueAbortRequested;
    };

    switch (job.operation) {
    case VDQtJobOperation::VideoExport: {
        if (!audioPrepared && job.options.includeAudio
            && job.options.audioMode == AudioMode_FullProcessing) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "The selected audio source could not be opened for full processing.");
            return false;
        }
        VDQtVideoExporter::ExportOptions options = job.options;
        options.inputPath = inputPath;
        options.protectedSourcePaths = allQueueSources;
        options.unattended = true;
        QString queuedStage;
        if (!prepareQueuedStage(&queuedStage)) return false;
        options.outputPath = queuedStage;
        VDQtVideoExporter exporter;
        const bool result = exporter.exportVideo(
            options, &decoder, &audioPlayer,
            mJobControlWindow ? static_cast<QWidget *>(mJobControlWindow) : this,
            nullptr, progress);
        if (!result && exporter.wasCancelled()) {
            mQueueAbortRequested = true;
            mQueueStopRequested = true;
        }
        if (!result && errorMessage && errorMessage->isEmpty())
            *errorMessage = exporter.lastError();
        if (!result) {
            QFile::remove(queuedStage);
            return false;
        }
        return commitQueuedStage(queuedStage);
    }
    case VDQtJobOperation::RawVideoExport: {
        VDQtVideoExporter::RawExportOptions options;
        options.inputPath = inputPath;
        options.protectedSourcePaths = allQueueSources;
        options.startFrame = job.options.startFrame;
        options.endFrame = job.options.endFrame;
        options.customFps = job.options.customFps;
        options.convertFpsPreserveDuration = job.options.convertFpsPreserveDuration;
        options.decimateFactor = job.options.decimateFactor;
        options.pixelFormat = job.processing.rawVideo.pixelFormat;
        options.scanlineAlignment = job.processing.rawVideo.scanlineAlignment;
        options.swapChromaPlanes = job.processing.rawVideo.swapChromaPlanes;
        options.bottomUp = job.processing.rawVideo.bottomUp;
        options.colorMatrix = job.processing.rawVideo.colorMatrix;
        options.fullRange = job.processing.rawVideo.fullRange;
        options.unattended = true;
        options.timelineSegments = job.options.timelineSegments;
        QString queuedStage;
        if (!prepareQueuedStage(&queuedStage)) return false;
        options.outputPath = queuedStage;
        VDQtVideoExporter exporter;
        const bool result = exporter.exportRawVideo(
            options, &decoder, &audioPlayer,
            mJobControlWindow ? static_cast<QWidget *>(mJobControlWindow) : this,
            progress);
        if (!result && exporter.wasCancelled()) {
            mQueueAbortRequested = true;
            mQueueStopRequested = true;
        }
        if (!result && errorMessage && errorMessage->isEmpty())
            *errorMessage = exporter.lastError();
        if (!result) {
            QFile::remove(queuedStage);
            return false;
        }
        return commitQueuedStage(queuedStage);
    }
    case VDQtJobOperation::AudioExport: {
        if (!audioPrepared || !audioPlayer.hasAudio()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "The queued source has no decodable audio stream.");
            return false;
        }
        int64_t startSample = 0;
        int64_t sampleCount = -1;
        if (job.options.endFrame >= job.options.startFrame
            && job.options.endFrame >= 0) {
            const double fps = std::max(1.0, decoder.getFps());
            double start = decoder.getFrameTimestampSeconds(job.options.startFrame);
            double end = decoder.getFrameTimestampSeconds(job.options.endFrame);
            const double duration =
                decoder.getFrameDurationSeconds(job.options.endFrame);
            if (!std::isfinite(start)) start = job.options.startFrame / fps;
            if (!std::isfinite(end)) end = job.options.endFrame / fps;
            end += std::isfinite(duration) && duration > 0.0
                ? duration : 1.0 / fps;
            startSample = std::max<int64_t>(
                0, static_cast<int64_t>(std::llround(
                       start * audioPlayer.getSampleRate())));
            sampleCount = std::max<int64_t>(
                1, static_cast<int64_t>(std::llround(
                       (end - start) * audioPlayer.getSampleRate())));
        }
        QString queuedStage;
        if (!prepareQueuedStage(&queuedStage)) return false;
        const bool result = audioPlayer.exportAudioToFile(
            queuedStage, startSample, sampleCount, progress);
        if (!result) {
            QFile::remove(queuedStage);
            if (mQueueAbortRequested && errorMessage)
                *errorMessage = QStringLiteral("Audio export was aborted.");
            return false;
        }
        return commitQueuedStage(queuedStage);
    }
    case VDQtJobOperation::ImageSequenceExport: {
        VDQtJobState mutableJob = job;
        return executeImageSequenceJob(mutableJob, decoder, errorMessage);
    }
    case VDQtJobOperation::VideoAnalysis: {
        const VDQtVideoDecoder::VDScanResult scan = decoder.scanVideoStream(progress);
        if (scan.cancelled) {
            if (errorMessage) *errorMessage = QStringLiteral("Video analysis was aborted.");
            return false;
        }
        if (!scan.errorMessage.isEmpty()) {
            if (errorMessage) *errorMessage = scan.errorMessage;
            return false;
        }
        mJobQueue->appendJobLog(
            row, QStringLiteral(
                     "Analysis indexed %1 frame(s); %2 bad and %3 masked frame(s); %4 key frame(s).")
                     .arg(scan.totalFrames).arg(scan.badFrames)
                     .arg(scan.maskedFrames).arg(scan.keyFrames));
        return true;
    }
    }
    if (errorMessage) *errorMessage = QStringLiteral("Unknown queued operation.");
    return false;
}

bool VDQtMainWindow::executeImageSequenceJob(
    VDQtJobState& job,
    VDQtVideoDecoder& decoder,
    QString *errorMessage) {
    int totalFrames = decoder.getFrameCount();
    if (!decoder.isAvsNative()) {
        const VDQtVideoDecoder::VDScanResult scan = decoder.scanVideoStream(
            [this](int completed, int total) {
                if (mActiveJobIndex >= 0) {
                    const double fraction = total > 0
                        ? 0.1 * std::clamp(
                              static_cast<double>(completed) / total, 0.0, 1.0)
                        : 0.0;
                    mJobQueue->setJobProgress(mActiveJobIndex, fraction);
                }
                QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
                return !mQueueAbortRequested;
            });
        if (scan.cancelled) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "Image-sequence indexing was aborted.");
            return false;
        }
        if (!scan.errorMessage.isEmpty()) {
            if (errorMessage) *errorMessage = scan.errorMessage;
            return false;
        }
        totalFrames = decoder.getFrameCount();
    }
    if (totalFrames <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The source has no decodable video frames.");
        return false;
    }

    VDQtTimeline timeline;
    timeline.reset(totalFrames, true);
    if (!job.options.timelineSegments.isEmpty()
        && !timeline.replaceSegments(job.options.timelineSegments, errorMessage, true))
        return false;
    const int timelineFrames = static_cast<int>(timeline.frameCount());
    const bool explicitRange = job.options.endFrame >= job.options.startFrame
                            && job.options.endFrame >= 0;
    if (timelineFrames <= 0 || (explicitRange
        && (job.options.startFrame < 0 || job.options.startFrame >= timelineFrames))) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The requested image-sequence range is outside the source timeline.");
        return false;
    }
    const int first = explicitRange ? job.options.startFrame : 0;
    const int last = explicitRange
        ? std::min(job.options.endFrame, timelineFrames - 1)
        : timelineFrames - 1;
    const VDFilterTimingInfo timing =
        VDQtFilterSystem::instance().getTimingInfo();
    if (!timing.sequenceSupported || timing.outputFramesPerInput <= 0
        || last - first + 1 > std::numeric_limits<int>::max()
                              / timing.outputFramesPerInput) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The active temporal filter chain has an unsupported output size.");
        return false;
    }
    const int outputCount =
        (last - first + 1) * timing.outputFramesPerInput;
    const QFileInfo baseInfo(job.options.outputPath);
    const QString directory = baseInfo.absolutePath();
    const QString baseName = baseInfo.completeBaseName().isEmpty()
        ? QStringLiteral("frame") : baseInfo.completeBaseName();
    QString extension = job.imageExtension.trimmed().toLower();
    if (extension.isEmpty()) extension = baseInfo.suffix().toLower();
    if (extension.isEmpty()) extension = QStringLiteral("png");

    QStringList allSources;
    for (const VDQtJobState& candidate : mJobQueue->jobs()) {
        allSources.append(candidate.sourcePaths);
        if (!candidate.audioSourcePath.isEmpty())
            allSources.append(candidate.audioSourcePath);
    }
    allSources.removeDuplicates();
    QStringList targets;
    QSet<QString> existingTargets;
    targets.reserve(outputCount);
    for (int index = 0; index < outputCount; ++index) {
        const QString target = QDir(directory).filePath(
            QStringLiteral("%1_%2.%3")
                .arg(baseName)
                .arg(static_cast<qint64>(job.imageStartIndex) + index,
                     std::max(1, job.imageMinimumDigits), 10, QLatin1Char('0'))
                .arg(extension));
        if (!VDQtSourceSafety::evaluateOutputPath(target, allSources).isSafe()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "A generated image path aliases a queued source or cannot be safely audited.");
            return false;
        }
        const QFileInfo targetInfo(target);
        if ((targetInfo.exists() || targetInfo.isSymLink())
            && !job.replaceExisting) {
            if (errorMessage) *errorMessage = QString(
                "Generated destination already exists and replacement was not approved:\n%1")
                .arg(target);
            return false;
        }
        if (targetInfo.exists() || targetInfo.isSymLink())
            existingTargets.insert(target);
        targets.append(target);
    }

    QTemporaryDir staging(
        QDir(directory).filePath(QStringLiteral(".virtualdub-batch-images-XXXXXX")));
    if (!staging.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "A staging directory could not be created beside the destination.");
        return false;
    }
    QStringList staged;
    staged.reserve(outputCount);
    int rendered = 0;
    for (int timelineFrame = first; timelineFrame <= last; ++timelineFrame) {
        if (mQueueAbortRequested) return false;
        const qint64 sourceFrame = timeline.mapOutputToSource(timelineFrame);
        QImage raw = decoder.getFrameImage(static_cast<int>(sourceFrame));
        if (raw.isNull()) {
            if (errorMessage) *errorMessage = QString(
                "Frame %1 could not be decoded.").arg(sourceFrame);
            return false;
        }
        QList<QImage> filtered;
        if (!VDQtFilterSystem::instance().processFrameSequence(raw, filtered)
            || filtered.size() != timing.outputFramesPerInput) {
            if (errorMessage) *errorMessage = QString(
                "The filter chain failed at timeline frame %1.").arg(timelineFrame);
            return false;
        }
        for (const QImage& image : filtered) {
            const QString path = staging.filePath(
                QStringLiteral("frame_%1.%2")
                    .arg(rendered, 8, 10, QLatin1Char('0')).arg(extension));
            if (image.isNull() || !image.save(path, nullptr, job.imageQuality)) {
                if (errorMessage) *errorMessage = QString(
                    "Image %1 could not be encoded.").arg(rendered);
                return false;
            }
            staged.append(path);
            ++rendered;
            mJobQueue->setJobProgress(
                mActiveJobIndex,
                0.1 + 0.9 * static_cast<double>(rendered) / outputCount);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            if (mQueueAbortRequested) return false;
        }
    }

    const QString backupDirectory = staging.filePath(QStringLiteral("backups"));
    if (!QDir().mkpath(backupDirectory)) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "Transactional image backups could not be prepared.");
        return false;
    }
    // Revalidate after the potentially long render. A path that appeared in
    // the meantime was never part of the user's replacement approval.
    for (const QString& target : targets) {
        if (!VDQtSourceSafety::evaluateOutputPath(target, allSources).isSafe()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "A generated image destination became unsafe while rendering; no files were changed.");
            return false;
        }
        const QFileInfo targetInfo(target);
        if ((targetInfo.exists() || targetInfo.isSymLink())
            && !existingTargets.contains(target)) {
            if (errorMessage) *errorMessage = QString(
                "A new image destination appeared while rendering; no files were changed:\n%1")
                .arg(target);
            return false;
        }
    }
    QVector<QPair<QString, QString>> backups;
    QStringList committed;
    const auto rollback = [&]() {
        bool restored = true;
        for (auto it = committed.crbegin(); it != committed.crend(); ++it)
            restored = QFile::remove(*it) && restored;
        for (auto it = backups.crbegin(); it != backups.crend(); ++it)
            restored = QFile::rename(it->second, it->first) && restored;
        return restored;
    };
    for (int index = 0; index < targets.size(); ++index) {
        const QFileInfo existing(targets.at(index));
        if (!existing.exists() && !existing.isSymLink()) continue;
        const QString backup =
            QDir(backupDirectory).filePath(QString::number(index));
        QFile::setPermissions(staged.at(index), existing.permissions());
        if (!QFile::rename(targets.at(index), backup)) {
            rollback();
            if (errorMessage) *errorMessage = QStringLiteral(
                "An existing image could not be backed up; no new sequence was committed.");
            return false;
        }
        backups.append(qMakePair(targets.at(index), backup));
    }
    for (int index = 0; index < targets.size(); ++index) {
        if (!QFile::rename(staged.at(index), targets.at(index))) {
            const bool restored = rollback();
            if (errorMessage) *errorMessage = restored
                ? QStringLiteral("The completed image sequence could not be committed; previous files were restored.")
                : QStringLiteral("The image commit failed and automatic rollback was incomplete.");
            return false;
        }
        committed.append(targets.at(index));
    }
    return true;
}

void VDQtMainWindow::reloadQueuedJob(int row) {
    if (!mJobQueue || mJobQueue->isRunning()) return;
    const VDQtJobState *job = mJobQueue->jobAt(row);
    if (!job || job->sourcePaths.isEmpty()) return;

    QString inputPath = job->sourcePaths.first();
    QString error;
    if (job->sourcePaths.size() > 1) {
        inputPath = mTimelineTempDirectory.filePath(
            QStringLiteral("reloaded-job.ffconcat"));
        const bool written = job->imageSequenceFps > 0.0
            ? writeImageSequenceManifest(
                  inputPath, job->sourcePaths, job->imageSequenceFps, &error)
            : writeConcatManifest(inputPath, job->sourcePaths, &error);
        if (!written) {
            QMessageBox::critical(this, QStringLiteral("Reload Job Error"), error);
            return;
        }
    }
    if (!job->rawPixelFormat.isEmpty()) {
        if (!materializeRawVideo(
                job->sourcePaths.first(), job->rawPixelFormat,
                job->rawWidth, job->rawHeight, job->rawFrameRate,
                job->rawByteOffset, &inputPath, &error)) {
            QMessageBox::critical(this, QStringLiteral("Reload Job Error"), error);
            return;
        }
    }
    if (!openVideoFile(inputPath)) return;
    applyProcessingState(job->processing);
    mTimelineSources = job->sourcePaths;
    mImageSequenceFps = job->imageSequenceFps;
    mRawInputPixelFormat = job->rawPixelFormat;
    mRawInputWidth = job->rawWidth;
    mRawInputHeight = job->rawHeight;
    mRawInputFrameRate = job->rawFrameRate;
    mRawInputByteOffset = job->rawByteOffset;
    if (!job->options.timelineSegments.isEmpty()) {
        if (!ensureExactFrameRange(QStringLiteral("queued edit list"))
            || !mTimeline.replaceSegments(job->options.timelineSegments, &error, true)) {
            QMessageBox::critical(
                this, QStringLiteral("Reload Job Error"),
                error.isEmpty() ? QStringLiteral("The queued edit list is invalid.") : error);
            return;
        }
        updateTimelineView(0, true);
    }
    mAudioSourcePath = job->audioSourcePath;
    mAudioStreamIndex = job->audioStreamIndex;
    mAudioDisabled = job->audioDisabled;
    if (mAudioDisabled) {
        mAudioPlayer.close();
    } else if (!mAudioSourcePath.isEmpty()) {
        mAudioPlayer.close();
        if (!mAudioPlayer.openFile(mAudioSourcePath, mAudioStreamIndex)) {
            QMessageBox::warning(
                this, QStringLiteral("Reload Job Audio"),
                QStringLiteral("The video and processing settings were restored, but the external audio source could not be opened."));
        }
    } else if (mAudioStreamIndex >= 0 && !mVideoDecoder.isAvsNative()) {
        mAudioPlayer.close();
        if (!mAudioPlayer.openFile(inputPath, mAudioStreamIndex)) {
            QMessageBox::warning(
                this, QStringLiteral("Reload Job Audio"),
                QStringLiteral("The video and processing settings were restored, but the selected embedded audio stream could not be opened."));
        }
    }
    if (job->options.endFrame >= job->options.startFrame
        && job->options.endFrame >= 0) {
        mPositionControl->SetSelection(
            job->options.startFrame,
            static_cast<qint64>(job->options.endFrame) + 1);
        mPositionControl->SetPosition(job->options.startFrame);
    } else {
        mPositionControl->SetSelection(0, 0);
        mPositionControl->SetPosition(0);
    }
    statusBar()->showMessage(
        QStringLiteral("Reloaded queued job: %1").arg(job->name));
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
        loadedOutputSafety(pipePath, mVideoDecoder, mAudioPlayer, mTimelineSources);
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
    if (mTimeline.isModified()) config.timelineSegments = mTimeline.segments();

    if (!mAudioDisabled && mAudioPlayer.hasAudio()) {
        if (!ensureExactFrameRange(QStringLiteral("frame-server audio range"))) return;
        const qint64 audioStart = config.startFrame;
        const qint64 audioEndExclusive = config.endFrame >= config.startFrame
            ? static_cast<qint64>(config.endFrame) + 1 : mTimeline.frameCount();
        QString rangeError;
        const QList<VDQtTimelineSegment> segments = mTimeline.copyRange(
            audioStart, audioEndExclusive, &rangeError);
        if (segments.isEmpty()) {
            QMessageBox::critical(this, "Frame Server Audio Error",
                                  rangeError.isEmpty()
                                      ? QStringLiteral("The audio edit range is empty.")
                                      : rangeError);
            return;
        }
        const int sampleRate = std::max(1, mAudioPlayer.getSampleRate());
        QList<QPair<int64_t, int64_t>> ranges;
        for (const VDQtTimelineSegment& segment : segments) {
            const int first = static_cast<int>(segment.sourceStartFrame);
            const int last = static_cast<int>(
                segment.sourceStartFrame + segment.frameCount - 1);
            double startSeconds = mVideoDecoder.getFrameTimestampSeconds(first);
            if (!std::isfinite(startSeconds))
                startSeconds = first / std::max(1.0, mVideoDecoder.getFps());
            double durationSeconds = segment.frameCount
                / std::max(1.0, mVideoDecoder.getFps());
            const double lastTimestamp =
                mVideoDecoder.getFrameTimestampSeconds(last);
            const double lastDuration =
                mVideoDecoder.getFrameDurationSeconds(last);
            if (std::isfinite(lastTimestamp) && std::isfinite(lastDuration)
                && lastTimestamp + lastDuration > startSeconds) {
                durationSeconds = lastTimestamp + lastDuration - startSeconds;
            }
            ranges.append({
                std::max<int64_t>(0, static_cast<int64_t>(
                    std::llround(startSeconds * sampleRate))),
                std::max<int64_t>(1, static_cast<int64_t>(
                    std::llround(durationSeconds * sampleRate)))
            });
        }
        QFile::remove(mFrameServerAudioPath);
        mFrameServerAudioPath = mTimelineTempDirectory.filePath(
            QString("frameserver_%1.wav").arg(
                QUuid::createUuid().toString(QUuid::Id128)));
        QProgressDialog audioProgress(
            QStringLiteral("Preparing frame-server audio..."),
            QStringLiteral("Cancel"), 0, 100, this);
        audioProgress.setWindowModality(Qt::WindowModal);
        audioProgress.setMinimumDuration(0);
        if (!mAudioPlayer.exportAudioRangesToFile(
                mFrameServerAudioPath, ranges,
                [&audioProgress](int current, int total) {
                    audioProgress.setRange(0, std::max(1, total));
                    audioProgress.setValue(std::clamp(current, 0,
                                                       std::max(1, total)));
                    QApplication::processEvents(QEventLoop::AllEvents, 50);
                    return !audioProgress.wasCanceled();
                })) {
            QFile::remove(mFrameServerAudioPath);
            mFrameServerAudioPath.clear();
            if (!audioProgress.wasCanceled()) {
                QMessageBox::critical(
                    this, "Frame Server Audio Error",
                    "The edited audio range could not be prepared.");
            }
            return;
        }
        config.audioPath = mFrameServerAudioPath;
    }
    QString error;
    if (!mFrameServer->start(config, &error)) {
        QFile::remove(mFrameServerAudioPath);
        mFrameServerAudioPath.clear();
        QMessageBox::critical(this, "Frame Server Error", error);
        return;
    }
    QMessageBox::information(
        this, "Frame Server Started",
        QString("The selected, filtered audio/video stream is available as an RGB NUT stream at:\n%1\n\n"
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
    if (mJobQueue && mJobQueue->isRunning()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Job Queue Running"),
            QStringLiteral("Abort the current job and close the application?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        mCloseAfterQueueStops = true;
        abortCurrentJob();
        event->ignore();
        return;
    }
    if (mJobQueue) {
        QString error;
        if (!mJobQueue->flush(&error) && !error.isEmpty())
            VDLogWindow::instance(this)->appendLog(
                QStringLiteral("[Job queue] Save failed: %1").arg(error));
    }
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
            loadedOutputSafety(
                savePath, mVideoDecoder, mAudioPlayer, mTimelineSources);
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
            VDQtJobState job = currentJobTemplate();
            job.operation = VDQtJobOperation::VideoExport;
            job.options = opts;
            job.replaceExisting = videoTarget.exists() || videoTarget.isSymLink();
            job.name = QStringLiteral("%1 - Video export")
                .arg(QFileInfo(primarySessionSourcePath()).completeBaseName());
            QString queueError;
            if (!mJobQueue->addJobs({job}, &queueError)) {
                QMessageBox::critical(this, QStringLiteral("Job Queue Error"),
                                      queueError);
                return;
            }
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
        loadedOutputSafety(
            outputPath, mVideoDecoder, mAudioPlayer, mTimelineSources);
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
    options.protectedSourcePaths = mTimelineSources;
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
    if (mTimeline.isModified()) options.timelineSegments = mTimeline.segments();

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
        loadedOutputSafety(
            outputPath, mVideoDecoder, mAudioPlayer, mTimelineSources);
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
    options.protectedSourcePaths = mTimelineSources;
    if (mTimeline.isModified()) options.timelineSegments = mTimeline.segments();

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

    int sourceFrameCount = mVideoDecoder.getFrameCount();
    if (!mVideoDecoder.isFrameCountExact()) {
        QProgressDialog indexingProgress(
            "Indexing source frames for image export...", "Cancel",
            0, sourceFrameCount > 0 ? sourceFrameCount : 0, this);
        indexingProgress.setWindowModality(Qt::WindowModal);
        indexingProgress.setMinimumDuration(0);
        const int initialEstimate = sourceFrameCount;
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
        sourceFrameCount = mVideoDecoder.getFrameCount();
        mTimeline.setSourceFrameCount(sourceFrameCount, true);
        if (mTimeline.frameCount() > 0)
            mPositionControl->SetRange(0, mTimeline.frameCount() - 1);
    }
    const int totalFrames = static_cast<int>(mTimeline.frameCount());
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
                loadedOutputSafety(
                    targetPath, mVideoDecoder, mAudioPlayer, mTimelineSources);
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

        const int sourceFrame = sourceFrameForTimelineFrame(f);
        QImage rawFrame = mVideoDecoder.getFrameImage(sourceFrame);
        if (rawFrame.isNull()) {
            renderFailed = true;
            VDLogWindow::instance(this)->appendLog(
                QString("[Export] Failed to decode image-sequence timeline frame %1 (source %2)")
                    .arg(f).arg(sourceFrame));
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
            loadedOutputSafety(
                targetPath, mVideoDecoder, mAudioPlayer, mTimelineSources);
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
    qint64 position = mPositionControl->GetPosition();
    qint64 end = mPositionControl->GetSelectionEnd();
    if (end <= position) {
        if (!ensureExactFrameRange(QStringLiteral("selection range"))) return;
        position = std::min(position, mPositionControl->GetRangeEnd());
        end = mPositionControl->GetRangeEnd() + 1;
    }
    mPositionControl->SetSelection(position, end);
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

void VDQtMainWindow::updateEditActions() {
    const bool open = mVideoDecoder.isOpen();
    const bool selection = open && mPositionControl
        && mPositionControl->hasSelection();
    if (actEditUndo) actEditUndo->setEnabled(open && mTimeline.canUndo());
    if (actEditRedo) actEditRedo->setEnabled(open && mTimeline.canRedo());
    if (actEditCut) actEditCut->setEnabled(selection);
    if (actEditCopy) actEditCopy->setEnabled(selection);
    if (actEditPaste) actEditPaste->setEnabled(open && !mTimelineClipboard.isEmpty());
    if (actEditDelete) actEditDelete->setEnabled(selection);
    if (actEditCrop) actEditCrop->setEnabled(selection);
    if (actEditReset) actEditReset->setEnabled(open && mTimeline.isModified());
}

int VDQtMainWindow::sourceFrameForTimelineFrame(qint64 timelineFrame) const {
    const qint64 mapped = mTimeline.mapOutputToSource(timelineFrame);
    return mapped >= 0 && mapped <= std::numeric_limits<int>::max()
        ? static_cast<int>(mapped) : -1;
}

void VDQtMainWindow::updateTimelineView(qint64 preferredPosition,
                                        bool clearSelection) {
    const qint64 count = mTimeline.frameCount();
    const qint64 last = std::max<qint64>(0, count - 1);
    mPositionControl->SetRange(0, last);
    if (clearSelection) mPositionControl->SetSelection(0, 0);
    const qint64 position = count > 0
        ? std::clamp(preferredPosition, qint64(0), last) : 0;
    mPositionControl->SetPosition(position);
    if (count <= 0) {
        mInputDisplay->clearDisplay();
        mOutputDisplay->clearDisplay();
        statusBar()->showMessage(QStringLiteral("The edited timeline is empty"));
    }
    updateEditActions();
}

bool VDQtMainWindow::selectedTimelineRange(
    qint64 *startFrame,
    qint64 *endFrameExclusive,
    const QString& operationLabel) {
    if (!mVideoDecoder.isOpen()) return false;
    if (!ensureExactFrameRange(operationLabel)) return false;
    qint64 start = 0;
    qint64 end = 0;
    if (!mPositionControl->GetSelection(start, end)
        || start < 0 || end <= start || end > mTimeline.frameCount()) {
        QMessageBox::information(
            this, operationLabel,
            QStringLiteral("Select a non-empty frame range first."));
        return false;
    }
    if (startFrame) *startFrame = start;
    if (endFrameExclusive) *endFrameExclusive = end;
    return true;
}

void VDQtMainWindow::onEditUndo() {
    if (!mTimeline.undo()) return;
    updateTimelineView(mPositionControl->GetPosition(), true);
    statusBar()->showMessage(QStringLiteral("Timeline edit undone"));
}

void VDQtMainWindow::onEditRedo() {
    if (!mTimeline.redo()) return;
    updateTimelineView(mPositionControl->GetPosition(), true);
    statusBar()->showMessage(QStringLiteral("Timeline edit redone"));
}

void VDQtMainWindow::onEditCopy() {
    qint64 start = 0;
    qint64 end = 0;
    if (!selectedTimelineRange(&start, &end, QStringLiteral("Copy frames"))) return;
    QString error;
    mTimelineClipboard = mTimeline.copyRange(start, end, &error);
    if (mTimelineClipboard.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Copy Frames"), error);
        return;
    }
    updateEditActions();
    statusBar()->showMessage(
        QString("Copied %1 frame(s) from the timeline").arg(end - start));
}

void VDQtMainWindow::onEditCut() {
    qint64 start = 0;
    qint64 end = 0;
    if (!selectedTimelineRange(&start, &end, QStringLiteral("Cut frames"))) return;
    QString error;
    const QList<VDQtTimelineSegment> copied =
        mTimeline.copyRange(start, end, &error);
    if (copied.isEmpty() || !mTimeline.deleteRange(start, end, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cut Frames"), error);
        return;
    }
    mTimelineClipboard = copied;
    updateTimelineView(start, true);
    statusBar()->showMessage(
        QString("Cut %1 frame(s) from the timeline").arg(end - start));
}

void VDQtMainWindow::onEditPaste() {
    if (mTimelineClipboard.isEmpty() || !mVideoDecoder.isOpen()) return;
    if (!ensureExactFrameRange(QStringLiteral("paste destination"))) return;
    qint64 insertPosition = mPositionControl->GetPosition();
    qint64 selectionStart = 0;
    qint64 selectionEnd = 0;
    const bool replaceSelection =
        mPositionControl->GetSelection(selectionStart, selectionEnd);
    if (replaceSelection) insertPosition = selectionStart;
    QString error;
    const bool changed = replaceSelection
        ? mTimeline.replaceRange(selectionStart, selectionEnd,
                                 mTimelineClipboard, &error)
        : mTimeline.insert(insertPosition, mTimelineClipboard, &error);
    if (!changed) {
        QMessageBox::warning(this, QStringLiteral("Paste Frames"), error);
        return;
    }
    qint64 pastedFrames = 0;
    for (const VDQtTimelineSegment& segment : mTimelineClipboard)
        pastedFrames += segment.frameCount;
    updateTimelineView(insertPosition, true);
    mPositionControl->SetSelection(insertPosition, insertPosition + pastedFrames);
    statusBar()->showMessage(
        QString("Pasted %1 frame(s) into the timeline").arg(pastedFrames));
}

void VDQtMainWindow::onEditDelete() {
    qint64 start = 0;
    qint64 end = 0;
    if (!selectedTimelineRange(&start, &end, QStringLiteral("Delete frames"))) return;
    QString error;
    if (!mTimeline.deleteRange(start, end, &error)) {
        QMessageBox::warning(this, QStringLiteral("Delete Frames"), error);
        return;
    }
    updateTimelineView(start, true);
    statusBar()->showMessage(
        QString("Deleted %1 frame(s) from the timeline").arg(end - start));
}

void VDQtMainWindow::onEditCropToSelection() {
    qint64 start = 0;
    qint64 end = 0;
    if (!selectedTimelineRange(&start, &end,
                               QStringLiteral("Crop to selection"))) return;
    QString error;
    if (!mTimeline.cropToRange(start, end, &error)) {
        QMessageBox::warning(this, QStringLiteral("Crop Timeline"), error);
        return;
    }
    updateTimelineView(0, true);
    statusBar()->showMessage(
        QString("Timeline cropped to %1 frame(s)").arg(end - start));
}

void VDQtMainWindow::onEditResetTimeline() {
    if (!mTimeline.isModified()) return;
    QString error;
    if (!mTimeline.resetEdits(&error)) {
        QMessageBox::warning(this, QStringLiteral("Reset Timeline"), error);
        return;
    }
    updateTimelineView(0, true);
    statusBar()->showMessage(QStringLiteral("Timeline edits reset"));
}

void VDQtMainWindow::onEditPreviousSceneChange() {
    findSceneChange(false);
}

void VDQtMainWindow::onEditNextSceneChange() {
    findSceneChange(true);
}

void VDQtMainWindow::findSceneChange(bool forward) {
    if (!mVideoDecoder.isOpen()
        || !ensureExactFrameRange(QStringLiteral("scene-change search"))) return;
    const int frameCount = static_cast<int>(mTimeline.frameCount());
    const int current = static_cast<int>(mPositionControl->GetPosition());
    if (frameCount < 2 || (forward && current + 1 >= frameCount)
        || (!forward && current <= 0)) {
        statusBar()->showMessage(QStringLiteral("No scene change in that direction"));
        return;
    }
    mPlaybackTimer->stop();
    mAudioPlayer.pause();
    const auto thumbnail = [this](int timelineFrame) {
        const int sourceFrame = sourceFrameForTimelineFrame(timelineFrame);
        if (sourceFrame < 0) return QImage();
        return mVideoDecoder.getFrameImage(sourceFrame)
            .scaled(96, 54, Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .convertToFormat(QImage::Format_RGB888);
    };
    const auto difference = [](const QImage& left, const QImage& right) {
        if (left.isNull() || right.isNull() || left.size() != right.size())
            return 0.0;
        quint64 sum = 0;
        for (int y = 0; y < left.height(); ++y) {
            const uchar *a = left.constScanLine(y);
            const uchar *b = right.constScanLine(y);
            for (int x = 0; x < left.width() * 3; ++x)
                sum += static_cast<quint64>(std::abs(
                    static_cast<int>(a[x]) - static_cast<int>(b[x])));
        }
        return static_cast<double>(sum)
            / (static_cast<double>(left.width()) * left.height() * 3.0 * 255.0);
    };

    const int firstBoundary = forward ? current + 1 : current;
    const int finalBoundary = forward ? frameCount - 1 : 1;
    const int steps = forward ? finalBoundary - firstBoundary + 1
                              : firstBoundary - finalBoundary + 1;
    QProgressDialog progress(
        forward ? QStringLiteral("Finding next scene change...")
                : QStringLiteral("Finding previous scene change..."),
        QStringLiteral("Cancel"), 0, std::max(1, steps), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    constexpr double kSceneDifferenceThreshold = 0.22;
    int found = -1;
    int completed = 0;
    for (int boundary = firstBoundary;
         forward ? boundary <= finalBoundary : boundary >= finalBoundary;
         boundary += forward ? 1 : -1) {
        const QImage before = thumbnail(boundary - 1);
        const QImage after = thumbnail(boundary);
        ++completed;
        progress.setValue(completed);
        QApplication::processEvents(QEventLoop::AllEvents, 25);
        if (progress.wasCanceled()) break;
        if (difference(before, after) >= kSceneDifferenceThreshold) {
            found = boundary;
            break;
        }
    }
    progress.close();
    if (found >= 0) {
        mPositionControl->SetPosition(found);
        statusBar()->showMessage(QString("Scene change at frame %1").arg(found));
    } else if (!progress.wasCanceled()) {
        statusBar()->showMessage(QStringLiteral("No scene change found"));
    }
}

bool VDQtMainWindow::ensureExactFrameRange(const QString& operationLabel) {
    if (!mVideoDecoder.isOpen())
        return true;
    if (mVideoDecoder.isFrameCountExact()) {
        mTimeline.setSourceFrameCount(mVideoDecoder.getFrameCount(), true);
        mPositionControl->SetRange(
            0, std::max<qint64>(0, mTimeline.frameCount() - 1));
        return mTimeline.frameCount() > 0;
    }

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
    mTimeline.setSourceFrameCount(exactCount, true);
    mPositionControl->SetRange(
        0, std::max<qint64>(0, mTimeline.frameCount() - 1));
    updateEditActions();
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
    if (!mVideoDecoder.isOpen()
        || !ensureExactFrameRange(QStringLiteral("selection range"))) return;
    const int frameCount = static_cast<int>(mTimeline.frameCount());
    if (frameCount <= 0) return;

    qint64 selectionStart = 0;
    qint64 selectionEnd = frameCount;
    if (!mPositionControl->GetSelection(selectionStart, selectionEnd)) {
        selectionStart = mPositionControl->GetPosition();
        selectionEnd = std::min<qint64>(frameCount, selectionStart + 1);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Select Timeline Range"));
    auto *layout = new QFormLayout(&dialog);
    auto *start = new QSpinBox(&dialog);
    auto *end = new QSpinBox(&dialog);
    auto *duration = new QLabel(&dialog);
    start->setRange(0, frameCount - 1);
    end->setRange(1, frameCount);
    start->setValue(static_cast<int>(selectionStart));
    end->setValue(static_cast<int>(selectionEnd));
    start->setSuffix(QStringLiteral("  (inclusive)"));
    end->setSuffix(QStringLiteral("  (exclusive)"));
    const auto refreshDuration = [start, end, duration, this]() {
        const int frames = std::max(0, end->value() - start->value());
        const double fps = mVideoDecoder.getFps() > 0.0
            ? mVideoDecoder.getFps() : 29.97;
        duration->setText(QString("%1 frame(s), approximately %2 seconds")
            .arg(frames).arg(frames / fps, 0, 'f', 3));
    };
    connect(start, &QSpinBox::valueChanged, &dialog,
            [refreshDuration](int) { refreshDuration(); });
    connect(end, &QSpinBox::valueChanged, &dialog,
            [refreshDuration](int) { refreshDuration(); });
    refreshDuration();
    layout->addRow(QStringLiteral("Start frame:"), start);
    layout->addRow(QStringLiteral("End frame:"), end);
    layout->addRow(QStringLiteral("Duration:"), duration);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (end->value() <= start->value()) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Invalid Range"),
                QStringLiteral("The exclusive end frame must be greater than the start frame."));
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    mPositionControl->SetSelection(start->value(), end->value());
    mPositionControl->SetPosition(start->value());
}

void VDQtMainWindow::onVideoCopySourceFrame() {
    if (mVideoDecoder.isOpen()) {
        const int sourceFrame = sourceFrameForTimelineFrame(
            mPositionControl->GetPosition());
        QImage frame = mVideoDecoder.getFrameImage(sourceFrame);
        if (!frame.isNull()) {
            QApplication::clipboard()->setImage(frame);
            statusBar()->showMessage(
                QString("Source frame %1 copied to clipboard").arg(sourceFrame));
        }
    }
}

void VDQtMainWindow::onVideoCopyOutputFrame() {
    if (mVideoDecoder.isOpen()) {
        const int sourceFrame = sourceFrameForTimelineFrame(
            mPositionControl->GetPosition());
        QImage frame = mVideoDecoder.getFrameImage(sourceFrame);
        if (!frame.isNull()) {
            QImage processed = VDQtFilterSystem::instance().processFrame(frame);
            QApplication::clipboard()->setImage(processed);
            statusBar()->showMessage(QString("Output frame %1 copied to clipboard").arg(mPositionControl->GetPosition()));
        }
    }
}

void VDQtMainWindow::onVideoCopySourceFrameNum() {
    int frame = sourceFrameForTimelineFrame(mPositionControl->GetPosition());
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
        currentFrame = mVideoDecoder.getFrameImage(sourceFrameForTimelineFrame(
            mPositionControl->GetPosition()));
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

void VDQtMainWindow::onAudioSource() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::information(this, "Audio Source",
                                 "Open a video before selecting its audio source.");
        return;
    }

    constexpr int kNoAudio = -1000;
    constexpr int kExternalAudio = -1001;
    constexpr int kAviSynthAudio = -1002;
    QStringList labels{QStringLiteral("No audio")};
    QList<int> streamIndices{kNoAudio};
    if (mVideoDecoder.isAvsNative()) {
        const AVS_VideoInfo *vi = mVideoDecoder.getAvsVi();
        if (vi && avs_has_audio(vi)) {
            labels.append(QStringLiteral("AviSynth clip audio (%1 Hz · %2 ch)")
                              .arg(vi->audio_samples_per_second)
                              .arg(vi->nchannels));
            streamIndices.append(kAviSynthAudio);
        }
    } else {
        QString probeError;
        const auto streams = VDQtAudioPlayer::probeAudioStreams(
            mVideoDecoder.getFilePath(), &probeError);
        for (const VDAudioStreamInfo& stream : streams) {
            labels.append(stream.displayName());
            streamIndices.append(stream.streamIndex);
        }
        if (streams.isEmpty() && !probeError.isEmpty()) {
            VDLogWindow::instance(this)->appendLog(
                QString("[Audio] Stream probe failed: %1").arg(probeError));
        }
    }
    labels.append(QStringLiteral("External audio file..."));
    streamIndices.append(kExternalAudio);

    int initial = 0;
    if (!mAudioDisabled) {
        if (!mAudioSourcePath.isEmpty()) {
            initial = labels.size() - 1;
        } else if (mVideoDecoder.isAvsNative() && mAudioPlayer.hasAudio()) {
            initial = streamIndices.indexOf(kAviSynthAudio);
        } else {
            const int matched = streamIndices.indexOf(mAudioStreamIndex);
            if (matched >= 0) initial = matched;
        }
    }
    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        this, QStringLiteral("Audio Source"), QStringLiteral("Use audio from:"),
        labels, std::max(0, initial), false, &accepted);
    if (!accepted) return;
    const int selection = labels.indexOf(selected);
    if (selection < 0 || selection >= streamIndices.size()) return;
    int requestedStream = streamIndices.at(selection);
    QString requestedPath;

    if (requestedStream == kExternalAudio) {
        requestedPath = QFileDialog::getOpenFileName(
            this, QStringLiteral("Select External Audio"), QString(),
            QStringLiteral("Audio and Media Files (*.wav *.flac *.mp3 *.m4a *.aac *.ogg *.opus *.ac3 *.mka *.mp4 *.mkv *.mov *.avi);;All Files (*)"));
        if (requestedPath.isEmpty()) return;
        QString probeError;
        const auto streams = VDQtAudioPlayer::probeAudioStreams(
            requestedPath, &probeError);
        if (streams.isEmpty()) {
            QMessageBox::critical(
                this, "Audio Source Error",
                probeError.isEmpty()
                    ? QStringLiteral("The selected file contains no audio streams.")
                    : QString("The selected audio source could not be inspected:\n%1")
                          .arg(probeError));
            return;
        }
        requestedStream = streams.first().streamIndex;
        if (streams.size() > 1) {
            QStringList streamLabels;
            for (const VDAudioStreamInfo& stream : streams)
                streamLabels.append(stream.displayName());
            bool streamAccepted = false;
            const QString streamName = QInputDialog::getItem(
                this, QStringLiteral("External Audio Stream"),
                QStringLiteral("Stream:"), streamLabels, 0, false,
                &streamAccepted);
            if (!streamAccepted) return;
            const int streamChoice = streamLabels.indexOf(streamName);
            if (streamChoice < 0) return;
            requestedStream = streams.at(streamChoice).streamIndex;
        }
    }

    mPlaybackTimer->stop();
    mAudioPlayer.stop();
    if (requestedStream == kNoAudio) {
        mAudioPlayer.close();
        mAvsAudioDecoder.close();
        mAudioDisabled = true;
        mAudioSourcePath.clear();
        mAudioStreamIndex = -1;
        statusBar()->showMessage(QStringLiteral("Audio disabled"));
        return;
    }

    bool opened = false;
    if (requestedStream == kAviSynthAudio) {
        mAvsAudioDecoder.close();
        mAvsAudioDecoder.setDecompressionConfig(
            mDecompressionFormatConfig.formatName,
            mDecompressionFormatConfig.colorSpace,
            mDecompressionFormatConfig.componentRange);
        mAvsAudioDecoder.setErrorMode(mDecoderErrorModeConfig.errorMode);
        opened = mAvsAudioDecoder.openFile(mVideoDecoder.getFilePath())
            && mAudioPlayer.openAvsClip(
                mAvsAudioDecoder.getAvsClip(), mAvsAudioDecoder.getAvsVi());
        mAudioSourcePath.clear();
    } else {
        const QString path = requestedPath.isEmpty()
            ? mVideoDecoder.getFilePath() : requestedPath;
        opened = mAudioPlayer.openFile(path, requestedStream)
            && mAudioPlayer.hasAudio();
        mAudioSourcePath = requestedPath.isEmpty()
            ? QString() : QFileInfo(requestedPath).absoluteFilePath();
    }
    if (!opened) {
        mAudioDisabled = true;
        mAudioStreamIndex = -1;
        QMessageBox::critical(this, "Audio Source Error",
                              "The selected audio stream could not be decoded.");
        return;
    }
    mAudioDisabled = false;
    mAudioStreamIndex = mAudioPlayer.getSelectedStreamIndex();
    mAudioPlayer.refreshAudioFilters();
    onAudioModeFullProcessing();
    statusBar()->showMessage(
        mAudioSourcePath.isEmpty()
            ? QString("Audio stream %1 selected").arg(mAudioStreamIndex)
            : QString("External audio selected: %1")
                  .arg(QFileInfo(mAudioSourcePath).fileName()));
}

void VDQtMainWindow::onAudioModeDirectStream() {
    if (!mAudioSourcePath.isEmpty() || mAudioStreamIndex >= 0) {
        QMessageBox::information(
            this, "Audio Direct Stream Copy",
            "A selected or external audio stream is decoded through the audio graph. "
            "Choose the default embedded stream in Audio > Source before enabling direct copy.");
        actAudioDirectStream->setChecked(false);
        actAudioFullProcessing->setChecked(true);
        mAudioMode = AudioMode_FullProcessing;
        return;
    }
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

void VDQtMainWindow::onAudioFilters() {
    VDQtAudioFilterSystem& system = VDQtAudioFilterSystem::instance();
    QList<VDAudioFilterInstance> working = system.activeChain();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Audio Filters"));
    dialog.resize(680, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(&dialog);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({QStringLiteral("Enabled"),
                                      QStringLiteral("Filter")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table);

    auto *buttonRow = new QHBoxLayout;
    auto *addButton = new QPushButton(QStringLiteral("Add..."), &dialog);
    auto *removeButton = new QPushButton(QStringLiteral("Remove"), &dialog);
    auto *upButton = new QPushButton(QStringLiteral("Up"), &dialog);
    auto *downButton = new QPushButton(QStringLiteral("Down"), &dialog);
    auto *configureButton = new QPushButton(QStringLiteral("Configure..."), &dialog);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(removeButton);
    buttonRow->addWidget(upButton);
    buttonRow->addWidget(downButton);
    buttonRow->addWidget(configureButton);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    bool rebuilding = false;
    const auto rebuild = [&]() {
        rebuilding = true;
        const int selected = table->currentRow();
        table->setRowCount(working.size());
        for (int row = 0; row < working.size(); ++row) {
            auto *enabledItem = new QTableWidgetItem;
            enabledItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                  | Qt::ItemIsUserCheckable);
            enabledItem->setCheckState(working.at(row).enabled
                                           ? Qt::Checked : Qt::Unchecked);
            table->setItem(row, 0, enabledItem);
            auto *nameItem = new QTableWidgetItem(working.at(row).name);
            nameItem->setToolTip(system.availableFilters().at(
                static_cast<int>(working.at(row).type)).description);
            table->setItem(row, 1, nameItem);
        }
        if (!working.isEmpty())
            table->selectRow(std::clamp(selected, 0,
                                        static_cast<int>(working.size()) - 1));
        rebuilding = false;
    };
    const auto updateButtons = [&]() {
        const int row = table->currentRow();
        const bool valid = row >= 0 && row < working.size();
        removeButton->setEnabled(valid);
        configureButton->setEnabled(valid);
        upButton->setEnabled(valid && row > 0);
        downButton->setEnabled(valid && row + 1 < working.size());
    };

    connect(table, &QTableWidget::itemChanged, &dialog,
            [&](QTableWidgetItem *item) {
        if (rebuilding || !item || item->column() != 0) return;
        const int row = item->row();
        if (row >= 0 && row < working.size())
            working[row].enabled = item->checkState() == Qt::Checked;
    });
    connect(table, &QTableWidget::itemSelectionChanged, &dialog, updateButtons);
    connect(addButton, &QPushButton::clicked, &dialog, [&]() {
        const auto catalog = system.availableFilters();
        QStringList names;
        for (const auto& info : catalog) names.append(info.name);
        bool accepted = false;
        const QString selected = QInputDialog::getItem(
            &dialog, QStringLiteral("Add Audio Filter"),
            QStringLiteral("Filter:"), names, 0, false, &accepted);
        if (!accepted) return;
        const int index = names.indexOf(selected);
        if (index < 0 || index >= catalog.size()) return;
        working.append(system.createFilter(catalog.at(index).type));
        rebuild();
        table->selectRow(working.size() - 1);
        updateButtons();
    });
    connect(removeButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = table->currentRow();
        if (row < 0 || row >= working.size()) return;
        working.removeAt(row);
        rebuild();
        updateButtons();
    });
    connect(upButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = table->currentRow();
        if (row <= 0 || row >= working.size()) return;
        working.move(row, row - 1);
        rebuild();
        table->selectRow(row - 1);
        updateButtons();
    });
    connect(downButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = table->currentRow();
        if (row < 0 || row + 1 >= working.size()) return;
        working.move(row, row + 1);
        rebuild();
        table->selectRow(row + 1);
        updateButtons();
    });
    connect(configureButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = table->currentRow();
        if (row < 0 || row >= working.size()) return;
        VDAudioFilterInstance& filter = working[row];
        if (filter.params.isEmpty()) {
            QMessageBox::information(&dialog, QStringLiteral("Audio Filter"),
                                     QStringLiteral("This filter has no parameters."));
            return;
        }
        QDialog editor(&dialog);
        editor.setWindowTitle(QStringLiteral("Configure %1").arg(filter.name));
        auto *form = new QFormLayout(&editor);
        QMap<QString, QDoubleSpinBox *> controls;
        for (auto it = filter.params.cbegin(); it != filter.params.cend(); ++it) {
            auto *control = new QDoubleSpinBox(&editor);
            control->setDecimals(4);
            control->setRange(-1000000.0, 1000000.0);
            control->setSingleStep(0.1);
            if (it.key() == QStringLiteral("cutoffHz")
                || it.key() == QStringLiteral("sampleRate")) {
                control->setDecimals(0);
                control->setRange(10.0, 768000.0);
                control->setSingleStep(100.0);
            } else if (it.key() == QStringLiteral("decibels")) {
                control->setRange(-96.0, 24.0);
            } else if (it.key() == QStringLiteral("semitones")) {
                control->setRange(-48.0, 48.0);
            } else if (it.key() == QStringLiteral("factor")) {
                control->setRange(0.03125, 32.0);
            } else if (it.key() == QStringLiteral("mix")) {
                control->setRange(0.0, 1.0);
            } else if (it.key() == QStringLiteral("delayMs")
                       || it.key() == QStringLiteral("depthMs")) {
                control->setRange(0.0, 1000.0);
            } else if (it.key() == QStringLiteral("rateHz")) {
                control->setRange(0.01, 20.0);
            } else if (it.key() == QStringLiteral("left")
                       || it.key() == QStringLiteral("right")
                       || it.key() == QStringLiteral("crossfeed")) {
                control->setRange(-4.0, 4.0);
            }
            control->setValue(it.value());
            form->addRow(it.key(), control);
            controls.insert(it.key(), control);
        }
        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &editor);
        connect(buttons, &QDialogButtonBox::accepted, &editor, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
        form->addRow(buttons);
        if (editor.exec() != QDialog::Accepted) return;
        for (auto it = controls.cbegin(); it != controls.cend(); ++it)
            filter.params[it.key()] = it.value()->value();
    });

    auto *dialogButtons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(dialogButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(dialogButtons);
    rebuild();
    updateButtons();
    if (dialog.exec() != QDialog::Accepted) return;

    system.replaceActiveChain(working);
    mAudioPlayer.refreshAudioFilters();
    if (system.hasEnabledFilters()) onAudioModeFullProcessing();
    statusBar()->showMessage(QString("Audio filter chain updated: %1 filter(s)")
                                 .arg(working.size()));
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

void VDQtMainWindow::onToolsBackendCatalog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Backend and Plugin Catalog"));
    dialog.resize(920, 650);
    auto *layout = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs);

    const auto commandOutput = [](const QStringList& arguments) {
        QProcess process;
        process.start(QStringLiteral("ffmpeg"), arguments);
        if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
            process.kill();
            process.waitForFinished();
            return QStringLiteral("The ffmpeg backend could not be queried.\n")
                + process.errorString();
        }
        return QString::fromLocal8Bit(
            process.readAllStandardOutput() + process.readAllStandardError());
    };
    const auto addTextTab = [&](const QString& label, const QString& text) {
        auto *editor = new QPlainTextEdit(&dialog);
        editor->setReadOnly(true);
        editor->setLineWrapMode(QPlainTextEdit::NoWrap);
        editor->setPlainText(text.trimmed());
        tabs->addTab(editor, label);
    };
    addTextTab(QStringLiteral("Formats"), commandOutput(
        {QStringLiteral("-hide_banner"), QStringLiteral("-formats")}));
    addTextTab(QStringLiteral("Codecs"), commandOutput(
        {QStringLiteral("-hide_banner"), QStringLiteral("-codecs")}));
    addTextTab(QStringLiteral("Filters"), commandOutput(
        {QStringLiteral("-hide_banner"), QStringLiteral("-filters")}));
    addTextTab(QStringLiteral("Devices"), commandOutput(
        {QStringLiteral("-hide_banner"), QStringLiteral("-devices")}));

    QStringList pluginDirectories;
    const QString environmentPath = qEnvironmentVariable("AVISYNTH_PLUGIN_PATH");
    if (!environmentPath.isEmpty())
        pluginDirectories.append(environmentPath.split(QLatin1Char(':'),
                                                        Qt::SkipEmptyParts));
    pluginDirectories << QStringLiteral("/usr/lib/avisynth")
                      << QStringLiteral("/usr/local/lib/avisynth")
                      << QStringLiteral("/usr/lib64/avisynth")
                      << QStringLiteral("/usr/local/lib64/avisynth");
    pluginDirectories.removeDuplicates();
    QStringList pluginReport;
    pluginReport << QStringLiteral(
        "VirtualDubQT uses FFmpeg's registered demuxers, muxers and codecs as its "
        "native input/output plugin layer. AviSynth+ loads Linux-native shared-object "
        "plugins from its configured plugin directories. Windows .vdplugin binaries "
        "cannot be loaded into a native Linux process.\n");
    for (const QString& directoryPath : pluginDirectories) {
        QDir directory(directoryPath);
        if (!directory.exists()) continue;
        pluginReport << QStringLiteral("[%1]").arg(directory.absolutePath());
        const QStringList entries = directory.entryList(
            {QStringLiteral("*.so"), QStringLiteral("*.so.*"),
             QStringLiteral("*.avsi")}, QDir::Files, QDir::Name);
        if (entries.isEmpty()) pluginReport << QStringLiteral("  (no plugins found)");
        for (const QString& entry : entries)
            pluginReport << QStringLiteral("  %1").arg(entry);
        pluginReport << QString();
    }
    if (pluginReport.size() == 1)
        pluginReport << QStringLiteral("No AviSynth plugin directories were found.");
    addTextTab(QStringLiteral("AviSynth Plugins"), pluginReport.join(QLatin1Char('\n')));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.exec();
}

void VDQtMainWindow::onToolsSystemInformation() {
    QStringList videoCodecs;
    for (const VDVideoCodecInfo& codec
         : VDQtCodecEngine::instance().getAvailableVideoCodecs()) {
        QString availabilityError;
        const bool available = codec.id == QStringLiteral("rawvideo")
            || VDQtCodecEngine::instance().checkVideoEncoderAvailable(
                codec.id, &availabilityError);
        videoCodecs << QStringLiteral("  %1: %2")
                           .arg(codec.id,
                                available ? QStringLiteral("available")
                                          : QStringLiteral("unavailable"));
    }
    QStringList audioCodecs;
    for (const VDAudioCodecInfo& codec
         : VDQtCodecEngine::instance().getAvailableAudioCodecs()) {
        QString error;
        const bool available = VDQtCodecEngine::instance()
            .checkAudioEncoderAvailable(codec.id, &error);
        audioCodecs << QStringLiteral("  %1: %2")
                           .arg(codec.id, available ? QStringLiteral("available")
                                                   : QStringLiteral("unavailable"));
    }
    const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    const QString report = QString(
        "VirtualDubQT runtime\n"
        "  Qt: %1\n"
        "  FFmpeg: %2\n"
        "  libavformat ABI: %3\n"
        "  libavcodec ABI: %4\n\n"
        "System\n"
        "  OS: %5 %6\n"
        "  Kernel/architecture: %7 / %8\n"
        "  Logical CPU threads: %9\n"
        "  Default audio output: %10\n"
        "  ffmpeg executable: %11\n"
        "  ffprobe executable: %12\n\n"
        "Configured video encoders\n%13\n\n"
        "Configured audio encoders\n%14")
        .arg(QString::fromLatin1(qVersion()),
             QString::fromLatin1(av_version_info()))
        .arg(AV_VERSION_MAJOR(avformat_version()))
        .arg(AV_VERSION_MAJOR(avcodec_version()))
        .arg(QSysInfo::prettyProductName(), QSysInfo::productVersion(),
             QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture())
        .arg(QThread::idealThreadCount())
        .arg(outputDevice.isNull() ? QStringLiteral("none")
                                   : outputDevice.description())
        .arg(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")),
             QStandardPaths::findExecutable(QStringLiteral("ffprobe")),
             videoCodecs.join(QLatin1Char('\n')),
             audioCodecs.join(QLatin1Char('\n')));
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("System Information"));
    dialog.resize(760, 600);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setPlainText(report);
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void VDQtMainWindow::onCaptureVideo() {
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        QMessageBox::critical(this, "Capture Error",
                              "The ffmpeg executable is required for Linux capture.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Capture Video (V4L2 / ALSA)"));
    auto *form = new QFormLayout(&dialog);
    auto *videoDevice = new QComboBox(&dialog);
    videoDevice->setEditable(true);
    const QStringList devices = QDir(QStringLiteral("/dev")).entryList(
        {QStringLiteral("video*")}, QDir::System | QDir::Files,
        QDir::Name);
    for (const QString& device : devices)
        videoDevice->addItem(QDir(QStringLiteral("/dev")).filePath(device));
    if (videoDevice->count() == 0) videoDevice->addItem(QStringLiteral("/dev/video0"));
    auto *captureAudio = new QCheckBox(QStringLiteral("Capture ALSA audio"), &dialog);
    captureAudio->setChecked(true);
    auto *audioDevice = new QLineEdit(QStringLiteral("default"), &dialog);
    auto *width = new QSpinBox(&dialog);
    width->setRange(160, 7680); width->setValue(1280);
    auto *height = new QSpinBox(&dialog);
    height->setRange(120, 4320); height->setValue(720);
    auto *frameRate = new QDoubleSpinBox(&dialog);
    frameRate->setRange(1.0, 240.0); frameRate->setDecimals(3);
    frameRate->setValue(30.0);
    auto *videoCodec = new QComboBox(&dialog);
    videoCodec->addItem(QStringLiteral("H.264 (libx264)"), QStringLiteral("libx264"));
    videoCodec->addItem(QStringLiteral("H.265 (libx265)"), QStringLiteral("libx265"));
    videoCodec->addItem(QStringLiteral("FFV1 lossless"), QStringLiteral("ffv1"));
    videoCodec->addItem(QStringLiteral("Raw video"), QStringLiteral("rawvideo"));
    auto *audioCodec = new QComboBox(&dialog);
    audioCodec->addItem(QStringLiteral("AAC"), QStringLiteral("aac"));
    audioCodec->addItem(QStringLiteral("FLAC"), QStringLiteral("flac"));
    audioCodec->addItem(QStringLiteral("PCM 16-bit"), QStringLiteral("pcm_s16le"));
    auto *output = new QLineEdit(&dialog);
    auto *browse = new QPushButton(QStringLiteral("Browse..."), &dialog);
    auto *outputRow = new QHBoxLayout;
    outputRow->addWidget(output); outputRow->addWidget(browse);
    form->addRow(QStringLiteral("Video device:"), videoDevice);
    form->addRow(QString(), captureAudio);
    form->addRow(QStringLiteral("ALSA device:"), audioDevice);
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(width); sizeRow->addWidget(new QLabel(QStringLiteral("×"), &dialog));
    sizeRow->addWidget(height); sizeRow->addStretch();
    form->addRow(QStringLiteral("Frame size:"), sizeRow);
    form->addRow(QStringLiteral("Frame rate:"), frameRate);
    form->addRow(QStringLiteral("Video codec:"), videoCodec);
    form->addRow(QStringLiteral("Audio codec:"), audioCodec);
    form->addRow(QStringLiteral("Output file:"), outputRow);
    connect(captureAudio, &QCheckBox::toggled, audioDevice, &QWidget::setEnabled);
    connect(captureAudio, &QCheckBox::toggled, audioCodec, &QWidget::setEnabled);
    connect(browse, &QPushButton::clicked, &dialog, [&]() {
        const QString path = QFileDialog::getSaveFileName(
            &dialog, QStringLiteral("Capture Output"), output->text(),
            QStringLiteral("Matroska (*.mkv);;MP4 (*.mp4);;AVI (*.avi);;MOV (*.mov);;All Files (*)"));
        if (!path.isEmpty()) output->setText(path);
    });
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Start Capture"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString outputPath = QFileInfo(output->text().trimmed()).absoluteFilePath();
    if (output->text().trimmed().isEmpty()) {
        QMessageBox::critical(this, "Capture Error", "Choose an output file.");
        return;
    }
    if (QFileInfo::exists(outputPath)
        && QMessageBox::question(this, "Replace Existing Capture?",
                                 QString("Replace this file?\n%1").arg(outputPath),
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) != QMessageBox::Yes) return;
    if (mVideoDecoder.isOpen()
        && !loadedOutputSafety(
                outputPath, mVideoDecoder, mAudioPlayer, mTimelineSources).isSafe()) {
        QMessageBox::critical(this, "Unsafe Capture Path",
                              "The capture output aliases a loaded media source.");
        return;
    }
    QString encoderError;
    const QString selectedVideoCodec = videoCodec->currentData().toString();
    if (selectedVideoCodec != QStringLiteral("rawvideo")
        && !VDQtCodecEngine::instance().checkVideoEncoderAvailable(
            selectedVideoCodec, &encoderError)) {
        QMessageBox::critical(this, "Capture Encoder Error", encoderError);
        return;
    }
    const QString selectedAudioCodec = audioCodec->currentData().toString();
    if (captureAudio->isChecked()
        && !VDQtCodecEngine::instance().checkAudioEncoderAvailable(
            selectedAudioCodec, &encoderError)) {
        QMessageBox::critical(this, "Capture Encoder Error", encoderError);
        return;
    }

    QTemporaryFile staged(stagedOutputTemplate(outputPath));
    staged.setAutoRemove(true);
    if (!staged.open()) {
        QMessageBox::critical(this, "Capture Error",
                              "A staging file could not be created beside the output.");
        return;
    }
    const QString stagedPath = staged.fileName();
    staged.close();
    QStringList arguments{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("warning"),
        QStringLiteral("-thread_queue_size"), QStringLiteral("1024"),
        QStringLiteral("-f"), QStringLiteral("v4l2"),
        QStringLiteral("-framerate"), QString::number(frameRate->value(), 'f', 3),
        QStringLiteral("-video_size"),
        QString("%1x%2").arg(width->value()).arg(height->value()),
        QStringLiteral("-i"), videoDevice->currentText().trimmed()
    };
    if (captureAudio->isChecked()) {
        arguments << QStringLiteral("-thread_queue_size") << QStringLiteral("1024")
                  << QStringLiteral("-f") << QStringLiteral("alsa")
                  << QStringLiteral("-i") << audioDevice->text().trimmed();
    }
    arguments << QStringLiteral("-map") << QStringLiteral("0:v:0");
    if (captureAudio->isChecked())
        arguments << QStringLiteral("-map") << QStringLiteral("1:a:0");
    arguments << QStringLiteral("-c:v") << selectedVideoCodec;
    if (selectedVideoCodec == QStringLiteral("libx264")
        || selectedVideoCodec == QStringLiteral("libx265")) {
        arguments << QStringLiteral("-preset") << QStringLiteral("veryfast")
                  << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    }
    if (captureAudio->isChecked())
        arguments << QStringLiteral("-c:a") << selectedAudioCodec;
    else
        arguments << QStringLiteral("-an");
    arguments << QStringLiteral("-y") << stagedPath;

    QProcess capture;
    capture.setProcessChannelMode(QProcess::MergedChannels);
    capture.start(QStringLiteral("ffmpeg"), arguments, QIODevice::ReadWrite);
    if (!capture.waitForStarted(5000)) {
        QFile::remove(stagedPath);
        QMessageBox::critical(this, "Capture Error", capture.errorString());
        return;
    }
    QProgressDialog progress(
        QStringLiteral("Capturing from %1...").arg(videoDevice->currentText()),
        QStringLiteral("Stop Capture"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray diagnosticTail;
    bool stopRequested = false;
    qint64 stopRequestedAt = -1;
    while (!capture.waitForFinished(100)) {
        diagnosticTail += capture.readAll();
        if (diagnosticTail.size() > 128 * 1024)
            diagnosticTail.remove(0, diagnosticTail.size() - 128 * 1024);
        progress.setLabelText(QString("Capturing from %1...  %2 s")
                                  .arg(videoDevice->currentText())
                                  .arg(elapsed.elapsed() / 1000));
        QApplication::processEvents(QEventLoop::AllEvents, 100);
        if (progress.wasCanceled() && !stopRequested) {
            stopRequested = true;
            stopRequestedAt = elapsed.elapsed();
            capture.write("q\n");
            capture.waitForBytesWritten(250);
        }
        if (stopRequested && elapsed.elapsed() - stopRequestedAt > 2000
            && capture.state() != QProcess::NotRunning) {
            capture.terminate();
            if (!capture.waitForFinished(1500)) {
                capture.kill();
                capture.waitForFinished(1500);
            }
        }
    }
    diagnosticTail += capture.readAll();
    progress.close();
    const bool captured = capture.exitStatus() == QProcess::NormalExit
        && capture.exitCode() == 0 && QFileInfo(stagedPath).size() > 0;
    if (!captured || !replaceWithStagedFile(stagedPath, outputPath)) {
        QFile::remove(stagedPath);
        QMessageBox::critical(
            this, "Capture Error",
            QString("Capture failed.\n\n%1")
                .arg(QString::fromLocal8Bit(diagnosticTail.right(8192))));
        return;
    }
    statusBar()->showMessage(
        QString("Capture saved: %1").arg(QFileInfo(outputPath).fileName()));
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
    const int sourceFrame = sourceFrameForTimelineFrame(frameIndex);
    if (sourceFrame < 0) return;
    const double timestamp = mVideoDecoder.getFrameTimestampSeconds(sourceFrame);
    if (std::isfinite(timestamp))
        mAudioPlayer.seekToTimeSeconds(timestamp);
    else
        mAudioPlayer.seekToFrame(sourceFrame, mVideoDecoder.getFps());
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
        if (mTimeline.frameCount() > 0) {
            int target = static_cast<int>(mTimeline.frameCount() - 1);
            mPositionControl->SetRange(0, target);
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
        if (!mTimeline.sourceFrameCountExact() && mTimeline.isIdentity()) {
            const int rangeEnd = std::max(target, mVideoDecoder.getFrameCount() - 1);
            mPositionControl->SetRange(0, rangeEnd);
            mPositionControl->SetPosition(target);
        } else if (mTimeline.frameCount() > 0) {
            mPositionControl->SetPosition(std::min(
                static_cast<int>(mTimeline.frameCount() - 1), target));
        }
        break;
    }

    case VDQT_PCN_KEYPREV: // 8 - Previous Keyframe (<<K)
    {
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        const int source = sourceFrameForTimelineFrame(
            mPositionControl->GetPosition());
        const int targetSource = mVideoDecoder.getPreviousKeyFrame(source);
        const qint64 target = mTimeline.mapSourceToOutput(
            targetSource, mPositionControl->GetPosition(), false);
        if (target >= 0) mPositionControl->SetPosition(target);
        break;
    }

    case VDQT_PCN_KEYNEXT: // 9 - Next Keyframe (K>>)
    {
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        const int source = sourceFrameForTimelineFrame(
            mPositionControl->GetPosition());
        const int targetSource = mVideoDecoder.getNextKeyFrame(source);
        const qint64 target = mTimeline.mapSourceToOutput(
            targetSource, mPositionControl->GetPosition(), true);
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
        const int currentTimelineFrame =
            static_cast<int>(mPositionControl->GetPosition());
        const int currentSourceFrame =
            sourceFrameForTimelineFrame(currentTimelineFrame);
        const int targetSourceFrame = sourceFrameForTimelineFrame(targetFrame);
        if (targetSourceFrame >= 0 && currentSourceFrame >= 0
            && targetSourceFrame - currentSourceFrame
                != targetFrame - currentTimelineFrame) {
            seekAudioToVideoFrame(targetFrame);
        }
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

    const bool playing = mPlaybackTimer->isActive();
    if (playing && mFrameRequestPending) {
        // Heavy preview filters may take longer than one playback clock tick.
        // Keep only the newest requested frame without invalidating work that
        // is already nearly complete; discarded completed frames were the
        // largest source of visible preview starvation.
        mQueuedPlaybackFrame = frameIndex;
        return;
    }
    const int sourceFrame = sourceFrameForTimelineFrame(frameIndex);
    if (sourceFrame < 0) return;

    const quint64 generation = ++mFrameRequestGeneration;
    mRequestedTimelineFrame = frameIndex;
    mFrameRequestPending = true;
    mFrameDecodeWorker->requestFrame(
        sourceFrame,
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

    mFrameRequestPending = false;
    const int timelineFrame = mRequestedTimelineFrame;

    mPositionControl->SetCurrentFrameKey(keyFrame);
    mInputDisplay->setFrameImage(inputImage);
    if (!outputImage.isNull()) mOutputDisplay->setFrameImage(outputImage);

    if (frameCount > 0) {
        const bool exactFrameCount = frameCountStatus
            == static_cast<int>(VDQtVideoDecoder::FrameCountStatus::Exact);
        mTimeline.setSourceFrameCount(frameCount, exactFrameCount);
        // Provisional metadata may underestimate a stream. Keep an expanded
        // interactive range while playback is discovering frames; shrinking
        // it would clamp the slider backwards and enqueue a spurious seek on
        // every frame beyond the estimate.
        const int timelineLast = static_cast<int>(
            std::max<qint64>(0, mTimeline.frameCount() - 1));
        const int rangeEnd = exactFrameCount || mTimeline.isModified()
            ? timelineLast
            : std::max({static_cast<int>(mPositionControl->GetRangeEnd()),
                        timelineLast,
                        timelineFrame});
        if (mPositionControl->GetRangeEnd() != rangeEnd)
            mPositionControl->SetRange(0, rangeEnd);
    }

    const double fps = mVideoDecoder.getFps();
    double timeSeconds = timestampSeconds;
    if (mTimeline.isModified() && fps > 0.0)
        timeSeconds = timelineFrame / fps;
    if (!std::isfinite(timeSeconds)) timeSeconds = (fps > 0) ? (timelineFrame / fps) : 0;
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
    const QString frameLabel = timelineFrame == frameIndex
        ? QString::number(timelineFrame)
        : QString("%1 (source %2)").arg(timelineFrame).arg(frameIndex);
    const QString timelineLast = mTimeline.frameCount() > 0
        ? QString::number(mTimeline.frameCount() - 1) : QStringLiteral("?");
    statusBar()->showMessage(QString("Frame: %1 / %2  |  Time: %3  |  %4x%5 @ %6 fps")
        .arg(frameLabel)
        .arg(mTimeline.isModified() ? timelineLast : lastFrame)
        .arg(timeStr)
        .arg(inputImage.width())
        .arg(inputImage.height())
        .arg(fps, 0, 'f', 2));

    const int queuedFrame = std::exchange(mQueuedPlaybackFrame, -1);
    if (mPlaybackTimer->isActive() && queuedFrame >= 0
        && queuedFrame != timelineFrame) {
        updateFrameDisplay(queuedFrame);
    }
}

void VDQtMainWindow::onDecodedFrameUnavailable(int frameIndex,
                                               quint64 generation,
                                               const QString& errorMessage,
                                               int frameCount,
                                               int frameCountStatus) {
    if (generation != mFrameRequestGeneration || !mVideoDecoder.isOpen()) return;

    mFrameRequestPending = false;

    const bool exact = frameCountStatus
        == static_cast<int>(VDQtVideoDecoder::FrameCountStatus::Exact);
    if (mPlaybackTimer->isActive() && exact) {
        mPlaybackTimer->stop();
        mAudioPlayer.stop();
        if (frameCount > 0) {
            mTimeline.setSourceFrameCount(frameCount, true);
            const int lastFrame = static_cast<int>(
                std::max<qint64>(0, mTimeline.frameCount() - 1));
            mPositionControl->SetRange(0, lastFrame);
            if (mPositionControl->GetPosition() != lastFrame)
                mPositionControl->SetPosition(lastFrame);
        }
    } else if (!errorMessage.isEmpty()) {
        statusBar()->showMessage(
            QString("Unable to decode frame %1: %2").arg(frameIndex).arg(errorMessage));
    }
    const int queuedFrame = std::exchange(mQueuedPlaybackFrame, -1);
    if (mPlaybackTimer->isActive() && queuedFrame >= 0)
        updateFrameDisplay(queuedFrame);
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
    mFrameRequestPending = false;
    mQueuedPlaybackFrame = -1;
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
