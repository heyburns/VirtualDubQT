#include "VDQtPositionControl.h"
#include <QStylePainter>
#include <QStyleOptionSlider>
#include <cstdio>

VDTimelineSlider::VDTimelineSlider(Qt::Orientation orientation, QWidget *parent)
    : QSlider(orientation, parent), mSelStart(0), mSelEnd(0) {
    setMouseTracking(true);
}

void VDTimelineSlider::setSelection(int start, int end) {
    if (mSelStart != start || mSelEnd != end) {
        mSelStart = start;
        mSelEnd = end;
        update();
    }
}

void VDTimelineSlider::paintEvent(QPaintEvent *ev) {
    QStylePainter p(this);
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    // 1. Draw standard background groove
    opt.subControls = QStyle::SC_SliderGroove;
    p.drawComplexControl(QStyle::CC_Slider, opt);

    // 2. Draw Selection Range Highlight along the groove (VirtualDub Timeline Selection)
    QRect grooveRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
    int handleHalfWidth = handleRect.width() / 2;
    int trackLeft = grooveRect.left() + handleHalfWidth;
    int trackRight = grooveRect.right() - handleHalfWidth;
    int trackWidth = std::max(1, trackRight - trackLeft);

    if (mSelStart < mSelEnd && mSelEnd > 0 && maximum() > minimum()) {
        double range = maximum() - minimum();
        int x1 = trackLeft + static_cast<int>(std::round((mSelStart - minimum()) * trackWidth / range));
        int x2 = trackLeft + static_cast<int>(std::round((mSelEnd - minimum()) * trackWidth / range));
        x1 = std::clamp(x1, grooveRect.left(), grooveRect.right());
        x2 = std::clamp(x2, grooveRect.left(), grooveRect.right());

        int selW = std::max(3, x2 - x1);
        QRect selRect(x1, grooveRect.top() - 1, selW, grooveRect.height() + 2);

        p.setRenderHint(QPainter::Antialiasing, true);

        // Highlight selection band (Bright Cyan gradient matching VirtualDub theme)
        QLinearGradient grad(selRect.topLeft(), selRect.bottomLeft());
        grad.setColorAt(0.0, QColor(0, 240, 255, 230));
        grad.setColorAt(1.0, QColor(0, 160, 230, 250));
        p.setBrush(grad);
        p.setPen(QPen(QColor(0, 255, 255), 1));
        p.drawRoundedRect(selRect, 3, 3);

        // Selection In/Out Tick Endpoints (VirtualDub timeline marker tabs)
        p.setPen(QPen(QColor(255, 255, 255), 2));
        p.drawLine(x1, grooveRect.top() - 4, x1, grooveRect.bottom() + 4);
        p.drawLine(x2, grooveRect.top() - 4, x2, grooveRect.bottom() + 4);
    }

    // 3. Draw Handle (Playhead thumb)
    opt.subControls = QStyle::SC_SliderHandle;
    p.drawComplexControl(QStyle::CC_Slider, opt);
}

VDQtPositionControlWidget::VDQtPositionControlWidget(QWidget *parent)
    : QWidget(parent) {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 4, 6, 4);
    mainLayout->setSpacing(4);

    // Trackbar slider with custom selection highlighting
    mSlider = new VDTimelineSlider(Qt::Horizontal, this);
    mSlider->setRange(0, 1000);
    mSlider->setValue(0);
    mSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 8px; background: #22222c; border: 1px solid #363646; border-radius: 4px; }"
        "QSlider::handle:horizontal { background: #ffffff; border: 2px solid #00bcd4; width: 16px; margin-top: -5px; margin-bottom: -5px; border-radius: 8px; }"
        "QSlider::handle:horizontal:hover { background: #00e5ff; border: 2px solid #ffffff; }"
    );
    connect(mSlider, &QSlider::valueChanged, this, &VDQtPositionControlWidget::onSliderValueChanged);
    mainLayout->addWidget(mSlider);

    // Buttons and Status Layout
    QHBoxLayout *controlLayout = new QHBoxLayout();
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(4);

    auto createBtn = [this, controlLayout](const QString& text, const QString& tooltip, int actionCode) -> QPushButton* {
        QPushButton *btn = new QPushButton(text, this);
        btn->setToolTip(tooltip);
        btn->setFixedSize(32, 26);
        btn->setProperty("actionCode", actionCode);
        btn->setStyleSheet(
            "QPushButton { background-color: #242430; color: #e0e0e0; border: 1px solid #383848; border-radius: 4px; font-weight: bold; }"
            "QPushButton:hover { background-color: #323244; border-color: #00bcd4; }"
            "QPushButton:pressed { background-color: #00bcd4; color: #121216; }"
        );
        connect(btn, &QPushButton::clicked, this, &VDQtPositionControlWidget::onTransportButtonClicked);
        controlLayout->addWidget(btn);
        return btn;
    };

    btnStart       = createBtn("|<",  "Jump to Start (Home)", PCN_START);
    btnPrevKey     = createBtn("<<K", "Previous Keyframe (Shift+Left)", PCN_KEYPREV);
    btnPrev        = createBtn("<",   "Step Backward (Left)", PCN_BACKWARD);
    btnStop        = createBtn("■",   "Stop (Esc)", PCN_STOP);
    btnPlay        = createBtn("▶",   "Play Input (Enter)", PCN_PLAY);
    btnPlayPreview = createBtn("▶|",  "Play Preview (F7)", PCN_PLAYPREVIEW);
    btnNext        = createBtn(">",   "Step Forward (Right)", PCN_FORWARD);
    btnNextKey     = createBtn("K>>", "Next Keyframe (Shift+Right)", PCN_KEYNEXT);
    btnEnd         = createBtn(">|",  "Jump to End (End)", PCN_END);
    btnMarkIn      = createBtn("[",   "Set Selection Start ([)", PCN_MARKIN);
    btnMarkOut     = createBtn("]",   "Set Selection End (])", PCN_MARKOUT);

    controlLayout->addSpacing(12);

    mStatusLabel = new QLabel("Frame 0 (0:00:00.000)", this);
    mStatusLabel->setStyleSheet("color: #00bcd4; font-family: monospace; font-size: 12px; font-weight: bold; padding: 2px 8px; background: #1a1a22; border-radius: 4px;");
    controlLayout->addWidget(mStatusLabel, 1);

    mainLayout->addLayout(controlLayout);

    mScrubTimer.setSingleShot(true);
    connect(&mScrubTimer, &QTimer::timeout, this, [this]() {
        if (mPendingScrubPos >= 0) {
            NotifyEvent(VDPositionControlEventData::kEventJump, mPendingScrubPos);
            Q_EMIT positionChanged(mPendingScrubPos);
            mPendingScrubPos = -1;
        }
    });
}

VDQtPositionControlWidget::~VDQtPositionControlWidget() {
}

void VDQtPositionControlWidget::SetRange(VDPosition lo, VDPosition hi, bool updateNow) {
    mRangeLo = lo;
    mRangeHi = hi;
    mSlider->setRange((int)lo, (int)hi);
    if (updateNow) UpdateStatusText();
}

void VDQtPositionControlWidget::SetPosition(VDPosition pos) {
    if (mPosition != pos) {
        mPosition = pos;
        mSlider->setValue((int)pos);
        UpdateStatusText();
        Q_EMIT positionChanged((int)mPosition);
    }
}

void VDQtPositionControlWidget::SetPositionSilent(VDPosition pos) {
    if (mPosition != pos) {
        mPosition = pos;
        QSignalBlocker blocker(mSlider);
        mSlider->setValue((int)pos);
        UpdateStatusText();
    }
}

void VDQtPositionControlWidget::SetSelection(VDPosition start, VDPosition end, bool updateNow) {
    mSelStart = start;
    mSelEnd = end;
    mSlider->setSelection((int)start, (int)end);
    if (updateNow) UpdateStatusText();
}

void VDQtPositionControlWidget::SetMessage(const wchar_t* s) {
    if (s) {
        mStatusLabel->setText(QString::fromWCharArray(s));
    }
}

void VDQtPositionControlWidget::onSliderValueChanged(int value) {
    mPosition = value;
    UpdateStatusText(); // Real-time status text update (0ms latency!)
    mPendingScrubPos = value;

    if (!mScrubTimer.isActive()) {
        mScrubTimer.start(8); // Limit scrubbing dispatch rate to 120 FPS
    }
}

void VDQtPositionControlWidget::onTransportButtonClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    int actionCode = btn->property("actionCode").toInt();

    VDPositionControlEventData::EventType ev = VDPositionControlEventData::kEventNone;
    switch (actionCode) {
        case PCN_START:       ev = VDPositionControlEventData::kEventJumpToStart; break;
        case PCN_END:         ev = VDPositionControlEventData::kEventJumpToEnd; break;
        case PCN_KEYPREV:     ev = VDPositionControlEventData::kEventJumpToPrevKey; break;
        case PCN_KEYNEXT:     ev = VDPositionControlEventData::kEventJumpToNextKey; break;
        case PCN_BACKWARD:    ev = VDPositionControlEventData::kEventJumpToPrev; break;
        case PCN_FORWARD:     ev = VDPositionControlEventData::kEventJumpToNext; break;
        case PCN_MARKIN:      mSelStart = mPosition; break;
        case PCN_MARKOUT:     mSelEnd = mPosition; break;
    }

    NotifyEvent(ev, mPosition);
    Q_EMIT transportActionTriggered(actionCode);
}

void VDQtPositionControlWidget::NotifyEvent(VDPositionControlEventData::EventType type, VDPosition pos) {
    VDPositionControlEventData data;
    data.mEventType = type;
    data.mPosition = pos;
    mEventPositionUpdated.Raise(this, data);
}

void VDQtPositionControlWidget::UpdateStatusText() {
    double fps = mFrameRate.asDouble();
    if (fps <= 0.0) fps = 29.97;
    double seconds = (fps > 0) ? ((double)mPosition / fps) : 0.0;
    int hrs = (int)(seconds / 3600.0);
    int mins = (int)((seconds - hrs * 3600.0) / 60.0);
    double secs = seconds - hrs * 3600.0 - mins * 60.0;

    QString status = QString("Frame %1 (%2:%3:%4)%5")
                         .arg(mPosition)
                         .arg(hrs, 2, 10, QChar('0'))
                         .arg(mins, 2, 10, QChar('0'))
                         .arg(secs, 0, 'f', 3)
                         .arg(mCurrentFrameIsKey ? QStringLiteral(" [K]") : QString());

    if (mSelStart != mSelEnd) {
        status += QString(" | Selection: %1 - %2 (%3 frames)").arg(mSelStart).arg(mSelEnd).arg(mSelEnd - mSelStart);
    }

    mStatusLabel->setText(status);
}
