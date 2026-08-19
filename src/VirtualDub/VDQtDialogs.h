#ifndef VDQTDIALOGS_H
#define VDQTDIALOGS_H

#include <QDialog>
#include <QTableWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>

#include "VDQtFilterSystem.h"

// Video Filters Manager Dialog
class VDVideoFiltersDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDVideoFiltersDialog(int sourceWidth, int sourceHeight, const QImage &sourceFrame = QImage(), QWidget *parent = nullptr);

private Q_SLOTS:
    void onItemChanged(QTableWidgetItem *item);
    void onAddClicked();
    void onDeleteClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onConfigureClicked();

private:
    void refreshFilterTable();

private:
    int mSourceWidth;
    int mSourceHeight;
    QImage mSourceFrame;

    QTableWidget *mFilterTable;
    QPushButton *btnAdd;
    QPushButton *btnDelete;
    QPushButton *btnMoveUp;
    QPushButton *btnMoveDown;
    QPushButton *btnConfigure;
};

// Filter Preview Floating Dialog
class VDFilterPreviewDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDFilterPreviewDialog(QWidget *parent = nullptr);
    void updatePreviewImage(const QImage &image);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void refreshDisplay();

    QLabel *mPreviewLabel;
    QLabel *mInfoLabel;
    QImage mCurrentImage;
};

// Filter: Resize Configuration Dialog
class VDResizeFilterDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDResizeFilterDialog(const QMap<QString, double>& params, int sourceW, int sourceH, QWidget *parent = nullptr);
    QMap<QString, double> getParams() const;

private Q_SLOTS:
    void onSizeOptionChanged();
    void onAspectOptionChanged();
    void onFramingOptionChanged();
    void onPickColorClicked();
    void updateCalculatedDimensions();
    void onRelWChanged(int val);
    void onRelHChanged(int val);
    void onAbsWChanged(int val);
    void onAbsHChanged(int val);

private:
    int mSourceW;
    int mSourceH;
    bool mUpdating;
    QColor mFillColor;

    // Size options
    QRadioButton *radAbsolute;
    QRadioButton *radRelative;
    QSpinBox *spinAbsW;
    QSpinBox *spinAbsH;
    QSpinBox *spinRelW;
    QSpinBox *spinRelH;

    // Aspect ratio
    QRadioButton *radAspectDisabled;
    QRadioButton *radAspectSame;
    QRadioButton *radAspectRatio;
    QSpinBox *spinAspectW;
    QSpinBox *spinAspectH;

    // Filter mode & interlaced
    QComboBox *comboFilterMode;
    QCheckBox *chkInterlaced;

    // Framing options
    QRadioButton *radFrameNone;
    QRadioButton *radFrameSize;
    QRadioButton *radFrameCropAspect;
    QRadioButton *radFrameLetterboxAspect;
    QSpinBox *spinFrameW;
    QSpinBox *spinFrameH;
    QSpinBox *spinFrameAspectW;
    QSpinBox *spinFrameAspectH;
    QPushButton *btnPickColor;
    QWidget *colorSwatch;

    // Codec-friendly sizing
    QRadioButton *radCodecNone;
    QRadioButton *radCodec2;
    QRadioButton *radCodec4;
    QRadioButton *radCodec8;
    QRadioButton *radCodec16;
};

// Rotate Filter Dialog (Matching VirtualDub Screenshot)
class VDRotateFilterDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDRotateFilterDialog(const QMap<QString, double>& params, QWidget *parent = nullptr);
    QMap<QString, double> getParams() const;
    int getMode() const;

private:
    QRadioButton *radLeft90;
    QRadioButton *radRight90;
    QRadioButton *radAround180;
};

// Brightness / Contrast Filter Dialog (Matching VirtualDub Screenshot)
class VDBrightnessContrastFilterDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDBrightnessContrastFilterDialog(const QMap<QString, double>& params, const QImage &sourceFrame = QImage(), QWidget *parent = nullptr);
    ~VDBrightnessContrastFilterDialog() override;
    QMap<QString, double> getParams() const;

private Q_SLOTS:
    void onSliderValueChanged();
    void onTogglePreviewClicked();

private:
    void updatePreviewImage();

    QSlider *sliderBrightness;
    QSlider *sliderContrast;
    QPushButton *btnShowPreview;
    QPushButton *btnOk;
    QPushButton *btnCancel;

    QImage mSourceFrame;
    VDFilterPreviewDialog *mPreviewDialog;
};

// Box Blur Filter Dialog (Matching VirtualDub Screenshot)
class VDBoxBlurFilterDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDBoxBlurFilterDialog(const QMap<QString, double>& params, const QImage &sourceFrame = QImage(), QWidget *parent = nullptr);
    ~VDBoxBlurFilterDialog() override;
    QMap<QString, double> getParams() const;

private Q_SLOTS:
    void onSliderValueChanged();
    void onTogglePreviewClicked();

private:
    void updateLabels();
    void updatePreviewImage();

    QSlider *sliderRadius;
    QSlider *sliderPower;
    QLabel *lblRadiusValue;
    QLabel *lblPowerValue;
    QPushButton *btnShowPreview;
    QPushButton *btnOk;
    QPushButton *btnCancel;

    QImage mSourceFrame;
    VDFilterPreviewDialog *mPreviewDialog;
};

// Sharpen Filter Dialog (Matching VirtualDub Screenshot)
class VDSharpenFilterDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDSharpenFilterDialog(const QMap<QString, double>& params, const QImage &sourceFrame = QImage(), QWidget *parent = nullptr);
    ~VDSharpenFilterDialog() override;
    QMap<QString, double> getParams() const;

private Q_SLOTS:
    void onSliderValueChanged();
    void onTogglePreviewClicked();

private:
    void updatePreviewImage();

    QSlider *sliderSharpen;
    QLabel *lblSharpenValue;
    QPushButton *btnShowPreview;
    QPushButton *btnOk;
    QPushButton *btnCancel;

    QImage mSourceFrame;
    VDFilterPreviewDialog *mPreviewDialog;
};

// 6-Axis Color Correction Filter Dialog (Matching VirtualDub Screenshot)
class VD6AxisFilterDialog : public QDialog {
    Q_OBJECT
public:
    explicit VD6AxisFilterDialog(const QMap<QString, double>& params, const QImage &sourceFrame = QImage(), QWidget *parent = nullptr);
    ~VD6AxisFilterDialog() override;
    QMap<QString, double> getParams() const;

private Q_SLOTS:
    void onSliderValueChanged();
    void onTogglePreviewClicked();

private:
    void updatePreviewImage();

    QSlider *sliderIntensity;
    QSlider *sliderRedGreen;
    QSlider *sliderYellowBlue;
    QSlider *sliderSaturation;
    QSlider *sliderRed;
    QSlider *sliderOrange;
    QSlider *sliderLime;
    QSlider *sliderEmerald;
    QSlider *sliderBlue;
    QSlider *sliderPurple;

    QPushButton *btnShowPreview;
    QPushButton *btnOk;
    QPushButton *btnCancel;

    QImage mSourceFrame;
    VDFilterPreviewDialog *mPreviewDialog;
};

// Bob Doubler Filter Dialog (Matching VirtualDub Screenshot)
class VDBobDoublerFilterDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDBobDoublerFilterDialog(const QMap<QString, double>& params, const QImage &sourceFrame = QImage(), QWidget *parent = nullptr);
    ~VDBobDoublerFilterDialog() override;
    QMap<QString, double> getParams() const;

private Q_SLOTS:
    void onModeChanged();
    void onTogglePreviewClicked();

private:
    void updatePreviewImage();

    QRadioButton *radTFF;
    QRadioButton *radBFF;

    QRadioButton *radBob;
    QRadioButton *radELA;
    QRadioButton *radAdaptiveELA;
    QRadioButton *radNoneFields;
    QRadioButton *radNoneFrames;

    QPushButton *btnShowPreview;
    QPushButton *btnOk;
    QPushButton *btnCancel;

    QImage mSourceFrame;
    VDFilterPreviewDialog *mPreviewDialog;
};

// Add Filter Selection Dialog
class VDVideoFilterAddDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDVideoFilterAddDialog(QWidget *parent = nullptr);
    QString getSelectedFilterName() const;
    VDFilterType getSelectedFilterType() const;

private:
    QListWidget *mFilterList;
    QLabel *mDescLabel;
    QList<VDQtFilterSystem::FilterInfo> mAvailableFilters;
};

// Video Frame Rate Control Dialog (Matching VirtualDub Screenshot)
struct VDFrameRateConfig {
    int sourceMode = 0; // 0: No change, 1: Custom fps, 2: Match audio
    double customSourceFps = 0.0;
    int convMode = 0;   // 0: All frames, 1: Decimate 2, 2: Decimate 3, 3: Decimate N, 4: Convert to fps
    int decimateN = 2;
    double convertFps = 0.0;
};

class VDFrameRateDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDFrameRateDialog(double sourceFps, double audioMatchFps, const VDFrameRateConfig &initialConfig = VDFrameRateConfig(), QWidget *parent = nullptr);
    VDFrameRateConfig getConfig() const;
    double getTargetFps() const;
    int getDecimateFactor() const;

private Q_SLOTS:
    void onSourceRadioToggled();
    void onConversionRadioToggled();

private:
    double mSourceFps;
    double mAudioMatchFps;
    VDFrameRateConfig mConfig;

    // Group 1: Source rate adjustment
    QRadioButton *radSourceNoChange;
    QRadioButton *radSourceCustom;
    QRadioButton *radSourceMatchAudio;
    QLineEdit *txtSourceCustomFps;

    // Group 2: Frame rate conversion
    QRadioButton *radConvAllFrames;
    QRadioButton *radConvDecimate2;
    QRadioButton *radConvDecimate3;
    QRadioButton *radConvDecimateN;
    QRadioButton *radConvCustomFps;
    QLineEdit *txtConvDecimateN;
    QLineEdit *txtConvCustomFps;

    QPushButton *btnOk;
    QPushButton *btnCancel;
};

// Decompression Format Dialog (Matching VirtualDub Screenshot)
struct VDDecompressionFormatConfig {
    QString formatName = "Autoselect";
    int colorSpace = 0;     // 0: No change, 1: Rec. 601 (SD), 2: Rec. 709 (HD)
    int componentRange = 0; // 0: No change, 1: Limited (Y: 16-235), 2: Full (0-255)
};

class VDDecodeFormatDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDDecodeFormatDialog(const QString &decoderName, const QString &actualFormat, const VDDecompressionFormatConfig &initialConfig = VDDecompressionFormatConfig(), QWidget *parent = nullptr);
    VDDecompressionFormatConfig getConfig() const;

private:
    QString mDecoderName;
    QString mActualFormat;
    VDDecompressionFormatConfig mConfig;

    // Top info
    QLabel *lblDecoderVal;
    QLineEdit *txtActualFormat;

    // Formats radio group
    QButtonGroup *grpFormats;
    QRadioButton *radAutoselect;
    QRadioButton *radRGB24;

    // YCbCr Properties
    QButtonGroup *grpColorSpace;
    QRadioButton *radCSNoChange;
    QRadioButton *radCSRec601;
    QRadioButton *radCSRec709;

    QButtonGroup *grpRange;
    QRadioButton *radRangeNoChange;
    QRadioButton *radRangeLimited;
    QRadioButton *radRangeFull;

    QPushButton *btnOk;
    QPushButton *btnCancel;
};

// Decoder Error Mode Dialog (Matching VirtualDub Screenshot)
struct VDDecoderErrorModeConfig {
    int errorMode = 0; // 0: Report all errors, 1: Conceal errors, 2: Decode even if garbled
};

class VDDecoderErrorModeDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDDecoderErrorModeDialog(const VDDecoderErrorModeConfig &initialConfig = VDDecoderErrorModeConfig(), QWidget *parent = nullptr);
    VDDecoderErrorModeConfig getConfig() const;

private:
    VDDecoderErrorModeConfig mConfig;

    QButtonGroup *grpErrorMode;
    QRadioButton *radReportAll;
    QRadioButton *radConceal;
    QRadioButton *radGarbled;

    QPushButton *btnOk;
    QPushButton *btnCancel;
};

#include "VDQtCodecSettings.h"
#include "VDQtCodecEngine.h"

// Save Audio Dialog with Codec Dropdown & Independent Full Processing Controls
class VDSaveAudioDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDSaveAudioDialog(const QString &defaultDir, const QString &defaultFileName, const QString &compressionInfo, const QString &sampleLayoutInfo, QWidget *parent = nullptr);
    QString getSelectedFilePath() const;
    VDAudioCodecConfig getAudioConfig() const;

private Q_SLOTS:
    void onBrowseClicked();
    void onFilterChanged(int index);
    void onCodecChanged(int index);
    void onRateControlModeChanged();
    void onSaveClicked();

private:
    QString mDirectory;
    QLineEdit *txtFileName;
    QComboBox *cboFileType;

    // Codec & Processing controls
    QComboBox *cboCodec;
    QGroupBox *grpBitrate;
    QRadioButton *mRadioVbr;
    QRadioButton *mRadioCbr;
    QLabel *lblVbr;
    QComboBox *mVbrCombo;
    QComboBox *mCbrBitrateCombo;
    QComboBox *mSampleRateCombo;
    QComboBox *mChannelsCombo;

    QPushButton *btnSave;
    QPushButton *btnCancel;
    QPushButton *btnBrowse;
    QLabel *lblDir;
};

// Video Compression Dialog
class VDVideoCompressionDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDVideoCompressionDialog(QWidget *parent = nullptr);

private Q_SLOTS:
    void onCodecSelectionChanged();
    void onSaveClicked();
    void onConfigureClicked();
    void onPixelFormatClicked();

private:
    QListWidget *mCodecList;
    
    // Info Box Labels
    QLabel *mLabelDeltaFrames;
    QLabel *mLabelFourCC;
    QLabel *mLabelDriverName;
    QLabel *mLabelPixFmtText;
    QLabel *mInfoText;
    
    // Buttons
    QPushButton *btnConfigure;
    QPushButton *btnPixelFormat;
    QPushButton *btnAbout;
    
    // Radio buttons
    QRadioButton *mRadioFiltered;
    QRadioButton *mRadioShowAll;
};

// Audio Compression Dialog
class VDAudioCompressionDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDAudioCompressionDialog(QWidget *parent = nullptr);

private Q_SLOTS:
    void onCodecSelectionChanged();
    void onRateControlModeChanged();
    void onSaveClicked();

private:
    QListWidget *mCodecList;
    QGroupBox *mBitrateGroup;
    QRadioButton *mRadioVbr;
    QRadioButton *mRadioCbr;
    QLabel *lblVbr;
    QComboBox *mVbrCombo;
    QComboBox *mCbrBitrateCombo;
    QComboBox *mSampleRateCombo;
    QComboBox *mChannelsCombo;
};

// About Dialog
class VDAboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDAboutDialog(QWidget *parent = nullptr);
};

// Log Console Window
class VDLogWindow : public QDialog {
    Q_OBJECT
public:
    explicit VDLogWindow(QWidget *parent = nullptr);
    static VDLogWindow* instance(QWidget *parent = nullptr);
    void appendLog(const QString &text);

private:
    QTextEdit *mLogText;
    static VDLogWindow *sInstance;
};

// VDSaveVideoDialog (File -> Save video... F7 matching VirtualDub2 screenshot)
class VDSaveVideoDialog : public QDialog {
    Q_OBJECT
public:
    explicit VDSaveVideoDialog(int videoMode = 3, int audioMode = 0, const QString &defaultDir = QString(), const QString &defaultBaseName = QString(), QWidget *parent = nullptr);

    QString getSelectedFilePath() const;
    QString getSelectedContainerType() const;
    bool isFastStartEnabled() const;

private Q_SLOTS:
    void onBrowseClicked();
    void onFileTypeIndexChanged(int index);

private:
    QString mDefaultDir;
    QString mDefaultBaseName;
    QLineEdit *mFileNameEdit;
    QComboBox *mFileTypeCombo;
    QPushButton *mBrowseBtn;

    QLabel *mVideoCompressionLabel;
    QLabel *mVideoPixFmtLabel;

    QLabel *mAudioCompressionLabel;
    QLabel *mAudioSampleLayoutLabel;

};

#endif // VDQTDIALOGS_H
