#ifndef VDQTPROJECTFILE_H
#define VDQTPROJECTFILE_H

#include "VDQtCodecEngine.h"
#include "VDQtDialogs.h"
#include "VDQtFilterSystem.h"
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
    QMap<QString, QString> textMetadata;
};

struct VDQtProjectState {
    QString sourcePath;
    QStringList sourcePaths;
    qint64 position = 0;
    bool hasSelection = false;
    qint64 selectionStart = 0;
    qint64 selectionEnd = 0;
    VDQtProcessingState processing;
};

struct VDQtJobState {
    QStringList sourcePaths;
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
