#include "VDQtTimeline.h"

#include <algorithm>
#include <limits>

namespace {

void setError(QString *errorMessage, const QString& message) {
    if (errorMessage) *errorMessage = message;
}

qint64 segmentTotal(const QList<VDQtTimelineSegment>& segments) {
    qint64 total = 0;
    for (const VDQtTimelineSegment& segment : segments) {
        if (segment.frameCount > std::numeric_limits<qint64>::max() - total)
            return -1;
        total += segment.frameCount;
    }
    return total;
}

} // namespace

void VDQtTimeline::reset(qint64 sourceFrameCount, bool exactFrameCount) {
    mSourceFrameCount = std::max<qint64>(0, sourceFrameCount);
    mSourceFrameCountExact = exactFrameCount;
    mSegments.clear();
    if (mSourceFrameCount > 0)
        mSegments.append({0, mSourceFrameCount});
    clearHistory();
}

void VDQtTimeline::setSourceFrameCount(qint64 sourceFrameCount,
                                       bool exactFrameCount) {
    sourceFrameCount = std::max<qint64>(0, sourceFrameCount);
    const bool identityBeforeUpdate = isIdentity();
    mSourceFrameCount = sourceFrameCount;
    mSourceFrameCountExact = exactFrameCount;
    if (identityBeforeUpdate) {
        mSegments.clear();
        if (sourceFrameCount > 0) mSegments.append({0, sourceFrameCount});
        clearHistory();
    }
}

qint64 VDQtTimeline::frameCount() const {
    const qint64 total = segmentTotal(mSegments);
    return std::max<qint64>(0, total);
}

bool VDQtTimeline::isIdentity() const {
    if (mSourceFrameCount == 0) return mSegments.isEmpty();
    return mSegments.size() == 1
        && mSegments.first().sourceStartFrame == 0
        && mSegments.first().frameCount == mSourceFrameCount;
}

bool VDQtTimeline::validateSegments(
    const QList<VDQtTimelineSegment>& segments,
    QString *errorMessage) const {
    qint64 total = 0;
    for (const VDQtTimelineSegment& segment : segments) {
        if (segment.sourceStartFrame < 0 || segment.frameCount <= 0) {
            setError(errorMessage,
                     QStringLiteral("The timeline contains an invalid source range."));
            return false;
        }
        if (segment.sourceStartFrame > std::numeric_limits<qint64>::max()
                - segment.frameCount
            || total > std::numeric_limits<qint64>::max() - segment.frameCount) {
            setError(errorMessage, QStringLiteral("The timeline range is too large."));
            return false;
        }
        const qint64 sourceEnd = segment.sourceStartFrame + segment.frameCount;
        if (mSourceFrameCountExact && sourceEnd > mSourceFrameCount) {
            setError(errorMessage,
                     QStringLiteral("The timeline references frames past the source end."));
            return false;
        }
        total += segment.frameCount;
        if (total > std::numeric_limits<int>::max()) {
            setError(errorMessage,
                     QStringLiteral("The timeline exceeds the editor's frame limit."));
            return false;
        }
    }
    return true;
}

QList<VDQtTimelineSegment> VDQtTimeline::normalized(
    const QList<VDQtTimelineSegment>& segments) {
    QList<VDQtTimelineSegment> result;
    result.reserve(segments.size());
    for (const VDQtTimelineSegment& segment : segments) {
        if (segment.frameCount <= 0) continue;
        if (!result.isEmpty()) {
            VDQtTimelineSegment& previous = result.last();
            if (previous.sourceStartFrame + previous.frameCount
                == segment.sourceStartFrame) {
                previous.frameCount += segment.frameCount;
                continue;
            }
        }
        result.append(segment);
    }
    return result;
}

bool VDQtTimeline::replaceSegments(
    const QList<VDQtTimelineSegment>& segments,
    QString *errorMessage,
    bool clearHistoryAfterReplace) {
    const QList<VDQtTimelineSegment> compact = normalized(segments);
    if (!validateSegments(compact, errorMessage)) return false;
    mSegments = compact;
    if (clearHistoryAfterReplace) clearHistory();
    return true;
}

qint64 VDQtTimeline::mapOutputToSource(qint64 outputFrame) const {
    if (outputFrame < 0) return -1;
    qint64 outputCursor = 0;
    for (const VDQtTimelineSegment& segment : mSegments) {
        if (outputFrame < outputCursor + segment.frameCount)
            return segment.sourceStartFrame + outputFrame - outputCursor;
        outputCursor += segment.frameCount;
    }
    return -1;
}

qint64 VDQtTimeline::mapSourceToOutput(qint64 sourceFrame,
                                       qint64 outputHint,
                                       bool searchForward) const {
    if (sourceFrame < 0) return -1;
    qint64 outputCursor = 0;
    qint64 bestBeforeHint = -1;
    for (const VDQtTimelineSegment& segment : mSegments) {
        if (sourceFrame >= segment.sourceStartFrame
            && sourceFrame < segment.sourceStartFrame + segment.frameCount) {
            const qint64 candidate = outputCursor
                + sourceFrame - segment.sourceStartFrame;
            if (searchForward && candidate >= outputHint) return candidate;
            if (!searchForward && candidate <= outputHint)
                bestBeforeHint = candidate;
        }
        outputCursor += segment.frameCount;
    }
    return searchForward ? -1 : bestBeforeHint;
}

QList<VDQtTimelineSegment> VDQtTimeline::slice(
    qint64 startFrame,
    qint64 endFrameExclusive) const {
    QList<VDQtTimelineSegment> result;
    qint64 outputCursor = 0;
    for (const VDQtTimelineSegment& segment : mSegments) {
        const qint64 segmentStart = outputCursor;
        const qint64 segmentEnd = outputCursor + segment.frameCount;
        const qint64 overlapStart = std::max(startFrame, segmentStart);
        const qint64 overlapEnd = std::min(endFrameExclusive, segmentEnd);
        if (overlapStart < overlapEnd) {
            result.append({segment.sourceStartFrame + overlapStart - segmentStart,
                           overlapEnd - overlapStart});
        }
        outputCursor = segmentEnd;
        if (outputCursor >= endFrameExclusive) break;
    }
    return normalized(result);
}

QList<VDQtTimelineSegment> VDQtTimeline::copyRange(
    qint64 startFrame,
    qint64 endFrameExclusive,
    QString *errorMessage) const {
    if (startFrame < 0 || endFrameExclusive <= startFrame
        || endFrameExclusive > frameCount()) {
        setError(errorMessage, QStringLiteral("The selected timeline range is invalid."));
        return {};
    }
    return slice(startFrame, endFrameExclusive);
}

bool VDQtTimeline::applyEdit(const QList<VDQtTimelineSegment>& segments,
                             QString *errorMessage) {
    const QList<VDQtTimelineSegment> compact = normalized(segments);
    if (!validateSegments(compact, errorMessage)) return false;
    if (compact == mSegments) return true;
    mUndoStack.append(mSegments);
    while (mUndoStack.size() > kMaximumHistoryEntries) mUndoStack.removeFirst();
    mRedoStack.clear();
    mSegments = compact;
    return true;
}

bool VDQtTimeline::deleteRange(qint64 startFrame,
                               qint64 endFrameExclusive,
                               QString *errorMessage) {
    if (startFrame < 0 || endFrameExclusive <= startFrame
        || endFrameExclusive > frameCount()) {
        setError(errorMessage, QStringLiteral("The selected timeline range is invalid."));
        return false;
    }
    QList<VDQtTimelineSegment> result = slice(0, startFrame);
    result.append(slice(endFrameExclusive, frameCount()));
    return applyEdit(result, errorMessage);
}

bool VDQtTimeline::cropToRange(qint64 startFrame,
                               qint64 endFrameExclusive,
                               QString *errorMessage) {
    if (startFrame < 0 || endFrameExclusive <= startFrame
        || endFrameExclusive > frameCount()) {
        setError(errorMessage, QStringLiteral("The selected timeline range is invalid."));
        return false;
    }
    return applyEdit(slice(startFrame, endFrameExclusive), errorMessage);
}

bool VDQtTimeline::insert(qint64 outputFrame,
                          const QList<VDQtTimelineSegment>& segments,
                          QString *errorMessage) {
    if (outputFrame < 0 || outputFrame > frameCount()) {
        setError(errorMessage, QStringLiteral("The timeline insertion point is invalid."));
        return false;
    }
    const QList<VDQtTimelineSegment> compact = normalized(segments);
    if (compact.isEmpty()) {
        setError(errorMessage, QStringLiteral("There are no copied frames to insert."));
        return false;
    }
    if (!validateSegments(compact, errorMessage)) return false;
    QList<VDQtTimelineSegment> result = slice(0, outputFrame);
    result.append(compact);
    result.append(slice(outputFrame, frameCount()));
    return applyEdit(result, errorMessage);
}

bool VDQtTimeline::replaceRange(
    qint64 startFrame,
    qint64 endFrameExclusive,
    const QList<VDQtTimelineSegment>& segments,
    QString *errorMessage) {
    if (startFrame < 0 || endFrameExclusive < startFrame
        || endFrameExclusive > frameCount()) {
        setError(errorMessage, QStringLiteral("The selected timeline range is invalid."));
        return false;
    }
    const QList<VDQtTimelineSegment> compact = normalized(segments);
    if (!compact.isEmpty() && !validateSegments(compact, errorMessage)) return false;
    QList<VDQtTimelineSegment> result = slice(0, startFrame);
    result.append(compact);
    result.append(slice(endFrameExclusive, frameCount()));
    return applyEdit(result, errorMessage);
}

bool VDQtTimeline::resetEdits(QString *errorMessage) {
    QList<VDQtTimelineSegment> identity;
    if (mSourceFrameCount > 0) identity.append({0, mSourceFrameCount});
    return applyEdit(identity, errorMessage);
}

bool VDQtTimeline::clear(QString *errorMessage) {
    return applyEdit({}, errorMessage);
}

bool VDQtTimeline::undo() {
    if (mUndoStack.isEmpty()) return false;
    mRedoStack.append(mSegments);
    mSegments = mUndoStack.takeLast();
    return true;
}

bool VDQtTimeline::redo() {
    if (mRedoStack.isEmpty()) return false;
    mUndoStack.append(mSegments);
    mSegments = mRedoStack.takeLast();
    return true;
}

void VDQtTimeline::clearHistory() {
    mUndoStack.clear();
    mRedoStack.clear();
}
