#include "VDQtVideoDisplay.h"
#include <QStyleOption>
#include <QFontMetrics>
#include <QActionGroup>
#include <QApplication>
#include <algorithm>
#include <cmath>

VDVideoDisplayWidget::VDVideoDisplayWidget(const QString& title, QWidget *parent)
    : QWidget(parent),
      mTitle(title),
      mInfoText("No Video Loaded"),
      mZoomLevel(-1.0), // Auto size by default
      mAspectRatioMode(AspectRatioMode::PixelSource),
      mFilterMode(FilterMode::AnyAvailable),
      mDisplayMode(DisplayMode::Default),
      mIsDragging(false),
      mPanOffset(0, 0) {
    setMinimumSize(64, 48);
    setStyleSheet("background-color: #111115; border: 1px solid #33333d; border-radius: 4px;");
    setMouseTracking(true);
}

void VDVideoDisplayWidget::setFrameImage(const QImage& img) {
    mFrameImage = img;
    // Let Qt coalesce obsolete paints when playback or scrubbing produces
    // frames faster than the display can refresh.
    update();
}

void VDVideoDisplayWidget::setLabelText(const QString& text) {
    mInfoText = text;
    update();
}

void VDVideoDisplayWidget::clearDisplay() {
    mFrameImage = QImage();
    mInfoText = "No Video Loaded";
    mPanOffset = QPoint(0, 0);
    update();
}

void VDVideoDisplayWidget::setZoomLevel(double zoom) {
    mZoomLevel = zoom;
    if (mZoomLevel <= 0) {
        mPanOffset = QPoint(0, 0);
    }
    update();
}

void VDVideoDisplayWidget::setAspectRatioMode(AspectRatioMode mode) {
    mAspectRatioMode = mode;
    update();
}

QSize VDVideoDisplayWidget::calculateScaledSize() const {
    if (mFrameImage.isNull()) return QSize(0, 0);

    double srcW = mFrameImage.width();
    double srcH = mFrameImage.height();
    double par = 1.0;

    switch (mAspectRatioMode) {
    case AspectRatioMode::FreeAdjust:
        return size();
    case AspectRatioMode::PixelSource:
    case AspectRatioMode::PixelSquare:
        par = 1.0;
        break;
    case AspectRatioMode::PixelDV_NTSC:
        par = 10.0 / 11.0;
        break;
    case AspectRatioMode::PixelDV_PAL:
        par = 59.0 / 54.0;
        break;
    case AspectRatioMode::PixelDV_NTSCWide:
        par = 40.0 / 33.0;
        break;
    case AspectRatioMode::PixelSVCD_NTSC:
        par = 15.0 / 11.0;
        break;
    case AspectRatioMode::PixelDV_PALWide:
        par = 118.0 / 81.0;
        break;
    case AspectRatioMode::PixelSVCD_PAL:
        par = 59.0 / 36.0;
        break;
    case AspectRatioMode::PixelSVCD_NTSCWide:
        par = 20.0 / 11.0;
        break;
    case AspectRatioMode::PixelSVCD_PALWide:
        par = 59.0 / 27.0;
        break;
    case AspectRatioMode::FrameTV:
        par = (4.0 / 3.0) / (srcW / srcH);
        break;
    case AspectRatioMode::FrameDV:
        par = (15.0 / 11.0) / (srcW / srcH);
        break;
    case AspectRatioMode::FrameWide:
        par = (16.0 / 9.0) / (srcW / srcH);
        break;
    }

    double displayW = srcW * par;
    double displayH = srcH;

    if (mZoomLevel <= 0) { // Auto size: fit inside widget
        double widgetRatio = (double)width() / height();
        double displayRatio = displayW / displayH;

        if (widgetRatio > displayRatio) {
            int h = height();
            int w = static_cast<int>(h * displayRatio);
            return QSize(w, h);
        } else {
            int w = width();
            int h = static_cast<int>(w / displayRatio);
            return QSize(w, h);
        }
    } else { // Fixed zoom percentage
        int w = static_cast<int>(displayW * mZoomLevel);
        int h = static_cast<int>(displayH * mZoomLevel);
        return QSize(w, h);
    }
}

void VDVideoDisplayWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (mFilterMode != FilterMode::Point) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
    }

    // Draw background
    if (mDisplayMode == DisplayMode::TransparencyChecker) {
        p.fillRect(rect(), QColor(0x1a, 0x1a, 0x20));
        int checkSize = 16;
        for (int y = 0; y < height(); y += checkSize) {
            for (int x = 0; x < width(); x += checkSize) {
                if (((x / checkSize) + (y / checkSize)) % 2 == 0) {
                    p.fillRect(x, y, checkSize, checkSize, QColor(0x2a, 0x2a, 0x32));
                }
            }
        }
    } else if (mDisplayMode == DisplayMode::TransparencyBlack) {
        p.fillRect(rect(), Qt::black);
    } else if (mDisplayMode == DisplayMode::TransparencyGray) {
        p.fillRect(rect(), QColor(0x50, 0x50, 0x50));
    } else {
        p.fillRect(rect(), QColor(0x12, 0x12, 0x16));
    }

    if (!mFrameImage.isNull()) {
        QSize drawSize = calculateScaledSize();

        QImage renderImg = mFrameImage;
        if (mDisplayMode == DisplayMode::AlphaOnly && mFrameImage.hasAlphaChannel()) {
            renderImg = mFrameImage.convertToFormat(QImage::Format_Alpha8);
        }

        Qt::TransformationMode transformMode = (mFilterMode == FilterMode::Point) ? Qt::FastTransformation : Qt::SmoothTransformation;
        QImage scaled = renderImg.scaled(drawSize, Qt::IgnoreAspectRatio, transformMode);

        int x = (width() - scaled.width()) / 2 + mPanOffset.x();
        int y = (height() - scaled.height()) / 2 + mPanOffset.y();

        p.drawImage(x, y, scaled);
    } else {
        // Draw placeholder text / title
        p.setPen(QColor(0x70, 0x75, 0x88));
        QFont font = p.font();
        font.setPointSize(11);
        font.setBold(true);
        p.setFont(font);

        QString displayText = QString("%1\n(%2)").arg(mTitle, mInfoText);
        p.drawText(rect(), Qt::AlignCenter, displayText);
    }

    // Header badge overlay
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 160));
    p.drawRoundedRect(8, 8, 120, 24, 4, 4);

    p.setPen(QColor(0x00, 0xbc, 0xd4));
    QFont badgeFont = p.font();
    badgeFont.setPointSize(9);
    badgeFont.setBold(true);
    p.setFont(badgeFont);
    p.drawText(QRect(12, 8, 112, 24), Qt::AlignVCenter | Qt::AlignLeft, mTitle);
}

void VDVideoDisplayWidget::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1e1e24; color: #e0e0e0; border: 1px solid #3c3c46; padding: 4px; }"
        "QMenu::item { padding: 4px 20px 4px 20px; font-size: 11px; }"
        "QMenu::item:selected { background-color: #2b2b36; color: #00bcd4; }"
        "QMenu::separator { height: 1px; background: #3c3c46; margin: 4px 0px; }"
    );

    // -------------------------------------------------------------------------
    // ZOOM SUBMENU
    // -------------------------------------------------------------------------
    QMenu *zoomMenu = menu.addMenu("Zoom");
    QActionGroup *zoomGroup = new QActionGroup(this);

    struct ZoomOption { QString label; double ratio; };
    ZoomOption zoomOpts[] = {
        {"6%", 0.06}, {"12%", 0.12}, {"25%", 0.25}, {"33%", 0.33},
        {"50%", 0.50}, {"66%", 0.66}, {"75%", 0.75}, {"100%", 1.00},
        {"150%", 1.50}, {"200%", 2.00}, {"300%", 3.00}, {"400%", 4.00},
        {"Auto size", -1.0}
    };

    for (const auto& opt : zoomOpts) {
        QAction *act = zoomMenu->addAction(opt.label);
        act->setCheckable(true);
        act->setActionGroup(zoomGroup);
        if (std::abs(mZoomLevel - opt.ratio) < 0.01) {
            act->setChecked(true);
        }
        connect(act, &QAction::triggered, [this, r = opt.ratio]() {
            setZoomLevel(r);
        });
    }

    zoomMenu->addSeparator();
    QAction *actResetZoom = zoomMenu->addAction("Reset to exact size");
    connect(actResetZoom, &QAction::triggered, [this]() {
        setZoomLevel(1.00);
        mPanOffset = QPoint(0, 0);
    });

    // -------------------------------------------------------------------------
    // ASPECT RATIO SUBMENU
    // -------------------------------------------------------------------------
    QMenu *arMenu = menu.addMenu("Aspect Ratio");
    QActionGroup *arGroup = new QActionGroup(this);

    struct AROption { QString label; AspectRatioMode mode; };
    AROption arOpts[] = {
        {"Free adjust", AspectRatioMode::FreeAdjust},
        {"1:1 pixel (Source)", AspectRatioMode::PixelSource},
        {"10:11 pixel (DV-NTSC)", AspectRatioMode::PixelDV_NTSC},
        {"1:1 pixel (Square)", AspectRatioMode::PixelSquare},
        {"59:54 pixel (DV-PAL)", AspectRatioMode::PixelDV_PAL},
        {"40:33 pixel (DV-NTSC Wide)", AspectRatioMode::PixelDV_NTSCWide},
        {"15:11 pixel (SVCD-NTSC)", AspectRatioMode::PixelSVCD_NTSC},
        {"118:81 pixel (DV-PAL Wide)", AspectRatioMode::PixelDV_PALWide},
        {"59:36 pixel (SVCD-PAL)", AspectRatioMode::PixelSVCD_PAL},
        {"20:11 pixel (SVCD-NTSC Wide)", AspectRatioMode::PixelSVCD_NTSCWide},
        {"59:27 pixel (SVCD-PAL Wide)", AspectRatioMode::PixelSVCD_PALWide},
        {"4:3 frame (TV)", AspectRatioMode::FrameTV},
        {"15:11 frame (DV)", AspectRatioMode::FrameDV},
        {"16:9 frame (Wide)", AspectRatioMode::FrameWide}
    };

    for (const auto& opt : arOpts) {
        QAction *act = arMenu->addAction(opt.label);
        act->setCheckable(true);
        act->setActionGroup(arGroup);
        if (mAspectRatioMode == opt.mode) {
            act->setChecked(true);
        }
        connect(act, &QAction::triggered, [this, m = opt.mode]() {
            setAspectRatioMode(m);
        });
    }

    // -------------------------------------------------------------------------
    // PREFERRED FILTER SUBMENU
    // -------------------------------------------------------------------------
    QMenu *filterMenu = menu.addMenu("Preferred filter");
    QActionGroup *filterGroup = new QActionGroup(this);

    struct FilterOption { QString label; FilterMode mode; };
    FilterOption filterOpts[] = {
        {"Point", FilterMode::Point},
        {"Bilinear", FilterMode::Bilinear},
        {"Bicubic", FilterMode::Bicubic},
        {"Any available", FilterMode::AnyAvailable}
    };

    for (const auto& opt : filterOpts) {
        QAction *act = filterMenu->addAction(opt.label);
        act->setCheckable(true);
        act->setActionGroup(filterGroup);
        if (mFilterMode == opt.mode) {
            act->setChecked(true);
        }
        connect(act, &QAction::triggered, [this, m = opt.mode]() {
            mFilterMode = m;
            update();
        });
    }

    // -------------------------------------------------------------------------
    // DISPLAY MODE SUBMENU
    // -------------------------------------------------------------------------
    QMenu *dispMenu = menu.addMenu("Display mode");
    QActionGroup *dispGroup = new QActionGroup(this);

    struct DispOption { QString label; DisplayMode mode; };
    DispOption dispOpts[] = {
        {"Default", DisplayMode::Default},
        {"Color only", DisplayMode::ColorOnly},
        {"Alpha only", DisplayMode::AlphaOnly},
        {"Transparency (checker)", DisplayMode::TransparencyChecker},
        {"Transparency (black)", DisplayMode::TransparencyBlack},
        {"Transparency (gray)", DisplayMode::TransparencyGray}
    };

    for (const auto& opt : dispOpts) {
        QAction *act = dispMenu->addAction(opt.label);
        act->setCheckable(true);
        act->setActionGroup(dispGroup);
        if (mDisplayMode == opt.mode) {
            act->setChecked(true);
        }
        connect(act, &QAction::triggered, [this, m = opt.mode]() {
            mDisplayMode = m;
            update();
        });
    }

    menu.exec(event->globalPos());
}

void VDVideoDisplayWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && mZoomLevel > 0) {
        mIsDragging = true;
        mLastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void VDVideoDisplayWidget::mouseMoveEvent(QMouseEvent *event) {
    if (mIsDragging && mZoomLevel > 0) {
        QPoint delta = event->pos() - mLastMousePos;
        mLastMousePos = event->pos();
        mPanOffset += delta;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void VDVideoDisplayWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && mIsDragging) {
        mIsDragging = false;
        unsetCursor();
    }
    QWidget::mouseReleaseEvent(event);
}

void VDVideoDisplayWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    update();
}
