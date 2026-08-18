#include "VDQtDialogs.h"
#include <algorithm>
#include <cmath>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>

// Style sheet snippet for dark polished Qt dialog look matching VirtualDub2 aesthetics
static const char* kDialogStyle =
    "QDialog { background-color: #1a1a22; color: #e0e0e0; }"
    "QLabel { color: #d0d0d8; font-size: 12px; }"
    "QGroupBox { border: 1px solid #333342; border-radius: 6px; margin-top: 10px; font-weight: bold; color: #00bcd4; padding-top: 10px; }"
    "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 5px; }"
    "QPushButton { background-color: #282836; color: #e0e0e0; border: 1px solid #3d3d52; border-radius: 4px; padding: 6px 16px; font-weight: bold; }"
    "QPushButton:hover { background-color: #38384d; border-color: #00bcd4; }"
    "QPushButton:pressed { background-color: #00bcd4; color: #121216; }"
    "QTableWidget, QListWidget, QTextEdit { background-color: #121218; color: #e0e0e0; border: 1px solid #333342; border-radius: 4px; gridline-color: #2a2a38; }"
    "QHeaderView::section { background-color: #22222d; color: #00bcd4; border: 1px solid #2d2d3c; padding: 4px; font-weight: bold; }"
    "QLineEdit, QSpinBox, QComboBox { background-color: #121218; color: #ffffff; border: 1px solid #38384d; border-radius: 4px; padding: 4px; }"
    "QTabBar::tab { background: #22222d; color: #b0b0bc; padding: 8px 16px; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
    "QTabBar::tab:selected { background: #00bcd4; color: #121216; font-weight: bold; }";

#include <QColorDialog>
#include <QButtonGroup>
#include <QPainter>

// -----------------------------------------------------------------------------
// VDVideoFiltersDialog Implementation
// -----------------------------------------------------------------------------
VDVideoFiltersDialog::VDVideoFiltersDialog(int sourceWidth, int sourceHeight, const QImage &sourceFrame, QWidget *parent)
    : QDialog(parent), mSourceWidth(sourceWidth), mSourceHeight(sourceHeight), mSourceFrame(sourceFrame) {
    setWindowTitle("Filters");
    resize(640, 440);
    setStyleSheet(kDialogStyle);

    QVBoxLayout *outerLayout = new QVBoxLayout(this);

    QHBoxLayout *mainLayout = new QHBoxLayout();

    // Filter Table (Columns: Enable Check, Input, Output, Filter)
    mFilterTable = new QTableWidget(0, 4, this);
    mFilterTable->setHorizontalHeaderLabels({" ", "Input", "Output", "Filter"});
    mFilterTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    mFilterTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mFilterTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    mFilterTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    mFilterTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mFilterTable->verticalHeader()->setVisible(false);
    mainLayout->addWidget(mFilterTable, 1);

    // Right action buttons panel (matching VirtualDub2 layout)
    QVBoxLayout *btnLayout = new QVBoxLayout();
    QDialogButtonBox *topBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(topBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(topBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    btnLayout->addWidget(topBox);

    btnAdd = new QPushButton("Add...", this);
    btnDelete = new QPushButton("Delete", this);
    btnMoveUp = new QPushButton("Move Up", this);
    btnMoveDown = new QPushButton("Move Down", this);
    btnConfigure = new QPushButton("Configure...", this);

    btnLayout->addWidget(btnAdd);
    btnLayout->addWidget(btnDelete);
    btnLayout->addSpacing(10);
    btnLayout->addWidget(btnMoveUp);
    btnLayout->addWidget(btnMoveDown);
    btnLayout->addSpacing(10);
    btnLayout->addWidget(btnConfigure);
    btnLayout->addStretch();

    mainLayout->addLayout(btnLayout);
    outerLayout->addLayout(mainLayout);

    connect(btnAdd, &QPushButton::clicked, this, &VDVideoFiltersDialog::onAddClicked);
    connect(btnDelete, &QPushButton::clicked, this, &VDVideoFiltersDialog::onDeleteClicked);
    connect(btnMoveUp, &QPushButton::clicked, this, &VDVideoFiltersDialog::onMoveUpClicked);
    connect(btnMoveDown, &QPushButton::clicked, this, &VDVideoFiltersDialog::onMoveDownClicked);
    connect(btnConfigure, &QPushButton::clicked, this, &VDVideoFiltersDialog::onConfigureClicked);
    connect(mFilterTable, &QTableWidget::itemChanged, this, &VDVideoFiltersDialog::onItemChanged);

    refreshFilterTable();
}

void VDVideoFiltersDialog::refreshFilterTable() {
    mFilterTable->blockSignals(true);
    mFilterTable->setRowCount(0);
    const auto& chain = VDQtFilterSystem::instance().getActiveChain();

    int curW = mSourceWidth > 0 ? mSourceWidth : 1920;
    int curH = mSourceHeight > 0 ? mSourceHeight : 1080;

    for (int i = 0; i < chain.size(); i++) {
        const auto& filter = chain[i];
        int row = mFilterTable->rowCount();
        mFilterTable->insertRow(row);

        int inW = curW;
        int inH = curH;
        int outW = inW;
        int outH = inH;

        QString filterText = filter.name;

        if (filter.type == VDFilterType::Resize) {
            outW = filter.params.value("width", inW);
            outH = filter.params.value("height", inH);
            int filterMode = filter.params.value("filterMode", 4);
            QString modeStr = "Precise bicubic (A=-0.75)";
            switch (filterMode) {
                case 0: modeStr = "Nearest neighbor"; break;
                case 1: modeStr = "Bilinear"; break;
                case 2: modeStr = "Bicubic"; break;
                case 3: modeStr = "Precise bilinear"; break;
                case 4: modeStr = "Precise bicubic (A=-0.75)"; break;
                case 5: modeStr = "Precise bicubic (A=-0.60)"; break;
                case 6: modeStr = "Precise bicubic (A=-1.00)"; break;
                case 7: modeStr = "Lanczos3"; break;
            }
            filterText = QString("resize (%1)").arg(modeStr);
        } else if (filter.type == VDFilterType::Rotate) {
            int mode = static_cast<int>(filter.params.value("mode", 0));
            if (mode == 0) {
                outW = inH;
                outH = inW;
                filterText = "rotate (left 90°)";
            } else if (mode == 1) {
                outW = inH;
                outH = inW;
                filterText = "rotate (right 90°)";
            } else {
                outW = inW;
                outH = inH;
                filterText = "rotate (180°)";
            }
        } else if (filter.type == VDFilterType::BrightnessContrast) {
            int bright = static_cast<int>(filter.params.value("bright", 0));
            int cont = static_cast<int>(filter.params.value("cont", 16));
            int brightPct = (bright * 25) / 64;
            int contPct = (cont * 25) / 4;
            filterText = QString("brightness/contrast (bright %+1%, cont %2%)")
                .arg(brightPct)
                .arg(contPct);
        } else if (filter.type == VDFilterType::SixAxis) {
            filterText = "6-axis color correction";
        } else if (filter.type == VDFilterType::BobDoubler) {
            int fo = static_cast<int>(filter.params.value("field_order", 1));
            int mode = static_cast<int>(filter.params.value("mode", 0));
            const char *foStr = (fo == 1) ? "BFF" : "TFF";
            static const char *const kModes[] = { "bob", "ELA", "adaptive ELA", "none-fields", "none-frames" };
            const char *modeStr = (mode >= 0 && mode < 5) ? kModes[mode] : "bob";
            filterText = QString("bob doubler (%1, %2)").arg(foStr).arg(modeStr);
        } else if (filter.type == VDFilterType::Blur) {
            int width = static_cast<int>(filter.params.value("width", 1));
            int power = static_cast<int>(filter.params.value("power", 1));
            int radius = width + power - 1;
            filterText = QString("box blur (radius %1, power %2)").arg(radius).arg(power);
        } else if (filter.type == VDFilterType::Sharpen) {
            int amount = static_cast<int>(filter.params.value("amount", 16));
            filterText = QString("sharpen (by %1)").arg(amount);
        }

        // Col 0: Checkbox
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        checkItem->setCheckState(filter.enabled ? Qt::Checked : Qt::Unchecked);
        mFilterTable->setItem(row, 0, checkItem);

        // Col 1: Input Resolution
        mFilterTable->setItem(row, 1, new QTableWidgetItem(QString("%1x%2").arg(inW).arg(inH)));

        // Col 2: Output Resolution
        mFilterTable->setItem(row, 2, new QTableWidgetItem(QString("%1x%2").arg(outW).arg(outH)));

        // Col 3: Filter summary
        mFilterTable->setItem(row, 3, new QTableWidgetItem(filterText));

        if (filter.enabled) {
            curW = outW;
            curH = outH;
        }
    }
    mFilterTable->blockSignals(false);
}

void VDVideoFiltersDialog::onItemChanged(QTableWidgetItem *item) {
    if (item && item->column() == 0) {
        int row = item->row();
        bool enabled = (item->checkState() == Qt::Checked);
        VDQtFilterSystem::instance().setFilterEnabled(row, enabled);
        refreshFilterTable();
    }
}

void VDVideoFiltersDialog::onAddClicked() {
    VDVideoFilterAddDialog addDlg(this);
    if (addDlg.exec() == QDialog::Accepted) {
        VDFilterType type = addDlg.getSelectedFilterType();
        if (type == VDFilterType::SixAxis) {
            QMap<QString, double> defParams;
            defParams["intensity"] = 1.0;
            defParams["red_green"] = 0.0;
            defParams["yellow_blue"] = 0.0;
            defParams["saturation"] = 1.0;
            defParams["red"] = 1.0;
            defParams["orange"] = 1.0;
            defParams["lime"] = 1.0;
            defParams["emerald"] = 1.0;
            defParams["blue"] = 1.0;
            defParams["purple"] = 1.0;
            VD6AxisFilterDialog sixDlg(defParams, mSourceFrame, this);
            if (sixDlg.exec() == QDialog::Accepted) {
                VDQtFilterSystem::instance().addFilter(type);
                int lastIndex = VDQtFilterSystem::instance().getActiveChain().size() - 1;
                VDQtFilterSystem::instance().updateFilterParams(lastIndex, sixDlg.getParams());
                refreshFilterTable();
            }
        } else if (type == VDFilterType::BobDoubler) {
            QMap<QString, double> defParams;
            defParams["field_order"] = 1;
            defParams["mode"] = 0;
            VDBobDoublerFilterDialog bobDlg(defParams, mSourceFrame, this);
            if (bobDlg.exec() == QDialog::Accepted) {
                VDQtFilterSystem::instance().addFilter(type);
                int lastIndex = VDQtFilterSystem::instance().getActiveChain().size() - 1;
                VDQtFilterSystem::instance().updateFilterParams(lastIndex, bobDlg.getParams());
                refreshFilterTable();
            }
        } else if (type == VDFilterType::Resize) {
            VDResizeFilterDialog resizeDlg(QMap<QString, double>(), mSourceWidth, mSourceHeight, this);
            if (resizeDlg.exec() == QDialog::Accepted) {
                VDQtFilterSystem::instance().addFilter(type);
                int lastIndex = VDQtFilterSystem::instance().getActiveChain().size() - 1;
                VDQtFilterSystem::instance().updateFilterParams(lastIndex, resizeDlg.getParams());
                refreshFilterTable();
            }
        } else if (type == VDFilterType::Rotate) {
            QMap<QString, double> defParams;
            defParams["mode"] = 0;
            defParams["angle"] = 270;
            VDRotateFilterDialog rotDlg(defParams, this);
            if (rotDlg.exec() == QDialog::Accepted) {
                VDQtFilterSystem::instance().addFilter(type);
                int lastIndex = VDQtFilterSystem::instance().getActiveChain().size() - 1;
                VDQtFilterSystem::instance().updateFilterParams(lastIndex, rotDlg.getParams());
                refreshFilterTable();
            }
        } else if (type == VDFilterType::BrightnessContrast) {
            QMap<QString, double> defParams;
            defParams["bright"] = 0;
            defParams["cont"] = 16;
            VDBrightnessContrastFilterDialog bcDlg(defParams, mSourceFrame, this);
            if (bcDlg.exec() == QDialog::Accepted) {
                VDQtFilterSystem::instance().addFilter(type);
                int lastIndex = VDQtFilterSystem::instance().getActiveChain().size() - 1;
                VDQtFilterSystem::instance().updateFilterParams(lastIndex, bcDlg.getParams());
                refreshFilterTable();
            }
        } else if (type == VDFilterType::Blur) {
            QMap<QString, double> defParams;
            defParams["width"] = 1;
            defParams["power"] = 1;
            defParams["radius"] = 1;
            VDBoxBlurFilterDialog blurDlg(defParams, mSourceFrame, this);
            if (blurDlg.exec() == QDialog::Accepted) {
                VDQtFilterSystem::instance().addFilter(type);
                int lastIndex = VDQtFilterSystem::instance().getActiveChain().size() - 1;
                VDQtFilterSystem::instance().updateFilterParams(lastIndex, blurDlg.getParams());
                refreshFilterTable();
            }
        } else if (type == VDFilterType::Sharpen) {
            QMap<QString, double> defParams;
            defParams["amount"] = 16;
            VDSharpenFilterDialog sharpDlg(defParams, mSourceFrame, this);
            if (sharpDlg.exec() == QDialog::Accepted) {
                VDQtFilterSystem::instance().addFilter(type);
                int lastIndex = VDQtFilterSystem::instance().getActiveChain().size() - 1;
                VDQtFilterSystem::instance().updateFilterParams(lastIndex, sharpDlg.getParams());
                refreshFilterTable();
            }
        } else {
            VDQtFilterSystem::instance().addFilter(type);
            refreshFilterTable();
        }
    }
}

void VDVideoFiltersDialog::onDeleteClicked() {
    int row = mFilterTable->currentRow();
    if (row >= 0) {
        VDQtFilterSystem::instance().removeFilter(row);
        refreshFilterTable();
    }
}

void VDVideoFiltersDialog::onMoveUpClicked() {
    int row = mFilterTable->currentRow();
    if (row > 0) {
        VDQtFilterSystem::instance().moveFilterUp(row);
        refreshFilterTable();
        mFilterTable->setCurrentCell(row - 1, 0);
    }
}

void VDVideoFiltersDialog::onMoveDownClicked() {
    int row = mFilterTable->currentRow();
    if (row >= 0 && row < mFilterTable->rowCount() - 1) {
        VDQtFilterSystem::instance().moveFilterDown(row);
        refreshFilterTable();
        mFilterTable->setCurrentCell(row + 1, 0);
    }
}

void VDVideoFiltersDialog::onConfigureClicked() {
    int row = mFilterTable->currentRow();
    if (row < 0) return;

    const auto& chain = VDQtFilterSystem::instance().getActiveChain();
    if (row >= chain.size()) return;

    auto filter = chain[row];

    if (filter.type == VDFilterType::SixAxis) {
        VD6AxisFilterDialog sixDlg(filter.params, mSourceFrame, this);
        if (sixDlg.exec() == QDialog::Accepted) {
            VDQtFilterSystem::instance().updateFilterParams(row, sixDlg.getParams());
            refreshFilterTable();
        }
    } else if (filter.type == VDFilterType::BobDoubler) {
        VDBobDoublerFilterDialog bobDlg(filter.params, mSourceFrame, this);
        if (bobDlg.exec() == QDialog::Accepted) {
            VDQtFilterSystem::instance().updateFilterParams(row, bobDlg.getParams());
            refreshFilterTable();
        }
    } else if (filter.type == VDFilterType::Resize) {
        VDResizeFilterDialog resizeDlg(filter.params, mSourceWidth, mSourceHeight, this);
        if (resizeDlg.exec() == QDialog::Accepted) {
            VDQtFilterSystem::instance().updateFilterParams(row, resizeDlg.getParams());
            refreshFilterTable();
        }
    } else if (filter.type == VDFilterType::Rotate) {
        VDRotateFilterDialog rotDlg(filter.params, this);
        if (rotDlg.exec() == QDialog::Accepted) {
            VDQtFilterSystem::instance().updateFilterParams(row, rotDlg.getParams());
            refreshFilterTable();
        }
    } else if (filter.type == VDFilterType::BrightnessContrast) {
        VDBrightnessContrastFilterDialog bcDlg(filter.params, mSourceFrame, this);
        if (bcDlg.exec() == QDialog::Accepted) {
            VDQtFilterSystem::instance().updateFilterParams(row, bcDlg.getParams());
            refreshFilterTable();
        }
    } else if (filter.type == VDFilterType::Blur) {
        VDBoxBlurFilterDialog blurDlg(filter.params, mSourceFrame, this);
        if (blurDlg.exec() == QDialog::Accepted) {
            VDQtFilterSystem::instance().updateFilterParams(row, blurDlg.getParams());
            refreshFilterTable();
        }
    } else if (filter.type == VDFilterType::Sharpen) {
        VDSharpenFilterDialog sharpDlg(filter.params, mSourceFrame, this);
        if (sharpDlg.exec() == QDialog::Accepted) {
            VDQtFilterSystem::instance().updateFilterParams(row, sharpDlg.getParams());
            refreshFilterTable();
        }
    } else {
        QMessageBox::information(this, "Filter Config", QString("'%1' filter is active.").arg(filter.name));
    }
}

// -----------------------------------------------------------------------------
// VDResizeFilterDialog Implementation (Matching Screenshot 1)
// -----------------------------------------------------------------------------
VDResizeFilterDialog::VDResizeFilterDialog(const QMap<QString, double>& params, int sourceW, int sourceH, QWidget *parent)
    : QDialog(parent), mSourceW(sourceW > 0 ? sourceW : 1440), mSourceH(sourceH > 0 ? sourceH : 1080) {
    setWindowTitle("Filter: Resize");
    setStyleSheet(kDialogStyle);
    resize(520, 500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // -------------------------------------------------------------------------
    // GROUP 1: SIZE OPTIONS
    // -------------------------------------------------------------------------
    QGroupBox *grpSize = new QGroupBox("Size options", this);
    QGridLayout *gridSize = new QGridLayout(grpSize);

    radAbsolute = new QRadioButton("Absolute (pixels)", this);
    radRelative = new QRadioButton("Relative (%)", this);

    QButtonGroup *sizeGroup = new QButtonGroup(this);
    sizeGroup->addButton(radAbsolute);
    sizeGroup->addButton(radRelative);

    spinAbsW = new QSpinBox(this); spinAbsW->setRange(16, 7680); spinAbsW->setValue(params.value("absW", mSourceW));
    spinAbsH = new QSpinBox(this); spinAbsH->setRange(16, 4320); spinAbsH->setValue(params.value("absH", mSourceH));

    spinRelW = new QSpinBox(this); spinRelW->setRange(1, 1000); spinRelW->setValue(params.value("relW", 100));
    spinRelH = new QSpinBox(this); spinRelH->setRange(1, 1000); spinRelH->setValue(params.value("relH", 100));

    gridSize->addWidget(new QLabel("New size", this), 0, 0);
    gridSize->addWidget(radAbsolute, 0, 1);
    gridSize->addWidget(spinAbsW, 0, 2);
    gridSize->addWidget(new QLabel("x", this), 0, 3);
    gridSize->addWidget(spinAbsH, 0, 4);

    gridSize->addWidget(radRelative, 1, 1);
    gridSize->addWidget(spinRelW, 1, 2);
    gridSize->addWidget(new QLabel("x", this), 1, 3);
    gridSize->addWidget(spinRelH, 1, 4);

    radAspectDisabled = new QRadioButton("Disabled", this);
    radAspectSame = new QRadioButton("Same as source", this);
    radAspectRatio = new QRadioButton("Compute height from ratio:", this);

    QButtonGroup *aspectGroup = new QButtonGroup(this);
    aspectGroup->addButton(radAspectDisabled);
    aspectGroup->addButton(radAspectSame);
    aspectGroup->addButton(radAspectRatio);

    spinAspectW = new QSpinBox(this); spinAspectW->setRange(1, 100); spinAspectW->setValue(params.value("aspectW", 4));
    spinAspectH = new QSpinBox(this); spinAspectH->setRange(1, 100); spinAspectH->setValue(params.value("aspectH", 3));

    gridSize->addWidget(new QLabel("Aspect ratio", this), 2, 0);
    gridSize->addWidget(radAspectDisabled, 2, 1);

    gridSize->addWidget(radAspectSame, 3, 1);

    gridSize->addWidget(radAspectRatio, 4, 1);
    gridSize->addWidget(spinAspectW, 4, 2);
    gridSize->addWidget(new QLabel(":", this), 4, 3);
    gridSize->addWidget(spinAspectH, 4, 4);

    comboFilterMode = new QComboBox(this);
    comboFilterMode->addItems({
        "Nearest neighbor",
        "Bilinear",
        "Bicubic",
        "Precise bilinear",
        "Precise bicubic (A=-0.75)",
        "Precise bicubic (A=-0.60)",
        "Precise bicubic (A=-1.00)",
        "Lanczos3"
    });
    comboFilterMode->setCurrentIndex(params.value("filterMode", 4));

    chkInterlaced = new QCheckBox("Interlaced", this);
    chkInterlaced->setChecked(params.value("interlaced", 0) > 0);

    gridSize->addWidget(new QLabel("Filter mode", this), 5, 0);
    gridSize->addWidget(comboFilterMode, 5, 1, 1, 3);
    gridSize->addWidget(chkInterlaced, 5, 4);

    mainLayout->addWidget(grpSize);

    // -------------------------------------------------------------------------
    // MIDDLE ROW: FRAMING & CODEC-FRIENDLY SIZING
    // -------------------------------------------------------------------------
    QHBoxLayout *midLayout = new QHBoxLayout();

    // Group 2: Framing options
    QGroupBox *grpFraming = new QGroupBox("Framing options", this);
    QVBoxLayout *vboxFrame = new QVBoxLayout(grpFraming);

    radFrameNone = new QRadioButton("Do not letterbox or crop", this);
    radFrameSize = new QRadioButton("Letterbox/crop to size:", this);
    radFrameCropAspect = new QRadioButton("Crop to aspect ratio", this);
    radFrameLetterboxAspect = new QRadioButton("Letterbox to aspect ratio", this);

    QButtonGroup *framingGroup = new QButtonGroup(this);
    framingGroup->addButton(radFrameNone);
    framingGroup->addButton(radFrameSize);
    framingGroup->addButton(radFrameCropAspect);
    framingGroup->addButton(radFrameLetterboxAspect);

    spinFrameW = new QSpinBox(this); spinFrameW->setRange(16, 7680); spinFrameW->setValue(params.value("frameW", 320));
    spinFrameH = new QSpinBox(this); spinFrameH->setRange(16, 4320); spinFrameH->setValue(params.value("frameH", 240));

    QHBoxLayout *hFrameSize = new QHBoxLayout();
    hFrameSize->addWidget(radFrameSize);
    hFrameSize->addWidget(spinFrameW);
    hFrameSize->addWidget(new QLabel("x", this));
    hFrameSize->addWidget(spinFrameH);

    spinFrameAspectW = new QSpinBox(this); spinFrameAspectW->setRange(1, 100); spinFrameAspectW->setValue(params.value("frameAspectW", 4));
    spinFrameAspectH = new QSpinBox(this); spinFrameAspectH->setRange(1, 100); spinFrameAspectH->setValue(params.value("frameAspectH", 3));

    QHBoxLayout *hFrameAspect = new QHBoxLayout();
    hFrameAspect->addSpacing(20);
    hFrameAspect->addWidget(new QLabel("Aspect ratio", this));
    hFrameAspect->addWidget(spinFrameAspectW);
    hFrameAspect->addWidget(new QLabel(":", this));
    hFrameAspect->addWidget(spinFrameAspectH);

    QHBoxLayout *hColor = new QHBoxLayout();
    hColor->addWidget(new QLabel("Fill color", this));

    mFillColor = QColor(
        params.value("fillColorR", 0),
        params.value("fillColorG", 0),
        params.value("fillColorB", 0)
    );

    colorSwatch = new QWidget(this);
    colorSwatch->setFixedSize(24, 24);
    colorSwatch->setStyleSheet(QString("background-color: %1; border: 1px solid #777;").arg(mFillColor.name()));

    btnPickColor = new QPushButton("Pick color...", this);
    hColor->addWidget(colorSwatch);
    hColor->addWidget(btnPickColor);
    hColor->addStretch();

    vboxFrame->addWidget(radFrameNone);
    vboxFrame->addLayout(hFrameSize);
    vboxFrame->addWidget(radFrameCropAspect);
    vboxFrame->addWidget(radFrameLetterboxAspect);
    vboxFrame->addLayout(hFrameAspect);
    vboxFrame->addLayout(hColor);

    midLayout->addWidget(grpFraming, 3);

    // Group 3: Codec-friendly sizing
    QGroupBox *grpCodec = new QGroupBox("Codec-friendly sizing", this);
    QVBoxLayout *vboxCodec = new QVBoxLayout(grpCodec);

    radCodecNone = new QRadioButton("Do not adjust", this);
    radCodec2 = new QRadioButton("Multiples of 2", this);
    radCodec4 = new QRadioButton("Multiples of 4", this);
    radCodec8 = new QRadioButton("Multiples of 8", this);
    radCodec16 = new QRadioButton("Multiples of 16", this);

    QButtonGroup *codecGroup = new QButtonGroup(this);
    codecGroup->addButton(radCodecNone);
    codecGroup->addButton(radCodec2);
    codecGroup->addButton(radCodec4);
    codecGroup->addButton(radCodec8);
    codecGroup->addButton(radCodec16);

    vboxCodec->addWidget(radCodecNone);
    vboxCodec->addWidget(radCodec2);
    vboxCodec->addWidget(radCodec4);
    vboxCodec->addWidget(radCodec8);
    vboxCodec->addWidget(radCodec16);
    vboxCodec->addStretch();

    midLayout->addWidget(grpCodec, 2);
    mainLayout->addLayout(midLayout);

    // -------------------------------------------------------------------------
    // BOTTOM BUTTON BAR
    // -------------------------------------------------------------------------
    QHBoxLayout *bottomBar = new QHBoxLayout();
    QPushButton *btnShowPreview = new QPushButton("Show preview", this);
    QPushButton *btnApply = new QPushButton("Apply", this);
    QDialogButtonBox *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    bottomBar->addWidget(btnShowPreview);
    bottomBar->addStretch();
    bottomBar->addWidget(btnApply);
    bottomBar->addWidget(btnBox);

    mainLayout->addLayout(bottomBar);

    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(btnPickColor, &QPushButton::clicked, this, &VDResizeFilterDialog::onPickColorClicked);

    connect(radAbsolute, &QRadioButton::toggled, this, &VDResizeFilterDialog::onSizeOptionChanged);
    connect(radRelative, &QRadioButton::toggled, this, &VDResizeFilterDialog::onSizeOptionChanged);
    connect(radAspectDisabled, &QRadioButton::toggled, this, &VDResizeFilterDialog::onAspectOptionChanged);
    connect(radAspectSame, &QRadioButton::toggled, this, &VDResizeFilterDialog::onAspectOptionChanged);
    connect(radAspectRatio, &QRadioButton::toggled, this, &VDResizeFilterDialog::onAspectOptionChanged);

    connect(spinAbsW, QOverload<int>::of(&QSpinBox::valueChanged), this, &VDResizeFilterDialog::onAbsWChanged);
    connect(spinAbsH, QOverload<int>::of(&QSpinBox::valueChanged), this, &VDResizeFilterDialog::onAbsHChanged);
    connect(spinRelW, QOverload<int>::of(&QSpinBox::valueChanged), this, &VDResizeFilterDialog::onRelWChanged);
    connect(spinRelH, QOverload<int>::of(&QSpinBox::valueChanged), this, &VDResizeFilterDialog::onRelHChanged);

    QLineEdit *leRelW = spinRelW->findChild<QLineEdit*>();
    if (leRelW) {
        connect(leRelW, &QLineEdit::textEdited, this, [this](const QString &) {
            spinRelW->interpretText();
            onRelWChanged(spinRelW->value());
        });
    }
    QLineEdit *leRelH = spinRelH->findChild<QLineEdit*>();
    if (leRelH) {
        connect(leRelH, &QLineEdit::textEdited, this, [this](const QString &) {
            spinRelH->interpretText();
            onRelHChanged(spinRelH->value());
        });
    }
    QLineEdit *leAbsW = spinAbsW->findChild<QLineEdit*>();
    if (leAbsW) {
        connect(leAbsW, &QLineEdit::textEdited, this, [this](const QString &) {
            spinAbsW->interpretText();
            onAbsWChanged(spinAbsW->value());
        });
    }
    QLineEdit *leAbsH = spinAbsH->findChild<QLineEdit*>();
    if (leAbsH) {
        connect(leAbsH, &QLineEdit::textEdited, this, [this](const QString &) {
            spinAbsH->interpretText();
            onAbsHChanged(spinAbsH->value());
        });
    }

    // Radio initial values
    int sizeMode = params.value("sizeMode", 1); // Relative by default in screenshot
    if (sizeMode == 0) radAbsolute->setChecked(true); else radRelative->setChecked(true);

    int aspectMode = params.value("aspectMode", 1); // Same as source
    if (aspectMode == 0) radAspectDisabled->setChecked(true);
    else if (aspectMode == 1) radAspectSame->setChecked(true);
    else radAspectRatio->setChecked(true);

    int framingMode = params.value("framingMode", 0);
    if (framingMode == 0) radFrameNone->setChecked(true);
    else if (framingMode == 1) radFrameSize->setChecked(true);
    else if (framingMode == 2) radFrameCropAspect->setChecked(true);
    else radFrameLetterboxAspect->setChecked(true);

    int codecAdjust = params.value("codecAdjust", 0);
    if (codecAdjust == 2) radCodec2->setChecked(true);
    else if (codecAdjust == 4) radCodec4->setChecked(true);
    else if (codecAdjust == 8) radCodec8->setChecked(true);
    else if (codecAdjust == 16) radCodec16->setChecked(true);
    else radCodecNone->setChecked(true);

    onSizeOptionChanged();
    onAspectOptionChanged();
}

void VDResizeFilterDialog::onSizeOptionChanged() {
    bool isAbs = radAbsolute->isChecked();

    spinAbsW->setEnabled(isAbs);
    spinAbsH->setEnabled(isAbs);

    spinRelW->setEnabled(!isAbs);
    spinRelH->setEnabled(!isAbs);

    updateCalculatedDimensions();
}

void VDResizeFilterDialog::onAspectOptionChanged() {
    spinAspectW->setEnabled(radAspectRatio->isChecked());
    spinAspectH->setEnabled(radAspectRatio->isChecked());

    if (radAspectSame->isChecked()) {
        spinRelH->setValue(spinRelW->value());
    } else if (radAspectRatio->isChecked() && spinAspectW->value() > 0) {
        double ratio = (double)spinAspectH->value() / spinAspectW->value();
        spinRelH->setValue(std::max(1, static_cast<int>(std::round(spinRelW->value() * ratio))));
    }

    updateCalculatedDimensions();
}

void VDResizeFilterDialog::onFramingOptionChanged() {
    spinFrameW->setEnabled(radFrameSize->isChecked());
    spinFrameH->setEnabled(radFrameSize->isChecked());
    spinFrameAspectW->setEnabled(radFrameCropAspect->isChecked() || radFrameLetterboxAspect->isChecked());
    spinFrameAspectH->setEnabled(radFrameCropAspect->isChecked() || radFrameLetterboxAspect->isChecked());
}

void VDResizeFilterDialog::onPickColorClicked() {
    QColor col = QColorDialog::getColor(mFillColor, this, "Select Framing Fill Color");
    if (col.isValid()) {
        mFillColor = col;
        colorSwatch->setStyleSheet(QString("background-color: %1; border: 1px solid #777;").arg(mFillColor.name()));
    }
}

void VDResizeFilterDialog::onRelWChanged(int val) {
    if (mUpdating) return;
    mUpdating = true;

    if (radAspectSame->isChecked()) {
        spinRelH->setValue(val);
    } else if (radAspectRatio->isChecked() && spinAspectW->value() > 0) {
        double ratio = (double)spinAspectH->value() / spinAspectW->value();
        int newRelH = static_cast<int>(std::round(val * ratio));
        spinRelH->setValue(std::max(1, newRelH));
    }

    updateCalculatedDimensions();
    mUpdating = false;
}

void VDResizeFilterDialog::onRelHChanged(int val) {
    if (mUpdating) return;
    mUpdating = true;

    if (radAspectSame->isChecked()) {
        spinRelW->setValue(val);
    } else if (radAspectRatio->isChecked() && spinAspectH->value() > 0) {
        double ratio = (double)spinAspectW->value() / spinAspectH->value();
        int newRelW = static_cast<int>(std::round(val * ratio));
        spinRelW->setValue(std::max(1, newRelW));
    }

    updateCalculatedDimensions();
    mUpdating = false;
}

void VDResizeFilterDialog::onAbsWChanged(int val) {
    if (mUpdating) return;
    mUpdating = true;

    if (radAspectSame->isChecked() && mSourceW > 0) {
        int newH = static_cast<int>(std::round(val * (double)mSourceH / mSourceW));
        spinAbsH->setValue(std::max(16, newH));
    } else if (radAspectRatio->isChecked() && spinAspectW->value() > 0) {
        int newH = static_cast<int>(std::round(val * (double)spinAspectH->value() / spinAspectW->value()));
        spinAbsH->setValue(std::max(16, newH));
    }

    if (mSourceW > 0) spinRelW->setValue(static_cast<int>(std::round(val * 100.0 / mSourceW)));
    if (mSourceH > 0) spinRelH->setValue(static_cast<int>(std::round(spinAbsH->value() * 100.0 / mSourceH)));

    mUpdating = false;
}

void VDResizeFilterDialog::onAbsHChanged(int val) {
    if (mUpdating) return;
    mUpdating = true;

    if (radAspectSame->isChecked() && mSourceH > 0) {
        int newW = static_cast<int>(std::round(val * (double)mSourceW / mSourceH));
        spinAbsW->setValue(std::max(16, newW));
    }

    if (mSourceW > 0) spinRelW->setValue(static_cast<int>(std::round(spinAbsW->value() * 100.0 / mSourceW)));
    if (mSourceH > 0) spinRelH->setValue(static_cast<int>(std::round(val * 100.0 / mSourceH)));

    mUpdating = false;
}

void VDResizeFilterDialog::updateCalculatedDimensions() {
    if (radRelative->isChecked()) {
        int w = static_cast<int>(std::round(mSourceW * (spinRelW->value() / 100.0)));
        int h = static_cast<int>(std::round(mSourceH * (spinRelH->value() / 100.0)));

        spinAbsW->setValue(std::max(16, w));
        spinAbsH->setValue(std::max(16, h));
    } else if (radAbsolute->isChecked()) {
        if (mSourceW > 0) spinRelW->setValue(static_cast<int>(std::round(spinAbsW->value() * 100.0 / mSourceW)));
        if (mSourceH > 0) spinRelH->setValue(static_cast<int>(std::round(spinAbsH->value() * 100.0 / mSourceH)));
    }
}

QMap<QString, double> VDResizeFilterDialog::getParams() const {
    QMap<QString, double> p;
    p["sizeMode"] = radAbsolute->isChecked() ? 0 : 1;
    p["absW"] = spinAbsW->value();
    p["absH"] = spinAbsH->value();
    p["relW"] = spinRelW->value();
    p["relH"] = spinRelH->value();

    int aspectMode = 0;
    if (radAspectSame->isChecked()) aspectMode = 1;
    else if (radAspectRatio->isChecked()) aspectMode = 2;
    p["aspectMode"] = aspectMode;
    p["aspectW"] = spinAspectW->value();
    p["aspectH"] = spinAspectH->value();

    p["filterMode"] = comboFilterMode->currentIndex();
    p["interlaced"] = chkInterlaced->isChecked() ? 1 : 0;

    int framingMode = 0;
    if (radFrameSize->isChecked()) framingMode = 1;
    else if (radFrameCropAspect->isChecked()) framingMode = 2;
    else if (radFrameLetterboxAspect->isChecked()) framingMode = 3;
    p["framingMode"] = framingMode;
    p["frameW"] = spinFrameW->value();
    p["frameH"] = spinFrameH->value();
    p["frameAspectW"] = spinFrameAspectW->value();
    p["frameAspectH"] = spinFrameAspectH->value();

    p["fillColorR"] = mFillColor.red();
    p["fillColorG"] = mFillColor.green();
    p["fillColorB"] = mFillColor.blue();

    int codecAdjust = 0;
    if (radCodec2->isChecked()) codecAdjust = 2;
    else if (radCodec4->isChecked()) codecAdjust = 4;
    else if (radCodec8->isChecked()) codecAdjust = 8;
    else if (radCodec16->isChecked()) codecAdjust = 16;
    p["codecAdjust"] = codecAdjust;

    // Target output dimensions
    int targetW = spinAbsW->value();
    int targetH = spinAbsH->value();

    if (codecAdjust > 1) {
        targetW = (targetW / codecAdjust) * codecAdjust;
        targetH = (targetH / codecAdjust) * codecAdjust;
    }

    p["width"] = targetW;
    p["height"] = targetH;

    return p;
}

// -----------------------------------------------------------------------------
// VDRotateFilterDialog Implementation (Matching Screenshot)
// -----------------------------------------------------------------------------
VDRotateFilterDialog::VDRotateFilterDialog(const QMap<QString, double>& params, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Filter: rotate");
    setStyleSheet(kDialogStyle);
    setFixedSize(220, 160);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    QGroupBox *grpRotate = new QGroupBox("Rotate", this);
    QVBoxLayout *vboxRotate = new QVBoxLayout(grpRotate);
    vboxRotate->setContentsMargins(10, 8, 10, 8);
    vboxRotate->setSpacing(6);

    radLeft90 = new QRadioButton("&Left by 90°", this);
    radRight90 = new QRadioButton("&Right by 90°", this);
    radAround180 = new QRadioButton("&Around 180°", this);

    QButtonGroup *btnGroup = new QButtonGroup(this);
    btnGroup->addButton(radLeft90);
    btnGroup->addButton(radRight90);
    btnGroup->addButton(radAround180);

    vboxRotate->addWidget(radLeft90);
    vboxRotate->addWidget(radRight90);
    vboxRotate->addWidget(radAround180);

    mainLayout->addWidget(grpRotate);

    int mode = static_cast<int>(params.value("mode", 0));
    if (!params.contains("mode") && params.contains("angle")) {
        double angle = params.value("angle", 270);
        if (angle == 90) mode = 1;
        else if (angle == 180) mode = 2;
        else mode = 0;
    }

    if (mode == 1) radRight90->setChecked(true);
    else if (mode == 2) radAround180->setChecked(true);
    else radLeft90->setChecked(true);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);
    QPushButton *btnOk = new QPushButton("OK", this);
    QPushButton *btnCancel = new QPushButton("Cancel", this);
    btnOk->setDefault(true);
    btnOk->setFixedWidth(70);
    btnCancel->setFixedWidth(70);

    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnCancel);

    mainLayout->addLayout(btnLayout);

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

int VDRotateFilterDialog::getMode() const {
    if (radRight90->isChecked()) return 1;
    if (radAround180->isChecked()) return 2;
    return 0;
}

QMap<QString, double> VDRotateFilterDialog::getParams() const {
    QMap<QString, double> p;
    int mode = getMode();
    p["mode"] = mode;
    if (mode == 0) p["angle"] = 270;
    else if (mode == 1) p["angle"] = 90;
    else if (mode == 2) p["angle"] = 180;
    return p;
}

// -----------------------------------------------------------------------------
// VDFilterPreviewDialog Implementation
// -----------------------------------------------------------------------------
VDFilterPreviewDialog::VDFilterPreviewDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Filter Preview");
    setStyleSheet(kDialogStyle);
    resize(640, 500);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    mPreviewLabel = new QLabel(this);
    mPreviewLabel->setAlignment(Qt::AlignCenter);
    mPreviewLabel->setStyleSheet("background-color: #111; border: 1px solid #444; border-radius: 4px;");
    mPreviewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mPreviewLabel->setMinimumSize(320, 240);

    mInfoLabel = new QLabel("Preview", this);
    mInfoLabel->setStyleSheet("color: #888; font-size: 11px;");

    layout->addWidget(mPreviewLabel, 1);
    layout->addWidget(mInfoLabel, 0);
}

void VDFilterPreviewDialog::updatePreviewImage(const QImage &image) {
    mCurrentImage = image;
    mInfoLabel->setText(QString("Preview Size: %1x%2").arg(image.width()).arg(image.height()));
    refreshDisplay();
}

void VDFilterPreviewDialog::refreshDisplay() {
    if (mCurrentImage.isNull()) return;
    QSize labelSize = mPreviewLabel->size();
    if (labelSize.width() <= 0 || labelSize.height() <= 0) return;
    QImage scaled = mCurrentImage.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    mPreviewLabel->setPixmap(QPixmap::fromImage(scaled));
}

void VDFilterPreviewDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    refreshDisplay();
}

void VDFilterPreviewDialog::closeEvent(QCloseEvent *event) {
    QDialog::closeEvent(event);
}

// -----------------------------------------------------------------------------
// VDBrightnessContrastFilterDialog Implementation (Matching Screenshot)
// -----------------------------------------------------------------------------
VDBrightnessContrastFilterDialog::VDBrightnessContrastFilterDialog(const QMap<QString, double>& params, const QImage &sourceFrame, QWidget *parent)
    : QDialog(parent), mSourceFrame(sourceFrame), mPreviewDialog(nullptr) {
    setWindowTitle("Filter: brightness/contrast");
    setStyleSheet(kDialogStyle);
    setFixedSize(380, 190);

    if (mSourceFrame.isNull()) {
        mSourceFrame = QImage(640, 480, QImage::Format_RGB888);
        mSourceFrame.fill(Qt::black);
        QPainter p(&mSourceFrame);
        QColor colors[] = { Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue, Qt::black };
        int barW = 640 / 8;
        for (int i = 0; i < 8; ++i) {
            p.fillRect(i * barW, 0, barW, 240, colors[i]);
        }
        for (int x = 0; x < 640; ++x) {
            int g = (x * 255) / 640;
            p.setPen(QColor(g, g, g));
            p.drawLine(x, 240, x, 480);
        }
        p.end();
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(8);

    // ROW 1: BRIGHTNESS
    QHBoxLayout *hBright = new QHBoxLayout();
    QLabel *lblBright = new QLabel("&Brightness", this);
    lblBright->setFixedWidth(75);

    QVBoxLayout *vBrightSlider = new QVBoxLayout();
    vBrightSlider->setSpacing(2);

    sliderBrightness = new QSlider(Qt::Horizontal, this);
    sliderBrightness->setRange(-256, 256);
    sliderBrightness->setTickInterval(32);
    sliderBrightness->setTickPosition(QSlider::TicksBelow);
    sliderBrightness->setValue(static_cast<int>(params.value("bright", 0)));
    lblBright->setBuddy(sliderBrightness);

    QHBoxLayout *hBrightLabels = new QHBoxLayout();
    QLabel *lblBlack = new QLabel("Black", this);
    lblBlack->setStyleSheet("font-size: 11px; color: #888;");
    QLabel *lblNormalB = new QLabel("Normal", this);
    lblNormalB->setStyleSheet("font-size: 11px; color: #888;");
    lblNormalB->setAlignment(Qt::AlignCenter);
    QLabel *lblWhite = new QLabel("White", this);
    lblWhite->setStyleSheet("font-size: 11px; color: #888;");
    lblWhite->setAlignment(Qt::AlignRight);

    hBrightLabels->addWidget(lblBlack);
    hBrightLabels->addWidget(lblNormalB);
    hBrightLabels->addWidget(lblWhite);

    vBrightSlider->addWidget(sliderBrightness);
    vBrightSlider->addLayout(hBrightLabels);

    hBright->addWidget(lblBright);
    hBright->addLayout(vBrightSlider);
    mainLayout->addLayout(hBright);

    // ROW 2: CONTRAST
    QHBoxLayout *hCont = new QHBoxLayout();
    QLabel *lblCont = new QLabel("&Contrast", this);
    lblCont->setFixedWidth(75);

    QVBoxLayout *vContSlider = new QVBoxLayout();
    vContSlider->setSpacing(2);

    sliderContrast = new QSlider(Qt::Horizontal, this);
    sliderContrast->setRange(0, 32);
    sliderContrast->setTickInterval(4);
    sliderContrast->setTickPosition(QSlider::TicksBelow);
    sliderContrast->setValue(static_cast<int>(params.value("cont", 16)));
    lblCont->setBuddy(sliderContrast);

    QHBoxLayout *hContLabels = new QHBoxLayout();
    QLabel *lbl0 = new QLabel("0%", this);
    lbl0->setStyleSheet("font-size: 11px; color: #888;");
    QLabel *lbl100 = new QLabel("100%", this);
    lbl100->setStyleSheet("font-size: 11px; color: #888;");
    lbl100->setAlignment(Qt::AlignCenter);
    QLabel *lbl200 = new QLabel("200%", this);
    lbl200->setStyleSheet("font-size: 11px; color: #888;");
    lbl200->setAlignment(Qt::AlignRight);

    hContLabels->addWidget(lbl0);
    hContLabels->addWidget(lbl100);
    hContLabels->addWidget(lbl200);

    vContSlider->addWidget(sliderContrast);
    vContSlider->addLayout(hContLabels);

    hCont->addWidget(lblCont);
    hCont->addLayout(vContSlider);
    mainLayout->addLayout(hCont);

    mainLayout->addSpacing(4);

    // ROW 3: BUTTONS
    QHBoxLayout *hButtons = new QHBoxLayout();
    btnShowPreview = new QPushButton("Show preview", this);
    btnShowPreview->setEnabled(true);
    btnShowPreview->setFixedWidth(95);

    btnOk = new QPushButton("OK", this);
    btnOk->setDefault(true);
    btnOk->setFixedWidth(75);

    btnCancel = new QPushButton("Cancel", this);
    btnCancel->setFixedWidth(75);

    hButtons->addWidget(btnShowPreview);
    hButtons->addStretch();
    hButtons->addWidget(btnOk);
    hButtons->addWidget(btnCancel);

    mainLayout->addLayout(hButtons);

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnShowPreview, &QPushButton::clicked, this, &VDBrightnessContrastFilterDialog::onTogglePreviewClicked);
    connect(sliderBrightness, &QSlider::valueChanged, this, &VDBrightnessContrastFilterDialog::onSliderValueChanged);
    connect(sliderContrast, &QSlider::valueChanged, this, &VDBrightnessContrastFilterDialog::onSliderValueChanged);
}

VDBrightnessContrastFilterDialog::~VDBrightnessContrastFilterDialog() {
    if (mPreviewDialog) {
        mPreviewDialog->close();
        delete mPreviewDialog;
        mPreviewDialog = nullptr;
    }
}

void VDBrightnessContrastFilterDialog::onTogglePreviewClicked() {
    if (!mPreviewDialog) {
        mPreviewDialog = new VDFilterPreviewDialog(this);
    }

    if (mPreviewDialog->isVisible()) {
        mPreviewDialog->hide();
        btnShowPreview->setText("Show preview");
    } else {
        mPreviewDialog->show();
        mPreviewDialog->raise();
        btnShowPreview->setText("Hide preview");
        updatePreviewImage();
    }
}

void VDBrightnessContrastFilterDialog::onSliderValueChanged() {
    if (mPreviewDialog && mPreviewDialog->isVisible()) {
        updatePreviewImage();
    }
}

void VDBrightnessContrastFilterDialog::updatePreviewImage() {
    if (mSourceFrame.isNull() || !mPreviewDialog) return;

    int bright = sliderBrightness->value();
    int cont = sliderContrast->value();

    float bias = bright - 0.5f;
    float scale = static_cast<float>(cont) / 16.0f;

    uint8_t table[256];
    int32_t y0 = static_cast<int32_t>(std::round(bias * 65536.0f)) + 0x8000;
    int32_t dydx = static_cast<int32_t>(std::round(scale * 65536.0f));

    for (int i = 0; i < 256; ++i) {
        int y = y0 >> 16;
        y0 += dydx;
        table[i] = static_cast<uint8_t>(std::clamp(y, 0, 255));
    }

    QImage processed = mSourceFrame.convertToFormat(QImage::Format_RGB888);
    int h = processed.height();
    int w = processed.width();

    for (int y = 0; y < h; ++y) {
        uchar *scan = processed.scanLine(y);
        for (int x = 0; x < w * 3; ++x) {
            scan[x] = table[scan[x]];
        }
    }

    mPreviewDialog->updatePreviewImage(processed);
}

QMap<QString, double> VDBrightnessContrastFilterDialog::getParams() const {
    QMap<QString, double> p;
    p["bright"] = sliderBrightness->value();
    p["cont"] = sliderContrast->value();
    return p;
}

// -----------------------------------------------------------------------------
// VDBoxBlurFilterDialog Implementation (Matching Screenshot)
// -----------------------------------------------------------------------------
VDBoxBlurFilterDialog::VDBoxBlurFilterDialog(const QMap<QString, double>& params, const QImage &sourceFrame, QWidget *parent)
    : QDialog(parent), mSourceFrame(sourceFrame), mPreviewDialog(nullptr) {
    setWindowTitle("Filter: box blur");
    setStyleSheet(kDialogStyle);
    setFixedSize(380, 190);

    if (mSourceFrame.isNull()) {
        mSourceFrame = QImage(640, 480, QImage::Format_RGB888);
        mSourceFrame.fill(Qt::black);
        QPainter p(&mSourceFrame);
        QColor colors[] = { Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue, Qt::black };
        int barW = 640 / 8;
        for (int i = 0; i < 8; ++i) {
            p.fillRect(i * barW, 0, barW, 240, colors[i]);
        }
        for (int x = 0; x < 640; ++x) {
            int g = (x * 255) / 640;
            p.setPen(QColor(g, g, g));
            p.drawLine(x, 240, x, 480);
        }
        p.end();
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(8);

    // ROW 1: RADIUS / WIDTH
    QHBoxLayout *hRadius = new QHBoxLayout();
    QLabel *lblRadius = new QLabel("&Filter", this);
    lblRadius->setFixedWidth(50);

    sliderRadius = new QSlider(Qt::Horizontal, this);
    sliderRadius->setRange(1, 48);
    sliderRadius->setTickInterval(4);
    sliderRadius->setTickPosition(QSlider::TicksBelow);
    int initialWidth = static_cast<int>(params.value("width", 1));
    if (initialWidth < 1) initialWidth = 1;
    sliderRadius->setValue(initialWidth);
    lblRadius->setBuddy(sliderRadius);

    lblRadiusValue = new QLabel("radius 1", this);
    lblRadiusValue->setFixedWidth(80);
    lblRadiusValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    hRadius->addWidget(lblRadius);
    hRadius->addWidget(sliderRadius);
    hRadius->addWidget(lblRadiusValue);
    mainLayout->addLayout(hRadius);

    // ROW 2: POWER
    QHBoxLayout *hPower = new QHBoxLayout();
    QLabel *lblPower = new QLabel("&Filter", this);
    lblPower->setFixedWidth(50);

    sliderPower = new QSlider(Qt::Horizontal, this);
    sliderPower->setRange(1, 3);
    sliderPower->setTickInterval(1);
    sliderPower->setTickPosition(QSlider::TicksBelow);
    int initialPower = static_cast<int>(params.value("power", 1));
    if (initialPower < 1 || initialPower > 3) initialPower = 1;
    sliderPower->setValue(initialPower);
    lblPower->setBuddy(sliderPower);

    lblPowerValue = new QLabel("1 - box", this);
    lblPowerValue->setFixedWidth(80);
    lblPowerValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    hPower->addWidget(lblPower);
    hPower->addWidget(sliderPower);
    hPower->addWidget(lblPowerValue);
    mainLayout->addLayout(hPower);

    mainLayout->addSpacing(4);

    // ROW 3: BUTTONS
    QHBoxLayout *hButtons = new QHBoxLayout();
    btnShowPreview = new QPushButton("Show preview", this);
    btnShowPreview->setEnabled(true);
    btnShowPreview->setFixedWidth(95);

    btnOk = new QPushButton("OK", this);
    btnOk->setDefault(true);
    btnOk->setFixedWidth(75);

    btnCancel = new QPushButton("Cancel", this);
    btnCancel->setFixedWidth(75);

    hButtons->addWidget(btnShowPreview);
    hButtons->addStretch();
    hButtons->addWidget(btnOk);
    hButtons->addWidget(btnCancel);

    mainLayout->addLayout(hButtons);

    updateLabels();

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnShowPreview, &QPushButton::clicked, this, &VDBoxBlurFilterDialog::onTogglePreviewClicked);
    connect(sliderRadius, &QSlider::valueChanged, this, &VDBoxBlurFilterDialog::onSliderValueChanged);
    connect(sliderPower, &QSlider::valueChanged, this, &VDBoxBlurFilterDialog::onSliderValueChanged);
}

VDBoxBlurFilterDialog::~VDBoxBlurFilterDialog() {
    if (mPreviewDialog) {
        mPreviewDialog->close();
        delete mPreviewDialog;
        mPreviewDialog = nullptr;
    }
}

void VDBoxBlurFilterDialog::updateLabels() {
    int w = sliderRadius->value();
    int p = sliderPower->value();
    int rad = w + p - 1;
    lblRadiusValue->setText(QString("radius %1").arg(rad));

    static const char *const szPowers[] = { "1 - box", "2 - quadratic", "3 - cubic" };
    if (p >= 1 && p <= 3) {
        lblPowerValue->setText(szPowers[p - 1]);
    }
}

void VDBoxBlurFilterDialog::onSliderValueChanged() {
    updateLabels();
    if (mPreviewDialog && mPreviewDialog->isVisible()) {
        updatePreviewImage();
    }
}

void VDBoxBlurFilterDialog::onTogglePreviewClicked() {
    if (!mPreviewDialog) {
        mPreviewDialog = new VDFilterPreviewDialog(this);
    }

    if (mPreviewDialog->isVisible()) {
        mPreviewDialog->hide();
        btnShowPreview->setText("Show preview");
    } else {
        mPreviewDialog->show();
        mPreviewDialog->raise();
        btnShowPreview->setText("Hide preview");
        updatePreviewImage();
    }
}

void VDBoxBlurFilterDialog::updatePreviewImage() {
    if (mSourceFrame.isNull() || !mPreviewDialog) return;

    QMap<QString, double> p = getParams();
    int width = static_cast<int>(p.value("width", 1));
    int power = static_cast<int>(p.value("power", 1));

    QImage blurImage = mSourceFrame.convertToFormat(QImage::Format_RGB888);

    auto boxBlurPass = [](QImage &img, int radius) {
        if (radius <= 0) return;
        int w = img.width();
        int h = img.height();
        QImage temp = img;
        int winSize = 2 * radius + 1;

        // Horiz
        for (int y = 0; y < h; ++y) {
            const uchar *srcRow = img.constScanLine(y);
            uchar *dstRow = temp.scanLine(y);
            for (int c = 0; c < 3; ++c) {
                int sum = 0;
                for (int x = -radius; x <= radius; ++x) {
                    int cx = std::clamp(x, 0, w - 1);
                    sum += srcRow[cx * 3 + c];
                }
                for (int x = 0; x < w; ++x) {
                    dstRow[x * 3 + c] = static_cast<uchar>(sum / winSize);
                    int lx = std::clamp(x - radius, 0, w - 1);
                    int rx = std::clamp(x + radius + 1, 0, w - 1);
                    sum += srcRow[rx * 3 + c] - srcRow[lx * 3 + c];
                }
            }
        }

        // Vert
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) {
                int sum = 0;
                for (int y = -radius; y <= radius; ++y) {
                    int cy = std::clamp(y, 0, h - 1);
                    sum += temp.constScanLine(cy)[x * 3 + c];
                }
                for (int y = 0; y < h; ++y) {
                    img.scanLine(y)[x * 3 + c] = static_cast<uchar>(sum / winSize);
                    int ty = std::clamp(y - radius, 0, h - 1);
                    int by = std::clamp(y + radius + 1, 0, h - 1);
                    sum += temp.constScanLine(by)[x * 3 + c] - temp.constScanLine(ty)[x * 3 + c];
                }
            }
        }
    };

    for (int i = 0; i < power; ++i) {
        boxBlurPass(blurImage, width);
    }

    mPreviewDialog->updatePreviewImage(blurImage);
}

QMap<QString, double> VDBoxBlurFilterDialog::getParams() const {
    QMap<QString, double> p;
    int w = sliderRadius->value();
    int pow = sliderPower->value();
    p["width"] = w;
    p["power"] = pow;
    p["radius"] = w + pow - 1;
    return p;
}

// -----------------------------------------------------------------------------
// VDSharpenFilterDialog Implementation (Matching Screenshot)
// -----------------------------------------------------------------------------
VDSharpenFilterDialog::VDSharpenFilterDialog(const QMap<QString, double>& params, const QImage &sourceFrame, QWidget *parent)
    : QDialog(parent), mSourceFrame(sourceFrame), mPreviewDialog(nullptr) {
    setWindowTitle("Dialog"); // Or "Filter: sharpen" matching screenshot
    setStyleSheet(kDialogStyle);
    setFixedSize(360, 160);

    if (mSourceFrame.isNull()) {
        mSourceFrame = QImage(640, 480, QImage::Format_RGB888);
        mSourceFrame.fill(Qt::black);
        QPainter p(&mSourceFrame);
        QColor colors[] = { Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue, Qt::black };
        int barW = 640 / 8;
        for (int i = 0; i < 8; ++i) {
            p.fillRect(i * barW, 0, barW, 240, colors[i]);
        }
        for (int x = 0; x < 640; ++x) {
            int g = (x * 255) / 640;
            p.setPen(QColor(g, g, g));
            p.drawLine(x, 240, x, 480);
        }
        p.end();
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(8);

    // ROW 1: SLIDER + VALUE
    QHBoxLayout *hSliderRow = new QHBoxLayout();
    sliderSharpen = new QSlider(Qt::Horizontal, this);
    sliderSharpen->setRange(0, 64);
    sliderSharpen->setTickInterval(2);
    sliderSharpen->setTickPosition(QSlider::TicksBelow);
    int initialVal = static_cast<int>(params.value("amount", 16));
    if (initialVal < 0) initialVal = 0;
    if (initialVal > 64) initialVal = 64;
    sliderSharpen->setValue(initialVal);

    lblSharpenValue = new QLabel(QString::number(initialVal), this);
    lblSharpenValue->setFixedWidth(30);
    lblSharpenValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    hSliderRow->addWidget(sliderSharpen);
    hSliderRow->addWidget(lblSharpenValue);

    QVBoxLayout *vSliderBlock = new QVBoxLayout();
    vSliderBlock->setSpacing(2);
    vSliderBlock->addLayout(hSliderRow);

    QHBoxLayout *hLabels = new QHBoxLayout();
    QLabel *lblNone = new QLabel("None", this);
    lblNone->setStyleSheet("font-size: 11px; color: #888;");
    QLabel *lblMax = new QLabel("Maximum", this);
    lblMax->setStyleSheet("font-size: 11px; color: #888;");
    lblMax->setAlignment(Qt::AlignRight);

    hLabels->addWidget(lblNone);
    hLabels->addStretch();
    hLabels->addWidget(lblMax);
    hLabels->addSpacing(35); // aligns under slider track right edge

    vSliderBlock->addLayout(hLabels);
    mainLayout->addLayout(vSliderBlock);

    mainLayout->addSpacing(4);

    // ROW 2: BUTTONS
    QHBoxLayout *hButtons = new QHBoxLayout();
    btnShowPreview = new QPushButton("Show preview", this);
    btnShowPreview->setEnabled(true);
    btnShowPreview->setFixedWidth(95);

    btnOk = new QPushButton("OK", this);
    btnOk->setDefault(true);
    btnOk->setFixedWidth(75);

    btnCancel = new QPushButton("Cancel", this);
    btnCancel->setFixedWidth(75);

    hButtons->addWidget(btnShowPreview);
    hButtons->addStretch();
    hButtons->addWidget(btnOk);
    hButtons->addWidget(btnCancel);

    mainLayout->addLayout(hButtons);

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnShowPreview, &QPushButton::clicked, this, &VDSharpenFilterDialog::onTogglePreviewClicked);
    connect(sliderSharpen, &QSlider::valueChanged, this, &VDSharpenFilterDialog::onSliderValueChanged);
}

VDSharpenFilterDialog::~VDSharpenFilterDialog() {
    if (mPreviewDialog) {
        mPreviewDialog->close();
        delete mPreviewDialog;
        mPreviewDialog = nullptr;
    }
}

void VDSharpenFilterDialog::onSliderValueChanged() {
    lblSharpenValue->setText(QString::number(sliderSharpen->value()));
    if (mPreviewDialog && mPreviewDialog->isVisible()) {
        updatePreviewImage();
    }
}

void VDSharpenFilterDialog::onTogglePreviewClicked() {
    if (!mPreviewDialog) {
        mPreviewDialog = new VDFilterPreviewDialog(this);
    }

    if (mPreviewDialog->isVisible()) {
        mPreviewDialog->hide();
        btnShowPreview->setText("Show preview");
    } else {
        mPreviewDialog->show();
        mPreviewDialog->raise();
        btnShowPreview->setText("Hide preview");
        updatePreviewImage();
    }
}

void VDSharpenFilterDialog::updatePreviewImage() {
    if (mSourceFrame.isNull() || !mPreviewDialog) return;

    int v = sliderSharpen->value();
    QImage processed = mSourceFrame.convertToFormat(QImage::Format_RGB888);
    if (v > 0) {
        int w = processed.width();
        int h = processed.height();
        QImage temp = processed;
        int centerWeight = 256 + 8 * v;

        for (int y = 0; y < h; ++y) {
            uchar *dstRow = processed.scanLine(y);
            int yPrev = std::clamp(y - 1, 0, h - 1);
            int yNext = std::clamp(y + 1, 0, h - 1);

            const uchar *srcRowPrev = temp.constScanLine(yPrev);
            const uchar *srcRowCurr = temp.constScanLine(y);
            const uchar *srcRowNext = temp.constScanLine(yNext);

            for (int x = 0; x < w; ++x) {
                int xPrev = std::clamp(x - 1, 0, w - 1);
                int xNext = std::clamp(x + 1, 0, w - 1);

                for (int c = 0; c < 3; ++c) {
                    int centerVal = srcRowCurr[x * 3 + c];
                    int sumNeighbors =
                        srcRowPrev[xPrev * 3 + c] + srcRowPrev[x * 3 + c] + srcRowPrev[xNext * 3 + c] +
                        srcRowCurr[xPrev * 3 + c]                         + srcRowCurr[xNext * 3 + c] +
                        srcRowNext[xPrev * 3 + c] + srcRowNext[x * 3 + c] + srcRowNext[xNext * 3 + c];

                    int val = (centerVal * centerWeight - sumNeighbors * v + 128) >> 8;
                    dstRow[x * 3 + c] = static_cast<uchar>(std::clamp(val, 0, 255));
                }
            }
        }
    }

    mPreviewDialog->updatePreviewImage(processed);
}

QMap<QString, double> VDSharpenFilterDialog::getParams() const {
    QMap<QString, double> p;
    p["amount"] = sliderSharpen->value();
    return p;
}

// -----------------------------------------------------------------------------
// VD6AxisFilterDialog Implementation (Matching Screenshot)
// -----------------------------------------------------------------------------
static QWidget* createAxisSliderRow(const QString &title, QSlider *slider, QLabel *lblValue, const QString &gradientCss, const QString &minLabel = "0", const QString &midLabel = "0.5", const QString &maxLabel = "1") {
    QWidget *container = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 1, 0, 1);
    layout->setSpacing(1);

    QHBoxLayout *hTop = new QHBoxLayout();
    QLabel *lblTitle = new QLabel(title, container);
    lblTitle->setStyleSheet("font-weight: 500; font-size: 11px;");
    lblTitle->setFixedWidth(85);

    QLabel *lblMin = new QLabel(minLabel, container);
    lblMin->setStyleSheet("font-size: 10px; color: #888;");
    QLabel *lblMid = new QLabel(midLabel, container);
    lblMid->setStyleSheet("font-size: 10px; color: #888;");
    lblMid->setAlignment(Qt::AlignCenter);
    QLabel *lblMax = new QLabel(maxLabel, container);
    lblMax->setStyleSheet("font-size: 10px; color: #888;");
    lblMax->setAlignment(Qt::AlignRight);

    lblValue->setFixedWidth(40);
    lblValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lblValue->setStyleSheet("font-size: 11px; background: #2b2b2b; border: 1px solid #444; padding: 1px 3px; border-radius: 2px;");

    hTop->addWidget(lblTitle);
    hTop->addWidget(lblMin);
    hTop->addStretch();
    hTop->addWidget(lblMid);
    hTop->addStretch();
    hTop->addWidget(lblMax);
    hTop->addSpacing(8);
    hTop->addWidget(lblValue);
    layout->addLayout(hTop);

    slider->setStyleSheet(QString(
        "QSlider::groove:horizontal {"
        "   height: 8px;"
        "   background: %1;"
        "   border: 1px solid #444;"
        "   border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "   background: #e0e0e0;"
        "   border: 1px solid #222;"
        "   width: 10px;"
        "   margin-top: -4px;"
        "   margin-bottom: -4px;"
        "   border-radius: 2px;"
        "}"
    ).arg(gradientCss));

    layout->addWidget(slider);
    return container;
}

VD6AxisFilterDialog::VD6AxisFilterDialog(const QMap<QString, double>& params, const QImage &sourceFrame, QWidget *parent)
    : QDialog(parent), mSourceFrame(sourceFrame), mPreviewDialog(nullptr) {
    setWindowTitle("filter: 6-axis color correction");
    setStyleSheet(kDialogStyle);
    setFixedSize(460, 560);

    if (mSourceFrame.isNull()) {
        mSourceFrame = QImage(640, 480, QImage::Format_RGB888);
        mSourceFrame.fill(Qt::black);
        QPainter p(&mSourceFrame);
        QColor colors[] = { Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue, Qt::black };
        int barW = 640 / 8;
        for (int i = 0; i < 8; ++i) {
            p.fillRect(i * barW, 0, barW, 240, colors[i]);
        }
        for (int x = 0; x < 640; ++x) {
            int g = (x * 255) / 640;
            p.setPen(QColor(g, g, g));
            p.drawLine(x, 240, x, 480);
        }
        p.end();
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 10, 14, 10);
    mainLayout->setSpacing(4);

    auto createSlider = [this](int minVal, int maxVal, int curVal) -> QSlider* {
        QSlider *s = new QSlider(Qt::Horizontal, this);
        s->setRange(minVal, maxVal);
        s->setValue(curVal);
        connect(s, &QSlider::valueChanged, this, &VD6AxisFilterDialog::onSliderValueChanged);
        return s;
    };

    // 1. Intensity (0..2.0, default 1.0 -> 0..200)
    int intVal = static_cast<int>(std::round(params.value("intensity", 1.0) * 100.0));
    sliderIntensity = createSlider(0, 200, intVal);
    QLabel *lblIntensityVal = new QLabel(QString::number(params.value("intensity", 1.0), 'f', 2), this);
    mainLayout->addWidget(createAxisSliderRow("Intensity", sliderIntensity, lblIntensityVal, "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #000000, stop:1 #ffffff)", "0", "0.5", "1"));

    // 2. Red-Green (-1.0..1.0, default 0.0 -> -100..100)
    int rgVal = static_cast<int>(std::round(params.value("red_green", 0.0) * 100.0));
    sliderRedGreen = createSlider(-100, 100, rgVal);
    QLabel *lblRedGreenVal = new QLabel(QString::number(params.value("red_green", 0.0), 'f', 2), this);
    mainLayout->addWidget(createAxisSliderRow("Red-Green", sliderRedGreen, lblRedGreenVal, "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ff2020, stop:1 #20ff20)", "", "0", ""));

    // 3. Yellow-Blue (-1.0..1.0, default 0.0 -> -100..100)
    int ybVal = static_cast<int>(std::round(params.value("yellow_blue", 0.0) * 100.0));
    sliderYellowBlue = createSlider(-100, 100, ybVal);
    QLabel *lblYellowBlueVal = new QLabel(QString::number(params.value("yellow_blue", 0.0), 'f', 2), this);
    mainLayout->addWidget(createAxisSliderRow("Yellow-Blue", sliderYellowBlue, lblYellowBlueVal, "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffff20, stop:1 #0080ff)", "", "0", ""));

    // 4. Saturation (0..2.0, default 1.0 -> 0..200)
    int satVal = static_cast<int>(std::round(params.value("saturation", 1.0) * 100.0));
    sliderSaturation = createSlider(0, 200, satVal);
    QLabel *lblSaturationVal = new QLabel(QString::number(params.value("saturation", 1.0), 'f', 2), this);
    mainLayout->addWidget(createAxisSliderRow("Saturation", sliderSaturation, lblSaturationVal, "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #000000, stop:1 #ffffff)", "", "1", ""));

    // 5. Red (0..2.0, default 1.0 -> 0..200)
    int redVal = static_cast<int>(std::round(params.value("red", 1.0) * 100.0));
    sliderRed = createSlider(0, 200, redVal);
    QLabel *lblRedVal = new QLabel(QString::number(params.value("red", 1.0), 'f', 2), this);
    mainLayout->addWidget(createAxisSliderRow("Red", sliderRed, lblRedVal, "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #ff2020)", "", "0.5", "1"));

    // 6. Orange (0..2.0, default 1.0 -> 0..200)
    int orgVal = static_cast<int>(std::round(params.value("orange", 1.0) * 100.0));
    sliderOrange = createSlider(0, 200, orgVal);
    QLabel *lblOrangeVal = new QLabel(QString::number(params.value("orange", 1.0), 'f', 2), this);
    mainLayout->addWidget(createAxisSliderRow("Orange", sliderOrange, lblOrangeVal, "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #ffa500)", "", "0.5", "1"));

    // 7. Lime (0..2.0, default 1.0 -> 0..200)
    int limeVal = static_cast<int>(std::round(params.value("lime", 1.0) * 100.0));
    sliderLime = createSlider(0, 200, limeVal);
    QLabel *lblLimeVal = new QLabel(QString::number(params.value("lime", 1.0), 'f', 2), this);
    mainLayout->addWidget(createAxisSliderRow("Lime", sliderLime, lblLimeVal, "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #7fff00)", "", "0.5", "1"));

    // 8. Emerald (0..2.0, default 1.0 -> 0..200)
    int emVal = static_cast<int>(std::round(params.value("emerald", 1.0) * 100.0));
    sliderEmerald = createSlider(0, 200, emVal);
    QLabel *lblEmeraldVal = new QLabel(QString::number(params.value("emerald", 1.0), 'f', 2), this);
    mainLayout->addWidget(createAxisSliderRow("Emerald", sliderEmerald, lblEmeraldVal, "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #00ff80)", "", "0.5", "1"));

    // 9. Blue (0..2.0, default 1.0 -> 0..200)
    int blueVal = static_cast<int>(std::round(params.value("blue", 1.0) * 100.0));
    sliderBlue = createSlider(0, 200, blueVal);
    QLabel *lblBlueVal = new QLabel(QString::number(params.value("blue", 1.0), 'f', 2), this);
    mainLayout->addWidget(createAxisSliderRow("Blue", sliderBlue, lblBlueVal, "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #0080ff)", "", "0.5", "1"));

    // 10. Purple (0..2.0, default 1.0 -> 0..200)
    int purVal = static_cast<int>(std::round(params.value("purple", 1.0) * 100.0));
    sliderPurple = createSlider(0, 200, purVal);
    QLabel *lblPurpleVal = new QLabel(QString::number(params.value("purple", 1.0), 'f', 2), this);
    mainLayout->addWidget(createAxisSliderRow("Purple", sliderPurple, lblPurpleVal, "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #b020ff)", "", "0.5", "1"));

    mainLayout->addSpacing(4);

    // ROW 11: BUTTONS
    QHBoxLayout *hButtons = new QHBoxLayout();
    btnShowPreview = new QPushButton("Show preview", this);
    btnShowPreview->setEnabled(true);
    btnShowPreview->setFixedWidth(95);

    btnOk = new QPushButton("OK", this);
    btnOk->setDefault(true);
    btnOk->setFixedWidth(75);

    btnCancel = new QPushButton("Cancel", this);
    btnCancel->setFixedWidth(75);

    hButtons->addWidget(btnShowPreview);
    hButtons->addStretch();
    hButtons->addWidget(btnOk);
    hButtons->addWidget(btnCancel);

    mainLayout->addLayout(hButtons);

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnShowPreview, &QPushButton::clicked, this, &VD6AxisFilterDialog::onTogglePreviewClicked);
}

VD6AxisFilterDialog::~VD6AxisFilterDialog() {
    if (mPreviewDialog) {
        mPreviewDialog->close();
        delete mPreviewDialog;
        mPreviewDialog = nullptr;
    }
}

void VD6AxisFilterDialog::onSliderValueChanged() {
    if (mPreviewDialog && mPreviewDialog->isVisible()) {
        updatePreviewImage();
    }
}

void VD6AxisFilterDialog::onTogglePreviewClicked() {
    if (!mPreviewDialog) {
        mPreviewDialog = new VDFilterPreviewDialog(this);
    }

    if (mPreviewDialog->isVisible()) {
        mPreviewDialog->hide();
        btnShowPreview->setText("Show preview");
    } else {
        mPreviewDialog->show();
        mPreviewDialog->raise();
        btnShowPreview->setText("Hide preview");
        updatePreviewImage();
    }
}

void VD6AxisFilterDialog::updatePreviewImage() {
    if (mSourceFrame.isNull() || !mPreviewDialog) return;

    QMap<QString, double> p = getParams();
    float intensity = static_cast<float>(p.value("intensity", 1.0));
    float redGreen = static_cast<float>(p.value("red_green", 0.0));
    float yellowBlue = static_cast<float>(p.value("yellow_blue", 0.0));
    float satGlobal = static_cast<float>(p.value("saturation", 1.0));
    float redGain = static_cast<float>(p.value("red", 1.0));
    float orangeGain = static_cast<float>(p.value("orange", 1.0));
    float limeGain = static_cast<float>(p.value("lime", 1.0));
    float emeraldGain = static_cast<float>(p.value("emerald", 1.0));
    float blueGain = static_cast<float>(p.value("blue", 1.0));
    float purpleGain = static_cast<float>(p.value("purple", 1.0));

    QImage processed = mSourceFrame.convertToFormat(QImage::Format_RGB888);
    int h = processed.height();
    int w = processed.width();

    const float axesAngles[6] = { 0.0f, 30.0f, 90.0f, 180.0f, 240.0f, 300.0f };
    const float axesGains[6] = { redGain, orangeGain, limeGain, emeraldGain, blueGain, purpleGain };

    for (int y = 0; y < h; ++y) {
        uchar *scan = processed.scanLine(y);
        for (int x = 0; x < w; ++x) {
            float r = scan[x * 3 + 0] / 255.0f;
            float g = scan[x * 3 + 1] / 255.0f;
            float b = scan[x * 3 + 2] / 255.0f;

            float cmax = std::max(r, std::max(g, b));
            float cmin = std::min(r, std::min(g, b));
            float delta = cmax - cmin;

            float H = 0.0f;
            float S = (cmax > 1e-5f) ? (delta / cmax) : 0.0f;
            float V = cmax;

            if (delta > 1e-5f) {
                if (cmax == r) {
                    H = 60.0f * std::fmod(((g - b) / delta) + 6.0f, 6.0f);
                } else if (cmax == g) {
                    H = 60.0f * (((b - r) / delta) + 2.0f);
                } else {
                    H = 60.0f * (((r - g) / delta) + 4.0f);
                }
            }

            float axisMod = 0.0f;
            for (int k = 0; k < 6; ++k) {
                float diff = std::abs(H - axesAngles[k]);
                if (diff > 180.0f) diff = 360.0f - diff;
                if (diff < 60.0f) {
                    float weight = 1.0f - (diff / 60.0f);
                    axisMod += weight * (axesGains[k] - 1.0f);
                }
            }

            float newS = std::clamp(S * satGlobal * (1.0f + axisMod), 0.0f, 1.0f);

            float C = V * newS;
            float X = C * (1.0f - std::abs(std::fmod(H / 60.0f, 2.0f) - 1.0f));
            float m = V - C;

            float nr = 0.0f, ng = 0.0f, nb = 0.0f;
            if (H < 60.0f)       { nr = C; ng = X; nb = 0.0f; }
            else if (H < 120.0f) { nr = X; ng = C; nb = 0.0f; }
            else if (H < 180.0f) { nr = 0.0f; ng = C; nb = X; }
            else if (H < 240.0f) { nr = 0.0f; ng = X; nb = C; }
            else if (H < 300.0f) { nr = X; ng = 0.0f; nb = C; }
            else                 { nr = C; ng = 0.0f; nb = X; }

            nr = (nr + m) * intensity + redGreen * 0.15f + yellowBlue * 0.08f;
            ng = (ng + m) * intensity - redGreen * 0.15f + yellowBlue * 0.08f;
            nb = (nb + m) * intensity - yellowBlue * 0.16f;

            scan[x * 3 + 0] = static_cast<uchar>(std::clamp(std::round(nr * 255.0f), 0.0f, 255.0f));
            scan[x * 3 + 1] = static_cast<uchar>(std::clamp(std::round(ng * 255.0f), 0.0f, 255.0f));
            scan[x * 3 + 2] = static_cast<uchar>(std::clamp(std::round(nb * 255.0f), 0.0f, 255.0f));
        }
    }

    mPreviewDialog->updatePreviewImage(processed);
}

QMap<QString, double> VD6AxisFilterDialog::getParams() const {
    QMap<QString, double> p;
    p["intensity"] = sliderIntensity->value() / 100.0;
    p["red_green"] = sliderRedGreen->value() / 100.0;
    p["yellow_blue"] = sliderYellowBlue->value() / 100.0;
    p["saturation"] = sliderSaturation->value() / 100.0;
    p["red"] = sliderRed->value() / 100.0;
    p["orange"] = sliderOrange->value() / 100.0;
    p["lime"] = sliderLime->value() / 100.0;
    p["emerald"] = sliderEmerald->value() / 100.0;
    p["blue"] = sliderBlue->value() / 100.0;
    p["purple"] = sliderPurple->value() / 100.0;
    return p;
}

// -----------------------------------------------------------------------------
// VDBobDoublerFilterDialog Implementation (Matching Screenshot)
// -----------------------------------------------------------------------------
VDBobDoublerFilterDialog::VDBobDoublerFilterDialog(const QMap<QString, double>& params, const QImage &sourceFrame, QWidget *parent)
    : QDialog(parent), mSourceFrame(sourceFrame), mPreviewDialog(nullptr) {
    setWindowTitle("Filter: Bob doubler");
    setStyleSheet(kDialogStyle);
    setFixedSize(390, 290);

    if (mSourceFrame.isNull()) {
        mSourceFrame = QImage(640, 480, QImage::Format_RGB888);
        mSourceFrame.fill(Qt::black);
        QPainter p(&mSourceFrame);
        QColor colors[] = { Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue, Qt::black };
        int barW = 640 / 8;
        for (int i = 0; i < 8; ++i) {
            p.fillRect(i * barW, 0, barW, 240, colors[i]);
        }
        for (int x = 0; x < 640; ++x) {
            int g = (x * 255) / 640;
            p.setPen(QColor(g, g, g));
            p.drawLine(x, 240, x, 480);
        }
        p.end();
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 14, 18, 14);
    mainLayout->setSpacing(12);

    // Section 1: Field order
    QHBoxLayout *hFieldOrder = new QHBoxLayout();
    QLabel *lblFieldOrder = new QLabel("Field order", this);
    lblFieldOrder->setFixedWidth(100);
    lblFieldOrder->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    QVBoxLayout *vFieldRadios = new QVBoxLayout();
    vFieldRadios->setSpacing(4);
    radTFF = new QRadioButton("Top field first", this);
    radBFF = new QRadioButton("Bottom field first", this);

    QButtonGroup *grpFieldOrder = new QButtonGroup(this);
    grpFieldOrder->addButton(radTFF, 0);
    grpFieldOrder->addButton(radBFF, 1);

    int curFieldOrder = static_cast<int>(params.value("field_order", 1));
    if (curFieldOrder == 0) radTFF->setChecked(true);
    else radBFF->setChecked(true);

    vFieldRadios->addWidget(radTFF);
    vFieldRadios->addWidget(radBFF);

    hFieldOrder->addWidget(lblFieldOrder);
    hFieldOrder->addLayout(vFieldRadios);
    mainLayout->addLayout(hFieldOrder);

    // Section 2: Deinterlacing
    QHBoxLayout *hDeinterlace = new QHBoxLayout();
    QLabel *lblDeinterlace = new QLabel("Deinterlacing", this);
    lblDeinterlace->setFixedWidth(100);
    lblDeinterlace->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    QVBoxLayout *vDeintRadios = new QVBoxLayout();
    vDeintRadios->setSpacing(4);
    radBob = new QRadioButton("Bob", this);
    radELA = new QRadioButton("ELA", this);
    radAdaptiveELA = new QRadioButton("Adaptive ELA", this);
    radNoneFields = new QRadioButton("None - alternate fields", this);
    radNoneFrames = new QRadioButton("None - double up frames", this);

    QButtonGroup *grpDeint = new QButtonGroup(this);
    grpDeint->addButton(radBob, 0);
    grpDeint->addButton(radELA, 1);
    grpDeint->addButton(radAdaptiveELA, 2);
    grpDeint->addButton(radNoneFields, 3);
    grpDeint->addButton(radNoneFrames, 4);

    int curMode = static_cast<int>(params.value("mode", 0));
    if (curMode == 1) radELA->setChecked(true);
    else if (curMode == 2) radAdaptiveELA->setChecked(true);
    else if (curMode == 3) radNoneFields->setChecked(true);
    else if (curMode == 4) radNoneFrames->setChecked(true);
    else radBob->setChecked(true);

    vDeintRadios->addWidget(radBob);
    vDeintRadios->addWidget(radELA);
    vDeintRadios->addWidget(radAdaptiveELA);
    vDeintRadios->addWidget(radNoneFields);
    vDeintRadios->addWidget(radNoneFrames);

    hDeinterlace->addWidget(lblDeinterlace);
    hDeinterlace->addLayout(vDeintRadios);
    mainLayout->addLayout(hDeinterlace);

    mainLayout->addStretch();

    // Section 3: Buttons
    QHBoxLayout *hButtons = new QHBoxLayout();
    btnShowPreview = new QPushButton("Show preview", this);
    btnShowPreview->setEnabled(true);
    btnShowPreview->setFixedWidth(95);

    btnOk = new QPushButton("OK", this);
    btnOk->setDefault(true);
    btnOk->setFixedWidth(75);

    btnCancel = new QPushButton("Cancel", this);
    btnCancel->setFixedWidth(75);

    hButtons->addWidget(btnShowPreview);
    hButtons->addStretch();
    hButtons->addWidget(btnOk);
    hButtons->addWidget(btnCancel);

    mainLayout->addLayout(hButtons);

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnShowPreview, &QPushButton::clicked, this, &VDBobDoublerFilterDialog::onTogglePreviewClicked);

    connect(radTFF, &QRadioButton::toggled, this, &VDBobDoublerFilterDialog::onModeChanged);
    connect(radBFF, &QRadioButton::toggled, this, &VDBobDoublerFilterDialog::onModeChanged);
    connect(radBob, &QRadioButton::toggled, this, &VDBobDoublerFilterDialog::onModeChanged);
    connect(radELA, &QRadioButton::toggled, this, &VDBobDoublerFilterDialog::onModeChanged);
    connect(radAdaptiveELA, &QRadioButton::toggled, this, &VDBobDoublerFilterDialog::onModeChanged);
    connect(radNoneFields, &QRadioButton::toggled, this, &VDBobDoublerFilterDialog::onModeChanged);
    connect(radNoneFrames, &QRadioButton::toggled, this, &VDBobDoublerFilterDialog::onModeChanged);
}

VDBobDoublerFilterDialog::~VDBobDoublerFilterDialog() {
    if (mPreviewDialog) {
        mPreviewDialog->close();
        delete mPreviewDialog;
        mPreviewDialog = nullptr;
    }
}

void VDBobDoublerFilterDialog::onModeChanged() {
    if (mPreviewDialog && mPreviewDialog->isVisible()) {
        updatePreviewImage();
    }
}

void VDBobDoublerFilterDialog::onTogglePreviewClicked() {
    if (!mPreviewDialog) {
        mPreviewDialog = new VDFilterPreviewDialog(this);
    }

    if (mPreviewDialog->isVisible()) {
        mPreviewDialog->hide();
        btnShowPreview->setText("Show preview");
    } else {
        mPreviewDialog->show();
        mPreviewDialog->raise();
        btnShowPreview->setText("Hide preview");
        updatePreviewImage();
    }
}

void VDBobDoublerFilterDialog::updatePreviewImage() {
    if (mSourceFrame.isNull() || !mPreviewDialog) return;

    QMap<QString, double> p = getParams();
    QImage processed = mSourceFrame.convertToFormat(QImage::Format_RGB888);

    int fieldOrder = static_cast<int>(p.value("field_order", 1));
    int mode = static_cast<int>(p.value("mode", 0));
    bool odd = (fieldOrder == 1);

    int bpp = 3;
    int w = processed.width();
    int h = processed.height();

    QImage temp = processed;

    memcpy(processed.scanLine(0), temp.constScanLine(odd ? 1 : 0), w * bpp);

    for (int y = 1; y < h - 1; ++y) {
        bool scanOdd = (y & 1) != 0;
        if (scanOdd == odd) {
            memcpy(processed.scanLine(y), temp.constScanLine(y), w * bpp);
        } else {
            uchar *dst = processed.scanLine(y);
            const uchar *src1 = temp.constScanLine(y - 1);
            const uchar *src2 = temp.constScanLine(y + 1);

            if (mode == 0) {
                for (int x = 0; x < w * bpp; ++x) {
                    dst[x] = static_cast<uchar>((static_cast<int>(src1[x]) + static_cast<int>(src2[x]) + 1) >> 1);
                }
            } else if (mode == 1 || mode == 2) {
                for (int x = 0; x < w; ++x) {
                    int xPrev = std::clamp(x - 1, 0, w - 1);
                    int xNext = std::clamp(x + 1, 0, w - 1);

                    int d0 = 0, d1 = 0, d2 = 0;
                    for (int c = 0; c < 3; ++c) {
                        int diff0 = std::abs(static_cast<int>(src1[xPrev * bpp + c]) - static_cast<int>(src2[xNext * bpp + c]));
                        int diff1 = std::abs(static_cast<int>(src1[x * bpp + c])     - static_cast<int>(src2[x * bpp + c]));
                        int diff2 = std::abs(static_cast<int>(src1[xNext * bpp + c]) - static_cast<int>(src2[xPrev * bpp + c]));
                        d0 += diff0; d1 += diff1; d2 += diff2;
                    }

                    if (d0 < d1 && d0 < d2) {
                        for (int c = 0; c < 3; ++c) {
                            dst[x * bpp + c] = static_cast<uchar>((static_cast<int>(src1[xPrev * bpp + c]) + static_cast<int>(src2[xNext * bpp + c]) + 1) >> 1);
                        }
                    } else if (d2 < d1 && d2 < d0) {
                        for (int c = 0; c < 3; ++c) {
                            dst[x * bpp + c] = static_cast<uchar>((static_cast<int>(src1[xNext * bpp + c]) + static_cast<int>(src2[xPrev * bpp + c]) + 1) >> 1);
                        }
                    } else {
                        for (int c = 0; c < 3; ++c) {
                            dst[x * bpp + c] = static_cast<uchar>((static_cast<int>(src1[x * bpp + c]) + static_cast<int>(src2[x * bpp + c]) + 1) >> 1);
                        }
                    }
                }
            } else {
                memcpy(dst, temp.constScanLine(odd ? y : (y - 1)), w * bpp);
            }
        }
    }

    memcpy(processed.scanLine(h - 1), temp.constScanLine(odd ? (h - 1) : (h - 2)), w * bpp);

    mPreviewDialog->updatePreviewImage(processed);
}

QMap<QString, double> VDBobDoublerFilterDialog::getParams() const {
    QMap<QString, double> p;
    p["field_order"] = radBFF->isChecked() ? 1 : 0;
    if (radELA->isChecked()) p["mode"] = 1;
    else if (radAdaptiveELA->isChecked()) p["mode"] = 2;
    else if (radNoneFields->isChecked()) p["mode"] = 3;
    else if (radNoneFrames->isChecked()) p["mode"] = 4;
    else p["mode"] = 0;
    return p;
}

// -----------------------------------------------------------------------------
// VDVideoFilterAddDialog
// -----------------------------------------------------------------------------
VDVideoFilterAddDialog::VDVideoFilterAddDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Add Filter");
    resize(520, 380);
    setStyleSheet(kDialogStyle);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mFilterList = new QListWidget(this);

    mAvailableFilters = VDQtFilterSystem::instance().getAvailableFilters();
    std::sort(mAvailableFilters.begin(), mAvailableFilters.end(), [](const VDQtFilterSystem::FilterInfo &a, const VDQtFilterSystem::FilterInfo &b) {
        return a.name.toLower() < b.name.toLower();
    });
    for (const auto& f : mAvailableFilters) {
        QListWidgetItem *item = new QListWidgetItem(f.name, mFilterList);
        item->setToolTip(f.description);
    }

    mainLayout->addWidget(mFilterList);

    mDescLabel = new QLabel("Select a video filter to add to the processing chain.", this);
    mDescLabel->setWordWrap(true);
    mainLayout->addWidget(mDescLabel);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(box);

    connect(mFilterList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < mAvailableFilters.size()) {
            mDescLabel->setText(mAvailableFilters[row].description);
        }
    });

    if (mFilterList->count() > 0) mFilterList->setCurrentRow(0);
}

QString VDVideoFilterAddDialog::getSelectedFilterName() const {
    int row = mFilterList->currentRow();
    if (row >= 0 && row < mAvailableFilters.size()) {
        return mAvailableFilters[row].name;
    }
    return QString();
}

VDFilterType VDVideoFilterAddDialog::getSelectedFilterType() const {
    int row = mFilterList->currentRow();
    if (row >= 0 && row < mAvailableFilters.size()) {
        return mAvailableFilters[row].type;
    }
    return VDFilterType::Grayscale;
}

// -----------------------------------------------------------------------------
// VDFrameRateDialog Implementation (Matching Screenshot)
// -----------------------------------------------------------------------------
VDFrameRateDialog::VDFrameRateDialog(double sourceFps, double audioMatchFps, const VDFrameRateConfig &initialConfig, QWidget *parent)
    : QDialog(parent), mSourceFps(sourceFps > 0 ? sourceFps : 29.970), mAudioMatchFps(audioMatchFps > 0 ? audioMatchFps : mSourceFps), mConfig(initialConfig) {
    setWindowTitle("Video frame rate control");
    setStyleSheet(kDialogStyle);
    setFixedSize(480, 360);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(10);

    // -------------------------------------------------------------------------
    // GROUP 1: Source rate adjustment
    // -------------------------------------------------------------------------
    QGroupBox *grpSource = new QGroupBox("Source rate adjustment", this);
    QVBoxLayout *vSourceLayout = new QVBoxLayout(grpSource);
    vSourceLayout->setContentsMargins(12, 12, 12, 10);
    vSourceLayout->setSpacing(6);

    radSourceNoChange = new QRadioButton(QString("No change (current: %1 fps)").arg(mSourceFps, 0, 'f', 3), grpSource);

    QHBoxLayout *hCustomFps = new QHBoxLayout();
    radSourceCustom = new QRadioButton("Change frame rate to (fps):", grpSource);
    txtSourceCustomFps = new QLineEdit(grpSource);
    txtSourceCustomFps->setPlaceholderText(QString::number(mSourceFps, 'f', 3));
    hCustomFps->addWidget(radSourceCustom);
    hCustomFps->addWidget(txtSourceCustomFps, 1);

    radSourceMatchAudio = new QRadioButton(QString("Change so video and audio durations match  (%1 fps)").arg(mAudioMatchFps, 0, 'f', 3), grpSource);

    QLabel *lblNote = new QLabel("Note: Changing the framerate will cause audio/video desynchronization.", grpSource);
    lblNote->setStyleSheet("font-size: 11px; color: #888; margin-top: 4px;");

    QButtonGroup *grpSourceRadio = new QButtonGroup(this);
    grpSourceRadio->addButton(radSourceNoChange, 0);
    grpSourceRadio->addButton(radSourceCustom, 1);
    grpSourceRadio->addButton(radSourceMatchAudio, 2);

    if (mConfig.sourceMode == 1) {
        radSourceCustom->setChecked(true);
        txtSourceCustomFps->setEnabled(true);
        if (mConfig.customSourceFps > 0) {
            txtSourceCustomFps->setText(QString::number(mConfig.customSourceFps, 'f', 3));
        }
    } else if (mConfig.sourceMode == 2) {
        radSourceMatchAudio->setChecked(true);
        txtSourceCustomFps->setEnabled(false);
    } else {
        radSourceNoChange->setChecked(true);
        txtSourceCustomFps->setEnabled(false);
    }

    vSourceLayout->addWidget(radSourceNoChange);
    vSourceLayout->addLayout(hCustomFps);
    vSourceLayout->addWidget(radSourceMatchAudio);
    vSourceLayout->addWidget(lblNote);
    mainLayout->addWidget(grpSource);

    // -------------------------------------------------------------------------
    // GROUP 2: Frame rate conversion
    // -------------------------------------------------------------------------
    QGroupBox *grpConv = new QGroupBox("Frame rate conversion", this);
    QVBoxLayout *vConvLayout = new QVBoxLayout(grpConv);
    vConvLayout->setContentsMargins(12, 12, 12, 10);
    vConvLayout->setSpacing(6);

    radConvAllFrames = new QRadioButton("Process all frames", grpConv);
    radConvDecimate2 = new QRadioButton("Process every other frame (decimate by 2)", grpConv);
    radConvDecimate3 = new QRadioButton("Process every third frame (decimate by 3)", grpConv);

    QHBoxLayout *hDecimateN = new QHBoxLayout();
    radConvDecimateN = new QRadioButton("Decimate by", grpConv);
    txtConvDecimateN = new QLineEdit(grpConv);
    txtConvDecimateN->setFixedWidth(60);
    hDecimateN->addWidget(radConvDecimateN);
    hDecimateN->addWidget(txtConvDecimateN);
    hDecimateN->addStretch();

    QHBoxLayout *hConvertFps = new QHBoxLayout();
    radConvCustomFps = new QRadioButton("Convert to fps:", grpConv);
    txtConvCustomFps = new QLineEdit(grpConv);
    hConvertFps->addWidget(radConvCustomFps);
    hConvertFps->addWidget(txtConvCustomFps, 1);

    QButtonGroup *grpConvRadio = new QButtonGroup(this);
    grpConvRadio->addButton(radConvAllFrames, 0);
    grpConvRadio->addButton(radConvDecimate2, 1);
    grpConvRadio->addButton(radConvDecimate3, 2);
    grpConvRadio->addButton(radConvDecimateN, 3);
    grpConvRadio->addButton(radConvCustomFps, 4);

    if (mConfig.convMode == 1) {
        radConvDecimate2->setChecked(true);
        txtConvDecimateN->setEnabled(false);
        txtConvCustomFps->setEnabled(false);
    } else if (mConfig.convMode == 2) {
        radConvDecimate3->setChecked(true);
        txtConvDecimateN->setEnabled(false);
        txtConvCustomFps->setEnabled(false);
    } else if (mConfig.convMode == 3) {
        radConvDecimateN->setChecked(true);
        txtConvDecimateN->setEnabled(true);
        txtConvDecimateN->setText(QString::number(mConfig.decimateN > 0 ? mConfig.decimateN : 2));
        txtConvCustomFps->setEnabled(false);
    } else if (mConfig.convMode == 4) {
        radConvCustomFps->setChecked(true);
        txtConvCustomFps->setEnabled(true);
        if (mConfig.convertFps > 0) {
            txtConvCustomFps->setText(QString::number(mConfig.convertFps, 'f', 3));
        }
        txtConvDecimateN->setEnabled(false);
    } else {
        radConvAllFrames->setChecked(true);
        txtConvDecimateN->setEnabled(false);
        txtConvCustomFps->setEnabled(false);
    }

    vConvLayout->addWidget(radConvAllFrames);
    vConvLayout->addWidget(radConvDecimate2);
    vConvLayout->addWidget(radConvDecimate3);
    vConvLayout->addLayout(hDecimateN);
    vConvLayout->addLayout(hConvertFps);
    mainLayout->addWidget(grpConv);

    // -------------------------------------------------------------------------
    // BUTTONS
    // -------------------------------------------------------------------------
    QHBoxLayout *hButtons = new QHBoxLayout();
    btnOk = new QPushButton("OK", this);
    btnOk->setDefault(true);
    btnOk->setFixedWidth(75);

    btnCancel = new QPushButton("Cancel", this);
    btnCancel->setFixedWidth(75);

    hButtons->addStretch();
    hButtons->addWidget(btnOk);
    hButtons->addWidget(btnCancel);
    mainLayout->addLayout(hButtons);

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    connect(radSourceNoChange, &QRadioButton::toggled, this, &VDFrameRateDialog::onSourceRadioToggled);
    connect(radSourceCustom, &QRadioButton::toggled, this, &VDFrameRateDialog::onSourceRadioToggled);
    connect(radSourceMatchAudio, &QRadioButton::toggled, this, &VDFrameRateDialog::onSourceRadioToggled);

    connect(radConvAllFrames, &QRadioButton::toggled, this, &VDFrameRateDialog::onConversionRadioToggled);
    connect(radConvDecimate2, &QRadioButton::toggled, this, &VDFrameRateDialog::onConversionRadioToggled);
    connect(radConvDecimate3, &QRadioButton::toggled, this, &VDFrameRateDialog::onConversionRadioToggled);
    connect(radConvDecimateN, &QRadioButton::toggled, this, &VDFrameRateDialog::onConversionRadioToggled);
    connect(radConvCustomFps, &QRadioButton::toggled, this, &VDFrameRateDialog::onConversionRadioToggled);
}

void VDFrameRateDialog::onSourceRadioToggled() {
    txtSourceCustomFps->setEnabled(radSourceCustom->isChecked());
    if (radSourceCustom->isChecked()) {
        txtSourceCustomFps->setFocus();
    }
}

void VDFrameRateDialog::onConversionRadioToggled() {
    txtConvDecimateN->setEnabled(radConvDecimateN->isChecked());
    txtConvCustomFps->setEnabled(radConvCustomFps->isChecked());
    if (radConvDecimateN->isChecked()) {
        txtConvDecimateN->setFocus();
    } else if (radConvCustomFps->isChecked()) {
        txtConvCustomFps->setFocus();
    }
}

VDFrameRateConfig VDFrameRateDialog::getConfig() const {
    VDFrameRateConfig cfg;
    if (radSourceCustom->isChecked()) {
        cfg.sourceMode = 1;
        cfg.customSourceFps = txtSourceCustomFps->text().toDouble();
    } else if (radSourceMatchAudio->isChecked()) {
        cfg.sourceMode = 2;
    } else {
        cfg.sourceMode = 0;
    }

    if (radConvDecimate2->isChecked()) {
        cfg.convMode = 1;
    } else if (radConvDecimate3->isChecked()) {
        cfg.convMode = 2;
    } else if (radConvDecimateN->isChecked()) {
        cfg.convMode = 3;
        cfg.decimateN = txtConvDecimateN->text().toInt();
    } else if (radConvCustomFps->isChecked()) {
        cfg.convMode = 4;
        cfg.convertFps = txtConvCustomFps->text().toDouble();
    } else {
        cfg.convMode = 0;
    }
    return cfg;
}

double VDFrameRateDialog::getTargetFps() const {
    if (radSourceCustom->isChecked()) {
        bool ok = false;
        double val = txtSourceCustomFps->text().toDouble(&ok);
        if (ok && val > 0.0) return val;
    } else if (radSourceMatchAudio->isChecked()) {
        return mAudioMatchFps;
    }
    if (radConvCustomFps->isChecked()) {
        bool ok = false;
        double val = txtConvCustomFps->text().toDouble(&ok);
        if (ok && val > 0.0) return val;
    }
    if (radConvDecimate2->isChecked()) return mSourceFps / 2.0;
    if (radConvDecimate3->isChecked()) return mSourceFps / 3.0;
    if (radConvDecimateN->isChecked()) {
        bool ok = false;
        int factor = txtConvDecimateN->text().toInt(&ok);
        if (ok && factor > 1) return mSourceFps / factor;
    }
    return mSourceFps;
}

int VDFrameRateDialog::getDecimateFactor() const {
    if (radConvDecimate2->isChecked()) return 2;
    if (radConvDecimate3->isChecked()) return 3;
    if (radConvDecimateN->isChecked()) {
        bool ok = false;
        int factor = txtConvDecimateN->text().toInt(&ok);
        if (ok && factor > 1) return factor;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// VDDecodeFormatDialog Implementation (Matching Screenshot)
// -----------------------------------------------------------------------------
VDDecodeFormatDialog::VDDecodeFormatDialog(const QString &decoderName, const QString &actualFormat, const VDDecompressionFormatConfig &initialConfig, QWidget *parent)
    : QDialog(parent), mDecoderName(decoderName.isEmpty() ? "AVIFile/Avisynth input driver (internal)" : decoderName),
      mActualFormat(actualFormat.isEmpty() ? "YUV420" : actualFormat), mConfig(initialConfig) {
    setWindowTitle("Decompression format");
    setStyleSheet(kDialogStyle);
    setFixedSize(590, 420);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(10);

    // -------------------------------------------------------------------------
    // Top Info Section
    // -------------------------------------------------------------------------
    QGridLayout *topGrid = new QGridLayout();
    topGrid->setHorizontalSpacing(10);
    topGrid->setVerticalSpacing(6);

    QLabel *lblDecoderKey = new QLabel("Decoder:", this);
    lblDecoderVal = new QLabel(mDecoderName, this);
    lblDecoderVal->setStyleSheet("font-weight: bold;");

    QLabel *lblFormatKey = new QLabel("Actual format:", this);
    QHBoxLayout *hFormatVal = new QHBoxLayout();
    txtActualFormat = new QLineEdit(mActualFormat, this);
    txtActualFormat->setReadOnly(true);
    txtActualFormat->setFixedWidth(240);
    QLabel *lblDefaultTag = new QLabel("(Default)", this);
    lblDefaultTag->setStyleSheet("color: #888;");
    hFormatVal->addWidget(txtActualFormat);
    hFormatVal->addWidget(lblDefaultTag);
    hFormatVal->addStretch();

    topGrid->addWidget(lblDecoderKey, 0, 0);
    topGrid->addWidget(lblDecoderVal, 0, 1);
    topGrid->addWidget(lblFormatKey, 1, 0);
    topGrid->addLayout(hFormatVal, 1, 1);

    mainLayout->addLayout(topGrid);

    // Separator
    QFrame *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(sep);

    // -------------------------------------------------------------------------
    // Middle Section: Formats & YCbCr Properties
    // -------------------------------------------------------------------------
    QHBoxLayout *hMidLayout = new QHBoxLayout();
    hMidLayout->setSpacing(14);

    // Left Section: Formats
    QVBoxLayout *vFormatsLayout = new QVBoxLayout();
    vFormatsLayout->setSpacing(4);

    grpFormats = new QButtonGroup(this);

    radAutoselect = new QRadioButton("Autoselect", this);
    grpFormats->addButton(radAutoselect);
    vFormatsLayout->addWidget(radAutoselect);
    vFormatsLayout->addSpacing(4);

    radRGB24 = new QRadioButton("RGB24", this);
    grpFormats->addButton(radRGB24);
    vFormatsLayout->addWidget(radRGB24);
    vFormatsLayout->addStretch();
    hMidLayout->addLayout(vFormatsLayout, 1);

    // Right Section: Interpret YCbCr properties
    QGroupBox *grpYCbCr = new QGroupBox("Interpret YCbCr properties:", this);
    QVBoxLayout *vYCbCrLayout = new QVBoxLayout(grpYCbCr);
    vYCbCrLayout->setContentsMargins(10, 10, 10, 10);
    vYCbCrLayout->setSpacing(10);

    // Color space
    QHBoxLayout *hColorSpace = new QHBoxLayout();
    QLabel *lblCS = new QLabel("Color space", grpYCbCr);
    lblCS->setFixedWidth(90);
    lblCS->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    QVBoxLayout *vCSRadios = new QVBoxLayout();
    vCSRadios->setSpacing(3);
    grpColorSpace = new QButtonGroup(this);
    radCSNoChange = new QRadioButton("No change", grpYCbCr);
    radCSRec601 = new QRadioButton("Rec. 601 (SD)", grpYCbCr);
    radCSRec709 = new QRadioButton("Rec. 709 (HD)", grpYCbCr);
    grpColorSpace->addButton(radCSNoChange, 0);
    grpColorSpace->addButton(radCSRec601, 1);
    grpColorSpace->addButton(radCSRec709, 2);
    vCSRadios->addWidget(radCSNoChange);
    vCSRadios->addWidget(radCSRec601);
    vCSRadios->addWidget(radCSRec709);

    hColorSpace->addWidget(lblCS);
    hColorSpace->addLayout(vCSRadios);
    vYCbCrLayout->addLayout(hColorSpace);

    // Component range
    QHBoxLayout *hRange = new QHBoxLayout();
    QLabel *lblRange = new QLabel("Component range", grpYCbCr);
    lblRange->setFixedWidth(90);
    lblRange->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    QVBoxLayout *vRangeRadios = new QVBoxLayout();
    vRangeRadios->setSpacing(3);
    grpRange = new QButtonGroup(this);
    radRangeNoChange = new QRadioButton("No change", grpYCbCr);
    radRangeLimited = new QRadioButton("Limited (Y: 16-235)", grpYCbCr);
    radRangeFull = new QRadioButton("Full (0-255)", grpYCbCr);
    grpRange->addButton(radRangeNoChange, 0);
    grpRange->addButton(radRangeLimited, 1);
    grpRange->addButton(radRangeFull, 2);
    vRangeRadios->addWidget(radRangeNoChange);
    vRangeRadios->addWidget(radRangeLimited);
    vRangeRadios->addWidget(radRangeFull);

    hRange->addWidget(lblRange);
    hRange->addLayout(vRangeRadios);
    vYCbCrLayout->addLayout(hRange);
    vYCbCrLayout->addStretch();

    hMidLayout->addWidget(grpYCbCr);
    mainLayout->addLayout(hMidLayout, 1);

    // Set initial values
    if (mConfig.formatName == "RGB24") radRGB24->setChecked(true);
    else radAutoselect->setChecked(true);

    if (mConfig.colorSpace == 1) radCSRec601->setChecked(true);
    else if (mConfig.colorSpace == 2) radCSRec709->setChecked(true);
    else radCSNoChange->setChecked(true);

    if (mConfig.componentRange == 1) radRangeLimited->setChecked(true);
    else if (mConfig.componentRange == 2) radRangeFull->setChecked(true);
    else radRangeNoChange->setChecked(true);

    // -------------------------------------------------------------------------
    // Bottom Buttons
    // -------------------------------------------------------------------------
    QHBoxLayout *hButtons = new QHBoxLayout();
    btnOk = new QPushButton("OK", this);
    btnOk->setDefault(true);
    btnOk->setFixedWidth(75);

    btnCancel = new QPushButton("Cancel", this);
    btnCancel->setFixedWidth(75);

    hButtons->addStretch();
    hButtons->addWidget(btnOk);
    hButtons->addWidget(btnCancel);
    mainLayout->addLayout(hButtons);

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

VDDecompressionFormatConfig VDDecodeFormatDialog::getConfig() const {
    VDDecompressionFormatConfig cfg;
    if (radRGB24->isChecked()) cfg.formatName = "RGB24";
    else cfg.formatName = "Autoselect";

    if (radCSRec601->isChecked()) cfg.colorSpace = 1;
    else if (radCSRec709->isChecked()) cfg.colorSpace = 2;
    else cfg.colorSpace = 0;

    if (radRangeLimited->isChecked()) cfg.componentRange = 1;
    else if (radRangeFull->isChecked()) cfg.componentRange = 2;
    else cfg.componentRange = 0;

    return cfg;
}

// -----------------------------------------------------------------------------
// VDDecoderErrorModeDialog Implementation (Matching Screenshot)
// -----------------------------------------------------------------------------
VDDecoderErrorModeDialog::VDDecoderErrorModeDialog(const VDDecoderErrorModeConfig &initialConfig, QWidget *parent)
    : QDialog(parent), mConfig(initialConfig) {
    setWindowTitle("Decoder error mode");
    setStyleSheet(kDialogStyle);
    setFixedSize(440, 230);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // Group Box
    QGroupBox *grpBox = new QGroupBox("Decoder error mode", this);
    QVBoxLayout *vGrpLayout = new QVBoxLayout(grpBox);
    vGrpLayout->setContentsMargins(12, 10, 12, 10);
    vGrpLayout->setSpacing(6);

    grpErrorMode = new QButtonGroup(this);
    radReportAll = new QRadioButton("&Report all errors", grpBox);
    radConceal = new QRadioButton("&Conceal errors and resume decoding at next keyframe", grpBox);
    radGarbled = new QRadioButton("&Decode even if the result may be garbled", grpBox);

    grpErrorMode->addButton(radReportAll, 0);
    grpErrorMode->addButton(radConceal, 1);
    grpErrorMode->addButton(radGarbled, 2);

    vGrpLayout->addWidget(radReportAll);
    vGrpLayout->addWidget(radConceal);
    vGrpLayout->addWidget(radGarbled);

    mainLayout->addWidget(grpBox);

    // Note Label
    QLabel *lblNote = new QLabel("Note: Some errors cannot be concealed or ignored -- including, but not limited to, a crash in a third-party driver.", this);
    lblNote->setWordWrap(true);
    lblNote->setStyleSheet("color: #444; font-size: 11px;");
    mainLayout->addWidget(lblNote);

    mainLayout->addSpacing(4);

    // Initial state
    if (mConfig.errorMode == 1) radConceal->setChecked(true);
    else if (mConfig.errorMode == 2) radGarbled->setChecked(true);
    else radReportAll->setChecked(true);

    // Bottom Buttons
    QHBoxLayout *hButtons = new QHBoxLayout();
    btnOk = new QPushButton("OK", this);
    btnOk->setDefault(true);
    btnOk->setFixedWidth(75);

    btnCancel = new QPushButton("Cancel", this);
    btnCancel->setFixedWidth(75);

    hButtons->addStretch();
    hButtons->addWidget(btnOk);
    hButtons->addWidget(btnCancel);

    mainLayout->addLayout(hButtons);

    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

VDDecoderErrorModeConfig VDDecoderErrorModeDialog::getConfig() const {
    VDDecoderErrorModeConfig cfg;
    if (radConceal->isChecked()) cfg.errorMode = 1;
    else if (radGarbled->isChecked()) cfg.errorMode = 2;
    else cfg.errorMode = 0;
    return cfg;
}

// -----------------------------------------------------------------------------
// VDSaveAudioDialog Implementation (Full Processing Codec Selection)
// -----------------------------------------------------------------------------
VDSaveAudioDialog::VDSaveAudioDialog(const QString &defaultDir, const QString &defaultFileName, const QString &compressionInfo, const QString &sampleLayoutInfo, QWidget *parent)
    : QDialog(parent) {
    (void)compressionInfo;
    (void)sampleLayoutInfo;

    VDSaveAudioSessionConfig sCfg = VDQtCodecSettings::instance().getSaveAudioSessionConfig();
    if (!defaultDir.isEmpty()) {
        mDirectory = defaultDir;
    } else if (!sCfg.directory.isEmpty()) {
        mDirectory = sCfg.directory;
    } else {
        mDirectory = QDir::homePath();
    }

    setWindowTitle("Save Audio Stream (Full Processing)");
    setStyleSheet(kDialogStyle);
    resize(640, 520);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(8);

    // Save in / Directory Bar
    QHBoxLayout *hSaveIn = new QHBoxLayout();
    QLabel *lblSaveIn = new QLabel("Save in:", this);
    lblSaveIn->setFixedWidth(60);

    lblDir = new QLabel(QDir(mDirectory).dirName().isEmpty() ? mDirectory : QDir(mDirectory).dirName(), this);
    lblDir->setStyleSheet("background: #252530; border: 1px solid #3d3d4d; border-radius: 3px; padding: 4px 8px; font-weight: bold;");
    btnBrowse = new QPushButton("Browse...", this);
    btnBrowse->setFixedWidth(80);

    hSaveIn->addWidget(lblSaveIn);
    hSaveIn->addWidget(lblDir, 1);
    hSaveIn->addWidget(btnBrowse);
    mainLayout->addLayout(hSaveIn);

    // File list preview box / explorer area
    QListWidget *fileList = new QListWidget(this);
    fileList->setStyleSheet("background: #181822; border: 1px solid #333345; border-radius: 4px;");

    auto refreshFiles = [this, fileList]() {
        fileList->clear();
        QDir dir(mDirectory);
        QStringList entries = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        for (const QString &entry : entries) {
            QListWidgetItem *item = new QListWidgetItem(entry, fileList);
            if (QFileInfo(dir.filePath(entry)).isDir()) {
                item->setForeground(QColor("#00bcd4"));
            }
        }
    };
    refreshFiles();
    mainLayout->addWidget(fileList, 1);

    connect(fileList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item) {
            QString name = item->text();
            QString fullPath = QDir(mDirectory).filePath(name);
            if (QFileInfo(fullPath).isFile()) {
                txtFileName->setText(name);
            }
        }
    });

    connect(fileList, &QListWidget::itemDoubleClicked, this, [this, refreshFiles](QListWidgetItem *item) {
        if (item) {
            QString name = item->text();
            QString fullPath = QDir(mDirectory).filePath(name);
            if (QFileInfo(fullPath).isDir()) {
                mDirectory = fullPath;
                lblDir->setText(QDir(mDirectory).dirName().isEmpty() ? mDirectory : QDir(mDirectory).dirName());
                refreshFiles();
            } else {
                txtFileName->setText(name);
                onSaveClicked();
            }
        }
    });

    // File name and Files of type grid
    QGridLayout *grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);

    QLabel *lblFileName = new QLabel("File name:", this);
    txtFileName = new QLineEdit(defaultFileName.isEmpty() ? "test.wav" : defaultFileName, this);
    grid->addWidget(lblFileName, 0, 0);
    grid->addWidget(txtFileName, 0, 1);

    QLabel *lblFileType = new QLabel("Files of type:", this);
    cboFileType = new QComboBox(this);
    cboFileType->addItem("Windows audio (*.wav, *.w64)", ".wav");
    cboFileType->addItem("M4A (MPEG-4 Part 14) (*.m4a)", ".m4a");
    cboFileType->addItem("MP3 Audio (*.mp3)", ".mp3");
    cboFileType->addItem("Opus Audio (*.opus, *.mka)", ".opus");
    cboFileType->addItem("AC3 (Dolby Digital) (*.ac3)", ".ac3");
    cboFileType->addItem("FLAC Lossless Audio (*.flac)", ".flac");
    cboFileType->addItem("All files (*.*)", ".*");

    grid->addWidget(lblFileType, 1, 0);
    grid->addWidget(cboFileType, 1, 1);

    mainLayout->addLayout(grid);

    // Audio Compression & Codec Settings Group Box (Full Processing Controls)
    QGroupBox *grpCodec = new QGroupBox("Audio Codec & Full Processing Options", this);
    QVBoxLayout *codecLayout = new QVBoxLayout(grpCodec);

    QHBoxLayout *hCodecSelect = new QHBoxLayout();
    QLabel *lblCodec = new QLabel("Audio Codec:", this);
    lblCodec->setFixedWidth(100);
    cboCodec = new QComboBox(this);
    cboCodec->addItem("PCM Uncompressed (pcm_s16le) [Default]", "pcm_s16le");
    cboCodec->addItem("AAC (Advanced Audio Coding)", "aac");
    cboCodec->addItem("MP3 (libmp3lame)", "libmp3lame");
    cboCodec->addItem("Opus Audio Codec", "libopus");
    cboCodec->addItem("Vorbis (Ogg Vorbis)", "libvorbis");
    cboCodec->addItem("AC3 (Dolby Digital)", "ac3");
    cboCodec->addItem("FLAC Lossless Audio", "flac");
    cboCodec->setCurrentIndex(0); // Default to PCM!

    hCodecSelect->addWidget(lblCodec);
    hCodecSelect->addWidget(cboCodec, 1);
    codecLayout->addLayout(hCodecSelect);

    // Bitrate & Quality controls
    grpBitrate = new QGroupBox("Bitrate & Quality Mode", this);
    QVBoxLayout *bLayout = new QVBoxLayout(grpBitrate);

    mRadioVbr = new QRadioButton("Variable Bitrate (VBR Quality Mode)", this);
    mRadioCbr = new QRadioButton("Constant Bitrate (CBR Mode)", this);
    mRadioCbr->setChecked(true);

    QHBoxLayout *vbrLayout = new QHBoxLayout();
    lblVbr = new QLabel("VBR Quality Preset:", this);
    mVbrCombo = new QComboBox(this);
    vbrLayout->addWidget(lblVbr);
    vbrLayout->addWidget(mVbrCombo, 1);

    QHBoxLayout *cbrLayout = new QHBoxLayout();
    cbrLayout->addWidget(new QLabel("Target Bitrate:", this));
    mCbrBitrateCombo = new QComboBox(this);
    mCbrBitrateCombo->addItem("64 kbps", 64);
    mCbrBitrateCombo->addItem("96 kbps", 96);
    mCbrBitrateCombo->addItem("128 kbps", 128);
    mCbrBitrateCombo->addItem("160 kbps", 160);
    mCbrBitrateCombo->addItem("192 kbps", 192);
    mCbrBitrateCombo->addItem("256 kbps", 256);
    mCbrBitrateCombo->addItem("320 kbps", 320);
    mCbrBitrateCombo->setCurrentIndex(4); // 192k default
    cbrLayout->addWidget(mCbrBitrateCombo, 1);

    bLayout->addWidget(mRadioVbr);
    bLayout->addLayout(vbrLayout);
    bLayout->addWidget(mRadioCbr);
    bLayout->addLayout(cbrLayout);
    codecLayout->addWidget(grpBitrate);

    // Resampling & Channels controls
    QHBoxLayout *hResample = new QHBoxLayout();
    QLabel *lblSample = new QLabel("Sample Rate:", this);
    mSampleRateCombo = new QComboBox(this);
    mSampleRateCombo->addItem("Same as Source", 0);
    mSampleRateCombo->addItem("44.1 kHz", 44100);
    mSampleRateCombo->addItem("48.0 kHz", 48000);
    mSampleRateCombo->addItem("96.0 kHz", 96000);

    QLabel *lblChannels = new QLabel("Channels:", this);
    mChannelsCombo = new QComboBox(this);
    mChannelsCombo->addItem("Same as Source", 0);
    mChannelsCombo->addItem("Mono (1 Channel)", 1);
    mChannelsCombo->addItem("Stereo (2 Channels)", 2);
    mChannelsCombo->addItem("5.1 Surround (6 Channels)", 6);

    hResample->addWidget(lblSample);
    hResample->addWidget(mSampleRateCombo);
    hResample->addSpacing(16);
    hResample->addWidget(lblChannels);
    hResample->addWidget(mChannelsCombo);
    hResample->addStretch();
    codecLayout->addLayout(hResample);

    mainLayout->addWidget(grpCodec);

    // Buttons
    QHBoxLayout *hButtons = new QHBoxLayout();
    btnSave = new QPushButton("Save", this);
    btnSave->setDefault(true);
    btnSave->setFixedWidth(80);

    btnCancel = new QPushButton("Cancel", this);
    btnCancel->setFixedWidth(80);

    hButtons->addStretch();
    hButtons->addWidget(btnSave);
    hButtons->addWidget(btnCancel);
    mainLayout->addLayout(hButtons);

    connect(btnBrowse, &QPushButton::clicked, this, [this, refreshFiles]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Directory", mDirectory);
        if (!dir.isEmpty()) {
            mDirectory = dir;
            lblDir->setText(QDir(mDirectory).dirName().isEmpty() ? mDirectory : QDir(mDirectory).dirName());
            refreshFiles();
        }
    });

    connect(cboCodec, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VDSaveAudioDialog::onCodecChanged);
    connect(cboFileType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VDSaveAudioDialog::onFilterChanged);
    connect(mRadioVbr, &QRadioButton::toggled, this, &VDSaveAudioDialog::onRateControlModeChanged);
    connect(mRadioCbr, &QRadioButton::toggled, this, &VDSaveAudioDialog::onRateControlModeChanged);
    connect(btnSave, &QPushButton::clicked, this, &VDSaveAudioDialog::onSaveClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    // Load initial values from session config
    int cIdx = cboCodec->findData(sCfg.codecId);
    if (cIdx >= 0) cboCodec->setCurrentIndex(cIdx);
    else cboCodec->setCurrentIndex(0);

    if (sCfg.rateControlMode == "vbr") mRadioVbr->setChecked(true);
    else mRadioCbr->setChecked(true);

    onCodecChanged(cboCodec->currentIndex());

    int vIdx = mVbrCombo->findData(sCfg.vbrQuality);
    if (vIdx >= 0) mVbrCombo->setCurrentIndex(vIdx);

    int cbIdx = mCbrBitrateCombo->findData(sCfg.bitrateKbps);
    if (cbIdx >= 0) mCbrBitrateCombo->setCurrentIndex(cbIdx);

    int sIdx = mSampleRateCombo->findData(sCfg.sampleRate);
    if (sIdx >= 0) mSampleRateCombo->setCurrentIndex(sIdx);

    int chIdx = mChannelsCombo->findData(sCfg.channels);
    if (chIdx >= 0) mChannelsCombo->setCurrentIndex(chIdx);

    if (sCfg.fileTypeIndex >= 0 && sCfg.fileTypeIndex < cboFileType->count()) {
        cboFileType->setCurrentIndex(sCfg.fileTypeIndex);
    }

    onRateControlModeChanged();
}

void VDSaveAudioDialog::onBrowseClicked() {
}

void VDSaveAudioDialog::onCodecChanged(int index) {
    QString codecId = cboCodec->itemData(index).toString();
    bool isCompressed = (codecId != "pcm_s16le");
    grpBitrate->setEnabled(isCompressed);

    mVbrCombo->clear();
    if (codecId == "libmp3lame") {
        mRadioVbr->setEnabled(true);
        mRadioCbr->setEnabled(true);
        lblVbr->setText("VBR Quality Preset:");
        mVbrCombo->addItem("V0 (~245 kbps, Extreme Quality)", 0);
        mVbrCombo->addItem("V1 (~225 kbps, Very High Quality)", 1);
        mVbrCombo->addItem("V2 (~190 kbps, Standard / Recommended)", 2);
        mVbrCombo->addItem("V3 (~175 kbps)", 3);
        mVbrCombo->addItem("V4 (~165 kbps, Medium Quality)", 4);
        mVbrCombo->addItem("V5 (~130 kbps)", 5);
        mVbrCombo->addItem("V6 (~115 kbps)", 6);
        mVbrCombo->addItem("V7 (~100 kbps)", 7);
        mVbrCombo->addItem("V8 (~85 kbps)", 8);
        mVbrCombo->addItem("V9 (~65 kbps, Low Bitrate)", 9);
        mVbrCombo->setCurrentIndex(2); // Default V2 (~190k)
    } else if (codecId == "aac") {
        mRadioVbr->setEnabled(true);
        mRadioCbr->setEnabled(true);
        lblVbr->setText("VBR Quality Level:");
        mVbrCombo->addItem("Q5 (~256 kbps, Maximum Quality)", 5);
        mVbrCombo->addItem("Q4 (~192 kbps, High Quality)", 4);
        mVbrCombo->addItem("Q3 (~128 kbps, Medium Quality)", 3);
        mVbrCombo->addItem("Q2 (~96 kbps, Low Bitrate)", 2);
        mVbrCombo->addItem("Q1 (~64 kbps, Minimum Bitrate)", 1);
        mVbrCombo->setCurrentIndex(1); // Default Q4 (~192k)
    } else if (codecId == "libopus") {
        mRadioVbr->setEnabled(true);
        mRadioCbr->setEnabled(true);
        lblVbr->setText("Rate Control Mode:");
        mVbrCombo->addItem("Constrained VBR (Recommended for Opus)", 1);
        mVbrCombo->addItem("Hard CBR (Strict Constant Bitrate)", 0);
        mVbrCombo->setCurrentIndex(0);
    } else if (codecId == "libvorbis") {
        mRadioVbr->setEnabled(true);
        mRadioCbr->setEnabled(true);
        lblVbr->setText("VBR Quality Level:");
        mVbrCombo->addItem("Q10 (~500 kbps, Maximum Quality)", 10);
        mVbrCombo->addItem("Q8 (~256 kbps, Very High Quality)", 8);
        mVbrCombo->addItem("Q6 (~192 kbps, High Quality)", 6);
        mVbrCombo->addItem("Q5 (~160 kbps, Standard / Recommended)", 5);
        mVbrCombo->addItem("Q4 (~128 kbps, Medium Quality)", 4);
        mVbrCombo->addItem("Q2 (~96 kbps, Low Bitrate)", 2);
        mVbrCombo->addItem("Q0 (~64 kbps, Minimum Bitrate)", 0);
        mVbrCombo->setCurrentIndex(3); // Default Q5 (~160k)
    } else if (codecId == "ac3") {
        mRadioVbr->setEnabled(false);
        mRadioCbr->setChecked(true);
        mRadioCbr->setEnabled(true);
        lblVbr->setText("VBR (Not Supported by AC3)");
    } else if (codecId == "flac") {
        mRadioVbr->setEnabled(true);
        mRadioVbr->setChecked(true);
        mRadioCbr->setEnabled(false);
        lblVbr->setText("Compression Level:");
        mVbrCombo->addItem("Level 8 (Maximum Compression / Smallest File)", 8);
        mVbrCombo->addItem("Level 7 (Very High Compression)", 7);
        mVbrCombo->addItem("Level 6 (High Compression)", 6);
        mVbrCombo->addItem("Level 5 (Default / Recommended)", 5);
        mVbrCombo->addItem("Level 4 (Medium Compression)", 4);
        mVbrCombo->addItem("Level 3 (Fast Encoding)", 3);
        mVbrCombo->addItem("Level 2 (Faster Encoding)", 2);
        mVbrCombo->addItem("Level 1 (Very Fast Encoding)", 1);
        mVbrCombo->addItem("Level 0 (Fastest / Minimal CPU)", 0);
        mVbrCombo->setCurrentIndex(3); // Default Level 5
    }

    int curSampleRate = mSampleRateCombo->currentData().isValid() ? mSampleRateCombo->currentData().toInt() : 0;
    mSampleRateCombo->clear();
    if (codecId == "libopus") {
        mSampleRateCombo->addItem("Same as Source (Default)", 0);
        mSampleRateCombo->addItem("48.0 kHz (Fullband / Recommended)", 48000);
        mSampleRateCombo->addItem("24.0 kHz (Superwideband)", 24000);
        mSampleRateCombo->addItem("16.0 kHz (Wideband)", 16000);
        mSampleRateCombo->addItem("12.0 kHz (Mediumband)", 12000);
        mSampleRateCombo->addItem("8.0 kHz (Narrowband)", 8000);
    } else if (codecId == "ac3") {
        mSampleRateCombo->addItem("Same as Source (Default)", 0);
        mSampleRateCombo->addItem("48.0 kHz (Recommended)", 48000);
        mSampleRateCombo->addItem("44.1 kHz", 44100);
        mSampleRateCombo->addItem("32.0 kHz", 32000);
    } else if (codecId == "libmp3lame") {
        mSampleRateCombo->addItem("Same as Source (Default)", 0);
        mSampleRateCombo->addItem("48.0 kHz", 48000);
        mSampleRateCombo->addItem("44.1 kHz", 44100);
        mSampleRateCombo->addItem("32.0 kHz", 32000);
        mSampleRateCombo->addItem("24.0 kHz", 24000);
        mSampleRateCombo->addItem("22.05 kHz", 22050);
        mSampleRateCombo->addItem("16.0 kHz", 16000);
        mSampleRateCombo->addItem("12.0 kHz", 12000);
        mSampleRateCombo->addItem("11.025 kHz", 11025);
        mSampleRateCombo->addItem("8.0 kHz", 8000);
    } else if (codecId == "aac") {
        mSampleRateCombo->addItem("Same as Source (Default)", 0);
        mSampleRateCombo->addItem("96.0 kHz", 96000);
        mSampleRateCombo->addItem("88.2 kHz", 88200);
        mSampleRateCombo->addItem("48.0 kHz", 48000);
        mSampleRateCombo->addItem("44.1 kHz", 44100);
        mSampleRateCombo->addItem("32.0 kHz", 32000);
        mSampleRateCombo->addItem("24.0 kHz", 24000);
        mSampleRateCombo->addItem("22.05 kHz", 22050);
        mSampleRateCombo->addItem("16.0 kHz", 16000);
        mSampleRateCombo->addItem("12.0 kHz", 12000);
        mSampleRateCombo->addItem("11.025 kHz", 11025);
        mSampleRateCombo->addItem("8.0 kHz", 8000);
    } else {
        mSampleRateCombo->addItem("Same as Source (Default)", 0);
        mSampleRateCombo->addItem("192.0 kHz", 192000);
        mSampleRateCombo->addItem("96.0 kHz", 96000);
        mSampleRateCombo->addItem("88.2 kHz", 88200);
        mSampleRateCombo->addItem("48.0 kHz", 48000);
        mSampleRateCombo->addItem("44.1 kHz", 44100);
        mSampleRateCombo->addItem("32.0 kHz", 32000);
        mSampleRateCombo->addItem("24.0 kHz", 24000);
        mSampleRateCombo->addItem("22.05 kHz", 22050);
        mSampleRateCombo->addItem("16.0 kHz", 16000);
        mSampleRateCombo->addItem("12.0 kHz", 12000);
        mSampleRateCombo->addItem("11.025 kHz", 11025);
        mSampleRateCombo->addItem("8.0 kHz", 8000);
    }
    int idx = mSampleRateCombo->findData(curSampleRate);
    mSampleRateCombo->setCurrentIndex(idx >= 0 ? idx : 0);

    // Synchronize default file extension
    QString ext = ".wav";
    if (codecId == "aac") ext = ".m4a";
    else if (codecId == "libmp3lame") ext = ".mp3";
    else if (codecId == "libopus") ext = ".opus";
    else if (codecId == "libvorbis") ext = ".ogg";
    else if (codecId == "ac3") ext = ".ac3";
    else if (codecId == "flac") ext = ".flac";

    QString curName = txtFileName->text();
    int dot = curName.lastIndexOf('.');
    if (dot >= 0) {
        curName = curName.left(dot) + ext;
    } else {
        curName = curName + ext;
    }
    txtFileName->setText(curName);

    // Update cboFileType selection if matching
    for (int i = 0; i < cboFileType->count(); ++i) {
        if (cboFileType->itemData(i).toString().contains(ext)) {
            QSignalBlocker blocker(cboFileType);
            cboFileType->setCurrentIndex(i);
            break;
        }
    }

    onRateControlModeChanged();
}

void VDSaveAudioDialog::onRateControlModeChanged() {
    bool isVbr = mRadioVbr->isChecked();
    mVbrCombo->setEnabled(isVbr);
    lblVbr->setEnabled(isVbr);
    mCbrBitrateCombo->setEnabled(!isVbr);
}

void VDSaveAudioDialog::onFilterChanged(int index) {
    QString ext = cboFileType->itemData(index).toString();
    if (!ext.isEmpty() && ext != ".*") {
        QString curName = txtFileName->text();
        int dot = curName.lastIndexOf('.');
        if (dot >= 0) {
            curName = curName.left(dot) + ext;
        } else {
            curName = curName + ext;
        }
        txtFileName->setText(curName);
    }
}

void VDSaveAudioDialog::onSaveClicked() {
    if (txtFileName->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Save File", "Please enter a valid file name.");
        return;
    }

    VDAudioCodecConfig cfg = getAudioConfig();
    QString err;
    if (!VDQtCodecEngine::instance().checkAudioEncoderAvailable(cfg.codecId, &err)) {
        QMessageBox::critical(this, "Audio Encoder Not Available", err);
        return;
    }

    // Persist session export settings
    VDSaveAudioSessionConfig sCfg;
    sCfg.directory = mDirectory;
    sCfg.codecId = cfg.codecId;
    sCfg.rateControlMode = cfg.rateControlMode;
    sCfg.vbrQuality = cfg.vbrQuality;
    sCfg.bitrateKbps = cfg.bitrateKbps;
    sCfg.sampleRate = cfg.sampleRate;
    sCfg.channels = cfg.channels;
    sCfg.fileTypeIndex = cboFileType->currentIndex();
    VDQtCodecSettings::instance().setSaveAudioSessionConfig(sCfg);

    accept();
}

QString VDSaveAudioDialog::getSelectedFilePath() const {
    QString name = txtFileName->text().trimmed();
    if (QFileInfo(name).isAbsolute()) return name;
    return QDir(mDirectory).filePath(name);
}

VDAudioCodecConfig VDSaveAudioDialog::getAudioConfig() const {
    VDAudioCodecConfig cfg;
    cfg.codecId = cboCodec->currentData().toString();
    cfg.codecName = cboCodec->currentText();
    cfg.rateControlMode = mRadioVbr->isChecked() ? "vbr" : "cbr";
    cfg.vbrQuality = mVbrCombo->currentData().toInt();
    cfg.bitrateKbps = mCbrBitrateCombo->currentData().toInt();
    cfg.sampleRate = mSampleRateCombo->currentData().toInt();
    cfg.channels = mChannelsCombo->currentData().toInt();
    return cfg;
}

// -----------------------------------------------------------------------------
// VDVideoCompressionDialog
// -----------------------------------------------------------------------------
VDVideoCompressionDialog::VDVideoCompressionDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Select video compression");
    resize(640, 480);
    setStyleSheet(kDialogStyle);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *topLayout = new QHBoxLayout();

    // Left List
    mCodecList = new QListWidget(this);
    const struct CodecDisplayEntry {
        const char *display;
        const char *id;
        const char *fourcc;
        const char *pixfmt;
        bool delta;
    } kCodecs[] = {
        { "(Uncompressed RGB/YCbCr)", "(Uncompressed)", "DIB ", "rgb24", false },
        { "FFMPEG / Apple ProRes (iCodec Pro)", "prores_ks", "apch", "yuv422p10le", false },
        { "FFMPEG / VP8", "libvpx", "VP80", "yuv420p", true },
        { "FFMPEG / VP9", "libvpx-vp9", "VP90", "yuv420p", true },
        { "FFMPEG / x265", "libx265", "hvc1", "yuv420p", true },
        { "FFMPEG / x265 lossless", "libx265_lossless", "hvc1", "yuv420p", false },
        { "FFMPEG FFV1 lossless codec", "ffv1", "ffv1", "yuv420p", false },
        { "FFMPEG Huffyuv lossless codec", "huffyuv", "hfyu", "yuv422p", false },
        { "GoPro CineForm (native)", "cfhd", "CFHD", "yuv422p10le", false },
        { "Lagarith Lossless Codec", "lagarith", "LAGS", "yuv420p", false },
        { "x264 10 bit - H.264/MPEG-4 AVC codec", "libx264_10bit", "avc1", "yuv420p10le", true },
        { "x264 8 bit - H.264/MPEG-4 AVC codec", "libx264", "avc1", "yuv420p", true }
    };

    for (const auto& entry : kCodecs) {
        QListWidgetItem *item = new QListWidgetItem(entry.display, mCodecList);
        item->setData(Qt::UserRole, entry.id);
        item->setData(Qt::UserRole + 1, entry.fourcc);
        item->setData(Qt::UserRole + 2, entry.pixfmt);
        item->setData(Qt::UserRole + 3, entry.delta);
    }
    topLayout->addWidget(mCodecList, 1);

    // Right Box: Video codec information
    QGroupBox *infoGroup = new QGroupBox("Video codec information", this);
    QVBoxLayout *infoGroupLayout = new QVBoxLayout(infoGroup);

    QHBoxLayout *topInfoRow = new QHBoxLayout();
    QGridLayout *gridInfo = new QGridLayout();

    gridInfo->addWidget(new QLabel("Delta frames", this), 0, 0);
    mLabelDeltaFrames = new QLabel("No", this);
    gridInfo->addWidget(mLabelDeltaFrames, 0, 1);

    gridInfo->addWidget(new QLabel("FOURCC code", this), 1, 0);
    mLabelFourCC = new QLabel("'apch'", this);
    gridInfo->addWidget(mLabelFourCC, 1, 1);

    gridInfo->addWidget(new QLabel("Driver name", this), 2, 0);
    mLabelDriverName = new QLabel("avlib-1.vdplugin", this);
    gridInfo->addWidget(mLabelDriverName, 2, 1);

    topInfoRow->addLayout(gridInfo);

    btnAbout = new QPushButton("About", this);
    btnAbout->setFixedWidth(80);
    topInfoRow->addWidget(btnAbout, 0, Qt::AlignTop);

    infoGroupLayout->addLayout(topInfoRow);

    mInfoText = new QLabel("Works with VD formats", this);
    mInfoText->setStyleSheet("border: 1px solid #777; background-color: #222; padding: 12px;");
    mInfoText->setMinimumHeight(70);
    infoGroupLayout->addWidget(mInfoText);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnConfigure = new QPushButton("Configure", this);
    btnPixelFormat = new QPushButton("Pixel Format", this);
    mLabelPixFmtText = new QLabel("YUV422P16", this);

    btnRow->addWidget(btnConfigure);
    btnRow->addWidget(btnPixelFormat);
    btnRow->addWidget(mLabelPixFmtText);
    btnRow->addStretch();

    infoGroupLayout->addLayout(btnRow);

    topLayout->addWidget(infoGroup, 1);
    mainLayout->addLayout(topLayout);

    // Bottom Controls & Radio Buttons
    QHBoxLayout *bottomRow = new QHBoxLayout();

    QVBoxLayout *radioBox = new QVBoxLayout();
    mRadioFiltered = new QRadioButton("Similar to source: RGB24", this);
    mRadioShowAll = new QRadioButton("Show all formats", this);
    mRadioShowAll->setChecked(true);
    radioBox->addWidget(mRadioFiltered);
    radioBox->addWidget(mRadioShowAll);
    bottomRow->addLayout(radioBox);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &VDVideoCompressionDialog::onSaveClicked);
    connect(box, &QDialogButtonBox::rejected, this, &VDVideoCompressionDialog::reject);
    bottomRow->addWidget(box);

    mainLayout->addLayout(bottomRow);

    connect(mCodecList, &QListWidget::currentRowChanged, this, &VDVideoCompressionDialog::onCodecSelectionChanged);
    connect(btnConfigure, &QPushButton::clicked, this, &VDVideoCompressionDialog::onConfigureClicked);
    connect(btnPixelFormat, &QPushButton::clicked, this, &VDVideoCompressionDialog::onPixelFormatClicked);
    connect(btnAbout, &QPushButton::clicked, this, [this]() {
        QListWidgetItem *item = mCodecList->currentItem();
        if (item) {
            QMessageBox::information(this, "About Codec", QString("Codec: %1\nFOURCC: %2\nDriver: %3")
                .arg(item->text())
                .arg(item->data(Qt::UserRole + 1).toString())
                .arg("avlib-1.vdplugin"));
        }
    });

    // Select default codec
    VDVideoCodecParams params = VDQtCodecEngine::instance().getVideoParams();
    for (int i = 0; i < mCodecList->count(); ++i) {
        if (mCodecList->item(i)->data(Qt::UserRole).toString() == params.codecId) {
            mCodecList->setCurrentRow(i);
            break;
        }
    }
    if (mCodecList->currentRow() < 0) mCodecList->setCurrentRow(1); // Default to ProRes

    onCodecSelectionChanged();
}

void VDVideoCompressionDialog::onCodecSelectionChanged() {
    QListWidgetItem *item = mCodecList->currentItem();
    if (!item) return;

    QString codecId = item->data(Qt::UserRole).toString();
    QString fourcc = item->data(Qt::UserRole + 1).toString();
    QString defaultPixfmt = item->data(Qt::UserRole + 2).toString();
    bool delta = item->data(Qt::UserRole + 3).toBool();

    VDVideoCodecParams params = VDQtCodecEngine::instance().getVideoParamsForCodec(codecId);
    if (params.pixFmt.isEmpty()) params.pixFmt = defaultPixfmt;

    mLabelDeltaFrames->setText(delta ? "Yes" : "No");
    mLabelFourCC->setText(QString("'%1'").arg(fourcc));
    mLabelPixFmtText->setText(params.pixFmt.toUpper());
}

void VDVideoCompressionDialog::onPixelFormatClicked() {
    QListWidgetItem *item = mCodecList->currentItem();
    if (!item) return;
    QString codecId = item->data(Qt::UserRole).toString();

    QDialog dlg(this);
    dlg.setWindowTitle("Pixel Format & Bit Depth");
    dlg.setStyleSheet(kDialogStyle);
    QFormLayout *fl = new QFormLayout(&dlg);

    QComboBox *combo = new QComboBox(&dlg);
    combo->addItem("YUV420P (8-bit 4:2:0 Planar)", "yuv420p");
    combo->addItem("YUV422P (8-bit 4:2:2 Planar)", "yuv422p");
    combo->addItem("YUV444P (8-bit 4:4:4 Planar)", "yuv444p");
    combo->addItem("YUV420P10 (10-bit 4:2:0 Little-Endian)", "yuv420p10le");
    combo->addItem("YUV422P10 (10-bit 4:2:2 Little-Endian)", "yuv422p10le");
    combo->addItem("YUV444P10 (10-bit 4:4:4 Little-Endian)", "yuv444p10le");
    combo->addItem("YUV422P16 (16-bit 4:2:2 Little-Endian)", "yuv422p16le");
    combo->addItem("RGB24 (8-bit Packed RGB)", "rgb24");
    combo->addItem("RGBA (8-bit Packed RGB + Alpha)", "rgba");

    VDVideoCodecParams currentParams = VDQtCodecEngine::instance().getVideoParamsForCodec(codecId);
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toString() == currentParams.pixFmt.toLower()) {
            combo->setCurrentIndex(i);
            break;
        }
    }

    fl->addRow("Pixel Format / Bit Depth:", combo);

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    fl->addRow(bb);

    if (dlg.exec() == QDialog::Accepted) {
        QString chosenFmt = combo->itemData(combo->currentIndex()).toString();
        mLabelPixFmtText->setText(chosenFmt.toUpper());
        currentParams.pixFmt = chosenFmt;
        VDQtCodecEngine::instance().setVideoParamsForCodec(codecId, currentParams);
    }
}

void VDVideoCompressionDialog::onConfigureClicked() {
    QListWidgetItem *item = mCodecList->currentItem();
    if (!item) return;
    QString codecId = item->data(Qt::UserRole).toString();

    VDVideoCodecParams params = VDQtCodecEngine::instance().getVideoParamsForCodec(codecId);

    if (codecId == "prores_ks") {
        QDialog dlg(this);
        dlg.setWindowTitle("Apple ProRes Codec Configuration");
        dlg.setStyleSheet(kDialogStyle);
        QFormLayout *fl = new QFormLayout(&dlg);

        QComboBox *pCombo = new QComboBox(&dlg);
        pCombo->addItem("Proxy (Profile 0)", 0);
        pCombo->addItem("LT (Profile 1)", 1);
        pCombo->addItem("Standard / SQ (Profile 2)", 2);
        pCombo->addItem("HQ (Profile 3)", 3);
        pCombo->addItem("4444 (Profile 4)", 4);
        pCombo->addItem("4444 XQ (Profile 5)", 5);
        pCombo->setCurrentIndex(std::clamp(params.proresProfile, 0, 5));

        fl->addRow("ProRes Quality Profile:", pCombo);

        QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        fl->addRow(bb);

        if (dlg.exec() == QDialog::Accepted) {
            params.proresProfile = pCombo->currentData().toInt();
            VDQtCodecEngine::instance().setVideoParamsForCodec(codecId, params);
        }
    } else if (codecId == "libx264" || codecId == "libx265" || codecId == "libx264_10bit" || codecId == "libx265_lossless") {
        QDialog dlg(this);
        dlg.setWindowTitle(QString("%1 Codec Configuration").arg(codecId.contains("x265") ? "x265 / HEVC" : "x264 / AVC"));
        dlg.setStyleSheet(kDialogStyle);
        QFormLayout *fl = new QFormLayout(&dlg);

        QSpinBox *crfBox = new QSpinBox(&dlg);
        crfBox->setRange(0, 51);
        crfBox->setValue(params.crf);

        QComboBox *presetCombo = new QComboBox(&dlg);
        presetCombo->addItems({"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"});
        int pIdx = presetCombo->findText(params.preset);
        if (pIdx >= 0) presetCombo->setCurrentIndex(pIdx);
        else presetCombo->setCurrentIndex(5); // medium

        QSpinBox *keyframeBox = new QSpinBox(&dlg);
        keyframeBox->setRange(0, 1000);
        keyframeBox->setValue(params.keyframeInterval > 0 ? params.keyframeInterval : 250);

        fl->addRow("Constant Rate Factor (CRF 0..51):", crfBox);
        fl->addRow("Speed Preset:", presetCombo);
        fl->addRow("Keyframe Interval (GOP, 250 default):", keyframeBox);

        QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        fl->addRow(bb);

        if (dlg.exec() == QDialog::Accepted) {
            params.crf = crfBox->value();
            params.preset = presetCombo->currentText();
            params.keyframeInterval = keyframeBox->value();
            VDQtCodecEngine::instance().setVideoParamsForCodec(codecId, params);
        }
    } else if (codecId == "libvpx" || codecId == "libvpx-vp9") {
        QDialog dlg(this);
        dlg.setWindowTitle("VP8 / VP9 Codec Configuration");
        dlg.setStyleSheet(kDialogStyle);
        QFormLayout *fl = new QFormLayout(&dlg);

        QSpinBox *crfBox = new QSpinBox(&dlg);
        crfBox->setRange(0, 63);
        crfBox->setValue(params.crf);

        QSpinBox *keyframeBox = new QSpinBox(&dlg);
        keyframeBox->setRange(0, 1000);
        keyframeBox->setValue(params.keyframeInterval > 0 ? params.keyframeInterval : (codecId == "libvpx-vp9" ? 240 : 120));

        fl->addRow("Constant Rate Factor (CRF 0..63):", crfBox);
        fl->addRow("Keyframe Interval (GOP):", keyframeBox);

        QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        fl->addRow(bb);

        if (dlg.exec() == QDialog::Accepted) {
            params.crf = crfBox->value();
            params.keyframeInterval = keyframeBox->value();
            VDQtCodecEngine::instance().setVideoParamsForCodec(codecId, params);
        }
    } else if (codecId == "ffv1") {
        QDialog dlg(this);
        dlg.setWindowTitle("FFV1 Codec Configuration");
        dlg.setStyleSheet(kDialogStyle);
        QFormLayout *fl = new QFormLayout(&dlg);

        QComboBox *verCombo = new QComboBox(&dlg);
        verCombo->addItem("Version 1 (Legacy)", 1);
        verCombo->addItem("Version 3 (Standard / Multi-threaded)", 3);
        verCombo->setCurrentIndex(params.ffv1Version == 1 ? 0 : 1);

        QComboBox *coderCombo = new QComboBox(&dlg);
        coderCombo->addItem("Golomb-Rice (Fast)", 0);
        coderCombo->addItem("Range Coder (Better compression)", 1);
        coderCombo->setCurrentIndex(params.ffv1Coder == 0 ? 0 : 1);

        QSpinBox *slicesSpin = new QSpinBox(&dlg);
        slicesSpin->setRange(1, 64);
        slicesSpin->setValue(params.ffv1Slices > 0 ? params.ffv1Slices : 16);

        fl->addRow("FFV1 Version:", verCombo);
        fl->addRow("Entropy Coder:", coderCombo);
        fl->addRow("Multi-threading Slices:", slicesSpin);

        QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        fl->addRow(bb);

        if (dlg.exec() == QDialog::Accepted) {
            params.ffv1Version = verCombo->currentData().toInt();
            params.ffv1Coder = coderCombo->currentData().toInt();
            params.ffv1Slices = slicesSpin->value();
            VDQtCodecEngine::instance().setVideoParamsForCodec(codecId, params);
        }
    } else {
        QMessageBox::information(this, "Codec Configuration", "Standard options active for this codec.");
    }
}

void VDVideoCompressionDialog::onSaveClicked() {
    QListWidgetItem *item = mCodecList->currentItem();
    if (item) {
        QString codecId = item->data(Qt::UserRole).toString();
        VDVideoCodecParams params = VDQtCodecEngine::instance().getVideoParamsForCodec(codecId);
        VDQtCodecEngine::instance().setVideoParams(params);
    }
    accept();
}

// -----------------------------------------------------------------------------
// VDAudioCompressionDialog
// -----------------------------------------------------------------------------
VDAudioCompressionDialog::VDAudioCompressionDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Select Audio Compression & Codec Settings");
    resize(580, 420);
    setStyleSheet(kDialogStyle);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *topLayout = new QHBoxLayout();

    // Left List
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->addWidget(new QLabel("Select Audio Codec:", this));
    mCodecList = new QListWidget(this);
    struct AudioCodecEntry {
        const char *name;
        const char *id;
    } const kAudioCodecs[] = {
        { "PCM Uncompressed (pcm_s16le)", "pcm_s16le" },
        { "AAC (Advanced Audio Coding)", "aac" },
        { "MP3 (libmp3lame)", "libmp3lame" },
        { "Opus Audio Codec", "libopus" },
        { "Vorbis (Ogg Vorbis)", "libvorbis" },
        { "AC3 (Dolby Digital)", "ac3" },
        { "FLAC Lossless Audio", "flac" }
    };

    for (const auto& entry : kAudioCodecs) {
        QListWidgetItem *item = new QListWidgetItem(entry.name, mCodecList);
        item->setData(Qt::UserRole, entry.id);
    }
    leftLayout->addWidget(mCodecList);
    topLayout->addLayout(leftLayout, 1);

    // Right Settings
    QVBoxLayout *rightLayout = new QVBoxLayout();

    // Bitrate / Quality Box
    mBitrateGroup = new QGroupBox("Bitrate & Quality Mode", this);
    QVBoxLayout *bLayout = new QVBoxLayout(mBitrateGroup);

    mRadioVbr = new QRadioButton("Variable Bitrate (VBR Quality Mode)", this);
    mRadioCbr = new QRadioButton("Constant Bitrate (CBR Mode)", this);

    QHBoxLayout *vbrLayout = new QHBoxLayout();
    lblVbr = new QLabel("VBR Quality Preset:", this);
    mVbrCombo = new QComboBox(this);
    vbrLayout->addWidget(lblVbr);
    vbrLayout->addWidget(mVbrCombo, 1);

    QHBoxLayout *cbrLayout = new QHBoxLayout();
    cbrLayout->addWidget(new QLabel("Target Bitrate:", this));
    mCbrBitrateCombo = new QComboBox(this);
    mCbrBitrateCombo->addItem("64 kbps", 64);
    mCbrBitrateCombo->addItem("96 kbps", 96);
    mCbrBitrateCombo->addItem("128 kbps", 128);
    mCbrBitrateCombo->addItem("160 kbps", 160);
    mCbrBitrateCombo->addItem("192 kbps", 192);
    mCbrBitrateCombo->addItem("256 kbps", 256);
    mCbrBitrateCombo->addItem("320 kbps", 320);
    cbrLayout->addWidget(mCbrBitrateCombo, 1);

    bLayout->addWidget(mRadioVbr);
    bLayout->addLayout(vbrLayout);
    bLayout->addWidget(mRadioCbr);
    bLayout->addLayout(cbrLayout);

    rightLayout->addWidget(mBitrateGroup);

    // Format Box
    QGroupBox *fmtGroup = new QGroupBox("Audio Resampling & Channels", this);
    QFormLayout *fmtForm = new QFormLayout(fmtGroup);

    mSampleRateCombo = new QComboBox(this);
    mSampleRateCombo->addItem("Same as Source", 0);
    mSampleRateCombo->addItem("44.1 kHz", 44100);
    mSampleRateCombo->addItem("48.0 kHz", 48000);
    mSampleRateCombo->addItem("96.0 kHz", 96000);

    mChannelsCombo = new QComboBox(this);
    mChannelsCombo->addItem("Same as Source", 0);
    mChannelsCombo->addItem("Mono (1 Channel)", 1);
    mChannelsCombo->addItem("Stereo (2 Channels)", 2);
    mChannelsCombo->addItem("5.1 Surround (6 Channels)", 6);

    fmtForm->addRow("Sample Rate:", mSampleRateCombo);
    fmtForm->addRow("Channels:", mChannelsCombo);

    rightLayout->addWidget(fmtGroup);
    topLayout->addLayout(rightLayout, 1);

    mainLayout->addLayout(topLayout);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &VDAudioCompressionDialog::onSaveClicked);
    // Buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton *btnOk = new QPushButton("OK", this);
    btnOk->setDefault(true);
    QPushButton *btnCancel = new QPushButton("Cancel", this);
    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnCancel);
    mainLayout->addLayout(btnLayout);

    connect(mRadioVbr, &QRadioButton::toggled, this, &VDAudioCompressionDialog::onRateControlModeChanged);
    connect(mRadioCbr, &QRadioButton::toggled, this, &VDAudioCompressionDialog::onRateControlModeChanged);
    connect(mCodecList, &QListWidget::currentRowChanged, this, &VDAudioCompressionDialog::onCodecSelectionChanged);
    connect(btnOk, &QPushButton::clicked, this, &VDAudioCompressionDialog::onSaveClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    // Load initial values
    VDAudioCodecConfig cfg = VDQtCodecSettings::instance().getAudioConfig();
    int selRow = 0;
    for (int i = 0; i < mCodecList->count(); ++i) {
        if (mCodecList->item(i)->data(Qt::UserRole).toString() == cfg.codecId) {
            selRow = i;
            break;
        }
    }
    mCodecList->setCurrentRow(selRow);

    if (cfg.rateControlMode == "vbr") mRadioVbr->setChecked(true);
    else mRadioCbr->setChecked(true);

    onCodecSelectionChanged();

    for (int i = 0; i < mVbrCombo->count(); ++i) {
        if (mVbrCombo->itemData(i).toInt() == cfg.vbrQuality) {
            mVbrCombo->setCurrentIndex(i);
            break;
        }
    }

    for (int i = 0; i < mCbrBitrateCombo->count(); ++i) {
        if (mCbrBitrateCombo->itemData(i).toInt() == cfg.bitrateKbps) {
            mCbrBitrateCombo->setCurrentIndex(i);
            break;
        }
    }

    for (int i = 0; i < mSampleRateCombo->count(); ++i) {
        if (mSampleRateCombo->itemData(i).toInt() == cfg.sampleRate) {
            mSampleRateCombo->setCurrentIndex(i);
            break;
        }
    }

    for (int i = 0; i < mChannelsCombo->count(); ++i) {
        if (mChannelsCombo->itemData(i).toInt() == cfg.channels) {
            mChannelsCombo->setCurrentIndex(i);
            break;
        }
    }

    onRateControlModeChanged();
}

void VDAudioCompressionDialog::onCodecSelectionChanged() {
    QListWidgetItem *item = mCodecList->currentItem();
    if (!item) return;

    QString codecId = item->data(Qt::UserRole).toString();
    bool isCompressed = (codecId != "pcm_s16le");
    mBitrateGroup->setEnabled(isCompressed);

    int curVal = mVbrCombo->currentData().isValid() ? mVbrCombo->currentData().toInt() : 2;
    mVbrCombo->clear();

    if (codecId == "libmp3lame") { // MP3
        mRadioVbr->setEnabled(true);
        mRadioCbr->setEnabled(true);
        lblVbr->setText("VBR Quality Preset:");
        mVbrCombo->addItem("V0 (~245 kbps, Extreme Quality)", 0);
        mVbrCombo->addItem("V1 (~225 kbps, Very High Quality)", 1);
        mVbrCombo->addItem("V2 (~190 kbps, Standard / Recommended)", 2);
        mVbrCombo->addItem("V3 (~175 kbps)", 3);
        mVbrCombo->addItem("V4 (~165 kbps, Medium Quality)", 4);
        mVbrCombo->addItem("V5 (~130 kbps)", 5);
        mVbrCombo->addItem("V6 (~115 kbps)", 6);
        mVbrCombo->addItem("V7 (~100 kbps)", 7);
        mVbrCombo->addItem("V8 (~85 kbps)", 8);
        mVbrCombo->addItem("V9 (~65 kbps, Low Bitrate)", 9);
        for (int i = 0; i < mVbrCombo->count(); ++i) {
            if (mVbrCombo->itemData(i).toInt() == curVal) { mVbrCombo->setCurrentIndex(i); break; }
        }
        if (mVbrCombo->currentIndex() < 0) mVbrCombo->setCurrentIndex(2); // V2 default
    } else if (codecId == "aac") { // AAC
        mRadioVbr->setEnabled(true);
        mRadioCbr->setEnabled(true);
        lblVbr->setText("VBR Quality Level:");
        mVbrCombo->addItem("Q5 (~256 kbps, Maximum Quality)", 5);
        mVbrCombo->addItem("Q4 (~192 kbps, High Quality)", 4);
        mVbrCombo->addItem("Q3 (~128 kbps, Medium Quality)", 3);
        mVbrCombo->addItem("Q2 (~96 kbps, Low Bitrate)", 2);
        mVbrCombo->addItem("Q1 (~64 kbps, Minimum Bitrate)", 1);
        for (int i = 0; i < mVbrCombo->count(); ++i) {
            if (mVbrCombo->itemData(i).toInt() == curVal) { mVbrCombo->setCurrentIndex(i); break; }
        }
        if (mVbrCombo->currentIndex() < 0) mVbrCombo->setCurrentIndex(1); // Q4 default
    } else if (codecId == "libopus") { // Opus
        mRadioVbr->setEnabled(true);
        mRadioCbr->setEnabled(true);
        lblVbr->setText("Rate Control Mode:");
        mVbrCombo->addItem("Constrained VBR (Recommended for Opus)", 1);
        mVbrCombo->addItem("Hard CBR (Strict Constant Bitrate)", 0);
        mVbrCombo->setCurrentIndex(0);
    } else if (codecId == "libvorbis") { // Vorbis
        mRadioVbr->setEnabled(true);
        mRadioCbr->setEnabled(true);
        lblVbr->setText("VBR Quality Level:");
        mVbrCombo->addItem("Q10 (~500 kbps, Maximum Quality)", 10);
        mVbrCombo->addItem("Q8 (~256 kbps, Very High Quality)", 8);
        mVbrCombo->addItem("Q6 (~192 kbps, High Quality)", 6);
        mVbrCombo->addItem("Q5 (~160 kbps, Standard / Recommended)", 5);
        mVbrCombo->addItem("Q4 (~128 kbps, Medium Quality)", 4);
        mVbrCombo->addItem("Q2 (~96 kbps, Low Bitrate)", 2);
        mVbrCombo->addItem("Q0 (~64 kbps, Minimum Bitrate)", 0);
        for (int i = 0; i < mVbrCombo->count(); ++i) {
            if (mVbrCombo->itemData(i).toInt() == curVal) { mVbrCombo->setCurrentIndex(i); break; }
        }
        if (mVbrCombo->currentIndex() < 0) mVbrCombo->setCurrentIndex(3); // Q5 default
    } else if (codecId == "ac3") { // AC3
        mRadioVbr->setEnabled(false);
        mRadioCbr->setChecked(true);
        mRadioCbr->setEnabled(true);
        lblVbr->setText("VBR (Not Supported by AC3)");
    } else if (codecId == "flac") { // FLAC
        mRadioVbr->setEnabled(true);
        mRadioVbr->setChecked(true);
        mRadioCbr->setEnabled(false);
        lblVbr->setText("Compression Level:");
        mVbrCombo->addItem("Level 8 (Maximum Compression / Smallest File)", 8);
        mVbrCombo->addItem("Level 7 (Very High Compression)", 7);
        mVbrCombo->addItem("Level 6 (High Compression)", 6);
        mVbrCombo->addItem("Level 5 (Default / Recommended)", 5);
        mVbrCombo->addItem("Level 4 (Medium Compression)", 4);
        mVbrCombo->addItem("Level 3 (Fast Encoding)", 3);
        mVbrCombo->addItem("Level 2 (Faster Encoding)", 2);
        mVbrCombo->addItem("Level 1 (Very Fast Encoding)", 1);
        mVbrCombo->addItem("Level 0 (Fastest / Minimal CPU)", 0);
        for (int i = 0; i < mVbrCombo->count(); ++i) {
            if (mVbrCombo->itemData(i).toInt() == curVal) { mVbrCombo->setCurrentIndex(i); break; }
        }
        if (mVbrCombo->currentIndex() < 0) mVbrCombo->setCurrentIndex(3); // Default Level 5
    }

    int curSampleRate = mSampleRateCombo->currentData().isValid() ? mSampleRateCombo->currentData().toInt() : 0;
    mSampleRateCombo->clear();
    if (codecId == "libopus") {
        mSampleRateCombo->addItem("Same as Source (Default)", 0);
        mSampleRateCombo->addItem("48.0 kHz (Fullband / Recommended)", 48000);
        mSampleRateCombo->addItem("24.0 kHz (Superwideband)", 24000);
        mSampleRateCombo->addItem("16.0 kHz (Wideband)", 16000);
        mSampleRateCombo->addItem("12.0 kHz (Mediumband)", 12000);
        mSampleRateCombo->addItem("8.0 kHz (Narrowband)", 8000);
    } else if (codecId == "ac3") {
        mSampleRateCombo->addItem("Same as Source (Default)", 0);
        mSampleRateCombo->addItem("48.0 kHz (Recommended)", 48000);
        mSampleRateCombo->addItem("44.1 kHz", 44100);
        mSampleRateCombo->addItem("32.0 kHz", 32000);
    } else if (codecId == "libmp3lame") {
        mSampleRateCombo->addItem("Same as Source (Default)", 0);
        mSampleRateCombo->addItem("48.0 kHz", 48000);
        mSampleRateCombo->addItem("44.1 kHz", 44100);
        mSampleRateCombo->addItem("32.0 kHz", 32000);
        mSampleRateCombo->addItem("24.0 kHz", 24000);
        mSampleRateCombo->addItem("22.05 kHz", 22050);
        mSampleRateCombo->addItem("16.0 kHz", 16000);
        mSampleRateCombo->addItem("12.0 kHz", 12000);
        mSampleRateCombo->addItem("11.025 kHz", 11025);
        mSampleRateCombo->addItem("8.0 kHz", 8000);
    } else if (codecId == "aac") {
        mSampleRateCombo->addItem("Same as Source (Default)", 0);
        mSampleRateCombo->addItem("96.0 kHz", 96000);
        mSampleRateCombo->addItem("88.2 kHz", 88200);
        mSampleRateCombo->addItem("48.0 kHz", 48000);
        mSampleRateCombo->addItem("44.1 kHz", 44100);
        mSampleRateCombo->addItem("32.0 kHz", 32000);
        mSampleRateCombo->addItem("24.0 kHz", 24000);
        mSampleRateCombo->addItem("22.05 kHz", 22050);
        mSampleRateCombo->addItem("16.0 kHz", 16000);
        mSampleRateCombo->addItem("12.0 kHz", 12000);
        mSampleRateCombo->addItem("11.025 kHz", 11025);
        mSampleRateCombo->addItem("8.0 kHz", 8000);
    } else {
        mSampleRateCombo->addItem("Same as Source (Default)", 0);
        mSampleRateCombo->addItem("192.0 kHz", 192000);
        mSampleRateCombo->addItem("96.0 kHz", 96000);
        mSampleRateCombo->addItem("88.2 kHz", 88200);
        mSampleRateCombo->addItem("48.0 kHz", 48000);
        mSampleRateCombo->addItem("44.1 kHz", 44100);
        mSampleRateCombo->addItem("32.0 kHz", 32000);
        mSampleRateCombo->addItem("24.0 kHz", 24000);
        mSampleRateCombo->addItem("22.05 kHz", 22050);
        mSampleRateCombo->addItem("16.0 kHz", 16000);
        mSampleRateCombo->addItem("12.0 kHz", 12000);
        mSampleRateCombo->addItem("11.025 kHz", 11025);
        mSampleRateCombo->addItem("8.0 kHz", 8000);
    }
    int idx = mSampleRateCombo->findData(curSampleRate);
    mSampleRateCombo->setCurrentIndex(idx >= 0 ? idx : 0);

    onRateControlModeChanged();
}

void VDAudioCompressionDialog::onRateControlModeChanged() {
    bool isVbr = mRadioVbr->isChecked();
    mVbrCombo->setEnabled(isVbr);
    lblVbr->setEnabled(isVbr);
    mCbrBitrateCombo->setEnabled(!isVbr);
}

void VDAudioCompressionDialog::onSaveClicked() {
    QListWidgetItem *item = mCodecList->currentItem();
    if (!item) return;

    VDAudioCodecConfig cfg;
    cfg.codecId = item->data(Qt::UserRole).toString();
    cfg.codecName = item->text();

    QString err;
    if (!VDQtCodecEngine::instance().checkAudioEncoderAvailable(cfg.codecId, &err)) {
        QMessageBox::critical(this, "Audio Encoder Not Available", err);
        return;
    }

    cfg.rateControlMode = mRadioVbr->isChecked() ? "vbr" : "cbr";
    cfg.vbrQuality = mVbrCombo->currentData().toInt();
    cfg.bitrateKbps = mCbrBitrateCombo->currentData().toInt();
    cfg.sampleRate = mSampleRateCombo->currentData().toInt();
    cfg.channels = mChannelsCombo->currentData().toInt();

    VDQtCodecSettings::instance().setAudioConfig(cfg);

    VDAudioCodecParams aParams;
    aParams.codecId = cfg.codecId;
    aParams.rateMode = cfg.rateControlMode;
    aParams.vbrQuality = cfg.vbrQuality;
    aParams.bitrateKbps = cfg.bitrateKbps;
    aParams.sampleRate = cfg.sampleRate;
    aParams.channels = cfg.channels;
    VDQtCodecEngine::instance().setAudioParams(aParams);

    accept();
}

// -----------------------------------------------------------------------------
// VDGoToFrameDialog
// -----------------------------------------------------------------------------
VDGoToFrameDialog::VDGoToFrameDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Go To Position");
    resize(340, 180);
    setStyleSheet(kDialogStyle);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    mRadioFrame = new QRadioButton("Jump to Frame Number:", this);
    mRadioFrame->setChecked(true);
    mFrameSpin = new QSpinBox(this);
    mFrameSpin->setRange(0, 10000000);
    mainLayout->addWidget(mRadioFrame);
    mainLayout->addWidget(mFrameSpin);

    mRadioTime = new QRadioButton("Jump to Time (hh:mm:ss.ms):", this);
    mTimeEdit = new QLineEdit("00:00:00.000", this);
    mainLayout->addWidget(mRadioTime);
    mainLayout->addWidget(mTimeEdit);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(box);
}

int VDGoToFrameDialog::getFrameNumber() const {
    return mFrameSpin->value();
}

// -----------------------------------------------------------------------------
// VDAboutDialog
// -----------------------------------------------------------------------------
VDAboutDialog::VDAboutDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("About VirtualDub (Native Linux Port)");
    resize(480, 340);
    setStyleSheet(kDialogStyle);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *titleLabel = new QLabel("VirtualDubQt", this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #00bcd4;");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    QLabel *verLabel = new QLabel("Version 0.1 (Native Modern C++/Qt6 Linux Port)", this);
    verLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(verLabel);

    QTextEdit *text = new QTextEdit(this);
    text->setReadOnly(true);
    text->setHtml(
        "<p><b>VirtualDub</b> is a powerful video capture and utility software.</p>"
        "<p>Original Authors: Avery Lee, Anton Shekhovtsov, v0lt.</p>"
        "<p>Native Linux C++/Qt6 Architecture Port.</p>"
        "<p>Licensed under GNU General Public License v3.0 (GPLv3).</p>"
    );
    mainLayout->addWidget(text);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    mainLayout->addWidget(box);
}

// -----------------------------------------------------------------------------
// VDLogWindow
// -----------------------------------------------------------------------------
VDLogWindow* VDLogWindow::sInstance = nullptr;

VDLogWindow::VDLogWindow(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("VirtualDub Log Window");
    resize(600, 360);
    setStyleSheet(kDialogStyle);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mLogText = new QTextEdit(this);
    mLogText->setReadOnly(true);
    mLogText->setFontFamily("Monospace");
    mainLayout->addWidget(mLogText);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnClear = new QPushButton("Clear", this);
    QPushButton *btnCopy = new QPushButton("Copy to Clipboard", this);
    connect(btnClear, &QPushButton::clicked, mLogText, &QTextEdit::clear);
    connect(btnCopy, &QPushButton::clicked, [this]() {
        QApplication::clipboard()->setText(mLogText->toPlainText());
    });

    btnLayout->addWidget(btnClear);
    btnLayout->addWidget(btnCopy);
    btnLayout->addStretch();

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    btnLayout->addWidget(box);

    mainLayout->addLayout(btnLayout);
}

VDLogWindow* VDLogWindow::instance(QWidget *parent) {
    if (!sInstance) {
        sInstance = new VDLogWindow(parent);
    }
    return sInstance;
}

void VDLogWindow::appendLog(const QString &text) {
    if (mLogText) mLogText->append(text);
}

// -----------------------------------------------------------------------------
// VDSaveVideoDialog (Matching VirtualDub2 Save File (F7) Screenshot)
// -----------------------------------------------------------------------------
VDSaveVideoDialog::VDSaveVideoDialog(int videoMode, int audioMode, const QString &defaultDir, const QString &defaultBaseName, QWidget *parent)
    : QDialog(parent), mDefaultDir(defaultDir), mDefaultBaseName(defaultBaseName) {
    setWindowTitle("Save File");
    resize(640, 420);
    setStyleSheet(kDialogStyle);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // File selection row
    QFormLayout *formLayout = new QFormLayout();

    QHBoxLayout *fileRow = new QHBoxLayout();
    mFileNameEdit = new QLineEdit(this);
    mBrowseBtn = new QPushButton("Browse...", this);
    fileRow->addWidget(mFileNameEdit);
    fileRow->addWidget(mBrowseBtn);

    formLayout->addRow("File name:", fileRow);

    mFileTypeCombo = new QComboBox(this);
    mFileTypeCombo->addItem("Audio-Video Interleave (*.avi)", "avi");
    mFileTypeCombo->addItem("AVI handled by FFMPEG (*.avi)", "avi_ffmpeg");
    mFileTypeCombo->addItem("Matroska (*.mkv)", "mkv");
    mFileTypeCombo->addItem("WebM (*.webm)", "webm");
    mFileTypeCombo->addItem("QuickTime / MOV (*.mov)", "mov");
    mFileTypeCombo->addItem("MOV +faststart (*.mov)", "mov_faststart");
    mFileTypeCombo->addItem("MP4 (MPEG-4 Part 14) (*.mp4)", "mp4");
    mFileTypeCombo->addItem("MP4 +faststart (*.mp4)", "mp4_faststart");
    mFileTypeCombo->addItem("NUT (*.nut)", "nut");
    mFileTypeCombo->addItem("any format by FFMPEG (*.*)", "any");
    mFileTypeCombo->addItem("All files (*.*)", "all");

    // Read session settings
    VDSaveVideoSessionConfig vCfg = VDQtCodecSettings::instance().getSaveVideoSessionConfig();
    mFileTypeCombo->setCurrentIndex(std::clamp(vCfg.fileTypeIndex, 0, mFileTypeCombo->count() - 1));

    QString typeKey = mFileTypeCombo->currentData().toString();
    QString ext = "mp4";
    if (typeKey == "webm") ext = "webm";
    else if (typeKey == "mkv") ext = "mkv";
    else if (typeKey == "mov" || typeKey == "mov_faststart") ext = "mov";
    else if (typeKey == "mp4" || typeKey == "mp4_faststart") ext = "mp4";
    else if (typeKey == "nut") ext = "nut";
    else if (typeKey == "avi" || typeKey == "avi_ffmpeg") ext = "avi";

    QString initialName;
    if (!mDefaultBaseName.isEmpty()) {
        initialName = mDefaultBaseName + "." + ext;
    } else if (!vCfg.lastFileName.isEmpty()) {
        initialName = vCfg.lastFileName;
    } else {
        initialName = "output." + ext;
    }
    mFileNameEdit->setText(initialName);

    formLayout->addRow("Files of type:", mFileTypeCombo);

    mainLayout->addLayout(formLayout);

    // Video / Audio Info Summary (Bottom panels from VirtualDub2 screenshot)
    QHBoxLayout *infoRow = new QHBoxLayout();

    QGroupBox *videoGroup = new QGroupBox("Video", this);
    QFormLayout *vForm = new QFormLayout(videoGroup);

    VDVideoCodecParams vParams = VDQtCodecEngine::instance().getVideoParams();
    QString videoCompStr;
    if (videoMode == 0) {
        videoCompStr = "(Direct stream copy)";
    } else if (videoMode == 1) {
        videoCompStr = QString("%1 (Fast recompress)").arg(vParams.codecId);
    } else if (videoMode == 2) {
        videoCompStr = QString("%1 (Normal recompress)").arg(vParams.codecId);
    } else {
        videoCompStr = QString("%1 (Full processing)").arg(vParams.codecId);
    }
    mVideoCompressionLabel = new QLabel(videoCompStr, this);
    mVideoPixFmtLabel = new QLabel((videoMode == 0) ? "SOURCE" : vParams.pixFmt.toUpper(), this);

    vForm->addRow("Compression:", mVideoCompressionLabel);
    vForm->addRow("Pixel format:", mVideoPixFmtLabel);

    infoRow->addWidget(videoGroup);

    QGroupBox *audioGroup = new QGroupBox("Audio", this);
    QFormLayout *aForm = new QFormLayout(audioGroup);

    VDAudioCodecParams aParams = VDQtCodecEngine::instance().getAudioParams();
    QString audioCompStr;
    if (audioMode == 0) {
        audioCompStr = "(Direct stream copy)";
    } else {
        audioCompStr = QString("%1 (Full processing)").arg(aParams.codecId);
    }
    mAudioCompressionLabel = new QLabel(audioCompStr, this);
    mAudioSampleLayoutLabel = new QLabel(QString("%1Hz 16-bit %2ch")
        .arg(aParams.sampleRate > 0 ? aParams.sampleRate : 48000)
        .arg(aParams.channels > 0 ? aParams.channels : 2), this);

    aForm->addRow("Compression:", mAudioCompressionLabel);
    aForm->addRow("Sample layout:", mAudioSampleLayoutLabel);

    infoRow->addWidget(audioGroup);

    mainLayout->addLayout(infoRow);

    // Buttons
    QHBoxLayout *bottomRow = new QHBoxLayout();

    QDialogButtonBox *btnBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, [this]() {
        VDSaveVideoSessionConfig vCfg;
        vCfg.fileTypeIndex = mFileTypeCombo->currentIndex();
        vCfg.lastFileName = mFileNameEdit->text();
        VDQtCodecSettings::instance().setSaveVideoSessionConfig(vCfg);
        accept();
    });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    bottomRow->addWidget(btnBox);
    mainLayout->addLayout(bottomRow);

    connect(mBrowseBtn, &QPushButton::clicked, this, &VDSaveVideoDialog::onBrowseClicked);
    connect(mFileTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VDSaveVideoDialog::onFileTypeIndexChanged);
}

void VDSaveVideoDialog::onBrowseClicked() {
    QString filter = mFileTypeCombo->currentText();
    QString initial = mFileNameEdit->text();
    if (!mDefaultDir.isEmpty() && !QFileInfo(initial).isAbsolute()) {
        initial = QDir(mDefaultDir).filePath(initial);
    }
    QString path = QFileDialog::getSaveFileName(this, "Save File", initial, filter);
    if (!path.isEmpty()) {
        mFileNameEdit->setText(path);
    }
}

void VDSaveVideoDialog::onFileTypeIndexChanged(int) {
    QString currentName = mFileNameEdit->text();
    QFileInfo fi(currentName);
    QString baseName = fi.completeBaseName();
    if (baseName.isEmpty()) baseName = (!mDefaultBaseName.isEmpty()) ? mDefaultBaseName : "output";

    QString typeKey = mFileTypeCombo->currentData().toString();
    QString ext = "mp4";
    if (typeKey == "webm") ext = "webm";
    else if (typeKey == "mkv") ext = "mkv";
    else if (typeKey == "mov" || typeKey == "mov_faststart") ext = "mov";
    else if (typeKey == "mp4" || typeKey == "mp4_faststart") ext = "mp4";
    else if (typeKey == "nut") ext = "nut";
    else if (typeKey == "avi" || typeKey == "avi_ffmpeg") ext = "avi";

    if (fi.isAbsolute()) {
        mFileNameEdit->setText(fi.dir().filePath(baseName + "." + ext));
    } else {
        mFileNameEdit->setText(baseName + "." + ext);
    }
}

QString VDSaveVideoDialog::getSelectedFilePath() const {
    QString path = mFileNameEdit->text().trimmed();
    if (path.isEmpty()) return QString();
    if (!mDefaultDir.isEmpty() && !QFileInfo(path).isAbsolute()) {
        path = QDir(mDefaultDir).filePath(path);
    }
    return path;
}

QString VDSaveVideoDialog::getSelectedContainerType() const {
    return mFileTypeCombo->currentData().toString();
}

bool VDSaveVideoDialog::isFastStartEnabled() const {
    QString typeKey = mFileTypeCombo->currentData().toString();
    return (typeKey == "mov_faststart" || typeKey == "mp4_faststart");
}
