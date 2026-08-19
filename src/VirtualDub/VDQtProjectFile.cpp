#include "VDQtProjectFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <cmath>
#include <limits>

namespace {

constexpr int kDocumentVersion = 3;
constexpr int kOldestSupportedDocumentVersion = 1;
constexpr qint64 kMaximumDocumentBytes = qint64{4} * 1024 * 1024;

void setError(QString *errorMessage, const QString& message) {
    if (errorMessage) *errorMessage = message;
}

bool isSafeImageExtension(const QString& extension) {
    if (extension.isEmpty() || extension.size() > 16) return false;
    for (const QChar character : extension) {
        if (!character.isLetterOrNumber()) return false;
    }
    return true;
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

    QJsonArray audioFilters;
    for (const VDAudioFilterInstance& filter : state.audioFilters) {
        QJsonObject filterObject;
        filterObject["id"] = filter.id;
        filterObject["name"] = filter.name;
        filterObject["type"] = static_cast<int>(filter.type);
        filterObject["enabled"] = filter.enabled;
        QJsonObject parameters;
        for (auto it = filter.params.cbegin(); it != filter.params.cend(); ++it)
            parameters[it.key()] = it.value();
        filterObject["parameters"] = parameters;
        audioFilters.append(filterObject);
    }
    object["audioFilters"] = audioFilters;

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
            || type >= static_cast<int>(VDFilterType::Count)) {
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

    const QJsonArray audioFilters = object.value("audioFilters").toArray();
    if (audioFilters.size() > 256) {
        setError(errorMessage, QStringLiteral("The processing file contains too many audio filters."));
        return false;
    }
    for (const QJsonValue& value : audioFilters) {
        if (!value.isObject()) {
            setError(errorMessage, QStringLiteral("An audio filter entry is malformed."));
            return false;
        }
        const QJsonObject filterObject = value.toObject();
        const int type = filterObject.value("type").toInt(-1);
        if (type < 0 || type >= static_cast<int>(VDAudioFilterType::Count)) {
            setError(errorMessage, QStringLiteral("An audio filter entry has an unknown type."));
            return false;
        }
        VDAudioFilterInstance filter;
        filter.id = filterObject.value("id").toString();
        filter.name = filterObject.value("name").toString();
        filter.type = static_cast<VDAudioFilterType>(type);
        filter.enabled = filterObject.value("enabled").toBool(true);
        const QJsonObject parameters = filterObject.value("parameters").toObject();
        if (parameters.size() > 128 || filter.name.size() > 256
            || filter.id.size() > 256) {
            setError(errorMessage, QStringLiteral("An audio filter entry is too large."));
            return false;
        }
        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
            const double parameter = it.value().toDouble(
                std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(parameter) || it.key().size() > 128) {
                setError(errorMessage, QStringLiteral("An audio filter parameter is invalid."));
                return false;
            }
            filter.params.insert(it.key(), parameter);
        }
        result.audioFilters.append(filter);
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
    const QByteArray serialized =
        QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (serialized.size() > kMaximumDocumentBytes) {
        setError(errorMessage, QStringLiteral(
            "The settings document exceeds the 4 MiB safety limit."));
        return false;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        setError(errorMessage, output.errorString());
        return false;
    }
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
    const int version = object.value("version").toInt();
    if (object.value("kind").toString() != expectedKind
        || version < kOldestSupportedDocumentVersion
        || version > kDocumentVersion) {
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
    if (!state.rawPixelFormat.isEmpty()
        && (sources.size() != 1 || state.imageSequenceFps > 0.0
            || state.rawWidth <= 0 || state.rawHeight <= 0
            || !std::isfinite(state.rawFrameRate)
            || state.rawFrameRate <= 0.0 || state.rawByteOffset < 0)) {
        setError(errorMessage,
                 QStringLiteral("The raw-video project source parameters are invalid."));
        return false;
    }
    QJsonArray serializedSources;
    for (const QString& source : sources) {
        const QFileInfo sourceInfo(source);
        serializedSources.append(sourceInfo.isAbsolute()
            ? projectInfo.absoluteDir().relativeFilePath(sourceInfo.absoluteFilePath())
            : source);
    }
    root["sourcePaths"] = serializedSources;
    if (!serializedSources.isEmpty()) root["sourcePath"] = serializedSources.first();
    root["imageSequenceFps"] = state.imageSequenceFps;
    root["rawPixelFormat"] = state.rawPixelFormat;
    root["rawWidth"] = state.rawWidth;
    root["rawHeight"] = state.rawHeight;
    root["rawFrameRate"] = state.rawFrameRate;
    root["rawByteOffset"] = static_cast<double>(state.rawByteOffset);
    if (!state.audioSourcePath.isEmpty()) {
        const QFileInfo audioInfo(state.audioSourcePath);
        root["audioSourcePath"] = audioInfo.isAbsolute()
            ? projectInfo.absoluteDir().relativeFilePath(audioInfo.absoluteFilePath())
            : state.audioSourcePath;
    }
    root["audioStreamIndex"] = state.audioStreamIndex;
    root["audioDisabled"] = state.audioDisabled;
    root["position"] = static_cast<double>(state.position);
    root["hasSelection"] = state.hasSelection;
    root["selectionStart"] = static_cast<double>(state.selectionStart);
    root["selectionEnd"] = static_cast<double>(state.selectionEnd);
    root["sourceFrameCount"] = static_cast<double>(state.sourceFrameCount);
    QJsonArray timelineSegments;
    for (const VDQtTimelineSegment& segment : state.timelineSegments) {
        QJsonObject segmentObject;
        segmentObject["sourceStartFrame"] =
            static_cast<double>(segment.sourceStartFrame);
        segmentObject["frameCount"] = static_cast<double>(segment.frameCount);
        timelineSegments.append(segmentObject);
    }
    root["timelineSegments"] = timelineSegments;
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
    result.imageSequenceFps = root.value("imageSequenceFps").toDouble(0.0);
    result.rawPixelFormat = root.value("rawPixelFormat").toString();
    result.rawWidth = root.value("rawWidth").toInt(0);
    result.rawHeight = root.value("rawHeight").toInt(0);
    result.rawFrameRate = root.value("rawFrameRate").toDouble(0.0);
    const double serializedRawByteOffset =
        root.value("rawByteOffset").toDouble(0.0);
    if (!std::isfinite(serializedRawByteOffset)
        || serializedRawByteOffset < 0.0
        || serializedRawByteOffset
            > static_cast<double>(std::numeric_limits<qint64>::max())) {
        setError(errorMessage,
                 QStringLiteral("The project contains an invalid raw-video byte offset."));
        return false;
    }
    result.rawByteOffset = static_cast<qint64>(serializedRawByteOffset);
    result.audioSourcePath = root.value("audioSourcePath").toString();
    if (!result.audioSourcePath.isEmpty()
        && !QFileInfo(result.audioSourcePath).isAbsolute()) {
        result.audioSourcePath = QFileInfo(path).absoluteDir().absoluteFilePath(
            result.audioSourcePath);
    }
    if (!result.audioSourcePath.isEmpty())
        result.audioSourcePath = QDir::cleanPath(result.audioSourcePath);
    result.audioStreamIndex = root.value("audioStreamIndex").toInt(-1);
    result.audioDisabled = root.value("audioDisabled").toBool(false);
    result.position = static_cast<qint64>(root.value("position").toDouble());
    result.hasSelection = root.value("hasSelection").toBool(false);
    result.selectionStart = static_cast<qint64>(root.value("selectionStart").toDouble());
    result.selectionEnd = static_cast<qint64>(root.value("selectionEnd").toDouble());
    result.sourceFrameCount = static_cast<qint64>(
        root.value("sourceFrameCount").toDouble());
    if (result.position < 0 || result.selectionStart < 0
        || result.selectionEnd < result.selectionStart
        || result.audioStreamIndex < -1
        || result.audioSourcePath.size() > 32768
        || !std::isfinite(result.imageSequenceFps)
        || result.imageSequenceFps < 0.0 || result.imageSequenceFps > 1000.0
        || result.rawPixelFormat.size() > 64
        || result.rawWidth < 0 || result.rawWidth > 65536
        || result.rawHeight < 0 || result.rawHeight > 65536
        || !std::isfinite(result.rawFrameRate)
        || result.rawFrameRate < 0.0 || result.rawFrameRate > 10000.0
        || result.rawByteOffset < 0
        || (!result.rawPixelFormat.isEmpty()
            && (result.sourcePaths.size() != 1
                || result.imageSequenceFps > 0.0
                || result.rawWidth <= 0 || result.rawHeight <= 0
                || result.rawFrameRate <= 0.0))
        || result.sourceFrameCount < 0
        || result.position > std::numeric_limits<int>::max()
        || result.selectionEnd > std::numeric_limits<int>::max()
        || result.sourceFrameCount > std::numeric_limits<int>::max()) {
        setError(errorMessage, QStringLiteral("The project contains an invalid timeline position or selection."));
        return false;
    }
    const QJsonArray timelineSegments = root.value("timelineSegments").toArray();
    if (timelineSegments.size() > 100000) {
        setError(errorMessage, QStringLiteral("The project timeline has too many edit segments."));
        return false;
    }
    qint64 timelineLength = 0;
    for (const QJsonValue& segmentValue : timelineSegments) {
        if (!segmentValue.isObject()) {
            setError(errorMessage, QStringLiteral("A project timeline segment is malformed."));
            return false;
        }
        const QJsonObject segmentObject = segmentValue.toObject();
        VDQtTimelineSegment segment;
        segment.sourceStartFrame = static_cast<qint64>(
            segmentObject.value("sourceStartFrame").toDouble(-1));
        segment.frameCount = static_cast<qint64>(
            segmentObject.value("frameCount").toDouble(-1));
        if (segment.sourceStartFrame < 0 || segment.frameCount <= 0
            || segment.sourceStartFrame > std::numeric_limits<int>::max()
            || segment.frameCount > std::numeric_limits<int>::max()
            || segment.sourceStartFrame + segment.frameCount
                > std::numeric_limits<int>::max()
            || timelineLength > std::numeric_limits<int>::max()
                - segment.frameCount) {
            setError(errorMessage, QStringLiteral("A project timeline segment is invalid."));
            return false;
        }
        timelineLength += segment.frameCount;
        result.timelineSegments.append(segment);
    }
    if (!result.timelineSegments.isEmpty()
        && (result.position >= timelineLength
            || result.selectionEnd > timelineLength)) {
        setError(errorMessage,
                 QStringLiteral("The saved position or selection is outside the edited timeline."));
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
        const bool requiresOutput =
            job.operation != VDQtJobOperation::VideoAnalysis;
        if (job.sourcePaths.isEmpty()
            || (requiresOutput && job.options.outputPath.isEmpty())) {
            setError(errorMessage, QStringLiteral("A queued job has no source or destination."));
            return false;
        }
        const int operation = static_cast<int>(job.operation);
        const int status = static_cast<int>(job.status);
        if (operation < static_cast<int>(VDQtJobOperation::VideoExport)
            || operation > static_cast<int>(VDQtJobOperation::VideoAnalysis)
            || status < static_cast<int>(VDQtJobStatus::Pending)
            || status > static_cast<int>(VDQtJobStatus::Interrupted)
            || !std::isfinite(job.progress) || job.progress < 0.0
            || job.progress > 1.0 || job.name.size() > 1024
            || job.id.size() > 128 || job.error.size() > 65536
            || job.logEntries.size() > 1000
            || !isSafeImageExtension(job.imageExtension)
            || job.imageQuality < -1 || job.imageQuality > 100
            || job.imageMinimumDigits < 1 || job.imageMinimumDigits > 12
            || job.imageStartIndex < 0) {
            setError(errorMessage,
                     QStringLiteral("A queued job contains invalid runtime state."));
            return false;
        }
        for (const QString& entry : job.logEntries) {
            if (entry.size() > 65536) {
                setError(errorMessage,
                         QStringLiteral("A queued job log entry is too large."));
                return false;
            }
        }
        if (!std::isfinite(job.imageSequenceFps)
            || job.imageSequenceFps < 0.0 || job.imageSequenceFps > 1000.0
            || job.rawPixelFormat.size() > 64
            || job.rawWidth < 0 || job.rawWidth > 65536
            || job.rawHeight < 0 || job.rawHeight > 65536
            || !std::isfinite(job.rawFrameRate)
            || job.rawFrameRate < 0.0 || job.rawFrameRate > 10000.0
            || job.rawByteOffset < 0
            || (!job.rawPixelFormat.isEmpty()
                && (job.sourcePaths.size() != 1 || job.rawWidth <= 0
                    || job.rawHeight <= 0 || job.rawFrameRate <= 0.0))) {
            setError(errorMessage,
                     QStringLiteral("A queued raw/image-sequence source is invalid."));
            return false;
        }
        QJsonObject object;
        object["id"] = job.id.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces) : job.id;
        object["name"] = job.name;
        object["operation"] = operation;
        object["status"] = status;
        object["progress"] = job.progress;
        object["error"] = job.error;
        object["replaceExisting"] = job.replaceExisting;
        if (job.startedAtUtc.isValid())
            object["startedAtUtc"] = job.startedAtUtc.toUTC().toString(Qt::ISODateWithMs);
        if (job.endedAtUtc.isValid())
            object["endedAtUtc"] = job.endedAtUtc.toUTC().toString(Qt::ISODateWithMs);
        QJsonArray logEntries;
        for (const QString& entry : job.logEntries) logEntries.append(entry);
        object["logEntries"] = logEntries;
        QJsonArray sources;
        for (const QString& source : job.sourcePaths) {
            const QFileInfo sourceInfo(source);
            sources.append(sourceInfo.isAbsolute()
                ? documentDirectory.relativeFilePath(sourceInfo.absoluteFilePath())
                : source);
        }
        object["sourcePaths"] = sources;
        object["imageSequenceFps"] = job.imageSequenceFps;
        object["rawPixelFormat"] = job.rawPixelFormat;
        object["rawWidth"] = job.rawWidth;
        object["rawHeight"] = job.rawHeight;
        object["rawFrameRate"] = job.rawFrameRate;
        object["rawByteOffset"] = static_cast<double>(job.rawByteOffset);
        if (!job.audioSourcePath.isEmpty()) {
            const QFileInfo audioInfo(job.audioSourcePath);
            object["audioSourcePath"] = audioInfo.isAbsolute()
                ? documentDirectory.relativeFilePath(audioInfo.absoluteFilePath())
                : job.audioSourcePath;
        }
        object["audioStreamIndex"] = job.audioStreamIndex;
        object["audioDisabled"] = job.audioDisabled;
        object["imageExtension"] = job.imageExtension;
        object["imageQuality"] = job.imageQuality;
        object["imageMinimumDigits"] = job.imageMinimumDigits;
        object["imageStartIndex"] = job.imageStartIndex;
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
        QJsonArray timelineSegments;
        for (const VDQtTimelineSegment& segment : job.options.timelineSegments) {
            QJsonObject segmentObject;
            segmentObject["sourceStartFrame"] =
                static_cast<double>(segment.sourceStartFrame);
            segmentObject["frameCount"] = static_cast<double>(segment.frameCount);
            timelineSegments.append(segmentObject);
        }
        options["timelineSegments"] = timelineSegments;
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
        job.id = object.value("id").toString();
        if (job.id.isEmpty())
            job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        job.name = object.value("name").toString();
        const int operation = object.value("operation").toInt(
            static_cast<int>(VDQtJobOperation::VideoExport));
        const int serializedStatus = object.value("status").toInt(
            static_cast<int>(VDQtJobStatus::Pending));
        if (operation < static_cast<int>(VDQtJobOperation::VideoExport)
            || operation > static_cast<int>(VDQtJobOperation::VideoAnalysis)
            || serializedStatus < static_cast<int>(VDQtJobStatus::Pending)
            || serializedStatus > static_cast<int>(VDQtJobStatus::Interrupted)) {
            setError(errorMessage,
                     QStringLiteral("A queued job has an invalid operation or status."));
            return false;
        }
        job.operation = static_cast<VDQtJobOperation>(operation);
        job.status = static_cast<VDQtJobStatus>(serializedStatus);
        if (job.status == VDQtJobStatus::Starting
            || job.status == VDQtJobStatus::Running
            || job.status == VDQtJobStatus::Aborting) {
            job.status = VDQtJobStatus::Interrupted;
            job.error = QStringLiteral(
                "The application exited while this job was running.");
        } else {
            job.error = object.value("error").toString();
        }
        job.progress = object.value("progress").toDouble(0.0);
        job.replaceExisting = object.value("replaceExisting").toBool(false);
        job.startedAtUtc = QDateTime::fromString(
            object.value("startedAtUtc").toString(), Qt::ISODateWithMs);
        job.endedAtUtc = QDateTime::fromString(
            object.value("endedAtUtc").toString(), Qt::ISODateWithMs);
        if (serializedStatus == static_cast<int>(VDQtJobStatus::Starting)
            || serializedStatus == static_cast<int>(VDQtJobStatus::Running)
            || serializedStatus == static_cast<int>(VDQtJobStatus::Aborting)) {
            job.endedAtUtc = QDateTime::currentDateTimeUtc();
        }
        const QJsonArray serializedLog = object.value("logEntries").toArray();
        if (serializedLog.size() > 1000) {
            setError(errorMessage, QStringLiteral("A queued job has too many log entries."));
            return false;
        }
        for (const QJsonValue& logValue : serializedLog) {
            const QString entry = logValue.toString();
            if (entry.size() > 65536) {
                setError(errorMessage,
                         QStringLiteral("A queued job log entry is too large."));
                return false;
            }
            job.logEntries.append(entry);
        }
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
        job.audioSourcePath = object.value("audioSourcePath").toString();
        job.imageSequenceFps = object.value("imageSequenceFps").toDouble(0.0);
        job.rawPixelFormat = object.value("rawPixelFormat").toString();
        job.rawWidth = object.value("rawWidth").toInt(0);
        job.rawHeight = object.value("rawHeight").toInt(0);
        job.rawFrameRate = object.value("rawFrameRate").toDouble(0.0);
        const double serializedRawByteOffset =
            object.value("rawByteOffset").toDouble(0.0);
        if (!std::isfinite(serializedRawByteOffset)
            || serializedRawByteOffset < 0.0
            || serializedRawByteOffset
                > static_cast<double>(std::numeric_limits<qint64>::max())) {
            setError(errorMessage,
                     QStringLiteral("A queued raw-video byte offset is invalid."));
            return false;
        }
        job.rawByteOffset = static_cast<qint64>(serializedRawByteOffset);
        if (!job.audioSourcePath.isEmpty()
            && !QFileInfo(job.audioSourcePath).isAbsolute()) {
            job.audioSourcePath = documentDirectory.absoluteFilePath(
                job.audioSourcePath);
        }
        if (!job.audioSourcePath.isEmpty())
            job.audioSourcePath = QDir::cleanPath(job.audioSourcePath);
        job.audioStreamIndex = object.value("audioStreamIndex").toInt(-1);
        job.audioDisabled = object.value("audioDisabled").toBool(false);
        job.imageExtension = object.value("imageExtension").toString(
            QStringLiteral("png"));
        job.imageQuality = object.value("imageQuality").toInt(-1);
        job.imageMinimumDigits = object.value("imageMinimumDigits").toInt(6);
        job.imageStartIndex = object.value("imageStartIndex").toInt(0);
        QString outputPath = object.value("outputPath").toString();
        if ((outputPath.isEmpty()
             && job.operation != VDQtJobOperation::VideoAnalysis)
            || outputPath.size() > 32768) {
            setError(errorMessage, QStringLiteral("A queued destination path is invalid."));
            return false;
        }
        if (!outputPath.isEmpty() && !QFileInfo(outputPath).isAbsolute())
            outputPath = documentDirectory.absoluteFilePath(outputPath);
        const QJsonObject options = object.value("options").toObject();
        job.options.inputPath = job.sourcePaths.first();
        job.options.outputPath = outputPath.isEmpty()
            ? QString() : QDir::cleanPath(outputPath);
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
        const QJsonArray timelineSegments =
            options.value("timelineSegments").toArray();
        if (timelineSegments.size() > 100000) {
            setError(errorMessage,
                     QStringLiteral("A queued timeline has too many edit segments."));
            return false;
        }
        qint64 timelineLength = 0;
        for (const QJsonValue& segmentValue : timelineSegments) {
            const QJsonObject segmentObject = segmentValue.toObject();
            VDQtTimelineSegment segment;
            segment.sourceStartFrame = static_cast<qint64>(
                segmentObject.value("sourceStartFrame").toDouble(-1));
            segment.frameCount = static_cast<qint64>(
                segmentObject.value("frameCount").toDouble(-1));
            if (!segmentValue.isObject() || segment.sourceStartFrame < 0
                || segment.frameCount <= 0
                || segment.sourceStartFrame + segment.frameCount
                    > std::numeric_limits<int>::max()
                || timelineLength > std::numeric_limits<int>::max()
                    - segment.frameCount) {
                setError(errorMessage,
                         QStringLiteral("A queued timeline segment is invalid."));
                return false;
            }
            timelineLength += segment.frameCount;
            job.options.timelineSegments.append(segment);
        }
        if (job.options.startFrame < 0 || job.options.endFrame < -1
            || job.audioStreamIndex < -1
            || job.audioSourcePath.size() > 32768
            || !std::isfinite(job.progress) || job.progress < 0.0
            || job.progress > 1.0 || job.name.size() > 1024
            || job.id.size() > 128 || job.error.size() > 65536
            || !isSafeImageExtension(job.imageExtension)
            || job.imageQuality < -1 || job.imageQuality > 100
            || job.imageMinimumDigits < 1 || job.imageMinimumDigits > 12
            || job.imageStartIndex < 0
            || !std::isfinite(job.imageSequenceFps)
            || job.imageSequenceFps < 0.0 || job.imageSequenceFps > 1000.0
            || job.rawPixelFormat.size() > 64
            || job.rawWidth < 0 || job.rawWidth > 65536
            || job.rawHeight < 0 || job.rawHeight > 65536
            || !std::isfinite(job.rawFrameRate)
            || job.rawFrameRate < 0.0 || job.rawFrameRate > 10000.0
            || job.rawByteOffset < 0
            || (!job.rawPixelFormat.isEmpty()
                && (job.sourcePaths.size() != 1 || job.rawWidth <= 0
                    || job.rawHeight <= 0 || job.rawFrameRate <= 0.0))
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
