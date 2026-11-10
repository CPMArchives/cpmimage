// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>
#include "filename_policy.h"
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

class Window : public QMainWindow {
    Q_OBJECT
public:
    Window(QString root, QString python, QString helper, int deadlineMs = 30000, int graceMs = 2000, QString noticesPath = {}, QString settingsFile = {});
    ~Window() override;
    void openImage(const QString &path, const QString &format, const QString &container);
    void exportFile(const QString &destination);
    int entryCount() const;
    bool readerRunning() const { return process.state() != QProcess::NotRunning; }
    void selectEntry(int row);
    QString lastError() const { return error; }
signals:
    void completed(bool success);
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
    void chooseExport();
    void editFilenameSettings();
    QString selectedExportName(QString *problem) const;
    QString root, python, helper, image, format, container, session, requestId, operation, error;
    QByteArray output;
    QProcess process;
    QTimer deadline, cancellationDeadline;
    QByteArray pending;
    int deadlineMs, graceMs;
    void *readerJob = nullptr;
    bool busy = false, closing = false;
    QComboBox *formats;
    QLabel *source, *notice, *status;
    QPushButton *openButton, *exportButton, *cancelButton;
    QTableWidget *table;
    QListWidget *userAreas;
    QJsonArray directoryEntries;
    QString imageNote;
    FilenamePolicy filenamePolicy;
    QSettings *settings;
};
