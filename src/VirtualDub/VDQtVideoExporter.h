#ifndef VDQTVIDEOEXPORTER_H
#define VDQTVIDEOEXPORTER_H

#include <QString>
#include <QProgressDialog>
#include <QImage>
#include <functional>
#include "VDQtVideoDecoder.h"
#include "VDQtFilterSystem.h"

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
        QString codecName; // e.g. "libx264", "libx265", "mpeg4"
        int crf = 23;      // Constant Rate Factor (e.g. 23)
        int bitrate = 0;   // Target bitrate in kbps if CRF disabled
        QString containerType; // e.g. "mov_faststart", "mp4_faststart", "webm", "mkv"
        bool fastStart = false;
    };

    bool exportVideo(const ExportOptions& options,
                     VDQtVideoDecoder *activeDecoder = nullptr,
                     VDQtAudioPlayer *audioPlayer = nullptr,
                     QWidget *parentWidget = nullptr,
                     std::function<void(int frameIndex, const QImage &rawFrame, const QImage &filteredFrame)> frameCallback = nullptr);
};

#endif // VDQTVIDEOEXPORTER_H
