#include "VDQtProjectFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>
#include <limits>

namespace {

constexpr int kDocumentVersion = 1;
constexpr qint64 kMaximumDocumentBytes = 4 * 1024 * 1024;

void setError(QString *errorMessage, const QString& message) {
    if (errorMessage) *errorMessage = message;
}

QJsonObject videoCodecToJson(const VDVideoCodecParams& value) {
    QJsonObject object;
    object["codecId"] = value.codecId;
    object["rateMode"] = value.rateMode;
    object["crf"] = value.crf;
    object["targetBitrateKbps"] = value.targetBitrateKbps;
    object["maxBitrateKbps"] = value.maxBitrateKbps;
    object["preset"] = value.preset;
    object["tune"] = value.tune;
    object["profile"] = value.profile;
    object["pixelFormat"] = value.pixFmt;
    object["colorMatrix"] = value.colorMatrix;
    object["keyframeInterval"] = value.keyframeInterval;
    object["bFrames"] = value.bFrames;
    object["proresProfile"] = value.proresProfile;
    object["proresVendor"] = value.proresVendor;
    object["ffv1Version"] = value.ffv1Version;
    object["ffv1Coder"] = value.ffv1Coder;
    object["ffv1Slices"] = value.ffv1Slices;
    object["huffyuvPredictor"] = value.huffyuvPredictor;
    object["cineformQuality"] = value.cineformQuality;
    return object;
}

VDVideoCodecParams videoCodecFromJson(const QJsonObject& object) {
    VDVideoCodecParams value;
    value.codecId = object.value("codecId").toString(value.codecId);
    value.rateMode = object.value("rateMode").toString(value.rateMode);
    value.crf = object.value("crf").toInt(value.crf);
    value.targetBitrateKbps = object.value("targetBitrateKbps").toInt(value.targetBitrateKbps);
    value.maxBitrateKbps = object.value("maxBitrateKbps").toInt(value.maxBitrateKbps);
    value.preset = object.value("preset").toString(value.preset);
    value.tune = object.value("tune").toString(value.tune);
    value.profile = object.value("profile").toString(value.profile);
    value.pixFmt = object.value("pixelFormat").toString(value.pixFmt);
    value.colorMatrix = object.value("colorMatrix").toString(value.colorMatrix);
    value.keyframeInterval = object.value("keyframeInterval").toInt(value.keyframeInterval);
    value.bFrames = object.value("bFrames").toInt(value.bFrames);
    value.proresProfile = object.value("proresProfile").toInt(value.proresProfile);
    value.proresVendor = object.value("proresVendor").toString(value.proresVendor);
    value.ffv1Version = object.value("ffv1Version").toInt(value.ffv1Version);
    value.ffv1Coder = object.value("ffv1Coder").toInt(value.ffv1Coder);
    value.ffv1Slices = object.value("ffv1Slices").toInt(value.ffv1Slices);
    value.huffyuvPredictor = object.value("huffyuvPredictor").toInt(value.huffyuvPredictor);
    value.cineformQuality = object.value("cineformQuality").toInt(value.cineformQuality);
    return value;
}

QJsonObject audioCodecToJson(const VDAudioCodecParams& value) {
    QJsonObject object;
    object["codecId"] = value.codecId;
    object["rateMode"] = value.rateMode;
    object["vbrQuality"] = value.vbrQuality;
    object["bitrateKbps"] = value.bitrateKbps;
    object["sampleRate"] = value.sampleRate;
    object["channels"] = value.channels;
    object["bitDepth"] = value.bitDepth;
    return object;
}

VDAudioCodecParams audioCodecFromJson(const QJsonObject& object) {
    VDAudioCodecParams value;
    value.codecId = object.value("codecId").toString(value.codecId);
    value.rateMode = object.value("rateMode").toString(value.rateMode);
    value.vbrQuality = object.value("vbrQuality").toInt(value.vbrQuality);
    value.bitrateKbps = object.value("bitrateKbps").toInt(value.bitrateKbps);
    value.sampleRate = object.value("sampleRate").toInt(value.sampleRate);
    value.channels = object.value("channels").toInt(value.channels);
    value.bitDepth = object.value("bitDepth").toInt(value.bitDepth);
    return value;
}

QJsonObject processingToJson(const VDQtProcessingState& state) {
    QJsonObject object;
    object["videoMode"] = state.videoMode;
    object["audioMode"] = state.audioMode;
    object["smartRendering"] = state.smartRendering;
    object["preserveEmptyFrames"] = state.preserveEmptyFrames;

    QJsonObject frameRate;
    frameRate["sourceMode"] = state.frameRate.sourceMode;
    frameRate["customSourceFps"] = state.frameRate.customSourceFps;
    frameRate["conversionMode"] = state.frameRate.convMode;
    frameRate["decimateN"] = state.frameRate.decimateN;
    frameRate["convertFps"] = state.frameRate.convertFps;
    object["frameRate"] = frameRate;

    QJsonObject decompression;
    decompression["formatName"] = state.decompression.formatName;
    decompression["colorSpace"] = state.decompression.colorSpace;
    decompression["componentRange"] = state.decompression.componentRange;
    object["decompression"] = decompression;

    QJsonObject decoderError;
    decoderError["mode"] = state.decoderErrorMode.errorMode;
    object["decoderError"] = decoderError;

    QJsonObject rawVideo;
    rawVideo["pixelFormat"] = state.rawVideo.pixelFormat;
    rawVideo["scanlineAlignment"] = state.rawVideo.scanlineAlignment;
    rawVideo["swapChromaPlanes"] = state.rawVideo.swapChromaPlanes;
    rawVideo["bottomUp"] = state.rawVideo.bottomUp;
    rawVideo["colorMatrix"] = state.rawVideo.colorMatrix;
    rawVideo["fullRange"] = state.rawVideo.fullRange;
    object["rawVideo"] = rawVideo;

    object["videoCodec"] = videoCodecToJson(state.videoCodec);
    object["audioCodec"] = audioCodecToJson(state.audioCodec);

    QJsonArray filters;
    for (const VDFilterInstance& filter : state.filters) {
        QJsonObject filterObject;
        filterObject["id"] = filter.id;
        filterObject["name"] = filter.name;
        filterObject["type"] = static_cast<int>(filter.type);
        filterObject["enabled"] = filter.enabled;
        QJsonObject parameters;
        for (auto it = filter.params.cbegin(); it != filter.params.cend(); ++it)
            parameters[it.key()] = it.value();
        filterObject["parameters"] = parameters;
        filters.append(filterObject);
    }
    object["filters"] = filters;

    QJsonObject metadata;
    for (auto it = state.textMetadata.cbegin(); it != state.textMetadata.cend(); ++it)
        metadata[it.key()] = it.value();
    object["textMetadata"] = metadata;
    return object;
}

bool parseProcessing(const QJsonObject& object,
                     VDQtProcessingState *state,
                     QString *errorMessage) {
    if (!state) {
        setError(errorMessage, QStringLiteral("No processing-state destination was provided."));
        return false;
    }
    VDQtProcessingState result;
    result.videoMode = object.value("videoMode").toInt(result.videoMode);
    result.audioMode = object.value("audioMode").toInt(result.audioMode);
    result.smartRendering = object.value("smartRendering").toBool(false);
    result.preserveEmptyFrames = object.value("preserveEmptyFrames").toBool(true);
    if (result.videoMode < VideoMode_DirectStreamCopy
        || result.videoMode > VideoMode_FullProcessing
        || result.audioMode < AudioMode_DirectStreamCopy
        || result.audioMode > AudioMode_FullProcessing) {
        setError(errorMessage, QStringLiteral("The processing file contains an invalid audio/video mode."));
        return false;
    }

    const QJsonObject frameRate = object.value("frameRate").toObject();
    result.frameRate.sourceMode = frameRate.value("sourceMode").toInt();
    result.frameRate.customSourceFps = frameRate.value("customSourceFps").toDouble();
    result.frameRate.convMode = frameRate.value("conversionMode").toInt();
    result.frameRate.decimateN = frameRate.value("decimateN").toInt(2);
    result.frameRate.convertFps = frameRate.value("convertFps").toDouble();
    if (result.frameRate.sourceMode < 0 || result.frameRate.sourceMode > 2
        || result.frameRate.convMode < 0 || result.frameRate.convMode > 4
        || result.frameRate.decimateN < 1 || result.frameRate.decimateN > 1000000
        || !std::isfinite(result.frameRate.customSourceFps)
        || !std::isfinite(result.frameRate.convertFps)
        || result.frameRate.customSourceFps < 0.0
        || result.frameRate.convertFps < 0.0
        || result.frameRate.customSourceFps > 10000.0
        || result.frameRate.convertFps > 10000.0) {
        setError(errorMessage, QStringLiteral("The processing file contains invalid frame-rate settings."));
        return false;
    }

    const QJsonObject decompression = object.value("decompression").toObject();
    result.decompression.formatName =
        decompression.value("formatName").toString(QStringLiteral("Autoselect"));
    result.decompression.colorSpace = decompression.value("colorSpace").toInt();
    result.decompression.componentRange = decompression.value("componentRange").toInt();
    const QJsonObject decoderError = object.value("decoderError").toObject();
    result.decoderErrorMode.errorMode = decoderError.value("mode").toInt();
    if (result.decompression.colorSpace < 0 || result.decompression.colorSpace > 2
        || result.decompression.componentRange < 0
        || result.decompression.componentRange > 2
        || result.decoderErrorMode.errorMode < 0
        || result.decoderErrorMode.errorMode > 2) {
        setError(errorMessage, QStringLiteral("The processing file contains invalid decoder settings."));
        return false;
    }

    const QJsonObject rawVideo = object.value("rawVideo").toObject();
    result.rawVideo.pixelFormat =
        rawVideo.value("pixelFormat").toString(QStringLiteral("yuv420p"));
    result.rawVideo.scanlineAlignment = rawVideo.value("scanlineAlignment").toInt(4);
    result.rawVideo.swapChromaPlanes = rawVideo.value("swapChromaPlanes").toBool(true);
    result.rawVideo.bottomUp = rawVideo.value("bottomUp").toBool(false);
    result.rawVideo.colorMatrix =
        rawVideo.value("colorMatrix").toString(QStringLiteral("bt601"));
    result.rawVideo.fullRange = rawVideo.value("fullRange").toBool(false);
    const int alignment = result.rawVideo.scanlineAlignment;
    if (result.rawVideo.pixelFormat.size() > 64 || alignment < 1 || alignment > 64
        || (alignment & (alignment - 1)) != 0
        || (result.rawVideo.colorMatrix != QStringLiteral("bt601")
            && result.rawVideo.colorMatrix != QStringLiteral("bt709"))) {
        setError(errorMessage, QStringLiteral("The processing file contains invalid raw-video settings."));
        return false;
    }

    result.videoCodec = videoCodecFromJson(object.value("videoCodec").toObject());
    result.audioCodec = audioCodecFromJson(object.value("audioCodec").toObject());
    if (result.videoCodec.codecId.isEmpty() || result.videoCodec.codecId.size() > 128
        || result.audioCodec.codecId.isEmpty() || result.audioCodec.codecId.size() > 128
        || result.videoCodec.crf < 0 || result.videoCodec.crf > 100
        || result.videoCodec.bFrames < 0 || result.videoCodec.bFrames > 32
        || result.videoCodec.keyframeInterval < 0
        || result.videoCodec.keyframeInterval > 1000000
        || result.audioCodec.sampleRate < 0 || result.audioCodec.sampleRate > 768000
        || result.audioCodec.channels < 0 || result.audioCodec.channels > 64
        || result.audioCodec.bitDepth < 1 || result.audioCodec.bitDepth > 64) {
        setError(errorMessage, QStringLiteral("The processing file contains invalid codec settings."));
        return false;
    }

    const QJsonArray filters = object.value("filters").toArray();
    if (filters.size() > 256) {
        setError(errorMessage, QStringLiteral("The processing file contains too many filters."));
        return false;
    }
    for (const QJsonValue& value : filters) {
        if (!value.isObject()) {
            setError(errorMessage, QStringLiteral("A filter entry is malformed."));
            return false;
        }
        const QJsonObject filterObject = value.toObject();
        const int type = filterObject.value("type").toInt(-1);
        if (type < static_cast<int>(VDFilterType::SixAxis)
            || type > static_cast<int>(VDFilterType::Sharpen)) {
            setError(errorMessage, QStringLiteral("A filter entry has an unknown type."));
            return false;
        }
        VDFilterInstance filter;
        filter.id = filterObject.value("id").toString();
        filter.name = filterObject.value("name").toString();
        filter.type = static_cast<VDFilterType>(type);
        filter.enabled = filterObject.value("enabled").toBool(true);
        const QJsonObject parameters = filterObject.value("parameters").toObject();
        if (parameters.size() > 128 || filter.name.size() > 256 || filter.id.size() > 256) {
            setError(errorMessage, QStringLiteral("A filter entry is too large."));
            return false;
        }
        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
            const double parameter = it.value().toDouble(
                std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(parameter) || it.key().size() > 128) {
                setError(errorMessage, QStringLiteral("A filter parameter is invalid."));
                return false;
            }
            filter.params.insert(it.key(), parameter);
        }
        result.filters.append(filter);
    }

    const QJsonObject metadata = object.value("textMetadata").toObject();
    if (metadata.size() > 128) {
        setError(errorMessage, QStringLiteral("The processing file contains too many metadata fields."));
        return false;
    }
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        const QString text = it.value().toString();
        if (it.key().size() > 128 || text.size() > 65536) {
            setError(errorMessage, QStringLiteral("A metadata field is too large."));
            return false;
        }
        result.textMetadata.insert(it.key(), text);
    }

    *state = result;
    return true;
}

bool writeDocument(const QString& path,
                   const QJsonObject& root,
                   QString *errorMessage) {
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        setError(errorMessage, output.errorString());
        return false;
    }
    const QByteArray serialized = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (output.write(serialized) != serialized.size() || !output.commit()) {
        setError(errorMessage, output.errorString().isEmpty()
            ? QStringLiteral("The settings file could not be committed.")
            : output.errorString());
        return false;
    }
    return true;
}

bool readDocument(const QString& path,
                  const QString& expectedKind,
                  QJsonObject *root,
                  QString *errorMessage) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        setError(errorMessage, input.errorString());
        return false;
    }
    if (input.size() < 1 || input.size() > kMaximumDocumentBytes) {
        setError(errorMessage, QStringLiteral("The settings file is empty or unreasonably large."));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QString("Invalid JSON at offset %1: %2")
            .arg(parseError.offset).arg(parseError.errorString()));
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value("kind").toString() != expectedKind
        || object.value("version").toInt() != kDocumentVersion) {
        setError(errorMessage, QStringLiteral("This file has an unsupported type or version."));
        return false;
    }
    if (root) *root = object;
    return true;
}

} // namespace

bool VDQtProjectFile::saveProcessingSettings(
    const QString& path,
    const VDQtProcessingState& state,
    QString *errorMessage) {
    QJsonObject root;
    root["kind"] = QStringLiteral("VirtualDubQTProcessingSettings");
    root["version"] = kDocumentVersion;
    root["processing"] = processingToJson(state);
    return writeDocument(path, root, errorMessage);
}

bool VDQtProjectFile::loadProcessingSettings(
    const QString& path,
    VDQtProcessingState *state,
    QString *errorMessage) {
    QJsonObject root;
    return readDocument(
               path, QStringLiteral("VirtualDubQTProcessingSettings"),
               &root, errorMessage)
        && parseProcessing(root.value("processing").toObject(), state, errorMessage);
}

bool VDQtProjectFile::saveProject(
    const QString& path,
    const VDQtProjectState& state,
    QString *errorMessage) {
    QJsonObject root;
    root["kind"] = QStringLiteral("VirtualDubQTProject");
    root["version"] = kDocumentVersion;
    const QFileInfo projectInfo(path);
    QStringList sources = state.sourcePaths;
    if (sources.isEmpty() && !state.sourcePath.isEmpty()) sources.append(state.sourcePath);
    QJsonArray serializedSources;
    for (const QString& source : sources) {
        const QFileInfo sourceInfo(source);
        serializedSources.append(sourceInfo.isAbsolute()
            ? projectInfo.absoluteDir().relativeFilePath(sourceInfo.absoluteFilePath())
            : source);
    }
    root["sourcePaths"] = serializedSources;
    if (!serializedSources.isEmpty()) root["sourcePath"] = serializedSources.first();
    root["position"] = static_cast<double>(state.position);
    root["hasSelection"] = state.hasSelection;
    root["selectionStart"] = static_cast<double>(state.selectionStart);
    root["selectionEnd"] = static_cast<double>(state.selectionEnd);
    root["processing"] = processingToJson(state.processing);
    return writeDocument(path, root, errorMessage);
}

bool VDQtProjectFile::loadProject(
    const QString& path,
    VDQtProjectState *state,
    QString *errorMessage) {
    if (!state) {
        setError(errorMessage, QStringLiteral("No project-state destination was provided."));
        return false;
    }
    QJsonObject root;
    if (!readDocument(path, QStringLiteral("VirtualDubQTProject"), &root, errorMessage))
        return false;

    VDQtProjectState result;
    QJsonArray serializedSources = root.value("sourcePaths").toArray();
    if (serializedSources.isEmpty() && root.value("sourcePath").isString())
        serializedSources.append(root.value("sourcePath"));
    if (serializedSources.isEmpty() || serializedSources.size() > 128) {
        setError(errorMessage, QStringLiteral("The project has no valid source path."));
        return false;
    }
    for (const QJsonValue& serializedSource : serializedSources) {
        QString sourcePath = serializedSource.toString();
        if (sourcePath.isEmpty() || sourcePath.size() > 32768) {
            setError(errorMessage, QStringLiteral("The project contains an invalid source path."));
            return false;
        }
        if (!QFileInfo(sourcePath).isAbsolute())
            sourcePath = QFileInfo(path).absoluteDir().absoluteFilePath(sourcePath);
        result.sourcePaths.append(QDir::cleanPath(sourcePath));
    }
    result.sourcePath = result.sourcePaths.first();
    result.position = static_cast<qint64>(root.value("position").toDouble());
    result.hasSelection = root.value("hasSelection").toBool(false);
    result.selectionStart = static_cast<qint64>(root.value("selectionStart").toDouble());
    result.selectionEnd = static_cast<qint64>(root.value("selectionEnd").toDouble());
    if (result.position < 0 || result.selectionStart < 0
        || result.selectionEnd < result.selectionStart
        || result.position > std::numeric_limits<int>::max()
        || result.selectionEnd > std::numeric_limits<int>::max()) {
        setError(errorMessage, QStringLiteral("The project contains an invalid timeline position or selection."));
        return false;
    }
    if (!parseProcessing(
            root.value("processing").toObject(), &result.processing, errorMessage))
        return false;
    *state = result;
    return true;
}

bool VDQtProjectFile::saveJobQueue(
    const QString& path,
    const QList<VDQtJobState>& jobs,
    QString *errorMessage) {
    if (jobs.size() > 1000) {
        setError(errorMessage, QStringLiteral("The job queue exceeds the 1000-job limit."));
        return false;
    }
    const QDir documentDirectory = QFileInfo(path).absoluteDir();
    QJsonArray serializedJobs;
    for (const VDQtJobState& job : jobs) {
        if (job.sourcePaths.isEmpty() || job.options.outputPath.isEmpty()) {
            setError(errorMessage, QStringLiteral("A queued job has no source or destination."));
            return false;
        }
        QJsonObject object;
        QJsonArray sources;
        for (const QString& source : job.sourcePaths) {
            const QFileInfo sourceInfo(source);
            sources.append(sourceInfo.isAbsolute()
                ? documentDirectory.relativeFilePath(sourceInfo.absoluteFilePath())
                : source);
        }
        object["sourcePaths"] = sources;
        const QFileInfo outputInfo(job.options.outputPath);
        object["outputPath"] = outputInfo.isAbsolute()
            ? documentDirectory.relativeFilePath(outputInfo.absoluteFilePath())
            : job.options.outputPath;
        QJsonObject options;
        options["startFrame"] = job.options.startFrame;
        options["endFrame"] = job.options.endFrame;
        options["customFps"] = job.options.customFps;
        options["convertFpsPreserveDuration"] = job.options.convertFpsPreserveDuration;
        options["decimateFactor"] = job.options.decimateFactor;
        options["videoMode"] = job.options.videoMode;
        options["audioMode"] = job.options.audioMode;
        options["containerType"] = job.options.containerType;
        options["fastStart"] = job.options.fastStart;
        options["includeAudio"] = job.options.includeAudio;
        options["videoCodecOverride"] = job.options.videoCodecOverride;
        options["videoPixelFormatOverride"] = job.options.videoPixelFormatOverride;
        options["smartRendering"] = job.options.smartRendering;
        options["preserveEmptyFrames"] = job.options.preserveEmptyFrames;
        QJsonObject metadata;
        for (auto it = job.options.metadata.cbegin(); it != job.options.metadata.cend(); ++it)
            metadata[it.key()] = it.value();
        options["metadata"] = metadata;
        object["options"] = options;
        object["processing"] = processingToJson(job.processing);
        serializedJobs.append(object);
    }
    QJsonObject root;
    root["kind"] = QStringLiteral("VirtualDubQTJobQueue");
    root["version"] = kDocumentVersion;
    root["jobs"] = serializedJobs;
    return writeDocument(path, root, errorMessage);
}

bool VDQtProjectFile::loadJobQueue(
    const QString& path,
    QList<VDQtJobState> *jobs,
    QString *errorMessage) {
    if (!jobs) {
        setError(errorMessage, QStringLiteral("No job-queue destination was provided."));
        return false;
    }
    QJsonObject root;
    if (!readDocument(
            path, QStringLiteral("VirtualDubQTJobQueue"), &root, errorMessage))
        return false;
    const QJsonArray serializedJobs = root.value("jobs").toArray();
    if (serializedJobs.size() > 1000) {
        setError(errorMessage, QStringLiteral("The job queue exceeds the 1000-job limit."));
        return false;
    }
    const QDir documentDirectory = QFileInfo(path).absoluteDir();
    QList<VDQtJobState> result;
    for (const QJsonValue& value : serializedJobs) {
        if (!value.isObject()) {
            setError(errorMessage, QStringLiteral("A queued job is malformed."));
            return false;
        }
        const QJsonObject object = value.toObject();
        const QJsonArray serializedSources = object.value("sourcePaths").toArray();
        if (serializedSources.isEmpty() || serializedSources.size() > 128) {
            setError(errorMessage, QStringLiteral("A queued job has an invalid source list."));
            return false;
        }
        VDQtJobState job;
        for (const QJsonValue& sourceValue : serializedSources) {
            QString source = sourceValue.toString();
            if (source.isEmpty() || source.size() > 32768) {
                setError(errorMessage, QStringLiteral("A queued source path is invalid."));
                return false;
            }
            if (!QFileInfo(source).isAbsolute())
                source = documentDirectory.absoluteFilePath(source);
            job.sourcePaths.append(QDir::cleanPath(source));
        }
        QString outputPath = object.value("outputPath").toString();
        if (outputPath.isEmpty() || outputPath.size() > 32768) {
            setError(errorMessage, QStringLiteral("A queued destination path is invalid."));
            return false;
        }
        if (!QFileInfo(outputPath).isAbsolute())
            outputPath = documentDirectory.absoluteFilePath(outputPath);
        const QJsonObject options = object.value("options").toObject();
        job.options.inputPath = job.sourcePaths.first();
        job.options.outputPath = QDir::cleanPath(outputPath);
        job.options.startFrame = options.value("startFrame").toInt();
        job.options.endFrame = options.value("endFrame").toInt(-1);
        job.options.customFps = options.value("customFps").toDouble();
        job.options.convertFpsPreserveDuration =
            options.value("convertFpsPreserveDuration").toBool(false);
        job.options.decimateFactor = options.value("decimateFactor").toInt(1);
        job.options.videoMode = options.value("videoMode").toInt(VideoMode_FullProcessing);
        job.options.audioMode = options.value("audioMode").toInt(AudioMode_DirectStreamCopy);
        job.options.containerType = options.value("containerType").toString();
        job.options.fastStart = options.value("fastStart").toBool(false);
        job.options.includeAudio = options.value("includeAudio").toBool(true);
        job.options.videoCodecOverride = options.value("videoCodecOverride").toString();
        job.options.videoPixelFormatOverride =
            options.value("videoPixelFormatOverride").toString();
        job.options.smartRendering = options.value("smartRendering").toBool(false);
        job.options.preserveEmptyFrames =
            options.value("preserveEmptyFrames").toBool(true);
        if (job.options.startFrame < 0 || job.options.endFrame < -1
            || (job.options.endFrame >= 0
                && job.options.endFrame < job.options.startFrame)
            || !std::isfinite(job.options.customFps)
            || job.options.customFps < 0.0 || job.options.customFps > 10000.0
            || job.options.decimateFactor < 1
            || job.options.decimateFactor > 1000000
            || job.options.videoMode < VideoMode_DirectStreamCopy
            || job.options.videoMode > VideoMode_FullProcessing
            || job.options.audioMode < AudioMode_DirectStreamCopy
            || job.options.audioMode > AudioMode_FullProcessing
            || job.options.containerType.size() > 128
            || job.options.videoCodecOverride.size() > 128
            || job.options.videoPixelFormatOverride.size() > 128) {
            setError(errorMessage, QStringLiteral("A queued job contains invalid export options."));
            return false;
        }
        const QJsonObject metadata = options.value("metadata").toObject();
        if (metadata.size() > 128) {
            setError(errorMessage, QStringLiteral("A queued job contains too much metadata."));
            return false;
        }
        for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
            const QString text = it.value().toString();
            if (it.key().size() > 128 || text.size() > 65536) {
                setError(errorMessage, QStringLiteral("A queued metadata field is too large."));
                return false;
            }
            job.options.metadata.insert(it.key(), text);
        }
        if (!parseProcessing(
                object.value("processing").toObject(), &job.processing, errorMessage))
            return false;
        result.append(job);
    }
    *jobs = result;
    return true;
}
