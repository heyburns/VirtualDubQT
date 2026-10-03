// Curated FFmpeg encoder catalog and option normalization. Availability checks
// interrogate the linked libavcodec build; argument builders are the canonical
// translation from UI/project parameters to ffmpeg(1) encoder options.
#include "VDQtCodecEngine.h"
#include "VDQtCodecSettings.h"
#include <algorithm>
#include <cmath>
#include <QSet>

extern "C" {
#include <libavcodec/avcodec.h>
}

VDQtCodecEngine::VDQtCodecEngine() {
    resetToDefaults();
}

VDQtCodecEngine::~VDQtCodecEngine() {
}

VDQtCodecEngine& VDQtCodecEngine::instance() {
    static VDQtCodecEngine inst;
    return inst;
}

VDVideoCodecCapabilities VDQtCodecEngine::getVideoCapabilities(const QString& codecId) {
    VDVideoCodecCapabilities result;
    const QString id = codecId.trimmed().toLower();
    if (id == "libx264" || id == "libx264_10bit"
        || id == "libx265" || id == "libx265_lossless") {
        result.rateModes = id == "libx265_lossless"
            ? QStringList{"lossless"} : QStringList{"crf", "bitrate", "cqp", "lossless"};
        result.qualityMaximum = 51;
        result.presets = {"ultrafast", "superfast", "veryfast", "faster", "fast",
                          "medium", "slow", "slower", "veryslow", "placebo"};
        result.tunes = id.startsWith("libx265")
            ? QStringList{"psnr", "ssim", "grain", "fastdecode", "zerolatency", "animation"}
            : QStringList{"film", "animation", "grain", "stillimage", "psnr",
                          "ssim", "fastdecode", "zerolatency"};
        result.profiles = id.startsWith("libx265")
            ? QStringList{"main", "main10", "main12", "main422-10", "main422-12",
                          "main444-8", "main444-10", "main444-12"}
            : QStringList{"baseline", "main", "high", "high10", "high422", "high444"};
        result.supportsMaxBitrate = id != "libx265_lossless";
        result.supportsTwoPass = id != "libx265_lossless";
        result.supportsKeyframes = result.supportsBFrames = true;
    } else if (id == "libvpx" || id == "libvpx-vp9") {
        result.rateModes = {"crf", "bitrate"};
        if (id == "libvpx-vp9") result.rateModes.append("lossless");
        result.supportsMaxBitrate = result.supportsTwoPass = true;
        result.supportsKeyframes = true;
    } else if (id == "libsvtav1") {
        result.rateModes = {"crf", "bitrate", "cqp"};
        for (int preset = 0; preset <= 13; ++preset)
            result.presets.append(QString::number(preset));
        result.supportsKeyframes = true;
    } else if (id == "mpeg4" || id == "mpeg2video" || id == "mpeg1video") {
        result.rateModes = {"cqp", "bitrate"};
        result.qualityMinimum = 1;
        result.qualityMaximum = 31;
        result.supportsMaxBitrate = result.supportsKeyframes = result.supportsBFrames = true;
    } else if (id == "mjpeg") {
        result.rateModes = {"cqp"};
        result.qualityMinimum = 1;
        result.qualityMaximum = 31;
    }
    return result;
}

VDVideoEncoderClock VDQtCodecEngine::getVideoEncoderClock(const QString& codecId, double nominalFrameRate) {
    const QString id = codecId.trimmed().toLower();
    if (id == "mpeg4") return {1, 60000}; // MPEG-4's denominator is limited to 16 bits.
    if (id != "mpeg1video" && id != "mpeg2video") return {};
    AVRational rate{60, 1};
    const AVCodec *codec = avcodec_find_encoder_by_name(id.toUtf8().constData());
    const AVRational *supportedRates = nullptr;
#if LIBAVCODEC_VERSION_MAJOR >= 62
    const void *configuration = nullptr;
    if (codec && avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_FRAME_RATE,
            0, &configuration, nullptr) >= 0)
        supportedRates = static_cast<const AVRational*>(configuration);
#else
    if (codec) supportedRates = codec->supported_framerates;
#endif
    // Retain an encodable source rate. Otherwise use the encoder's finest valid
    // clock; precise NUT input timestamps still determine the VFR presentation.
    double fastest = 0;
    if (supportedRates) {
        for (const AVRational *candidate = supportedRates; candidate->num > 0 && candidate->den > 0; ++candidate) {
            const double fps = static_cast<double>(candidate->num) / candidate->den;
            if (std::isfinite(nominalFrameRate) && nominalFrameRate > 0
                && std::abs(fps - nominalFrameRate) <= nominalFrameRate * 1e-7) {
                rate = *candidate;
                break;
            }
            if (fps > fastest) { fastest = fps; rate = *candidate; }
        }
    }
    return {rate.den, rate.num};
}

// -----------------------------------------------------------------------------
// Runtime encoder catalog
// -----------------------------------------------------------------------------

QList<VDVideoCodecInfo> VDQtCodecEngine::getAvailableVideoCodecs() const {
    // Preferred encoders appear first with richer capability metadata. They are
    // still filtered against the local FFmpeg build; the UI must never promise
    // an encoder merely because this application knows how to configure it.
    const QList<VDVideoCodecInfo> preferred = {
        {QStringLiteral("rawvideo"), QStringLiteral("Uncompressed RGB/YCbCr"),
         QStringLiteral("Uncompressed native frames."), false, false, false, false, false, true},
        {QStringLiteral("prores_ks"), QStringLiteral("Apple ProRes"),
         QStringLiteral("10-bit intra-frame ProRes proxy through 4444 XQ."), false, false, false, false, true, false},
        {QStringLiteral("libx264"), QStringLiteral("H.264 / AVC"),
         QStringLiteral("x264 8-bit H.264 encoder."), true, true, true, true, true, false},
        {QStringLiteral("libx264_10bit"), QStringLiteral("H.264 / AVC 10-bit"),
         QStringLiteral("x264 H.264 with a 10-bit pixel format."), true, true, true, true, true, false},
        {QStringLiteral("libx265"), QStringLiteral("H.265 / HEVC"),
         QStringLiteral("x265 HEVC encoder."), true, true, true, true, true, false},
        {QStringLiteral("libx265_lossless"), QStringLiteral("H.265 lossless"),
         QStringLiteral("Lossless x265 HEVC mode."), false, false, true, true, true, true},
        {QStringLiteral("libvpx"), QStringLiteral("VP8"),
         QStringLiteral("WebM VP8 encoder."), true, true, false, false, false, false},
        {QStringLiteral("libvpx-vp9"), QStringLiteral("VP9"),
         QStringLiteral("WebM VP9 encoder."), true, true, false, false, false, false},
        {QStringLiteral("libsvtav1"), QStringLiteral("AV1 (SVT-AV1)"),
         QStringLiteral("Scalable Video Technology AV1 encoder."), true, true, true, false, false, false},
        {QStringLiteral("ffv1"), QStringLiteral("FFV1"),
         QStringLiteral("Mathematically lossless intra-frame FFV1."), false, false, false, false, false, true},
        {QStringLiteral("huffyuv"), QStringLiteral("HuffYUV"),
         QStringLiteral("Lossless HuffYUV encoder."), false, false, false, false, false, true},
        {QStringLiteral("cfhd"), QStringLiteral("GoPro CineForm"),
         QStringLiteral("Wavelet CineForm intermediate codec."), false, false, false, false, false, false}
    };
    QList<VDVideoCodecInfo> result;
    QSet<QString> included;
    for (const VDVideoCodecInfo& info : preferred) {
        QString probeId = info.id;
        if (probeId == QStringLiteral("libx264_10bit")) probeId = QStringLiteral("libx264");
        else if (probeId == QStringLiteral("libx265_lossless")) probeId = QStringLiteral("libx265");
        if (probeId == QStringLiteral("rawvideo")
            || avcodec_find_encoder_by_name(probeId.toUtf8().constData())) {
            result.append(info);
            included.insert(info.id);
            included.insert(probeId);
        }
    }

    // Preserve access to less common distribution-provided encoders by adding
    // everything else after the curated list in alphabetical order.
    QList<VDVideoCodecInfo> discovered;
    void *iterator = nullptr;
    const AVCodec *codec = nullptr;
    while ((codec = av_codec_iterate(&iterator))) {
        if (!av_codec_is_encoder(codec) || codec->type != AVMEDIA_TYPE_VIDEO
            || !codec->name) continue;
        const QString id = QString::fromUtf8(codec->name);
        if (included.contains(id)) continue;
        const AVCodecDescriptor *descriptor = avcodec_descriptor_get(codec->id);
        const bool lossless = descriptor
            && (descriptor->props & AV_CODEC_PROP_LOSSLESS);
        const QString longName = codec->long_name
            ? QString::fromUtf8(codec->long_name) : id;
        const auto controls = getVideoCapabilities(id);
        discovered.append({id, longName,
            QStringLiteral("Installed FFmpeg video encoder (%1).").arg(id),
            controls.rateModes.contains("crf"), controls.rateModes.contains("bitrate"),
            !controls.presets.isEmpty(), !controls.tunes.isEmpty(),
            !controls.profiles.isEmpty(), lossless});
        included.insert(id);
    }
    std::sort(discovered.begin(), discovered.end(),
        [](const VDVideoCodecInfo& left, const VDVideoCodecInfo& right) {
            return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
        });
    result.append(discovered);
    return result;
}

QList<VDAudioCodecInfo> VDQtCodecEngine::getAvailableAudioCodecs() const {
    const QList<VDAudioCodecInfo> preferred = {
        {QStringLiteral("pcm_s16le"), QStringLiteral("PCM 16-bit"),
         QStringLiteral("Uncompressed signed 16-bit PCM."), false, false, true},
        {QStringLiteral("pcm_s24le"), QStringLiteral("PCM 24-bit"),
         QStringLiteral("Uncompressed signed 24-bit PCM."), false, false, true},
        {QStringLiteral("aac"), QStringLiteral("AAC"),
         QStringLiteral("FFmpeg AAC encoder."), true, true, false},
        {QStringLiteral("libmp3lame"), QStringLiteral("MP3"),
         QStringLiteral("LAME MP3 encoder."), true, true, false},
        {QStringLiteral("libopus"), QStringLiteral("Opus"),
         QStringLiteral("Opus audio encoder."), true, true, false},
        {QStringLiteral("libvorbis"), QStringLiteral("Vorbis"),
         QStringLiteral("Ogg Vorbis encoder."), true, true, false},
        {QStringLiteral("ac3"), QStringLiteral("Dolby Digital AC-3"),
         QStringLiteral("AC-3 audio encoder."), false, true, false},
        {QStringLiteral("flac"), QStringLiteral("FLAC"),
         QStringLiteral("Lossless FLAC audio encoder."), false, false, true}
    };
    QList<VDAudioCodecInfo> result;
    QSet<QString> included;
    for (const VDAudioCodecInfo& info : preferred) {
        if (avcodec_find_encoder_by_name(info.id.toUtf8().constData())) {
            result.append(info);
            included.insert(info.id);
        }
    }
    QList<VDAudioCodecInfo> discovered;
    void *iterator = nullptr;
    const AVCodec *codec = nullptr;
    while ((codec = av_codec_iterate(&iterator))) {
        if (!av_codec_is_encoder(codec) || codec->type != AVMEDIA_TYPE_AUDIO
            || !codec->name) continue;
        const QString id = QString::fromUtf8(codec->name);
        if (included.contains(id)) continue;
        const AVCodecDescriptor *descriptor = avcodec_descriptor_get(codec->id);
        const bool lossless = descriptor
            && (descriptor->props & AV_CODEC_PROP_LOSSLESS);
        const QString longName = codec->long_name
            ? QString::fromUtf8(codec->long_name) : id;
        discovered.append({id, longName,
            QStringLiteral("Installed FFmpeg audio encoder (%1).").arg(id),
            !lossless, !lossless, lossless});
        included.insert(id);
    }
    std::sort(discovered.begin(), discovered.end(),
        [](const VDAudioCodecInfo& left, const VDAudioCodecInfo& right) {
            return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
        });
    result.append(discovered);
    return result;
}

// -----------------------------------------------------------------------------
// Session parameters and codec-specific defaults
// -----------------------------------------------------------------------------

VDVideoCodecParams VDQtCodecEngine::getDefaultVideoParamsForCodec(const QString &requestedCodecId) {
    const QString codecId = requestedCodecId.trimmed().toLower();
    VDVideoCodecParams p;
    p.codecId = codecId;
    // Fields from other codec families are retained in the saved schema for
    // compatibility, but neutral defaults must not look like active controls.
    p.rateMode = "default";
    p.preset.clear();
    p.profile.clear();
    p.tune = "none";
    if (codecId == "libx264" || codecId == "libx264_10bit") {
        p.rateMode = "crf";
        p.crf = 23;
        p.preset = "medium";
        p.tune = "none";
        p.profile = (codecId == "libx264_10bit") ? "high10" : "high";
        p.pixFmt = (codecId == "libx264_10bit") ? "yuv420p10le" : "yuv420p";
        p.keyframeInterval = 250;
        p.bFrames = 3;
    } else if (codecId == "libx265") {
        p.rateMode = "crf";
        p.crf = 28;
        p.preset = "medium";
        p.tune = "none";
        p.profile = "main";
        p.pixFmt = "yuv420p";
        p.keyframeInterval = 250;
        p.bFrames = 3;
    } else if (codecId == "libx265_lossless") {
        p.rateMode = "lossless";
        p.crf = 0;
        p.preset = "medium";
        p.tune = "none";
        p.profile = "main";
        p.pixFmt = "yuv420p";
        p.keyframeInterval = 250;
        p.bFrames = 3;
    } else if (codecId == "libvpx") {
        p.rateMode = "crf";
        p.crf = 10;
        p.targetBitrateKbps = 2000;
        p.preset = "medium";
        p.pixFmt = "yuv420p";
        p.keyframeInterval = 120;
    } else if (codecId == "libvpx-vp9") {
        p.rateMode = "crf";
        p.crf = 31;
        p.targetBitrateKbps = 0;
        p.preset = "medium";
        p.pixFmt = "yuv420p";
        p.keyframeInterval = 240;
    } else if (codecId == "libsvtav1") {
        p.rateMode = "crf";
        p.crf = 30;
        p.preset = "6";
        p.pixFmt = "yuv420p";
        p.keyframeInterval = 240;
    } else if (codecId == "prores_ks") {
        p.proresProfile = 2; // Standard / SQ
        p.proresVendor = "appl";
        p.pixFmt = "yuv422p10le";
        p.keyframeInterval = 1;
    } else if (codecId == "ffv1") {
        p.ffv1Version = 3;
        p.ffv1Coder = 1;
        p.ffv1Slices = 16;
        p.pixFmt = "yuv420p";
        p.keyframeInterval = 1;
    } else if (codecId == "huffyuv") {
        p.huffyuvPredictor = 1;
        p.pixFmt = "yuv422p";
        p.keyframeInterval = 1;
    } else if (codecId == "cfhd") {
        p.cineformQuality = 3;
        p.pixFmt = "yuv422p10le";
        p.keyframeInterval = 1;
    } else if (codecId == "mpeg4" || codecId == "mpeg2video" || codecId == "mpeg1video"
               || codecId == "mjpeg") {
        p.rateMode = "cqp";
        p.crf = 5; // Native MPEG/MJPEG qscale, not an x264-style CRF value.
        p.pixFmt = codecId == "mjpeg" ? "yuvj420p" : "yuv420p";
        p.keyframeInterval = codecId == "mjpeg" ? 0 : 250;
    } else {
        p.pixFmt = "yuv420p";
        p.keyframeInterval = 0;
    }
    return p;
}

VDVideoCodecParams VDQtCodecEngine::getVideoParamsForCodec(const QString &codecId) const {
    if (mCodecParamsMap.contains(codecId)) {
        return mCodecParamsMap.value(codecId);
    }
    return getDefaultVideoParamsForCodec(codecId);
}

void VDQtCodecEngine::setVideoParamsForCodec(const QString &codecId, const VDVideoCodecParams& params) {
    mCodecParamsMap[codecId] = params;
    if (mVideoParams.codecId == codecId) {
        mVideoParams = params;
    }
}

void VDQtCodecEngine::setVideoParams(const VDVideoCodecParams& params) {
    mVideoParams = params;
    mCodecParamsMap[params.codecId] = params;
}

void VDQtCodecEngine::setAudioParams(const VDAudioCodecParams& params) {
    mAudioParams = params;
}

void VDQtCodecEngine::resetToDefaults() {
    // These defaults are application-session state. Project/job snapshots copy
    // concrete values, so resetting here cannot silently alter queued work.
    mCodecParamsMap.clear();
    mVideoParams = getDefaultVideoParamsForCodec("prores_ks");

    // Audio defaults
    mAudioParams.codecId = "aac";
    mAudioParams.rateMode = "vbr";
    mAudioParams.vbrQuality = 4;
    mAudioParams.bitrateKbps = 192;
    mAudioParams.sampleRate = 0;
    mAudioParams.channels = 0;
    mAudioParams.bitDepth = 16;
}

VDAudioCodecParams VDQtCodecEngine::audioParamsFromConfig(
    const VDAudioCodecConfig& config,
    int sourceSampleRate,
    int sourceChannels)
{
    VDAudioCodecParams params;
    params.codecId = config.codecId;
    params.rateMode = config.rateControlMode;
    params.vbrQuality = config.vbrQuality;
    params.bitrateKbps = config.bitrateKbps;
    params.sampleRate = config.sampleRate > 0 ? config.sampleRate
                                              : std::max(0, sourceSampleRate);
    params.channels = config.channels > 0 ? config.channels
                                           : std::max(0, sourceChannels);
    if (config.codecId.compare(QStringLiteral("pcm_s24le"), Qt::CaseInsensitive) == 0)
        params.bitDepth = 24;
    else if (config.codecId.compare(QStringLiteral("pcm_s32le"), Qt::CaseInsensitive) == 0
             || config.codecId.compare(QStringLiteral("pcm_f32le"), Qt::CaseInsensitive) == 0)
        params.bitDepth = 32;
    else
        params.bitDepth = 16;
    return params;
}

// -----------------------------------------------------------------------------
// FFmpeg command-line translation and capability checks
// -----------------------------------------------------------------------------

bool VDQtCodecEngine::buildFfmpegVideoEncodeArguments(
    const VDVideoCodecParams& params, bool preserveNativeVfr,
    QStringList *arguments, QString *errorMessage)
{
    const QString id = params.codecId.trimmed().toLower();
    const auto capabilities = getVideoCapabilities(id);
    const auto fail = [&](const QString& message) {
        if (errorMessage) *errorMessage = QStringLiteral("%1: %2").arg(params.codecId, message);
        return false;
    };
    if (!arguments) return fail(QStringLiteral("No encoder argument destination was supplied."));
    const bool fixedMode = capabilities.rateModes == QStringList{"default"};
    // Old files inherited CRF 23/preset medium/profile high even for intra-frame
    // encoders with their own quality controls. These inert schema defaults are
    // accepted, but a non-default ignored rate request is not.
    const bool legacyFixedDefault = fixedMode && params.rateMode == "crf" && params.crf == 23;
    const bool intrinsicLossless = (id == "ffv1" || id == "huffyuv" || id == "rawvideo"
        || id == "(uncompressed)" || id == "uncompressed" || id.isEmpty())
        && params.rateMode == "lossless";
    if (!capabilities.rateModes.contains(params.rateMode) && !legacyFixedDefault && !intrinsicLossless)
        return fail(QStringLiteral("Rate-control mode '%1' is unsupported. Available modes: %2.")
                    .arg(params.rateMode, capabilities.rateModes.join(", ")));
    if ((params.rateMode == "crf" || params.rateMode == "cqp") && !fixedMode
        && (params.crf < capabilities.qualityMinimum || params.crf > capabilities.qualityMaximum))
        return fail(QStringLiteral("Quality must be between %1 and %2.")
                    .arg(capabilities.qualityMinimum).arg(capabilities.qualityMaximum));
    if (params.targetBitrateKbps < 0 || params.targetBitrateKbps > 1000000
        || params.maxBitrateKbps < 0 || params.maxBitrateKbps > 1000000)
        return fail(QStringLiteral("Bitrates must be between 0 and 1000000 kbps."));
    if (params.rateMode == "bitrate" && params.targetBitrateKbps == 0)
        return fail(QStringLiteral("Target bitrate must be greater than zero."));
    if (id == "libvpx" && params.rateMode == "crf" && params.targetBitrateKbps == 0)
        return fail(QStringLiteral("VP8 constrained quality also requires a positive target bitrate."));
    if (params.maxBitrateKbps > 0) {
        if (!capabilities.supportsMaxBitrate || params.rateMode == "cqp" || params.rateMode == "lossless")
            return fail(QStringLiteral("A maximum bitrate is unsupported in the selected mode."));
        if (params.rateMode == "bitrate" && params.maxBitrateKbps < params.targetBitrateKbps)
            return fail(QStringLiteral("Maximum bitrate cannot be lower than the target bitrate."));
        if (id.startsWith("libvpx") && params.rateMode != "bitrate")
            return fail(QStringLiteral("VP8/VP9 maximum bitrate requires bitrate mode."));
    }
    if (params.twoPass && (!capabilities.supportsTwoPass || params.rateMode != "bitrate"))
        return fail(QStringLiteral("Two-pass encoding requires a supported encoder in bitrate mode."));
    const auto validateChoice = [&](const QString& value, const QStringList& choices,
                                    const QString& neutral, const QString& label) {
        if (value.isEmpty() || value == neutral) return true;
        if (choices.isEmpty() || !choices.contains(value))
            return fail(QStringLiteral("Unsupported %1 '%2'.").arg(label, value));
        return true;
    };
    if (!validateChoice(params.preset, capabilities.presets,
                        capabilities.presets.isEmpty() ? "medium" : QString(), "preset")
        || !validateChoice(params.tune, capabilities.tunes, "none", "tune")
        || !validateChoice(params.profile, capabilities.profiles,
                           capabilities.profiles.isEmpty() ? "high" : QString(), "profile")) return false;
    if (params.keyframeInterval < 0 || params.keyframeInterval > 10000
        || params.bFrames < 0 || params.bFrames > 16)
        return fail(QStringLiteral("Keyframe interval must be 0..10000 and B-frames 0..16."));
    if (!capabilities.supportsBFrames && params.bFrames > 0)
        return fail(QStringLiteral("B-frame control is unsupported by this encoder."));
    if (!capabilities.supportsKeyframes && params.keyframeInterval > 1)
        return fail(QStringLiteral("Keyframe control is unsupported by this encoder."));
    const bool x264 = id == "libx264" || id == "libx264_10bit";
    const bool x265 = id == "libx265" || id == "libx265_lossless";
    if (x264 && params.profile == "baseline" && params.bFrames > 0)
        return fail(QStringLiteral("The H.264 baseline profile does not support B-frames. Set B-frames to zero or select another profile."));
    if (x264 && (params.rateMode == "lossless"
        || ((params.rateMode == "cqp" || params.rateMode == "crf") && params.crf == 0))
        && !params.profile.isEmpty() && params.profile != "high444")
        return fail(QStringLiteral("Lossless x264 requires the encoder-default or high444 profile."));

    QStringList result;
    const QString encoder = id == "libx264_10bit" ? QStringLiteral("libx264")
        : id == "libx265_lossless" ? QStringLiteral("libx265")
        : (id.isEmpty() || id == "(uncompressed)" || id == "uncompressed")
            ? QStringLiteral("rawvideo") : id;
    result << "-c:v" << encoder;
    if (id == "prores_ks") {
        if (params.proresProfile < 0 || params.proresProfile > 5)
            return fail(QStringLiteral("ProRes profile must be 0..5."));
        result << "-profile:v" << QString::number(params.proresProfile);
        if (!params.proresVendor.isEmpty()) result << "-vendor" << params.proresVendor;
    } else if (id == "ffv1") {
        if ((params.ffv1Version != 1 && params.ffv1Version != 3)
            || params.ffv1Coder < 0 || params.ffv1Coder > 1
            || !QList<int>{1, 4, 6, 9, 12, 16, 24, 30}.contains(params.ffv1Slices))
            return fail(QStringLiteral("Invalid FFV1 version, coder or slice count."));
        result << "-level" << QString::number(params.ffv1Version)
               << "-coder" << QString::number(params.ffv1Coder)
               << "-slices" << QString::number(params.ffv1Slices);
    } else if (id == "huffyuv") {
        if (params.huffyuvPredictor < 0 || params.huffyuvPredictor > 2)
            return fail(QStringLiteral("HuffYUV predictor must be 0..2."));
        result << "-pred" << QString::number(params.huffyuvPredictor);
    } else if (id == "cfhd") {
        if (params.cineformQuality < 0 || params.cineformQuality > 12)
            return fail(QStringLiteral("CineForm quality must be 0..12."));
        result << "-quality" << QString::number(params.cineformQuality);
    } else if (params.rateMode == "lossless") {
        if (x264) result << "-qp" << "0";
        else if (x265) result << "-x265-params" << "lossless=1";
        else if (id == "libvpx-vp9") result << "-lossless" << "1" << "-b:v" << "0";
    } else if (params.rateMode == "bitrate") {
        result << "-b:v" << QString("%1k").arg(params.targetBitrateKbps);
    } else if (params.rateMode == "crf" && !fixedMode) {
        result << "-crf" << QString::number(params.crf);
        if (id == "libvpx") result << "-b:v" << QString("%1k").arg(params.targetBitrateKbps);
        else if (id == "libvpx-vp9" || id == "libsvtav1") result << "-b:v" << "0";
    } else if (params.rateMode == "cqp") {
        result << ((x264 || x265 || id == "libsvtav1") ? "-qp" : "-q:v")
               << QString::number(params.crf);
        if (id == "libsvtav1") result << "-b:v" << "0";
    }
    if (!capabilities.presets.isEmpty() && !params.preset.isEmpty())
        result << "-preset" << params.preset;
    if (!capabilities.tunes.isEmpty() && !params.tune.isEmpty() && params.tune != "none")
        result << "-tune" << params.tune;
    if (!capabilities.profiles.isEmpty() && !params.profile.isEmpty())
        result << "-profile:v" << params.profile;
    if (params.maxBitrateKbps > 0) {
        // FFmpeg requires a VBV buffer with maxrate. Use an explicit two-second
        // reservoir; multiply in 64 bits rather than overflowing an int.
        result << "-maxrate" << QString("%1k").arg(params.maxBitrateKbps)
               << "-bufsize" << QString("%1k").arg(qint64(params.maxBitrateKbps) * 2);
    }
    if (capabilities.supportsKeyframes && params.keyframeInterval > 0)
        result << "-g" << QString::number(params.keyframeInterval);
    if (capabilities.supportsBFrames)
        result << "-bf" << QString::number(preserveNativeVfr ? 0 : params.bFrames);
    *arguments = result;
    if (errorMessage) errorMessage->clear();
    return true;
}

QStringList VDQtCodecEngine::buildFfmpegAudioEncodeArguments(
    const VDAudioCodecParams& params)
{
    QStringList args;
    QString codec = params.codecId.trimmed().toLower();
    if (codec.isEmpty()) codec = QStringLiteral("aac");
    const bool isVbr = params.rateMode.compare(QStringLiteral("vbr"), Qt::CaseInsensitive) == 0;

    if (codec == "aac") {
        args << "-c:a" << "aac";
        if (isVbr) {
            static constexpr double qualities[] = { 0.2, 0.5, 0.9, 1.4, 2.0 };
            const int index = std::clamp(params.vbrQuality - 1, 0, 4);
            args << "-q:a" << QString::number(qualities[index], 'f', 2);
        } else {
            args << "-b:a" << QString("%1k").arg(params.bitrateKbps > 0
                                                    ? params.bitrateKbps : 192);
        }
    } else if (codec == "libfdk_aac") {
        args << "-c:a" << "libfdk_aac";
        if (isVbr) {
            args << "-vbr" << QString::number(std::clamp(params.vbrQuality, 1, 5));
        } else {
            args << "-b:a" << QString("%1k").arg(params.bitrateKbps > 0
                                                    ? params.bitrateKbps : 192);
        }
    } else if (codec == "libmp3lame" || codec == "mp3") {
        args << "-c:a" << "libmp3lame";
        if (isVbr) {
            args << "-q:a" << QString::number(std::clamp(params.vbrQuality, 0, 9));
        } else {
            args << "-b:a" << QString("%1k").arg(params.bitrateKbps > 0
                                                    ? params.bitrateKbps : 192);
        }
    } else if (codec == "libopus" || codec == "opus") {
        const bool haveLibOpus = avcodec_find_encoder_by_name("libopus") != nullptr;
        args << "-c:a" << (haveLibOpus ? "libopus" : "opus");
        if (!haveLibOpus) args << "-strict" << "-2";
        args << "-b:a" << QString("%1k").arg(params.bitrateKbps > 0
                                                ? params.bitrateKbps : 160)
             << "-vbr" << (isVbr ? "on" : "off");
    } else if (codec == "libvorbis" || codec == "vorbis") {
        args << "-c:a" << "libvorbis";
        if (isVbr) {
            args << "-q:a" << QString::number(std::clamp(params.vbrQuality, 0, 10));
        } else {
            args << "-b:a" << QString("%1k").arg(params.bitrateKbps > 0
                                                    ? params.bitrateKbps : 160);
        }
    } else if (codec == "flac") {
        args << "-c:a" << "flac"
             << "-compression_level"
             << QString::number(std::clamp(params.vbrQuality > 0
                                               ? params.vbrQuality : 5,
                                           0, 8));
    } else if (codec == "ac3") {
        args << "-c:a" << "ac3"
             << "-b:a" << QString("%1k").arg(params.bitrateKbps > 0
                                                ? params.bitrateKbps : 384);
    } else if (codec == "(uncompressed)" || codec == "uncompressed"
               || codec.startsWith("pcm")) {
        QString pcmCodec = codec;
        if (pcmCodec == "(uncompressed)" || pcmCodec == "uncompressed"
            || pcmCodec == "pcm") {
            if (params.bitDepth >= 32) pcmCodec = QStringLiteral("pcm_s32le");
            else if (params.bitDepth >= 24) pcmCodec = QStringLiteral("pcm_s24le");
            else pcmCodec = QStringLiteral("pcm_s16le");
        }
        args << "-c:a" << pcmCodec;
    } else {
        args << "-c:a" << codec;
        if (params.bitrateKbps > 0)
            args << "-b:a" << QString("%1k").arg(params.bitrateKbps);
    }

    // Some encoders accept only a small set of sampling rates. Normalize here
    // rather than letting different export callers make different decisions.
    if (codec == "libopus" || codec == "opus") {
        int rate = params.sampleRate;
        if (rate != 8000 && rate != 12000 && rate != 16000
            && rate != 24000 && rate != 48000) {
            rate = 48000;
        }
        args << "-ar" << QString::number(rate);
    } else if (codec == "ac3") {
        int rate = params.sampleRate;
        if (rate != 32000 && rate != 44100 && rate != 48000)
            rate = 48000;
        args << "-ar" << QString::number(rate);
    } else if (params.sampleRate > 0 && params.sampleRate <= 192000) {
        args << "-ar" << QString::number(params.sampleRate);
    }

    if (params.channels > 0 && params.channels <= 8)
        args << "-ac" << QString::number(params.channels);
    return args;
}

bool VDQtCodecEngine::checkAudioEncoderAvailable(const QString &codecId, QString *outError) const {
    QString id = codecId.trimmed().toLower();
    if (id.isEmpty() || id == "pcm_s16le" || id == "pcm_s24le" || id == "pcm_f32le" || id == "(uncompressed)" || id == "uncompressed") {
        return true;
    }

    // Video export invokes FFmpeg directly, so only an FFmpeg encoder counts as
    // available here. Standalone command-line encoders are handled separately by
    // the dedicated audio-export workflow.
    if (id == "libmp3lame" || id == "mp3") {
        if (avcodec_find_encoder_by_name("libmp3lame") != nullptr) {
            return true;
        }
        if (outError) {
            *outError = QString("The MP3 audio encoder is not available on your system.\n\n"
                                "• Your FFmpeg installation was built without 'libmp3lame' support.\n"
                                "To resolve this:\n"
                                "1. Select another audio codec (e.g. AAC, Opus, FLAC, or Uncompressed WAV).\n"
                                "2. Or install/recompile FFmpeg with libmp3lame enabled.");
        }
        return false;
    }

    // Special case: Opus (native 'opus' or external-library 'libopus' in FFmpeg)
    if (id == "libopus" || id == "opus") {
        if (avcodec_find_encoder_by_name("opus") != nullptr || avcodec_find_encoder_by_name("libopus") != nullptr) {
            return true;
        }
        if (outError) {
            *outError = QString("The Opus audio encoder is not available in your FFmpeg installation.\n\n"
                                "To resolve this:\n"
                                "1. Select another audio codec (e.g. AAC, FLAC, or Uncompressed WAV).\n"
                                "2. Or install/recompile FFmpeg with Opus encoder support enabled.");
        }
        return false;
    }

    // AAC encoder names are not interchangeable in the FFmpeg command line.
    if (id == "aac" || id == "libfdk_aac") {
        if (avcodec_find_encoder_by_name(id.toUtf8().constData()) != nullptr) {
            return true;
        }
        if (outError) {
            *outError = QString("The AAC audio encoder is not available in your FFmpeg installation.\n\n"
                                "To resolve this:\n"
                                "1. Select another audio codec (e.g. FLAC or Uncompressed WAV).\n"
                                "2. Or install/recompile FFmpeg with AAC support enabled.");
        }
        return false;
    }

    // The export pipeline requests libvorbis explicitly. FFmpeg's native
    // experimental encoder is not a drop-in alias for that command line.
    if (id == "libvorbis" || id == "vorbis") {
        if (avcodec_find_encoder_by_name("libvorbis") != nullptr) {
            return true;
        }
        if (outError) {
            *outError = QString("The Vorbis audio encoder is not available in your FFmpeg installation.\n\n"
                                "To resolve this:\n"
                                "1. Select another audio codec (e.g. AAC, Opus, FLAC, or Uncompressed WAV).\n"
                                "2. Or install/recompile FFmpeg with Vorbis support enabled.");
        }
        return false;
    }

    // Check avcodec for the encoder name
    const AVCodec *codec = avcodec_find_encoder_by_name(id.toUtf8().constData());
    if (codec != nullptr) {
        return true;
    }

    if (outError) {
        *outError = QString("The audio encoder '%1' is not available in your FFmpeg installation.\n\n"
                            "To resolve this:\n"
                            "1. Choose an available audio codec (such as AAC, FLAC, or Uncompressed WAV).\n"
                            "2. Or install/recompile FFmpeg with support for '%1' enabled.")
                    .arg(codecId);
    }
    return false;
}

bool VDQtCodecEngine::checkVideoEncoderAvailable(const QString &codecId, QString *outError) const {
    QString id = codecId.trimmed().toLower();
    if (id.isEmpty() || id == "(uncompressed)" || id == "uncompressed" || id == "rawvideo") {
        return true;
    }

    // Handle x265 variants
    if (id == "libx265_lossless") {
        id = "libx265";
    } else if (id == "libx264_10bit") {
        id = "libx264";
    }

    // Check direct encoder name
    const AVCodec *codec = avcodec_find_encoder_by_name(id.toUtf8().constData());
    if (codec != nullptr) {
        return true;
    }

    if (outError) {
        *outError = QString("The video encoder '%1' is not available in your FFmpeg installation.\n\n"
                            "To resolve this:\n"
                            "1. Choose an available video codec (such as FFV1, HuffYUV, ProRes, or Uncompressed).\n"
                            "2. Or install/recompile FFmpeg with support for '%1' enabled.")
                    .arg(codecId);
    }
    return false;
}
