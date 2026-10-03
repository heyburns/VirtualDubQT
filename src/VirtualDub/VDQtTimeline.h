#ifndef VDQTTIMELINE_H
#define VDQTTIMELINE_H

#include <QList>
#include <QString>

// A timeline segment references a half-open range in the decoder's flattened
// source stream. Appended media is flattened by the concat input, so edits do
// not need to own decoders or frame buffers.
struct VDQtTimelineSegment {
    qint64 sourceStartFrame = 0;
    qint64 frameCount = 0;
    // Masked ranges retain their timeline duration but display the last frame
    // from the preceding unmasked range, matching VirtualDub's FrameSubset.
    bool masked = false;

    bool operator==(const VDQtTimelineSegment& other) const {
        return sourceStartFrame == other.sourceStartFrame
            && frameCount == other.frameCount
            && masked == other.masked;
    }
};

// Non-destructive edit list. Segment operations never touch media; they only
// map output-frame positions to the flattened decoder source. Every mutating
// edit normalizes adjacent compatible segments and records a bounded undo
// snapshot. Half-open ranges [start,end) are used throughout to avoid fencepost
// ambiguity at selection end markers.
class VDQtTimeline {
public:
    static constexpr int kMaximumHistoryEntries = 100;

    void reset(qint64 sourceFrameCount, bool exactFrameCount);
    void setSourceFrameCount(qint64 sourceFrameCount, bool exactFrameCount);

    qint64 sourceFrameCount() const { return mSourceFrameCount; }
    bool sourceFrameCountExact() const { return mSourceFrameCountExact; }
    qint64 frameCount() const;
    // Identity is intent, not a guess from the current metadata/segment shape.
    // An unknown identity source has no known frames yet; it is not an empty edit.
    bool isEmpty() const { return !mIdentity && mSegments.isEmpty(); }
    bool isIdentity() const { return mIdentity; }
    bool isModified() const {
        // A verified full-source list changes no pixels/ranges. Preserve the
        // existing Fast Recompress/direct-copy eligibility for that case while
        // keeping the explicit mapping intent available to persistence.
        return !mIdentity && (mSegments.isEmpty() || !mSourceFrameCountExact
            || mSegments.size() != 1 || mSegments.first().sourceStartFrame != 0
            || mSegments.first().frameCount != mSourceFrameCount || mSegments.first().masked);
    }

    const QList<VDQtTimelineSegment>& segments() const { return mSegments; }
    bool replaceSegments(const QList<VDQtTimelineSegment>& segments,
                         QString *errorMessage = nullptr,
                         bool clearHistory = true);

    qint64 mapOutputToSource(qint64 outputFrame) const;
    bool isOutputFrameMasked(qint64 outputFrame) const;
    qint64 mapSourceToOutput(qint64 sourceFrame,
                             qint64 outputHint = 0,
                             bool searchForward = true) const;
    QList<VDQtTimelineSegment> copyRange(qint64 startFrame,
                                         qint64 endFrameExclusive,
                                         QString *errorMessage = nullptr) const;
    bool deleteRange(qint64 startFrame,
                     qint64 endFrameExclusive,
                     QString *errorMessage = nullptr);
    bool cropToRange(qint64 startFrame,
                     qint64 endFrameExclusive,
                     QString *errorMessage = nullptr);
    bool insert(qint64 outputFrame,
                const QList<VDQtTimelineSegment>& segments,
                QString *errorMessage = nullptr);
    bool replaceRange(qint64 startFrame,
                      qint64 endFrameExclusive,
                      const QList<VDQtTimelineSegment>& segments,
                      QString *errorMessage = nullptr);
    bool resetEdits(QString *errorMessage = nullptr);
    bool clear(QString *errorMessage = nullptr);

    bool canUndo() const { return !mUndoStack.isEmpty(); }
    bool canRedo() const { return !mRedoStack.isEmpty(); }
    bool undo();
    bool redo();
    void clearHistory();

    static QList<VDQtTimelineSegment> normalized(
        const QList<VDQtTimelineSegment>& segments);

private:
    bool validateSegments(const QList<VDQtTimelineSegment>& segments,
                          QString *errorMessage) const;
    bool applyEdit(const QList<VDQtTimelineSegment>& segments,
                   QString *errorMessage, bool identity = false);
    QList<VDQtTimelineSegment> slice(qint64 startFrame,
                                     qint64 endFrameExclusive) const;

    qint64 mSourceFrameCount = 0;
    bool mSourceFrameCountExact = false;
    bool mIdentity = true;
    QList<VDQtTimelineSegment> mSegments;
    struct State {
        QList<VDQtTimelineSegment> segments;
        bool identity = true;
    };
    void restoreState(const State& state);
    QList<State> mUndoStack; // Segment snapshots and their mapping intent.
    QList<State> mRedoStack; // Cleared by a new edit, not by preview metadata.
};

#endif // VDQTTIMELINE_H
