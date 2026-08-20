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

void VDTimelineSlider::setMarkers(const QList<qint64>& markers) {
    if (mMarkers == markers) return;
    mMarkers = markers;
    update();
}

void VDTimelineSlider::paintEvent(QPaintEvent *) {
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

    if (!mMarkers.isEmpty() && maximum() > minimum()) {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(255, 176, 48), 1));
        p.setBrush(QColor(255, 176, 48));
        const double range = maximum() - minimum();
        for (qint64 marker : mMarkers) {
            if (marker < minimum() || marker > maximum()) continue;
            const int x = trackLeft + static_cast<int>(std::round(
                (marker - minimum()) * trackWidth / range));
            QPolygon triangle;
            triangle << QPoint(x, grooveRect.top() - 7)
                     << QPoint(x - 4, grooveRect.top() - 13)
                     << QPoint(x + 4, grooveRect.top() - 13);
            p.drawPolygon(triangle);
            p.drawLine(x, grooveRect.top() - 7, x, grooveRect.bottom() + 5);
        }
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

    btnStart       = createBtn("|<",  "Jump to Start (Home)", VDQT_PCN_START);
    btnPrevKey     = createBtn("<<K", "Previous Keyframe (Shift+Left)", VDQT_PCN_KEYPREV);
    btnPrev        = createBtn("<",   "Step Backward (Left)", VDQT_PCN_BACKWARD);
    btnStop        = createBtn("■",   "Stop (Esc)", VDQT_PCN_STOP);
    btnPlay        = createBtn("▶",   "Play Input (Enter)", VDQT_PCN_PLAY);
    btnPlayPreview = createBtn("▶|",  "Play Preview (F7)", VDQT_PCN_PLAYPREVIEW);
    btnNext        = createBtn(">",   "Step Forward (Right)", VDQT_PCN_FORWARD);
    btnNextKey     = createBtn("K>>", "Next Keyframe (Shift+Right)", VDQT_PCN_KEYNEXT);
    btnEnd         = createBtn(">|",  "Jump to End (End)", VDQT_PCN_END);
    btnMarkIn      = createBtn("[",   "Set Selection Start ([)", VDQT_PCN_MARKIN);
    btnMarkOut     = createBtn("]",   "Set Selection End (])", VDQT_PCN_MARKOUT);

    controlLayout->addSpacing(12);

    mStatusLabel = new QLabel("Frame 0 (0:00:00.000)", this);
    mStatusLabel->setStyleSheet("color: #00bcd4; font-family: monospace; font-size: 12px; font-weight: bold; padding: 2px 8px; background: #1a1a22; border-radius: 4px;");
    controlLayout->addWidget(mStatusLabel, 1);

    mainLayout->addLayout(controlLayout);

    mScrubTimer.setSingleShot(true);
    connect(&mScrubTimer, &QTimer::timeout,
            this, &VDQtPositionControlWidget::DispatchPendingScrub);
    connect(mSlider, &QSlider::sliderPressed, this, [this]() {
        Q_EMIT userScrubStarted();
    });
    connect(mSlider, &QSlider::sliderReleased, this, [this]() {
        if (mPendingScrubPos >= 0) {
            mScrubTimer.stop();
            DispatchPendingScrub();
        }
    });
}

void VDQtPositionControlWidget::SetRange(qint64 lo, qint64 hi, bool updateNow) {
    mRangeLo = lo;
    mRangeHi = hi;
    if (mZoomEnabled) {
        mZoomStart = std::clamp(mZoomStart, lo, hi);
        mZoomEnd = std::clamp(mZoomEnd, mZoomStart, hi);
        if (mZoomStart >= mZoomEnd) mZoomEnabled = false;
    }
    mSlider->setRange(
        static_cast<int>(mZoomEnabled ? mZoomStart : lo),
        static_cast<int>(mZoomEnabled ? mZoomEnd : hi));
    if (updateNow) UpdateStatusText();
}

void VDQtPositionControlWidget::SetZoomRange(qint64 start, qint64 end) {
    start = std::clamp(start, mRangeLo, mRangeHi);
    end = std::clamp(end, mRangeLo, mRangeHi);
    if (start >= end) {
        ClearZoomRange();
        return;
    }
    mZoomEnabled = true;
    mZoomStart = start;
    mZoomEnd = end;
    mSlider->setRange(static_cast<int>(start), static_cast<int>(end));
    SetPosition(std::clamp(mPosition, start, end));
}

void VDQtPositionControlWidget::ClearZoomRange() {
    if (!mZoomEnabled) return;
    mZoomEnabled = false;
    mZoomStart = mRangeLo;
    mZoomEnd = mRangeHi;
    mSlider->setRange(static_cast<int>(mRangeLo), static_cast<int>(mRangeHi));
    SetPosition(std::clamp(mPosition, mRangeLo, mRangeHi));
}

void VDQtPositionControlWidget::SetPosition(qint64 pos) {
    if (mPosition != pos) {
        // Programmatic movement (playback, stepping, navigation) is already
        // committed by the explicit positionChanged() emission below. Do not
        // let QSlider::valueChanged route it through the user-scrub debounce a
        // second time.
        mScrubTimer.stop();
        mPendingScrubPos = -1;
        mPosition = pos;
        QSignalBlocker blocker(mSlider);
        mSlider->setValue((int)pos);
        UpdateStatusText();
        Q_EMIT positionChanged((int)mPosition);
    }
}

void VDQtPositionControlWidget::SetPositionSilent(qint64 pos) {
    if (mPosition != pos) {
        mPosition = pos;
        QSignalBlocker blocker(mSlider);
        mSlider->setValue((int)pos);
        UpdateStatusText();
    }
}

void VDQtPositionControlWidget::SetSelection(qint64 start, qint64 end, bool updateNow) {
    start = std::clamp(start, mRangeLo, mRangeHi + 1);
    end = std::clamp(end, mRangeLo, mRangeHi + 1);
    if (start > end) std::swap(start, end);
    const bool changed = start != mSelStart || end != mSelEnd;
    mSelStart = start;
    mSelEnd = end;
    mSlider->setSelection((int)start, (int)end);
    if (updateNow) UpdateStatusText();
    if (changed) Q_EMIT selectionChanged(mSelStart, mSelEnd);
}

void VDQtPositionControlWidget::onSliderValueChanged(int value) {
    mPosition = value;
    UpdateStatusText(); // Real-time status text update (0ms latency!)
    mPendingScrubPos = value;

    if (!mScrubTimer.isActive()) {
        mScrubTimer.start(16); // Coalesce user dragging to at most ~60 dispatches/sec.
    }
}

void VDQtPositionControlWidget::DispatchPendingScrub() {
    if (mPendingScrubPos < 0)
        return;

    const int position = mPendingScrubPos;
    mPendingScrubPos = -1;
    Q_EMIT positionChanged(position);
}

void VDQtPositionControlWidget::onTransportButtonClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    int actionCode = btn->property("actionCode").toInt();

    Q_EMIT transportActionTriggered(actionCode);
}

void VDQtPositionControlWidget::UpdateStatusText() {
    double fps = mFrameRate;
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
