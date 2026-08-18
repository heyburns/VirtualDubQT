#include "VDQtCodecEngine.h"
#include <algorithm>

VDQtCodecEngine::VDQtCodecEngine() {
    resetToDefaults();
}

VDQtCodecEngine::~VDQtCodecEngine() {
}

VDQtCodecEngine& VDQtCodecEngine::instance() {
    static VDQtCodecEngine inst;
    return inst;
}

VDVideoCodecParams VDQtCodecEngine::getDefaultVideoParamsForCodec(const QString &codecId) {
    VDVideoCodecParams p;
    p.codecId = codecId;
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
    } else {
        p.pixFmt = "yuv420p";
        p.keyframeInterval = 250;
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

extern "C" {
#include <libavcodec/avcodec.h>
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
