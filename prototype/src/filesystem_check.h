// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class FilesystemCheckDialog : public QDialog {
public:
    using Reply = std::function<void(QJsonObject, QString)>;
    using Request = std::function<void(QJsonObject, Reply)>;
    using OpenSlot = std::function<void(int, QString)>;

    FilesystemCheckDialog(QString activeImage, QString activeFormat, Request request,
                          OpenSlot openSlot, QWidget *parent);

private:
    void updateSource();
    void updateControls();
    void clearReport();
    void check();
    void showReport(const QJsonObject &report);

    Request request;
    OpenSlot openSlot;
    bool busy = false;
    bool catalogueLoaded = false;
    QString checkedRevision;
    QJsonArray findings;
    QComboBox *mode, *formats;
    QWidget *external;
    QLineEdit *path;
    QLabel *status, *identity;
    QPushButton *run, *goToSlot;
    QTableWidget *table;
};
