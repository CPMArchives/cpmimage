// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QPlainTextEdit;

class ImageComparison : public QDialog {
public:
    using Reply = std::function<void(QJsonObject, QString)>;
    struct Source {
        QString label;
        std::function<void(QJsonObject, Reply)> request;
        std::function<void()> cancel;
    };

    ImageComparison(Source left, QList<Source> targets, QWidget *parent);

private:
    struct Side {
        QJsonObject manifest;
        QMap<QString, QJsonObject> files;
        QMap<QString, QJsonArray> extents;
        QMap<int, QJsonObject> directory;
        int directorySlots = 0;
        int directoryBlocks = 0;
        int blockSize = 0;
    };

    void beginComparison();
    void send(int side, QJsonObject request, std::function<void(QJsonObject)> accept);
    void collect(int side, bool directory, int offset = 0);
    void fingerprint(int side, int offset);
    void classify(bool directory, int offset = 0);
    void verifyRevision(int side);
    void render();
    void renderFiles();
    void showFileDetails();
    void showSlotDetails();
    void fail(const QString &message);
    const Source &source(int side) const;

    Source left;
    QList<Source> targets;
    Side sides[2];
    int target = 0;
    int pendingSide = -1;
    QStringList contentNames;
    QStringList fileKeys;
    QList<int> slotKeys;
    QJsonArray fileResults, directoryResults;
    QList<QString> visibleFiles;
    QList<int> visibleSlots;

    QComboBox *targetPicker, *areas, *filter;
    QPushButton *compare;
    QLabel *status, *summary;
    QTabWidget *results;
    QTableWidget *fileTable, *slotTable;
    QPlainTextEdit *fileDetails, *slotDetails;
};
