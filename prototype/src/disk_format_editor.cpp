// SPDX-License-Identifier: GPL-3.0-or-later
#include "disk_format_editor.h"
#include "format_diagnostics.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QVector>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>
#include <limits>
#include <utility>

namespace {
constexpr int maxInteger = std::numeric_limits<int>::max();
constexpr qint64 workingLimit = 128 * 1024 * 1024;
constexpr qint64 importLimit = 256 * 1024;

QJsonValue automatic(int value) {
    return value ? QJsonValue(value) : QJsonValue(QJsonValue::Null);
}

const QStringList offsetUnits{"bytes", "kib", "mib", "sectors", "tracks"};
}

DiskFormatEditor::DiskFormatEditor(Request request, const QJsonArray &catalogue,
                                   const QString &imagePath, const QString &selectedFormat, QWidget *parent)
    : QDialog(parent), request(std::move(request)), catalogue(catalogue) {
    setObjectName("diskFormatEditor");
    setWindowTitle("Disk Format Editor");
    resize(680, 710);
    auto *outer = new QVBoxLayout(this);
    form = new QWidget;
    auto *body = new QVBoxLayout(form);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(form);
    outer->addWidget(scroll);
    auto fieldGroup = [&](const QString &title) {
        auto *group = new QGroupBox(title);
        body->addWidget(group);
        return new QFormLayout(group);
    };
    auto button = [&](QLayout *layout, const QString &text, const QString &name, auto action) {
        auto *control = new QPushButton(text);
        control->setObjectName(name);
        control->setAutoDefault(false);
        layout->addWidget(control);
        connect(control, &QPushButton::clicked, this, action);
        return control;
    };
    auto *picker = new QHBoxLayout;
    body->addLayout(picker);
    selection = new QComboBox;
    selection->setObjectName("diskFormatRecords");
    picker->addWidget(selection, 1);
    button(picker, "New", "diskFormatNew", [this] { load({}); });
    button(picker, "Duplicate", "diskFormatDuplicate", [this] { duplicateSelected(); });
    button(picker, "Import diskdefs…", "diskFormatImport", [this] {
        auto *dialog = new QFileDialog(this, "Import diskdefs");
        dialog->setObjectName("diskFormatImportDialog");
        dialog->setFileMode(QFileDialog::ExistingFile);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &QFileDialog::fileSelected, this, [this](const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) { showError(file.errorString()); return; }
            const auto bytes = file.read(importLimit + 1);
            if (bytes.size() > importLimit || bytes.contains('\0')) {
                showError("Choose a UTF-8 definition file no larger than 256 KiB."); return;
            }
            const auto text = QString::fromUtf8(bytes);
            if (text.toUtf8() != bytes && !(bytes.startsWith("\xef\xbb\xbf") && text.toUtf8() == bytes.mid(3))) {
                showError("The definition file must be UTF-8."); return;
            }
            importText(text);
        });
        dialog->open();
    });

    auto *identity = fieldGroup("Identity");
    auto line = [&](QFormLayout *layout, const QString &label, const QString &name) {
        auto *control = new QLineEdit;
        control->setObjectName(name);
        layout->addRow(label, control);
        connect(control, &QLineEdit::textChanged, this, [this] { updateDerived(); });
        return control;
    };
    auto number = [&](QFormLayout *layout, const QString &label, const QString &name,
                      const QString &empty = QString()) {
        auto *control = new QSpinBox;
        control->setObjectName(name);
        // Preserve imported values so validation can report them without clamping.
        control->setRange(0, maxInteger);
        control->setSpecialValueText(empty);
        layout->addRow(label, control);
        connect(control, &QSpinBox::valueChanged, this, [this] { updateDerived(); });
        return control;
    };
    auto combo = [&](QFormLayout *layout, const QString &label, const QStringList &options, const QString &name) {
        auto *control = new QComboBox;
        control->setObjectName(name);
        control->addItems(options);
        layout->addRow(label, control);
        connect(control, &QComboBox::currentIndexChanged, this, [this] { updateDerived(); });
        return control;
    };
    identifier = line(identity, "Format identifier", "diskFormatIdentifier");
    displayName = line(identity, "Display name", "diskFormatDisplayName");

    auto *geometry = fieldGroup("Sector layout");
    sectorSize = number(geometry, "Bytes per sector", "diskFormatSectorSize", "Required");
    tracks = number(geometry, "Track groups", "diskFormatTracks", "Required");
    sectors = number(geometry, "Sectors per track", "diskFormatSectors", "Required");

    auto *filesystem = fieldGroup("Filesystem");
    blockSize = number(filesystem, "Allocation block bytes", "diskFormatBlockSize", "Required");
    entries = number(filesystem, "Directory entries", "diskFormatEntries", "Required");
    directoryBlocks = number(filesystem, "Reserved directory blocks", "diskFormatDirectoryBlocks", "Automatic");
    os = combo(filesystem, "OS", {"Default (CP/M 2.2)", "CP/M 2.2", "CP/M 3", "ISX", "P2DOS", "Z-System"}, "diskFormatOs");

    auto *boot = fieldGroup("Boot reservation");
    bootUnit = combo(boot, "Units", {"Tracks", "Sectors"}, "diskFormatBootUnit");
    bootCount = number(boot, "Count", "diskFormatBootCount");

    auto *advanced = fieldGroup("Advanced");
    skewMode = combo(advanced, "Sector ordering", {"Default (sequential)", "Skew step", "Explicit table"}, "diskFormatSkewMode");
    skewStep = number(advanced, "Skew step", "diskFormatSkewStep");
    slotOrder = line(advanced, "RAW slots (comma separated)", "diskFormatSlots");
    offsetUnit = combo(advanced, "Offset units", {"Bytes", "KiB", "MiB", "Sectors", "Tracks"}, "diskFormatOffsetUnit");
    offset = number(advanced, "Image offset", "diskFormatOffset");
    extents = number(advanced, "Logical extent factor", "diskFormatExtents", "Automatic");
    libdsk = line(advanced, "LibDsk standard geometry", "diskFormatLibDsk");
    connect(skewMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (loading || index != 2 || !slotOrder->text().isEmpty() || sectors->value() > 1024) return;
        const int width = sectors->value();
        const int step = previousSkewStep;
        QStringList order;
        QVector<bool> used(width, false);
        for (int i = 0, slot = 0; i < width; ++i, slot = (slot + qint64(step)) % width) {
            while (used[slot]) slot = (slot + 1) % width;
            used[slot] = true;
            order.append(QString::number(slot));
        }
        slotOrder->setText(order.join(','));
    });

    auto *check = fieldGroup("Image check");
    auto *file = new QHBoxLayout;
    image = new QLineEdit(imagePath);
    image->setObjectName("diskFormatCheckPath");
    file->addWidget(image);
    button(file, "Browse…", "diskFormatBrowse", [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Choose an image to check", image->text());
        if (!path.isEmpty()) image->setText(path);
    });
    check->addRow("Image", file);
    auto *actions = new QHBoxLayout;
    check->addRow(actions);
    button(actions, "Check against image", "diskFormatCheck", [this] { runDefinition("definition_check_image"); });
    applyMapping = button(actions, "Use mapped sector layout", "diskFormatUseMapping", [this] {
        if (checkedGeometry.isEmpty()) return;
        const auto geometry = checkedGeometry;
        loading = true;
        sectorSize->setValue(geometry["sector_size"].toInt());
        tracks->setValue(geometry["tracks"].toInt());
        sectors->setValue(geometry["sectors_per_track"].toInt());
        loading = false;
        updateDerived();
    });
    button(actions, "Preview mapping conversion…", "diskFormatPreviewMapping", [this] { runDefinition("mapping_preview"); });
    connect(image, &QLineEdit::textChanged, this, [this] { invalidateImageCheck(); });

    auto *results = fieldGroup("Results");
    results->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto label = [&](const QString &title, const QString &name) {
        auto *control = new QLabel;
        control->setObjectName(name);
        control->setTextFormat(Qt::PlainText);
        control->setWordWrap(true);
        control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        results->addRow(title, control);
        return control;
    };
    derived = label("Calculated layout", "diskFormatDerived");
    definitionStatus = label("Definition", "diskFormatValidation");
    imageStatus = label("Image compatibility", "diskFormatCompatibility");
    operationStatus = label("Operations", "diskFormatOperations");
    diagnostics = label("Import diagnostics", "diskFormatDiagnostics");
    auto *footer = new QHBoxLayout;
    outer->addLayout(footer);
    button(footer, "Validate", "diskFormatValidate", [this] { runDefinition("definition_validate"); });
    button(footer, "Save definition", "diskFormatSave", [this] { runDefinition("definition_save"); });
    button(footer, "Export diskdefs…", "diskFormatExport", [this] { runDefinition("definition_export"); });
    footer->addStretch();
    button(footer, "Close", "diskFormatClose", [this] { reject(); });
    connect(selection, &QComboBox::activated, this, [this](int index) {
        if (index == 0) { load({}); return; }
        const auto id = selection->itemData(index).toString();
        for (const auto &value : records) {
            const auto item = value.toObject();
            if (item["identifier"].toString() == id) { load(item); return; }
        }
        send({{"op", "definition_import"}, {"catalogue_identifier", id}}, [this](auto reply, auto error) {
            if (!error.isEmpty()) { showError(error); return; }
            load(reply["record"].toObject());
            showImportDiagnostics(reply["diagnostics"].toArray());
            bundledSelection = true;
            formFieldsEnabled(false);
            definitionStatus->setText("Catalogue entry. Duplicate it to make an editable copy.");
        });
    });
    load({});
    preferredSelection = selectedFormat;
}

void DiskFormatEditor::reject() {
    if (!pending) QDialog::reject();
}

void DiskFormatEditor::send(QJsonObject message, Reply reply) {
    if (pending) return;
    pending = true;
    form->setEnabled(false);
    for (auto *action : findChildren<QPushButton *>(QString(), Qt::FindDirectChildrenOnly))
        if (action->objectName() != "diskFormatClose") action->setEnabled(false);
    QPointer<DiskFormatEditor> guard(this);
    request(std::move(message), [guard, reply = std::move(reply)](auto result, auto error) {
        if (!guard) return;
        guard->pending = false;
        guard->form->setEnabled(true);
        guard->formFieldsEnabled(!guard->bundledSelection);
        reply(std::move(result), std::move(error));
    });
}

void DiskFormatEditor::refreshRecords() {
    send({{"op", "definition_list"}}, [this](auto result, auto error) {
        if (!error.isEmpty()) { showError(error); return; }
        records = result["records"].toArray();
        const auto active = current["identifier"].toString();
        selection->clear();
        selection->addItem("New definition");
        QSet<QString> identifiers;
        for (const auto &value : records) {
            const auto item = value.toObject();
            const auto id = item["identifier"].toString();
            identifiers.insert(id);
            selection->addItem(item["display_name"].toString(id), id);
        }
        for (const auto &value : catalogue) {
            const auto id = value.toObject()["id"].toString();
            if (id != "amstrad" && !identifiers.contains(id)) selection->addItem(id, id);
        }
        if (!result["unsupported"].toArray().isEmpty())
            diagnostics->setText("Definitions from unsupported schema versions were preserved.");
        const int index = selection->findData(preferredSelection.isEmpty() ? active : preferredSelection);
        if (index > 0) {
            selection->setCurrentIndex(index);
            if (!preferredSelection.isEmpty()) emit selection->activated(index);
        }
        preferredSelection.clear();
    });
}

void DiskFormatEditor::formFieldsEnabled(bool enabled) {
    for (auto *control : QList<QWidget *>{identifier, displayName, sectorSize, tracks, sectors,
            blockSize, entries, directoryBlocks, os, bootUnit, bootCount, skewMode, offsetUnit,
            offset, extents, libdsk})
        control->setEnabled(enabled);
    skewStep->setEnabled(enabled && skewMode->currentIndex() == 1);
    slotOrder->setEnabled(enabled && skewMode->currentIndex() == 2);
    for (const auto *name : {"diskFormatSave", "diskFormatExport", "diskFormatValidate"})
        findChild<QPushButton *>(name)->setEnabled(enabled && !pending);
}

void DiskFormatEditor::load(const QJsonObject &item) {
    loading = true;
    bundledSelection = false;
    unresolved = false;
    current = item;
    if (current.isEmpty())
        current = {{"schema_version", 1}, {"record_id", QUuid::createUuid().toString(QUuid::WithoutBraces)}};
    if (item.isEmpty()) selection->setCurrentIndex(0);
    const auto fields = item["definition"].toObject();
    identifier->setText(item["identifier"].toString());
    displayName->setText(item["display_name"].toString());
    sectorSize->setValue(fields["seclen"].toInt());
    tracks->setValue(fields["tracks"].toInt());
    sectors->setValue(fields["sectrk"].toInt());
    blockSize->setValue(fields["blocksize"].toInt());
    entries->setValue(fields["maxdir"].toInt());
    directoryBlocks->setValue(fields["dirblks"].toInt());
    extents->setValue(fields["logicalextents"].toInt());
    libdsk->setText(fields["libdsk_format"].toString());
    const QStringList systems{"", "2.2", "3", "isx", "p2dos", "zsys"};
    os->setCurrentIndex(qMax<qsizetype>(0, systems.indexOf(fields["os"].toString())));
    const auto boot = fields["boot"].toObject();
    bootCount->setValue(boot["count"].toInt());
    bootUnit->setCurrentIndex(boot["unit"].toString() == "sectors" ? 1 : 0);
    const auto skew = fields["skew"].toObject();
    const auto mode = skew["mode"].toString();
    skewMode->setCurrentIndex(mode == "table" ? 2 : mode == "step" ? 1 : 0);
    skewStep->setValue(skew["step"].toInt(1));
    previousSkewStep = mode == "step" ? skewStep->value() : 1;
    QStringList values;
    for (const auto &slot : skew["slots"].toArray()) values.append(QString::number(slot.toInt()));
    slotOrder->setText(values.join(','));
    const auto start = fields["offset"].toObject();
    offset->setValue(start["value"].toInt());
    offsetUnit->setCurrentIndex(qMax<qsizetype>(0, offsetUnits.indexOf(start["unit"].toString())));
    diagnostics->clear();
    invalidateImageCheck();
    loading = false;
    updateDerived();
    baseline = record();
}

QJsonObject DiskFormatEditor::record() const {
    auto result = current;
    result["identifier"] = identifier->text();
    if (displayName->text().isEmpty()) result.remove("display_name");
    else result["display_name"] = displayName->text();
    QJsonObject skew{{"mode", "default"}};
    if (skewMode->currentIndex() == 1) skew = {{"mode", "step"}, {"step", skewStep->value()}};
    if (skewMode->currentIndex() == 2) {
        QJsonArray order;
        for (const auto &value : slotOrder->text().split(',')) {
            bool valid;
            const int index = value.trimmed().toInt(&valid);
            order.append(valid ? QJsonValue(index) : QJsonValue(value));
        }
        skew = {{"mode", "table"}, {"slots", order}};
    }
    const QStringList systems{"", "2.2", "3", "isx", "p2dos", "zsys"};
    result["definition"] = QJsonObject{
        {"seclen", sectorSize->value()}, {"tracks", tracks->value()}, {"sectrk", sectors->value()},
        {"blocksize", blockSize->value()}, {"maxdir", entries->value()},
        {"dirblks", automatic(directoryBlocks->value())}, {"logicalextents", automatic(extents->value())},
        {"boot", QJsonObject{{"unit", bootUnit->currentIndex() ? "sectors" : "tracks"}, {"count", bootCount->value()}}},
        {"skew", skew}, {"os", os->currentIndex() ? QJsonValue(systems.at(os->currentIndex())) : QJsonValue(QJsonValue::Null)},
        {"offset", QJsonObject{{"unit", offsetUnits.at(offsetUnit->currentIndex())}, {"value", offset->value()}}},
        {"libdsk_format", libdsk->text().isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(libdsk->text())}
    };
    return result;
}

void DiskFormatEditor::invalidateImageCheck() {
    checkedMapping = {};
    checkedGeometry = {};
    applyMapping->setEnabled(false);
    imageStatus->setText(image->text().isEmpty() ? "No image selected." : "Image unchecked.");
}

void DiskFormatEditor::updateDerived() {
    if (loading) return;
    formFieldsEnabled(!bundledSelection);
    if (skewMode->currentIndex() != 2)
        previousSkewStep = skewMode->currentIndex() == 1 ? skewStep->value() : 1;
    applyMapping->setEnabled(!bundledSelection && !checkedGeometry.isEmpty());
    imageStatus->setText(image->text().isEmpty() ? "No image selected." : "Image unchecked.");
    definitionStatus->setText("Definition unchecked.");
    operationStatus->setText("Validate the definition to check operation availability.");
    const qint64 sector = sectorSize->value(), width = sectors->value();
    const qint64 trackCount = tracks->value(), block = blockSize->value(), maxDir = entries->value();
    if (!sector || !trackCount || !width || !block || !maxDir) {
        derived->setText("Complete the sector and filesystem fields.");
        return;
    }
    if (sector > 16384 || width > 65536 || block > 16384 || maxDir > 65536) {
        derived->setText("Check the sector and filesystem field limits.");
        return;
    }
    const qint64 multiplier[] = {1, 1024, 1048576, sector, sector * width};
    const qint64 start = offset->value() * multiplier[offsetUnit->currentIndex()];
    const qint64 reserved = bootCount->value() * (bootUnit->currentIndex() ? 1 : width);
    const qint64 count = trackCount * width;
    if (count > maxInteger / sector || start > maxInteger || reserved >= count || block % sector || sector % 128) {
        derived->setText("Check sector size, allocation block size, capacity, offset, and boot reservation.");
        return;
    }
    const qint64 minimumDir = (maxDir * 32 + block - 1) / block;
    const qint64 total = (count - reserved) * sector / block;
    const qint64 dir = directoryBlocks->value() ? directoryBlocks->value() : minimumDir;
    const qint64 factor = extents->value() ? extents->value() : qMax<qint64>(1, ((total > 256 ? 8 : 16) * block) / 16384);
    derived->setText(QString("RAW size %1 bytes, offset %2 bytes\n"
                            "Boot reservation %3 sectors (%4 bytes)\n"
                            "%5 filesystem blocks, %6 directory blocks, extent factor %7\n"
                            "Initial file-data capacity %8 bytes")
                     .arg(start + count * sector).arg(start).arg(reserved).arg(reserved * sector)
                     .arg(total).arg(dir).arg(factor).arg(qMax<qint64>(0, total - dir) * block));
}

void DiskFormatEditor::showError(const QString &message) {
    definitionStatus->setText(message);
}

void DiskFormatEditor::importText(const QString &text) {
    send({{"op", "definition_import"}, {"text", text}}, [this](auto result, auto error) {
        if (!error.isEmpty()) { showError(error); return; }
        selection->setCurrentIndex(0);
        load(result["record"].toObject());
        baseline = {};
        showImportDiagnostics(result["diagnostics"].toArray());
    });
}

void DiskFormatEditor::showImportDiagnostics(const QJsonArray &items) {
    QStringList messages;
    for (const auto &value : items) {
        const auto item = value.toObject();
        messages.append(QString("Line %1 — %2").arg(item["line"].toInt()).arg(item["message"].toString()));
        unresolved |= item["code"].toString() != "noncanonical_offset";
    }
    diagnostics->setText(messages.join('\n'));
}

void DiskFormatEditor::duplicateSelected() {
    if (selection->currentData().toString() == "amstrad") return;
    auto copy = record();
    copy["record_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy["identifier"] = QString();
    const bool review = unresolved;
    const auto messages = diagnostics->text();
    selection->setCurrentIndex(0);
    load(copy);
    baseline = {};
    unresolved = review;
    diagnostics->setText(messages);
    identifier->setFocus();
}

void DiskFormatEditor::runDefinition(const QString &operation) {
    const bool checking = operation == "definition_check_image" || operation == "mapping_preview";
    if (checking && image->text().trimmed().isEmpty()) {
        imageStatus->setText("Choose an image to check.");
        return;
    }
    QJsonObject message{{"op", operation}, {"record", record()}};
    if (checking) message["image"] = image->text();
    if (unresolved && (operation == "definition_save" || operation == "definition_export")) {
        if (QMessageBox::question(this, "Resolve imported directives",
                "Use the fields shown in the editor to produce canonical diskdefs? The original block is preserved in the record.")
            != QMessageBox::Yes) return;
        message["resolved_import"] = true;
    }
    send(message, [this, operation, message, checking](auto result, auto error) {
        if (checking && (message["image"].toString() != image->text() ||
                         message["record"].toObject() != record())) return;
        if (!error.isEmpty()) {
            if (checking && !error.contains("(invalid_definition)")) {
                invalidateImageCheck();
                const auto findings = FormatDiagnostics::describe(result["evaluation"].toObject());
                imageStatus->setText(findings.isEmpty() ? error : findings);
            }
            else showError(error);
            return;
        }
        definitionStatus->setText("Definition accepted by cpmtools.");
        const auto geometry = result["geometry"].toObject();
        const qint64 bytes = geometry["offset"].toInteger() + geometry["tracks"].toInteger()
            * geometry["sectors_per_track"].toInteger() * geometry["sector_size"].toInteger();
        operationStatus->setText(bytes <= workingLimit
            ? "RAW initialization available within the working-image limit. Structured writes require a compatible mapping."
            : "Initialization unavailable. The image exceeds the 128 MiB working-image limit.");
        if (operation == "definition_save") {
            current = message["record"].toObject();
            saved = true;
            baseline = record();
            definitionStatus->setText("Definition saved.");
            refreshRecords();
        } else if (operation == "definition_export") {
            const auto path = QFileDialog::getSaveFileName(this, "Export diskdefs",
                                                           identifier->text() + ".FDF", "Disk definitions (*.FDF);;All files (*)");
            if (path.isEmpty()) return;
            const auto text = result["diskdefs"].toString().toUtf8();
            QSaveFile destination(path);
            if (!destination.open(QIODevice::WriteOnly) || destination.write(text) != text.size() || !destination.commit())
                showError(destination.errorString());
            else definitionStatus->setText("Disk definition exported.");
        } else if (operation == "definition_check_image") {
            checkedMapping = result["mapping"].toObject();
            checkedGeometry = checkedMapping.isEmpty() ? QJsonObject{} : geometry;
            applyMapping->setEnabled(!checkedGeometry.isEmpty() && !bundledSelection);
            const auto findings = FormatDiagnostics::describe(result["evaluation"].toObject());
            QString report = QString("%1 image readable. Mounted free space %2 bytes.")
                             .arg(result["container"].toString().toUpper()).arg(result["free_bytes"].toInteger());
            if (!findings.isEmpty()) report += '\n' + findings;
            if (!checkedMapping.isEmpty())
                report += '\n' + QString::fromUtf8(QJsonDocument(checkedMapping).toJson(QJsonDocument::Compact));
            imageStatus->setText(report);
        } else if (operation == "mapping_preview") {
            QMessageBox preview(QMessageBox::Question, "Replace mapping sidecar",
                                "Write the proposed strict mapping sidecar?", QMessageBox::Yes | QMessageBox::Cancel, this);
            preview.setDefaultButton(QMessageBox::Cancel);
            preview.setDetailedText(QString("Existing mapping\n%1\nProposed mapping\n%2")
                .arg(result["previous_profile"].isNull() ? QString("No sidecar") :
                     QString::fromUtf8(QJsonDocument(result["previous_profile"].toObject()).toJson()))
                .arg(QString::fromUtf8(QJsonDocument(result["profile"].toObject()).toJson())));
            if (preview.exec() != QMessageBox::Yes) return;
            auto writing = message;
            writing["op"] = "mapping_write";
            writing["profile"] = result["profile"];
            writing["mapping_sha256"] = result["mapping_sha256"];
            writing["confirmed"] = true;
            send(writing, [this](auto, auto failure) {
                imageStatus->setText(failure.isEmpty() ? "Mapping sidecar saved." : failure);
            });
        }
    });
}
