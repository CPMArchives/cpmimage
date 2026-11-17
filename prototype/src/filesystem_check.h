// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include "image_comparison.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class FilesystemCheckDialog : public QDialog {
public:
    using Reply = std::function<void(QJsonObject, QString)>;
    using Request = std::function<void(QJsonObject, Reply)>;
    using OpenLocation = std::function<void(int, QString)>;

    FilesystemCheckDialog(QString activeImage, QString activeFormat, Request request,
                          OpenLocation openSlot, OpenLocation openBlock, QWidget *parent,
                          QList<ImageComparison::Source> references = {});

private:
    void updateSource();
    void updateControls();
    void clearReport();
    void check();
    void showRepairPlan();
    void showReport(const QJsonObject &report);

    Request request;
    OpenLocation openSlot, openBlock;
    int checkedBlockCount = 0;
    bool busy = false;
    bool catalogueLoaded = false;
    QString checkedRevision, checkedDefinition;
    QList<ImageComparison::Source> references;
    QJsonArray findings;
    int omittedFindings = 0;
    bool repairLayout = false;
    QComboBox *mode, *formats;
    QWidget *external;
    QLineEdit *path;
    QLabel *status, *identity;
    QPushButton *run, *goToSlot, *goToBlock, *reviewPlan;
    QTableWidget *table;
};
