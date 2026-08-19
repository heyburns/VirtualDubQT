#ifndef VDQTVIDEOEXPORTER_H
#define VDQTVIDEOEXPORTER_H

#include <QString>
#include <QProgressDialog>
#include <QImage>
#include <functional>
#include "VDQtVideoDecoder.h"
#include "VDQtFilterSystem.h"
#include "VDQtTimeline.h"

enum VideoProcessingMode {
    VideoMode_DirectStreamCopy = 0,
    VideoMode_FastRecompress   = 1,
    VideoMode_NormalRecompress = 2,
    VideoMode_FullProcessing   = 3
};

enum AudioProcessingMode {
    AudioMode_DirectStreamCopy = 0,
    AudioMode_FullProcessing   = 1
};

class VDQtAudioPlayer;

class VDQtVideoExporter {
public:
    VDQtVideoExporter();
    ~VDQtVideoExporter();

    struct ExportOptions {
        QString inputPath;
        QString outputPath;
        QStringList protectedSourcePaths;
        int startFrame = 0;
        int endFrame = -1;
        double customFps = 0.0;
        // When true, customFps is a conversion target and source duration is
        // preserved by timestamp-based frame duplication/drop. When false,
        // customFps reinterprets the selected frames at the requested rate.
        bool convertFpsPreserveDuration = false;
        int decimateFactor = 1;
        int videoMode = VideoMode_FullProcessing;
        int audioMode = AudioMode_DirectStreamCopy;
        QString containerType; // e.g. "mov_faststart", "mp4_faststart", "webm", "mkv"
        bool fastStart = false;
        bool includeAudio = true;
        QString videoCodecOverride;
        QString videoPixelFormatOverride;
        QMap<QString, QString> metadata;
        // Conservatively copies a clean GOP-aligned range and otherwise falls
        // back to the selected recompression mode for frame-exact output.
        bool smartRendering = false;
        bool preserveEmptyFrames = true;
        // Empty means the decoder's identity timeline. Non-empty edit lists
        // map output frames to source frames and force frame-accurate render.
        QList<VDQtTimelineSegment> timelineSegments;
    };

    struct RawExportOptions {
        QString inputPath;
        QString outputPath;
        QStringList protectedSourcePaths;
        int startFrame = 0;
        int endFrame = -1;
        double customFps = 0.0;
        bool convertFpsPreserveDuration = false;
        int decimateFactor = 1;
        QString pixelFormat = QStringLiteral("yuv420p");
        int scanlineAlignment = 4;
        bool swapChromaPlanes = true;
        bool bottomUp = false;
        QString colorMatrix = QStringLiteral("bt601");
        bool fullRange = false;
        QList<VDQtTimelineSegment> timelineSegments;
    };

    bool exportVideo(const ExportOptions& options,
                     VDQtVideoDecoder *activeDecoder = nullptr,
                     VDQtAudioPlayer *audioPlayer = nullptr,
                     QWidget *parentWidget = nullptr,
                     std::function<void(int frameIndex, const QImage &rawFrame, const QImage &filteredFrame)> frameCallback = nullptr);

    bool exportRawVideo(
        const RawExportOptions& options,
        VDQtVideoDecoder *activeDecoder = nullptr,
        VDQtAudioPlayer *audioPlayer = nullptr,
        QWidget *parentWidget = nullptr,
        std::function<bool(int completedFrames, int totalFrames)> progressCallback = nullptr);
};

#endif // VDQTVIDEOEXPORTER_H
