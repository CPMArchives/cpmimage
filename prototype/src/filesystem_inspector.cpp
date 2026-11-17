// SPDX-License-Identifier: GPL-3.0-or-later
#include "filesystem_inspector.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace {
constexpr int pageSize = 64;

QString filename(const QJsonObject &row) {
    const auto extension = row["extension"].toString();
    return row["name"].toString() + (extension.isEmpty() ? QString() : "." + extension);
}

QString entryType(const QString &kind) {
    if (kind == "file") return "File extent";
    if (kind == "unused") return "Unused slot";
    if (kind == "label") return "Disk label";
    if (kind == "timestamp") return "Timestamp";
    if (kind == "password") return "Password metadata";
    return "Unknown entry type";
}
}

FilesystemInspector::FilesystemInspector(Request request, const QString &imageName, QWidget *parent,
                                         int slot, QString initialRevision, int allocationBlock,
                                         std::function<void(int, QString)> openSectors)
    : QDialog(parent), request(std::move(request)), revision(std::move(initialRevision)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle("Filesystem Structure Inspector");
    setObjectName("filesystemStructureInspector");
    resize(850, 560);

    auto *layout = new QVBoxLayout(this);
    auto *image = new QLabel(imageName, this);
    image->setTextFormat(Qt::PlainText);
    layout->addWidget(image);
    summary = new QLabel(this);
    summary->setObjectName("filesystemStructureSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    controls = new QWidget(this);
    layout->addWidget(controls, 1);
    auto *body = new QVBoxLayout(controls);
    body->setContentsMargins(0, 0, 0, 0);

    auto *views = new QHBoxLayout;
    auto addButton = [this](QHBoxLayout *row, const QString &text, const QString &name, auto clicked) {
        auto *button = new QPushButton(text, controls);
        button->setObjectName(name);
        button->setAutoDefault(false);
        row->addWidget(button);
        connect(button, &QPushButton::clicked, this, clicked);
        return button;
    };
    addButton(views, "Directory Entries", "filesystemDirectory", [this] { load(View::Directory); });
    addButton(views, "Extents", "filesystemExtents", [this] { load(View::Extents); });
    addButton(views, "Allocation Blocks", "filesystemBlocks", [this] { load(View::Blocks); });
    views->addStretch();
    addButton(views, "Reload", "filesystemReload", [this] {
        revision.clear();
        load(View::Directory);
    });
    body->addLayout(views);

    page = new QLabel(controls);
    page->setObjectName("filesystemStructurePage");
    page->setTextFormat(Qt::PlainText);
    body->addWidget(page);
    table = new QTableWidget(0, 0, controls);
    table->setObjectName("filesystemStructureRows");
    table->setAccessibleName("Filesystem structure rows");
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    body->addWidget(table, 1);

    auto *links = new QHBoxLayout;
    pointers = new QComboBox(controls);
    pointers->setObjectName("filesystemExtentPointers");
    pointers->setAccessibleName("Selected extent's block pointers");
    links->addWidget(pointers, 1);
    openBlock = addButton(links, "Go to Block", "filesystemGoToBlock", [this] {
        const int selected = pointers->currentData().toInt();
        load(View::Blocks, selected / pageSize * pageSize, -1, selected);
    });
    openExtent = addButton(links, "Go to Extent", "filesystemGoToExtent", [this] {
        const auto row = selectedRow();
        const int index = row["extent_index"].toInt();
        load(View::Extents, index / pageSize * pageSize, -1, row["slot"].toInt());
    });
    openSlot = addButton(links, "Go to Directory Slot", "filesystemGoToSlot", [this] {
        const int slot = selectedRow()["slot"].toInt();
        load(View::Directory, slot / pageSize * pageSize, -1, slot);
    });
    references = addButton(links, "Show References", "filesystemShowReferences", [this] {
        load(View::References, 0, selectedRow()["block"].toInt());
    });
    physicalSectors = addButton(links, "Go to Physical Sectors", "filesystemGoToPhysicalSectors",
        [this, openSectors = std::move(openSectors)] {
            if (openSectors) openSectors(selectedRow()["block"].toInt(), revision);
        });
    body->addLayout(links);
    connect(table, &QTableWidget::itemSelectionChanged, this, &FilesystemInspector::updateLinks);
    connect(pointers, &QComboBox::currentIndexChanged, this, [this] {
        openBlock->setEnabled(pointers->currentData().isValid());
    });

    auto *footer = new QHBoxLayout;
    previous = addButton(footer, "Previous Page", "filesystemPrevious", [this] {
        load(view, std::max(0, start - pageSize), block);
    });
    following = addButton(footer, "Next Page", "filesystemNext", [this] { load(view, next, block); });
    footer->addStretch();
    body->addLayout(footer);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateLinks();
    QTimer::singleShot(0, this, [this, slot, allocationBlock] {
        if (!isVisible()) return;
        if (allocationBlock >= 0)
            load(View::Blocks, allocationBlock / pageSize * pageSize, -1, allocationBlock);
        else
            load(View::Directory, std::max(0, slot) / pageSize * pageSize, -1, slot);
    });
}

void FilesystemInspector::load(View target, int offset, int targetBlock, int selected) {
    const QStringList names{"directory", "extents", "blocks", "references"};
    QJsonObject fields{{"view", names.at(static_cast<int>(target))}, {"start", offset}, {"limit", pageSize}};
    if (!revision.isEmpty()) fields["revision"] = revision;
    if (target == View::References) fields["block"] = targetBlock;
    controls->setEnabled(false);
    page->setText("Reading filesystem structures…");
    request(fields, [guard = QPointer<FilesystemInspector>(this), target, offset, targetBlock, selected]
                    (const QJsonObject &result, const QString &error) {
        if (!guard || !guard->isVisible()) return;
        guard->controls->setEnabled(true);
        if (!error.isEmpty() || !result["rows"].isArray() || result["revision"].toString().isEmpty()) {
            guard->page->setText(error.isEmpty() ? "The image reader returned incomplete filesystem data." : error);
            guard->rows = {};
            guard->table->setRowCount(0);
            guard->previous->setEnabled(false);
            guard->following->setEnabled(false);
            guard->updateLinks();
            return;
        }
        guard->view = target;
        guard->start = offset;
        guard->block = targetBlock;
        guard->render(result, selected);
    });
}

void FilesystemInspector::render(const QJsonObject &result, int selected) {
    revision = result["revision"].toString();
    physicalLinks = result["physical_links"].toBool();
    next = result["next_start"].toInt(-1);
    rows = result["rows"].toArray();
    blockCount = result["block_count"].toInt();
    summary->setText(QString("Disk definition %1 · %2 bytes per block · %3 directory blocks · %4 logical extents per physical extent\n%5")
                     .arg(result["format"].toString()).arg(result["block_size"].toInt())
                     .arg(result["directory_blocks"].toInt()).arg(result["extent_factor"].toInt())
                     .arg(result["dirty"].toBool() ? "Working image includes unsaved changes." : "Working image has no unsaved changes."));
    QString title;
    QStringList columns;
    if (view == View::Blocks) {
        title = "Allocation blocks";
        columns = {"Block", "State"};
    } else if (view == View::References) {
        title = QString("References to block %1").arg(block);
        columns = {"Directory slot", "Pointer position"};
    } else {
        title = view == View::Directory ? "Directory entries" : "File extents";
        columns = {"Slot", "Entry type", "User", "File", "Extent", "Physical extent", "Records", "Attributes"};
    }
    const int total = result["total"].toInt();
    page->setText(rows.isEmpty() ? title + " · No entries" :
                  QString("%1 · %2–%3 of %4").arg(title).arg(start + 1).arg(start + rows.size()).arg(total));
    previous->setEnabled(start > 0);
    following->setEnabled(next >= 0);
    table->clear();
    table->setColumnCount(columns.size());
    table->setHorizontalHeaderLabels(columns);
    table->setRowCount(rows.size());
    int selectedRowIndex = 0;
    for (int r = 0; r < rows.size(); ++r) {
        const auto row = rows.at(r).toObject();
        QStringList values;
        if (view == View::Blocks) {
            const int number = row["block"].toInt();
            const auto state = row["state"].toString();
            values = {QString::number(number), state == "directory" ? "Reserved for directory" :
                                               state == "allocated" ? "Allocated" : "Free"};
            if (number == selected) selectedRowIndex = r;
        } else {
            const int slot = row["slot"].toInt();
            values = {QString::number(slot)};
            if (slot == selected) selectedRowIndex = r;
            if (view == View::References) {
                values << QString::number(row["pointer"].toInt() + 1);
            } else {
                const auto kind = row["kind"].toString();
                values << entryType(kind);
                if (kind == "file") {
                    values << QString::number(row["user"].toInt()) << filename(row)
                           << QString::number(row["extent"].toInt()) << QString::number(row["physical_extent"].toInt())
                           << QString::number(row["record_count"].toInt());
                    const int flags = row["attributes"].toInt();
                    QStringList attributes;
                    for (int bit = 0; bit < 4; ++bit)
                        if (flags & (1 << bit)) attributes << QString("F%1").arg(bit + 1);
                    if (flags & 256) attributes << "RO";
                    if (flags & 512) attributes << "SYS";
                    if (flags & 1024) attributes << "ARC";
                    values << attributes.join(' ');
                }
            }
        }
        for (int c = 0; c < values.size(); ++c) table->setItem(r, c, new QTableWidgetItem(values.at(c)));
    }
    if (!rows.isEmpty()) table->selectRow(selectedRowIndex);
    updateLinks();
}

QJsonObject FilesystemInspector::selectedRow() const {
    const int index = table->currentRow();
    return index >= 0 && index < rows.size() ? rows.at(index).toObject() : QJsonObject{};
}

void FilesystemInspector::updateLinks() {
    const auto row = selectedRow();
    const auto blocks = row["pointers"].toArray();
    pointers->clear();
    for (int index = 0; index < blocks.size(); ++index) {
        const auto value = blocks.at(index);
        if (!value.isDouble()) {
            pointers->addItem(QString("Pointer %1 · unused").arg(index + 1));
        } else {
            const int number = value.toInt();
            const bool valid = number > 0 && number < blockCount;
            pointers->addItem(QString("Pointer %1 · block %2%3").arg(index + 1).arg(number)
                              .arg(valid ? QString() : " (out of range)"),
                              valid ? QVariant(number) : QVariant{});
        }
    }
    pointers->setVisible(!blocks.isEmpty());
    openBlock->setVisible(!blocks.isEmpty());
    openBlock->setEnabled(pointers->currentData().isValid());
    openExtent->setVisible(view == View::Directory && row["extent_index"].isDouble());
    openSlot->setVisible(!row.isEmpty() && (view == View::Extents || view == View::References));
    references->setVisible(!row.isEmpty() && view == View::Blocks);
    physicalSectors->setVisible(!row.isEmpty() && view == View::Blocks && physicalLinks);
}
