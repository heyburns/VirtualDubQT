#ifndef VDQTBATCHWIZARD_H
#define VDQTBATCHWIZARD_H

#include "VDQtJobQueue.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;

class VDQtBatchWizardDialog final : public QDialog {
    Q_OBJECT
public:
    explicit VDQtBatchWizardDialog(
        const VDQtJobState& processingTemplate,
        const QList<VDQtJobState>& existingJobs,
        QWidget *parent = nullptr);

    QList<VDQtJobState> jobs() const { return mJobs; }
    void addSourceFiles(const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private Q_SLOTS:
    void browseOutputDirectory();
    void addFiles();
    void removeSelected();
    void filterOutputNames();
    void updateOutputMode();
    void updateOperation();
    void acceptJobs();

private:
    QString selectedExtension() const;
    QString selectedContainer() const;
    VDQtJobOperation selectedOperation() const;
    QString outputPathForRow(int row) const;
    void setRowOutputName(int row, const QString& name);
    QList<VDQtJobState> buildJobs(QString *errorMessage) const;

    VDQtJobState mTemplate;
    QList<VDQtJobState> mExistingJobs;
    QList<VDQtJobState> mJobs;
    QRadioButton *mRelativeOutput = nullptr;
    QRadioButton *mAbsoluteOutput = nullptr;
    QLineEdit *mOutputDirectory = nullptr;
    QPushButton *mBrowseOutput = nullptr;
    QComboBox *mOperation = nullptr;
    QComboBox *mContainer = nullptr;
    QComboBox *mImageFormat = nullptr;
    QCheckBox *mReplaceExisting = nullptr;
    QTableWidget *mTable = nullptr;
    QPushButton *mAddFiles = nullptr;
    QPushButton *mRemove = nullptr;
    QPushButton *mFilterNames = nullptr;
    QPushButton *mAddToQueue = nullptr;
};

#endif
