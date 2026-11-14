// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>
#include <functional>
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

class Window : public QMainWindow {
    Q_OBJECT
public:
    Window(QString root, QString python, QString helper, int deadlineMs = 30000, int graceMs = 2000, QString noticesPath = {}, QString settingsFile = {});
    ~Window() override;
    void openImage(const QString &path, const QString &format, const QString &container);
    void loadCatalogue(const QString &path = {});
    void inspectImage(const QString &path, const QString &explicitFormat = {}, const QString &type = {});
    void exportFile(const QString &destination);
    void exportSelection(const QString &directory, bool skipExisting = false);
    void exportUserArea(const QString &directory, bool skipExisting = false);
    QString lastBatchReport() const { return batchReport; }
    int entryCount() const;
    bool readerRunning() const { return process.state() != QProcess::NotRunning; }
    void selectEntry(int row);
    QString lastError() const { return error; }
signals:
    void completed(bool success);
    void exportProgress(int completed, int total);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void begin(QJsonObject request);
    void readReply();
    void fail(const QString &message);
    void setBusy(bool value);
    void cancel();
    void resetImage();
    void stopReader();
    bool containReader();
    void sendPending();
    void showUserArea();
    void updateSelectionStatus();
    void updateUserAreas();
    void chooseImage();
    void chooseSave(bool saveAs);
    void chooseEdit(const QString &action);
    bool confirmWrite();
    bool dirty = false, writeConfirmed = false;
    bool resolveEdits(std::function<void()> next);
    std::function<void()> afterSave;
    QJsonObject pendingSave;
    void chooseExport();
    void editFilenameSettings();
    void chooseBatch(bool wholeArea);
    QJsonArray exportEntries(bool wholeArea) const;
    void startBatch(ExportPlan plan, bool skipExisting);
    void nextBatchFile();
    void finishBatch();
    void showExportReport();
    void showImageInformation();
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
    bool busy = false, closing = false;
    QPushButton *formats;
    QAction *externalCatalogueAction, *bundledCatalogueAction;
    QJsonArray catalogueRows;
    QString cataloguePath, requestedImage, requestedFormat, requestedContainer;
    QString inspectedHash, inspectedContainer, activeCatalogue;
    bool retainSource = false;
    QLabel *source, *status;
    QPushButton *openButton, *exportButton, *cancelButton;
    QTableWidget *table;
    QListWidget *userAreas;
    QJsonArray directoryEntries;
    QString imageNote, imdComment;
    FilenamePolicy filenamePolicy;
    QSettings *settings;
    QPushButton *areaExportButton, *reportButton, *imageInfoButton;
    QAction *imageInfoAction;
    QString snapshotHash, exportDiagnostics;
    QProgressBar *progress;
    ExportPlan batch;
    int batchIndex = 0;
    bool batchActive = false, batchInFlight = false, batchCancelRequested = false;
    QString batchReport;
};
