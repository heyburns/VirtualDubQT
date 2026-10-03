#ifndef VDQTVIDEOEXPORTER_H
#define VDQTVIDEOEXPORTER_H

#include <QString>
#include <QProgressDialog>
#include <QImage>
#include <functional>
#include "VDQtVideoDecoder.h"
#include "VDQtFilterSystem.h"
#include "VDQtTimeline.h"
#include "VDQtCodecEngine.h"
#include "VDQtAudioFilterSystem.h"
#include <optional>

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

// Coordinates every export path. Depending on mode it either asks FFmpeg to
// remux compressed packets, builds a native-planar FFmpeg pipeline (Fast
// Recompress), or pulls QImages through VDQtVideoDecoder/filter/timeline code.
// Output is first written to a temporary sibling and atomically renamed only
// after success, protecting existing destinations from partial exports.
class VDQtVideoExporter {
public:
    VDQtVideoExporter();
    ~VDQtVideoExporter();

    bool wasCancelled() const { return mWasCancelled; }
    QString lastError() const { return mLastError; }

    struct ProcessingSnapshot {
        VDVideoCodecParams videoCodec;
        VDAudioCodecParams audioCodec;
        QList<VDFilterInstance> filters;
        QList<VDAudioFilterInstance> audioFilters;
    };
    static ProcessingSnapshot captureProcessingSnapshot();

    struct ExportOptions {
        // Missing means snapshot the editor once at entry; queued jobs supply
        // their saved settings explicitly without changing the editor.
        std::optional<ProcessingSnapshot> processing;
        QString inputPath;
        QString outputPath;
        // Every directly or indirectly loaded source. SourceSafety rejects an
        // output that aliases any entry, including through hard/symbolic links.
        QStringList protectedSourcePaths;
        int startFrame = 0;
        int endFrame = -1; // Inclusive; -1 selects the known source end.
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
        int animationLoopCount = 0;
        bool animationAlpha = true;
        bool animationGrayscale = false;
        QMap<QString, QString> metadata;
        // Conservatively copies a clean GOP-aligned range and otherwise falls
        // back to the selected recompression mode for frame-exact output.
        bool smartRendering = false;
        bool preserveEmptyFrames = true;
        // Queue/batch execution reports failures through job state instead of
        // opening modal questions or error boxes. Progress remains visible and
        // cancellable.
        bool unattended = false;
        // A missing mapping means source identity; an explicit empty mapping
        // means no frames. Nonempty legacy callers remain explicit automatically.
        bool timelineExplicit = false;
        QList<VDQtTimelineSegment> timelineSegments;
        bool hasExplicitTimeline() const { return timelineExplicit || !timelineSegments.isEmpty(); }
    };

    struct RawExportOptions {
        std::optional<ProcessingSnapshot> processing;
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
        bool unattended = false;
        bool timelineExplicit = false;
        QList<VDQtTimelineSegment> timelineSegments;
        bool hasExplicitTimeline() const { return timelineExplicit || !timelineSegments.isEmpty(); }
    };

    bool exportVideo(const ExportOptions& options,
                     VDQtVideoDecoder *activeDecoder = nullptr,
                     VDQtAudioPlayer *audioPlayer = nullptr,
                     QWidget *parentWidget = nullptr,
                     std::function<void(int frameIndex, const QImage &rawFrame, const QImage &filteredFrame)> frameCallback = nullptr,
                     std::function<bool(int completedFrames, int totalFrames)> progressCallback = nullptr);

    bool exportRawVideo(
        const RawExportOptions& options,
        VDQtVideoDecoder *activeDecoder = nullptr,
        VDQtAudioPlayer *audioPlayer = nullptr,
        QWidget *parentWidget = nullptr,
        std::function<bool(int completedFrames, int totalFrames)> progressCallback = nullptr);

private:
    // Exporter instances are reusable; each public operation resets both fields.
    bool mWasCancelled = false;
    QString mLastError;
};

#endif // VDQTVIDEOEXPORTER_H
