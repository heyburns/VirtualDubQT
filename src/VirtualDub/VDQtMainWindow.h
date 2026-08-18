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
#include "VDQtFrameDecodeWorker.h"
#include "VDQtPositionControl.h"
#include "VDQtDialogs.h"
#include "VDQtVideoDecoder.h"
#include "VDQtAudioPlayer.h"
#include "VDQtVideoExporter.h"
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
    void onFileClose();
    void onFileInformation();
    void onFileSaveAVI();
    void onFileSaveAudio();
    void onFileRunAnalysisPass();
    void onFileSaveImageSequence();
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
    void onAudioCompression();

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
    void updateRecentFilesMenu();
    void addRecentFile(const QString& filePath);

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
    double mPlaybackStartTimestamp = 0.0;
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
    QAction *actVideoNormalRecompress;
    QAction *actVideoFullProcessing;
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
