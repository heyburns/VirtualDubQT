// Versioned JSON serialization for processing settings, editing projects, and
// job queues. Helper functions below keep enum/config encoding symmetric and
// validate bounded values before public loaders commit a reconstructed state.
#include "VDQtProjectFile.h"
#include "VDQtColorPolicy.h"
#include "VDQtFilterValidation.h"

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

constexpr int kDocumentVersion = 7;
// Processing settings contain no timeline intent and their schema is unchanged.
// Keep writing their established version so older builds can still read them.
constexpr int kProcessingSettingsVersion = 6;
constexpr int kOldestSupportedDocumentVersion = 1;
constexpr qint64 kMaximumDocumentBytes = qint64{4} * 1024 * 1024;

// JSON helpers are paired to make format evolution reviewable. New optional
// keys need defaults in the corresponding parser; incompatible structural
// changes require a document-version bump and migration handling.
void setError(QString *errorMessage, const QString& message) {
    if (errorMessage) *errorMessage = message;
}

// JSON numbers are doubles. Validate before converting, not after: INT64_MAX
// rounds to 2^63 as a double, so an inclusive double(max) check admits undefined
// behavior. Missing legacy fields retain their defaults; present wrong types,
// fractional values and unrepresentable integers must never become defaults.
template<class Integer>
bool readIntegerValue(const QJsonValue& value, const QString& name,
                      Integer *destination, qint64 minimum, qint64 maximum,
                      qint64 fallback, QString *errorMessage) {
    const double number = value.isUndefined() ? static_cast<double>(fallback)
                                              : value.toDouble();
    if ((!value.isUndefined() && !value.isDouble())
        || !std::isfinite(number) || std::trunc(number) != number
        || number >= std::ldexp(1.0, 63)
        || static_cast<long double>(number) < static_cast<long double>(minimum)
        || static_cast<long double>(number) > static_cast<long double>(maximum)) {
        setError(errorMessage, QStringLiteral("The saved integer '%1' is invalid.").arg(name));
        return false;
    }
    *destination = static_cast<Integer>(number);
    return true;
}

template<class Integer>
bool readInteger(const QJsonObject& object, const char *key, Integer *destination,
                 qint64 minimum, qint64 maximum, qint64 fallback,
                 QString *errorMessage) {
    return readIntegerValue(object.value(QLatin1String(key)), QLatin1String(key),
                            destination, minimum, maximum, fallback, errorMessage);
}

bool parseTimelineIntent(const QJsonObject& object, int version,
                         bool *explicitTimeline, QString *errorMessage) {
    const QJsonValue segments = object.value("timelineSegments");
    if (version < 7) {
        // Legacy empty/missing arrays meant source identity, not delete-all.
        *explicitTimeline = !segments.toArray().isEmpty();
        return true;
    }
    const QJsonValue intent = object.value("timelineExplicit");
    if (!intent.isBool() || !segments.isArray()
        || (!intent.toBool() && !segments.toArray().isEmpty())) {
        setError(errorMessage, QStringLiteral("The saved timeline intent is missing or inconsistent."));
        return false;
    }
    *explicitTimeline = intent.toBool();
    return true;
}

bool isSafeImageExtension(const QString& extension) {
    if (extension.isEmpty() || extension.size() > 16) return false;
    for (const QChar character : extension) {
        if (!character.isLetterOrNumber()) return false;
    }
    return true;
}

QJsonObject videoCodecToJson(const VDVideoCodecParams& value) {
    // Field names are intentionally descriptive rather than matching FFmpeg
    // flags. This file format represents editor intent; VDQtCodecEngine owns
    // the separate translation to the command line used by the installed build.
    QJsonObject object;
    object["codecId"] = value.codecId;
    object["rateMode"] = value.rateMode;
    object["crf"] = value.crf;
    object["targetBitrateKbps"] = value.targetBitrateKbps;
    object["maxBitrateKbps"] = value.maxBitrateKbps;
    object["twoPass"] = value.twoPass;
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
    value.twoPass = object.value("twoPass").toBool(value.twoPass);
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
    // Processing state is source-independent. It is reused by settings files,
    // projects, and every queued job, so all three document types must pass
    // through this single serializer/parser pair to avoid behavior drift.
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
        if (filter.type == VDFilterType::Plugin) {
            filterObject["pluginId"] = filter.pluginId;
            filterObject["pluginConfiguration"] = QString::fromLatin1(
                filter.pluginConfiguration.toBase64());
        }
        QJsonObject parameters;
        for (auto it = filter.params.cbegin(); it != filter.params.cend(); ++it)
            parameters[it.key()] = it.value();
        filterObject["parameters"] = parameters;
        QJsonObject stringParameters;
        for (auto it = filter.stringParams.cbegin();
             it != filter.stringParams.cend(); ++it)
            stringParameters[it.key()] = it.value();
        filterObject["stringParameters"] = stringParameters;
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
    // Parse into a fresh value and publish only at the end. Besides making the
    // operation transactional, this applies current defaults to keys absent in
    // older documents before their explicitly stored values are overlaid.
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
        || !VDQtResolveColorMatrix(result.rawVideo.colorMatrix, 8)) {
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

    // Limits below defend both memory use and later UI/processing loops. Project
    // files are user-editable JSON and therefore must be treated as untrusted.
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
        filter.pluginId = filterObject.value("pluginId").toString();
        const QString encodedConfiguration =
            filterObject.value("pluginConfiguration").toString();
        if (encodedConfiguration.size() > 1400000) {
            setError(errorMessage, QStringLiteral("A plugin filter configuration is too large."));
            return false;
        }
        filter.pluginConfiguration = QByteArray::fromBase64(
            encodedConfiguration.toLatin1());
        const QJsonObject parameters = filterObject.value("parameters").toObject();
        const QJsonObject stringParameters =
            filterObject.value("stringParameters").toObject();
        if (parameters.size() > 128 || filter.name.size() > 256
            || filter.id.size() > 256 || filter.pluginId.size() > 256
            || stringParameters.size() > 64
            || (filter.type == VDFilterType::Plugin && filter.pluginId.isEmpty())) {
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
        for (auto it = stringParameters.constBegin();
             it != stringParameters.constEnd(); ++it) {
            if (!it.value().isString() || it.key().size() > 128
                || it.value().toString().size() > 65536) {
                setError(errorMessage,
                         QStringLiteral("A string-valued filter parameter is invalid."));
                return false;
            }
            filter.stringParams.insert(it.key(), it.value().toString());
        }
        QString filterError;
        if (!VDQtValidateFilter(filter, &filterError)) {
            setError(errorMessage, QStringLiteral("Filter '%1': %2").arg(filter.name, filterError));
            return false;
        }
        result.filters.append(filter);
    }

    // Assign legacy IDs in the configuration itself, so all later pipeline
    // snapshots and saved projects refer to the same independent stages.
    result.filters = VDQtFilterSystem::normalizeChainIds(std::move(result.filters));

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
    if (!VDQtValidateAudioFilters(result.audioFilters, errorMessage)) return false;

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
    // QSaveFile writes beside the destination and renames on commit. A crash or
    // full disk can therefore leave the previous valid project intact instead
    // of replacing it with a truncated JSON document.
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
    // Kind prevents accidentally treating a job list as a project merely
    // because both are JSON; version bounds provide a clear migration boundary.
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

// Public save/load functions add the document kind/version envelope and use
// QSaveFile for atomic replacement. Loaders parse into local state first.
bool VDQtProjectFile::saveProcessingSettings(
    const QString& path,
    const VDQtProcessingState& state,
    QString *errorMessage) {
    QJsonObject root;
    root["kind"] = QStringLiteral("VirtualDubQTProcessingSettings");
    root["version"] = kProcessingSettingsVersion;
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
    // Project-specific fields describe the source, edited timeline, playhead,
    // audio selection, and markers. The processing snapshot is appended once at
    // the end so it remains identical to standalone processing settings.
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
    root["zoomEnabled"] = state.zoomEnabled;
    root["zoomStart"] = static_cast<double>(state.zoomStart);
    root["zoomEnd"] = static_cast<double>(state.zoomEnd);
    QJsonArray markers;
    for (qint64 marker : state.markers)
        markers.append(static_cast<double>(marker));
    root["markers"] = markers;
    root["sourceFrameCount"] = static_cast<double>(state.sourceFrameCount);
    root["sourceFrameCountExact"] = state.sourceFrameCountExact;
    root["timelineExplicit"] = state.hasExplicitTimeline();
    QJsonArray timelineSegments;
    for (const VDQtTimelineSegment& segment : state.timelineSegments) {
        QJsonObject segmentObject;
        segmentObject["sourceStartFrame"] =
            static_cast<double>(segment.sourceStartFrame);
        segmentObject["frameCount"] = static_cast<double>(segment.frameCount);
        segmentObject["masked"] = segment.masked;
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
    // Keep reconstructed state local until source lists, timeline segments, UI
    // positions, and processing settings have all passed validation.
    QJsonObject root;
    if (!readDocument(path, QStringLiteral("VirtualDubQTProject"), &root, errorMessage))
        return false;

    VDQtProjectState result;
    const int version = root.value("version").toInt();
    if (!parseTimelineIntent(root, version, &result.timelineExplicit, errorMessage)) return false;
    if (version >= 7 && !root.value("sourceFrameCountExact").isBool()) {
        setError(errorMessage, QStringLiteral("The saved source-count accuracy is missing or invalid."));
        return false;
    }
    // Older projects treated saved counts as exact; retain that migration rule.
    result.sourceFrameCountExact = version < 7 || root.value("sourceFrameCountExact").toBool();
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
    result.rawFrameRate = root.value("rawFrameRate").toDouble(0.0);
    if (!readInteger(root, "rawWidth", &result.rawWidth, 0, 65536, 0, errorMessage)
        || !readInteger(root, "rawHeight", &result.rawHeight, 0, 65536, 0, errorMessage)
        || !readInteger(root, "rawByteOffset", &result.rawByteOffset, 0,
                        std::numeric_limits<qint64>::max(), 0, errorMessage)) return false;
    result.audioSourcePath = root.value("audioSourcePath").toString();
    if (!result.audioSourcePath.isEmpty()
        && !QFileInfo(result.audioSourcePath).isAbsolute()) {
        result.audioSourcePath = QFileInfo(path).absoluteDir().absoluteFilePath(
            result.audioSourcePath);
    }
    if (!result.audioSourcePath.isEmpty())
        result.audioSourcePath = QDir::cleanPath(result.audioSourcePath);
    result.audioDisabled = root.value("audioDisabled").toBool(false);
    result.hasSelection = root.value("hasSelection").toBool(false);
    constexpr qint64 maximumFrame = std::numeric_limits<int>::max();
    if (!readInteger(root, "audioStreamIndex", &result.audioStreamIndex, -1, maximumFrame, -1, errorMessage)
        || !readInteger(root, "position", &result.position, 0, maximumFrame, 0, errorMessage)
        || !readInteger(root, "selectionStart", &result.selectionStart, 0, maximumFrame, 0, errorMessage)
        || !readInteger(root, "selectionEnd", &result.selectionEnd, 0, maximumFrame, 0, errorMessage)
        || !readInteger(root, "sourceFrameCount", &result.sourceFrameCount, 0, maximumFrame, 0, errorMessage)
        || !readInteger(root, "zoomStart", &result.zoomStart, 0, maximumFrame, 0, errorMessage)
        || !readInteger(root, "zoomEnd", &result.zoomEnd, 0, maximumFrame, 0, errorMessage)) return false;
    const QJsonArray markers = root.value("markers").toArray();
    if (markers.size() > 100000) {
        setError(errorMessage,
                 QStringLiteral("The project contains too many timeline markers."));
        return false;
    }
    for (const QJsonValue& markerValue : markers) {
        qint64 marker;
        if (!readIntegerValue(markerValue, QStringLiteral("marker"), &marker,
                              0, maximumFrame, -1, errorMessage)) return false;
        result.markers.append(marker);
    }
    result.zoomEnabled = root.value("zoomEnabled").toBool(false);
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
        || result.zoomStart < 0 || result.zoomEnd < result.zoomStart
        || result.zoomEnd > std::numeric_limits<int>::max()
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
        if (!readInteger(segmentObject, "sourceStartFrame", &segment.sourceStartFrame,
                         0, maximumFrame, -1, errorMessage)
            || !readInteger(segmentObject, "frameCount", &segment.frameCount,
                            1, maximumFrame, -1, errorMessage)) return false;
        segment.masked = segmentObject.value("masked").toBool(false);
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
            || result.selectionEnd > timelineLength
            || (result.zoomEnabled && result.zoomEnd > timelineLength))) {
        setError(errorMessage,
                 QStringLiteral("The saved position or selection is outside the edited timeline."));
        return false;
    }
    if (result.timelineExplicit && result.timelineSegments.isEmpty()
        && (result.position != 0 || result.hasSelection || result.selectionStart != 0
            || result.selectionEnd != 0 || result.zoomEnabled)) {
        setError(errorMessage, QStringLiteral("An empty edited timeline cannot contain a position, selection, or zoom range."));
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
        options["timelineExplicit"] = job.options.hasExplicitTimeline();
        QJsonArray timelineSegments;
        for (const VDQtTimelineSegment& segment : job.options.timelineSegments) {
            QJsonObject segmentObject;
            segmentObject["sourceStartFrame"] =
                static_cast<double>(segment.sourceStartFrame);
            segmentObject["frameCount"] = static_cast<double>(segment.frameCount);
            segmentObject["masked"] = segment.masked;
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
    // Job records intentionally contain their full processing snapshot. Queue
    // execution must not depend on whichever options happen to be selected in
    // the editor when the application is restarted later.
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
    // As with projects, build a temporary list first. One malformed record
    // rejects the document rather than leaving a partially replaced live queue.
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
        job.rawFrameRate = object.value("rawFrameRate").toDouble(0.0);
        constexpr qint64 maximumFrame = std::numeric_limits<int>::max();
        if (!readInteger(object, "rawWidth", &job.rawWidth, 0, 65536, 0, errorMessage)
            || !readInteger(object, "rawHeight", &job.rawHeight, 0, 65536, 0, errorMessage)
            || !readInteger(object, "rawByteOffset", &job.rawByteOffset, 0,
                            std::numeric_limits<qint64>::max(), 0, errorMessage)
            || !readInteger(object, "audioStreamIndex", &job.audioStreamIndex, -1, maximumFrame, -1, errorMessage)
            || !readInteger(object, "imageQuality", &job.imageQuality, -1, 100, -1, errorMessage)
            || !readInteger(object, "imageMinimumDigits", &job.imageMinimumDigits, 1, 12, 6, errorMessage)
            || !readInteger(object, "imageStartIndex", &job.imageStartIndex, 0, maximumFrame, 0, errorMessage)) return false;
        if (!job.audioSourcePath.isEmpty()
            && !QFileInfo(job.audioSourcePath).isAbsolute()) {
            job.audioSourcePath = documentDirectory.absoluteFilePath(
                job.audioSourcePath);
        }
        if (!job.audioSourcePath.isEmpty())
            job.audioSourcePath = QDir::cleanPath(job.audioSourcePath);
        job.audioDisabled = object.value("audioDisabled").toBool(false);
        job.imageExtension = object.value("imageExtension").toString(
            QStringLiteral("png"));
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
        if (!parseTimelineIntent(options, root.value("version").toInt(),
                                 &job.options.timelineExplicit, errorMessage)) return false;
        job.options.inputPath = job.sourcePaths.first();
        job.options.outputPath = outputPath.isEmpty()
            ? QString() : QDir::cleanPath(outputPath);
        if (!readInteger(options, "startFrame", &job.options.startFrame, 0, maximumFrame, 0, errorMessage)
            || !readInteger(options, "endFrame", &job.options.endFrame, -1, maximumFrame, -1, errorMessage)
            || !readInteger(options, "decimateFactor", &job.options.decimateFactor, 1, 1000000, 1, errorMessage)
            || !readInteger(options, "videoMode", &job.options.videoMode,
                            VideoMode_DirectStreamCopy, VideoMode_FullProcessing, VideoMode_FullProcessing, errorMessage)
            || !readInteger(options, "audioMode", &job.options.audioMode,
                            AudioMode_DirectStreamCopy, AudioMode_FullProcessing, AudioMode_DirectStreamCopy, errorMessage)) return false;
        job.options.customFps = options.value("customFps").toDouble();
        job.options.convertFpsPreserveDuration =
            options.value("convertFpsPreserveDuration").toBool(false);
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
            if (!segmentValue.isObject()
                || !readInteger(segmentObject, "sourceStartFrame", &segment.sourceStartFrame,
                                0, maximumFrame, -1, errorMessage)
                || !readInteger(segmentObject, "frameCount", &segment.frameCount,
                                1, maximumFrame, -1, errorMessage)) {
                if (!segmentValue.isObject())
                    setError(errorMessage, QStringLiteral("A queued timeline segment is malformed."));
                return false;
            }
            segment.masked = segmentObject.value("masked").toBool(false);
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
