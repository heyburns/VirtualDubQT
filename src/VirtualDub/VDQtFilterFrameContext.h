#ifndef VDQT_FILTER_FRAME_CONTEXT_H
#define VDQT_FILTER_FRAME_CONTEXT_H

#include "VDQtFilterSystem.h"
#include "VDQtTimeline.h"
#include "VDQtVideoDecoder.h"
#include <algorithm>
#include <cmath>
#include <limits>

// One timing contract for preview, clipboard, analysis, exports and serving.
// An empty segment list means identity here; explicit-empty timelines must be
// rejected by the caller before decoding. All nonempty lists are prevalidated.
inline VDFilterFrameContext VDQtFilterContextForFrame(
    VDQtVideoDecoder& decoder, const QList<VDQtTimelineSegment>& segments,
    qint64 frame, double sourceRate = 0.0) {
    VDFilterFrameContext context;
    context.frameNumber = frame;
    context.frameRate = sourceRate > 0 && std::isfinite(sourceRate) ? sourceRate : decoder.getFps();
    const auto elapsed = [&decoder](qint64 ordinal) {
        return ordinal >= 0 && ordinal <= std::numeric_limits<int>::max()
            ? decoder.getFrameElapsedSeconds(static_cast<int>(ordinal))
            : std::numeric_limits<double>::quiet_NaN();
    };
    qint64 timingSource = frame, source = frame, cursor = 0, heldSource = -1;
    double seconds = segments.isEmpty() ? elapsed(frame) : 0.0;
    for (const auto& segment : segments) {
        const qint64 count = std::min(segment.frameCount, frame - cursor);
        seconds += elapsed(segment.sourceStartFrame + count) - elapsed(segment.sourceStartFrame);
        if (frame < cursor + segment.frameCount) {
            timingSource = segment.sourceStartFrame + frame - cursor;
            source = segment.masked ? (heldSource >= 0 ? heldSource : segment.sourceStartFrame) : timingSource;
            break;
        }
        if (!segment.masked) heldSource = segment.sourceStartFrame + segment.frameCount - 1;
        cursor += segment.frameCount;
    }
    const double rateScale = context.frameRate > 0 && decoder.getFps() > 0
        ? decoder.getFps() / context.frameRate : 1.0;
    context.timestampSeconds = seconds * rateScale;
    if (!std::isfinite(context.timestampSeconds) && context.frameRate > 0)
        context.timestampSeconds = frame / context.frameRate;
    context.sourceFrameNumber = source;
    context.sourceTimestampSeconds = source >= 0 && source <= std::numeric_limits<int>::max()
        ? decoder.getFrameTimestampSeconds(static_cast<int>(source)) : -1.0;
    context.inputDurationSeconds = timingSource >= 0 && timingSource <= std::numeric_limits<int>::max()
        ? decoder.getFrameDurationSeconds(static_cast<int>(timingSource)) * rateScale : 0.0;
    context.outputFrameNumber = frame;
    context.outputTimestampSeconds = context.timestampSeconds;
    return context;
}

#endif
