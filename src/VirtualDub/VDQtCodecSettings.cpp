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

void VDQtCodecSettings::setVideoConfig(const VDVideoCodecConfig& cfg) {
    mVideoConfig = cfg;
}

void VDQtCodecSettings::setAudioConfig(const VDAudioCodecConfig& cfg) {
    mAudioConfig = cfg;
}

void VDQtCodecSettings::resetToDefaults() {
    // Video defaults
    mVideoConfig.codecId = "libx264";
    mVideoConfig.codecName = "H.264 / AVC (libx264)";
    mVideoConfig.rateControlMode = "crf";
    mVideoConfig.crf = 23;
    mVideoConfig.targetBitrateKbps = 5000;
    mVideoConfig.maxBitrateKbps = 10000;
    mVideoConfig.preset = "medium";
    mVideoConfig.tune = "none";
    mVideoConfig.profile = "high";
    mVideoConfig.proresProfile = 2; // Standard / SQ
    mVideoConfig.pixFmt = "yuv420p";
    mVideoConfig.keyframeInterval = 250;

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
