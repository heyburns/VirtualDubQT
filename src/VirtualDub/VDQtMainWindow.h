#ifndef VDQTMAINWINDOW_H
#define VDQTMAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QFileDialog>
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
#include "VDQtFrameServer.h"
#include "VDQtTimeline.h"
#include <QTimer>
#include <QThread>

class VDQtMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit VDQtMainWindow(QWidget *parent = nullptr);
    virtual ~VDQtMainWindow();

    bool openVideoFile(const QString& filePath);

protected:
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
    void onFileSaveAudio();
    void onFileRunAnalysisPass();
    void onFileExportRawVideo();
    void onFileSaveImageSequence();
    void onFileExportAnimatedGIF();
    void onFileLoadProcessingSettings();
    void onFileSaveProcessingSettings();
    void onFileRunScript();
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
    void onEditResetTimeline();
    void onEditPreviousSceneChange();
    void onEditNextSceneChange();

    void onViewDualView();
    void onViewInputOnly();
    void onViewOutputOnly();
    void onViewLogWindow();

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
    void onCaptureVideo();

    void onHelpAbout();

    void onPositionChanged(int frame);
    void onTransportAction(int actionCode);
    void onPlaybackTick();
    void onDecodedFrameReady(int frameIndex,
                             quint64 generation,
                             const QImage& inputImage,
                             const QImage& outputImage,
                             bool keyFrame,
                             double timestampSeconds,
                             int frameCount,
                             int frameCountStatus,
                             quint64 seekCount,
                             quint64 decodedFrameCount);
    void onDecodedFrameUnavailable(int frameIndex,
                                   quint64 generation,
                                   const QString& errorMessage,
                                   int frameCount,
                                   int frameCountStatus);

private:
    void createMenus();
    void createStatusBar();
    void applyTheme();
    void autoFitWindowToVideo();
    void updateFrameDisplay(int frameIndex);
    bool openInteractiveDecoder(const QString& filePath, QString *errorMessage);
    void closeInteractiveDecoder();
    void syncInteractiveFilterChain();
    void seekAudioToVideoFrame(int frameIndex);
    bool ensureExactFrameRange(const QString& operationLabel);
    bool loadProjectFile(const QString& path);
    bool materializeRawVideo(const QString& sourcePath,
                             const QString& pixelFormat,
                             int width,
                             int height,
                             double frameRate,
                             qint64 byteOffset,
                             QString *outputPath,
                             QString *errorMessage);
    VDQtProcessingState captureProcessingState() const;
    void applyProcessingState(const VDQtProcessingState& state);
    VDQtVideoExporter::ExportOptions currentExportOptions(
        const QString& outputPath,
        const QString& containerType,
        bool fastStart,
        bool fullSourceRange = false) const;
    QString primarySessionSourcePath() const;
    void updateEditActions();
    void updateTimelineView(qint64 preferredPosition, bool clearSelection);
    bool selectedTimelineRange(qint64 *startFrame, qint64 *endFrameExclusive,
                               const QString& operationLabel);
    int sourceFrameForTimelineFrame(qint64 timelineFrame) const;
    void updateRecentFilesMenu();
    void addRecentFile(const QString& filePath);
    void findSceneChange(bool forward);

    QSplitter *mVideoSplitter;
    VDVideoDisplayWidget *mInputDisplay;
    VDVideoDisplayWidget *mOutputDisplay;
    VDQtPositionControlWidget *mPositionControl;
    VDQtVideoDecoder mVideoDecoder;
    // Native AviSynth audio gets an independently evaluated clip. AviSynth
    // filters are not universally safe when get_audio/get_frame run on the
    // same graph from the audio and video threads.
    VDQtVideoDecoder mAvsAudioDecoder;
    VDQtAudioPlayer mAudioPlayer;
    QThread *mFrameDecodeThread = nullptr;
    VDQtFrameDecodeWorker *mFrameDecodeWorker = nullptr;
    quint64 mFrameRequestGeneration = 0;
    QTimer *mPlaybackTimer;
    QElapsedTimer mPlaybackElapsedTimer;
    int mPlaybackStartFrame = 0;
    bool mPlaybackPreview = false;

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
    QAction *actEditReset = nullptr;

    VDFrameRateConfig mFrameRateConfig;
    VDDecompressionFormatConfig mDecompressionFormatConfig;
    VDDecoderErrorModeConfig mDecoderErrorModeConfig;
    VDRawVideoExportConfig mRawVideoExportConfig;
    VDPreferencesConfig mPreferencesConfig;
    QMap<QString, QString> mTextMetadata;
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
    int mRequestedTimelineFrame = 0;
    bool mFrameRequestPending = false;
    int mQueuedPlaybackFrame = -1;
    struct QueuedVideoJob {
        enum Status { Pending, Running, Complete, Failed, Cancelled };
        QStringList sourcePaths;
        double imageSequenceFps = 0.0;
        QString rawPixelFormat;
        int rawWidth = 0;
        int rawHeight = 0;
        double rawFrameRate = 0.0;
        qint64 rawByteOffset = 0;
        QString audioSourcePath;
        int audioStreamIndex = -1;
        bool audioDisabled = false;
        VDQtVideoExporter::ExportOptions options;
        VDQtProcessingState processing;
        Status status = Pending;
        QString error;
    };
    QList<QueuedVideoJob> mVideoJobs;
    VDQtFrameServer *mFrameServer = nullptr;
    QString mFrameServerAudioPath;
    int mVideoMode = VideoMode_FullProcessing;
    bool mSmartRendering = false;
    bool mPreserveEmptyFrames = true;
    int mAudioMode = AudioMode_DirectStreamCopy;
    QString mAudioSourcePath;
    int mAudioStreamIndex = -1;
    bool mAudioDisabled = false;
    bool mIsExporting = false;
};

#endif // VDQTMAINWINDOW_H
