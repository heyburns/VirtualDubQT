#ifndef VDQT_AUDIO_EXPORT_H
#define VDQT_AUDIO_EXPORT_H

#include "VDQtAudioFilterSystem.h"
#include "VDQtCodecEngine.h"
#include "VDQtTimeline.h"
#include <functional>
#include <cstdint>

class VDQtAudioPlayer;
class VDQtVideoDecoder;

// A complete, operation-owned request. Ranges are source samples in edited
// order; {-1 count} is permitted only for a single range extending to EOF.
// Codec selection never depends on the temporary or destination filename.
struct VDQtAudioExportRequest {
    QString outputPath;
    bool replaceExisting = false; // Approval applies only to inspect-time files.
    VDAudioCodecParams codec;
    QList<VDAudioFilterInstance> filters;
    QList<QPair<int64_t, int64_t>> sampleRanges;
};

// Empty edits mean source identity. An explicitly empty timeline must be
// rejected by the caller. lastFrame is inclusive, -1 means the timeline end.
bool VDQtAudioRangesForTimeline(
    VDQtVideoDecoder& decoder, const QList<VDQtTimelineSegment>& edits,
    qint64 firstFrame, qint64 lastFrame, int sampleRate,
    QList<QPair<int64_t, int64_t>> *ranges, QString *errorMessage,
    const std::function<bool(int, int)>& progress = {});

bool VDQtExportAudio(
    VDQtAudioPlayer& player, const VDQtAudioExportRequest& request,
    const std::function<bool(int, int)>& progress = {}, QString *errorMessage = nullptr);

#endif
