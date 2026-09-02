#ifndef VDQTPROJECTFILE_H
#define VDQTPROJECTFILE_H

#include "VDQtCodecEngine.h"
#include "VDQtDialogs.h"
#include "VDQtAudioFilterSystem.h"
#include "VDQtFilterSystem.h"
#include "VDQtTimeline.h"
#include "VDQtVideoExporter.h"

#include <QMap>
#include <QDateTime>
#include <QString>
#include <QStringList>

// Everything that changes how frames/audio are processed, but not which media
// is loaded or how the timeline is edited. This split allows the same JSON
// representation to back processing-settings files, projects, and jobs.
struct VDQtProcessingState {
    int videoMode = VideoMode_FullProcessing;
    int audioMode = AudioMode_DirectStreamCopy;
    bool smartRendering = false;
    bool preserveEmptyFrames = true;
    VDFrameRateConfig frameRate;
    VDDecompressionFormatConfig decompression;
    VDDecoderErrorModeConfig decoderErrorMode;
    VDRawVideoExportConfig rawVideo;
    VDVideoCodecParams videoCodec;
    VDAudioCodecParams audioCodec;
    QList<VDFilterInstance> filters;
    QList<VDAudioFilterInstance> audioFilters;
    QMap<QString, QString> textMetadata;
};

// Complete restorable editing session. Paths are stored in portable JSON and
// resolved by the loader; sourcePaths contains appended sources while
// sourcePath remains the primary/backward-compatible field.
struct VDQtProjectState {
    QString sourcePath;
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
    qint64 position = 0;
    bool hasSelection = false;
    qint64 selectionStart = 0;
    qint64 selectionEnd = 0;
    bool zoomEnabled = false;
    qint64 zoomStart = 0;
    qint64 zoomEnd = 0;
    QList<qint64> markers;
    qint64 sourceFrameCount = 0;
    QList<VDQtTimelineSegment> timelineSegments;
    VDQtProcessingState processing;
};

// Queue operations share one serialized state structure. The operation selects
// which subset of VDQtJobState is consumed by the runner.
enum class VDQtJobOperation {
    VideoExport = 0,
    AudioExport,
    RawVideoExport,
    ImageSequenceExport,
    VideoAnalysis
};

enum class VDQtJobStatus {
    Pending = 0,
    Starting,
    Running,
    Aborting,
    Complete,
    Postponed,
    Cancelled,
    Failed,
    Interrupted
};

// Durable queue record. Runtime-only exporter/decoder objects never enter this
// structure, which is why interrupted jobs can be recovered after a restart.
struct VDQtJobState {
    QString id;
    QString name;
    VDQtJobOperation operation = VDQtJobOperation::VideoExport;
    VDQtJobStatus status = VDQtJobStatus::Pending;
    QDateTime startedAtUtc;
    QDateTime endedAtUtc;
    double progress = 0.0;
    QString error;
    QStringList logEntries;
    bool replaceExisting = false;
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
    QString imageExtension = QStringLiteral("png");
    int imageQuality = -1;
    int imageMinimumDigits = 6;
    int imageStartIndex = 0;
    VDQtVideoExporter::ExportOptions options;
    VDQtProcessingState processing;
};

// Versioned JSON boundary for all durable state. Load functions parse into
// temporary values and commit only after validation, so a malformed file does
// not partially overwrite the caller's current session.
class VDQtProjectFile {
public:
    static bool saveProcessingSettings(
        const QString& path,
        const VDQtProcessingState& state,
        QString *errorMessage = nullptr);
    static bool loadProcessingSettings(
        const QString& path,
        VDQtProcessingState *state,
        QString *errorMessage = nullptr);

    static bool saveProject(
        const QString& path,
        const VDQtProjectState& state,
        QString *errorMessage = nullptr);
    static bool loadProject(
        const QString& path,
        VDQtProjectState *state,
        QString *errorMessage = nullptr);
    static bool saveJobQueue(
        const QString& path,
        const QList<VDQtJobState>& jobs,
        QString *errorMessage = nullptr);
    static bool loadJobQueue(
        const QString& path,
        QList<VDQtJobState> *jobs,
        QString *errorMessage = nullptr);
};

#endif
