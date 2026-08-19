#ifndef VDQTCODECSETTINGS_H
#define VDQTCODECSETTINGS_H

#include <QString>

struct VDAudioCodecConfig {
    QString codecId = "aac";
    QString codecName = "AAC (Advanced Audio Coding)";
    
    // Rate Control Mode: "cbr", "vbr"
    QString rateControlMode = "cbr";
    int bitrateKbps = 192;
    int vbrQuality = 3;
    
    // Sample Rate & Channels
    int sampleRate = 0;
    int channels = 0;
};

struct VDSaveAudioSessionConfig {
    QString directory;
    QString codecId = "pcm_s16le";
    QString rateControlMode = "cbr";
    int bitrateKbps = 192;
    int vbrQuality = 2;
    int sampleRate = 0;
    int channels = 0;
    int fileTypeIndex = 0;
};

struct VDSaveVideoSessionConfig {
    QString directory;
    int fileTypeIndex = 6; // Default MP4
    QString lastFileName = "output.mp4";
};

class VDQtCodecSettings {
public:
    VDQtCodecSettings();
    ~VDQtCodecSettings();

    static VDQtCodecSettings& instance();

    const VDAudioCodecConfig& getAudioConfig() const { return mAudioConfig; }
    void setAudioConfig(const VDAudioCodecConfig& cfg);

    const VDSaveAudioSessionConfig& getSaveAudioSessionConfig() const { return mSaveAudioConfig; }
    void setSaveAudioSessionConfig(const VDSaveAudioSessionConfig& cfg) { mSaveAudioConfig = cfg; }

    const VDSaveVideoSessionConfig& getSaveVideoSessionConfig() const { return mSaveVideoConfig; }
    void setSaveVideoSessionConfig(const VDSaveVideoSessionConfig& cfg) { mSaveVideoConfig = cfg; }

    void resetToDefaults();

private:
    VDAudioCodecConfig mAudioConfig;
    VDSaveAudioSessionConfig mSaveAudioConfig;
    VDSaveVideoSessionConfig mSaveVideoConfig;
};

#endif // VDQTCODECSETTINGS_H
