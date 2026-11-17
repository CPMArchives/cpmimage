// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <functional>
#include <memory>
#include "filename_policy.h"
#include "export_plan.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QTimer>
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QListWidget;
class QSettings;
class QProgressBar;
class QAction;
class TransferFiles;

class QTemporaryDir;
class Window : public QMainWindow {
    Q_OBJECT
public:
    Window(QString root, QString python, QString helper, int deadlineMs = 30000, int graceMs = 2000, QString noticesPath = {}, QString settingsFile = {}, QString definitionStore = {});
    ~Window() override;
    void openFromPicker();
    void createImage();
    void importFiles(const QStringList &paths);
    void openImage(const QString &path, const QString &format, const QString &container);
    void setCataloguePath(const QString &path) { cataloguePath = path; }
    void loadCatalogue(const QString &path = {});
    void inspectImage(const QString &path, const QString &explicitFormat = {}, const QString &type = {});
    void exportFile(const QString &destination);
    void exportSelection(const QString &directory, bool skipExisting = false);
    void exportUserArea(const QString &directory, bool skipExisting = false);
    QString lastBatchReport() const { return batchReport; }
    bool closeSession(std::function<void()> retry);
    int entryCount() const;
    bool readerRunning() const { return process.state() != QProcess::NotRunning; }
    void selectEntry(int row);
    QString lastError() const { return error; }
signals:
    void completed(bool success);
    void sessionClosed();
    void comparisonAvailabilityChanged();
    void newImageRequested();
    void imageOpened(const QString &path);
    void imagePicked(const QString &path);
    void exportProgress(int completed, int total);
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    friend class Workspace;
    void begin(QJsonObject request);
    void requestBrokerResult(QJsonObject request, std::function<void(QJsonObject, QString)> reply);
    bool showDiskFormatEditor(const QString &selectedFormat = {}, const QString &draft = {});
    void acceptCatalogue(const QJsonObject &result);
    void readReply();
    void fail(const QString &message, const QJsonObject &details = {});
    void setBusy(bool value);
    void cancel();
    void resetImage();
    void stopReader();
    bool containReader();
    void sendPending();
    void showUserArea();
    void updateSelectionStatus();
    void updateDiskSpace(const QJsonObject &result);
    void updateUserAreas();
    void chooseImage();
    void chooseSave(bool saveAs);
    void chooseEdit(const QString &action, QStringList paths = {}, const TransferFiles *transfer = nullptr);
    void offerFiles(bool drag, bool cut = false);
    quint64 imageRevision = 0;
    void exportFlat(const QJsonArray &entries, const QString &directory, const QSet<QString> &textFiles = {});
    bool confirmWrite();
    bool dirty = false, writeConfirmed = false;
    bool resolveEdits(std::function<void()> next);
    std::function<void()> afterSave;
    QJsonObject pendingSave;
    QAction *showAllAction = nullptr;
    int previousUserArea = 0;
    QJsonObject initializeOptions(const QJsonObject &geometry);
    bool newImagePicker = false;
    bool unpublished = false;
    QJsonObject newDefinition, imageGeometry;
    void viewFile(QString mode = "auto");
    void inspectPreview(const QByteArray &bytes, const QString &name, const QString &title,
                        QWidget *pane, bool truncated, QString mode = "auto", int member = -1,
                        QPointer<QWidget> directoryView = {},
                        std::function<void(bool, QByteArray, QByteArray)> extracted = {});
    void showPreview(QByteArray bytes, QByteArray decoded, const QJsonObject &result,
                     const QString &title, QWidget *pane, bool truncated, QPointer<QWidget> returnTo = {});
    void showArchive(QByteArray bytes, QByteArray decoded, const QJsonObject &result,
                     const QString &title, QWidget *pane, QPointer<QWidget> returnTo = {});
    void finishPreviewDialog(QDialog *dialog, QWidget *pane, QPointer<QWidget> returnTo);
    QString previewMode;
    void showFilePreview(bool truncated);
    QAction *viewFileAction = nullptr;
    QString previewName;
    QWidget *previewPane = nullptr;
    std::unique_ptr<QTemporaryDir> previewDirectory;
    void chooseExport();
    void chooseContextExport();
    void updateExportAction();
    QWidget *exportPane = nullptr;
    QAction *exportAction = nullptr;
    void editFilenameSettings();
    void chooseBatch(bool wholeArea);
    QJsonArray exportEntries(bool wholeArea) const;
    void startBatch(ExportPlan plan, bool skipExisting);
    void nextBatchFile();
    void finishBatch();
    void showExportReport();
    void showImageInformation();
    void showPhysicalDiskInspector(int block = -1, QString revision = {});
    void showFilesystemInspector(int slot = -1, QString revision = {}, int allocationBlock = -1);
    void showFilesystemCheck();
    void chooseFormat(const QJsonObject &inspection, bool switching);
    void openDefinition(const QJsonObject &definition, bool sidecar, bool switching);
    QString selectedExportName(QString *problem) const;
    QString root, python, helper, image, format, container, session, requestId, operation, error;
    QByteArray output;
    QProcess process;
    QTimer deadline, cancellationDeadline;
    QByteArray pending;
    int deadlineMs, graceMs;
    void *readerJob = nullptr;
    bool busy = false;
    std::function<void()> closeRetry, closing;
    QPushButton *formats;
    QAction *externalCatalogueAction, *bundledCatalogueAction, *diskFormatEditorAction;
    QString definitionStore;
    std::function<void(QJsonObject, QString)> brokerResultHandler;
    QJsonArray catalogueRows;
    QString cataloguePath, requestedImage, requestedFormat, requestedContainer;
    QString inspectedHash, inspectedContainer, activeCatalogue;
    bool retainSource = false;
    QLabel *source, *status, *diskSpace;
    QPushButton *openButton, *exportButton, *cancelButton;
    QTableWidget *table;
    QListWidget *userAreas;
    QJsonArray directoryEntries;
    QString imageNote, imdComment;
    FilenamePolicy filenamePolicy;
    QSettings *settings;
    QPushButton *reportButton, *imageInfoButton;
    QAction *diskAnalysisAction;
    void bindWorkbenchAction(const char *icon, QAction *action);
    void showDiskAnalysis();
    QAction *openAction, *imageInfoAction, *physicalDiskAction, *filesystemInspectorAction, *filesystemCheckAction;
    QString snapshotHash, exportDiagnostics;
    QProgressBar *progress;
    ExportPlan batch;
    int batchIndex = 0;
    bool batchActive = false, batchInFlight = false, batchCancelRequested = false;
    QString batchReport;
};
