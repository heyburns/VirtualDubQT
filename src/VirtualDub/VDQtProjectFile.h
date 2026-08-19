#ifndef VDQTPROJECTFILE_H
#define VDQTPROJECTFILE_H

#include "VDQtCodecEngine.h"
#include "VDQtDialogs.h"
#include "VDQtAudioFilterSystem.h"
#include "VDQtFilterSystem.h"
#include "VDQtTimeline.h"
#include "VDQtVideoExporter.h"

#include <QMap>
#include <QString>
#include <QStringList>

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
    qint64 sourceFrameCount = 0;
    QList<VDQtTimelineSegment> timelineSegments;
    VDQtProcessingState processing;
};

struct VDQtJobState {
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
};

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
