// One immutable media-runtime selection for the GUI, jobs and subprocesses.
// No PATH search, system-tool fallback or user-selectable FFmpeg override.
#pragma once

#include <QString>

namespace VDQtMediaRuntime {
QString root();
QString ffmpeg();
QString ffprobe();
// Existing encoder sets naming ffmpeg/ffprobe (even absolute system paths)
// follow the same bundled-only policy. Other user-selected encoders stay intact.
QString encoderProgram(const QString& configuredProgram);
bool validate(QString *errorMessage = nullptr);
QString report();
}
