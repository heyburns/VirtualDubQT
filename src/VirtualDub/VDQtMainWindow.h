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

#include "VDQtVideoDisplay.h"
#include "VDQtPositionControl.h"
#include "VDQtDialogs.h"
#include "VDQtVideoDecoder.h"
#include "VDQtAudioPlayer.h"
#include "VDQtVideoExporter.h"
#include <QTimer>

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
    void onFileBatchWizard();
    void onFileStartFrameServer();
    void onFileLoadProcessingSettings();
    void onFileSaveProcessingSettings();
    void onFileRunScript();
    void onOpenRecentFile();
    void onFileQuit();

    void onEditSetSelectionStart();
    void onEditSetSelectionEnd();
    void onEditSelectAll();

    void onViewDualView();
    void onViewInputOnly();
    void onViewOutputOnly();
    void onViewLogWindow();

    void onVideoModeDirectStream();
    void onVideoModeFastRecompress();
    void onVideoModeNormalRecompress();
    void onVideoModeFullProcessing();
    void onVideoSmartRendering();
    void onVideoPreserveEmptyFrames();
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
    void onAudioCompression();

    void onOptionsPreferences();

    void onToolsJobControl();
    void onHelpAbout();

    void onPositionChanged(int frame);
    void onTransportAction(int actionCode);
    void onPlaybackTick();

private:
    void createMenus();
    void createStatusBar();
    void applyTheme();
    void autoFitWindowToVideo();
    void updateFrameDisplay(int frameIndex);
    void updateRecentFilesMenu();
    void addRecentFile(const QString& filePath);

    QSplitter *mVideoSplitter;
    VDVideoDisplayWidget *mInputDisplay;
    VDVideoDisplayWidget *mOutputDisplay;
    VDQtPositionControlWidget *mPositionControl;
    VDQtVideoDecoder mVideoDecoder;
    VDQtAudioPlayer mAudioPlayer;
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
    QAction *actAudioCompression;

    VDFrameRateConfig mFrameRateConfig;
    VDDecompressionFormatConfig mDecompressionFormatConfig;
    VDDecoderErrorModeConfig mDecoderErrorModeConfig;
    int mVideoMode = VideoMode_FullProcessing;
    int mAudioMode = AudioMode_DirectStreamCopy;
    bool mIsExporting = false;
};

#endif // VDQTMAINWINDOW_H
