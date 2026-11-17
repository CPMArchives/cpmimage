// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

class FilesystemInspector : public QDialog {
public:
    using Reply = std::function<void(QJsonObject, QString)>;
    using Request = std::function<void(QJsonObject, Reply)>;
    FilesystemInspector(Request request, const QString &imageName, QWidget *parent,
                        int slot = -1, QString revision = {}, int allocationBlock = -1,
                        std::function<void(int, QString)> openSectors = {});

private:
    enum class View { Directory, Extents, Blocks, References };
    void load(View view, int start = 0, int block = -1, int selected = -1);
    void render(const QJsonObject &result, int selected);
    void updateLinks();
    QJsonObject selectedRow() const;

    Request request;
    View view = View::Directory;
    QString revision;
    int start = 0;
    int next = -1;
    int block = -1;
    int blockCount = 0;
    bool physicalLinks = false;
    QJsonArray rows;
    QWidget *controls;
    QLabel *summary, *page;
    QTableWidget *table;
    QComboBox *pointers;
    QPushButton *previous, *following, *openBlock, *openSlot, *openExtent, *references, *physicalSectors;
};
