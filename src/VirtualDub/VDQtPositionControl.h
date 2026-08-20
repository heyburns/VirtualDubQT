#ifndef VDQTPOSITIONCONTROL_H
#define VDQTPOSITIONCONTROL_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QList>
#include <algorithm>

enum VDQtTransportAction {
    VDQT_PCN_STOP = 0,
    VDQT_PCN_PLAY = 1,
    VDQT_PCN_MARKIN = 2,
    VDQT_PCN_MARKOUT = 3,
    VDQT_PCN_START = 4,
    VDQT_PCN_BACKWARD = 5,
    VDQT_PCN_FORWARD = 6,
    VDQT_PCN_END = 7,
    VDQT_PCN_KEYPREV = 8,
    VDQT_PCN_KEYNEXT = 9,
    VDQT_PCN_PLAYPREVIEW = 10
};

class VDTimelineSlider : public QSlider {
    Q_OBJECT
public:
    explicit VDTimelineSlider(Qt::Orientation orientation, QWidget *parent = nullptr);
    void setSelection(int start, int end);
    void setMarkers(const QList<qint64>& markers);

protected:
    void paintEvent(QPaintEvent *ev) override;

private:
    int mSelStart = 0;
    int mSelEnd = 0;
    QList<qint64> mMarkers;
};

class VDQtPositionControlWidget : public QWidget {
    Q_OBJECT
public:
    explicit VDQtPositionControlWidget(QWidget *parent = nullptr);
    ~VDQtPositionControlWidget() override = default;

    void SetRange(qint64 lo, qint64 hi, bool updateNow = true);
    qint64 GetRangeBegin() const { return mRangeLo; }
    qint64 GetRangeEnd() const { return mRangeHi; }
    qint64 GetPosition() const { return mPosition; }
    void SetPosition(qint64 pos);
    void SetPositionSilent(qint64 pos);
    void SetCurrentFrameKey(bool keyFrame) { mCurrentFrameIsKey = keyFrame; UpdateStatusText(); }
    bool GetSelection(qint64& start, qint64& end) const { start = mSelStart; end = mSelEnd; return mSelStart < mSelEnd; }
    qint64 GetSelectionStart() const { return mSelStart; }
    qint64 GetSelectionEnd() const { return mSelEnd; }
    bool hasSelection() const { return mSelStart < mSelEnd; }
    int getEffectiveStartFrame(int totalFrames) const {
        if (totalFrames <= 0) return 0;
        if (mSelStart < mSelEnd && mSelStart >= 0) {
            return std::clamp(static_cast<int>(mSelStart), 0, totalFrames - 1);
        }
        return 0;
    }
    int getEffectiveEndFrame(int totalFrames) const {
        if (totalFrames <= 0) return 0;
        if (mSelStart < mSelEnd && mSelEnd > 0) {
            // VirtualDub selection end markers are exclusive.
            return std::min(static_cast<int>(mSelEnd - 1), totalFrames - 1);
        }
        return std::max(0, totalFrames - 1);
    }
    void SetSelection(qint64 start, qint64 end, bool updateNow = true);
    void SetFrameRate(double frameRate) { mFrameRate = frameRate; UpdateStatusText(); }
    void SetMarkers(const QList<qint64>& markers) { mSlider->setMarkers(markers); }
    void SetZoomRange(qint64 start, qint64 end);
    void ClearZoomRange();
    bool HasZoomRange() const { return mZoomEnabled; }
    qint64 GetZoomStart() const { return mZoomStart; }
    qint64 GetZoomEnd() const { return mZoomEnd; }

Q_SIGNALS:
    void positionChanged(int frame);
    void selectionChanged(qint64 startFrame, qint64 endFrameExclusive);
    void transportActionTriggered(int actionCode);
    void userScrubStarted();

private Q_SLOTS:
    void onSliderValueChanged(int value);
    void onTransportButtonClicked();

private:
    void DispatchPendingScrub();
    void UpdateStatusText();

    qint64 mRangeLo = 0;
    qint64 mRangeHi = 1000;
    qint64 mPosition = 0;
    qint64 mSelStart = 0;
    qint64 mSelEnd = 0;
    double mFrameRate = 30000.0 / 1001.0;

    VDTimelineSlider *mSlider;
    QLabel *mStatusLabel;
    QTimer mScrubTimer;
    int mPendingScrubPos = -1;
    bool mCurrentFrameIsKey = false;
    bool mZoomEnabled = false;
    qint64 mZoomStart = 0;
    qint64 mZoomEnd = 0;

    QPushButton *btnStart;
    QPushButton *btnPrevKey;
    QPushButton *btnPrev;
    QPushButton *btnStop;
    QPushButton *btnPlay;
    QPushButton *btnPlayPreview;
    QPushButton *btnNext;
    QPushButton *btnNextKey;
    QPushButton *btnEnd;
    QPushButton *btnMarkIn;
    QPushButton *btnMarkOut;
};

#endif // VDQTPOSITIONCONTROL_H
