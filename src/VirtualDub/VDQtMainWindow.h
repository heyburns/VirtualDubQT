#ifndef VDQTMAINWINDOW_H
#define VDQTMAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include "VDQtVideoDisplay.h"
#include "VDQtFrameDecodeWorker.h"
#include "VDQtPositionControl.h"
#include "VDQtDialogs.h"
#include "VDQtVideoDecoder.h"
#include "VDQtAudioPlayer.h"
#include "VDQtVideoExporter.h"
#include "VDQtProjectFile.h"
#include "VDQtJobQueue.h"
#include "VDQtFrameServer.h"
#include "VDQtTimeline.h"
#include "VDQtScriptEngine.h"
#include <QTimer>
#include <QThread>

class VDQtJobControlWindow;
class VDQtSourceSafetySnapshot;

// Top-level application controller. Besides constructing menus, this class is
// the integration boundary between durable session state, the authoritative
// decoder/audio player, the asynchronous interactive-preview worker, timeline
// edits, exporters, automation, jobs, and the frame server.
//
// Decoder ownership is deliberately split: mVideoDecoder supplies metadata,
// export, and shared native AviSynth access; mFrameDecodeWorker owns a second
// decoder on mFrameDecodeThread for responsive scrubbing. AVS is the exception:
// both paths share mVideoDecoder because evaluating third-party script graphs
// twice is unsafe. closeInteractiveDecoder() is synchronous and must run before
// the authoritative decoder or AVS environment is released.
class VDQtMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit VDQtMainWindow(QWidget *parent = nullptr);
    virtual ~VDQtMainWindow();

    bool openVideoFile(const QString& filePath);
    bool runAutomationScript(const QString& scriptPath,
                             QString *errorMessage = nullptr);
    bool runAutomationText(const QString& scriptText,
                           const QString& baseDirectory,
                           QString *errorMessage = nullptr);
    void setAutomationUnattended(bool unattended) {
        mAutomationUnattended = unattended;
    }
    bool automationExitRequested() const { return mAutomationExitRequested; }
    int automationExitCode() const { return mAutomationExitCode; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    // Menu Handlers
    void onFileOpen();
    void onFileReopen();
    void onFileAppendSegment();
    void onFileOpenImageSequence();
    void onFileOpenRawVideo();
    void onFileClose();
    void onFileInformation();
    void onFileSetTextInformation();
    void onFileLoadProject();
    void onFileSaveProject();
    void onFileSaveProjectAs();
    void onFileSaveAVI();
    void onFileSaveSegmentedAVI();
    void onFileSaveAudio();
    void onFileRunAnalysisPass();
    void onFileExportRawVideo();
    void onFileSaveImageSequence();
    void onFileExportAnimatedGIF();
    void onFileExportAnimatedPNG();
    void onFileExportFilmstrip();
    void onFileExportViaEncoderSet();
    void onFileLoadProcessingSettings();
    void onFileSaveProcessingSettings();
    void onFileRunScript();
    void onFileScriptEditor();
    void onFileJobControl();
    void onFileBatchWizard();
    void onFileStartFrameServer();
    void onFileStopFrameServer();
    void onOpenRecentFile();
    void onFileQuit();

    void onEditSetSelectionStart();
    void onEditSetSelectionEnd();
    void onEditSelectAll();
    void onEditUndo();
    void onEditRedo();
    void onEditCut();
    void onEditCopy();
    void onEditPaste();
    void onEditDelete();
    void onEditCropToSelection();
    void onEditJumpToPosition();
    void onEditResetTimeline();
    void onEditPreviousSceneChange();
    void onEditNextSceneChange();
    void onEditToggleMarker();
    void onEditPreviousMarker();
    void onEditNextMarker();
    void onEditClearMarkers();
    void onEditZoomToSelection();
    void onEditClearTimelineZoom();

    void onViewDualView();
    void onViewInputOnly();
    void onViewOutputOnly();
    void onViewLogWindow();
    void onViewAudioWaveform();

    void onVideoModeDirectStream();
    void onVideoModeFastRecompress();
    void onVideoModeNormalRecompress();
    void onVideoModeFullProcessing();
    void onVideoDecodeFormat();
    void onVideoCompression();
    void onVideoFilters();
    void onVideoFrameRate();
    void onVideoSelectRange();
    void onVideoCopySourceFrame();
    void onVideoCopyOutputFrame();
    void onVideoCopySourceFrameNum();
    void onVideoCopyOutputFrameNum();
    void onVideoScanErrors();
    void onVideoErrorMode();

    void onAudioModeDirectStream();
    void onAudioModeFullProcessing();
    void onAudioSource();
    void onAudioCompression();
    void onAudioFilters();

    void onOptionsPreferences();

    void onToolsBackendCatalog();
    void onToolsSystemInformation();
    void onToolsHistogram();
    void onToolsPerformanceProfiler();
    void onToolsMediaInspector();
    void onToolsHexViewer();
    void onCaptureVideo();

    void onHelpAbout();

    void onPositionChanged(int frame);
    void onTransportAction(int actionCode);
    void onPlaybackTick();
    void onDecodedFrameReady(int frameIndex,
                             quint64 generation,
                             const QImage& inputImage,
                             const QList<QImage>& outputImages,
                             bool keyFrame,
                             double timestampSeconds,
                             double durationSeconds,
                             int frameCount,
                             int frameCountStatus,
                             quint64 seekCount,
                             quint64 decodedFrameCount);
    void onDecodedFrameUnavailable(int frameIndex,
                                   quint64 generation,
                                   const QString& errorMessage,
                                   int frameCount,
                                   int frameCountStatus,
                                   bool reachedEndOfStream);
    void runPendingJobs();
    void stopJobQueue();
    void abortCurrentJob();
    void reloadQueuedJob(int row);

private:
    // Public actions may be delivered by timers/scripts as well as mouse input.
    // The RAII scopes protect the complete workflow, including event pumping.
    class OperationScope;
    class SourceTransitionScope;
    class SessionRollbackScope;
    bool editorActionsBlocked() const;
    bool openVideoFileImpl(const QString& filePath);
    void closeVideoSource();
    void releaseVideoSource();
    void finishSourceTransition();
    void scheduleDeferredSourceTransition();
    void performTransportAction(int actionCode);
    // UI construction and display scheduling.
    void createMenus();
    void createStatusBar();
    void applyTheme();
    void autoFitWindowToVideo();
    void updateFrameDisplay(int frameIndex);
    bool openInteractiveDecoder(const QString& filePath, QString *errorMessage);
    void closeInteractiveDecoder();
    void syncInteractiveFilterChain();
    void syncInteractiveFrameIndex();
    void seekAudioToVideoFrame(int frameIndex);
    bool ensureExactFrameRange(const QString& operationLabel);
    // Source/session persistence and special input materialization.
    bool loadProjectFile(const QString& path);
    bool appendVideoSegments(const QStringList& additions,
                             QString *errorMessage);
    bool exportSegmentedVideo(const QString& outputPath,
                              int sizeLimitMb,
                              int frameLimit,
                              int digitCount,
                              int segmentCount,
                              QString *errorMessage);
    bool exportViaEncoderSet(const QString& outputPath,
                             const QString& setName,
                             QString *errorMessage);
    bool startFrameServerAtPath(const QString& pipePath,
                                QString *errorMessage);
    bool materializeRawVideo(const QString& sourcePath,
                             const QString& pixelFormat,
                             int width,
                             int height,
                             double frameRate,
                             qint64 byteOffset,
                             QString *outputPath,
                             QString *errorMessage);
    VDQtProcessingState captureProcessingState() const;
    VDQtProjectState captureProjectState() const;
    void saveRecoverySnapshot();
    void applyProcessingState(const VDQtProcessingState& state);
    VDQtVideoExporter::ExportOptions currentExportOptions(
        const QString& outputPath,
        const QString& containerType,
        bool fastStart,
        bool fullSourceRange = false) const;
    QString primarySessionSourcePath() const;
    QString outputDirectoryForSource(const QFileInfo& source) const;
    void rememberOutputDirectory(const QString& outputPath);
    void updateEditActions();
    void updateTimelineView(qint64 preferredPosition, bool clearSelection);
    void refreshTimelineMarkers();
    bool selectedTimelineRange(qint64 *startFrame, qint64 *endFrameExclusive,
                               const QString& operationLabel);
    int sourceFrameForTimelineFrame(qint64 timelineFrame) const;
    void updateRecentFilesMenu();
    void addRecentFile(const QString& filePath);
    void findSceneChange(bool forward);
    // Queue and command-script execution reuse the same export functions as UI
    // actions, but unattended mode converts modal failures into returned text.
    VDQtJobState currentJobTemplate() const;
    bool executeQueuedJob(int row, QString *errorMessage);
    bool executeImageSequenceJob(VDQtJobState& job,
                                VDQtVideoDecoder& decoder,
                                VDQtSourceSafetySnapshot sourceSafety,
                                QString *errorMessage);
    bool executeAutomationProgram(const VDQtScriptProgram& program,
                                  QString *errorMessage);
    bool exportAutomationVideo(const QString& outputPath,
                               QString *errorMessage,
                               int animationLoopCount = 0,
                               bool animationAlpha = true,
                               bool animationGrayscale = false);
    bool exportAutomationAudio(const QString& outputPath,
                               bool raw, QString *errorMessage);
    bool exportAutomationRawVideo(const QString& outputPath,
                                  const QList<QVariant>& arguments,
                                  QString *errorMessage);
    void exportAnimatedImage(bool animatedPng);

    // Core widgets and authoritative media owners.
    QSplitter *mVideoSplitter;
    VDVideoDisplayWidget *mInputDisplay;
    VDVideoDisplayWidget *mOutputDisplay;
    VDQtPositionControlWidget *mPositionControl;
    VDQtVideoDecoder mVideoDecoder;
    VDQtAudioPlayer mAudioPlayer;
    // Interactive preview thread. Generations invalidate stale queued results;
    // only the newest requested frame is ever allowed to update the display.
    QThread *mFrameDecodeThread = nullptr;
    VDQtFrameDecodeWorker *mFrameDecodeWorker = nullptr;
    quint64 mFrameRequestGeneration = 0;
    // Playback clock. Audio presentation time is preferred when audio exists;
    // elapsed wall time is the fallback. Filter phases support doubled output.
    QTimer *mPlaybackTimer;
    QTimer *mRecoveryTimer = nullptr;
    QString mRecoveryPath;
    // An inherited recovery record is protected until successful restoration or
    // explicit discard. Source switches, timers and a clean exit are not discard.
    bool mRecoverySnapshotProtected = false;
    QElapsedTimer mPlaybackElapsedTimer;
    int mPlaybackStartFrame = 0;
    int mPlaybackPausedFrame = -1;
    bool mPlaybackPreview = false;
    int mPlaybackClockFrame = 0;
    int mPlaybackOutputPhase = 0;
    double mPlaybackClockFrameStartSeconds = 0.0;
    double mPlaybackFrameDurationSeconds = 1.0 / 29.97;
    double mPlaybackAudioOriginSeconds = -1.0;
    QList<QImage> mDecodedPreviewFrames;
    int mDecodedPreviewTimelineFrame = -1;

    // Actions whose checked/enabled state mirrors the session model.
    QMenu *mFileMenu;
    QAction *actFileOpen;
    QAction *actFileReopen;
    QAction *actFileClose;
    QAction *actFileSaveAVI;
    QAction *actFileQuit;
    QList<QAction*> mRecentFileActions;
    QAction *mRecentSeparator;

    QAction *actVideoDirectStream;
    QAction *actVideoFastRecompress;
    QAction *actVideoNormalRecompress;
    QAction *actVideoFullProcessing;
    QAction *actVideoSmartRendering;
    QAction *actVideoPreserveEmptyFrames;
    QAction *actVideoCompression;
    QAction *actVideoFilters;

    QAction *actAudioDirectStream;
    QAction *actAudioFullProcessing;
    QAction *actAudioSource = nullptr;
    QAction *actAudioCompression;
    QAction *actAudioFilters = nullptr;

    QAction *actEditUndo = nullptr;
    QAction *actEditRedo = nullptr;
    QAction *actEditCut = nullptr;
    QAction *actEditCopy = nullptr;
    QAction *actEditPaste = nullptr;
    QAction *actEditDelete = nullptr;
    QAction *actEditCrop = nullptr;
    QAction *actEditJump = nullptr;
    QAction *actEditReset = nullptr;

    // Serializable processing configuration.
    VDFrameRateConfig mFrameRateConfig;
    VDDecompressionFormatConfig mDecompressionFormatConfig;
    VDDecoderErrorModeConfig mDecoderErrorModeConfig;
    VDRawVideoExportConfig mRawVideoExportConfig;
    VDPreferencesConfig mPreferencesConfig;
    QMap<QString, QString> mTextMetadata;
    // A save destination belongs to this application run, not to a source
    // session or persistent preferences. Keep it across Open/Close cycles.
    QString mLastOutputDirectory;
    // Loaded-source/edit-session state. Temporary concat/raw manifests live in
    // mTimelineTempDirectory for exactly as long as this main-window session.
    QString mCurrentProjectPath;
    QTemporaryDir mTimelineTempDirectory;
    QStringList mTimelineSources;
    double mImageSequenceFps = 0.0;
    QString mRawInputPixelFormat;
    int mRawInputWidth = 0;
    int mRawInputHeight = 0;
    double mRawInputFrameRate = 0.0;
    qint64 mRawInputByteOffset = 0;
    VDQtTimeline mTimeline;
    QList<VDQtTimelineSegment> mTimelineClipboard;
    QList<qint64> mTimelineMarkers;
    int mRequestedTimelineFrame = 0;
    bool mFrameRequestPending = false;
    int mQueuedPlaybackFrame = -1;
    // Long-running controllers and queue state.
    VDQtJobQueue *mJobQueue = nullptr;
    VDQtJobControlWindow *mJobControlWindow = nullptr;
    bool mQueueStopRequested = false;
    bool mQueueAbortRequested = false;
    bool mCloseAfterQueueStops = false;
    int mActiveJobIndex = -1;
    VDQtFrameServer *mFrameServer = nullptr;
    QString mFrameServerAudioPath;
    // Current mode/source selections and automation return state.
    int mVideoMode = VideoMode_FullProcessing;
    bool mSmartRendering = false;
    bool mPreserveEmptyFrames = true;
    int mAudioMode = AudioMode_DirectStreamCopy;
    QString mAudioSourcePath;
    int mAudioStreamIndex = -1;
    bool mAudioDisabled = false;
    int mOperationDepth = 0;
    bool mSourceTransitionActive = false;
    bool mAutomationRunning = false;
    bool mDeferredSourcePending = false;
    bool mDeferredSourceScheduled = false;
    QString mDeferredSourcePath; // Empty represents a deferred Close.
    bool mAutomationUnattended = false;
    bool mAutomationExitRequested = false;
    int mAutomationExitCode = 0;
    QString mAutomationContainerType;
    QString mAutomationAudioFormat;
};

#endif // VDQTMAINWINDOW_H
