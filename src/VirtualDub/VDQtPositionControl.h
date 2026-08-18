#ifndef VDQTPOSITIONCONTROL_H
#define VDQTPOSITIONCONTROL_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <algorithm>
#include "PositionControl.h"
#include <vd2/system/Fraction.h>

class VDTimelineSlider : public QSlider {
    Q_OBJECT
public:
    explicit VDTimelineSlider(Qt::Orientation orientation, QWidget *parent = nullptr);
    void setSelection(int start, int end);

protected:
    void paintEvent(QPaintEvent *ev) override;

private:
    int mSelStart = 0;
    int mSelEnd = 0;
};

class VDQtPositionControlWidget : public QWidget, public IVDPositionControl {
    Q_OBJECT
public:
    explicit VDQtPositionControlWidget(QWidget *parent = nullptr);
    virtual ~VDQtPositionControlWidget();

    // IVDPositionControl implementation
    int GetNiceHeight() override { return 70; }
    void SetFrameTypeCallback(IVDPositionControlCallback *pCB) override { mpCB = pCB; }
    void SetRange(VDPosition lo, VDPosition hi, bool updateNow = true) override;
    void SetRangeZoom(bool v, bool updateNow = true) override { (void)v; (void)updateNow; }
    VDPosition GetRangeBegin() override { return mRangeLo; }
    VDPosition GetRangeEnd() override { return mRangeHi; }
    VDPosition GetPosition() override { return mPosition; }
    void SetPosition(VDPosition pos) override;
    void SetPositionSilent(VDPosition pos);
    void SetCurrentFrameKey(bool keyFrame) { mCurrentFrameIsKey = keyFrame; UpdateStatusText(); }
    void SetDisplayedPosition(VDPosition pos) override { SetPosition(pos); }
    bool GetSelection(VDPosition& start, VDPosition& end) override { start = mSelStart; end = mSelEnd; return mSelStart < mSelEnd; }
    VDPosition GetSelectionStart() const { return mSelStart; }
    VDPosition GetSelectionEnd() const { return mSelEnd; }
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
    void SetSelection(VDPosition start, VDPosition end, bool updateNow = true) override;
    bool GetSelection2(VDPosition& start, VDPosition& end) override { return GetSelection(start, end); }
    void SetSelection2(VDPosition start, VDPosition end, bool updateNow = true) override { SetSelection(start, end, updateNow); }
    void SetTimeline(VDTimeline& t) override { (void)t; }
    void SetFrameRate(const VDFraction& frameRate) override { mFrameRate = frameRate; UpdateStatusText(); }
    void SetAutoPositionUpdate(bool autoUpdate) override { (void)autoUpdate; }
    void SetAutoStep(bool autoStep) override { (void)autoStep; }
    void ResetShuttle() override {}
    VDEvent<IVDPositionControl, VDPositionControlEventData>& PositionUpdated() override { return mEventPositionUpdated; }
    void SetMessage(const wchar_t* s) override;

    // IVDRefCount implementation
    int AddRef() override { return ++mRefCount; }
    int Release() override { int r = --mRefCount; if (r <= 0) delete this; return r; }

Q_SIGNALS:
    void positionChanged(int frame);
    void transportActionTriggered(int actionCode);
    void userScrubStarted();

private Q_SLOTS:
    void onSliderValueChanged(int value);
    void onTransportButtonClicked();

private:
    void DispatchPendingScrub();
    void UpdateStatusText();
    void NotifyEvent(VDPositionControlEventData::EventType type, VDPosition pos);

    VDPosition mRangeLo = 0;
    VDPosition mRangeHi = 1000;
    VDPosition mPosition = 0;
    VDPosition mSelStart = 0;
    VDPosition mSelEnd = 0;
    VDFraction mFrameRate = VDFraction(30000, 1001);
    IVDPositionControlCallback *mpCB = nullptr;
    VDEvent<IVDPositionControl, VDPositionControlEventData> mEventPositionUpdated;
    int mRefCount = 1;

    VDTimelineSlider *mSlider;
    QLabel *mStatusLabel;
    QTimer mScrubTimer;
    int mPendingScrubPos = -1;
    bool mCurrentFrameIsKey = false;

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
