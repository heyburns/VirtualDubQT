#include "VDQtBatchWizard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QTableWidget>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString audioExtension(const VDAudioCodecParams& params) {
    const QString codec = params.codecId.toLower();
    if (codec.contains(QStringLiteral("mp3"))) return QStringLiteral("mp3");
    if (codec.contains(QStringLiteral("opus"))) return QStringLiteral("opus");
    if (codec.contains(QStringLiteral("vorbis"))) return QStringLiteral("ogg");
    if (codec.contains(QStringLiteral("flac"))) return QStringLiteral("flac");
    if (codec.contains(QStringLiteral("aac"))) return QStringLiteral("m4a");
    if (codec.contains(QStringLiteral("ac3"))) return QStringLiteral("ac3");
    return QStringLiteral("wav");
}

QString replaceText(QString input, const QString& search,
                    const QString& replacement, Qt::CaseSensitivity sensitivity) {
    if (search.isEmpty()) return input;
    qsizetype position = 0;
    while ((position = input.indexOf(search, position, sensitivity)) >= 0) {
        input.replace(position, search.size(), replacement);
        position += replacement.size();
    }
    return input;
}

} // namespace

VDQtBatchWizardDialog::VDQtBatchWizardDialog(
    const VDQtJobState& processingTemplate,
    const QList<VDQtJobState>& existingJobs,
    QWidget *parent)
    : QDialog(parent),
      mTemplate(processingTemplate),
      mExistingJobs(existingJobs) {
    setWindowTitle(QStringLiteral("Batch Wizard"));
    resize(900, 540);
    setAcceptDrops(true);

    auto *outer = new QVBoxLayout(this);
    auto *settings = new QGroupBox(QStringLiteral("Batch operation"), this);
    auto *form = new QFormLayout(settings);
    mOperation = new QComboBox(settings);
    mOperation->addItem(QStringLiteral("Re-save using current settings"),
                        static_cast<int>(VDQtJobOperation::VideoExport));
    mOperation->addItem(QStringLiteral("Extract processed audio"),
                        static_cast<int>(VDQtJobOperation::AudioExport));
    mOperation->addItem(QStringLiteral("Export raw video"),
                        static_cast<int>(VDQtJobOperation::RawVideoExport));
    mOperation->addItem(QStringLiteral("Export image sequence"),
                        static_cast<int>(VDQtJobOperation::ImageSequenceExport));
    mOperation->addItem(QStringLiteral("Run video analysis pass"),
                        static_cast<int>(VDQtJobOperation::VideoAnalysis));
    form->addRow(QStringLiteral("Operation:"), mOperation);

    mContainer = new QComboBox(settings);
    mContainer->addItem(QStringLiteral("Matroska (*.mkv)"), QStringLiteral("mkv"));
    mContainer->addItem(QStringLiteral("MP4 +faststart (*.mp4)"), QStringLiteral("mp4_faststart"));
    mContainer->addItem(QStringLiteral("QuickTime / MOV (*.mov)"), QStringLiteral("mov"));
    mContainer->addItem(QStringLiteral("WebM (*.webm)"), QStringLiteral("webm"));
    mContainer->addItem(QStringLiteral("AVI (*.avi)"), QStringLiteral("avi"));
    mContainer->addItem(QStringLiteral("NUT (*.nut)"), QStringLiteral("nut"));
    form->addRow(QStringLiteral("Output container:"), mContainer);

    mImageFormat = new QComboBox(settings);
    mImageFormat->addItem(QStringLiteral("PNG (*.png)"), QStringLiteral("png"));
    mImageFormat->addItem(QStringLiteral("Windows Bitmap (*.bmp)"), QStringLiteral("bmp"));
    mImageFormat->addItem(QStringLiteral("JPEG (*.jpg)"), QStringLiteral("jpg"));
    mImageFormat->addItem(QStringLiteral("TIFF (*.tif)"), QStringLiteral("tif"));
    mImageFormat->addItem(QStringLiteral("Targa (*.tga)"), QStringLiteral("tga"));
    form->addRow(QStringLiteral("Image format:"), mImageFormat);
    outer->addWidget(settings);

    auto *destination = new QGroupBox(QStringLiteral("Destination"), this);
    auto *destinationLayout = new QVBoxLayout(destination);
    mRelativeOutput = new QRadioButton(
        QStringLiteral("Place each result beside its source file"), destination);
    mAbsoluteOutput = new QRadioButton(
        QStringLiteral("Place all results in this directory:"), destination);
    mRelativeOutput->setChecked(true);
    destinationLayout->addWidget(mRelativeOutput);
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(mAbsoluteOutput);
    mOutputDirectory = new QLineEdit(destination);
    mBrowseOutput = new QPushButton(QStringLiteral("Browse..."), destination);
    pathRow->addWidget(mOutputDirectory, 1);
    pathRow->addWidget(mBrowseOutput);
    destinationLayout->addLayout(pathRow);
    mReplaceExisting = new QCheckBox(
        QStringLiteral("Replace existing destinations only after each new result completes"),
        destination);
    destinationLayout->addWidget(mReplaceExisting);
    outer->addWidget(destination);

    mTable = new QTableWidget(0, 2, this);
    mTable->setHorizontalHeaderLabels(
        {QStringLiteral("Source file"), QStringLiteral("Output name")});
    mTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    mTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mTable->setAlternatingRowColors(true);
    mTable->verticalHeader()->setVisible(false);
    outer->addWidget(mTable, 1);

    auto *actions = new QHBoxLayout;
    mAddFiles = new QPushButton(QStringLiteral("Add files..."), this);
    mRemove = new QPushButton(QStringLiteral("Remove"), this);
    mFilterNames = new QPushButton(QStringLiteral("Filter output names..."), this);
    actions->addWidget(mAddFiles);
    actions->addWidget(mRemove);
    actions->addWidget(mFilterNames);
    actions->addStretch();
    mAddToQueue = new QPushButton(QStringLiteral("Add to queue"), this);
    auto *close = new QPushButton(QStringLiteral("Close"), this);
    actions->addWidget(mAddToQueue);
    actions->addWidget(close);
    outer->addLayout(actions);

    connect(mBrowseOutput, &QPushButton::clicked,
            this, &VDQtBatchWizardDialog::browseOutputDirectory);
    connect(mAddFiles, &QPushButton::clicked,
            this, &VDQtBatchWizardDialog::addFiles);
    connect(mRemove, &QPushButton::clicked,
            this, &VDQtBatchWizardDialog::removeSelected);
    connect(mFilterNames, &QPushButton::clicked,
            this, &VDQtBatchWizardDialog::filterOutputNames);
    connect(mRelativeOutput, &QRadioButton::toggled,
            this, &VDQtBatchWizardDialog::updateOutputMode);
    connect(mAbsoluteOutput, &QRadioButton::toggled,
            this, &VDQtBatchWizardDialog::updateOutputMode);
    connect(mOperation, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VDQtBatchWizardDialog::updateOperation);
    connect(mContainer, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VDQtBatchWizardDialog::updateOperation);
    connect(mImageFormat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VDQtBatchWizardDialog::updateOperation);
    connect(mAddToQueue, &QPushButton::clicked,
            this, &VDQtBatchWizardDialog::acceptJobs);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    updateOutputMode();
    updateOperation();
}

void VDQtBatchWizardDialog::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void VDQtBatchWizardDialog::dropEvent(QDropEvent *event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isFile()) paths.append(path);
    }
    addSourceFiles(paths);
    event->acceptProposedAction();
}

void VDQtBatchWizardDialog::browseOutputDirectory() {
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Output Directory"),
        mOutputDirectory->text());
    if (path.isEmpty()) return;
    mOutputDirectory->setText(path);
    mAbsoluteOutput->setChecked(true);
}

void VDQtBatchWizardDialog::addFiles() {
    addSourceFiles(QFileDialog::getOpenFileNames(
        this, QStringLiteral("Select Files"), QString(),
        QStringLiteral("Video, Media, and Scripts (*.avi *.mp4 *.mkv *.mov *.webm *.flv *.wmv *.nut *.avs *.AVS *.vpy *.VPY);;All Files (*)")));
}

void VDQtBatchWizardDialog::addSourceFiles(const QStringList& paths) {
    QSet<QString> existing;
    for (int row = 0; row < mTable->rowCount(); ++row)
        existing.insert(QFileInfo(mTable->item(row, 0)->text()).absoluteFilePath());
    for (const QString& path : paths) {
        const QFileInfo source(path);
        if (!source.isFile()) continue;
        const QString absolute = source.absoluteFilePath();
        if (existing.contains(absolute)) continue;
        const int row = mTable->rowCount();
        mTable->insertRow(row);
        auto *sourceItem = new QTableWidgetItem(absolute);
        sourceItem->setFlags(sourceItem->flags() & ~Qt::ItemIsEditable);
        mTable->setItem(row, 0, sourceItem);
        QString outputName =
            source.completeBaseName() + QLatin1Char('.') + selectedExtension();
        if (outputName.compare(source.fileName(), Qt::CaseInsensitive) == 0) {
            outputName = source.completeBaseName()
                + QStringLiteral("_output.") + selectedExtension();
        }
        setRowOutputName(row, outputName);
        existing.insert(absolute);
    }
    if (mOutputDirectory->text().isEmpty() && mTable->rowCount() > 0)
        mOutputDirectory->setText(
            QFileInfo(mTable->item(0, 0)->text()).absolutePath());
}

void VDQtBatchWizardDialog::removeSelected() {
    QList<int> rows;
    for (const QModelIndex& index : mTable->selectionModel()->selectedRows())
        rows.append(index.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) mTable->removeRow(row);
}

void VDQtBatchWizardDialog::filterOutputNames() {
    if (mTable->rowCount() == 0) return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Filter Output Names"));
    auto *form = new QFormLayout(&dialog);
    auto *search = new QLineEdit(&dialog);
    auto *replacement = new QLineEdit(&dialog);
    auto *matchCase = new QCheckBox(QStringLiteral("Match case"), &dialog);
    form->addRow(QStringLiteral("Search for:"), search);
    form->addRow(QStringLiteral("Replace with:"), replacement);
    form->addRow(QString(), matchCase);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted || search->text().isEmpty()) return;
    const Qt::CaseSensitivity sensitivity = matchCase->isChecked()
        ? Qt::CaseSensitive : Qt::CaseInsensitive;
    for (int row = 0; row < mTable->rowCount(); ++row) {
        QTableWidgetItem *item = mTable->item(row, 1);
        item->setText(replaceText(item->text(), search->text(),
                                  replacement->text(), sensitivity));
    }
}

void VDQtBatchWizardDialog::updateOutputMode() {
    const bool absolute = mAbsoluteOutput->isChecked();
    mOutputDirectory->setEnabled(absolute);
    mBrowseOutput->setEnabled(absolute);
}

void VDQtBatchWizardDialog::updateOperation() {
    const VDQtJobOperation operation = selectedOperation();
    mContainer->setEnabled(operation == VDQtJobOperation::VideoExport);
    mImageFormat->setEnabled(operation == VDQtJobOperation::ImageSequenceExport);
    const QString extension = selectedExtension();
    for (int row = 0; row < mTable->rowCount(); ++row) {
        const QString source = mTable->item(row, 0)->text();
        QString base = QFileInfo(mTable->item(row, 1)->text()).completeBaseName();
        if (base.isEmpty()) base = QFileInfo(source).completeBaseName();
        QString outputName = operation == VDQtJobOperation::VideoAnalysis
            ? QStringLiteral("(none)")
            : base + QLatin1Char('.') + extension;
        if (outputName.compare(QFileInfo(source).fileName(),
                               Qt::CaseInsensitive) == 0) {
            outputName = QFileInfo(source).completeBaseName()
                + QStringLiteral("_output.") + extension;
        }
        setRowOutputName(row, outputName);
    }
}

QString VDQtBatchWizardDialog::selectedExtension() const {
    switch (selectedOperation()) {
    case VDQtJobOperation::VideoExport: {
        const QString container = selectedContainer();
        return container.startsWith(QStringLiteral("mp4")) ? QStringLiteral("mp4")
            : container.startsWith(QStringLiteral("mov")) ? QStringLiteral("mov")
            : container;
    }
    case VDQtJobOperation::AudioExport:
        return audioExtension(mTemplate.processing.audioCodec);
    case VDQtJobOperation::RawVideoExport:
        return QStringLiteral("raw");
    case VDQtJobOperation::ImageSequenceExport:
        return mImageFormat->currentData().toString();
    case VDQtJobOperation::VideoAnalysis:
        return QString();
    }
    return QStringLiteral("out");
}

QString VDQtBatchWizardDialog::selectedContainer() const {
    return mContainer->currentData().toString();
}

VDQtJobOperation VDQtBatchWizardDialog::selectedOperation() const {
    return static_cast<VDQtJobOperation>(mOperation->currentData().toInt());
}

QString VDQtBatchWizardDialog::outputPathForRow(int row) const {
    if (selectedOperation() == VDQtJobOperation::VideoAnalysis) return {};
    const QTableWidgetItem *sourceItem = mTable->item(row, 0);
    const QTableWidgetItem *outputItem = mTable->item(row, 1);
    if (!sourceItem || !outputItem) return {};
    const QString directory = mRelativeOutput->isChecked()
        ? QFileInfo(sourceItem->text()).absolutePath()
        : QDir(mOutputDirectory->text()).absolutePath();
    return QDir(directory).filePath(QFileInfo(outputItem->text()).fileName());
}

void VDQtBatchWizardDialog::setRowOutputName(int row, const QString& name) {
    QTableWidgetItem *item = mTable->item(row, 1);
    if (!item) {
        item = new QTableWidgetItem;
        mTable->setItem(row, 1, item);
    }
    item->setText(name);
    if (selectedOperation() == VDQtJobOperation::VideoAnalysis)
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    else
        item->setFlags(item->flags() | Qt::ItemIsEditable);
}

QList<VDQtJobState> VDQtBatchWizardDialog::buildJobs(
    QString *errorMessage) const {
    QList<VDQtJobState> result;
    if (mTable->rowCount() == 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Add at least one source file.");
        return result;
    }
    if (mAbsoluteOutput->isChecked()
        && (mOutputDirectory->text().trimmed().isEmpty()
            || !QFileInfo(mOutputDirectory->text()).isDir())) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Choose an existing output directory.");
        return result;
    }
    for (int row = 0; row < mTable->rowCount(); ++row) {
        const QString source = mTable->item(row, 0)->text();
        VDQtJobState job = mTemplate;
        job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        job.operation = selectedOperation();
        job.status = VDQtJobStatus::Pending;
        job.startedAtUtc = {};
        job.endedAtUtc = {};
        job.progress = 0.0;
        job.error.clear();
        job.logEntries.clear();
        job.replaceExisting = mReplaceExisting->isChecked();
        job.sourcePaths = {QFileInfo(source).absoluteFilePath()};
        job.imageSequenceFps = 0.0;
        job.rawPixelFormat.clear();
        job.audioSourcePath.clear();
        job.audioStreamIndex = -1;
        // The selected source stream cannot be reused across unrelated files,
        // but Audio > No audio is a processing choice and applies to the batch.
        job.audioDisabled = mTemplate.audioDisabled;
        job.options.inputPath = job.sourcePaths.first();
        job.options.outputPath = outputPathForRow(row);
        job.options.protectedSourcePaths = job.sourcePaths;
        job.options.startFrame = 0;
        job.options.endFrame = -1;
        job.options.timelineSegments.clear();
        job.options.containerType = selectedContainer();
        job.options.fastStart = selectedContainer().contains(QStringLiteral("faststart"));
        job.options.includeAudio =
            job.operation == VDQtJobOperation::VideoExport && !job.audioDisabled;
        job.name = QStringLiteral("%1 - %2")
            .arg(QFileInfo(source).completeBaseName(),
                 VDQtJobQueue::operationText(job.operation));
        job.imageExtension = mImageFormat->currentData().toString();
        if (job.operation == VDQtJobOperation::AudioExport)
            job.options.audioMode = AudioMode_FullProcessing;
        result.append(job);
    }
    QList<VDQtJobState> allJobs = mExistingJobs;
    allJobs.append(result);
    if (!VDQtJobQueue::validateJobs(allJobs, errorMessage)) {
        result.clear();
        return result;
    }
    QStringList existingDestinations;
    for (const VDQtJobState& job : result) {
        if (!job.options.outputPath.isEmpty()
            && (QFileInfo(job.options.outputPath).exists()
                || QFileInfo(job.options.outputPath).isSymLink()))
            existingDestinations.append(job.options.outputPath);
    }
    if (!existingDestinations.isEmpty() && !mReplaceExisting->isChecked()) {
        if (errorMessage)
            *errorMessage = QString(
                "%1 destination(s) already exist. Enable the guarded replacement "
                "option or rename the outputs.\n\n%2")
                .arg(existingDestinations.size())
                .arg(existingDestinations.mid(0, 8).join(QLatin1Char('\n')));
        result.clear();
    }
    return result;
}

void VDQtBatchWizardDialog::acceptJobs() {
    QString error;
    QList<VDQtJobState> result = buildJobs(&error);
    if (result.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Batch Wizard"),
                             error.isEmpty()
                                 ? QStringLiteral("No jobs were generated.") : error);
        return;
    }
    mJobs = result;
    accept();
}
