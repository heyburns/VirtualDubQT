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

class VDQtTimeline {
public:
    static constexpr int kMaximumHistoryEntries = 100;

    void reset(qint64 sourceFrameCount, bool exactFrameCount);
    void setSourceFrameCount(qint64 sourceFrameCount, bool exactFrameCount);

    qint64 sourceFrameCount() const { return mSourceFrameCount; }
    bool sourceFrameCountExact() const { return mSourceFrameCountExact; }
    qint64 frameCount() const;
    bool isEmpty() const { return mSegments.isEmpty(); }
    bool isIdentity() const;
    bool isModified() const { return !isIdentity(); }

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
                   QString *errorMessage);
    QList<VDQtTimelineSegment> slice(qint64 startFrame,
                                     qint64 endFrameExclusive) const;

    qint64 mSourceFrameCount = 0;
    bool mSourceFrameCountExact = false;
    QList<VDQtTimelineSegment> mSegments;
    QList<QList<VDQtTimelineSegment>> mUndoStack;
    QList<QList<VDQtTimelineSegment>> mRedoStack;
};

#endif // VDQTTIMELINE_H
