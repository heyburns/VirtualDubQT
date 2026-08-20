#include "VDQtMainWindow.h"
#include "VDQtSourceSafety.h"
#include "VDQtBatchWizard.h"
#include "VDQtJobControl.h"
#include "VDQtPluginHost.h"
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
#include <QProgressBar>
#include <QPixmap>
#include <QPainterPath>
#include <QSet>
#include <QRegularExpression>
#include <QDataStream>
#include <QFontDatabase>
#include <QUuid>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
        mRecoveryPath = QDir(queueDirectory).filePath(
            QStringLiteral("crash-recovery.vdqproject"));
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

    mRecoveryTimer = new QTimer(this);
    mRecoveryTimer->setInterval(30000);
    connect(mRecoveryTimer, &QTimer::timeout,
            this, &VDQtMainWindow::saveRecoverySnapshot);
    mRecoveryTimer->start();
    QTimer::singleShot(0, this, [this]() {
        if (mAutomationUnattended || mRecoveryPath.isEmpty()
            || !QFileInfo::exists(mRecoveryPath)) return;
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Recover Editing Session?"),
            QStringLiteral("VirtualDubQT found an editing session that was not "
                           "closed normally. Restore it now?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            if (loadProjectFile(mRecoveryPath)) {
                mCurrentProjectPath.clear();
                statusBar()->showMessage(
                    QStringLiteral("Recovered the previous editing session. "
                                   "Use Save Project As to keep it."));
            }
        }
        QFile::remove(mRecoveryPath);
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
    mFileMenu->addAction("Save segmented AVI...", this,
                         &VDQtMainWindow::onFileSaveSegmentedAVI);
    mFileMenu->addAction("&Save audio...", this, &VDQtMainWindow::onFileSaveAudio);
    mFileMenu->addAction("Run video analysis pass", this, &VDQtMainWindow::onFileRunAnalysisPass);

    QMenu *mExport = mFileMenu->addMenu("Export");
    mExport->addAction("Raw video...", this, &VDQtMainWindow::onFileExportRawVideo);
    mExport->addAction("Image sequence...", this, &VDQtMainWindow::onFileSaveImageSequence);
    mExport->addAction("Animated GIF...", this, &VDQtMainWindow::onFileExportAnimatedGIF);
    mExport->addAction("Animated PNG...", this, &VDQtMainWindow::onFileExportAnimatedPNG);
    mExport->addAction("Adobe Filmstrip...", this,
                       &VDQtMainWindow::onFileExportFilmstrip);
    mExport->addAction("Using external encoder set...", this,
                       &VDQtMainWindow::onFileExportViaEncoderSet);
    mFileMenu->addSeparator();

    mFileMenu->addAction("Load processing settings...", QKeySequence(Qt::CTRL | Qt::Key_L), this, &VDQtMainWindow::onFileLoadProcessingSettings);
    mFileMenu->addAction("Save processing settings...", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, &VDQtMainWindow::onFileSaveProcessingSettings);
    mFileMenu->addAction("Run script...", this, &VDQtMainWindow::onFileRunScript);
    mFileMenu->addAction("Script editor...", this,
                         &VDQtMainWindow::onFileScriptEditor);
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
    QMenu *markers = mEdit->addMenu(QStringLiteral("Markers"));
    markers->addAction(QStringLiteral("Add/remove marker at current frame"),
                       QKeySequence(Qt::CTRL | Qt::Key_M), this,
                       &VDQtMainWindow::onEditToggleMarker);
    markers->addAction(QStringLiteral("Previous marker"), this,
                       &VDQtMainWindow::onEditPreviousMarker);
    markers->addAction(QStringLiteral("Next marker"), this,
                       &VDQtMainWindow::onEditNextMarker);
    markers->addSeparator();
    markers->addAction(QStringLiteral("Clear all markers"), this,
                       &VDQtMainWindow::onEditClearMarkers);
    QMenu *timelineZoom = mEdit->addMenu(QStringLiteral("Timeline zoom"));
    timelineZoom->addAction(QStringLiteral("Zoom to selection"), this,
                            &VDQtMainWindow::onEditZoomToSelection);
    timelineZoom->addAction(QStringLiteral("Show full timeline"), this,
                            &VDQtMainWindow::onEditClearTimelineZoom);
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
    mView->addAction("Audio &Waveform...", this,
                     &VDQtMainWindow::onViewAudioWaveform);
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
    mTools->addAction("Video Histogram...", this,
                      &VDQtMainWindow::onToolsHistogram);
    mTools->addAction("Performance Profiler...", this,
                      &VDQtMainWindow::onToolsPerformanceProfiler);
    mTools->addAction("Media / RIFF Inspector...", this,
                      &VDQtMainWindow::onToolsMediaInspector);
    mTools->addAction("Hex Viewer...", this,
                      &VDQtMainWindow::onToolsHexViewer);
    mTools->addSeparator();
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

    QString error;
    if (!appendVideoSegments(additions, &error)) {
        QMessageBox::critical(this, "Append Segment Error", error);
        return;
    }
    statusBar()->showMessage(
        QString("Appended %1 segment(s); timeline now has %2 segments")
            .arg(additions.size()).arg(mTimelineSources.size()));
}

bool VDQtMainWindow::appendVideoSegments(
    const QStringList& additions, QString *errorMessage) {
    if (!mVideoDecoder.isOpen() || mTimelineSources.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "Open the first media segment before appending another segment.");
        return false;
    }
    if (!ensureExactFrameRange(QStringLiteral("timeline before append"))) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The current source could not be indexed before append.");
        return false;
    }
    for (const QString& source : mTimelineSources) {
        if (VDQtSourceSafety::isScriptPath(source)) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "Script-backed clips cannot be appended through the compressed-segment timeline.");
            return false;
        }
    }
    SegmentSignature reference;
    QString error;
    if (!probeSegmentSignature(mTimelineSources.first(), &reference, &error)) {
        if (errorMessage) *errorMessage = error;
        return false;
    }
    QStringList newTimeline = mTimelineSources;
    for (const QString& addition : additions) {
        SegmentSignature candidate;
        if (!probeSegmentSignature(addition, &candidate, &error)
            || !compatibleSegments(reference, candidate, &error)) {
            if (errorMessage) *errorMessage = QString(
                "The segment cannot be appended safely:\n%1\n\n%2")
                    .arg(addition, error);
            return false;
        }
        newTimeline.append(QFileInfo(addition).absoluteFilePath());
    }
    if (!mTimelineTempDirectory.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "A temporary timeline directory could not be created.");
        return false;
    }
    const QString manifestPath = mTimelineTempDirectory.filePath(
        QStringLiteral("timeline.ffconcat"));
    const QStringList oldTimeline = mTimelineSources;
    const QList<VDQtTimelineSegment> oldEditSegments = mTimeline.segments();
    const qint64 oldSourceFrameCount = mTimeline.sourceFrameCount();
    if (!writeConcatManifest(manifestPath, newTimeline, &error)) {
        if (errorMessage) *errorMessage = error;
        return false;
    }
    VDQtVideoDecoder validationDecoder;
    validationDecoder.setDecompressionConfig(
        mDecompressionFormatConfig.formatName,
        mDecompressionFormatConfig.colorSpace,
        mDecompressionFormatConfig.componentRange);
    validationDecoder.setErrorMode(mDecoderErrorModeConfig.errorMode);
    if (!validationDecoder.openFile(manifestPath)) {
        writeConcatManifest(manifestPath, oldTimeline, nullptr);
        if (errorMessage) *errorMessage = QString(
            "FFmpeg rejected the concatenated timeline:\n%1")
                .arg(validationDecoder.getLastError());
        return false;
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
        if (errorMessage) *errorMessage = QStringLiteral(
            "The concatenated timeline could not be opened.");
        return false;
    }
    applyProcessingState(processing);
    mTimelineSources = newTimeline;
    if (!ensureExactFrameRange(QStringLiteral("appended timeline"))) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The appended timeline could not be indexed.");
        return false;
    }
    QList<VDQtTimelineSegment> appendedEditSegments = oldEditSegments;
    const qint64 appendedFrames = mTimeline.sourceFrameCount() - oldSourceFrameCount;
    if (appendedFrames > 0)
        appendedEditSegments.append({oldSourceFrameCount, appendedFrames});
    if (!mTimeline.replaceSegments(appendedEditSegments, &error)) {
        if (errorMessage) *errorMessage = error;
        return false;
    }
    mCurrentProjectPath.clear();
    mPositionControl->SetPosition(std::min(
        oldPosition, std::max(0, static_cast<int>(mTimeline.frameCount()) - 1)));
    if (hadSelection)
        mPositionControl->SetSelection(oldSelectionStart, oldSelectionEnd);
    if (errorMessage) errorMessage->clear();
    return true;
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
    mTimelineMarkers.clear();
    refreshTimelineMarkers();
    mPositionControl->ClearZoomRange();
    mAudioSourcePath.clear();
    mAudioStreamIndex = -1;
    mAudioDisabled = false;
    if (!mRecoveryPath.isEmpty()) QFile::remove(mRecoveryPath);
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

VDQtProjectState VDQtMainWindow::captureProjectState() const {
    VDQtProjectState project;
    project.sourcePaths = mTimelineSources;
    if (project.sourcePaths.isEmpty() && mVideoDecoder.isOpen())
        project.sourcePaths = {mVideoDecoder.getFilePath()};
    if (!project.sourcePaths.isEmpty())
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
    project.zoomEnabled = mPositionControl->HasZoomRange();
    project.zoomStart = mPositionControl->GetZoomStart();
    project.zoomEnd = mPositionControl->GetZoomEnd();
    project.markers = mTimelineMarkers;
    project.sourceFrameCount = mTimeline.sourceFrameCount();
    project.timelineSegments = mTimeline.segments();
    project.processing = captureProcessingState();
    return project;
}

void VDQtMainWindow::saveRecoverySnapshot() {
    if (mAutomationUnattended || mIsExporting || !mVideoDecoder.isOpen()
        || mRecoveryPath.isEmpty()) return;
    QString error;
    if (!VDQtProjectFile::saveProject(
            mRecoveryPath, captureProjectState(), &error)) {
        VDLogWindow::instance(this)->appendLog(
            QStringLiteral("[Recovery] Session snapshot failed: %1").arg(error));
    }
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
        if (project.zoomEnabled && project.zoomStart < frameCount - 1) {
            mPositionControl->SetZoomRange(
                project.zoomStart,
                std::min<qint64>(project.zoomEnd, frameCount - 1));
        }
    }
    mTimelineMarkers.clear();
    for (qint64 marker : project.markers) {
        if (marker >= 0 && marker < mTimeline.sourceFrameCount())
            mTimelineMarkers.append(marker);
    }
    std::sort(mTimelineMarkers.begin(), mTimelineMarkers.end());
    mTimelineMarkers.erase(
        std::unique(mTimelineMarkers.begin(), mTimelineMarkers.end()),
        mTimelineMarkers.end());
    refreshTimelineMarkers();
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
    const VDQtProjectState project = captureProjectState();
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
    mPlaybackTimer->stop();
    mAudioPlayer.pause();
    if (!ensureExactFrameRange(QStringLiteral("video analysis pass"))) return;
    const int frameCount = static_cast<int>(mTimeline.frameCount());
    if (frameCount <= 0) {
        QMessageBox::warning(this, "Video Analysis", "The timeline contains no frames.");
        return;
    }

    const VDFilterTimingInfo timing = VDQtFilterSystem::instance().getTimingInfo();
    if (!timing.sequenceSupported || timing.outputFramesPerInput <= 0) {
        QMessageBox::critical(this, "Video Analysis",
                              "The active temporal filter chain cannot be analyzed.");
        return;
    }

    QProgressDialog progress("Running the complete processing chain...", "Cancel",
                             0, frameCount, this);
    progress.setWindowTitle("Video analysis pass");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QElapsedTimer elapsed;
    elapsed.start();
    quint64 lumaSum = 0;
    quint64 pixelsMeasured = 0;
    int minimumLuma = 255;
    int maximumLuma = 0;
    int outputFrames = 0;
    int failedFrames = 0;
    VDQtFilterSystem::instance().resetRuntimeState();
    for (int timelineFrame = 0; timelineFrame < frameCount; ++timelineFrame) {
        if (progress.wasCanceled()) break;
        const int sourceFrame = sourceFrameForTimelineFrame(timelineFrame);
        const QImage input = mVideoDecoder.getFrameImage(sourceFrame, true);
        QList<QImage> outputs;
        VDFilterFrameContext context;
        context.frameNumber = timelineFrame;
        context.timestampSeconds =
            mVideoDecoder.getFrameTimestampSeconds(sourceFrame);
        context.frameRate = mVideoDecoder.getFps();
        if (input.isNull()
            || !VDQtFilterSystem::instance().processFrameSequence(
                input, outputs, context)
            || outputs.size() != timing.outputFramesPerInput) {
            ++failedFrames;
        } else {
            outputFrames += outputs.size();
            for (const QImage& output : outputs) {
                const QImage rgb = output.convertToFormat(QImage::Format_RGB888);
                for (int y = 0; y < rgb.height(); ++y) {
                    const uchar *row = rgb.constScanLine(y);
                    for (int x = 0; x < rgb.width(); ++x) {
                        const int luma = (77 * row[x * 3]
                                        + 150 * row[x * 3 + 1]
                                        + 29 * row[x * 3 + 2]) >> 8;
                        minimumLuma = std::min(minimumLuma, luma);
                        maximumLuma = std::max(maximumLuma, luma);
                        lumaSum += static_cast<quint64>(luma);
                    }
                }
                pixelsMeasured += static_cast<quint64>(rgb.width()) * rgb.height();
            }
        }
        progress.setValue(timelineFrame + 1);
        progress.setLabelText(
            QString("Running the complete processing chain...\nFrame %1 of %2")
                .arg(timelineFrame + 1).arg(frameCount));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    const bool cancelled = progress.wasCanceled();
    progress.close();
    VDQtFilterSystem::instance().resetRuntimeState();
    updateFrameDisplay(mPositionControl->GetPosition());
    if (cancelled) {
        statusBar()->showMessage("Video analysis pass cancelled");
        return;
    }
    const double seconds = std::max(0.001, elapsed.elapsed() / 1000.0);
    const double meanLuma = pixelsMeasured > 0
        ? static_cast<double>(lumaSum) / pixelsMeasured : 0.0;
    const QString report = QString(
        "Processing analysis complete.\n\n"
        "Input frames: %1\nOutput frames: %2\nFailed frames: %3\n"
        "Output luma range: %4-%5 (mean %6)\n"
        "Processing speed: %7 input frames/second")
        .arg(frameCount).arg(outputFrames).arg(failedFrames)
        .arg(pixelsMeasured ? minimumLuma : 0)
        .arg(pixelsMeasured ? maximumLuma : 0)
        .arg(meanLuma, 0, 'f', 2)
        .arg(frameCount / seconds, 0, 'f', 2);
    VDLogWindow::instance(this)->appendLog(
        QStringLiteral("[Analysis] %1").arg(report.simplified()));
    statusBar()->showMessage(
        QString("Analysis complete: %1 output frames, %2 failures")
            .arg(outputFrames).arg(failedFrames));
    QMessageBox::information(this, "Video Analysis", report);
}

bool VDQtMainWindow::runAutomationScript(const QString& scriptPath,
                                         QString *errorMessage) {
    VDQtScriptProgram program;
    if (!VDQtScriptEngine::parseFile(scriptPath, &program, errorMessage))
        return false;
    return executeAutomationProgram(program, errorMessage);
}

bool VDQtMainWindow::runAutomationText(const QString& scriptText,
                                       const QString& baseDirectory,
                                       QString *errorMessage) {
    VDQtScriptProgram program;
    if (!VDQtScriptEngine::parseText(
            scriptText, baseDirectory, &program, errorMessage))
        return false;
    return executeAutomationProgram(program, errorMessage);
}

bool VDQtMainWindow::exportAutomationVideo(const QString& outputPath,
                                           QString *errorMessage,
                                           int animationLoopCount,
                                           bool animationAlpha,
                                           bool animationGrayscale) {
    if (!mVideoDecoder.isOpen()) {
        if (errorMessage) *errorMessage = QStringLiteral("No video is open.");
        return false;
    }
    mPlaybackTimer->stop();
    mAudioPlayer.stop();
    QString container = mAutomationContainerType.trimmed().toLower();
    if (container.isEmpty()) container = QFileInfo(outputPath).suffix().toLower();
    VDQtVideoExporter::ExportOptions options = currentExportOptions(
        outputPath, container,
        container.startsWith(QStringLiteral("mp4"))
            || container.startsWith(QStringLiteral("mov")));
    const QString suffix = QFileInfo(outputPath).suffix().toLower();
    if (suffix == QStringLiteral("gif")) {
        options.videoMode = VideoMode_FullProcessing;
        options.includeAudio = false;
        options.videoCodecOverride = QStringLiteral("gif");
        options.videoPixelFormatOverride = QStringLiteral("rgb8");
        options.containerType = QStringLiteral("gif");
        options.animationLoopCount = animationLoopCount;
    } else if (suffix == QStringLiteral("apng")
               || suffix == QStringLiteral("png")) {
        options.videoMode = VideoMode_FullProcessing;
        options.includeAudio = false;
        options.videoCodecOverride = QStringLiteral("apng");
        options.videoPixelFormatOverride = animationGrayscale
            ? QStringLiteral("gray")
            : (animationAlpha ? QStringLiteral("rgba")
                              : QStringLiteral("rgb24"));
        options.containerType = QStringLiteral("apng");
        options.animationLoopCount = animationLoopCount;
        options.animationAlpha = animationAlpha;
        options.animationGrayscale = animationGrayscale;
    }
    options.unattended = mAutomationUnattended;
    VDQtVideoExporter exporter;
    mIsExporting = true;
    const bool result = exporter.exportVideo(
        options, &mVideoDecoder, &mAudioPlayer,
        mAutomationUnattended ? nullptr : this);
    mIsExporting = false;
    if (!result && errorMessage) {
        *errorMessage = exporter.lastError().isEmpty()
            ? QStringLiteral("Video export failed or was cancelled.")
            : exporter.lastError();
    }
    return result;
}

bool VDQtMainWindow::exportAutomationAudio(const QString& outputPath,
                                           bool raw,
                                           QString *errorMessage) {
    if (!mAudioPlayer.hasAudio()) {
        if (errorMessage) *errorMessage = QStringLiteral("The current source has no decodable audio stream.");
        return false;
    }
    if (raw) {
        if (mVideoDecoder.isAvsNative() || mTimeline.isModified()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "Raw compressed audio cannot represent an AviSynth audio graph or an edited timeline. "
                "Use SaveWAV/SaveAudio for this source.");
            return false;
        }
        const QString sourcePath = !mAudioSourcePath.isEmpty()
            ? mAudioSourcePath : mVideoDecoder.getFilePath();
        const VDQtOutputSafetyReport safety = loadedOutputSafety(
            outputPath, mVideoDecoder, mAudioPlayer, mTimelineSources);
        if (!safety.isSafe()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "The raw-audio destination aliases a loaded source or cannot be audited safely.");
            return false;
        }
        QTemporaryFile staged(stagedOutputTemplate(outputPath));
        if (!staged.open()) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "A temporary output could not be created beside the destination.");
            return false;
        }
        const QString stagedPath = staged.fileName();
        staged.close();
        QStringList arguments{
            QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
            QStringLiteral("-loglevel"), QStringLiteral("error")
        };
        if (mPositionControl->hasSelection()) {
            if (!ensureExactFrameRange(QStringLiteral("raw-audio selection"))) {
                QFile::remove(stagedPath);
                return false;
            }
            const int first = static_cast<int>(
                mPositionControl->GetSelectionStart());
            const int last = static_cast<int>(
                std::min<qint64>(mPositionControl->GetSelectionEnd() - 1,
                                 mTimeline.frameCount() - 1));
            if (first < 0 || first > last) {
                QFile::remove(stagedPath);
                if (errorMessage) *errorMessage = QStringLiteral(
                    "The raw-audio selection is empty.");
                return false;
            }
            const double fps = std::max(1.0, mVideoDecoder.getFps());
            double startSeconds = mVideoDecoder.getFrameTimestampSeconds(first);
            double endSeconds = mVideoDecoder.getFrameTimestampSeconds(last);
            double lastDuration = mVideoDecoder.getFrameDurationSeconds(last);
            if (!std::isfinite(startSeconds)) startSeconds = first / fps;
            if (!std::isfinite(endSeconds)) endSeconds = last / fps;
            if (!std::isfinite(lastDuration) || lastDuration <= 0.0)
                lastDuration = 1.0 / fps;
            arguments << QStringLiteral("-ss")
                      << QString::number(std::max(0.0, startSeconds), 'f', 9);
            arguments << QStringLiteral("-i") << sourcePath
                      << QStringLiteral("-t")
                      << QString::number(
                             std::max(1.0 / fps,
                                      endSeconds + lastDuration - startSeconds),
                             'f', 9);
        } else {
            arguments << QStringLiteral("-i") << sourcePath;
        }
        const int streamIndex = mAudioPlayer.getSelectedStreamIndex();
        arguments << QStringLiteral("-map")
                  << (streamIndex >= 0
                          ? QString("0:%1").arg(streamIndex)
                          : QStringLiteral("0:a:0"))
                  << QStringLiteral("-vn")
                  << QStringLiteral("-c:a") << QStringLiteral("copy")
                  << QStringLiteral("-y") << stagedPath;
        QProcess process;
        process.start(QStringLiteral("ffmpeg"), arguments);
        if (!process.waitForStarted(3000)) {
            QFile::remove(stagedPath);
            if (errorMessage) *errorMessage = QStringLiteral(
                "The ffmpeg executable could not be started.");
            return false;
        }
        process.waitForFinished(-1);
        const bool encoded = process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == 0 && QFileInfo(stagedPath).size() > 0;
        const bool committed = encoded
            && loadedOutputSafety(outputPath, mVideoDecoder, mAudioPlayer,
                                  mTimelineSources).isSafe()
            && replaceWithStagedFile(stagedPath, outputPath);
        if (!committed) {
            const QString diagnostics = QString::fromUtf8(
                process.readAllStandardError()).trimmed();
            QFile::remove(stagedPath);
            if (errorMessage) *errorMessage = diagnostics.isEmpty()
                ? QStringLiteral("Raw compressed-audio extraction failed.")
                : diagnostics;
            return false;
        }
        return true;
    }

    int64_t startSample = 0;
    int64_t sampleCount = -1;
    if (mPositionControl->hasSelection() && mVideoDecoder.isOpen()) {
        const int startFrame = static_cast<int>(mPositionControl->GetSelectionStart());
        const int finalFrame = std::max(
            startFrame, static_cast<int>(mPositionControl->GetSelectionEnd() - 1));
        const double fps = std::max(1.0, mVideoDecoder.getFps());
        double start = mVideoDecoder.getFrameTimestampSeconds(startFrame);
        double end = mVideoDecoder.getFrameTimestampSeconds(finalFrame);
        double duration = mVideoDecoder.getFrameDurationSeconds(finalFrame);
        if (!std::isfinite(start)) start = startFrame / fps;
        if (!std::isfinite(end)) end = finalFrame / fps;
        if (!std::isfinite(duration) || duration <= 0.0) duration = 1.0 / fps;
        startSample = std::max<int64_t>(
            0, static_cast<int64_t>(std::llround(
                   start * mAudioPlayer.getSampleRate())));
        sampleCount = std::max<int64_t>(
            1, static_cast<int64_t>(std::llround(
                   (end + duration - start) * mAudioPlayer.getSampleRate())));
    }
    if (!mAudioPlayer.exportAudioToFile(outputPath, startSample, sampleCount)) {
        if (errorMessage) *errorMessage = QStringLiteral("Audio export failed or was cancelled.");
        return false;
    }
    return true;
}

bool VDQtMainWindow::exportAutomationRawVideo(
    const QString& outputPath, const QList<QVariant>& arguments,
    QString *errorMessage) {
    if (!mVideoDecoder.isOpen()) {
        if (errorMessage) *errorMessage = QStringLiteral("No video is open.");
        return false;
    }
    static const QMap<int, QString> formats = {
        {8, QStringLiteral("bgra")},
        {9, QStringLiteral("gray")},
        {11, QStringLiteral("uyvy422")},
        {12, QStringLiteral("yuyv422")},
        {14, QStringLiteral("yuv444p")},
        {15, QStringLiteral("yuv422p")},
        {16, QStringLiteral("yuv420p")},
        {25, QStringLiteral("nv12")}
    };
    if (arguments.size() < 4) {
        if (errorMessage) *errorMessage = QStringLiteral("SaveRawVideo requires four format arguments.");
        return false;
    }
    VDQtVideoExporter::RawExportOptions options;
    const int format = static_cast<int>(arguments.at(0).toLongLong());
    options.pixelFormat = formats.value(format);
    if (options.pixelFormat.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("The requested VirtualDub raw pixel format is not mapped by this build.");
        return false;
    }
    options.inputPath = mVideoDecoder.getFilePath();
    options.outputPath = outputPath;
    options.protectedSourcePaths = mTimelineSources;
    options.scanlineAlignment = static_cast<int>(arguments.at(1).toLongLong());
    options.swapChromaPlanes = arguments.at(2).toLongLong() != 0;
    options.bottomUp = arguments.at(3).toLongLong() != 0;
    options.unattended = mAutomationUnattended;
    if (mPositionControl->hasSelection()) {
        options.startFrame = static_cast<int>(mPositionControl->GetSelectionStart());
        options.endFrame = std::max(
            options.startFrame,
            static_cast<int>(mPositionControl->GetSelectionEnd() - 1));
    } else {
        options.endFrame = -1;
    }
    if (mTimeline.isModified()) options.timelineSegments = mTimeline.segments();
    VDQtVideoExporter exporter;
    const bool result = exporter.exportRawVideo(
        options, &mVideoDecoder, &mAudioPlayer,
        mAutomationUnattended ? nullptr : this);
    if (!result && errorMessage) {
        *errorMessage = exporter.lastError().isEmpty()
            ? QStringLiteral("Raw-video export failed or was cancelled.")
            : exporter.lastError();
    }
    return result;
}

bool VDQtMainWindow::executeAutomationProgram(
    const VDQtScriptProgram& program, QString *errorMessage) {
    VDQtProcessingState processing = captureProcessingState();
    QList<VDQtTimelineSegment> subsetSegments;
    bool subsetTouched = false;
    QStringList warnings;
    QList<std::array<int, 4>> audioFilterConnections;

    const auto fail = [&](const VDQtScriptCommand& command,
                          const QString& message) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Line %1 (%2): %3")
                .arg(command.line).arg(command.name, message);
        }
        return false;
    };
    const auto requireArguments = [&](const VDQtScriptCommand& command,
                                      int minimum, int maximum) {
        return command.arguments.size() >= minimum
            && command.arguments.size() <= maximum;
    };
    const auto resolvePath = [&](const QVariant& argument) {
        const QString path = argument.toString();
        return QFileInfo(path).isAbsolute()
            ? QFileInfo(path).absoluteFilePath()
            : QFileInfo(QDir(program.baseDirectory).filePath(path)).absoluteFilePath();
    };
    const auto applySubset = [&]() -> bool {
        if (!subsetTouched) return true;
        if (!mVideoDecoder.isOpen()) {
            if (errorMessage) *errorMessage = QStringLiteral("A script edit list requires an open video.");
            return false;
        }
        if (!ensureExactFrameRange(QStringLiteral("script edit list"))) {
            if (errorMessage) *errorMessage = QStringLiteral("The source could not be indexed for the script edit list.");
            return false;
        }
        QString timelineError;
        if (!mTimeline.replaceSegments(subsetSegments, &timelineError)) {
            if (errorMessage) *errorMessage = timelineError;
            return false;
        }
        updateTimelineView(0, false);
        subsetTouched = false;
        return true;
    };
    const auto addVideoFilter = [&](const QString& name,
                                    QString *filterError) -> bool {
        const auto catalog = VDQtFilterSystem::instance().getAvailableFilters();
        const auto normalizedName = [](QString value) {
            value = value.toLower();
            value.remove(QRegularExpression(QStringLiteral("[^a-z0-9]+")));
            return value;
        };
        const QString requestedKey = normalizedName(name);
        auto found = std::find_if(catalog.cbegin(), catalog.cend(),
            [&](const VDQtFilterSystem::FilterInfo& info) {
                return normalizedName(info.name) == requestedKey;
            });
        static const QMap<QString, VDFilterType> legacyAliases = {
            {QStringLiteral("resize"), VDFilterType::Resize},
            {QStringLiteral("invert"), VDFilterType::InvertColor},
            {QStringLiteral("grayscale"), VDFilterType::Grayscale},
            {QStringLiteral("greyscale"), VDFilterType::Grayscale},
            {QStringLiteral("fliphorizontally"), VDFilterType::FlipHorizontal},
            {QStringLiteral("flipvertically"), VDFilterType::FlipVertical},
            {QStringLiteral("blur"), VDFilterType::Blur},
            {QStringLiteral("blurmore"), VDFilterType::Blur},
            {QStringLiteral("nulltransform"), VDFilterType::NullTransform},
            {QStringLiteral("rotateleft"), VDFilterType::Rotate},
            {QStringLiteral("rotateright"), VDFilterType::Rotate}
        };
        VDFilterType requestedType = VDFilterType::Count;
        QString pluginId;
        if (found != catalog.cend()) {
            requestedType = found->type;
            pluginId = found->pluginId;
        } else {
            requestedType = legacyAliases.value(requestedKey,
                                                VDFilterType::Count);
        }
        if (requestedType == VDFilterType::Count) {
            if (filterError) *filterError = QStringLiteral("Video filter '%1' is not installed.").arg(name);
            return false;
        }
        VDQtFilterSystem factory;
        if (requestedType == VDFilterType::Plugin) {
            if (!factory.addPluginFilter(pluginId)) return false;
        } else {
            factory.addFilter(requestedType);
        }
        if (factory.getActiveChain().isEmpty()) return false;
        processing.filters.append(factory.getActiveChain().first());
        return true;
    };
    const auto configureVideoFilter = [&](int index,
                                          const QList<QVariant>& values,
                                          QString *filterError) -> bool {
        if (index < 0 || index >= processing.filters.size()) {
            if (filterError) *filterError = QStringLiteral("The video filter index is out of range.");
            return false;
        }
        VDFilterInstance& filter = processing.filters[index];
        const auto integer = [&](int argument) {
            return static_cast<int>(values.value(argument).toLongLong());
        };
        switch (filter.type) {
        case VDFilterType::Resize:
            if (values.size() >= 14) {
                const bool relative = (integer(2) & 1) != 0;
                filter.params[QStringLiteral("sizeMode")] = relative ? 1 : 0;
                if (relative) {
                    filter.params[QStringLiteral("relW")] =
                        std::max(0.001, values.at(0).toDouble());
                    filter.params[QStringLiteral("relH")] =
                        std::max(0.001, values.at(1).toDouble());
                } else {
                    filter.params[QStringLiteral("width")] = std::llround(
                        std::max(1.0, values.at(0).toDouble()));
                    filter.params[QStringLiteral("height")] = std::llround(
                        std::max(1.0, values.at(1).toDouble()));
                }
                filter.params[QStringLiteral("aspectW")] =
                    values.at(3).toDouble();
                filter.params[QStringLiteral("aspectH")] =
                    values.at(4).toDouble();
                filter.params[QStringLiteral("aspectMode")] = integer(5);
                filter.params[QStringLiteral("frameW")] = integer(6);
                filter.params[QStringLiteral("frameH")] = integer(7);
                filter.params[QStringLiteral("frameAspectW")] =
                    values.at(8).toDouble();
                filter.params[QStringLiteral("frameAspectH")] =
                    values.at(9).toDouble();
                filter.params[QStringLiteral("framingMode")] = integer(10);
                int mode = integer(11);
                filter.params[QStringLiteral("interlaced")] =
                    (mode & 128) != 0;
                filter.params[QStringLiteral("filterMode")] = mode & 127;
                const quint32 color = static_cast<quint32>(
                    values.at(13).toULongLong());
                filter.params[QStringLiteral("fillColorR")] = color & 0xffU;
                filter.params[QStringLiteral("fillColorG")] = (color >> 8) & 0xffU;
                filter.params[QStringLiteral("fillColorB")] = (color >> 16) & 0xffU;
                return true;
            } else if (values.size() >= 3) {
                const double width = std::max(1.0, values.at(0).toDouble());
                const double height = std::max(1.0, values.at(1).toDouble());
                filter.params[QStringLiteral("sizeMode")] = 0;
                filter.params[QStringLiteral("absW")] = width;
                filter.params[QStringLiteral("absH")] = height;
                filter.params[QStringLiteral("width")] = std::llround(width);
                filter.params[QStringLiteral("height")] = std::llround(height);
                int mode = 4;
                if (values.at(2).typeId() == QMetaType::QString) {
                    const QString filterName = values.at(2).toString().toLower();
                    if (filterName == QStringLiteral("point")
                        || filterName == QStringLiteral("nearest")) mode = 0;
                    else if (filterName == QStringLiteral("bilinear")) mode = 1;
                    else if (filterName == QStringLiteral("bicubic")) mode = 4;
                    else {
                        if (filterError) *filterError = QStringLiteral(
                            "The resize interpolation mode is unknown.");
                        return false;
                    }
                } else {
                    mode = integer(2);
                    filter.params[QStringLiteral("interlaced")] =
                        (mode & 128) != 0;
                    mode &= 127;
                }
                filter.params[QStringLiteral("filterMode")] = mode;
                if (values.size() >= 6) {
                    const quint32 color = static_cast<quint32>(
                        values.at(5).toULongLong());
                    filter.params[QStringLiteral("framingMode")] = 1;
                    filter.params[QStringLiteral("frameW")] = integer(3);
                    filter.params[QStringLiteral("frameH")] = integer(4);
                    filter.params[QStringLiteral("fillColorR")] = color & 0xffU;
                    filter.params[QStringLiteral("fillColorG")] = (color >> 8) & 0xffU;
                    filter.params[QStringLiteral("fillColorB")] = (color >> 16) & 0xffU;
                }
                return true;
            }
            break;
        case VDFilterType::Canvas:
            if (values.size() >= 7) {
                filter.params[QStringLiteral("width")] =
                    std::max(1.0, values.at(0).toDouble());
                filter.params[QStringLiteral("height")] =
                    std::max(1.0, values.at(1).toDouble());
                filter.params[QStringLiteral("x")] = values.at(4).toDouble();
                filter.params[QStringLiteral("y")] = values.at(5).toDouble();
                const quint32 color = static_cast<quint32>(
                    values.at(6).toULongLong());
                filter.params[QStringLiteral("red")] = color & 0xffU;
                filter.params[QStringLiteral("green")] = (color >> 8) & 0xffU;
                filter.params[QStringLiteral("blue")] = (color >> 16) & 0xffU;
                return true;
            }
            break;
        case VDFilterType::Fill:
            if (values.size() >= 5) {
                const int x1 = integer(0);
                const int y1 = integer(1);
                const int x2 = integer(2);
                const int y2 = integer(3);
                const quint32 color = static_cast<quint32>(
                    values.at(4).toULongLong());
                filter.params[QStringLiteral("x")] = x1;
                filter.params[QStringLiteral("y")] = y1;
                filter.params[QStringLiteral("width")] = std::max(0, x2 - x1);
                filter.params[QStringLiteral("height")] = std::max(0, y2 - y1);
                filter.params[QStringLiteral("red")] = color & 0xffU;
                filter.params[QStringLiteral("green")] = (color >> 8) & 0xffU;
                filter.params[QStringLiteral("blue")] = (color >> 16) & 0xffU;
                return true;
            }
            break;
        case VDFilterType::Logo:
            if (values.size() >= 8) {
                filter.stringParams[QStringLiteral("path")] =
                    resolvePath(values.at(0));
                filter.params[QStringLiteral("x")] = integer(1);
                filter.params[QStringLiteral("y")] = integer(2);
                filter.params[QStringLiteral("opacity")] = std::clamp(
                    integer(7) / 65536.0, 0.0, 1.0);
                if (values.at(3).typeId() == QMetaType::QString)
                    filter.stringParams[QStringLiteral("alphaPath")] =
                        resolvePath(values.at(3));
                return true;
            }
            break;
        case VDFilterType::BrightnessContrast:
            if (values.size() >= 2) {
                filter.params[QStringLiteral("bright")] = integer(0);
                filter.params[QStringLiteral("cont")] = integer(1);
                return true;
            }
            break;
        case VDFilterType::Blur:
            if (values.size() >= 2) {
                filter.params[QStringLiteral("width")] = integer(0);
                filter.params[QStringLiteral("power")] = integer(1);
                filter.params[QStringLiteral("radius")] =
                    std::max(0, integer(0) + integer(1) - 1);
                return true;
            }
            break;
        case VDFilterType::Sharpen:
            if (!values.isEmpty()) {
                filter.params[QStringLiteral("amount")] = integer(0);
                return true;
            }
            break;
        case VDFilterType::BobDoubler:
            if (values.size() >= 2) {
                filter.params[QStringLiteral("field_order")] = integer(0) ? 1 : 0;
                filter.params[QStringLiteral("mode")] = integer(1);
                return true;
            }
            break;
        case VDFilterType::Rotate:
            if (!values.isEmpty()) {
                const int mode = integer(0);
                filter.params[QStringLiteral("mode")] = mode;
                filter.params[QStringLiteral("angle")] = mode == 0 ? 270 : mode == 1 ? 90 : 180;
                return true;
            }
            break;
        case VDFilterType::HSVAdjust:
            if (values.size() >= 3) {
                filter.params[QStringLiteral("hueDegrees")] =
                    static_cast<qint16>(integer(0)) * (360.0 / 65536.0);
                filter.params[QStringLiteral("saturation")] = integer(1) / 65536.0;
                filter.params[QStringLiteral("value")] = integer(2) / 65536.0;
                return true;
            }
            break;
        case VDFilterType::Levels:
            if (values.size() >= 5) {
                filter.params[QStringLiteral("inputBlack")] = integer(0) * (255.0 / 65535.0);
                filter.params[QStringLiteral("inputWhite")] = integer(1) * (255.0 / 65535.0);
                filter.params[QStringLiteral("gamma")] = integer(2) / 16777216.0;
                filter.params[QStringLiteral("outputBlack")] = integer(3) * (255.0 / 65535.0);
                filter.params[QStringLiteral("outputWhite")] = integer(4) * (255.0 / 65535.0);
                return true;
            }
            break;
        case VDFilterType::Threshold:
            if (!values.isEmpty()) {
                filter.params[QStringLiteral("threshold")] = integer(0);
                return true;
            }
            break;
        case VDFilterType::Smoother:
            if (!values.isEmpty()) {
                filter.params[QStringLiteral("amount")] =
                    std::clamp(integer(0) / 100.0, 0.0, 1.0);
                return true;
            }
            break;
        case VDFilterType::Plugin:
            if (filterError) *filterError = QStringLiteral(
                "This native plugin's Sylia configuration method is not exposed by the VDX serialization ABI.");
            return false;
        default:
            if (values.isEmpty()) return true;
            break;
        }
        if (filterError) *filterError = QStringLiteral(
            "The filter's Config() signature is not compatible with this implementation.");
        return false;
    };
    const auto addAudioFilter = [&](const QString& requested,
                                    QString *filterError) -> bool {
        const QString normalized = requested.trimmed().toLower();
        const auto catalog = VDQtAudioFilterSystem::instance().availableFilters();
        auto found = std::find_if(catalog.cbegin(), catalog.cend(),
            [&](const VDQtAudioFilterSystem::FilterInfo& info) {
                return info.name.compare(requested, Qt::CaseInsensitive) == 0;
            });
        VDAudioFilterType type = VDAudioFilterType::Gain;
        bool passthrough = false;
        if (found != catalog.cend()) {
            type = found->type;
        } else if (normalized == QStringLiteral("lowpass")) {
            type = VDAudioFilterType::LowPass;
        } else if (normalized == QStringLiteral("highpass")) {
            type = VDAudioFilterType::HighPass;
        } else if (normalized == QStringLiteral("stretch")) {
            type = VDAudioFilterType::TimeStretch;
        } else if (normalized == QStringLiteral("ratty pitch shift")) {
            type = VDAudioFilterType::PitchShift;
        } else if (normalized == QStringLiteral("stereo chorus")) {
            type = VDAudioFilterType::Chorus;
        } else if (normalized == QStringLiteral("new rate")) {
            type = VDAudioFilterType::Resample;
        } else if (normalized == QStringLiteral("mix")) {
            type = VDAudioFilterType::ChannelMix;
        } else if (normalized == QStringLiteral("format convert")) {
            // Sample precision is selected by the native audio codec/output
            // configuration, so the graph node itself is a no-op here.
            type = VDAudioFilterType::Gain;
            passthrough = true;
        } else if (normalized == QStringLiteral("input")
                   || normalized == QStringLiteral("output")
                   || normalized == QStringLiteral("*sink")) {
            // VirtualDub serializes graph endpoint nodes. The native pipeline
            // has implicit endpoints, so retain their indices as no-op stages.
            type = VDAudioFilterType::Gain;
            passthrough = true;
        } else {
            if (filterError) {
                *filterError = QStringLiteral("Audio filter '%1' is not installed or mapped.")
                    .arg(requested);
            }
            return false;
        }
        VDAudioFilterInstance filter =
            VDQtAudioFilterSystem::instance().createFilter(type);
        filter.name = requested;
        if (passthrough)
            filter.params[QStringLiteral("_sylia.endpoint")] = 1.0;
        processing.audioFilters.append(filter);
        return true;
    };
    const auto configureAudioFilter = [&](int index, int parameterIndex,
                                          const QVariant& value,
                                          QString *filterError) -> bool {
        if (index < 0 || index >= processing.audioFilters.size()) {
            if (filterError) *filterError = QStringLiteral(
                "The audio filter index is out of range.");
            return false;
        }
        if (parameterIndex < 0 || parameterIndex > 65535) {
            if (filterError) *filterError = QStringLiteral(
                "The audio filter parameter index is invalid.");
            return false;
        }
        bool numeric = false;
        const double number = value.toDouble(&numeric);
        if (!numeric || !std::isfinite(number)) {
            if (filterError) *filterError = QStringLiteral(
                "This native audio filter requires a numeric parameter.");
            return false;
        }
        VDAudioFilterInstance& filter = processing.audioFilters[index];
        filter.params[QStringLiteral("_sylia.config.%1").arg(parameterIndex)] =
            number;
        if (parameterIndex != 0) return true;
        const QString legacyName = filter.name.trimmed().toLower();
        switch (filter.type) {
        case VDAudioFilterType::Gain:
            if (filter.params.contains(QStringLiteral("_sylia.endpoint")))
                return true;
            filter.params[QStringLiteral("decibels")] = number > 0.0
                ? 20.0 * std::log10(number) : -96.0;
            break;
        case VDAudioFilterType::LowPass:
        case VDAudioFilterType::HighPass:
            filter.params[QStringLiteral("cutoffHz")] =
                std::clamp(number, 1.0, 384000.0);
            break;
        case VDAudioFilterType::Resample:
            filter.params[QStringLiteral("sampleRate")] =
                std::clamp(number, 1000.0, 768000.0);
            break;
        case VDAudioFilterType::PitchShift:
            if (number <= 0.0) return false;
            filter.params[QStringLiteral("semitones")] =
                12.0 * std::log2(number);
            break;
        case VDAudioFilterType::TimeStretch:
            if (number <= 0.0) return false;
            filter.params[QStringLiteral("factor")] =
                legacyName == QStringLiteral("stretch") ? 1.0 / number : number;
            break;
        default:
            break;
        }
        return true;
    };

    for (const VDQtScriptCommand& command : program.commands) {
        const QString name = command.name;
        if (name == QStringLiteral("Open")
            || name == QStringLiteral("OpenSequence")) {
            if (!requireArguments(command, 1, 4)) return fail(command, QStringLiteral("Open requires a source path."));
            applyProcessingState(processing);
            if (!openVideoFile(resolvePath(command.arguments.first())))
                return fail(command, QStringLiteral("The source could not be opened."));
        } else if (name == QStringLiteral("Append")
                   || name == QStringLiteral("AppendSequence")) {
            if (!requireArguments(command, 1, 1))
                return fail(command, QStringLiteral("Append requires a source path."));
            applyProcessingState(processing);
            QString appendError;
            if (!appendVideoSegments(
                    {resolvePath(command.arguments.first())}, &appendError))
                return fail(command, appendError);
            processing = captureProcessingState();
        } else if (name == QStringLiteral("Close")) {
            if (!applySubset()) return false;
            onFileClose();
        } else if (name == QStringLiteral("video.SetMode")) {
            if (!requireArguments(command, 1, 1)) return fail(command, QStringLiteral("SetMode requires one integer."));
            const int mode = static_cast<int>(command.arguments.first().toLongLong());
            if (mode < VideoMode_DirectStreamCopy || mode > VideoMode_FullProcessing)
                return fail(command, QStringLiteral("The video processing mode is invalid."));
            processing.videoMode = mode;
        } else if (name == QStringLiteral("audio.SetMode")) {
            if (!requireArguments(command, 1, 1)) return fail(command, QStringLiteral("SetMode requires one integer."));
            const int mode = static_cast<int>(command.arguments.first().toLongLong());
            if (mode < AudioMode_DirectStreamCopy || mode > AudioMode_FullProcessing)
                return fail(command, QStringLiteral("The audio processing mode is invalid."));
            processing.audioMode = mode;
        } else if (name == QStringLiteral("video.SetSmartRendering")) {
            if (!requireArguments(command, 1, 1)) return fail(command, QStringLiteral("SetSmartRendering requires one value."));
            processing.smartRendering = command.arguments.first().toLongLong() != 0;
        } else if (name == QStringLiteral("video.SetPreserveEmptyFrames")) {
            if (!requireArguments(command, 1, 1)) return fail(command, QStringLiteral("SetPreserveEmptyFrames requires one value."));
            processing.preserveEmptyFrames = command.arguments.first().toLongLong() != 0;
        } else if (name == QStringLiteral("video.SetFrameRate2")) {
            if (!requireArguments(command, 3, 3)) return fail(command, QStringLiteral("SetFrameRate2 requires numerator, denominator, and decimation."));
            const qint64 numerator = command.arguments.at(0).toLongLong();
            const qint64 denominator = command.arguments.at(1).toLongLong();
            const int decimation = static_cast<int>(command.arguments.at(2).toLongLong());
            processing.frameRate.sourceMode = numerator > 0 && denominator > 0 ? 1 : 0;
            processing.frameRate.customSourceFps = numerator > 0 && denominator > 0
                ? static_cast<double>(numerator) / denominator : 0.0;
            if (decimation > 1) {
                processing.frameRate.convMode = 3;
                processing.frameRate.decimateN = decimation;
            } else if (processing.frameRate.convMode == 3) {
                processing.frameRate.convMode = 0;
            }
        } else if (name == QStringLiteral("video.SetTargetFrameRate")) {
            if (!requireArguments(command, 2, 2)) return fail(command, QStringLiteral("SetTargetFrameRate requires a rational rate."));
            const qint64 numerator = command.arguments.at(0).toLongLong();
            const qint64 denominator = command.arguments.at(1).toLongLong();
            if (numerator <= 0 || denominator <= 0) return fail(command, QStringLiteral("The target frame rate is invalid."));
            processing.frameRate.convMode = 4;
            processing.frameRate.convertFps = static_cast<double>(numerator) / denominator;
        } else if (name == QStringLiteral("video.SetCompression")) {
            if (command.arguments.isEmpty()) {
                processing.videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec(
                    QStringLiteral("rawvideo"));
            } else {
                const quint32 fourcc = static_cast<quint32>(command.arguments.at(0).toULongLong());
                QByteArray tag(4, '\0');
                for (int index = 0; index < 4; ++index)
                    tag[index] = static_cast<char>((fourcc >> (index * 8)) & 0xff);
                const QByteArray upper = tag.toUpper();
                QString codec;
                if (upper == "H264" || upper == "X264" || upper == "AVC1") codec = QStringLiteral("libx264");
                else if (upper == "XVID" || upper == "DIVX" || upper == "MP4V") codec = QStringLiteral("mpeg4");
                else if (upper == "HFYU") codec = QStringLiteral("huffyuv");
                else if (upper == "FFV1") codec = QStringLiteral("ffv1");
                else if (upper == "MJPG") codec = QStringLiteral("mjpeg");
                if (codec.isEmpty()) return fail(command, QStringLiteral("The Windows FourCC codec '%1' has no native encoder mapping.").arg(QString::fromLatin1(tag)));
                processing.videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec(codec);
                if (command.arguments.size() >= 4) {
                    const qint64 dataRate = command.arguments.at(3).toLongLong();
                    if (dataRate > 0) {
                        processing.videoCodec.rateMode = QStringLiteral("bitrate");
                        processing.videoCodec.targetBitrateKbps =
                            static_cast<int>(std::min<qint64>(
                                dataRate * 8 / 1000, 1000000));
                    }
                }
            }
        } else if (name == QStringLiteral("audio.SetConversion")) {
            if (!requireArguments(command, 3, 5)) return fail(command, QStringLiteral("SetConversion requires sample rate, precision, and channels."));
            processing.audioCodec.sampleRate = static_cast<int>(command.arguments.at(0).toLongLong());
            processing.audioCodec.bitDepth = std::max(1, static_cast<int>(command.arguments.at(1).toLongLong()));
            processing.audioCodec.channels = static_cast<int>(command.arguments.at(2).toLongLong());
        } else if (name == QStringLiteral("audio.SetCompressionWithHint")) {
            if (!requireArguments(command, 6, 9)) return fail(command, QStringLiteral("SetCompressionWithHint has an invalid argument list."));
            const int tag = static_cast<int>(command.arguments.at(0).toLongLong());
            if (tag == 1) processing.audioCodec.codecId = QStringLiteral("pcm_s16le");
            else if (tag == 0x55) processing.audioCodec.codecId = QStringLiteral("libmp3lame");
            else if (tag == 0xff) processing.audioCodec.codecId = QStringLiteral("aac");
            else if (tag == 0x2000) processing.audioCodec.codecId = QStringLiteral("ac3");
            else return fail(command, QStringLiteral("The Windows audio format tag has no native encoder mapping."));
            processing.audioCodec.sampleRate = static_cast<int>(command.arguments.at(1).toLongLong());
            processing.audioCodec.channels = static_cast<int>(command.arguments.at(2).toLongLong());
            processing.audioCodec.bitDepth = std::max(1, static_cast<int>(command.arguments.at(3).toLongLong()));
            const qint64 bytesPerSecond = command.arguments.at(4).toLongLong();
            if (bytesPerSecond > 0)
                processing.audioCodec.bitrateKbps = static_cast<int>(bytesPerSecond * 8 / 1000);
        } else if (name == QStringLiteral("audio.SetCompression")) {
            if (!command.arguments.isEmpty()) return fail(command, QStringLiteral("Use SetCompressionWithHint for a specified audio format."));
            processing.audioCodec = VDAudioCodecParams{};
            processing.audioCodec.codecId = QStringLiteral("pcm_s16le");
            processing.audioCodec.rateMode = QStringLiteral("cbr");
        } else if (name == QStringLiteral("audio.SetSource")) {
            if (!requireArguments(command, 1, 3)) return fail(command, QStringLiteral("SetSource has an invalid argument list."));
            if (command.arguments.first().typeId() == QMetaType::QString) {
                const QString source = resolvePath(command.arguments.first());
                if (!mAudioPlayer.openFile(source) || !mAudioPlayer.hasAudio())
                    return fail(command, QStringLiteral("The external audio source could not be opened."));
                mAudioSourcePath = source;
                mAudioStreamIndex = mAudioPlayer.getSelectedStreamIndex();
                mAudioDisabled = false;
            } else if (command.arguments.first().toLongLong() == 0) {
                mAudioPlayer.close();
                mAudioDisabled = true;
                mAudioSourcePath.clear();
                mAudioStreamIndex = -1;
            } else if (mVideoDecoder.isOpen()) {
                const int stream = command.arguments.size() > 1
                    ? static_cast<int>(command.arguments.at(1).toLongLong()) : -1;
                if (!mAudioPlayer.openFile(mVideoDecoder.getFilePath(), stream)
                    || !mAudioPlayer.hasAudio())
                    return fail(command, QStringLiteral("The selected embedded audio stream could not be opened."));
                mAudioDisabled = false;
                mAudioSourcePath.clear();
                mAudioStreamIndex = mAudioPlayer.getSelectedStreamIndex();
            }
        } else if (name == QStringLiteral("video.filters.Clear")) {
            processing.filters.clear();
        } else if (name == QStringLiteral("video.filters.Add")) {
            if (!requireArguments(command, 1, 1)) return fail(command, QStringLiteral("Add requires a filter name."));
            QString filterError;
            if (!addVideoFilter(command.arguments.first().toString(), &filterError))
                return fail(command, filterError);
        } else if (name.startsWith(QStringLiteral("video.filters.instance["))) {
            static const QRegularExpression expression(
                QStringLiteral("^video\\.filters\\.instance\\[(\\d+)\\]\\.([A-Za-z_][A-Za-z0-9_.]*)$"));
            const auto match = expression.match(name);
            if (!match.hasMatch()) return fail(command, QStringLiteral("The filter-instance command is malformed."));
            const int index = match.captured(1).toInt();
            const QString method = match.captured(2);
            if (index < 0 || index >= processing.filters.size())
                return fail(command, QStringLiteral("The video filter index is out of range."));
            if (method == QStringLiteral("SetEnabled")) {
                if (!requireArguments(command, 1, 1)) return fail(command, QStringLiteral("SetEnabled requires one value."));
                processing.filters[index].enabled = command.arguments.first().toLongLong() != 0;
            } else if (method == QStringLiteral("Remove")) {
                if (!requireArguments(command, 0, 0))
                    return fail(command, QStringLiteral(
                        "Remove does not take arguments."));
                processing.filters.removeAt(index);
            } else if (method == QStringLiteral("Config")) {
                QString filterError;
                if (!configureVideoFilter(index, command.arguments, &filterError))
                    return fail(command, filterError);
            } else if (method == QStringLiteral("SetClipping")) {
                if (!requireArguments(command, 4, 5))
                    return fail(command, QStringLiteral(
                        "SetClipping requires four insets and an optional precision flag."));
                static const QStringList keys = {
                    QStringLiteral("left"), QStringLiteral("top"),
                    QStringLiteral("right"), QStringLiteral("bottom")
                };
                for (int argument = 0; argument < 4; ++argument) {
                    processing.filters[index].params[
                        QStringLiteral("_sylia.clip.") + keys.at(argument)] =
                        std::max<qint64>(0, command.arguments.at(argument).toLongLong());
                }
                processing.filters[index].params[
                    QStringLiteral("_sylia.clip.precise")] =
                    command.arguments.size() < 5
                        || command.arguments.at(4).toLongLong() != 0;
            } else if (method == QStringLiteral("SetOpacityClipping")) {
                if (!requireArguments(command, 4, 4))
                    return fail(command, QStringLiteral(
                        "SetOpacityClipping requires four insets."));
                static const QStringList keys = {
                    QStringLiteral("left"), QStringLiteral("top"),
                    QStringLiteral("right"), QStringLiteral("bottom")
                };
                for (int argument = 0; argument < 4; ++argument) {
                    processing.filters[index].params[
                        QStringLiteral("_sylia.opacityClip.") + keys.at(argument)] =
                        std::max<qint64>(0, command.arguments.at(argument).toLongLong());
                }
            } else if (method == QStringLiteral("SetRangeFrames")) {
                if (!requireArguments(command, 2, 2))
                    return fail(command, QStringLiteral(
                        "SetRangeFrames requires a start and exclusive end frame."));
                const qint64 start = std::max<qint64>(
                    0, command.arguments.at(0).toLongLong());
                const qint64 end = command.arguments.at(1).toLongLong();
                if (end >= 0 && end < start)
                    return fail(command, QStringLiteral(
                        "The video filter range is invalid."));
                processing.filters[index].params[
                    QStringLiteral("_sylia.range.start")] = start;
                processing.filters[index].params[
                    QStringLiteral("_sylia.range.end")] = end;
            } else if (method == QStringLiteral("AddOpacityCurve")) {
                if (!requireArguments(command, 0, 0))
                    return fail(command, QStringLiteral(
                        "AddOpacityCurve does not take arguments."));
                processing.filters[index].params[
                    QStringLiteral("_sylia.opacity.count")] = 0.0;
            } else if (method == QStringLiteral("OpacityCurve.AddPoint")) {
                if (!requireArguments(command, 3, 3))
                    return fail(command, QStringLiteral(
                        "OpacityCurve.AddPoint requires position, opacity, and interpolation values."));
                VDFilterInstance& filter = processing.filters[index];
                const int point = std::clamp(static_cast<int>(
                    filter.params.value(QStringLiteral("_sylia.opacity.count"),
                                        0.0)), 0, 4095);
                const QString prefix = QStringLiteral("_sylia.opacity.%1.")
                    .arg(point);
                filter.params[prefix + QStringLiteral("x")] =
                    command.arguments.at(0).toDouble();
                filter.params[prefix + QStringLiteral("y")] = std::clamp(
                    command.arguments.at(1).toDouble(), 0.0, 1.0);
                filter.params[prefix + QStringLiteral("linear")] =
                    command.arguments.at(2).toLongLong() != 0;
                filter.params[QStringLiteral("_sylia.opacity.count")] =
                    point + 1;
            } else if (method == QStringLiteral("SetOutputName")
                       || method == QStringLiteral("DataPrefix")) {
                if (!requireArguments(command, 1, 1))
                    return fail(command, QStringLiteral(
                        "%1 requires one string argument.").arg(method));
                processing.filters[index].stringParams[
                    method == QStringLiteral("SetOutputName")
                        ? QStringLiteral("_sylia.outputName")
                        : QStringLiteral("_sylia.dataPrefix")] =
                    command.arguments.first().toString();
            } else if (method == QStringLiteral("SetForceSingleFBEnabled")) {
                warnings << QStringLiteral("Line %1: %2 is accepted but has no native Qt equivalent.")
                    .arg(command.line).arg(method);
            } else if (method == QStringLiteral("AddInput")) {
                return fail(command, QStringLiteral(
                    "This script requires a multi-input video filter graph, which the current native pipeline cannot represent."));
            } else {
                return fail(command, QStringLiteral("This filter-instance operation is not implemented."));
            }
        } else if (name == QStringLiteral("audio.filters.Clear")) {
            processing.audioFilters.clear();
            audioFilterConnections.clear();
        } else if (name == QStringLiteral("audio.filters.Add")) {
            if (!requireArguments(command, 1, 1)) return fail(command, QStringLiteral("Add requires an audio filter name."));
            QString filterError;
            if (!addAudioFilter(command.arguments.first().toString(),
                                &filterError))
                return fail(command, filterError);
        } else if (name == QStringLiteral("audio.filters.Connect")) {
            if (!requireArguments(command, 4, 4))
                return fail(command, QStringLiteral(
                    "Connect requires source filter/pin and destination filter/pin."));
            std::array<int, 4> connection{};
            for (int argument = 0; argument < 4; ++argument)
                connection[argument] = static_cast<int>(
                    command.arguments.at(argument).toLongLong());
            if (connection[0] < 0 || connection[2] <= connection[0]
                || connection[2] >= processing.audioFilters.size()
                || connection[1] < 0 || connection[3] < 0) {
                return fail(command, QStringLiteral(
                    "The audio filter connection is invalid."));
            }
            audioFilterConnections.append(connection);
        } else if (name.startsWith(QStringLiteral("audio.filters.instance["))) {
            static const QRegularExpression expression(
                QStringLiteral("^audio\\.filters\\.instance\\[(\\d+)\\]\\.([A-Za-z_][A-Za-z0-9_]*)$"));
            const auto match = expression.match(name);
            if (!match.hasMatch())
                return fail(command, QStringLiteral(
                    "The audio filter-instance command is malformed."));
            const int index = match.captured(1).toInt();
            const QString method = match.captured(2);
            if (index < 0 || index >= processing.audioFilters.size())
                return fail(command, QStringLiteral(
                    "The audio filter index is out of range."));
            if (method == QStringLiteral("SetInt")
                || method == QStringLiteral("SetLong")
                || method == QStringLiteral("SetDouble")) {
                if (!requireArguments(command, 2, 3))
                    return fail(command, QStringLiteral(
                        "%1 has an invalid argument list.").arg(method));
                QVariant parameter = command.arguments.at(1);
                if (command.arguments.size() == 3) {
                    if (method == QStringLiteral("SetLong")) {
                        const quint64 high = static_cast<quint32>(
                            command.arguments.at(1).toULongLong());
                        const quint64 low = static_cast<quint32>(
                            command.arguments.at(2).toULongLong());
                        parameter = static_cast<qulonglong>((high << 32) | low);
                    } else if (method == QStringLiteral("SetDouble")) {
                        const quint64 high = static_cast<quint32>(
                            command.arguments.at(1).toULongLong());
                        const quint64 low = static_cast<quint32>(
                            command.arguments.at(2).toULongLong());
                        const quint64 bits = (high << 32) | low;
                        double decoded = 0.0;
                        std::memcpy(&decoded, &bits, sizeof(decoded));
                        parameter = decoded;
                    }
                }
                QString filterError;
                if (!configureAudioFilter(
                        index, static_cast<int>(
                            command.arguments.at(0).toLongLong()),
                        parameter, &filterError))
                    return fail(command, filterError);
            } else if (method == QStringLiteral("SetString")
                       || method == QStringLiteral("SetRaw")
                       || method == QStringLiteral("SetBlock")) {
                if (!requireArguments(command, 2, 3))
                    return fail(command, QStringLiteral(
                        "%1 has an invalid argument list.").arg(method));
                warnings << QStringLiteral(
                    "Line %1: string/block configuration for '%2' was retained only by the source script; the mapped native filter has no equivalent parameter type.")
                    .arg(command.line).arg(processing.audioFilters.at(index).name);
            } else {
                return fail(command, QStringLiteral(
                    "This audio filter-instance operation is not implemented."));
            }
        } else if (name == QStringLiteral("subset.Delete")) {
            subsetSegments.clear();
            subsetTouched = false;
            if (mVideoDecoder.isOpen()) {
                if (!ensureExactFrameRange(QStringLiteral("script timeline reset")))
                    return fail(command, QStringLiteral("The source could not be indexed."));
                mTimeline.reset(mVideoDecoder.getFrameCount(), true);
                updateTimelineView(0, true);
            }
        } else if (name == QStringLiteral("subset.Clear")) {
            subsetSegments.clear();
            subsetTouched = true;
        } else if (name == QStringLiteral("subset.AddRange")
                   || name == QStringLiteral("subset.AddFrame")) {
            if (!requireArguments(command, 2, 2)) return fail(command, QStringLiteral("AddRange requires start and length."));
            const qint64 start = command.arguments.at(0).toLongLong();
            const qint64 length = command.arguments.at(1).toLongLong();
            if (start < 0 || length <= 0) return fail(command, QStringLiteral("The edit-list range is invalid."));
            subsetSegments.append({start, length});
            subsetTouched = true;
        } else if (name == QStringLiteral("subset.AddMaskedRange")) {
            if (!requireArguments(command, 2, 2))
                return fail(command, QStringLiteral(
                    "AddMaskedRange requires start and length."));
            const qint64 start = command.arguments.at(0).toLongLong();
            const qint64 length = command.arguments.at(1).toLongLong();
            if (start < 0 || length <= 0)
                return fail(command, QStringLiteral(
                    "The masked edit-list range is invalid."));
            subsetSegments.append({start, length, true});
            subsetTouched = true;
        } else if (name == QStringLiteral("video.SetRangeFrames")) {
            if (!requireArguments(command, 2, 2)) return fail(command, QStringLiteral("SetRangeFrames requires two frame positions."));
            mPositionControl->SetSelection(command.arguments.at(0).toLongLong(),
                                           command.arguments.at(1).toLongLong());
        } else if (name == QStringLiteral("video.SetRange")) {
            mPositionControl->SetSelection(0, 0);
        } else if (name == QStringLiteral("video.SetZoomFrames")) {
            if (!requireArguments(command, 2, 2))
                return fail(command, QStringLiteral(
                    "SetZoomFrames requires start and end positions."));
            const qint64 start = command.arguments.at(0).toLongLong();
            const qint64 end = command.arguments.at(1).toLongLong();
            if (start < 0 || end <= start
                || end > mTimeline.frameCount())
                return fail(command, QStringLiteral(
                    "The requested timeline zoom range is invalid."));
            mPositionControl->SetZoomRange(start, end - 1);
        } else if (name == QStringLiteral("video.AddMarker")) {
            if (!requireArguments(command, 1, 1))
                return fail(command, QStringLiteral("AddMarker requires a source frame."));
            const qint64 sourceMarker = command.arguments.first().toLongLong();
            const qint64 timelineMarker = mTimeline.mapSourceToOutput(
                sourceMarker, 0, true);
            if (timelineMarker < 0)
                return fail(command, QStringLiteral(
                    "The marker source frame is not present in the edited timeline."));
            if (!mTimelineMarkers.contains(sourceMarker)) {
                mTimelineMarkers.append(sourceMarker);
                std::sort(mTimelineMarkers.begin(), mTimelineMarkers.end());
                refreshTimelineMarkers();
            }
        } else if (name == QStringLiteral("project.ClearTextInfo")) {
            processing.textMetadata.clear();
        } else if (name == QStringLiteral("project.AddTextInfo")) {
            if (!requireArguments(command, 2, 2)) return fail(command, QStringLiteral("AddTextInfo requires a key and value."));
            const QString code = command.arguments.at(0).toString().toUpper();
            const QString key = code == QStringLiteral("IART") ? QStringLiteral("artist")
                : code == QStringLiteral("INAM") ? QStringLiteral("title")
                : code == QStringLiteral("ICMT") ? QStringLiteral("comment")
                : code == QStringLiteral("ICOP") ? QStringLiteral("copyright")
                : code == QStringLiteral("ICRD") ? QStringLiteral("date") : code;
            processing.textMetadata.insert(key, command.arguments.at(1).toString());
        } else if (name == QStringLiteral("SaveFormat")
                   || name == QStringLiteral("SaveFormatAVI")) {
            mAutomationContainerType = command.arguments.isEmpty()
                ? QStringLiteral("avi") : command.arguments.last().toString();
        } else if (name == QStringLiteral("SaveAudioFormat")) {
            mAutomationAudioFormat = command.arguments.isEmpty()
                ? QString() : command.arguments.last().toString();
        } else if (name == QStringLiteral("SaveAVI")
                   || name == QStringLiteral("SaveCompatibleAVI")) {
            if (!requireArguments(command, 1, 1)) return fail(command, QStringLiteral("SaveAVI requires an output path."));
            if (!applySubset()) return false;
            applyProcessingState(processing);
            QString exportError;
            if (!exportAutomationVideo(resolvePath(command.arguments.first()), &exportError))
                return fail(command, exportError);
        } else if (name == QStringLiteral("SaveWAV")
                   || name == QStringLiteral("SaveAudio")) {
            if (!requireArguments(command, 1, 2)) return fail(command, QStringLiteral("The audio save command requires an output path."));
            if (!applySubset()) return false;
            applyProcessingState(processing);
            QString exportError;
            if (!exportAutomationAudio(resolvePath(command.arguments.first()), false, &exportError))
                return fail(command, exportError);
        } else if (name == QStringLiteral("SaveRawAudio")) {
            if (!requireArguments(command, 1, 1)) return fail(command, QStringLiteral("SaveRawAudio requires an output path."));
            QString exportError;
            if (!exportAutomationAudio(resolvePath(command.arguments.first()), true, &exportError))
                return fail(command, exportError);
        } else if (name == QStringLiteral("SaveRawVideo")) {
            if (!requireArguments(command, 5, 5)) return fail(command, QStringLiteral("SaveRawVideo requires a path and four format arguments."));
            if (!applySubset()) return false;
            applyProcessingState(processing);
            QString exportError;
            if (!exportAutomationRawVideo(
                    resolvePath(command.arguments.first()),
                    command.arguments.mid(1), &exportError))
                return fail(command, exportError);
        } else if (name == QStringLiteral("RunNullVideoPass")) {
            if (!mVideoDecoder.isOpen()) return fail(command, QStringLiteral("No video is open."));
            if (!applySubset()) return false;
            applyProcessingState(processing);
            if (!ensureExactFrameRange(QStringLiteral("script null pass")))
                return fail(command, QStringLiteral("The source could not be indexed."));
            VDQtFilterSystem::instance().resetRuntimeState();
            for (qint64 frame = 0; frame < mTimeline.frameCount(); ++frame) {
                const int sourceFrame = sourceFrameForTimelineFrame(frame);
                const QImage input = mVideoDecoder.getFrameImage(sourceFrame, true);
                QList<QImage> outputs;
                VDFilterFrameContext context;
                context.frameNumber = frame;
                context.timestampSeconds =
                    mVideoDecoder.getFrameTimestampSeconds(sourceFrame);
                context.frameRate = mVideoDecoder.getFps();
                if (input.isNull()
                    || !VDQtFilterSystem::instance().processFrameSequence(
                        input, outputs, context)
                    || outputs.isEmpty()) {
                    VDQtFilterSystem::instance().resetRuntimeState();
                    return fail(command, QString(
                        "The null pass failed at timeline frame %1.").arg(frame));
                }
                if (!mAutomationUnattended)
                    QApplication::processEvents(QEventLoop::AllEvents, 10);
            }
            VDQtFilterSystem::instance().resetRuntimeState();
        } else if (name == QStringLiteral("Log")
                   || name == QStringLiteral("SetStatus")) {
            if (!command.arguments.isEmpty())
                VDLogWindow::instance(this)->appendLog(
                    QStringLiteral("[Script] %1").arg(command.arguments.first().toString()));
        } else if (name == QStringLiteral("Exit")) {
            if (!requireArguments(command, 1, 1)) return fail(command, QStringLiteral("Exit requires a return code."));
            mAutomationExitRequested = true;
            mAutomationExitCode = static_cast<int>(command.arguments.first().toLongLong());
        } else if (name == QStringLiteral("video.SetIVTC")) {
            if (command.arguments.isEmpty())
                return fail(command, QStringLiteral("SetIVTC requires an enable value."));
            if (command.arguments.first().toLongLong() != 0)
                return fail(command, QStringLiteral(
                    "Legacy pipeline IVTC is obsolete; add the inverse telecine video filter instead."));
        } else if (name == QStringLiteral("video.SetInputFormat")) {
            if (!requireArguments(command, 1, 1))
                return fail(command, QStringLiteral("SetInputFormat requires one format identifier."));
            // The native decoded-frame pipeline is RGB. Preserve the legacy
            // request that has a distinct representation here; all other
            // legacy formats use the best source-precision RGB image.
            processing.decompression.formatName =
                command.arguments.first().toLongLong() == 7
                    ? QStringLiteral("RGB24") : QStringLiteral("Autoselect");
        } else if (name == QStringLiteral("video.SetInputMatrix")) {
            if (!requireArguments(command, 2, 2))
                return fail(command, QStringLiteral("SetInputMatrix requires color space and range."));
            processing.decompression.colorSpace = std::clamp(
                static_cast<int>(command.arguments.at(0).toLongLong()), 0, 2);
            processing.decompression.componentRange = std::clamp(
                static_cast<int>(command.arguments.at(1).toLongLong()), 0, 2);
        } else if (name == QStringLiteral("video.SetOutputFormat")) {
            if (!requireArguments(command, 1, 1))
                return fail(command, QStringLiteral("SetOutputFormat requires one format identifier."));
            const QMap<qint64, QString> pixelFormats = {
                {7, QStringLiteral("rgb24")},
                {8, QStringLiteral("bgra")},
                {9, QStringLiteral("gray")},
                {10, QStringLiteral("uyvy422")},
                {11, QStringLiteral("yuyv422")},
                {13, QStringLiteral("yuv444p")},
                {14, QStringLiteral("yuv422p")},
                {15, QStringLiteral("yuv420p")},
                {22, QStringLiteral("nv12")},
                {54, QStringLiteral("rgba64le")},
                {56, QStringLiteral("yuv422p10le")},
                {57, QStringLiteral("yuv420p10le")},
                {65, QStringLiteral("p010le")}
            };
            const QString format = pixelFormats.value(
                command.arguments.first().toLongLong());
            if (format.isEmpty()) {
                warnings << QStringLiteral(
                    "Line %1: the legacy output pixel format has no native FFmpeg mapping; automatic format selection is retained.")
                    .arg(command.line);
            } else {
                processing.videoCodec.pixFmt = format;
            }
        } else if (name == QStringLiteral("video.SetOutputMatrix")) {
            if (!requireArguments(command, 2, 2))
                return fail(command, QStringLiteral(
                    "SetOutputMatrix requires color space and range."));
            const int colorSpace = static_cast<int>(
                command.arguments.at(0).toLongLong());
            const int colorRange = static_cast<int>(
                command.arguments.at(1).toLongLong());
            if (colorSpace == 1) {
                processing.videoCodec.colorMatrix = QStringLiteral("smpte170m");
                processing.rawVideo.colorMatrix = QStringLiteral("bt601");
            } else if (colorSpace == 2) {
                processing.videoCodec.colorMatrix = QStringLiteral("bt709");
                processing.rawVideo.colorMatrix = QStringLiteral("bt709");
            } else if (colorSpace == 3) {
                processing.videoCodec.colorMatrix = QStringLiteral("bt2020nc");
                processing.rawVideo.colorMatrix = QStringLiteral("bt2020");
            } else if (colorSpace != 0) {
                return fail(command, QStringLiteral(
                    "The requested output color space is not mapped."));
            }
            if (colorRange < 0 || colorRange > 2)
                return fail(command, QStringLiteral(
                    "The requested output color range is invalid."));
            if (colorRange != 0)
                processing.rawVideo.fullRange = colorRange == 2;
        } else if (name == QStringLiteral("audio.SetVolume")) {
            processing.audioFilters.erase(
                std::remove_if(processing.audioFilters.begin(),
                               processing.audioFilters.end(),
                    [](const VDAudioFilterInstance& filter) {
                        return filter.id == QStringLiteral("sylia-volume");
                    }),
                processing.audioFilters.end());
            if (!command.arguments.isEmpty()) {
                const double gain = std::max(
                    0.0, command.arguments.first().toLongLong() / 256.0);
                VDAudioFilterInstance volume =
                    VDQtAudioFilterSystem::instance().createFilter(
                        VDAudioFilterType::Gain);
                volume.id = QStringLiteral("sylia-volume");
                volume.name = QStringLiteral("script volume");
                volume.params[QStringLiteral("decibels")] = gain > 0.0
                    ? 20.0 * std::log10(gain) : -96.0;
                processing.audioFilters.prepend(volume);
            }
        } else if (name == QStringLiteral("audio.EnableFilterGraph")) {
            if (!requireArguments(command, 1, 1))
                return fail(command, QStringLiteral("EnableFilterGraph requires one value."));
            const bool enabled = command.arguments.first().toLongLong() != 0;
            for (VDAudioFilterInstance& filter : processing.audioFilters)
                filter.enabled = enabled;
        } else if (name == QStringLiteral("SetPreferencesInt")
                   || name == QStringLiteral("SetPreferencesBool")
                   || name == QStringLiteral("SetPreferencesString")) {
            if (!requireArguments(command, 2, 2))
                return fail(command, QStringLiteral(
                    "%1 requires a preference name and value.").arg(name));
            const QString key = command.arguments.first().toString()
                .trimmed().toLower();
            bool applied = false;
            if (name == QStringLiteral("SetPreferencesInt")) {
                const int value = static_cast<int>(
                    command.arguments.at(1).toLongLong());
                if (key.contains(QStringLiteral("frame cache"))) {
                    mPreferencesConfig.frameCacheMiB =
                        std::clamp(value, 16, 1024);
                    VDQtVideoDecoder::setFrameCacheBudgetMiB(
                        mPreferencesConfig.frameCacheMiB);
                    applied = true;
                } else if (key.contains(QStringLiteral("decoder thread"))) {
                    mPreferencesConfig.decoderThreads =
                        std::clamp(value, 0, 64);
                    VDQtVideoDecoder::setDecoderThreadCount(
                        mPreferencesConfig.decoderThreads);
                    applied = true;
                } else if (key.contains(QStringLiteral("playback"))
                           && key.contains(QStringLiteral("timer"))) {
                    mPreferencesConfig.playbackTimerIntervalMs =
                        std::clamp(value, 2, 50);
                    mPlaybackTimer->setInterval(
                        mPreferencesConfig.playbackTimerIntervalMs);
                    applied = true;
                }
            }
            if (!applied) {
                warnings << QStringLiteral(
                    "Line %1: Windows preference '%2' has no native session setting and was ignored.")
                    .arg(command.line).arg(command.arguments.first().toString());
            }
        } else if (name == QStringLiteral("video.filters.BeginUpdate")
                   || name == QStringLiteral("video.filters.EndUpdate")
                   || name == QStringLiteral("video.SetOutputReference")
                   || name == QStringLiteral("video.SetCompData")
                   || name == QStringLiteral("audio.SetInterleave")
                   || name == QStringLiteral("audio.SetClipMode")
                   || name == QStringLiteral("audio.SetEditMode")
                   || name == QStringLiteral("audio.SetCompData")
                   ) {
            warnings << QStringLiteral("Line %1: %2 has no separate native setting and was ignored.")
                .arg(command.line).arg(name);
        } else if (name == QStringLiteral("Preview")) {
            applyProcessingState(processing);
            onTransportAction(VDQT_PCN_PLAYPREVIEW);
        } else if (name == QStringLiteral("SaveAnimatedGIF")
                   || name == QStringLiteral("SaveAnimatedPNG")) {
            if (!requireArguments(command, 1,
                                  name == QStringLiteral("SaveAnimatedPNG") ? 4 : 2))
                return fail(command, QStringLiteral("The animation save command requires an output path."));
            if (!applySubset()) return false;
            applyProcessingState(processing);
            QString outputPath = resolvePath(command.arguments.first());
            if (name == QStringLiteral("SaveAnimatedGIF")
                && QFileInfo(outputPath).suffix().isEmpty())
                outputPath += QStringLiteral(".gif");
            if (name == QStringLiteral("SaveAnimatedPNG")
                && QFileInfo(outputPath).suffix().isEmpty())
                outputPath += QStringLiteral(".apng");
            QString exportError;
            const int loopCount = command.arguments.size() > 1
                ? std::max(0, static_cast<int>(command.arguments.at(1).toLongLong()))
                : 0;
            const bool preserveAlpha = name == QStringLiteral("SaveAnimatedPNG")
                && command.arguments.size() > 2
                && command.arguments.at(2).toLongLong() != 0;
            const bool grayscale = name == QStringLiteral("SaveAnimatedPNG")
                && command.arguments.size() > 3
                && command.arguments.at(3).toLongLong() != 0;
            if (!exportAutomationVideo(outputPath, &exportError, loopCount,
                                       preserveAlpha, grayscale))
                return fail(command, exportError);
        } else if (name == QStringLiteral("SaveImageSequence")
                   || name == QStringLiteral("SaveImageSequence2")) {
            const bool version2 = name.endsWith(QLatin1Char('2'));
            const int minimumArguments = version2 ? 5 : 4;
            const int maximumArguments = version2 ? 6 : 5;
            if (!requireArguments(command, minimumArguments, maximumArguments))
                return fail(command, QStringLiteral("The image-sequence command has an invalid argument list."));
            if (!applySubset()) return false;
            applyProcessingState(processing);
            if (!ensureExactFrameRange(QStringLiteral("script image sequence")))
                return fail(command, QStringLiteral("The source could not be indexed."));
            const QString prefix = resolvePath(command.arguments.at(0));
            QString suffix = command.arguments.at(1).toString();
            const int digits = std::clamp(
                static_cast<int>(command.arguments.at(2).toLongLong()), 1, 12);
            const int startNumber = version2
                ? static_cast<int>(command.arguments.at(3).toLongLong()) : 0;
            const int formatIndex = static_cast<int>(
                command.arguments.at(version2 ? 4 : 3).toLongLong());
            const int quality = std::clamp(static_cast<int>(
                command.arguments.value(version2 ? 5 : 4, 95).toLongLong()), 0, 100);
            static const QStringList formatSuffixes = {
                QStringLiteral(".bmp"), QStringLiteral(".tga"),
                QStringLiteral(".jpg"), QStringLiteral(".png"),
                QStringLiteral(".tga"), QStringLiteral(".tif"),
                QStringLiteral(".tif"), QStringLiteral(".tif")
            };
            if (suffix.isEmpty())
                suffix = formatSuffixes.value(formatIndex, QStringLiteral(".png"));
            const qint64 first = mPositionControl->hasSelection()
                ? mPositionControl->GetSelectionStart() : 0;
            const qint64 endExclusive = mPositionControl->hasSelection()
                ? std::min(mPositionControl->GetSelectionEnd(), mTimeline.frameCount())
                : mTimeline.frameCount();
            if (first < 0 || first >= endExclusive)
                return fail(command, QStringLiteral("The image-sequence range is empty."));
            const VDFilterTimingInfo timing =
                VDQtFilterSystem::instance().getTimingInfo();
            if (!timing.sequenceSupported || timing.outputFramesPerInput <= 0)
                return fail(command, QStringLiteral("The temporal filter output is unsupported."));
            const QFileInfo prefixInfo(prefix);
            QTemporaryDir staging(prefixInfo.dir().filePath(
                QStringLiteral(".virtualdub-script-images-XXXXXX")));
            if (!staging.isValid())
                return fail(command, QStringLiteral("An image staging directory could not be created."));
            QStringList targets;
            QStringList stagedPaths;
            VDQtFilterSystem::instance().resetRuntimeState();
            qint64 outputIndex = startNumber;
            for (qint64 frame = first; frame < endExclusive; ++frame) {
                const int sourceFrame = sourceFrameForTimelineFrame(frame);
                const QImage input = mVideoDecoder.getFrameImage(sourceFrame, true);
                QList<QImage> outputs;
                VDFilterFrameContext context;
                context.frameNumber = frame;
                context.timestampSeconds =
                    mVideoDecoder.getFrameTimestampSeconds(sourceFrame);
                context.frameRate = mVideoDecoder.getFps();
                if (input.isNull()
                    || !VDQtFilterSystem::instance().processFrameSequence(
                        input, outputs, context))
                    return fail(command, QString("Frame %1 could not be rendered.").arg(frame));
                for (const QImage& output : outputs) {
                    const QString number = QStringLiteral("%1").arg(
                        outputIndex++, digits, 10, QLatin1Char('0'));
                    const QString target = prefix + number + suffix;
                    if (!loadedOutputSafety(target, mVideoDecoder, mAudioPlayer,
                                            mTimelineSources).isSafe())
                        return fail(command, QString("An image output aliases a source: %1").arg(target));
                    const QString staged = staging.filePath(
                        QStringLiteral("%1%2").arg(stagedPaths.size(), 10, 10,
                                                   QLatin1Char('0')) + suffix);
                    if (output.isNull() || !output.save(staged, nullptr, quality))
                        return fail(command, QString("Image %1 could not be encoded.").arg(target));
                    targets.append(target);
                    stagedPaths.append(staged);
                }
            }
            const QString backupDirectory = staging.filePath(QStringLiteral("backups"));
            if (!QDir().mkpath(backupDirectory))
                return fail(command, QStringLiteral("Image backups could not be prepared."));
            QVector<QPair<QString, QString>> backups;
            QStringList committed;
            const auto rollback = [&]() {
                for (auto it = committed.crbegin(); it != committed.crend(); ++it)
                    QFile::remove(*it);
                for (auto it = backups.crbegin(); it != backups.crend(); ++it)
                    QFile::rename(it->second, it->first);
            };
            for (int index = 0; index < targets.size(); ++index) {
                const QFileInfo existing(targets.at(index));
                if (!existing.exists() && !existing.isSymLink()) continue;
                const QString backup = QDir(backupDirectory).filePath(
                    QString::number(index));
                if (!QFile::rename(targets.at(index), backup)) {
                    rollback();
                    return fail(command, QString("Existing image could not be backed up: %1")
                        .arg(targets.at(index)));
                }
                backups.append({targets.at(index), backup});
            }
            for (int index = 0; index < targets.size(); ++index) {
                QDir().mkpath(QFileInfo(targets.at(index)).absolutePath());
                if (!QFile::rename(stagedPaths.at(index), targets.at(index))) {
                    rollback();
                    return fail(command, QString("The completed image sequence could not be committed at %1")
                        .arg(targets.at(index)));
                }
                committed.append(targets.at(index));
            }
        } else if (name == QStringLiteral("SaveSegmentedAVI")) {
            if (!requireArguments(command, 3, 5))
                return fail(command, QStringLiteral(
                    "SaveSegmentedAVI requires a path, size limit, frame limit, and optional digit/segment counts."));
            if (!applySubset()) return false;
            applyProcessingState(processing);
            QString exportError;
            if (!exportSegmentedVideo(
                    resolvePath(command.arguments.at(0)),
                    static_cast<int>(command.arguments.at(1).toLongLong()),
                    static_cast<int>(command.arguments.at(2).toLongLong()),
                    command.arguments.size() >= 4
                        ? static_cast<int>(command.arguments.at(3).toLongLong()) : 2,
                    command.arguments.size() >= 5
                        ? static_cast<int>(command.arguments.at(4).toLongLong()) : 0,
                    &exportError))
                return fail(command, exportError);
        } else if (name == QStringLiteral("ExportViaEncoderSet")) {
            if (!requireArguments(command, 2, 2))
                return fail(command, QStringLiteral(
                    "ExportViaEncoderSet requires an output path and set name."));
            if (!applySubset()) return false;
            applyProcessingState(processing);
            QString exportError;
            if (!exportViaEncoderSet(
                    resolvePath(command.arguments.at(0)),
                    command.arguments.at(1).toString(), &exportError))
                return fail(command, exportError);
        } else if (name == QStringLiteral("StartServer")) {
            if (!requireArguments(command, 1, 1))
                return fail(command, QStringLiteral(
                    "StartServer requires a FIFO path."));
            if (!applySubset()) return false;
            applyProcessingState(processing);
            QString serverError;
            if (!startFrameServerAtPath(
                    resolvePath(command.arguments.first()), &serverError))
                return fail(command, serverError);
        } else {
            return fail(command, QStringLiteral("Unsupported Sylia command."));
        }
    }

    if (!audioFilterConnections.isEmpty()) {
        const bool linearGraph = std::all_of(
            audioFilterConnections.cbegin(), audioFilterConnections.cend(),
            [](const std::array<int, 4>& connection) {
                return connection[2] == connection[0] + 1
                    && connection[1] == 0 && connection[3] == 0;
            });
        if (!linearGraph) {
            warnings << QStringLiteral(
                "The audio script contains a branched or multi-pin graph. Its filters were retained in serialization order because the native preview/export pipeline is linear.");
        }
    }
    if (!applySubset()) return false;
    applyProcessingState(processing);
    for (const QString& warning : warnings)
        VDLogWindow::instance(this)->appendLog(QStringLiteral("[Script warning] %1").arg(warning));
    if (errorMessage) errorMessage->clear();
    return true;
}

void VDQtMainWindow::onFileRunScript() {
    QString fileName = QFileDialog::getOpenFileName(
        this, "Run Script", QString(),
        "VirtualDub Sylia Scripts (*.vdscript *.vcf *.jobs);;Video Scripts (*.avs *.AVS *.vpy *.VPY);;VirtualDubQT Projects (*.vdqproject);;VirtualDubQT Job Scripts (*.vdqjobs);;VirtualDubQT Processing Settings (*.vdqsettings);;All Files (*)");
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
        } else if (fileName.endsWith(".vdscript", Qt::CaseInsensitive)
                   || fileName.endsWith(".vcf", Qt::CaseInsensitive)
                   || fileName.endsWith(".jobs", Qt::CaseInsensitive)) {
            QString error;
            if (!runAutomationScript(fileName, &error)) {
                QMessageBox::critical(this, "Run Sylia Script Error", error);
                return;
            }
            statusBar()->showMessage(
                QString("Sylia script completed: %1")
                    .arg(QFileInfo(fileName).fileName()));
        } else {
            QMessageBox::warning(this, "Unsupported Script",
                                 "This script format is not supported. Use AviSynth, VapourSynth, "
                                 "a VirtualDub Sylia script, a .vdqproject file, a .vdqjobs file, "
                                 "or a .vdqsettings file.");
        }
    }
}

void VDQtMainWindow::onFileScriptEditor() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Sylia Script Editor"));
    dialog.resize(820, 620);
    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new QPlainTextEdit(&dialog);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setPlaceholderText(
        QStringLiteral("VirtualDub.Open(\"input.mkv\");\n"
                       "VirtualDub.video.SetMode(3);\n"
                       "VirtualDub.SaveAVI(\"output.mkv\");"));
    QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    editor->setFont(fixed);
    layout->addWidget(editor, 1);
    auto *status = new QLabel(QStringLiteral("New script"), &dialog);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *row = new QHBoxLayout;
    auto *open = new QPushButton(QStringLiteral("Open..."), &dialog);
    auto *save = new QPushButton(QStringLiteral("Save"), &dialog);
    auto *saveAs = new QPushButton(QStringLiteral("Save As..."), &dialog);
    auto *run = new QPushButton(QStringLiteral("Run"), &dialog);
    auto *close = new QPushButton(QStringLiteral("Close"), &dialog);
    row->addWidget(open); row->addWidget(save); row->addWidget(saveAs);
    row->addStretch(); row->addWidget(run); row->addWidget(close);
    layout->addLayout(row);
    QString currentPath;
    const auto saveDocument = [&](bool choosePath) -> bool {
        if (choosePath || currentPath.isEmpty()) {
            QString selected = QFileDialog::getSaveFileName(
                &dialog, QStringLiteral("Save Sylia Script"), currentPath,
                QStringLiteral("VirtualDub Sylia Scripts (*.vdscript *.vcf);;All Files (*)"));
            if (selected.isEmpty()) return false;
            if (QFileInfo(selected).suffix().isEmpty())
                selected += QStringLiteral(".vdscript");
            currentPath = QFileInfo(selected).absoluteFilePath();
        }
        QSaveFile file(currentPath);
        const QByteArray contents = editor->toPlainText().toUtf8();
        if (!file.open(QIODevice::WriteOnly)
            || file.write(contents) != contents.size() || !file.commit()) {
            QMessageBox::critical(&dialog, QStringLiteral("Script Save Error"),
                                  QStringLiteral("The script could not be saved."));
            return false;
        }
        editor->document()->setModified(false);
        status->setText(QString("Saved %1").arg(QFileInfo(currentPath).fileName()));
        return true;
    };
    connect(open, &QPushButton::clicked, &dialog, [&]() {
        const QString selected = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("Open Sylia Script"), currentPath,
            QStringLiteral("VirtualDub Sylia Scripts (*.vdscript *.vcf *.jobs);;All Files (*)"));
        if (selected.isEmpty()) return;
        QFile file(selected);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
            QMessageBox::critical(&dialog, QStringLiteral("Script Open Error"),
                                  QStringLiteral("The script could not be opened or is too large."));
            return;
        }
        editor->setPlainText(QString::fromUtf8(file.readAll()));
        currentPath = QFileInfo(selected).absoluteFilePath();
        editor->document()->setModified(false);
        status->setText(QString("Opened %1").arg(QFileInfo(currentPath).fileName()));
    });
    connect(save, &QPushButton::clicked, &dialog,
            [saveDocument]() { saveDocument(false); });
    connect(saveAs, &QPushButton::clicked, &dialog,
            [saveDocument]() { saveDocument(true); });
    connect(run, &QPushButton::clicked, &dialog, [&]() {
        QString error;
        const QString baseDirectory = currentPath.isEmpty()
            ? QDir::currentPath() : QFileInfo(currentPath).absolutePath();
        if (!runAutomationText(editor->toPlainText(), baseDirectory, &error)) {
            status->setText(QString("Error: %1").arg(error));
            QMessageBox::critical(&dialog, QStringLiteral("Script Error"), error);
        } else {
            status->setText(QStringLiteral("Script completed successfully."));
        }
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
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
    VDQtFilterSystem::instance().resetRuntimeState();
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
        VDFilterFrameContext filterContext;
        filterContext.frameNumber = timelineFrame;
        filterContext.timestampSeconds =
            decoder.getFrameTimestampSeconds(static_cast<int>(sourceFrame));
        filterContext.frameRate = decoder.getFps();
        if (!VDQtFilterSystem::instance().processFrameSequence(
                raw, filtered, filterContext)
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
    QString startError;
    if (!startFrameServerAtPath(pipePath, &startError)) {
        if (!startError.isEmpty())
            QMessageBox::critical(this, "Frame Server Error", startError);
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

bool VDQtMainWindow::startFrameServerAtPath(
    const QString& pipePath, QString *errorMessage) {
    if (!mVideoDecoder.isOpen()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "Open a video or script before starting a frame server.");
        return false;
    }
    if (mFrameServer && mFrameServer->isRunning()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "A frame server is already running.");
        return false;
    }
    const VDQtOutputSafetyReport safety = loadedOutputSafety(
        pipePath, mVideoDecoder, mAudioPlayer, mTimelineSources);
    if (!safety.isSafe()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The FIFO path aliases a source or cannot be audited safely.");
        return false;
    }
    if (QFileInfo(pipePath).exists() || QFileInfo(pipePath).isSymLink()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "Frame serving never replaces an existing filesystem entry.");
        return false;
    }

    VDQtFrameServer::Config config;
    config.sourcePath = mVideoDecoder.getFilePath();
    config.pipePath = QFileInfo(pipePath).absoluteFilePath();
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
        if (!ensureExactFrameRange(QStringLiteral("frame-server audio range"))) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "The audio range could not be indexed.");
            return false;
        }
        const qint64 audioStart = config.startFrame;
        const qint64 audioEndExclusive = config.endFrame >= config.startFrame
            ? static_cast<qint64>(config.endFrame) + 1 : mTimeline.frameCount();
        QString rangeError;
        const QList<VDQtTimelineSegment> segments = mTimeline.copyRange(
            audioStart, audioEndExclusive, &rangeError);
        if (segments.isEmpty()) {
            if (errorMessage) *errorMessage = rangeError.isEmpty()
                ? QStringLiteral("The frame-server audio range is empty.")
                : rangeError;
            return false;
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
                && lastTimestamp + lastDuration > startSeconds)
                durationSeconds = lastTimestamp + lastDuration - startSeconds;
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
        QProgressDialog progress(
            QStringLiteral("Preparing frame-server audio..."),
            QStringLiteral("Cancel"), 0, 100,
            mAutomationUnattended ? nullptr : this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(mAutomationUnattended
            ? std::numeric_limits<int>::max() : 0);
        const bool prepared = mAudioPlayer.exportAudioRangesToFile(
            mFrameServerAudioPath, ranges,
            [&progress](int current, int total) {
                progress.setRange(0, std::max(1, total));
                progress.setValue(std::clamp(current, 0, std::max(1, total)));
                QApplication::processEvents(QEventLoop::AllEvents, 50);
                return !progress.wasCanceled();
            });
        if (!prepared) {
            QFile::remove(mFrameServerAudioPath);
            mFrameServerAudioPath.clear();
            if (errorMessage) *errorMessage = progress.wasCanceled()
                ? QStringLiteral("Frame-server startup was cancelled.")
                : QStringLiteral("The edited audio range could not be prepared.");
            return false;
        }
        config.audioPath = mFrameServerAudioPath;
    }

    QString startError;
    if (!mFrameServer->start(config, &startError)) {
        QFile::remove(mFrameServerAudioPath);
        mFrameServerAudioPath.clear();
        if (errorMessage) *errorMessage = startError;
        return false;
    }
    if (errorMessage) errorMessage->clear();
    return true;
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
    if (event->isAccepted()) {
        if (mRecoveryTimer) mRecoveryTimer->stop();
        if (!mRecoveryPath.isEmpty()) QFile::remove(mRecoveryPath);
    }
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

void VDQtMainWindow::onFileSaveSegmentedAVI() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, "No Video Loaded",
                             "Please open a video or script first.");
        return;
    }
    const QFileInfo source(primarySessionSourcePath());
    QString outputPath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Segmented AVI"),
        source.dir().filePath(source.completeBaseName() + QStringLiteral(".avi")),
        QStringLiteral("AVI files (*.avi);;All Files (*)"));
    if (outputPath.isEmpty()) return;
    if (QFileInfo(outputPath).suffix().isEmpty())
        outputPath += QStringLiteral(".avi");

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Segment Limits"));
    auto *layout = new QFormLayout(&dialog);
    auto *useCount = new QCheckBox(QStringLiteral("Split into a fixed number of segments"), &dialog);
    auto *count = new QSpinBox(&dialog);
    count->setRange(1, 100000); count->setValue(2);
    auto *useFrames = new QCheckBox(QStringLiteral("Limit frames per segment"), &dialog);
    auto *frames = new QSpinBox(&dialog);
    frames->setRange(1, std::numeric_limits<int>::max()); frames->setValue(10000);
    auto *useSize = new QCheckBox(QStringLiteral("Limit file size per segment"), &dialog);
    useSize->setChecked(true);
    auto *size = new QSpinBox(&dialog);
    size->setRange(1, 1024 * 1024); size->setValue(2000); size->setSuffix(QStringLiteral(" MB"));
    auto *digits = new QSpinBox(&dialog);
    digits->setRange(1, 10); digits->setValue(2);
    layout->addRow(useCount, count);
    layout->addRow(useFrames, frames);
    layout->addRow(useSize, size);
    layout->addRow(QStringLiteral("Minimum digit count:"), digits);
    const auto updateEnabled = [=]() {
        count->setEnabled(useCount->isChecked());
        frames->setEnabled(useFrames->isChecked());
        size->setEnabled(useSize->isChecked());
    };
    connect(useCount, &QCheckBox::toggled, &dialog, updateEnabled);
    connect(useFrames, &QCheckBox::toggled, &dialog, updateEnabled);
    connect(useSize, &QCheckBox::toggled, &dialog, updateEnabled);
    updateEnabled();
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (!useCount->isChecked() && !useFrames->isChecked()
            && !useSize->isChecked()) {
            QMessageBox::warning(&dialog, QStringLiteral("No Segment Limit"),
                                 QStringLiteral("Enable at least one segment limit."));
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    QString error;
    if (!exportSegmentedVideo(
            outputPath, useSize->isChecked() ? size->value() : 0,
            useFrames->isChecked() ? frames->value() : 0,
            digits->value(), useCount->isChecked() ? count->value() : 0,
            &error)) {
        if (!error.isEmpty())
            QMessageBox::critical(this, QStringLiteral("Segmented AVI Error"), error);
        return;
    }
    QMessageBox::information(
        this, QStringLiteral("Segmented AVI Complete"),
        QStringLiteral("The AVI segments were exported successfully."));
}

bool VDQtMainWindow::exportSegmentedVideo(
    const QString& outputPath, int sizeLimitMb, int frameLimit,
    int digitCount, int segmentCount, QString *errorMessage) {
    if (!mVideoDecoder.isOpen()) {
        if (errorMessage) *errorMessage = QStringLiteral("No video is open.");
        return false;
    }
    if (sizeLimitMb <= 0 && frameLimit <= 0 && segmentCount <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "At least one segment limit must be enabled.");
        return false;
    }
    if (digitCount < 1 || digitCount > 10) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The segment digit count must be between 1 and 10.");
        return false;
    }
    if (!ensureExactFrameRange(QStringLiteral("segmented AVI export"))) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The source could not be indexed for segmented output.");
        return false;
    }
    const qint64 firstFrame = mPositionControl->hasSelection()
        ? mPositionControl->GetSelectionStart() : 0;
    const qint64 endExclusive = mPositionControl->hasSelection()
        ? std::min(mPositionControl->GetSelectionEnd(), mTimeline.frameCount())
        : mTimeline.frameCount();
    if (firstFrame < 0 || firstFrame >= endExclusive) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The selected segmented-output range is empty.");
        return false;
    }
    const qint64 totalFrames = endExclusive - firstFrame;
    qint64 framesPerSegment = totalFrames;
    if (segmentCount > 0)
        framesPerSegment = std::min(
            framesPerSegment,
            (totalFrames + segmentCount - 1) / segmentCount);
    if (frameLimit > 0)
        framesPerSegment = std::min<qint64>(framesPerSegment, frameLimit);
    if (sizeLimitMb > 0) {
        const VDVideoCodecParams video = VDQtCodecEngine::instance().getVideoParams();
        const VDAudioCodecParams audio = VDQtCodecEngine::instance().getAudioParams();
        qint64 estimatedKbps = mVideoMode == VideoMode_DirectStreamCopy
            ? 0 : std::max(100, video.targetBitrateKbps);
        if (estimatedKbps <= 0) {
            qint64 sourceBytes = 0;
            for (const QString& source : mTimelineSources)
                sourceBytes += std::max<qint64>(0, QFileInfo(source).size());
            const double duration = totalFrames
                / std::max(1.0, mVideoDecoder.getFps());
            estimatedKbps = duration > 0.0
                ? static_cast<qint64>(sourceBytes * 8.0 / duration / 1000.0)
                : 4000;
        }
        if (!mAudioDisabled) estimatedKbps += std::max(64, audio.bitrateKbps);
        const double secondsAtLimit = sizeLimitMb * 8192.0
            / std::max<qint64>(1, estimatedKbps);
        const qint64 estimatedFrames = std::max<qint64>(
            1, static_cast<qint64>(std::floor(
                   secondsAtLimit * std::max(1.0, mVideoDecoder.getFps()))));
        framesPerSegment = std::min(framesPerSegment, estimatedFrames);
    }
    framesPerSegment = std::max<qint64>(1, framesPerSegment);

    QList<QPair<qint64, qint64>> pending;
    for (qint64 start = firstFrame; start < endExclusive;
         start += framesPerSegment) {
        pending.append({start, std::min(endExclusive, start + framesPerSegment)});
    }
    if (pending.size() > 100000) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The requested limits would create more than 100,000 files.");
        return false;
    }

    const QFileInfo requested(outputPath);
    QString prefix = requested.completeBaseName();
    prefix.remove(QRegularExpression(QStringLiteral("\\.\\d+$")));
    const QDir destinationDirectory = requested.dir();
    QTemporaryDir staging(destinationDirectory.filePath(
        QStringLiteral(".virtualdub-segments-XXXXXX")));
    if (!staging.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "A staging directory could not be created beside the destination.");
        return false;
    }

    QStringList stagedPaths;
    const qint64 byteLimit = sizeLimitMb > 0
        ? static_cast<qint64>(sizeLimitMb) * 1024 * 1024 : 0;
    mPlaybackTimer->stop();
    mAudioPlayer.stop();
    for (int rangeIndex = 0; rangeIndex < pending.size(); ++rangeIndex) {
        const auto range = pending.at(rangeIndex);
        const QString stagedPath = staging.filePath(
            QString("segment_%1.avi").arg(stagedPaths.size(), 8, 10,
                                            QLatin1Char('0')));
        VDQtVideoExporter::ExportOptions options = currentExportOptions(
            stagedPath, QStringLiteral("avi"), false, true);
        options.startFrame = static_cast<int>(range.first);
        options.endFrame = static_cast<int>(range.second - 1);
        options.unattended = mAutomationUnattended;
        VDQtVideoExporter exporter;
        mIsExporting = true;
        const bool rendered = exporter.exportVideo(
            options, &mVideoDecoder, &mAudioPlayer,
            mAutomationUnattended ? nullptr : this);
        mIsExporting = false;
        if (!rendered) {
            if (errorMessage) *errorMessage = exporter.lastError().isEmpty()
                ? QStringLiteral("A video segment failed or was cancelled.")
                : exporter.lastError();
            return false;
        }
        if (byteLimit > 0 && QFileInfo(stagedPath).size() > byteLimit
            && range.second - range.first > 1) {
            QFile::remove(stagedPath);
            const qint64 middle = range.first
                + (range.second - range.first) / 2;
            pending[rangeIndex] = {range.first, middle};
            pending.insert(rangeIndex + 1, {middle, range.second});
            --rangeIndex;
            if (pending.size() > 100000) {
                if (errorMessage) *errorMessage = QStringLiteral(
                    "The size limit would create more than 100,000 files.");
                return false;
            }
            continue;
        }
        stagedPaths.append(stagedPath);
    }

    QStringList targets;
    for (int index = 0; index < stagedPaths.size(); ++index) {
        const QString target = destinationDirectory.filePath(
            QString("%1.%2.avi").arg(prefix).arg(
                index, digitCount, 10, QLatin1Char('0')));
        if (!loadedOutputSafety(target, mVideoDecoder, mAudioPlayer,
                                mTimelineSources).isSafe()) {
            if (errorMessage) *errorMessage = QString(
                "A segment destination aliases a source or cannot be audited safely: %1")
                    .arg(target);
            return false;
        }
        targets.append(target);
    }

    const QString backupDirectory = staging.filePath(QStringLiteral("backups"));
    if (!QDir().mkpath(backupDirectory)) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "Existing segment files could not be backed up.");
        return false;
    }
    QVector<QPair<QString, QString>> backups;
    QStringList committed;
    const auto rollback = [&]() {
        for (auto it = committed.crbegin(); it != committed.crend(); ++it)
            QFile::remove(*it);
        for (auto it = backups.crbegin(); it != backups.crend(); ++it)
            QFile::rename(it->second, it->first);
    };
    for (int index = 0; index < targets.size(); ++index) {
        if (!QFileInfo::exists(targets.at(index))
            && !QFileInfo(targets.at(index)).isSymLink()) continue;
        const QString backup = QDir(backupDirectory).filePath(
            QString::number(index));
        if (!QFile::rename(targets.at(index), backup)) {
            rollback();
            if (errorMessage) *errorMessage = QString(
                "The existing segment could not be backed up: %1")
                    .arg(targets.at(index));
            return false;
        }
        backups.append({targets.at(index), backup});
    }
    for (int index = 0; index < targets.size(); ++index) {
        if (!QFile::rename(stagedPaths.at(index), targets.at(index))) {
            rollback();
            if (errorMessage) *errorMessage = QString(
                "The completed segment could not be committed: %1")
                    .arg(targets.at(index));
            return false;
        }
        committed.append(targets.at(index));
    }
    if (errorMessage) errorMessage->clear();
    return true;
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
    exportAnimatedImage(false);
}

void VDQtMainWindow::onFileExportAnimatedPNG() {
    exportAnimatedImage(true);
}

void VDQtMainWindow::onFileExportFilmstrip() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, QStringLiteral("Filmstrip Export"),
                             QStringLiteral("Open a video or script first."));
        return;
    }
    const QFileInfo source(primarySessionSourcePath());
    QString outputPath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Adobe Filmstrip"),
        source.dir().filePath(source.completeBaseName() + QStringLiteral(".flm")),
        QStringLiteral("Adobe Filmstrip (*.flm);;All Files (*)"));
    if (outputPath.isEmpty()) return;
    if (QFileInfo(outputPath).suffix().isEmpty())
        outputPath += QStringLiteral(".flm");
    if (!loadedOutputSafety(outputPath, mVideoDecoder, mAudioPlayer,
                            mTimelineSources).isSafe()) {
        QMessageBox::critical(this, QStringLiteral("Unsafe Filmstrip Path"),
                              QStringLiteral("The filmstrip path aliases a loaded source or cannot be audited safely."));
        return;
    }
    const QFileInfo existing(outputPath);
    if ((existing.exists() || existing.isSymLink())
        && QMessageBox::warning(
               this, QStringLiteral("Replace Existing Filmstrip?"),
               QString("The destination already exists:\n%1\n\nReplace it after rendering succeeds?")
                   .arg(outputPath),
               QMessageBox::Yes | QMessageBox::Cancel,
               QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    if (!ensureExactFrameRange(QStringLiteral("filmstrip export"))) return;

    const qint64 first = mPositionControl->hasSelection()
        ? mPositionControl->GetSelectionStart() : 0;
    const qint64 endExclusive = mPositionControl->hasSelection()
        ? std::min(mPositionControl->GetSelectionEnd(), mTimeline.frameCount())
        : mTimeline.frameCount();
    if (first < 0 || first >= endExclusive) {
        QMessageBox::critical(this, QStringLiteral("Filmstrip Export"),
                              QStringLiteral("The selected range is empty."));
        return;
    }
    const VDFilterTimingInfo timing =
        VDQtFilterSystem::instance().getTimingInfo();
    if (!timing.sequenceSupported || timing.outputFramesPerInput <= 0) {
        QMessageBox::critical(this, QStringLiteral("Filmstrip Export"),
                              QStringLiteral("The temporal filter output is not supported."));
        return;
    }
    const int firstSource = sourceFrameForTimelineFrame(first);
    const QImage sourceFrame = mVideoDecoder.getFrameImage(firstSource, true);
    VDFilterFrameContext firstContext;
    firstContext.frameNumber = first;
    firstContext.timestampSeconds =
        mVideoDecoder.getFrameTimestampSeconds(firstSource);
    firstContext.frameRate = mVideoDecoder.getFps();
    VDQtFilterSystem::instance().resetRuntimeState();
    QList<QImage> firstOutputs;
    if (sourceFrame.isNull()
        || !VDQtFilterSystem::instance().processFrameSequence(
            sourceFrame, firstOutputs, firstContext)
        || firstOutputs.isEmpty() || firstOutputs.first().isNull()) {
        QMessageBox::critical(this, QStringLiteral("Filmstrip Export"),
                              QStringLiteral("The first output frame could not be rendered."));
        return;
    }
    const int width = firstOutputs.first().width();
    const int height = firstOutputs.first().height();
    if (width <= 0 || height <= 0 || width > 32767 || height > 32767) {
        QMessageBox::critical(this, QStringLiteral("Filmstrip Export"),
                              QStringLiteral("Adobe Filmstrip dimensions must fit in signed 16-bit fields."));
        return;
    }
    const qint64 outputCount = (endExclusive - first)
        * timing.outputFramesPerInput;
    if (outputCount > std::numeric_limits<qint32>::max()) {
        QMessageBox::critical(this, QStringLiteral("Filmstrip Export"),
                              QStringLiteral("The filmstrip contains too many frames."));
        return;
    }

    QTemporaryFile staged(stagedOutputTemplate(outputPath));
    if (!staged.open()) {
        QMessageBox::critical(this, QStringLiteral("Filmstrip Export"),
                              QStringLiteral("A staging file could not be created."));
        return;
    }
    const QString stagedPath = staged.fileName();
    staged.setAutoRemove(false);
    QProgressDialog progress(QStringLiteral("Writing Adobe Filmstrip..."),
                             QStringLiteral("Cancel"),
                             0, static_cast<int>(endExclusive - first), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    qint64 writtenFrames = 0;
    bool failed = false;
    for (qint64 frame = first; frame < endExclusive && !failed; ++frame) {
        QList<QImage> outputs;
        if (frame == first) {
            outputs = firstOutputs;
        } else {
            const int sourceIndex = sourceFrameForTimelineFrame(frame);
            const QImage input = mVideoDecoder.getFrameImage(sourceIndex, true);
            VDFilterFrameContext context;
            context.frameNumber = frame;
            context.timestampSeconds =
                mVideoDecoder.getFrameTimestampSeconds(sourceIndex);
            context.frameRate = mVideoDecoder.getFps();
            failed = input.isNull()
                || !VDQtFilterSystem::instance().processFrameSequence(
                    input, outputs, context);
        }
        for (QImage image : outputs) {
            if (image.size() != QSize(width, height)) {
                failed = true;
                break;
            }
            image = image.convertToFormat(QImage::Format_RGBA8888);
            for (int row = 0; row < height; ++row) {
                if (staged.write(reinterpret_cast<const char *>(
                                     image.constScanLine(row)),
                                 static_cast<qint64>(width) * 4)
                    != static_cast<qint64>(width) * 4) {
                    failed = true;
                    break;
                }
            }
            ++writtenFrames;
        }
        progress.setValue(static_cast<int>(frame - first + 1));
        QApplication::processEvents(QEventLoop::AllEvents, 25);
        if (progress.wasCanceled()) failed = true;
    }
    VDQtFilterSystem::instance().resetRuntimeState();
    if (!failed) {
        QByteArray header;
        QDataStream stream(&header, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << static_cast<qint32>(0x52616e64)
               << static_cast<qint32>(writtenFrames)
               << static_cast<qint16>(0) << static_cast<qint16>(0)
               << static_cast<qint16>(width) << static_cast<qint16>(height)
               << static_cast<qint16>(0)
               << static_cast<qint16>(std::clamp(
                      static_cast<int>(std::llround(
                          std::max(1.0, mVideoDecoder.getFps())
                          * timing.outputFramesPerInput)), 1, 32767));
        header.append(16, '\0');
        failed = header.size() != 36
            || staged.write(header) != header.size();
    }
    staged.close();
    progress.close();
    const bool committed = !failed && writtenFrames == outputCount
        && loadedOutputSafety(outputPath, mVideoDecoder, mAudioPlayer,
                              mTimelineSources).isSafe()
        && replaceWithStagedFile(stagedPath, outputPath);
    if (!committed) {
        QFile::remove(stagedPath);
        if (!progress.wasCanceled())
            QMessageBox::critical(this, QStringLiteral("Filmstrip Export"),
                                  QStringLiteral("The filmstrip could not be completed."));
        return;
    }
    statusBar()->showMessage(
        QString("Filmstrip saved: %1").arg(QFileInfo(outputPath).fileName()));
}

void VDQtMainWindow::onFileExportViaEncoderSet() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, QStringLiteral("External Encoder"),
                             QStringLiteral("Open a video or script first."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("External Encoder Sets"));
    auto *layout = new QFormLayout(&dialog);
    auto *setName = new QComboBox(&dialog);
    setName->setEditable(true);
    auto *program = new QLineEdit(&dialog);
    auto *arguments = new QLineEdit(&dialog);
    auto *extension = new QLineEdit(&dialog);
    extension->setMaximumWidth(100);
    auto *programRow = new QWidget(&dialog);
    auto *programLayout = new QHBoxLayout(programRow);
    programLayout->setContentsMargins(0, 0, 0, 0);
    auto *browse = new QPushButton(QStringLiteral("Browse..."), programRow);
    programLayout->addWidget(program);
    programLayout->addWidget(browse);
    layout->addRow(QStringLiteral("Set name:"), setName);
    layout->addRow(QStringLiteral("Program:"), programRow);
    layout->addRow(QStringLiteral("Arguments:"), arguments);
    layout->addRow(QStringLiteral("Output extension:"), extension);
    auto *help = new QLabel(
        QStringLiteral("Use {input} for the lossless processed source and {output} for the destination. "
                       "Arguments are launched directly, without a shell."), &dialog);
    help->setWordWrap(true);
    layout->addRow(help);

    QSettings settings;
    settings.beginGroup(QStringLiteral("ExternalEncoderSets"));
    QStringList names = settings.childGroups();
    settings.endGroup();
    names.sort(Qt::CaseInsensitive);
    setName->addItems(names);
    if (names.isEmpty()) {
        setName->setCurrentText(QStringLiteral("FFmpeg H.264"));
        program->setText(QStringLiteral("ffmpeg"));
        arguments->setText(QStringLiteral(
            "-y -i {input} -c:v libx264 -crf 20 -c:a aac {output}"));
        extension->setText(QStringLiteral("mp4"));
    }
    const auto loadSet = [&]() {
        const QString name = setName->currentText().trimmed();
        QSettings values;
        values.beginGroup(QStringLiteral("ExternalEncoderSets/%1").arg(name));
        if (values.contains(QStringLiteral("program"))) {
            program->setText(values.value(QStringLiteral("program")).toString());
            arguments->setText(values.value(QStringLiteral("arguments")).toString());
            extension->setText(values.value(QStringLiteral("extension")).toString());
        }
        values.endGroup();
    };
    connect(setName, &QComboBox::currentTextChanged, &dialog,
            [loadSet](const QString&) { loadSet(); });
    if (!names.isEmpty()) loadSet();
    connect(browse, &QPushButton::clicked, &dialog, [&]() {
        const QString selected = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("Select Encoder Program"), program->text());
        if (!selected.isEmpty()) program->setText(selected);
    });

    auto *buttonRow = new QWidget(&dialog);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto *saveSet = new QPushButton(QStringLiteral("Save Set"), buttonRow);
    auto *deleteSet = new QPushButton(QStringLiteral("Delete Set"), buttonRow);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, buttonRow);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Encode..."));
    buttonLayout->addWidget(saveSet);
    buttonLayout->addWidget(deleteSet);
    buttonLayout->addStretch();
    buttonLayout->addWidget(buttons);
    layout->addRow(buttonRow);

    const auto saveCurrentSet = [&]() -> bool {
        const QString name = setName->currentText().trimmed();
        const QString command = program->text().trimmed();
        const QString templateText = arguments->text().trimmed();
        QString suffix = extension->text().trimmed();
        if (name.isEmpty() || name.contains(QLatin1Char('/'))
            || command.isEmpty() || !templateText.contains(QStringLiteral("{input}"))
            || !templateText.contains(QStringLiteral("{output}"))) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Invalid Encoder Set"),
                QStringLiteral("Enter a name without '/', a program, and an argument template containing both {input} and {output}."));
            return false;
        }
        if (suffix.startsWith(QLatin1Char('.'))) suffix.remove(0, 1);
        if (suffix.isEmpty()) suffix = QStringLiteral("mkv");
        QSettings values;
        values.beginGroup(QStringLiteral("ExternalEncoderSets/%1").arg(name));
        values.setValue(QStringLiteral("program"), command);
        values.setValue(QStringLiteral("arguments"), templateText);
        values.setValue(QStringLiteral("extension"), suffix);
        values.endGroup();
        if (setName->findText(name) < 0) setName->addItem(name);
        return true;
    };
    connect(saveSet, &QPushButton::clicked, &dialog, [saveCurrentSet]() {
        saveCurrentSet();
    });
    connect(deleteSet, &QPushButton::clicked, &dialog, [&]() {
        const QString name = setName->currentText().trimmed();
        if (name.isEmpty()) return;
        QSettings values;
        values.remove(QStringLiteral("ExternalEncoderSets/%1").arg(name));
        const int index = setName->findText(name);
        if (index >= 0) setName->removeItem(index);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (saveCurrentSet()) dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    QString suffix = extension->text().trimmed();
    if (suffix.startsWith(QLatin1Char('.'))) suffix.remove(0, 1);
    const QFileInfo source(primarySessionSourcePath());
    QString outputPath = QFileDialog::getSaveFileName(
        this, QStringLiteral("External Encoder Output"),
        source.dir().filePath(source.completeBaseName() + QLatin1Char('.') + suffix),
        QStringLiteral("All Files (*)"));
    if (outputPath.isEmpty()) return;
    if (QFileInfo(outputPath).suffix().isEmpty())
        outputPath += QLatin1Char('.') + suffix;
    const QFileInfo target(outputPath);
    if (target.exists() || target.isSymLink()) {
        const auto answer = QMessageBox::warning(
            this, QStringLiteral("Replace Existing Output?"),
            QString("The destination already exists:\n%1\n\nReplace it only after encoding succeeds?")
                .arg(outputPath), QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }
    QString error;
    if (!exportViaEncoderSet(outputPath, setName->currentText().trimmed(), &error)) {
        QMessageBox::critical(this, QStringLiteral("External Encoder Error"), error);
        return;
    }
    QMessageBox::information(this, QStringLiteral("External Encoder Complete"),
                             QString("Output saved to:\n%1").arg(outputPath));
}

bool VDQtMainWindow::exportViaEncoderSet(
    const QString& outputPath, const QString& setName, QString *errorMessage) {
    if (!mVideoDecoder.isOpen()) {
        if (errorMessage) *errorMessage = QStringLiteral("No video is open.");
        return false;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("ExternalEncoderSets/%1").arg(setName));
    const QString program = settings.value(QStringLiteral("program")).toString().trimmed();
    const QString argumentTemplate = settings.value(
        QStringLiteral("arguments")).toString().trimmed();
    settings.endGroup();
    if (program.isEmpty() || !argumentTemplate.contains(QStringLiteral("{input}"))
        || !argumentTemplate.contains(QStringLiteral("{output}"))) {
        if (errorMessage) *errorMessage = QString(
            "External encoder set '%1' is missing or invalid.").arg(setName);
        return false;
    }
    if (!loadedOutputSafety(outputPath, mVideoDecoder, mAudioPlayer,
                            mTimelineSources).isSafe()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "The external-encoder destination aliases a source or cannot be audited safely.");
        return false;
    }

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "A temporary directory could not be created.");
        return false;
    }
    const QString intermediatePath = temporaryDirectory.filePath(
        QStringLiteral("processed-source.mkv"));
    const VDVideoCodecParams savedVideo =
        VDQtCodecEngine::instance().getVideoParams();
    const VDAudioCodecParams savedAudio =
        VDQtCodecEngine::instance().getAudioParams();
    VDVideoCodecParams intermediate =
        VDQtCodecEngine::getDefaultVideoParamsForCodec(QStringLiteral("ffv1"));
    intermediate.pixFmt = mVideoDecoder.sourceHasAlpha()
        ? (mVideoDecoder.getSourceBitDepth() > 8
               ? QStringLiteral("gbrap16le") : QStringLiteral("bgra"))
        : (mVideoDecoder.getSourceBitDepth() > 8
               ? QStringLiteral("yuv444p16le") : QStringLiteral("yuv444p"));
    intermediate.ffv1Slices = 4;
    VDQtCodecEngine::instance().setVideoParams(intermediate);
    VDAudioCodecParams pcm = savedAudio;
    pcm.codecId = mAudioPlayer.getBitsPerSample() > 16
        ? QStringLiteral("pcm_s24le") : QStringLiteral("pcm_s16le");
    pcm.rateMode = QStringLiteral("cbr");
    VDQtCodecEngine::instance().setAudioParams(pcm);
    VDQtVideoExporter::ExportOptions options = currentExportOptions(
        intermediatePath, QStringLiteral("mkv"), false);
    options.videoMode = VideoMode_FullProcessing;
    options.audioMode = AudioMode_FullProcessing;
    options.unattended = mAutomationUnattended;
    VDQtVideoExporter exporter;
    mPlaybackTimer->stop();
    mAudioPlayer.stop();
    mIsExporting = true;
    const bool prepared = exporter.exportVideo(
        options, &mVideoDecoder, &mAudioPlayer,
        mAutomationUnattended ? nullptr : this);
    mIsExporting = false;
    VDQtCodecEngine::instance().setVideoParams(savedVideo);
    VDQtCodecEngine::instance().setAudioParams(savedAudio);
    if (!prepared) {
        if (errorMessage) *errorMessage = exporter.lastError().isEmpty()
            ? QStringLiteral("The processed intermediate could not be rendered.")
            : exporter.lastError();
        return false;
    }

    QTemporaryFile staged(stagedOutputTemplate(outputPath));
    if (!staged.open()) {
        if (errorMessage) *errorMessage = QStringLiteral(
            "A staging file could not be created beside the destination.");
        return false;
    }
    const QString stagedPath = staged.fileName();
    staged.close();
    QFile::remove(stagedPath);
    QStringList arguments = QProcess::splitCommand(argumentTemplate);
    for (QString& argument : arguments) {
        argument.replace(QStringLiteral("{input}"), intermediatePath);
        argument.replace(QStringLiteral("{output}"), stagedPath);
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        if (errorMessage) *errorMessage = QString(
            "The external encoder could not be started: %1").arg(process.errorString());
        return false;
    }
    QProgressDialog progress(QStringLiteral("Running external encoder..."),
                             QStringLiteral("Cancel"), 0, 0,
                             mAutomationUnattended ? nullptr : this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(mAutomationUnattended ? std::numeric_limits<int>::max() : 0);
    while (!process.waitForFinished(50)) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress.wasCanceled()) {
            process.terminate();
            if (!process.waitForFinished(1500)) {
                process.kill();
                process.waitForFinished(3000);
            }
            QFile::remove(stagedPath);
            if (errorMessage) *errorMessage = QStringLiteral(
                "External encoding was cancelled.");
            return false;
        }
    }
    progress.close();
    const bool encoded = process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0 && QFileInfo(stagedPath).size() > 0;
    if (!encoded
        || !loadedOutputSafety(outputPath, mVideoDecoder, mAudioPlayer,
                               mTimelineSources).isSafe()
        || !replaceWithStagedFile(stagedPath, outputPath)) {
        const QString diagnostics = QString::fromLocal8Bit(
            process.readAll()).right(16384).trimmed();
        QFile::remove(stagedPath);
        if (errorMessage) *errorMessage = diagnostics.isEmpty()
            ? QStringLiteral("The external encoder failed or its output could not be committed.")
            : diagnostics;
        return false;
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

void VDQtMainWindow::exportAnimatedImage(bool animatedPng) {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(
            this, "No Video Loaded",
            "Please open a video or AviSynth script first.");
        return;
    }

    const QFileInfo sourceInfo(primarySessionSourcePath());
    const QString extension = animatedPng ? QStringLiteral(".apng")
                                          : QStringLiteral(".gif");
    const QString formatName = animatedPng ? QStringLiteral("Animated PNG")
                                           : QStringLiteral("Animated GIF");
    const QString suggestedPath = sourceInfo.dir().filePath(
        sourceInfo.completeBaseName() + extension);
    QString outputPath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export %1").arg(formatName), suggestedPath,
        animatedPng ? QStringLiteral("Animated PNG (*.apng *.png);;All Files (*)")
                    : QStringLiteral("Animated GIF (*.gif);;All Files (*)"));
    if (outputPath.isEmpty()) return;
    if (QFileInfo(outputPath).suffix().isEmpty()) outputPath += extension;

    QDialog animationOptions(this);
    animationOptions.setWindowTitle(
        QStringLiteral("%1 Options").arg(formatName));
    auto *animationLayout = new QFormLayout(&animationOptions);
    auto *loopCount = new QSpinBox(&animationOptions);
    loopCount->setRange(0, 1000000);
    loopCount->setSpecialValueText(QStringLiteral("Forever"));
    loopCount->setValue(0);
    animationLayout->addRow(QStringLiteral("Loop count:"), loopCount);
    QCheckBox *preserveAlpha = nullptr;
    QCheckBox *grayscale = nullptr;
    if (animatedPng) {
        preserveAlpha = new QCheckBox(QStringLiteral("Preserve alpha channel"),
                                      &animationOptions);
        preserveAlpha->setChecked(true);
        grayscale = new QCheckBox(QStringLiteral("Encode as grayscale"),
                                  &animationOptions);
        animationLayout->addRow(preserveAlpha);
        animationLayout->addRow(grayscale);
    }
    auto *animationButtons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &animationOptions);
    connect(animationButtons, &QDialogButtonBox::accepted,
            &animationOptions, &QDialog::accept);
    connect(animationButtons, &QDialogButtonBox::rejected,
            &animationOptions, &QDialog::reject);
    animationLayout->addRow(animationButtons);
    if (animationOptions.exec() != QDialog::Accepted) return;

    const VDQtOutputSafetyReport safety =
        loadedOutputSafety(
            outputPath, mVideoDecoder, mAudioPlayer, mTimelineSources);
    if (!safety.isSafe()) {
        QMessageBox::critical(
            this, QStringLiteral("Unsafe %1 Output Path").arg(formatName),
            QStringLiteral("The %1 destination aliases a loaded/script source or cannot be audited safely. "
                           "Choose another path.").arg(formatName));
        return;
    }
    const QFileInfo target(outputPath);
    if (target.exists() || target.isSymLink()) {
        const auto answer = QMessageBox::warning(
            this, QStringLiteral("Replace Existing %1?").arg(formatName),
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
    options.videoCodecOverride = animatedPng ? QStringLiteral("apng")
                                             : QStringLiteral("gif");
    options.videoPixelFormatOverride = animatedPng
        ? (grayscale && grayscale->isChecked()
               ? QStringLiteral("gray")
               : (preserveAlpha && preserveAlpha->isChecked()
                      ? QStringLiteral("rgba") : QStringLiteral("rgb24")))
        : QStringLiteral("rgb8");
    options.containerType = animatedPng ? QStringLiteral("apng")
                                        : QStringLiteral("gif");
    options.animationLoopCount = loopCount->value();
    options.animationAlpha = preserveAlpha && preserveAlpha->isChecked();
    options.animationGrayscale = grayscale && grayscale->isChecked();
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
            QString("%1 saved to %2").arg(formatName, QFileInfo(outputPath).fileName()));
        QMessageBox::information(
            this, QStringLiteral("%1 Export Complete").arg(formatName),
            QString("%1 exported successfully to:\n%2").arg(formatName, outputPath));
    } else {
        VDLogWindow::instance(this)->appendLog(
            QStringLiteral("[Export] %1 export failed or was cancelled.").arg(formatName));
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
    VDQtFilterSystem::instance().resetRuntimeState();
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
        VDFilterFrameContext filterContext;
        filterContext.frameNumber = f;
        filterContext.timestampSeconds =
            mVideoDecoder.getFrameTimestampSeconds(sourceFrame);
        filterContext.frameRate = mVideoDecoder.getFps();
        if (!VDQtFilterSystem::instance().processFrameSequence(
                rawFrame, filteredFrames, filterContext)
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
    mTimelineMarkers.erase(
        std::remove_if(mTimelineMarkers.begin(), mTimelineMarkers.end(),
                       [this](qint64 marker) {
                           return marker < 0
                               || marker >= mTimeline.sourceFrameCount();
                       }),
        mTimelineMarkers.end());
    refreshTimelineMarkers();
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

void VDQtMainWindow::refreshTimelineMarkers() {
    QList<qint64> displayed;
    for (qint64 sourceMarker : mTimelineMarkers) {
        const qint64 outputMarker = mTimeline.mapSourceToOutput(
            sourceMarker, 0, true);
        if (outputMarker >= 0) displayed.append(outputMarker);
    }
    std::sort(displayed.begin(), displayed.end());
    displayed.erase(std::unique(displayed.begin(), displayed.end()),
                    displayed.end());
    mPositionControl->SetMarkers(displayed);
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

void VDQtMainWindow::onEditToggleMarker() {
    if (!mVideoDecoder.isOpen()) return;
    const qint64 position = mPositionControl->GetPosition();
    const qint64 sourcePosition = mTimeline.mapOutputToSource(position);
    if (sourcePosition < 0) return;
    const int index = mTimelineMarkers.indexOf(sourcePosition);
    if (index >= 0)
        mTimelineMarkers.removeAt(index);
    else
        mTimelineMarkers.append(sourcePosition);
    std::sort(mTimelineMarkers.begin(), mTimelineMarkers.end());
    refreshTimelineMarkers();
    statusBar()->showMessage(index >= 0
        ? QString("Marker removed at frame %1").arg(position)
        : QString("Marker added at frame %1").arg(position));
}

void VDQtMainWindow::onEditPreviousMarker() {
    const qint64 position = mPositionControl->GetPosition();
    QList<qint64> displayed;
    for (qint64 marker : mTimelineMarkers) {
        const qint64 mapped = mTimeline.mapSourceToOutput(marker, position, false);
        if (mapped >= 0) displayed.append(mapped);
    }
    std::sort(displayed.begin(), displayed.end());
    for (auto it = displayed.crbegin(); it != displayed.crend(); ++it) {
        if (*it < position) {
            mPositionControl->SetPosition(*it);
            return;
        }
    }
    statusBar()->showMessage(QStringLiteral("No previous marker"));
}

void VDQtMainWindow::onEditNextMarker() {
    const qint64 position = mPositionControl->GetPosition();
    QList<qint64> displayed;
    for (qint64 marker : mTimelineMarkers) {
        const qint64 mapped = mTimeline.mapSourceToOutput(marker, position, true);
        if (mapped >= 0) displayed.append(mapped);
    }
    std::sort(displayed.begin(), displayed.end());
    for (qint64 marker : displayed) {
        if (marker > position) {
            mPositionControl->SetPosition(marker);
            return;
        }
    }
    statusBar()->showMessage(QStringLiteral("No next marker"));
}

void VDQtMainWindow::onEditClearMarkers() {
    mTimelineMarkers.clear();
    refreshTimelineMarkers();
    statusBar()->showMessage(QStringLiteral("Timeline markers cleared"));
}

void VDQtMainWindow::onEditZoomToSelection() {
    if (!mVideoDecoder.isOpen() || !mPositionControl->hasSelection()) {
        QMessageBox::information(
            this, QStringLiteral("Timeline Zoom"),
            QStringLiteral("Set a timeline selection before zooming."));
        return;
    }
    const qint64 start = mPositionControl->GetSelectionStart();
    const qint64 endExclusive = mPositionControl->GetSelectionEnd();
    if (endExclusive - start < 2) {
        QMessageBox::information(
            this, QStringLiteral("Timeline Zoom"),
            QStringLiteral("Select at least two frames to zoom the timeline."));
        return;
    }
    mPositionControl->SetZoomRange(start, endExclusive - 1);
    statusBar()->showMessage(
        QString("Timeline zoomed to frames %1-%2.")
            .arg(start).arg(endExclusive - 1));
}

void VDQtMainWindow::onEditClearTimelineZoom() {
    mPositionControl->ClearZoomRange();
    statusBar()->showMessage(QStringLiteral("Full timeline is visible."));
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

void VDQtMainWindow::onViewAudioWaveform() {
    if (!mAudioPlayer.hasAudio() || mAudioPlayer.getSampleRate() <= 0) {
        QMessageBox::information(this, QStringLiteral("Audio Waveform"),
                                 QStringLiteral("The current source has no decoded audio."));
        return;
    }
    const int sampleRate = mAudioPlayer.getSampleRate();
    double startSeconds = 0.0;
    double durationSeconds = 10.0;
    if (mPositionControl->hasSelection()) {
        const int firstFrame = sourceFrameForTimelineFrame(
            mPositionControl->GetSelectionStart());
        const int lastFrame = sourceFrameForTimelineFrame(
            mPositionControl->GetSelectionEnd() - 1);
        startSeconds = mVideoDecoder.getFrameTimestampSeconds(firstFrame);
        const double lastTime = mVideoDecoder.getFrameTimestampSeconds(lastFrame);
        const double lastDuration = mVideoDecoder.getFrameDurationSeconds(lastFrame);
        if (!std::isfinite(startSeconds))
            startSeconds = firstFrame / std::max(0.001, mVideoDecoder.getFps());
        if (std::isfinite(lastTime) && std::isfinite(lastDuration)
            && lastTime + lastDuration > startSeconds) {
            durationSeconds = lastTime + lastDuration - startSeconds;
        }
    } else {
        const int sourceFrame = sourceFrameForTimelineFrame(
            mPositionControl->GetPosition());
        startSeconds = mVideoDecoder.getFrameTimestampSeconds(sourceFrame);
        if (!std::isfinite(startSeconds))
            startSeconds = sourceFrame / std::max(0.001, mVideoDecoder.getFps());
    }
    durationSeconds = std::clamp(durationSeconds, 0.05, 30.0);
    const int64_t firstSample = static_cast<int64_t>(
        std::llround(std::max(0.0, startSeconds) * sampleRate));
    const int64_t sampleCount = static_cast<int64_t>(
        std::llround(durationSeconds * sampleRate));
    QTemporaryDir directory;
    if (!directory.isValid()) return;
    const QString wavPath = directory.filePath(QStringLiteral("waveform.wav"));
    QProgressDialog progress(
        QStringLiteral("Decoding the waveform preview..."),
        QStringLiteral("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    const bool exported = mAudioPlayer.exportAudioToFile(
        wavPath, firstSample, sampleCount,
        [&](int value, int maximum) {
            progress.setRange(0, std::max(1, maximum));
            progress.setValue(value);
            QApplication::processEvents(QEventLoop::AllEvents, 10);
            return !progress.wasCanceled();
        });
    progress.close();
    if (!exported) {
        if (!progress.wasCanceled())
            QMessageBox::critical(this, QStringLiteral("Audio Waveform"),
                                  QStringLiteral("The waveform audio could not be decoded."));
        return;
    }

    QFile wav(wavPath);
    if (!wav.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = wav.readAll();
    const auto le16 = [&bytes](qsizetype offset) -> quint16 {
        if (offset < 0 || offset + 2 > bytes.size()) return 0;
        const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
        return static_cast<quint16>(p[0] | (p[1] << 8));
    };
    const auto le32 = [&bytes](qsizetype offset) -> quint32 {
        if (offset < 0 || offset + 4 > bytes.size()) return 0;
        const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
        return static_cast<quint32>(p[0])
            | (static_cast<quint32>(p[1]) << 8)
            | (static_cast<quint32>(p[2]) << 16)
            | (static_cast<quint32>(p[3]) << 24);
    };
    quint16 formatTag = 0;
    quint16 channels = 0;
    quint16 bits = 0;
    qsizetype dataOffset = -1;
    qsizetype dataSize = 0;
    for (qsizetype offset = 12; offset + 8 <= bytes.size();) {
        const QByteArray id = bytes.mid(offset, 4);
        const quint32 chunkSize = le32(offset + 4);
        const qsizetype payload = offset + 8;
        if (id == QByteArray("fmt ") && chunkSize >= 16
            && payload + 16 <= bytes.size()) {
            formatTag = le16(payload);
            channels = le16(payload + 2);
            bits = le16(payload + 14);
            if (formatTag == 0xfffe && chunkSize >= 40)
                formatTag = le16(payload + 24);
        } else if (id == QByteArray("data")) {
            dataOffset = payload;
            dataSize = std::min<qsizetype>(
                chunkSize == 0xffffffffU ? bytes.size() - payload : chunkSize,
                bytes.size() - payload);
            break;
        }
        const quint64 next = static_cast<quint64>(payload)
            + chunkSize + (chunkSize & 1U);
        if (next <= static_cast<quint64>(offset)
            || next > static_cast<quint64>(bytes.size())) break;
        offset = static_cast<qsizetype>(next);
    }
    const int bytesPerSample = bits / 8;
    const int bytesPerFrame = channels * bytesPerSample;
    if (dataOffset < 0 || channels == 0 || bytesPerSample <= 0
        || bytesPerFrame <= 0 || dataSize < bytesPerFrame) {
        QMessageBox::critical(this, QStringLiteral("Audio Waveform"),
                              QStringLiteral("The temporary WAV layout is unsupported."));
        return;
    }
    const qint64 frames = dataSize / bytesPerFrame;
    QImage chart(1100, 360, QImage::Format_ARGB32_Premultiplied);
    chart.fill(QColor(18, 18, 24));
    QPainter painter(&chart);
    painter.setPen(QColor(55, 55, 68));
    painter.drawLine(0, chart.height() / 2, chart.width(), chart.height() / 2);
    painter.setPen(QPen(QColor(0, 205, 225), 1));
    const auto normalizedSample = [&](const char *sample) {
        const auto *p = reinterpret_cast<const uchar *>(sample);
        if (formatTag == 3 && bits == 32) {
            float value = 0.0f;
            std::memcpy(&value, sample, sizeof(value));
            return std::clamp(static_cast<double>(value), -1.0, 1.0);
        }
        if (bits == 8) return (static_cast<int>(p[0]) - 128) / 128.0;
        qint64 value = 0;
        if (bits == 16)
            value = static_cast<qint16>(p[0] | (p[1] << 8));
        else if (bits == 24) {
            value = static_cast<qint32>(p[0] | (p[1] << 8) | (p[2] << 16));
            if (value & 0x800000) value |= ~0xffffffLL;
        } else if (bits == 32)
            value = static_cast<qint32>(le32(sample - bytes.constData()));
        else return 0.0;
        const double scale = std::ldexp(1.0, bits - 1);
        return std::clamp(value / scale, -1.0, 1.0);
    };
    for (int x = 0; x < chart.width(); ++x) {
        const qint64 begin = frames * x / chart.width();
        const qint64 end = std::max(begin + 1,
            frames * (x + 1) / chart.width());
        double peak = 0.0;
        for (qint64 frame = begin; frame < end; ++frame) {
            const char *frameData = bytes.constData() + dataOffset
                + frame * bytesPerFrame;
            for (int channel = 0; channel < channels; ++channel)
                peak = std::max(peak, std::abs(normalizedSample(
                    frameData + channel * bytesPerSample)));
        }
        const int halfHeight = chart.height() / 2 - 18;
        const int amplitude = static_cast<int>(peak * halfHeight);
        painter.drawLine(x, chart.height() / 2 - amplitude,
                         x, chart.height() / 2 + amplitude);
    }
    painter.setPen(QColor(210, 210, 220));
    painter.drawText(10, 18,
        QString("%1 s from %2 s — %3 Hz, %4 channel(s), %5-bit")
            .arg(frames / static_cast<double>(sampleRate), 0, 'f', 3)
            .arg(startSeconds, 0, 'f', 3)
            .arg(sampleRate).arg(channels).arg(bits));
    painter.end();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Audio Waveform"));
    dialog.resize(1140, 450);
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(&dialog);
    label->setAlignment(Qt::AlignCenter);
    label->setPixmap(QPixmap::fromImage(chart));
    layout->addWidget(label);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
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
            VDFilterFrameContext filterContext;
            filterContext.frameNumber = mPositionControl->GetPosition();
            filterContext.timestampSeconds =
                mVideoDecoder.getFrameTimestampSeconds(sourceFrame);
            filterContext.frameRate = mVideoDecoder.getFps();
            VDQtFilterSystem::instance().resetRuntimeState();
            QImage processed = VDQtFilterSystem::instance().processFrame(
                frame, filterContext);
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

void VDQtMainWindow::onToolsHistogram() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::information(this, QStringLiteral("Video Histogram"),
                                 QStringLiteral("Open a video first."));
        return;
    }
    const auto renderHistogram = [](const QImage& image) {
        QImage chart(768, 360, QImage::Format_ARGB32_Premultiplied);
        chart.fill(QColor(18, 18, 24));
        if (image.isNull()) return chart;
        std::array<std::array<quint64, 256>, 3> bins{};
        const QImage pixels = image.convertToFormat(QImage::Format_RGBA8888);
        for (int y = 0; y < pixels.height(); ++y) {
            const uchar *line = pixels.constScanLine(y);
            for (int x = 0; x < pixels.width(); ++x) {
                ++bins[0][line[x * 4]];
                ++bins[1][line[x * 4 + 1]];
                ++bins[2][line[x * 4 + 2]];
            }
        }
        quint64 maximum = 1;
        for (const auto& channel : bins)
            for (quint64 value : channel) maximum = std::max(maximum, value);
        QPainter painter(&chart);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF plot(42, 20, chart.width() - 62, chart.height() - 58);
        painter.setPen(QColor(58, 58, 72));
        for (int grid = 0; grid <= 4; ++grid) {
            const qreal y = plot.top() + plot.height() * grid / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        const std::array<QColor, 3> colors = {
            QColor(255, 75, 75, 210), QColor(70, 235, 110, 210),
            QColor(75, 145, 255, 210)};
        const double logMaximum = std::log1p(static_cast<double>(maximum));
        for (int channel = 0; channel < 3; ++channel) {
            QPainterPath path;
            for (int value = 0; value < 256; ++value) {
                const qreal x = plot.left() + plot.width() * value / 255.0;
                const qreal normalized = std::log1p(
                    static_cast<double>(bins[channel][value])) / logMaximum;
                const qreal y = plot.bottom() - normalized * plot.height();
                if (value == 0) path.moveTo(x, y); else path.lineTo(x, y);
            }
            painter.setPen(QPen(colors[channel], 1.5));
            painter.drawPath(path);
        }
        painter.setPen(QColor(210, 210, 220));
        painter.drawText(QPointF(plot.left(), chart.height() - 14),
                         QStringLiteral("0"));
        painter.drawText(QPointF(plot.right() - 18, chart.height() - 14),
                         QStringLiteral("255"));
        painter.drawText(QPointF(10, 18), QStringLiteral("log count"));
        return chart;
    };

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Video Histogram"));
    dialog.resize(810, 450);
    auto *layout = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    const auto addImage = [&](const QString& title, const QImage& frame) {
        auto *label = new QLabel(tabs);
        label->setAlignment(Qt::AlignCenter);
        label->setPixmap(QPixmap::fromImage(renderHistogram(frame)));
        tabs->addTab(label, title);
    };
    addImage(QStringLiteral("Input"), mInputDisplay->frameImage());
    addImage(QStringLiteral("Filtered output"), mOutputDisplay->frameImage());
    layout->addWidget(tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void VDQtMainWindow::onToolsPerformanceProfiler() {
    if (!mVideoDecoder.isOpen() || mTimeline.frameCount() <= 0) {
        QMessageBox::information(this, QStringLiteral("Performance Profiler"),
                                 QStringLiteral("Open a video first."));
        return;
    }
    mPlaybackTimer->stop();
    mAudioPlayer.pause();
    const qint64 timelineCount = mTimeline.frameCount();
    qint64 start = mPositionControl->hasSelection()
        ? mPositionControl->GetSelectionStart()
        : mPositionControl->GetPosition();
    qint64 end = mPositionControl->hasSelection()
        ? mPositionControl->GetSelectionEnd()
        : std::min<qint64>(timelineCount, start + 120);
    start = std::clamp<qint64>(start, 0, timelineCount - 1);
    end = std::clamp<qint64>(end, start + 1, timelineCount);

    VDQtFilterSystem filters;
    filters.replaceActiveChainTransient(
        VDQtFilterSystem::instance().getActiveChain());
    QProgressDialog progress(
        QStringLiteral("Profiling decode and filter processing..."),
        QStringLiteral("Cancel"), 0, static_cast<int>(end - start), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    qint64 decodeNanoseconds = 0;
    qint64 filterNanoseconds = 0;
    qint64 processedFrames = 0;
    qint64 outputFrames = 0;
    for (qint64 frame = start; frame < end; ++frame) {
        QElapsedTimer timer;
        timer.start();
        const int sourceFrame = sourceFrameForTimelineFrame(frame);
        const QImage input = mVideoDecoder.getFrameImage(sourceFrame, true);
        decodeNanoseconds += timer.nsecsElapsed();
        if (input.isNull()) break;
        QList<QImage> outputs;
        VDFilterFrameContext context;
        context.frameNumber = frame;
        context.timestampSeconds =
            mVideoDecoder.getFrameTimestampSeconds(sourceFrame);
        context.frameRate = mVideoDecoder.getFps();
        timer.restart();
        if (!filters.processFrameSequence(input, outputs, context)
            || outputs.isEmpty()) break;
        filterNanoseconds += timer.nsecsElapsed();
        ++processedFrames;
        outputFrames += outputs.size();
        progress.setValue(static_cast<int>(processedFrames));
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        if (progress.wasCanceled()) break;
    }
    filters.resetRuntimeState();
    progress.close();
    const double decodeSeconds = decodeNanoseconds / 1.0e9;
    const double filterSeconds = filterNanoseconds / 1.0e9;
    const double totalSeconds = decodeSeconds + filterSeconds;
    const auto rate = [processedFrames](double seconds) {
        return seconds > 0.0 ? processedFrames / seconds : 0.0;
    };
    QMessageBox::information(
        this, QStringLiteral("Performance Profiler"),
        QString("Frames processed: %1 (%2 filter output frames)\n\n"
                "Decode: %3 ms total, %4 frames/s\n"
                "Filters: %5 ms total, %6 frames/s\n"
                "Combined: %7 ms total, %8 frames/s")
            .arg(processedFrames).arg(outputFrames)
            .arg(decodeSeconds * 1000.0, 0, 'f', 2)
            .arg(rate(decodeSeconds), 0, 'f', 2)
            .arg(filterSeconds * 1000.0, 0, 'f', 2)
            .arg(rate(filterSeconds), 0, 'f', 2)
            .arg(totalSeconds * 1000.0, 0, 'f', 2)
            .arg(rate(totalSeconds), 0, 'f', 2));
}

void VDQtMainWindow::onToolsMediaInspector() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::information(this, QStringLiteral("Media Inspector"),
                                 QStringLiteral("Open a media file first."));
        return;
    }
    const QString path = mVideoDecoder.getFilePath();
    QProcess probe;
    probe.start(QStringLiteral("ffprobe"),
                {QStringLiteral("-v"), QStringLiteral("error"),
                 QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
                 QStringLiteral("-show_chapters"), QStringLiteral("-show_programs"),
                 path});
    QString report;
    if (probe.waitForStarted(3000) && probe.waitForFinished(15000))
        report = QString::fromUtf8(probe.readAllStandardOutput());
    else
        report = QStringLiteral("ffprobe could not inspect this source.\n");

    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray header = file.read(16 * 1024 * 1024);
        if (header.size() >= 12
            && (header.startsWith("RIFF") || header.startsWith("RF64"))) {
            const auto le32 = [&header](qsizetype offset) -> quint32 {
                const auto *p = reinterpret_cast<const uchar *>(
                    header.constData() + offset);
                return static_cast<quint32>(p[0])
                    | (static_cast<quint32>(p[1]) << 8)
                    | (static_cast<quint32>(p[2]) << 16)
                    | (static_cast<quint32>(p[3]) << 24);
            };
            report.prepend(QString("RIFF/RF64 top-level chunks (%1):\n")
                               .arg(QString::fromLatin1(header.mid(8, 4))));
            qsizetype offset = 12;
            int chunkCount = 0;
            while (offset + 8 <= header.size() && chunkCount < 10000) {
                const QByteArray id = header.mid(offset, 4);
                const quint32 size = le32(offset + 4);
                QString detail = QString("  0x%1  %2  %3 bytes")
                    .arg(offset, 8, 16, QLatin1Char('0'))
                    .arg(QString::fromLatin1(id))
                    .arg(size);
                if (id == QByteArray("LIST") && offset + 12 <= header.size())
                    detail += QString(" (%1)").arg(
                        QString::fromLatin1(header.mid(offset + 8, 4)));
                report.prepend(detail + QLatin1Char('\n'));
                if (size == 0xffffffffU) break;
                const quint64 next = static_cast<quint64>(offset) + 8
                    + size + (size & 1U);
                if (next <= static_cast<quint64>(offset)
                    || next > static_cast<quint64>(header.size())) break;
                offset = static_cast<qsizetype>(next);
                ++chunkCount;
            }
            report.prepend(QStringLiteral("\n"));
        }
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Media / RIFF Inspector"));
    dialog.resize(900, 680);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setPlainText(report.trimmed());
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void VDQtMainWindow::onToolsHexViewer() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::information(this, QStringLiteral("Hex Viewer"),
                                 QStringLiteral("Open a file first."));
        return;
    }
    QFile file(mVideoDecoder.getFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, QStringLiteral("Hex Viewer"),
                              file.errorString());
        return;
    }
    constexpr qint64 kLimit = 1024 * 1024;
    const QByteArray data = file.read(kLimit);
    QString text;
    text.reserve(data.size() * 4 + 256);
    for (qsizetype offset = 0; offset < data.size(); offset += 16) {
        text += QString("%1  ").arg(offset, 8, 16, QLatin1Char('0'));
        QString ascii;
        for (int column = 0; column < 16; ++column) {
            if (offset + column < data.size()) {
                const uchar byte = static_cast<uchar>(data.at(offset + column));
                text += QString("%1 ").arg(byte, 2, 16, QLatin1Char('0'));
                ascii += byte >= 32 && byte < 127 ? QChar(byte) : QLatin1Char('.');
            } else {
                text += QStringLiteral("   ");
                ascii += QLatin1Char(' ');
            }
            if (column == 7) text += QLatin1Char(' ');
        }
        text += QStringLiteral(" |") + ascii + QStringLiteral("|\n");
    }
    if (!file.atEnd())
        text += QString("\nDisplay limited to the first %1 MiB of this %2-byte file.")
                    .arg(kLimit / (1024 * 1024)).arg(file.size());
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Hex Viewer — %1")
                              .arg(QFileInfo(file.fileName()).fileName()));
    dialog.resize(960, 700);
    auto *layout = new QVBoxLayout(&dialog);
    auto *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setPlainText(text);
    layout->addWidget(view);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void VDQtMainWindow::onToolsBackendCatalog() {
    VDQtPluginHost::instance().reload();
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
    addTextTab(QStringLiteral("VirtualDub Plugins"),
               VDQtPluginHost::instance().report());

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
        "AviSynth+ loads Linux-native shared-object plugins from its configured "
        "plugin directories. This list is separate from the VirtualDub-compatible "
        "native module host shown on the preceding tab.\n");
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
    auto *inputFormat = new QComboBox(&dialog);
    inputFormat->setEditable(true);
    inputFormat->addItem(QStringLiteral("Automatic"), QString());
    inputFormat->addItem(QStringLiteral("Motion JPEG"), QStringLiteral("mjpeg"));
    inputFormat->addItem(QStringLiteral("YUYV 4:2:2"), QStringLiteral("yuyv422"));
    inputFormat->addItem(QStringLiteral("NV12"), QStringLiteral("nv12"));
    auto *inspectDevice = new QPushButton(
        QStringLiteral("Inspect device formats and controls..."), &dialog);
    auto *controlValues = new QLineEdit(&dialog);
    controlValues->setPlaceholderText(
        QStringLiteral("brightness=128,contrast=32"));
    auto *applyControls = new QPushButton(
        QStringLiteral("Apply controls"), &dialog);
    auto *controlRow = new QHBoxLayout;
    controlRow->addWidget(controlValues, 1);
    controlRow->addWidget(applyControls);
    auto *livePreview = new QCheckBox(
        QStringLiteral("Show live preview and audio level while capturing"),
        &dialog);
    livePreview->setChecked(true);
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
    auto *timedStop = new QCheckBox(QStringLiteral("Stop automatically after"), &dialog);
    auto *durationSeconds = new QSpinBox(&dialog);
    durationSeconds->setRange(1, 7 * 24 * 60 * 60);
    durationSeconds->setValue(60);
    durationSeconds->setSuffix(QStringLiteral(" seconds"));
    auto *splitCapture = new QCheckBox(
        QStringLiteral("Split capture into files every"), &dialog);
    auto *segmentMinutes = new QSpinBox(&dialog);
    segmentMinutes->setRange(1, 24 * 60);
    segmentMinutes->setValue(30);
    segmentMinutes->setSuffix(QStringLiteral(" minutes"));
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
    form->addRow(QStringLiteral("Capture format:"), inputFormat);
    form->addRow(inspectDevice);
    form->addRow(QStringLiteral("Device controls:"), controlRow);
    form->addRow(QString(), livePreview);
    form->addRow(QString(), captureAudio);
    form->addRow(QStringLiteral("ALSA device:"), audioDevice);
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(width); sizeRow->addWidget(new QLabel(QStringLiteral("×"), &dialog));
    sizeRow->addWidget(height); sizeRow->addStretch();
    form->addRow(QStringLiteral("Frame size:"), sizeRow);
    form->addRow(QStringLiteral("Frame rate:"), frameRate);
    form->addRow(timedStop, durationSeconds);
    form->addRow(splitCapture, segmentMinutes);
    form->addRow(QStringLiteral("Video codec:"), videoCodec);
    form->addRow(QStringLiteral("Audio codec:"), audioCodec);
    form->addRow(QStringLiteral("Output file:"), outputRow);
    connect(captureAudio, &QCheckBox::toggled, audioDevice, &QWidget::setEnabled);
    connect(captureAudio, &QCheckBox::toggled, audioCodec, &QWidget::setEnabled);
    connect(timedStop, &QCheckBox::toggled,
            durationSeconds, &QWidget::setEnabled);
    durationSeconds->setEnabled(false);
    connect(splitCapture, &QCheckBox::toggled,
            segmentMinutes, &QWidget::setEnabled);
    segmentMinutes->setEnabled(false);
    connect(inspectDevice, &QPushButton::clicked, &dialog, [&]() {
        QProcess probe;
        QString text;
        const QString controller = QStandardPaths::findExecutable(
            QStringLiteral("v4l2-ctl"));
        if (!controller.isEmpty()) {
            probe.start(controller,
                        {QStringLiteral("--device"),
                         videoDevice->currentText().trimmed(),
                         QStringLiteral("--all"),
                         QStringLiteral("--list-formats-ext"),
                         QStringLiteral("--list-ctrls-menus")});
            if (probe.waitForStarted(3000) && probe.waitForFinished(10000))
                text = QString::fromLocal8Bit(probe.readAllStandardOutput())
                     + QString::fromLocal8Bit(probe.readAllStandardError());
        } else {
            probe.start(QStringLiteral("ffmpeg"),
                        {QStringLiteral("-hide_banner"), QStringLiteral("-f"),
                         QStringLiteral("v4l2"), QStringLiteral("-list_formats"),
                         QStringLiteral("all"), QStringLiteral("-i"),
                         videoDevice->currentText().trimmed()});
            probe.waitForStarted(3000);
            probe.waitForFinished(10000);
            text = QString::fromLocal8Bit(probe.readAllStandardError());
        }
        QDialog details(&dialog);
        details.setWindowTitle(QStringLiteral("Capture Device Information"));
        details.resize(760, 520);
        auto *detailsLayout = new QVBoxLayout(&details);
        auto *view = new QPlainTextEdit(text.trimmed(), &details);
        view->setReadOnly(true);
        detailsLayout->addWidget(view);
        auto *close = new QDialogButtonBox(
            QDialogButtonBox::Close, &details);
        connect(close, &QDialogButtonBox::rejected,
                &details, &QDialog::reject);
        detailsLayout->addWidget(close);
        details.exec();
    });
    connect(applyControls, &QPushButton::clicked, &dialog, [&]() {
        const QString controller = QStandardPaths::findExecutable(
            QStringLiteral("v4l2-ctl"));
        if (controller.isEmpty()) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Capture Controls"),
                QStringLiteral("Install v4l-utils to change camera controls."));
            return;
        }
        const QString values = controlValues->text().trimmed();
        if (values.isEmpty()) {
            QMessageBox::information(
                &dialog, QStringLiteral("Capture Controls"),
                QStringLiteral("Enter one or more name=value controls, separated by commas."));
            return;
        }
        QProcess setter;
        setter.start(controller,
                     {QStringLiteral("--device"),
                      videoDevice->currentText().trimmed(),
                      QStringLiteral("--set-ctrl"), values});
        if (!setter.waitForStarted(3000)
            || !setter.waitForFinished(5000)
            || setter.exitStatus() != QProcess::NormalExit
            || setter.exitCode() != 0) {
            QMessageBox::critical(
                &dialog, QStringLiteral("Capture Controls"),
                QString::fromLocal8Bit(setter.readAllStandardError()).trimmed());
            return;
        }
        QMessageBox::information(
            &dialog, QStringLiteral("Capture Controls"),
            QStringLiteral("The capture-device controls were applied."));
    });
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

    QTemporaryDir segmentStaging(QFileInfo(outputPath).dir().filePath(
        QStringLiteral(".virtualdub-capture-XXXXXX")));
    QTemporaryFile staged(stagedOutputTemplate(outputPath));
    staged.setAutoRemove(true);
    QString stagedPath;
    QString capturePath;
    if (splitCapture->isChecked()) {
        if (!segmentStaging.isValid()) {
            QMessageBox::critical(this, "Capture Error",
                                  "A segment staging directory could not be created beside the output.");
            return;
        }
        QString suffix = QFileInfo(outputPath).suffix();
        if (suffix.isEmpty()) suffix = QStringLiteral("mkv");
        capturePath = segmentStaging.filePath(
            QStringLiteral("capture-%06d.") + suffix);
    } else {
        if (!staged.open()) {
            QMessageBox::critical(this, "Capture Error",
                                  "A staging file could not be created beside the output.");
            return;
        }
        stagedPath = staged.fileName();
        staged.close();
        capturePath = stagedPath;
    }
    QStringList arguments{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        captureAudio->isChecked()
            ? QStringLiteral("info") : QStringLiteral("warning"),
        QStringLiteral("-thread_queue_size"), QStringLiteral("1024"),
        QStringLiteral("-f"), QStringLiteral("v4l2"),
        QStringLiteral("-framerate"), QString::number(frameRate->value(), 'f', 3),
        QStringLiteral("-video_size"),
        QString("%1x%2").arg(width->value()).arg(height->value()),
    };
    if (!inputFormat->currentData().toString().isEmpty())
        arguments << QStringLiteral("-input_format")
                  << inputFormat->currentData().toString();
    arguments << QStringLiteral("-i") << videoDevice->currentText().trimmed();
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
        arguments << QStringLiteral("-af")
                  << QStringLiteral("ebur128=metadata=1")
                  << QStringLiteral("-c:a") << selectedAudioCodec;
    else
        arguments << QStringLiteral("-an");
    if (timedStop->isChecked())
        arguments << QStringLiteral("-t")
                  << QString::number(durationSeconds->value());
    if (splitCapture->isChecked()) {
        const int segmentSeconds = segmentMinutes->value() * 60;
        if (selectedVideoCodec != QStringLiteral("rawvideo"))
            arguments << QStringLiteral("-force_key_frames")
                      << QString("expr:gte(t,n_forced*%1)").arg(segmentSeconds);
        arguments << QStringLiteral("-f") << QStringLiteral("segment")
                  << QStringLiteral("-segment_time")
                  << QString::number(segmentSeconds)
                  << QStringLiteral("-reset_timestamps") << QStringLiteral("1");
    }
    arguments << QStringLiteral("-stats_period") << QStringLiteral("0.25")
              << QStringLiteral("-y") << capturePath;
    if (livePreview->isChecked()) {
        arguments << QStringLiteral("-map") << QStringLiteral("0:v:0")
                  << QStringLiteral("-an")
                  << QStringLiteral("-vf")
                  << QStringLiteral("scale=640:-2:flags=fast_bilinear")
                  << QStringLiteral("-c:v") << QStringLiteral("mjpeg")
                  << QStringLiteral("-q:v") << QStringLiteral("7")
                  << QStringLiteral("-f") << QStringLiteral("image2pipe")
                  << QStringLiteral("pipe:1");
    }

    QProcess capture;
    capture.setProcessChannelMode(QProcess::SeparateChannels);
    capture.start(QStringLiteral("ffmpeg"), arguments, QIODevice::ReadWrite);
    if (!capture.waitForStarted(5000)) {
        if (!stagedPath.isEmpty()) QFile::remove(stagedPath);
        QMessageBox::critical(this, "Capture Error", capture.errorString());
        return;
    }
    QDialog progress(this);
    progress.setWindowTitle(QStringLiteral("Video Capture"));
    progress.setWindowModality(Qt::WindowModal);
    auto *captureLayout = new QVBoxLayout(&progress);
    auto *previewLabel = new QLabel(&progress);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setMinimumSize(
        livePreview->isChecked() ? QSize(640, 360) : QSize(420, 40));
    previewLabel->setStyleSheet(
        QStringLiteral("background:#101014;color:#a0a0aa;border:1px solid #333344;"));
    previewLabel->setText(livePreview->isChecked()
        ? QStringLiteral("Waiting for the first capture frame...")
        : QStringLiteral("Capture is running."));
    auto *captureStats = new QLabel(&progress);
    auto *captureProgress = new QProgressBar(&progress);
    captureProgress->setRange(
        0, timedStop->isChecked() ? durationSeconds->value() : 0);
    auto *stopCapture = new QPushButton(
        QStringLiteral("Stop Capture"), &progress);
    captureLayout->addWidget(previewLabel);
    captureLayout->addWidget(captureStats);
    captureLayout->addWidget(captureProgress);
    captureLayout->addWidget(stopCapture, 0, Qt::AlignRight);
    progress.resize(livePreview->isChecked() ? 680 : 460,
                    livePreview->isChecked() ? 500 : 180);
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray diagnosticTail;
    QByteArray previewBytes;
    bool stopRequested = false;
    qint64 stopRequestedAt = -1;
    qint64 droppedFrames = 0;
    qint64 duplicatedFrames = 0;
    const QRegularExpression droppedExpression(
        QStringLiteral("drop=\\s*(\\d+)"));
    const QRegularExpression duplicatedExpression(
        QStringLiteral("dup=\\s*(\\d+)"));
    const QRegularExpression audioLevelExpression(
        QStringLiteral("M:\\s*(-?(?:\\d+(?:\\.\\d+)?|inf))"));
    double momentaryLevel = -std::numeric_limits<double>::infinity();
    connect(stopCapture, &QPushButton::clicked, &progress,
            [&]() { stopRequested = true; });
    connect(&progress, &QDialog::rejected, &progress,
            [&]() { stopRequested = true; });
    progress.show();
    while (!capture.waitForFinished(100)) {
        diagnosticTail += capture.readAllStandardError();
        if (livePreview->isChecked()) {
            previewBytes += capture.readAllStandardOutput();
            for (;;) {
                const int start = previewBytes.indexOf("\xFF\xD8", 0);
                if (start < 0) {
                    if (previewBytes.size() > 1024 * 1024)
                        previewBytes.clear();
                    break;
                }
                const int end = previewBytes.indexOf("\xFF\xD9", start + 2);
                if (end < 0) {
                    if (start > 0) previewBytes.remove(0, start);
                    break;
                }
                const QByteArray jpeg = previewBytes.mid(start, end - start + 2);
                previewBytes.remove(0, end + 2);
                const QImage frame = QImage::fromData(jpeg, "JPG");
                if (!frame.isNull()) {
                    previewLabel->setPixmap(QPixmap::fromImage(frame).scaled(
                        previewLabel->size(), Qt::KeepAspectRatio,
                        Qt::FastTransformation));
                }
            }
        }
        if (diagnosticTail.size() > 128 * 1024)
            diagnosticTail.remove(0, diagnosticTail.size() - 128 * 1024);
        const QString diagnosticText = QString::fromLocal8Bit(diagnosticTail);
        auto droppedMatches = droppedExpression.globalMatch(diagnosticText);
        while (droppedMatches.hasNext())
            droppedFrames = droppedMatches.next().captured(1).toLongLong();
        auto duplicatedMatches = duplicatedExpression.globalMatch(diagnosticText);
        while (duplicatedMatches.hasNext())
            duplicatedFrames = duplicatedMatches.next().captured(1).toLongLong();
        auto levelMatches = audioLevelExpression.globalMatch(diagnosticText);
        while (levelMatches.hasNext()) {
            bool valid = false;
            const double value = levelMatches.next().captured(1).toDouble(&valid);
            if (valid) momentaryLevel = value;
        }
        const int elapsedSeconds = static_cast<int>(elapsed.elapsed() / 1000);
        if (timedStop->isChecked())
            captureProgress->setValue(
                std::min(elapsedSeconds, durationSeconds->value()));
        captureStats->setText(
            QString("%1 — %2 s    Dropped: %3    Duplicated: %4    Audio: %5")
                .arg(videoDevice->currentText())
                .arg(elapsedSeconds)
                .arg(droppedFrames)
                .arg(duplicatedFrames)
                .arg(captureAudio->isChecked()
                    ? (std::isfinite(momentaryLevel)
                        ? QString("%1 LUFS").arg(momentaryLevel, 0, 'f', 1)
                        : QStringLiteral("waiting"))
                    : QStringLiteral("off")));
        QApplication::processEvents(QEventLoop::AllEvents, 100);
        if (stopRequested && stopRequestedAt < 0) {
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
    diagnosticTail += capture.readAllStandardError();
    progress.close();
    const QStringList capturedSegments = splitCapture->isChecked()
        ? QDir(segmentStaging.path()).entryList(
              {QStringLiteral("capture-*")}, QDir::Files, QDir::Name)
        : QStringList{};
    const bool captured = capture.exitStatus() == QProcess::NormalExit
        && capture.exitCode() == 0
        && (splitCapture->isChecked()
                ? !capturedSegments.isEmpty()
                : QFileInfo(stagedPath).size() > 0);
    bool committed = false;
    if (captured && !splitCapture->isChecked()) {
        committed = replaceWithStagedFile(stagedPath, outputPath);
    } else if (captured) {
        const QFileInfo requested(outputPath);
        const QString suffix = requested.suffix().isEmpty()
            ? QStringLiteral("mkv") : requested.suffix();
        const QString prefix = requested.completeBaseName().isEmpty()
            ? QStringLiteral("capture") : requested.completeBaseName();
        QStringList targets;
        bool safe = true;
        for (int index = 0; index < capturedSegments.size(); ++index) {
            const QString target = requested.dir().filePath(
                QString("%1.%2.%3").arg(prefix).arg(
                    index, 3, 10, QLatin1Char('0')).arg(suffix));
            if (mVideoDecoder.isOpen()
                && !loadedOutputSafety(target, mVideoDecoder, mAudioPlayer,
                                       mTimelineSources).isSafe()) {
                safe = false;
                break;
            }
            targets.append(target);
        }
        QStringList collisions;
        for (const QString& target : targets) {
            if (QFileInfo::exists(target) || QFileInfo(target).isSymLink())
                collisions.append(target);
        }
        if (safe && !collisions.isEmpty()) {
            safe = QMessageBox::warning(
                this, QStringLiteral("Replace Capture Segments?"),
                QString("%1 segment file(s) already exist. Replace them only after capture succeeds?")
                    .arg(collisions.size()),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) == QMessageBox::Yes;
        }
        const QString backupDirectory = segmentStaging.filePath(
            QStringLiteral("backups"));
        QVector<QPair<QString, QString>> backups;
        QStringList installed;
        if (safe) safe = QDir().mkpath(backupDirectory);
        const auto rollback = [&]() {
            for (auto it = installed.crbegin(); it != installed.crend(); ++it)
                QFile::remove(*it);
            for (auto it = backups.crbegin(); it != backups.crend(); ++it)
                QFile::rename(it->second, it->first);
        };
        for (int index = 0; safe && index < targets.size(); ++index) {
            if (!QFileInfo::exists(targets.at(index))
                && !QFileInfo(targets.at(index)).isSymLink()) continue;
            const QString backup = QDir(backupDirectory).filePath(
                QString::number(index));
            safe = QFile::rename(targets.at(index), backup);
            if (safe) backups.append({targets.at(index), backup});
        }
        for (int index = 0; safe && index < targets.size(); ++index) {
            const QString sourceSegment = QDir(segmentStaging.path()).filePath(
                capturedSegments.at(index));
            safe = QFile::rename(sourceSegment, targets.at(index));
            if (safe) installed.append(targets.at(index));
        }
        if (!safe) rollback();
        committed = safe;
    }
    if (!captured || !committed) {
        if (!stagedPath.isEmpty()) QFile::remove(stagedPath);
        QMessageBox::critical(
            this, "Capture Error",
            QString("Capture failed.\n\n%1")
                .arg(QString::fromLocal8Bit(diagnosticTail.right(8192))));
        return;
    }
    statusBar()->showMessage(
        QString("Capture saved: %1%2 (%3 dropped, %4 duplicated)")
            .arg(QFileInfo(outputPath).fileName())
            .arg(splitCapture->isChecked()
                     ? QString(" (%1 segments)").arg(capturedSegments.size())
                     : QString())
            .arg(droppedFrames).arg(duplicatedFrames));
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
            mPlaybackClockFrame = mPlaybackStartFrame;
            mPlaybackOutputPhase = 0;
            mPlaybackClockFrameStartSeconds = 0.0;
            int step = 1;
            if (mFrameRateConfig.convMode == 1) step = 2;
            else if (mFrameRateConfig.convMode == 2) step = 3;
            else if (mFrameRateConfig.convMode == 3)
                step = std::max(1, mFrameRateConfig.decimateN);
            double playbackFps = mFrameRateConfig.sourceMode == 1
                && mFrameRateConfig.customSourceFps > 0.0
                ? mFrameRateConfig.customSourceFps : mVideoDecoder.getFps();
            if (!(playbackFps > 0.0)) playbackFps = 29.97;
            mPlaybackFrameDurationSeconds = step / playbackFps;
            const int sourceFrame = sourceFrameForTimelineFrame(mPlaybackStartFrame);
            mPlaybackAudioOriginSeconds = sourceFrame >= 0
                ? mVideoDecoder.getFrameTimestampSeconds(sourceFrame) : -1.0;
            seekAudioToVideoFrame(static_cast<int>(mPositionControl->GetPosition()));
            mAudioPlayer.play();
            syncInteractiveFilterChain();
            mDecodedPreviewFrames.clear();
            mDecodedPreviewTimelineFrame = -1;
            mPlaybackElapsedTimer.restart();
            mPlaybackTimer->setTimerType(Qt::PreciseTimer);
            mPlaybackTimer->start(mPreferencesConfig.playbackTimerIntervalMs);
            updateFrameDisplay(mPlaybackStartFrame);
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
            mPlaybackClockFrame = mPlaybackStartFrame;
            mPlaybackOutputPhase = 0;
            mPlaybackClockFrameStartSeconds = 0.0;
            int step = 1;
            if (mFrameRateConfig.convMode == 1) step = 2;
            else if (mFrameRateConfig.convMode == 2) step = 3;
            else if (mFrameRateConfig.convMode == 3)
                step = std::max(1, mFrameRateConfig.decimateN);
            double playbackFps = mFrameRateConfig.sourceMode == 1
                && mFrameRateConfig.customSourceFps > 0.0
                ? mFrameRateConfig.customSourceFps : mVideoDecoder.getFps();
            if (!(playbackFps > 0.0)) playbackFps = 29.97;
            mPlaybackFrameDurationSeconds = step / playbackFps;
            const int sourceFrame = sourceFrameForTimelineFrame(mPlaybackStartFrame);
            mPlaybackAudioOriginSeconds = sourceFrame >= 0
                ? mVideoDecoder.getFrameTimestampSeconds(sourceFrame) : -1.0;
            seekAudioToVideoFrame(static_cast<int>(mPositionControl->GetPosition()));
            mAudioPlayer.play();
            syncInteractiveFilterChain();
            mDecodedPreviewFrames.clear();
            mDecodedPreviewTimelineFrame = -1;
            mPlaybackElapsedTimer.restart();
            mPlaybackTimer->setTimerType(Qt::PreciseTimer);
            mPlaybackTimer->start(mPreferencesConfig.playbackTimerIntervalMs);
            updateFrameDisplay(mPlaybackStartFrame);
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

    double elapsedSeconds = mPlaybackElapsedTimer.elapsed() / 1000.0;
    // With an active output device, use the samples actually presented as the
    // clock. Decoder read-ahead can be hundreds of milliseconds ahead and is
    // intentionally not used here.
    if (mFrameRateConfig.sourceMode == 0
        && mPlaybackAudioOriginSeconds >= 0.0) {
        const double audioTime = mAudioPlayer.getPlaybackTimeSeconds();
        if (std::isfinite(audioTime)
            && audioTime + 0.050 >= mPlaybackAudioOriginSeconds) {
            elapsedSeconds = std::max(0.0,
                audioTime - mPlaybackAudioOriginSeconds);
        }
    }

    const double frameDuration = std::max(
        1.0 / 1000.0, mPlaybackFrameDurationSeconds);
    const double withinFrame = elapsedSeconds - mPlaybackClockFrameStartSeconds;
    const int phaseCount = mPlaybackPreview
        ? std::max(1, VDQtFilterSystem::instance()
                          .getTimingInfo().outputFramesPerInput)
        : 1;
    if (withinFrame < frameDuration) {
        const int phase = std::clamp(
            static_cast<int>(std::floor(
                std::max(0.0, withinFrame) * phaseCount / frameDuration)),
            0, phaseCount - 1);
        if (phase != mPlaybackOutputPhase) {
            mPlaybackOutputPhase = phase;
            if (mDecodedPreviewTimelineFrame == mPlaybackClockFrame
                && phase < mDecodedPreviewFrames.size()) {
                mOutputDisplay->setFrameImage(mDecodedPreviewFrames.at(phase));
            }
        }
        return;
    }

    int frameStep = 1;
    if (mFrameRateConfig.convMode == 1) frameStep = 2;
    else if (mFrameRateConfig.convMode == 2) frameStep = 3;
    else if (mFrameRateConfig.convMode == 3)
        frameStep = std::max(1, mFrameRateConfig.decimateN);
    const int framesElapsed = std::max(
        1, static_cast<int>(std::floor(withinFrame / frameDuration)));
    const int targetFrame = mPlaybackClockFrame + framesElapsed * frameStep;

    if (targetFrame != mPlaybackClockFrame) {
        const bool exactTimelineEnd = mTimeline.isModified()
            || mTimeline.sourceFrameCountExact();
        if (exactTimelineEnd && targetFrame >= mTimeline.frameCount()) {
            mPlaybackTimer->stop();
            mAudioPlayer.stop();
            const int lastFrame = static_cast<int>(
                std::max<qint64>(0, mTimeline.frameCount() - 1));
            mPositionControl->SetRange(0, lastFrame);
            mPositionControl->SetPosition(lastFrame);
            return;
        }
        const int currentTimelineFrame = mPlaybackClockFrame;
        const int currentSourceFrame =
            sourceFrameForTimelineFrame(currentTimelineFrame);
        const int targetSourceFrame = sourceFrameForTimelineFrame(targetFrame);
        if (targetSourceFrame >= 0 && currentSourceFrame >= 0
            && targetSourceFrame - currentSourceFrame
                != targetFrame - currentTimelineFrame) {
            seekAudioToVideoFrame(targetFrame);
            mAudioPlayer.play();
            const double timestamp = targetSourceFrame >= 0
                ? mVideoDecoder.getFrameTimestampSeconds(targetSourceFrame) : -1.0;
            mPlaybackAudioOriginSeconds = timestamp;
            mPlaybackElapsedTimer.restart();
            mPlaybackClockFrameStartSeconds = 0.0;
        } else {
            mPlaybackClockFrameStartSeconds += framesElapsed * frameDuration;
        }
        mPlaybackClockFrame = targetFrame;
        mPlaybackOutputPhase = 0;
        if (!mTimeline.sourceFrameCountExact() && mTimeline.isIdentity()
            && targetFrame >= mVideoDecoder.getFrameCount()) {
            mPositionControl->SetRange(
                0, std::max(targetFrame, mVideoDecoder.getFrameCount() - 1));
            mPositionControl->SetPosition(targetFrame);
            return;
        }
        mPositionControl->SetPosition(targetFrame);
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
                                         const QList<QImage>& outputImages,
                                         bool keyFrame,
                                         double timestampSeconds,
                                         double durationSeconds,
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
    mDecodedPreviewFrames = outputImages;
    mDecodedPreviewTimelineFrame = timelineFrame;
    if (!outputImages.isEmpty()) {
        const int phase = mPlaybackTimer->isActive() && mPlaybackPreview
            ? std::clamp(mPlaybackOutputPhase, 0,
                         static_cast<int>(outputImages.size()) - 1) : 0;
        mOutputDisplay->setFrameImage(outputImages.at(phase));
    }
    if (mPlaybackTimer->isActive() && timelineFrame == mPlaybackClockFrame) {
        double adjustedDuration = durationSeconds;
        int step = 1;
        if (mFrameRateConfig.convMode == 1) step = 2;
        else if (mFrameRateConfig.convMode == 2) step = 3;
        else if (mFrameRateConfig.convMode == 3)
            step = std::max(1, mFrameRateConfig.decimateN);
        if (mFrameRateConfig.sourceMode == 1
            && mFrameRateConfig.customSourceFps > 0.0) {
            adjustedDuration = static_cast<double>(step)
                / mFrameRateConfig.customSourceFps;
        } else {
            adjustedDuration *= step;
        }
        if (std::isfinite(adjustedDuration) && adjustedDuration > 0.0)
            mPlaybackFrameDurationSeconds = adjustedDuration;
    }

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
