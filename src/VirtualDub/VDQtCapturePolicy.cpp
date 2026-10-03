#include "VDQtCapturePolicy.h"
#include <cmath>

bool VDQtCaptureTimeExpired(double durationSeconds, qint64 elapsedMilliseconds) {
    return std::isfinite(durationSeconds) && durationSeconds > 0
        && elapsedMilliseconds >= durationSeconds * 1000.0;
}

QStringList VDQtCaptureOutputArguments(const VDQtCaptureOutputConfig& config, QString *errorMessage) {
    if (errorMessage) errorMessage->clear();
    if (config.path.isEmpty() || config.videoCodec.isEmpty()
        || (config.includeAudio && config.audioCodec.isEmpty())
        || !std::isfinite(config.durationSeconds) || config.durationSeconds < 0
        || config.durationSeconds > 7 * 86400 || (config.segmented && config.segmentSeconds <= 0)) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid capture output settings.");
        return {};
    }
    QStringList args{"-map", "0:v:0"};
    if (config.includeAudio) args << "-map" << "1:a:0";
    args << "-c:v" << config.videoCodec;
    if (config.videoCodec == "libx264" || config.videoCodec == "libx265")
        args << "-preset" << "veryfast" << "-pix_fmt" << "yuv420p";
    if (config.includeAudio)
        args << "-af" << "ebur128=metadata=1" << "-c:a" << config.audioCodec;
    else args << "-an";
    if (config.durationSeconds > 0) args << "-t" << QString::number(config.durationSeconds, 'g', 12);
    if (config.segmented) {
        if (config.videoCodec != "rawvideo")
            args << "-force_key_frames" << QString("expr:gte(t,n_forced*%1)").arg(config.segmentSeconds);
        args << "-f" << "segment" << "-segment_time" << QString::number(config.segmentSeconds)
             << "-reset_timestamps" << "1";
    }
    args << "-stats_period" << "0.25" << "-y" << config.path;
    if (config.preview) {
        args << "-map" << "0:v:0" << "-an" << "-vf" << "scale=640:-2:flags=fast_bilinear"
             << "-c:v" << "mjpeg" << "-q:v" << "7" << "-f" << "image2pipe";
        // -t is per output, not per input/process. Limiting only the recording
        // leaves this otherwise infinite preview output running after it ends.
        if (config.durationSeconds > 0) args << "-t" << QString::number(config.durationSeconds, 'g', 12);
        args << "pipe:1";
    }
    return args;
}
