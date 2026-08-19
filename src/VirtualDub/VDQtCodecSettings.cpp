#include "VDQtCodecSettings.h"
#include <QDir>

VDQtCodecSettings::VDQtCodecSettings() {
    resetToDefaults();
}

VDQtCodecSettings::~VDQtCodecSettings() {
}

VDQtCodecSettings& VDQtCodecSettings::instance() {
    static VDQtCodecSettings sInst;
    return sInst;
}

void VDQtCodecSettings::setAudioConfig(const VDAudioCodecConfig& cfg) {
    mAudioConfig = cfg;
}

void VDQtCodecSettings::resetToDefaults() {
    // Audio defaults
    mAudioConfig.codecId = "aac";
    mAudioConfig.codecName = "AAC (Advanced Audio Coding)";
    mAudioConfig.rateControlMode = "vbr";
    mAudioConfig.bitrateKbps = 192;
    mAudioConfig.vbrQuality = 4;
    mAudioConfig.sampleRate = 0; // Same as source
    mAudioConfig.channels = 0;   // Same as source

    // Save Audio dialog session defaults
    mSaveAudioConfig.directory = QDir::homePath();
    mSaveAudioConfig.codecId = "pcm_s16le";
    mSaveAudioConfig.rateControlMode = "cbr";
    mSaveAudioConfig.bitrateKbps = 192;
    mSaveAudioConfig.vbrQuality = 2;
    mSaveAudioConfig.sampleRate = 0;
    mSaveAudioConfig.channels = 0;
    mSaveAudioConfig.fileTypeIndex = 0;

    // Save Video dialog session defaults
    mSaveVideoConfig.directory = QDir::homePath();
    mSaveVideoConfig.fileTypeIndex = 6; // MP4
    mSaveVideoConfig.lastFileName = "output.mp4";
}
