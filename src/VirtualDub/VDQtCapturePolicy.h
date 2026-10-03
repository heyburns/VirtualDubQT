#ifndef VDQT_CAPTURE_POLICY_H
#define VDQT_CAPTURE_POLICY_H

#include <QStringList>

// Input/device discovery stays in the capture dialog. Output construction is
// shared with hardware-free process tests so recording and preview cannot drift
// into different duration policies. Zero duration means explicit untimed capture.
struct VDQtCaptureOutputConfig {
    QString path;
    QString videoCodec = QStringLiteral("rawvideo");
    QString audioCodec = QStringLiteral("pcm_s16le");
    bool includeAudio = false;
    bool preview = false;
    bool segmented = false;
    int segmentSeconds = 60;
    double durationSeconds = 0;
};

QStringList VDQtCaptureOutputArguments(const VDQtCaptureOutputConfig& config,
                                     QString *errorMessage = nullptr);
bool VDQtCaptureTimeExpired(double durationSeconds, qint64 elapsedMilliseconds);

#endif
