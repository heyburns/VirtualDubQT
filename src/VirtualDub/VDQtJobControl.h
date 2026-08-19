#ifndef VDQTJOBCONTROL_H
#define VDQTJOBCONTROL_H

#include "VDQtJobQueue.h"

#include <QAbstractTableModel>
#include <QDialog>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableView;

class VDQtJobTableModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit VDQtJobTableModel(VDQtJobQueue *queue,
                               QObject *parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

private:
    VDQtJobQueue *mQueue = nullptr;
};

class VDQtJobControlWindow final : public QDialog {
    Q_OBJECT
public:
    explicit VDQtJobControlWindow(VDQtJobQueue *queue,
                                  QWidget *parent = nullptr);
    ~VDQtJobControlWindow() override;

    void showAndRaise();

private Q_SLOTS:
    void updateSelectionControls();
    void updateRunState(bool running, int currentIndex);
    void updateCurrentProgress(int row);
    void showJobDetails(const QModelIndex& index);
    void openQueue();
    void appendQueue();
    void saveQueueAs();

private:
    int selectedRow() const;
    QList<int> selectedRows() const;
    void restoreUiState();
    void saveUiState() const;

    VDQtJobQueue *mQueue = nullptr;
    VDQtJobTableModel *mModel = nullptr;
    QTableView *mTable = nullptr;
    QPushButton *mCloseButton = nullptr;
    QPushButton *mMoveUpButton = nullptr;
    QPushButton *mMoveDownButton = nullptr;
    QPushButton *mPostponeButton = nullptr;
    QPushButton *mDeleteButton = nullptr;
    QPushButton *mStartButton = nullptr;
    QPushButton *mAbortButton = nullptr;
    QPushButton *mReloadButton = nullptr;
    QCheckBox *mAutoStart = nullptr;
    QLabel *mCurrentJob = nullptr;
    QProgressBar *mProgress = nullptr;
    QLabel *mPercent = nullptr;
};

#endif
