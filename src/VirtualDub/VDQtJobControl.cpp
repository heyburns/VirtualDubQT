#include "VDQtJobControl.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QDate>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString displayTime(const QDateTime& value) {
    if (!value.isValid()) return QStringLiteral("-");
    const QDateTime local = value.toLocalTime();
    return local.date() == QDate::currentDate()
        ? local.toString(QStringLiteral("h:mm ap"))
        : local.toString(QStringLiteral("ddd d h:mm ap"));
}

QString sourceDisplay(const VDQtJobState& job) {
    const QString first = job.sourcePaths.value(0);
    const QString name = QFileInfo(first).fileName();
    return job.sourcePaths.size() > 1
        ? QStringLiteral("%1 (+%2)").arg(name).arg(job.sourcePaths.size() - 1)
        : name;
}

} // namespace

VDQtJobTableModel::VDQtJobTableModel(VDQtJobQueue *queue, QObject *parent)
    : QAbstractTableModel(parent), mQueue(queue) {
    connect(queue, &VDQtJobQueue::queueAboutToReset,
            this, &VDQtJobTableModel::beginResetModel);
    connect(queue, &VDQtJobQueue::queueReset,
            this, &VDQtJobTableModel::endResetModel);
    connect(queue, &VDQtJobQueue::jobChanged, this, [this](int row) {
        if (row >= 0 && row < rowCount())
            Q_EMIT dataChanged(index(row, 0), index(row, columnCount() - 1));
    });
}

int VDQtJobTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || !mQueue ? 0 : mQueue->count();
}

int VDQtJobTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 6;
}

QVariant VDQtJobTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !mQueue) return {};
    const VDQtJobState *job = mQueue->jobAt(index.row());
    if (!job) return {};
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case 0: return job->name;
        case 1: return sourceDisplay(*job);
        case 2: return job->operation == VDQtJobOperation::VideoAnalysis
                    ? QStringLiteral("(analysis only)")
                    : QFileInfo(job->options.outputPath).fileName();
        case 3: return displayTime(job->startedAtUtc);
        case 4: return displayTime(job->endedAtUtc);
        case 5: {
            QString text = VDQtJobQueue::statusText(job->status);
            if (job->status == VDQtJobStatus::Running)
                text += QStringLiteral(" (%1%)")
                    .arg(qRound(job->progress * 100.0));
            return text;
        }
        default: return {};
        }
    }
    if (role == Qt::ToolTipRole) {
        QString text = QStringLiteral("Operation: %1\nSource: %2")
            .arg(VDQtJobQueue::operationText(job->operation),
                 job->sourcePaths.join(QStringLiteral("\n        ")));
        if (!job->options.outputPath.isEmpty())
            text += QStringLiteral("\nDestination: %1").arg(job->options.outputPath);
        if (!job->error.isEmpty())
            text += QStringLiteral("\n\n%1").arg(job->error);
        return text;
    }
    if (role == Qt::ForegroundRole) {
        if (job->status == VDQtJobStatus::Failed
            || job->status == VDQtJobStatus::Interrupted)
            return QColor(255, 110, 110);
        if (job->status == VDQtJobStatus::Complete)
            return QColor(120, 220, 150);
        if (job->status == VDQtJobStatus::Postponed)
            return QColor(180, 180, 180);
    }
    return {};
}

QVariant VDQtJobTableModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const QStringList headers = {
        QStringLiteral("Name"), QStringLiteral("Source"),
        QStringLiteral("Dest"), QStringLiteral("Start"),
        QStringLiteral("End"), QStringLiteral("Status")
    };
    return headers.value(section);
}

Qt::ItemFlags VDQtJobTableModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags value = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() == 0 && mQueue && !mQueue->isRunning())
        value |= Qt::ItemIsEditable;
    return value;
}

bool VDQtJobTableModel::setData(const QModelIndex& index,
                                const QVariant& value, int role) {
    if (role != Qt::EditRole || !index.isValid() || index.column() != 0
        || !mQueue || !mQueue->setJobName(index.row(), value.toString()))
        return false;
    Q_EMIT dataChanged(index, index);
    return true;
}

VDQtJobControlWindow::VDQtJobControlWindow(VDQtJobQueue *queue,
                                           QWidget *parent)
    : QDialog(parent), mQueue(queue) {
    setWindowTitle(QStringLiteral("Job Control"));
    setWindowFlags(windowFlags() | Qt::Window);
    setModal(false);
    resize(980, 520);

    auto *outer = new QVBoxLayout(this);
    auto *menuBar = new QMenuBar(this);
    QMenu *fileMenu = menuBar->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&Open job list..."), this,
                        &VDQtJobControlWindow::openQueue);
    fileMenu->addAction(QStringLiteral("&Append job list..."), this,
                        &VDQtJobControlWindow::appendQueue);
    fileMenu->addAction(QStringLiteral("&Save job list as..."), this,
                        &VDQtJobControlWindow::saveQueueAs);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Batch wizard..."), mQueue,
                        &VDQtJobQueue::batchWizardRequested);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Close"), this, &QWidget::hide);

    QMenu *editMenu = menuBar->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(QStringLiteral("Clear job list"), this, [this]() {
        if (mQueue->isEmpty()) return;
        if (QMessageBox::question(this, QStringLiteral("Clear Job List"),
                                  QStringLiteral("Remove every job from the list?"))
            == QMessageBox::Yes)
            mQueue->clearAll();
    });
    editMenu->addAction(QStringLiteral("Delete completed jobs"),
                        mQueue, &VDQtJobQueue::clearCompleted);
    editMenu->addAction(QStringLiteral("Failed/aborted to waiting"),
                        mQueue, &VDQtJobQueue::retryFailed);
    editMenu->addAction(QStringLiteral("Waiting to postponed"), this,
                        [this]() { mQueue->setAllPendingPostponed(true); });
    editMenu->addAction(QStringLiteral("Postponed to waiting"), this,
                        [this]() { mQueue->setAllPendingPostponed(false); });
    outer->setMenuBar(menuBar);

    auto *center = new QHBoxLayout;
    mModel = new VDQtJobTableModel(queue, this);
    mTable = new QTableView(this);
    mTable->setModel(mModel);
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mTable->setAlternatingRowColors(true);
    mTable->setSortingEnabled(false);
    mTable->verticalHeader()->setVisible(false);
    mTable->horizontalHeader()->setStretchLastSection(true);
    center->addWidget(mTable, 1);

    auto *controls = new QVBoxLayout;
    mCloseButton = new QPushButton(QStringLiteral("Close"), this);
    mMoveUpButton = new QPushButton(QStringLiteral("Move up"), this);
    mMoveDownButton = new QPushButton(QStringLiteral("Move down"), this);
    mPostponeButton = new QPushButton(QStringLiteral("Postpone"), this);
    mDeleteButton = new QPushButton(QStringLiteral("Delete"), this);
    mStartButton = new QPushButton(QStringLiteral("Start"), this);
    mAbortButton = new QPushButton(QStringLiteral("Abort"), this);
    mReloadButton = new QPushButton(QStringLiteral("Reload"), this);
    mAutoStart = new QCheckBox(QStringLiteral("Autostart"), this);
    controls->addWidget(mCloseButton);
    controls->addSpacing(8);
    controls->addWidget(mMoveUpButton);
    controls->addWidget(mMoveDownButton);
    controls->addWidget(mPostponeButton);
    controls->addWidget(mDeleteButton);
    controls->addSpacing(8);
    controls->addWidget(mStartButton);
    controls->addWidget(mAbortButton);
    controls->addWidget(mReloadButton);
    controls->addWidget(mAutoStart);
    controls->addStretch();
    center->addLayout(controls);
    outer->addLayout(center, 1);

    auto *progressRow = new QHBoxLayout;
    mCurrentJob = new QLabel(QStringLiteral("Current job: none"), this);
    mProgress = new QProgressBar(this);
    mProgress->setRange(0, 1000);
    mProgress->setValue(0);
    mPercent = new QLabel(QStringLiteral("0%"), this);
    progressRow->addWidget(mCurrentJob);
    progressRow->addWidget(mProgress, 1);
    progressRow->addWidget(mPercent);
    outer->addLayout(progressRow);

    connect(mCloseButton, &QPushButton::clicked, this, &QWidget::hide);
    connect(mMoveUpButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedRow();
        if (mQueue->moveJob(row, row - 1))
            mTable->selectRow(row - 1);
    });
    connect(mMoveDownButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedRow();
        if (mQueue->moveJob(row, row + 1))
            mTable->selectRow(row + 1);
    });
    connect(mPostponeButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedRow();
        const VDQtJobState *job = mQueue->jobAt(row);
        if (!job) return;
        mQueue->setJobStatus(
            row, job->status == VDQtJobStatus::Postponed
                ? VDQtJobStatus::Pending : VDQtJobStatus::Postponed);
    });
    connect(mDeleteButton, &QPushButton::clicked, this,
            [this]() { mQueue->removeRows(selectedRows()); });
    connect(mStartButton, &QPushButton::clicked, this, [this]() {
        if (mQueue->isRunning()) Q_EMIT mQueue->stopRequested();
        else Q_EMIT mQueue->runRequested();
    });
    connect(mAbortButton, &QPushButton::clicked,
            mQueue, &VDQtJobQueue::abortRequested);
    connect(mReloadButton, &QPushButton::clicked, this, [this]() {
        const int row = selectedRow();
        if (row >= 0) Q_EMIT mQueue->reloadRequested(row);
    });
    connect(mAutoStart, &QCheckBox::toggled,
            mQueue, &VDQtJobQueue::setAutoRunEnabled);
    connect(mTable, &QTableView::doubleClicked,
            this, &VDQtJobControlWindow::showJobDetails);
    connect(mTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &VDQtJobControlWindow::updateSelectionControls);
    connect(queue, &VDQtJobQueue::queueReset,
            this, &VDQtJobControlWindow::updateSelectionControls);
    connect(queue, &VDQtJobQueue::jobChanged,
            this, &VDQtJobControlWindow::updateCurrentProgress);
    connect(queue, &VDQtJobQueue::runningChanged,
            this, &VDQtJobControlWindow::updateRunState);

    restoreUiState();
    updateRunState(queue->isRunning(), queue->currentIndex());
    updateSelectionControls();
}

VDQtJobControlWindow::~VDQtJobControlWindow() {
    saveUiState();
}

void VDQtJobControlWindow::showAndRaise() {
    show();
    raise();
    activateWindow();
}

int VDQtJobControlWindow::selectedRow() const {
    const QModelIndexList rows = mTable->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

QList<int> VDQtJobControlWindow::selectedRows() const {
    QList<int> result;
    for (const QModelIndex& index : mTable->selectionModel()->selectedRows())
        result.append(index.row());
    return result;
}

void VDQtJobControlWindow::updateSelectionControls() {
    const int row = selectedRow();
    const VDQtJobState *job = mQueue->jobAt(row);
    const bool idle = !mQueue->isRunning();
    const bool selected = job != nullptr;
    mMoveUpButton->setEnabled(selected && idle && row > 0);
    mMoveDownButton->setEnabled(selected && idle && row + 1 < mQueue->count());
    mDeleteButton->setEnabled(selected && idle);
    mPostponeButton->setEnabled(selected && idle);
    mReloadButton->setEnabled(selected && idle);
    if (job) {
        mPostponeButton->setText(job->status == VDQtJobStatus::Postponed
            ? QStringLiteral("Resume") : QStringLiteral("Postpone"));
    }
}

void VDQtJobControlWindow::updateRunState(bool running, int currentIndex) {
    mStartButton->setText(running ? QStringLiteral("Stop")
                                  : QStringLiteral("Start"));
    mStartButton->setEnabled(running || mQueue->pendingCount() > 0);
    mAbortButton->setEnabled(running);
    mAutoStart->setEnabled(!running);
    mAutoStart->setChecked(mQueue->autoRunEnabled());
    updateSelectionControls();
    if (!running) {
        mCurrentJob->setText(QStringLiteral("Current job: none"));
        mProgress->setValue(0);
        mPercent->setText(QStringLiteral("0%"));
        setWindowTitle(QStringLiteral("Job Control"));
    } else {
        updateCurrentProgress(currentIndex);
    }
}

void VDQtJobControlWindow::updateCurrentProgress(int row) {
    if (!mQueue->isRunning() || row != mQueue->currentIndex()) return;
    const VDQtJobState *job = mQueue->jobAt(row);
    if (!job) return;
    const int value = qRound(job->progress * 1000.0);
    mCurrentJob->setText(QStringLiteral("Current job: %1").arg(job->name));
    mProgress->setValue(value);
    mPercent->setText(QStringLiteral("%1%").arg(qRound(job->progress * 100.0)));
    const bool currentStillActive = job->status == VDQtJobStatus::Starting
                                 || job->status == VDQtJobStatus::Running
                                 || job->status == VDQtJobStatus::Aborting;
    setWindowTitle(QStringLiteral("Job Control (%1 remaining)")
                       .arg(mQueue->pendingCount()
                            + (currentStillActive ? 1 : 0)));
}

void VDQtJobControlWindow::showJobDetails(const QModelIndex& index) {
    const VDQtJobState *job = mQueue->jobAt(index.row());
    if (!job) return;
    QString details;
    if (!job->error.isEmpty()) details += job->error;
    if (!job->logEntries.isEmpty()) {
        if (!details.isEmpty()) details += QStringLiteral("\n\n");
        details += job->logEntries.join(QLatin1Char('\n'));
    }
    if (details.isEmpty()) {
        details = QStringLiteral("No warnings or diagnostic messages were recorded.");
    }
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Job: %1").arg(job->name));
    box.setIcon(job->status == VDQtJobStatus::Failed
                    || job->status == VDQtJobStatus::Interrupted
                ? QMessageBox::Critical : QMessageBox::Information);
    box.setText(details);
    if (job->status == VDQtJobStatus::Failed
        || job->status == VDQtJobStatus::Cancelled
        || job->status == VDQtJobStatus::Interrupted
        || job->status == VDQtJobStatus::Complete) {
        QPushButton *retry = box.addButton(
            QStringLiteral("Return to waiting"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() == retry)
            mQueue->setJobStatus(index.row(), VDQtJobStatus::Pending);
    } else {
        box.exec();
    }
}

void VDQtJobControlWindow::openQueue() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Job List"), QString(),
        QStringLiteral("VirtualDubQT Job Lists (*.vdqjobs);;All Files (*)"));
    if (path.isEmpty()) return;
    if (!mQueue->isEmpty()
        && QMessageBox::question(
               this, QStringLiteral("Replace Job List"),
               QStringLiteral("Replace the current in-memory job list?"))
            != QMessageBox::Yes)
        return;
    QString error;
    if (!mQueue->replaceFromFile(path, &error))
        QMessageBox::critical(this, QStringLiteral("Open Job List Error"), error);
}

void VDQtJobControlWindow::appendQueue() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Append Job List"), QString(),
        QStringLiteral("VirtualDubQT Job Lists (*.vdqjobs);;All Files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!mQueue->appendFromFile(path, &error))
        QMessageBox::critical(this, QStringLiteral("Append Job List Error"), error);
}

void VDQtJobControlWindow::saveQueueAs() {
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Job List"), QStringLiteral("VirtualDub.vdqjobs"),
        QStringLiteral("VirtualDubQT Job Lists (*.vdqjobs);;All Files (*)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".vdqjobs");
    QString error;
    if (!mQueue->saveToFile(path, &error))
        QMessageBox::critical(this, QStringLiteral("Save Job List Error"), error);
}

void VDQtJobControlWindow::restoreUiState() {
    QSettings settings(QStringLiteral("VirtualDub"),
                       QStringLiteral("VirtualDub_Port"));
    restoreGeometry(settings.value(QStringLiteral("jobControl/geometry")).toByteArray());
    const QVariantList widths =
        settings.value(QStringLiteral("jobControl/columnWidths")).toList();
    if (widths.size() == mModel->columnCount()) {
        for (int i = 0; i < widths.size(); ++i)
            mTable->setColumnWidth(i, widths.at(i).toInt());
    } else {
        mTable->setColumnWidth(0, 150);
        mTable->setColumnWidth(1, 190);
        mTable->setColumnWidth(2, 190);
        mTable->setColumnWidth(3, 85);
        mTable->setColumnWidth(4, 85);
        mTable->setColumnWidth(5, 140);
    }
}

void VDQtJobControlWindow::saveUiState() const {
    QSettings settings(QStringLiteral("VirtualDub"),
                       QStringLiteral("VirtualDub_Port"));
    settings.setValue(QStringLiteral("jobControl/geometry"), saveGeometry());
    QVariantList widths;
    for (int i = 0; i < mModel->columnCount(); ++i)
        widths.append(mTable->columnWidth(i));
    settings.setValue(QStringLiteral("jobControl/columnWidths"), widths);
}
