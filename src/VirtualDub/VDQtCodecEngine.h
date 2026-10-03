#ifndef VDQTCODECENGINE_H
#define VDQTCODECENGINE_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

struct VDAudioCodecConfig;

// User-facing encoder capabilities. This is a curated catalog over the system
// FFmpeg installation, not a codec implementation of our own.
struct VDVideoCodecInfo {
    QString id;                 // e.g. "libx264", "prores_ks", "ffv1", "huffyuv", "cfhd"
    QString name;               // e.g. "FFmpeg H.264 / AVC (libx264)"
    QString description;        // e.g. "High efficiency MPEG-4 AVC video encoder"
    bool supportsCrf;
    bool supportsBitrate;
    bool supportsPresets;
    bool supportsTune;
    bool supportsProfiles;
    bool isLossless;
};

struct VDVideoCodecParams {
    QString codecId = "libx264";
    
    // Rate Control
    QString rateMode = "crf";           // "default", "crf", "bitrate", "cqp", "lossless"
    int crf = 23;                       // 0..51
    int targetBitrateKbps = 0;          // e.g. 6000
    int maxBitrateKbps = 0;
    bool twoPass = false;               // bitrate mode only
    
    // Speed & Tuning
    QString preset = "medium";          // "ultrafast" .. "veryslow"
    QString tune = "none";              // "film", "animation", "grain", "stillimage", "fastdecode", "zerolatency", "none"
    QString profile = "high";           // "baseline", "main", "high", "high10"
    
    // Color & Depth
    QString pixFmt = "yuv420p";         // "yuv420p", "yuv422p", "yuv444p", "yuv420p10le", "yuv422p10le", "rgb24", "rgba"
    QString colorMatrix;                // "bt709", "bt2020", "smpte170m"
    
    // Keyframes & GOP
    int keyframeInterval = 0;           // GOP size
    int bFrames = 0;                    // B-frame count
    
    // ProRes specific
    int proresProfile = 3;              // 0: Proxy, 1: LT, 2: Standard/SQ, 3: HQ, 4: 4444, 5: 4444 XQ
    QString proresVendor;               // "appl", "fmp4"
    
    // FFV1 specific
    int ffv1Version = 3;                // 1, 3
    int ffv1Coder = 1;                  // 0: Golomb, 1: Range Coder
    int ffv1Slices = 16;                // 4, 16, 24
    
    // HuffYUV specific
    int huffyuvPredictor = 1;           // 0: Left, 1: Plane, 2: Median
    
    // CineForm specific
    int cineformQuality = 3;            // FFmpeg 0..12, from film3+ (0) through low (12).
};

// Explicitly supported controls, shared by the dialog and command builder.
// An encoder name starting with "lib" does not imply x264-compatible options.
// Uncurated encoders remain usable with their encoder defaults, but we do not
// advertise rate-control options whose effect this application cannot promise.
struct VDVideoCodecCapabilities {
    QStringList rateModes = {QStringLiteral("default")};
    QStringList presets;
    QStringList tunes;
    QStringList profiles;
    int qualityMinimum = 0;
    int qualityMaximum = 63;
    bool supportsMaxBitrate = false;
    bool supportsTwoPass = false;
    bool supportsKeyframes = false;
    bool supportsBFrames = false;
};

struct VDVideoEncoderClock {
    int numerator = 1;
    int denominator = 1000000;
};

// Audio counterpart to VDVideoCodecInfo/VDVideoCodecParams.
struct VDAudioCodecInfo {
    QString id;                 // e.g. "aac", "libmp3lame", "libopus", "ac3", "flac", "pcm_s16le"
    QString name;
    QString description;
    bool supportsVbr = false;
    bool supportsCbr = false;
    bool isLossless = false;
};

struct VDAudioCodecParams {
    QString codecId = "aac";
    QString rateMode = "cbr";           // "vbr", "cbr"
    int vbrQuality = 3;                 // 1..5
    int bitrateKbps = 192;              // 64..320
    int sampleRate = 0;                 // 0: Source, 44100, 48000, 96000
    int channels = 0;                   // 0: Source, 1: Mono, 2: Stereo, 6: 5.1
    int bitDepth = 16;                  // 16, 24, 32
};

// Session codec registry. It remembers a separate option set per video codec
// so switching H.264 -> FFV1 -> H.264 restores the previous H.264 choices.
// Exporters ask this class for normalized FFmpeg command-line arguments; they
// should not reproduce codec-specific option mapping themselves.
class VDQtCodecEngine {
public:
    VDQtCodecEngine();
    ~VDQtCodecEngine();

    static VDQtCodecEngine& instance();

    QList<VDVideoCodecInfo> getAvailableVideoCodecs() const;
    QList<VDAudioCodecInfo> getAvailableAudioCodecs() const;

    static VDVideoCodecParams getDefaultVideoParamsForCodec(const QString &codecId);
    static VDVideoCodecCapabilities getVideoCapabilities(const QString &codecId);
    static VDVideoEncoderClock getVideoEncoderClock(const QString& codecId, double nominalFrameRate);

    // Applies only verified encoder controls. Unsupported saved combinations
    // fail with a diagnostic instead of silently choosing a different mode.
    // The destination argument list is untouched on failure.
    static bool buildFfmpegVideoEncodeArguments(
        const VDVideoCodecParams& params, bool preserveNativeVfr,
        QStringList *arguments, QString *errorMessage = nullptr);

    const VDVideoCodecParams& getVideoParams() const { return mVideoParams; }
    void setVideoParams(const VDVideoCodecParams& params);

    VDVideoCodecParams getVideoParamsForCodec(const QString &codecId) const;
    void setVideoParamsForCodec(const QString &codecId, const VDVideoCodecParams& params);

    const VDAudioCodecParams& getAudioParams() const { return mAudioParams; }
    void setAudioParams(const VDAudioCodecParams& params);

    bool checkAudioEncoderAvailable(const QString &codecId, QString *outError = nullptr) const;
    bool checkVideoEncoderAvailable(const QString &codecId, QString *outError = nullptr) const;

    // Builds the complete FFmpeg audio encoder option set used by every video
    // export path. Keeping this centralized prevents direct-video-copy exports
    // from silently ignoring rate control, resampling, or channel settings.
    static VDAudioCodecParams audioParamsFromConfig(
        const VDAudioCodecConfig& config,
        int sourceSampleRate = 0,
        int sourceChannels = 0);
    static QStringList buildFfmpegAudioEncodeArguments(const VDAudioCodecParams& params);

    void resetToDefaults();

private:
    VDVideoCodecParams mVideoParams;
    VDAudioCodecParams mAudioParams;
    QMap<QString, VDVideoCodecParams> mCodecParamsMap;
};

#endif // VDQTCODECENGINE_H
