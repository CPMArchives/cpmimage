// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_comparison.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace {
QString comparisonStatus(const QJsonObject &row) {
    const auto status = row["status"].toString();
    if (status == "added") return "Added";
    if (status == "removed") return "Removed";
    if (status == "unavailable")
        return row["attributes_changed"].toBool() ? "Content unavailable, attributes changed" : "Could not compare";
    if (status == "identical") return "Identical";
    if (row["content_changed"].toBool())
        return row["attributes_changed"].toBool() ? "Content and attributes changed" : "Content changed";
    return "Attributes changed";
}

QString attributes(const QJsonObject &file) {
    const int flags = file["attr"].toInt();
    if (!flags) return "None";
    QStringList names;
    for (int bit = 0; bit < 4; ++bit)
        if (flags & (1 << bit)) names << QString("F%1").arg(bit + 1);
    if (flags & 256) names << "RO";
    if (flags & 512) names << "SYS";
    if (flags & 1024) names << "ARC";
    if (flags & 0x3800) names << "Password protection";
    return names.isEmpty() ? QString::number(flags) : names.join(", ");
}

QString blocks(const QJsonObject &extent) {
    QStringList pointers;
    for (const auto &pointer : extent["pointers"].toArray())
        pointers << (pointer.isNull() ? QString("–") : QString::number(pointer.toInt()));
    return pointers.join(", ");
}

QString slotText(const QJsonObject &slot) {
    if (slot.isEmpty()) return "Unused or absent";
    const auto kind = slot["kind"].toString();
    if (kind == "label") return "Disk label";
    if (kind == "timestamp") return "Timestamp";
    if (kind == "password") return "Password metadata";
    if (kind == "unknown") return "Unknown entry";
    const auto extension = slot["extension"].toString();
    return QString("User %1 · %2%3 · extent %4")
        .arg(slot["user"].toInt()).arg(slot["name"].toString(), extension.isEmpty() ? QString() : "." + extension)
        .arg(slot["extent"].toInt());
}

QTableWidget *makeTable(QWidget *parent, const QStringList &columns, const char *name) {
    auto *table = new QTableWidget(0, columns.size(), parent);
    table->setObjectName(name);
    table->setHorizontalHeaderLabels(columns);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

QPlainTextEdit *makeDetails(QWidget *parent, const char *name) {
    auto *field = new QPlainTextEdit(parent);
    field->setObjectName(name);
    field->setReadOnly(true);
    field->setMaximumHeight(145);
    return field;
}

void appendRow(QTableWidget *table, const QStringList &values) {
    const int row = table->rowCount();
    table->insertRow(row);
    for (int col = 0; col < values.size(); ++col)
        table->setItem(row, col, new QTableWidgetItem(values.at(col)));
}
}

ImageComparison::ImageComparison(Source left, QList<Source> targets, QWidget *parent)
    : QDialog(parent), left(std::move(left)), targets(std::move(targets)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::ApplicationModal);
    setObjectName("imageComparison");
    setWindowTitle("Compare Images");
    resize(900, 600);

    auto *layout = new QVBoxLayout(this);
    auto *selection = new QHBoxLayout;
    auto *leftLabel = new QLabel("Left: " + this->left.label, this);
    leftLabel->setTextFormat(Qt::PlainText);
    selection->addWidget(leftLabel, 1);
    auto *rightLabel = new QLabel("Right", this);
    selection->addWidget(rightLabel);
    targetPicker = new QComboBox(this);
    targetPicker->setObjectName("imageComparisonTarget");
    rightLabel->setBuddy(targetPicker);
    targetPicker->setAccessibleName("Right image tab");
    for (const auto &choice : this->targets) targetPicker->addItem(choice.label);
    selection->addWidget(targetPicker, 1);
    compare = new QPushButton("Compare", this);
    compare->setObjectName("imageComparisonRun");
    compare->setDefault(true);
    selection->addWidget(compare);
    layout->addLayout(selection);
    status = new QLabel(this);
    status->setObjectName("imageComparisonStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    summary = new QLabel(this);
    summary->setObjectName("imageComparisonSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    results = new QTabWidget(this);
    results->hide();
    auto *files = new QWidget(results);
    auto *fileLayout = new QVBoxLayout(files);
    auto *filters = new QHBoxLayout;
    areas = new QComboBox(files);
    areas->setObjectName("imageComparisonArea");
    areas->setAccessibleName("User area");
    filter = new QComboBox(files);
    filter->setObjectName("imageComparisonFilter");
    filter->setAccessibleName("File result filter");
    filter->addItems({"All results", "Differences", "Identical files", "Could not compare"});
    filters->addWidget(areas);
    filters->addWidget(filter);
    filters->addStretch();
    fileLayout->addLayout(filters);
    fileTable = makeTable(files, {"User", "File", "Result", "Directory structure"}, "imageComparisonFiles");
    fileLayout->addWidget(fileTable, 1);
    fileDetails = makeDetails(files, "imageComparisonFileDetails");
    fileLayout->addWidget(fileDetails);
    results->addTab(files, "Files");

    auto *directory = new QWidget(results);
    auto *slotLayout = new QVBoxLayout(directory);
    slotTable = makeTable(directory, {"Slot", "Left entry", "Right entry", "Result"}, "imageComparisonDirectory");
    slotLayout->addWidget(slotTable, 1);
    slotDetails = makeDetails(directory, "imageComparisonSlotDetails");
    slotLayout->addWidget(slotDetails);
    results->addTab(directory, "Directory Differences");
    layout->addWidget(results, 1);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(close);

    connect(compare, &QPushButton::clicked, this, &ImageComparison::beginComparison);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::finished, this, [this] {
        if (pendingSide >= 0) source(pendingSide).cancel();
    });
    connect(targetPicker, &QComboBox::currentIndexChanged, this, [this] {
        results->hide();
        summary->clear();
        status->clear();
    });
    connect(areas, &QComboBox::currentIndexChanged, this, &ImageComparison::renderFiles);
    connect(filter, &QComboBox::currentIndexChanged, this, &ImageComparison::renderFiles);
    connect(fileTable, &QTableWidget::itemSelectionChanged, this, &ImageComparison::showFileDetails);
    connect(slotTable, &QTableWidget::itemSelectionChanged, this, &ImageComparison::showSlotDetails);
}

const ImageComparison::Source &ImageComparison::source(int side) const {
    return side == 0 ? left : targets.at(target);
}

void ImageComparison::beginComparison() {
    target = targetPicker->currentIndex();
    if (target < 0) return;
    compare->setEnabled(false);
    targetPicker->setEnabled(false);
    results->hide();
    summary->clear();
    sides[0] = {};
    sides[1] = {};
    contentNames.clear();
    fileKeys.clear();
    slotKeys.clear();
    fileResults = {};
    directoryResults = {};
    collect(0, false);
}

void ImageComparison::send(int side, QJsonObject fields, std::function<void(QJsonObject)> accept) {
    pendingSide = side;
    source(side).request(std::move(fields), [guard = QPointer<ImageComparison>(this), accept = std::move(accept)]
                         (QJsonObject result, const QString &error) {
        if (!guard || !guard->isVisible()) return;
        guard->pendingSide = -1;
        if (error.isEmpty()) accept(std::move(result));
        else guard->fail(error);
    });
}

void ImageComparison::collect(int side, bool directory, int offset) {
    status->setText(directory ? "Reading directory structures…" : "Reading file lists…");
    auto &data = sides[side];
    QJsonObject fields{{"op", directory ? "filesystem_structure" : "comparison_manifest"},
                       {"start", offset}, {"limit", 128}};
    if (directory) fields["view"] = "directory";
    if (!data.manifest.isEmpty()) fields["revision"] = data.manifest["revision"];
    send(side, fields, [this, side, directory, offset](const QJsonObject &result) {
        auto &data = sides[side];
        const auto revision = result["revision"].toString();
        if (!result["rows"].isArray() || revision.isEmpty() ||
            (!data.manifest.isEmpty() && revision != data.manifest["revision"].toString())) {
            fail("The image reader returned an incomplete comparison page.");
            return;
        }
        if (!directory && offset == 0) {
            data.manifest = result;
            data.manifest.remove("rows");
        }
        if (directory) {
            data.directorySlots = result["directory_slots"].toInt();
            data.directoryBlocks = result["directory_blocks"].toInt();
            data.blockSize = result["block_size"].toInt();
        }
        for (const auto &value : result["rows"].toArray()) {
            const auto row = value.toObject();
            if (!directory) {
                data.files.insert(row["name"].toString(), row);
            } else if (row["kind"].toString() != "unused") {
                data.directory.insert(row["slot"].toInt(), row);
                if (row["kind"].toString() == "file") {
                    const auto extension = row["extension"].toString();
                    const auto name = QString("%1").arg(row["user"].toInt(), 2, 10, QLatin1Char('0'))
                        + row["name"].toString().toUpper() + (extension.isEmpty() ? QString() : "." + extension.toUpper());
                    data.extents[name].append(row);
                }
            }
        }
        const int next = result["next_start"].toInt(-1);
        if (next > offset) { collect(side, directory, next); return; }
        if (!directory) { collect(side, true); return; }
        if (side == 0) { collect(1, false); return; }

        for (auto it = sides[0].files.cbegin(); it != sides[0].files.cend(); ++it) {
            fileKeys << it.key();
            const auto other = sides[1].files.value(it.key());
            if (!other.isEmpty() && it.value()["size"] == other["size"]) contentNames << it.key();
        }
        for (const auto &name : sides[1].files.keys())
            if (!sides[0].files.contains(name)) fileKeys << name;
        std::sort(fileKeys.begin(), fileKeys.end());
        slotKeys = sides[0].directory.keys();
        for (const auto slot : sides[1].directory.keys())
            if (!sides[0].directory.contains(slot)) slotKeys << slot;
        std::sort(slotKeys.begin(), slotKeys.end());
        fingerprint(0, 0);
    });
}

void ImageComparison::fingerprint(int side, int offset) {
    if (offset >= contentNames.size()) {
        if (side == 0) fingerprint(1, 0);
        else classify(false);
        return;
    }
    status->setText(QString("Reading file contents · %1 of %2 files on %3")
                    .arg(offset + 1).arg(contentNames.size()).arg(side == 0 ? "left" : "right"));
    QJsonArray names;
    for (int index = offset; index < contentNames.size() && names.size() < 8; ++index)
        names.append(contentNames.at(index));
    send(side, {{"op", "comparison_fingerprints"}, {"names", names},
                {"revision", sides[side].manifest["revision"]}}, [this, side, offset, names](const QJsonObject &result) {
        const auto hashes = result["rows"].toArray();
        if (result["revision"] != sides[side].manifest["revision"] || hashes.size() != names.size()) {
            fail("The image reader returned incomplete file fingerprints.");
            return;
        }
        for (const auto &value : hashes) {
            const auto row = value.toObject();
            auto &file = sides[side].files[row["name"].toString()];
            if (row["sha256"].isString()) file["sha256"] = row["sha256"];
            if (row["error"].isString()) file["read_error"] = row["error"];
        }
        fingerprint(side, offset + names.size());
    });
}

void ImageComparison::classify(bool directory, int offset) {
    const int count = directory ? slotKeys.size() : fileKeys.size();
    if (offset >= count) {
        if (directory) verifyRevision(0);
        else classify(true);
        return;
    }
    status->setText(directory ? "Comparing directory slots…" : "Comparing files…");
    QJsonArray pairs;
    qsizetype bytes = 0;
    for (int index = offset; index < count && pairs.size() < 64; ++index) {
        QJsonObject pair;
        for (int side = 0; side < 2; ++side) {
            QJsonObject row;
            if (directory) {
                row = sides[side].directory.value(slotKeys.at(index));
            } else {
                const auto name = fileKeys.at(index);
                row = sides[side].files.value(name);
                if (!row.isEmpty()) row["extents"] = sides[side].extents.value(name);
            }
            if (!row.isEmpty()) pair[side == 0 ? "left" : "right"] = row;
        }
        // Leave room for the request envelope and the broker's 2 MiB input limit.
        const auto size = QJsonDocument(pair).toJson(QJsonDocument::Compact).size();
        if (size > 1500000) {
            fail("The directory structure for one file is too large to compare.");
            return;
        }
        if (bytes + size > 1500000) break;
        bytes += size;
        pairs.append(pair);
    }
    send(0, {{"op", "comparison_classify"}, {"view", directory ? "directory" : "files"},
             {"left_definition", sides[0].manifest["definition_sha256"]},
             {"right_definition", sides[1].manifest["definition_sha256"]}, {"pairs", pairs}},
         [this, directory, offset, count = pairs.size()](const QJsonObject &result) {
        const auto rows = result["rows"].toArray();
        if (rows.size() != count) {
            fail("The image reader returned incomplete comparison results.");
            return;
        }
        auto &destination = directory ? directoryResults : fileResults;
        for (const auto &value : rows) destination.append(value);
        classify(directory, offset + count);
    });
}

void ImageComparison::verifyRevision(int side) {
    send(side, {{"op", "comparison_manifest"}, {"limit", 1}, {"revision", sides[side].manifest["revision"]}},
         [this, side](const QJsonObject &result) {
        if (result["revision"] != sides[side].manifest["revision"]) {
            fail("A working image changed. Compare the tabs again.");
        } else if (side == 0) {
            verifyRevision(1);
        } else {
            render();
        }
    });
}

void ImageComparison::fail(const QString &message) {
    status->setText(message);
    compare->setEnabled(true);
    targetPicker->setEnabled(true);
}

void ImageComparison::render() {
    QStringList description;
    for (int side = 0; side < 2; ++side) {
        const auto &data = sides[side];
        description << QString("%1: %2 · %3 · %4 directory slots%5")
                       .arg(side == 0 ? "Left" : "Right", source(side).label, data.manifest["format"].toString())
                       .arg(data.directorySlots)
                       .arg(data.manifest["dirty"].toBool() ? " · Unsaved changes included" : "");
        const auto total = data.manifest["total_bytes"].toInteger(-1);
        const auto free = data.manifest["free_bytes"].toInteger(-1);
        if (data.blockSize > 0 && free >= 0 && total >= free) {
            const auto allocated = (total - free) / data.blockSize - data.directoryBlocks;
            description.last() += QString(" · %1 directory blocks, %2 allocated, %3 free")
                                  .arg(data.directoryBlocks).arg(allocated).arg(free / data.blockSize);
        }
    }
    if (sides[0].manifest["definition_sha256"] != sides[1].manifest["definition_sha256"])
        description << "Disk definitions differ. Allocation block numbers are local to each side.";
    summary->setText(description.join('\n'));
    QSet<int> users;
    for (const auto &row : fileResults) users.insert(row.toObject()["user"].toInt());
    auto orderedUsers = users.values();
    std::sort(orderedUsers.begin(), orderedUsers.end());
    areas->clear();
    areas->addItem("All user areas", -1);
    for (const int user : orderedUsers) areas->addItem(QString("User %1").arg(user), user);

    slotTable->setRowCount(0);
    visibleSlots.clear();
    for (const auto &value : directoryResults) {
        const auto row = value.toObject();
        const auto result = row["status"].toString();
        if (result == "identical") continue;
        const int slot = row["slot"].toInt();
        visibleSlots << slot;
        appendRow(slotTable, {QString::number(slot), slotText(sides[0].directory.value(slot)),
                             slotText(sides[1].directory.value(slot)),
                             result == "unavailable" ? "Blocks incomparable" :
                             result == "added" ? "Added" : result == "removed" ? "Removed" : "Changed"});
    }
    results->show();
    renderFiles();
    if (slotTable->rowCount()) slotTable->selectRow(0);
    showSlotDetails();
    status->setText(QString("%1 file identities · %2 directory slots with differences or incomplete comparisons")
                    .arg(fileResults.size()).arg(visibleSlots.size()));
    compare->setEnabled(true);
    targetPicker->setEnabled(true);
}

void ImageComparison::renderFiles() {
    fileTable->setRowCount(0);
    visibleFiles.clear();
    const int user = areas->currentData().toInt();
    for (const auto &value : fileResults) {
        const auto row = value.toObject();
        const int area = row["user"].toInt();
        const auto status = row["status"].toString();
        const auto structure = row["structure"].toObject();
        QStringList flags;
        if (structure["slots_changed"].toBool()) flags << "Slots";
        if (structure["extents_changed"].toBool()) flags << "Extents";
        if (structure["allocation_changed"].toBool()) flags << "Allocation";
        if (user >= 0 && area != user) continue;
        if ((filter->currentIndex() == 1 && (status == "identical" || status == "unavailable")
             && flags.isEmpty() && !row["attributes_changed"].toBool()) ||
            (filter->currentIndex() == 2 && status != "identical") ||
            (filter->currentIndex() == 3 && status != "unavailable")) continue;
        if (!structure.isEmpty() && structure["allocation_changed"].isNull()) flags << "Blocks incomparable";
        visibleFiles << row["name"].toString();
        appendRow(fileTable, {QString::number(area), row["filename"].toString(), comparisonStatus(row), flags.join(", ")});
    }
    if (fileTable->rowCount()) fileTable->selectRow(0);
    showFileDetails();
}

void ImageComparison::showFileDetails() {
    const int row = fileTable->currentRow();
    if (row < 0 || row >= visibleFiles.size()) {
        fileDetails->setPlainText("No files match these filters.");
        return;
    }
    const auto name = visibleFiles.at(row);
    QStringList lines;
    for (int side = 0; side < 2; ++side) {
        const auto file = sides[side].files.value(name);
        const auto label = side == 0 ? QString("Left") : QString("Right");
        if (file.isEmpty()) {
            lines << label + ": Absent";
            continue;
        }
        lines << QString("%1: %2 logical bytes · Attributes %3").arg(label).arg(file["size"].toInteger()).arg(attributes(file));
        if (file["read_error"].isString()) lines << "Content read failed (" + file["read_error"].toString() + ").";
        for (const auto &value : sides[side].extents.value(name)) {
            const auto extent = value.toObject();
            lines << QString("  Slot %1 · extent %2 · blocks %3")
                     .arg(extent["slot"].toInt()).arg(extent["extent"].toInt()).arg(blocks(extent));
        }
    }
    fileDetails->setPlainText(lines.join('\n'));
}

void ImageComparison::showSlotDetails() {
    const int row = slotTable->currentRow();
    if (row < 0 || row >= visibleSlots.size()) {
        slotDetails->setPlainText("No directory slot differences found.");
        return;
    }
    const int slot = visibleSlots.at(row);
    QStringList lines;
    for (int side = 0; side < 2; ++side) {
        const auto record = sides[side].directory.value(slot);
        lines << QString("%1: %2").arg(side == 0 ? "Left" : "Right", slotText(record));
        if (record["kind"].toString() == "file") {
            lines << QString("  Records %1 · Attributes %2 · Blocks %3")
                     .arg(record["record_count"].toInt())
                     .arg(attributes(QJsonObject{{"attr", record["attributes"]}}), blocks(record));
        }
    }
    slotDetails->setPlainText(lines.join('\n'));
}
