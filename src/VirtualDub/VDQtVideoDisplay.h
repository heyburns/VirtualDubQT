#ifndef VDQTVIDEODISPLAY_H
#define VDQTVIDEODISPLAY_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QMenu>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPoint>

class VDVideoDisplayWidget : public QWidget {
    Q_OBJECT
public:
    enum class AspectRatioMode {
        FreeAdjust,
        PixelSource,        // 1:1 pixel (Source)
        PixelDV_NTSC,       // 10:11
        PixelSquare,        // 1:1 pixel (Square)
        PixelDV_PAL,        // 59:54
        PixelDV_NTSCWide,   // 40:33
        PixelSVCD_NTSC,     // 15:11
        PixelDV_PALWide,    // 118:81
        PixelSVCD_PAL,      // 59:36
        PixelSVCD_NTSCWide, // 20:11
        PixelSVCD_PALWide,  // 59:27
        FrameTV,            // 4:3 frame (TV)
        FrameDV,            // 15:11 frame (DV)
        FrameWide           // 16:9 frame (Wide)
    };

    enum class FilterMode {
        Point,
        Bilinear,
        Bicubic,
        AnyAvailable
    };

    enum class DisplayMode {
        Default,
        ColorOnly,
        AlphaOnly,
        TransparencyChecker,
        TransparencyBlack,
        TransparencyGray
    };

    explicit VDVideoDisplayWidget(const QString& title, QWidget *parent = nullptr);

    void setFrameImage(const QImage& img);
    void setLabelText(const QString& text);
    void clearDisplay();

    void setZoomLevel(double zoom); // -1.0 for Auto Size
    double zoomLevel() const { return mZoomLevel; }

    void setAspectRatioMode(AspectRatioMode mode);
    AspectRatioMode aspectRatioMode() const { return mAspectRatioMode; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void buildContextMenu();
    QSize calculateScaledSize() const;

    QString mTitle;
    QString mInfoText;
    QImage mFrameImage;

    double mZoomLevel; // -1.0 = Auto Size
    AspectRatioMode mAspectRatioMode;
    FilterMode mFilterMode;
    DisplayMode mDisplayMode;

    bool mIsDragging;
    QPoint mLastMousePos;
    QPoint mPanOffset;
};

#endif // VDQTVIDEODISPLAY_H
