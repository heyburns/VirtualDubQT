#ifndef VDQTCODECENGINE_H
#define VDQTCODECENGINE_H

#include <QString>
#include <QList>
#include <QMap>

// Video Codec Definitions
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
    QString rateMode = "crf";           // "crf", "bitrate", "cqp", "lossless"
    int crf = 23;                       // 0..51
    int targetBitrateKbps = 0;          // e.g. 6000
    int maxBitrateKbps = 0;
    
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
    int cineformQuality = 3;            // 0: medium+, 1: high+, 2: film1+, 3: film2+, 4: film3+
};

// Audio Codec Definitions
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

class VDQtCodecEngine {
public:
    VDQtCodecEngine();
    ~VDQtCodecEngine();

    static VDQtCodecEngine& instance();

    QList<VDVideoCodecInfo> getAvailableVideoCodecs() const;
    QList<VDAudioCodecInfo> getAvailableAudioCodecs() const;

    static VDVideoCodecParams getDefaultVideoParamsForCodec(const QString &codecId);

    const VDVideoCodecParams& getVideoParams() const { return mVideoParams; }
    void setVideoParams(const VDVideoCodecParams& params);

    VDVideoCodecParams getVideoParamsForCodec(const QString &codecId) const;
    void setVideoParamsForCodec(const QString &codecId, const VDVideoCodecParams& params);

    const VDAudioCodecParams& getAudioParams() const { return mAudioParams; }
    void setAudioParams(const VDAudioCodecParams& params);

    bool checkAudioEncoderAvailable(const QString &codecId, QString *outError = nullptr) const;
    bool checkVideoEncoderAvailable(const QString &codecId, QString *outError = nullptr) const;

    void resetToDefaults();

private:
    VDVideoCodecParams mVideoParams;
    VDAudioCodecParams mAudioParams;
    QMap<QString, VDVideoCodecParams> mCodecParamsMap;
};

#endif // VDQTCODECENGINE_H
