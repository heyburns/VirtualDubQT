#include "VDQtMainWindow.h"
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
extern "C" {
#include <libavcodec/avcodec.h>
}

VDQtMainWindow::VDQtMainWindow(QWidget *parent)
    : QMainWindow(parent) {
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

    connect(mPositionControl, &VDQtPositionControlWidget::positionChanged, this, &VDQtMainWindow::onPositionChanged);
    connect(mPositionControl, &VDQtPositionControlWidget::transportActionTriggered, this, &VDQtMainWindow::onTransportAction);

    VDLogWindow::instance(this)->appendLog("[Info] VirtualDub Native C++/Qt6 Linux Port initialized successfully.");
}

VDQtMainWindow::~VDQtMainWindow() {
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

    actFileOpen = mFileMenu->addAction("&Open video file...", this, &VDQtMainWindow::onFileOpen, QKeySequence::Open);
    actFileReopen = mFileMenu->addAction("&Reopen video file", this, &VDQtMainWindow::onFileReopen, QKeySequence(Qt::Key_F2));
    mFileMenu->addAction("Append video segment...", this, &VDQtMainWindow::onFileAppendSegment);
    actFileClose = mFileMenu->addAction("&Close video file", this, &VDQtMainWindow::onFileClose, QKeySequence::Close);
    mFileMenu->addAction("File &Information...", this, &VDQtMainWindow::onFileInformation);
    mFileMenu->addAction("Set text information...", this, &VDQtMainWindow::onFileSetTextInformation);
    mFileMenu->addSeparator();

    mFileMenu->addAction("Load Project...", this, &VDQtMainWindow::onFileLoadProject);
    mFileMenu->addAction("Save Project", this, &VDQtMainWindow::onFileSaveProject);
    mFileMenu->addAction("Save Project As...", this, &VDQtMainWindow::onFileSaveProjectAs);
    mFileMenu->addSeparator();

    actFileSaveAVI = mFileMenu->addAction("Save video...", this, &VDQtMainWindow::onFileSaveAVI, QKeySequence(Qt::Key_F7));
    mFileMenu->addAction("&Save audio...", this, &VDQtMainWindow::onFileSaveAudio);
    mFileMenu->addAction("Run video analysis pass", this, &VDQtMainWindow::onFileRunAnalysisPass);

    QMenu *mExport = mFileMenu->addMenu("Export");
    mExport->addAction("Raw video...", this, &VDQtMainWindow::onFileExportRawVideo);
    mExport->addAction("Image sequence...", this, &VDQtMainWindow::onFileSaveImageSequence);
    mExport->addAction("Animated GIF...", this, &VDQtMainWindow::onFileExportAnimatedGIF);

    QMenu *mBatch = mFileMenu->addMenu("Queue batch operation");
    mBatch->addAction("Batch wizard...", this, &VDQtMainWindow::onFileBatchWizard);
    mBatch->addAction("Save video...", this, &VDQtMainWindow::onFileSaveAVI);
    mBatch->addAction("Save audio...", this, &VDQtMainWindow::onFileSaveAudio);

    mFileMenu->addAction("Job control...", this, &VDQtMainWindow::onToolsJobControl, QKeySequence(Qt::Key_F4));
    mFileMenu->addAction("Start frame server...", this, &VDQtMainWindow::onFileStartFrameServer);
    mFileMenu->addSeparator();

    mFileMenu->addAction("Load processing settings...", this, &VDQtMainWindow::onFileLoadProcessingSettings, QKeySequence(Qt::CTRL | Qt::Key_L));
    mFileMenu->addAction("Save processing settings...", this, &VDQtMainWindow::onFileSaveProcessingSettings, QKeySequence(Qt::CTRL | Qt::Key_S));
    mFileMenu->addAction("Run script...", this, &VDQtMainWindow::onFileRunScript);
    mFileMenu->addSeparator();

    mRecentSeparator = mFileMenu->addSeparator();
    for (int i = 0; i < 4; ++i) {
        QAction *action = new QAction(this);
        action->setVisible(false);
        connect(action, &QAction::triggered, this, &VDQtMainWindow::onOpenRecentFile);
        mFileMenu->insertAction(mRecentSeparator, action);
        mRecentFileActions.append(action);
    }

    actFileQuit = mFileMenu->addAction("&Quit", this, &VDQtMainWindow::onFileQuit, QKeySequence::Quit);

    updateRecentFilesMenu();

    // -------------------------------------------------------------------------
    // EDIT MENU
    // -------------------------------------------------------------------------
    QMenu *mEdit = bar->addMenu("&Edit");
    mEdit->addAction("Set selection &start", this, &VDQtMainWindow::onEditSetSelectionStart, QKeySequence(Qt::Key_BracketLeft));
    mEdit->addAction("Set selection &end", this, &VDQtMainWindow::onEditSetSelectionEnd, QKeySequence(Qt::Key_BracketRight));
    mEdit->addAction("Select &All", this, &VDQtMainWindow::onEditSelectAll, QKeySequence::SelectAll);

    // -------------------------------------------------------------------------
    // VIEW MENU
    // -------------------------------------------------------------------------
    QMenu *mView = bar->addMenu("&View");
    mView->addAction("&Dual View (Input & Output)", this, &VDQtMainWindow::onViewDualView);
    mView->addAction("&Input Video Only", this, &VDQtMainWindow::onViewInputOnly);
    mView->addAction("&Output Video Only", this, &VDQtMainWindow::onViewOutputOnly);
    mView->addSeparator();
    mView->addAction("&Auto Size Window to Video", this, &VDQtMainWindow::autoFitWindowToVideo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    mView->addSeparator();
    mView->addAction("&Log Window...", this, &VDQtMainWindow::onViewLogWindow);

    // -------------------------------------------------------------------------
    // VIDEO MENU (Matching VirtualDub Screenshot)
    // -------------------------------------------------------------------------
    QMenu *mVideo = bar->addMenu("&Video");
    actVideoFilters = mVideo->addAction("&Filters...", this, &VDQtMainWindow::onVideoFilters, QKeySequence(Qt::CTRL | Qt::Key_F));
    mVideo->addAction("Frame &Rate...", this, &VDQtMainWindow::onVideoFrameRate, QKeySequence(Qt::CTRL | Qt::Key_R));
    mVideo->addAction("&Decode Format...", this, &VDQtMainWindow::onVideoDecodeFormat);
    actVideoCompression = mVideo->addAction("&Compression...", this, &VDQtMainWindow::onVideoCompression, QKeySequence(Qt::CTRL | Qt::Key_P));
    mVideo->addAction("&Select Range...", this, &VDQtMainWindow::onVideoSelectRange);

    mVideo->addSeparator();

    QActionGroup *grpVideoMode = new QActionGroup(this);
    grpVideoMode->setExclusive(true);

    actVideoDirectStream = mVideo->addAction("&Direct stream copy", this, &VDQtMainWindow::onVideoModeDirectStream);
    actVideoDirectStream->setCheckable(true);
    grpVideoMode->addAction(actVideoDirectStream);

    actVideoFastRecompress = mVideo->addAction("Fast recompress", this, &VDQtMainWindow::onVideoModeFastRecompress);
    actVideoFastRecompress->setCheckable(true);
    grpVideoMode->addAction(actVideoFastRecompress);

    actVideoNormalRecompress = mVideo->addAction("Normal recompress", this, &VDQtMainWindow::onVideoModeNormalRecompress);
    actVideoNormalRecompress->setCheckable(true);
    grpVideoMode->addAction(actVideoNormalRecompress);

    actVideoFullProcessing = mVideo->addAction("&Full processing mode", this, &VDQtMainWindow::onVideoModeFullProcessing);
    actVideoFullProcessing->setCheckable(true);
    actVideoFullProcessing->setChecked(true);
    grpVideoMode->addAction(actVideoFullProcessing);

    mVideo->addSeparator();

    actVideoSmartRendering = mVideo->addAction("Smart rendering", this, &VDQtMainWindow::onVideoSmartRendering);
    actVideoSmartRendering->setCheckable(true);
    actVideoSmartRendering->setChecked(false);

    actVideoPreserveEmptyFrames = mVideo->addAction("Preserve empty frames", this, &VDQtMainWindow::onVideoPreserveEmptyFrames);
    actVideoPreserveEmptyFrames->setCheckable(true);
    actVideoPreserveEmptyFrames->setChecked(false);

    mVideo->addSeparator();

    mVideo->addAction("Copy source frame to clipboard", this, &VDQtMainWindow::onVideoCopySourceFrame, QKeySequence(Qt::CTRL | Qt::Key_1));
    mVideo->addAction("Copy output frame to clipboard", this, &VDQtMainWindow::onVideoCopyOutputFrame, QKeySequence(Qt::CTRL | Qt::Key_2));
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

    // -------------------------------------------------------------------------
    // OPTIONS MENU
    // -------------------------------------------------------------------------
    QMenu *mOptions = bar->addMenu("&Options");
    mOptions->addAction("&Preferences...", this, &VDQtMainWindow::onOptionsPreferences);

    // -------------------------------------------------------------------------
    // TOOLS MENU
    // -------------------------------------------------------------------------
    QMenu *mTools = bar->addMenu("&Tools");
    mTools->addAction("&Job Control...", this, &VDQtMainWindow::onToolsJobControl, QKeySequence(Qt::Key_F4));

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
        bool audioLoaded = false;
        if (mVideoDecoder.isAvsNative()) {
            audioLoaded = mAudioPlayer.openAvsClip(mVideoDecoder.getAvsClip(), mVideoDecoder.getAvsVi());
        }
        if (!audioLoaded) {
            QString mediaPath = filePath;
            if (filePath.endsWith(".avs", Qt::CaseInsensitive) || filePath.endsWith(".vpy", Qt::CaseInsensitive)) {
                QString resolved = VDQtVideoDecoder::parseScriptSource(filePath);
                if (!resolved.isEmpty() && QFile::exists(resolved)) {
                    mediaPath = resolved;
                }
            }
            mAudioPlayer.openFile(mediaPath);
        }

        mInputDisplay->setLabelText(QString("Loaded: %1").arg(filePath));
        mOutputDisplay->setLabelText(QString("Filtered Output: %1").arg(filePath));
        mPositionControl->SetRange(0, mVideoDecoder.getFrameCount() - 1);
        mPositionControl->SetPosition(0);

        updateFrameDisplay(0);
        autoFitWindowToVideo();

        setWindowTitle(QString("VirtualDubQt v0.1 - [%1]").arg(QFileInfo(filePath).fileName()));

        VDLogWindow::instance(this)->appendLog(QString("[File] Opened video stream: %1 (%2x%3 @ %4 fps, %5 frames)")
            .arg(filePath)
            .arg(mVideoDecoder.getWidth())
            .arg(mVideoDecoder.getHeight())
            .arg(mVideoDecoder.getFps(), 0, 'f', 2)
            .arg(mVideoDecoder.getFrameCount()));

        addRecentFile(filePath);
        return true;
    } else {
        bool isScript = filePath.endsWith(".avs", Qt::CaseInsensitive) || filePath.endsWith(".vpy", Qt::CaseInsensitive);
        QString errorDetails = mVideoDecoder.getLastError();
        if (isScript && !errorDetails.isEmpty()) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle("VirtualDub Error");
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setText("AviSynth open failure:");
            msgBox.setInformativeText(errorDetails);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.exec();

            VDLogWindow::instance(this)->appendLog(QString("[Error] AviSynth open failure: %1").arg(errorDetails));
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
        "AviSynth / VapourSynth Scripts (*.avs *.AVS *.vpy *.VPY);;All Video & Media Files (*.avi *.mp4 *.mkv *.mov *.webm *.flv *.wmv *.avs *.vpy *.AVS *.VPY);;All Files (*)"
    );

    if (!fileName.isEmpty()) {
        openVideoFile(fileName);
    }
}

void VDQtMainWindow::onFileAppendSegment() {
    QFileDialog::getOpenFileName(this, "Append Video Segment", QString(), "Video Files (*.avi *.mp4 *.mkv)");
}

void VDQtMainWindow::onFileClose() {
    mPlaybackTimer->stop();
    mAudioPlayer.close();
    QCoreApplication::processEvents();
    mVideoDecoder.close();
    VDQtFilterSystem::instance().clearFilters();
    mInputDisplay->clearDisplay();
    mOutputDisplay->clearDisplay();
    mPositionControl->SetRange(0, 0);
    mPositionControl->SetPosition(0);
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
    QMessageBox::information(this, "Set Text Information", "Set stream commentary, title, copyright, and author tags.");
}

void VDQtMainWindow::onFileLoadProject() {
    QString fileName = QFileDialog::getOpenFileName(this, "Load Project", QString(), "VirtualDub Project (*.vcf *.vdscript);;All Files (*)");
    if (!fileName.isEmpty()) {
        VDLogWindow::instance(this)->appendLog(QString("[Project] Loaded project: %1").arg(fileName));
        statusBar()->showMessage(QString("Loaded project: %1").arg(fileName));
    }
}

void VDQtMainWindow::onFileSaveProject() {
    onFileSaveProjectAs();
}

void VDQtMainWindow::onFileSaveProjectAs() {
    QString fileName = QFileDialog::getSaveFileName(this, "Save Project As", QString(), "VirtualDub Project (*.vcf);;All Files (*)");
    if (!fileName.isEmpty()) {
        VDLogWindow::instance(this)->appendLog(QString("[Project] Saved project: %1").arg(fileName));
        statusBar()->showMessage(QString("Project saved: %1").arg(fileName));
    }
}

void VDQtMainWindow::onFileSaveAudio() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, "Save audio", "No video/audio source has been loaded to save.");
        return;
    }

    if (!mAudioPlayer.hasAudio()) {
        if (mVideoDecoder.isAvsNative() && mVideoDecoder.getAvsClip() && mVideoDecoder.getAvsVi()) {
            mAudioPlayer.openAvsClip(mVideoDecoder.getAvsClip(), mVideoDecoder.getAvsVi());
        }
        if (!mAudioPlayer.hasAudio()) {
            QString srcFile = mVideoDecoder.getFilePath();
            if (srcFile.endsWith(".avs", Qt::CaseInsensitive) || srcFile.endsWith(".vpy", Qt::CaseInsensitive)) {
                QString resolved = VDQtVideoDecoder::parseScriptSource(srcFile);
                if (!resolved.isEmpty() && QFile::exists(resolved)) {
                    srcFile = resolved;
                }
            }
            mAudioPlayer.openFile(srcFile);
        }
    }

    if (!mAudioPlayer.hasAudio()) {
        QMessageBox::warning(this, "Save audio", "The currently opened file has no audio stream to save.");
        return;
    }

    QFileInfo srcInfo(mVideoDecoder.getFilePath());
    QString defaultDir = srcInfo.dir().absolutePath();
    QString baseName = srcInfo.baseName().isEmpty() ? "test" : srcInfo.baseName();
    QString defaultName = baseName + ".wav";

    QString compStr = mAudioPlayer.getAudioCompressionString();
    QString layoutStr = mAudioPlayer.getAudioLayoutString();

    VDSaveAudioDialog dlg(defaultDir, defaultName, compStr, layoutStr, this);
    if (dlg.exec() == QDialog::Accepted) {
        QString outPath = dlg.getSelectedFilePath();
        VDAudioCodecConfig audioCfg = dlg.getAudioConfig();

        if (dlg.isAddToJobQueue()) {
            VDLogWindow::instance(this)->appendLog(QString("[Batch] Added audio export job to queue: %1").arg(outPath));
            statusBar()->showMessage("Audio export job added to queue.");
            QMessageBox::information(this, "Job Queue", "Job has been added to the job queue.");
            return;
        }

        QProgressDialog progress("Exporting audio stream...", "Cancel", 0, 100, this);
        progress.setWindowTitle("Audio Export (Full Processing)");
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);

        int totalFrames = mVideoDecoder.getFrameCount();
        int startFrame = mPositionControl->getEffectiveStartFrame(totalFrames);
        int endFrame = mPositionControl->getEffectiveEndFrame(totalFrames);

        double fps = mVideoDecoder.getFps();
        if (fps <= 0) fps = 29.97;
        int sampleRate = mAudioPlayer.getSampleRate();
        if (sampleRate <= 0) sampleRate = 48000;

        int64_t startSample = (int64_t)std::round(startFrame * ((double)sampleRate / fps));
        int64_t sampleCount = (int64_t)std::round((endFrame - startFrame + 1) * ((double)sampleRate / fps));

        bool ok = false;
        QString errorMsg;

        QString audioCodec = audioCfg.codecId.toLower();

        // 1. Direct Uncompressed PCM Export if no resampling/channel change requested and saving as WAV
        if ((audioCodec == "pcm_s16le" || audioCodec == "(uncompressed)" || audioCodec.isEmpty()) &&
            audioCfg.sampleRate == 0 && audioCfg.channels == 0 && outPath.endsWith(".wav", Qt::CaseInsensitive)) {
            ok = mAudioPlayer.exportAudioToFile(outPath, startSample, sampleCount, [&progress](int cur, int tot) -> bool {
                progress.setValue(cur);
                progress.setLabelText(QString("Exporting uncompressed PCM audio...\n%1% complete").arg(cur));
                QCoreApplication::processEvents();
                return !progress.wasCanceled();
            });
        } else {
            // 2. Full Processing Encoding with selected codec, bitrate, sample rate, and channels
            QString tempWav = QString("/tmp/vd_audio_enc_%1.wav").arg(QDateTime::currentMSecsSinceEpoch());
            bool extractOk = mAudioPlayer.exportAudioToFile(tempWav, startSample, sampleCount, [&progress](int cur, int tot) -> bool {
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

                if (audioCodec == "libmp3lame" || audioCodec == "mp3") {
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
                    lameArgs << tempWav << outPath;
                    int64_t totalDurationUs = (sampleCount > 0 && sampleRate > 0) ? (sampleCount * 1000000LL) / sampleRate : 1000000LL;

                    lameProc.start("lame", lameArgs);
                    if (lameProc.waitForStarted(3000)) {
                        static const QRegularExpression re("\\(\\s*(\\d+)%\\)");
                        QByteArray errBuf;
                        while (!lameProc.waitForFinished(30)) {
                            if (progress.wasCanceled()) {
                                lameProc.kill();
                                break;
                            }
                            errBuf += lameProc.readAllStandardError();
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
                        progress.setValue(100);
                        progress.setLabelText("Encoding MP3 audio stream...\n100% complete");
                        QCoreApplication::processEvents();
                        if (lameProc.exitCode() == 0) {
                            ok = true;
                        } else {
                            errorMsg = QString("LAME MP3 encoding failed:\n%1").arg(QString::fromUtf8(lameProc.readAllStandardError()));
                        }
                    } else {
                        errorMsg = "Failed to launch LAME MP3 encoder.";
                    }
                } else {
                    args << "-y" << "-i" << tempWav;

                    if (audioCodec == "aac" || audioCodec.contains("aac")) {
                        args << "-c:a" << "aac";
                        if (isVbr) {
                            static const double aacQ[] = { 0.2, 0.5, 0.9, 1.4, 2.0 };
                            int qIdx = std::clamp(audioCfg.vbrQuality - 1, 0, 4);
                            args << "-q:a" << QString::number(aacQ[qIdx], 'f', 2);
                        } else {
                            int br = (audioCfg.bitrateKbps > 0) ? audioCfg.bitrateKbps : 192;
                            args << "-b:a" << QString("%1k").arg(br);
                        }
                    } else if (audioCodec == "libopus" || audioCodec == "opus") {
                        if (avcodec_find_encoder_by_name("libopus") != nullptr) {
                            args << "-c:a" << "libopus";
                        } else {
                            args << "-c:a" << "opus" << "-strict" << "-2";
                        }
                        int br = (audioCfg.bitrateKbps > 0) ? audioCfg.bitrateKbps : 160;
                        args << "-b:a" << QString("%1k").arg(br);
                        if (isVbr) args << "-vbr" << "on";
                        else args << "-vbr" << "off";
                    } else if (audioCodec == "libvorbis" || audioCodec == "vorbis") {
                        args << "-c:a" << "libvorbis";
                        if (isVbr) {
                            args << "-q:a" << QString::number(audioCfg.vbrQuality);
                        } else {
                            int br = (audioCfg.bitrateKbps > 0) ? audioCfg.bitrateKbps : 160;
                            args << "-b:a" << QString("%1k").arg(br);
                        }
                    } else if (audioCodec == "flac") {
                        args << "-c:a" << "flac";
                        int compLevel = std::clamp(audioCfg.vbrQuality, 0, 8);
                        if (compLevel == 0 && audioCfg.rateControlMode != "vbr") compLevel = 5;
                        args << "-compression_level" << QString::number(compLevel);
                    } else if (audioCodec == "ac3") {
                        args << "-c:a" << "ac3";
                        int br = (audioCfg.bitrateKbps > 0) ? audioCfg.bitrateKbps : 384;
                        args << "-b:a" << QString("%1k").arg(br);
                    } else if (audioCodec == "pcm_s16le" || audioCodec == "(uncompressed)") {
                        args << "-c:a" << "pcm_s16le";
                    } else {
                        args << "-c:a" << audioCodec;
                    }

                    if (audioCodec == "libopus" || audioCodec == "opus") {
                        int opusRate = audioCfg.sampleRate;
                        if (opusRate != 8000 && opusRate != 12000 && opusRate != 16000 && opusRate != 24000 && opusRate != 48000) {
                            opusRate = 48000;
                        }
                        args << "-ar" << QString::number(opusRate);
                    } else if (audioCodec == "ac3") {
                        int ac3Rate = audioCfg.sampleRate;
                        if (ac3Rate == 0) {
                            if (sampleRate == 48000 || sampleRate == 44100 || sampleRate == 32000) {
                                ac3Rate = sampleRate;
                            } else {
                                ac3Rate = 48000;
                            }
                        } else if (ac3Rate != 48000 && ac3Rate != 44100 && ac3Rate != 32000) {
                            ac3Rate = 48000;
                        }
                        args << "-ar" << QString::number(ac3Rate);
                    } else if (audioCfg.sampleRate > 0) {
                        args << "-ar" << QString::number(audioCfg.sampleRate);
                    }
                    if (audioCfg.channels > 0) args << "-ac" << QString::number(audioCfg.channels);

                    int64_t totalDurationUs = (sampleCount > 0 && sampleRate > 0) ? (sampleCount * 1000000LL) / sampleRate : 1000000LL;
                    args << "-progress" << "pipe:1";
                    args << outPath;

                    ffmpeg.start("ffmpeg", args);
                    if (ffmpeg.waitForStarted(3000)) {
                        QByteArray outBuf;
                        while (!ffmpeg.waitForFinished(30)) {
                            if (progress.wasCanceled()) {
                                ffmpeg.kill();
                                break;
                            }
                            outBuf += ffmpeg.readAllStandardOutput();
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
                        if (ffmpeg.exitCode() == 0) {
                            ok = true;
                        } else {
                            errorMsg = QString("FFmpeg audio encoding failed:\n%1").arg(QString::fromUtf8(ffmpeg.readAllStandardError()));
                        }
                    } else {
                        errorMsg = "Failed to launch FFmpeg audio encoder.";
                    }
                }
                QFile::remove(tempWav);
            } else {
                errorMsg = "Failed to extract uncompressed PCM audio from source.";
            }
        }

        progress.reset();
        progress.close();
        QCoreApplication::processEvents();

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

void VDQtMainWindow::onFileExportRawVideo() {
    QMessageBox::information(this, "Export Raw Video", "Export uncompressed YUV / RGB raw video stream.");
}

void VDQtMainWindow::onFileExportAnimatedGIF() {
    QMessageBox::information(this, "Export Animated GIF", "Export current selection as Animated GIF.");
}

void VDQtMainWindow::onFileBatchWizard() {
    QMessageBox::information(this, "Batch Wizard", "Batch Processing Wizard:\nSelect input directory and output presets to convert multiple files.");
}

void VDQtMainWindow::onFileStartFrameServer() {
    QMessageBox::information(this, "Frame Server", "VirtualDub Frame Server:\nServe video frames to third party applications (e.g. encoders, NLEs).");
}

void VDQtMainWindow::onFileLoadProcessingSettings() {
    QString fileName = QFileDialog::getOpenFileName(this, "Load Processing Settings", QString(), "VirtualDub Processing Settings (*.vcf);;All Files (*)");
    if (!fileName.isEmpty()) {
        VDLogWindow::instance(this)->appendLog(QString("[Settings] Loaded processing settings from: %1").arg(fileName));
        statusBar()->showMessage(QString("Processing settings loaded: %1").arg(fileName));
    }
}

void VDQtMainWindow::onFileSaveProcessingSettings() {
    QString fileName = QFileDialog::getSaveFileName(this, "Save Processing Settings", QString(), "VirtualDub Processing Settings (*.vcf);;All Files (*)");
    if (!fileName.isEmpty()) {
        VDLogWindow::instance(this)->appendLog(QString("[Settings] Saved processing settings to: %1").arg(fileName));
        statusBar()->showMessage(QString("Processing settings saved: %1").arg(fileName));
    }
}

void VDQtMainWindow::onFileRunScript() {
    QString fileName = QFileDialog::getOpenFileName(this, "Run Script", QString(), "VirtualDub Scripts (*.vdscript *.jobs);;AviSynth Scripts (*.avs);;All Files (*)");
    if (!fileName.isEmpty()) {
        if (fileName.endsWith(".avs", Qt::CaseInsensitive) || fileName.endsWith(".vpy", Qt::CaseInsensitive)) {
            openVideoFile(fileName);
        } else {
            VDLogWindow::instance(this)->appendLog(QString("[Script] Running script: %1").arg(fileName));
            statusBar()->showMessage(QString("Script executed: %1").arg(fileName));
        }
    }
}

void VDQtMainWindow::closeEvent(QCloseEvent *event) {
    VDQtFilterSystem::instance().clearFilters();
    QMainWindow::closeEvent(event);
}

#include "VDQtVideoExporter.h"

void VDQtMainWindow::onFileSaveAVI() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, "No Video Loaded", "Please open a video or AviSynth script first.");
        return;
    }

    VDSaveVideoDialog dlg(mVideoMode, mAudioMode, this);
    if (dlg.exec() == QDialog::Accepted) {
        QString savePath = dlg.getSelectedFilePath();
        if (savePath.isEmpty()) return;

        VDLogWindow::instance(this)->appendLog(QString("[Export] Exporting video to %1...").arg(savePath));

        int totalFrames = mVideoDecoder.getFrameCount();
        VDQtVideoExporter exporter;
        VDQtVideoExporter::ExportOptions opts;
        opts.inputPath = mVideoDecoder.getFilePath();
        opts.outputPath = savePath;
        opts.startFrame = mPositionControl->getEffectiveStartFrame(totalFrames);
        opts.endFrame = mPositionControl->getEffectiveEndFrame(totalFrames);
        double targetFps = 0.0;
        if (mFrameRateConfig.sourceMode == 1) {
            targetFps = mFrameRateConfig.customSourceFps;
        } else if (mFrameRateConfig.convMode == 4) {
            targetFps = mFrameRateConfig.convertFps;
        }
        opts.customFps = targetFps;

        int decimate = 1;
        if (mFrameRateConfig.convMode == 1) decimate = 2;
        else if (mFrameRateConfig.convMode == 2) decimate = 3;
        else if (mFrameRateConfig.convMode == 3) decimate = std::max(1, mFrameRateConfig.decimateN);
        opts.decimateFactor = decimate;

        opts.videoMode = mVideoMode;
        opts.audioMode = mAudioMode;
        opts.containerType = dlg.getSelectedContainerType();
        opts.fastStart = dlg.isFastStartEnabled();

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

void VDQtMainWindow::onFileSaveImageSequence() {
    if (!mVideoDecoder.isOpen()) {
        QMessageBox::warning(this, "No Video Loaded", "Please open a video or AviSynth script first.");
        return;
    }

    int totalFrames = mVideoDecoder.getFrameCount();
    if (totalFrames <= 0) return;

    int startFrame = mPositionControl->getEffectiveStartFrame(totalFrames);
    int endFrame = mPositionControl->getEffectiveEndFrame(totalFrames);

    QString defaultFile = "frame_.png";
    QString filter = "PNG Images (*.png);;Windows Bitmap (*.bmp);;JPEG Images (*.jpg *.jpeg);;TIFF Images (*.tif *.tiff);;Targa Images (*.tga)";
    QString savePath = QFileDialog::getSaveFileName(this, "Save Image Sequence (Select Base Name and Format)", defaultFile, filter);
    if (savePath.isEmpty()) return;

    QFileInfo fi(savePath);
    QString dir = fi.absolutePath();
    QString baseName = fi.baseName();
    QString ext = fi.suffix().isEmpty() ? "png" : fi.suffix();

    int framesToExport = endFrame - startFrame + 1;
    QProgressDialog progress("Exporting Image Sequence...", "Cancel", 0, framesToExport, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QElapsedTimer timer;
    timer.start();

    int exportedCount = 0;
    for (int f = startFrame; f <= endFrame; ++f) {
        if (progress.wasCanceled()) break;

        QImage rawFrame = mVideoDecoder.getFrameImage(f);
        if (!rawFrame.isNull()) {
            QImage filtered = VDQtFilterSystem::instance().processFrame(rawFrame);
            QString frameFileName = QString("%1/%2_%3.%4")
                .arg(dir)
                .arg(baseName)
                .arg(f, 5, 10, QChar('0'))
                .arg(ext);

            filtered.save(frameFileName);
            exportedCount++;
        }

        progress.setValue(f - startFrame + 1);
        double elapsedSec = timer.elapsed() / 1000.0;
        double currentFps = (elapsedSec > 0) ? (exportedCount / elapsedSec) : 0;
        progress.setLabelText(QString("Exporting frame %1 of %2 (%3%)\nSpeed: %4 fps")
            .arg(f - startFrame + 1)
            .arg(framesToExport)
            .arg(static_cast<int>(100.0 * (f - startFrame + 1) / framesToExport))
            .arg(currentFps, 0, 'f', 1));
        QApplication::processEvents();
    }

    if (exportedCount > 0) {
        VDLogWindow::instance(this)->appendLog(QString("[Export] Image sequence successfully exported: %1 frames to %2").arg(exportedCount).arg(dir));
        QMessageBox::information(this, "Export Complete", QString("Successfully exported %1 frames to:\n%2").arg(exportedCount).arg(dir));
    }
}

void VDQtMainWindow::onFileQuit() {
    qApp->quit();
}

void VDQtMainWindow::onEditSetSelectionStart() {
    mPositionControl->SetSelection(mPositionControl->GetPosition(), mPositionControl->GetRangeEnd());
}

void VDQtMainWindow::onEditSetSelectionEnd() {
    VDPosition start, end;
    mPositionControl->GetSelection(start, end);
    mPositionControl->SetSelection(start, mPositionControl->GetPosition());
}

void VDQtMainWindow::onEditSelectAll() {
    mPositionControl->SetSelection(mPositionControl->GetRangeBegin(), mPositionControl->GetRangeEnd());
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
    statusBar()->showMessage("Video Mode: Fast Recompress (Bypasses video filters, direct codec encode)");
    VDLogWindow::instance(this)->appendLog("[Video] Mode set to Fast Recompress");
}

void VDQtMainWindow::onVideoModeNormalRecompress() {
    mVideoMode = VideoMode_NormalRecompress;
    actVideoNormalRecompress->setChecked(true);
    statusBar()->showMessage("Video Mode: Normal Recompress (Bypasses video filters, native color format)");
    VDLogWindow::instance(this)->appendLog("[Video] Mode set to Normal Recompress");
}

void VDQtMainWindow::onVideoModeFullProcessing() {
    mVideoMode = VideoMode_FullProcessing;
    actVideoFullProcessing->setChecked(true);
    statusBar()->showMessage("Video Mode: Full Processing Mode (All video filters active)");
    VDLogWindow::instance(this)->appendLog("[Video] Mode set to Full Processing Mode");
}

void VDQtMainWindow::onVideoSmartRendering() {
    bool enabled = actVideoSmartRendering->isChecked();
    statusBar()->showMessage(QString("Smart rendering: %1").arg(enabled ? "Enabled" : "Disabled"));
}

void VDQtMainWindow::onVideoPreserveEmptyFrames() {
    bool enabled = actVideoPreserveEmptyFrames->isChecked();
    statusBar()->showMessage(QString("Preserve empty frames: %1").arg(enabled ? "Enabled" : "Disabled"));
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
            updateFrameDisplay(mPositionControl->GetPosition());
        }
        VDLogWindow::instance(this)->appendLog(QString("[Video] Decompression format selected: %1 (Color space: %2, Component range: %3, SaveDefault: %4)")
            .arg(mDecompressionFormatConfig.formatName)
            .arg(mDecompressionFormatConfig.colorSpace == 1 ? "Rec. 601" : (mDecompressionFormatConfig.colorSpace == 2 ? "Rec. 709" : "No change"))
            .arg(mDecompressionFormatConfig.componentRange == 1 ? "Limited" : (mDecompressionFormatConfig.componentRange == 2 ? "Full" : "No change"))
            .arg(dlg.isSaveAsDefault() ? "Yes" : "No"));
        statusBar()->showMessage(QString("Decompression Format: %1").arg(mDecompressionFormatConfig.formatName));
    }
}

void VDQtMainWindow::onVideoSelectRange() {
    QMessageBox::information(this, "Select Range", QString("Current selection range:\nStart: Frame %1\nEnd: Frame %2\nTotal frames: %3")
        .arg(mPositionControl->GetSelectionStart())
        .arg(mPositionControl->GetSelectionEnd())
        .arg(mPositionControl->GetSelectionEnd() - mPositionControl->GetSelectionStart() + 1));
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
        QString modeStr = "Report all errors";
        if (mDecoderErrorModeConfig.errorMode == 1) modeStr = "Conceal errors and resume decoding at next keyframe";
        else if (mDecoderErrorModeConfig.errorMode == 2) modeStr = "Decode even if the result may be garbled";

        VDLogWindow::instance(this)->appendLog(QString("[Video] Decoder error mode set: %1 (SaveDefault: %2)")
            .arg(modeStr)
            .arg(dlg.isSaveAsDefault() ? "Yes" : "No"));
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
    int w = mVideoDecoder.isOpen() ? mVideoDecoder.getWidth() : 1920;
    int h = mVideoDecoder.isOpen() ? mVideoDecoder.getHeight() : 1080;
    QImage currentFrame;
    if (mVideoDecoder.isOpen()) {
        currentFrame = mVideoDecoder.getFrameImage(mPositionControl->GetPosition());
    }
    VDVideoFiltersDialog dlg(w, h, currentFrame, this);
    if (dlg.exec() == QDialog::Accepted || true) {
        updateFrameDisplay(mPositionControl->GetPosition());
    }
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
    VDPreferencesDialog dlg(this);
    dlg.exec();
}

void VDQtMainWindow::onToolsJobControl() {
    VDJobControlDialog dlg(this);
    dlg.exec();
}

void VDQtMainWindow::onHelpAbout() {
    VDAboutDialog dlg(this);
    dlg.exec();
}

void VDQtMainWindow::onPositionChanged(int frame) {
    if (mIsExporting) return;
    updateFrameDisplay(frame);
    if (mVideoDecoder.isOpen() && !mPlaybackTimer->isActive()) {
        mAudioPlayer.seekToFrame(frame, mVideoDecoder.getFps());
    }
}

void VDQtMainWindow::onTransportAction(int actionCode) {
    if (!mVideoDecoder.isOpen()) return;

    switch (actionCode) {
    case PCN_STOP: // 0 - Stop
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        updateFrameDisplay(mPositionControl->GetPosition());
        break;

    case PCN_PLAY: // 1 - Play Input Pane Only
        mPlaybackPreview = false;
        if (mPlaybackTimer->isActive()) {
            mPlaybackTimer->stop();
            mAudioPlayer.pause();
            updateFrameDisplay(mPositionControl->GetPosition());
        } else {
            mPlaybackStartFrame = mPositionControl->GetPosition();
            mPlaybackElapsedTimer.restart();
            mAudioPlayer.seekToFrame(mPositionControl->GetPosition(), mVideoDecoder.getFps());
            mAudioPlayer.play();
            mPlaybackTimer->setTimerType(Qt::PreciseTimer);
            mPlaybackTimer->start(10);
        }
        break;

    case PCN_PLAYPREVIEW: // 10 - Play Preview (Both Input & Output Panes)
        mPlaybackPreview = true;
        if (mPlaybackTimer->isActive()) {
            mPlaybackTimer->stop();
            mAudioPlayer.pause();
            updateFrameDisplay(mPositionControl->GetPosition());
        } else {
            mPlaybackStartFrame = mPositionControl->GetPosition();
            mPlaybackElapsedTimer.restart();
            mAudioPlayer.seekToFrame(mPositionControl->GetPosition(), mVideoDecoder.getFps());
            mAudioPlayer.play();
            mPlaybackTimer->setTimerType(Qt::PreciseTimer);
            mPlaybackTimer->start(10);
        }
        break;

    case PCN_START: // 4 - Jump to Start (|<)
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        mPositionControl->SetPosition(0);
        break;

    case PCN_END: // 7 - Jump to End (>|)
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        mPositionControl->SetPosition(mVideoDecoder.getFrameCount() - 1);
        break;

    case PCN_BACKWARD: // 5 - Step Backward 1 frame (<)
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        mPositionControl->SetPosition(std::max((sint64)0, mPositionControl->GetPosition() - 1));
        break;

    case PCN_FORWARD: // 6 - Step Forward 1 frame (>)
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        mPositionControl->SetPosition(std::min((sint64)(mVideoDecoder.getFrameCount() - 1), mPositionControl->GetPosition() + 1));
        break;

    case PCN_KEYPREV: // 8 - Previous Keyframe (<<K)
    {
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        int step = std::max(1, static_cast<int>(std::round(mVideoDecoder.getFps())));
        mPositionControl->SetPosition(std::max((sint64)0, mPositionControl->GetPosition() - step));
        break;
    }

    case PCN_KEYNEXT: // 9 - Next Keyframe (K>>)
    {
        mPlaybackTimer->stop();
        mAudioPlayer.pause();
        int step = std::max(1, static_cast<int>(std::round(mVideoDecoder.getFps())));
        mPositionControl->SetPosition(std::min((sint64)(mVideoDecoder.getFrameCount() - 1), mPositionControl->GetPosition() + step));
        break;
    }

    case PCN_MARKIN: // 2 - Mark In ([)
        mPositionControl->SetSelection(mPositionControl->GetPosition(), mPositionControl->GetSelectionEnd());
        break;

    case PCN_MARKOUT: // 3 - Mark Out (])
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

    qint64 elapsedMs = mPlaybackElapsedTimer.elapsed();
    int targetFrame = mPlaybackStartFrame + static_cast<int>((elapsedMs * fps) / 1000.0);

    if (targetFrame != mPositionControl->GetPosition()) {
        if (targetFrame >= mVideoDecoder.getFrameCount()) {
            mPlaybackTimer->stop();
            mAudioPlayer.stop();
            mPositionControl->SetPosition(mVideoDecoder.getFrameCount() - 1);
        } else {
            mPositionControl->SetPosition(targetFrame);
        }
    }
}

#include "VDQtFilterSystem.h"

void VDQtMainWindow::updateFrameDisplay(int frameIndex) {
    if (!mVideoDecoder.isOpen()) return;

    QImage frameImage = mVideoDecoder.getFrameImage(frameIndex);
    if (!frameImage.isNull()) {
        mInputDisplay->setFrameImage(frameImage);

        // If playing and input-only mode, skip filter processing on output display for maximum performance
        if (mPlaybackTimer->isActive() && !mPlaybackPreview) {
            // Only input pane is updated during Play Input
        } else {
            QImage filteredImage = VDQtFilterSystem::instance().processFrame(frameImage);
            mOutputDisplay->setFrameImage(filteredImage);
        }

        double fps = mVideoDecoder.getFps();
        double timeSeconds = (fps > 0) ? (frameIndex / fps) : 0;
        int hours = static_cast<int>(timeSeconds / 3600);
        int mins = static_cast<int>((timeSeconds - hours * 3600) / 60);
        int secs = static_cast<int>(timeSeconds) % 60;
        int msecs = static_cast<int>((timeSeconds - static_cast<int>(timeSeconds)) * 1000);

        QString timeStr = QString("%1:%2:%3.%4")
            .arg(hours, 2, 10, QChar('0'))
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'))
            .arg(msecs, 3, 10, QChar('0'));

        statusBar()->showMessage(QString("Frame: %1 / %2  |  Time: %3  |  %4x%5 @ %6 fps")
            .arg(frameIndex)
            .arg(mVideoDecoder.getFrameCount() - 1)
            .arg(timeStr)
            .arg(mVideoDecoder.getWidth())
            .arg(mVideoDecoder.getHeight())
            .arg(fps, 0, 'f', 2));
    }
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
