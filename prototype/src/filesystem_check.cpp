// SPDX-License-Identifier: GPL-3.0-or-later
#include "filesystem_check.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

namespace {
constexpr int workingMode = 0;
constexpr int externalMode = 1;

QString findingLabel(const QString &code) {
    static const QMap<QString, QString> labels{
        {"invalid_status", "Invalid directory status"},
        {"invalid_name_character", "Invalid filename character"},
        {"invalid_extent_low", "Invalid low extent byte"},
        {"invalid_extent_high", "Invalid high extent byte"},
        {"invalid_record_count", "Invalid record count"},
        {"invalid_last_record_count", "Invalid last-record byte count"},
        {"reserved_block", "Reference to a directory-reserved block"},
        {"block_out_of_range", "Block reference out of range"},
        {"shared_block", "Shared allocation block"},
        {"missing_allocated_sectors", "Missing allocated sectors"},
        {"duplicate_extent", "Duplicate file extent"},
        {"duplicate_label", "Duplicate disk label"},
        {"misplaced_timestamp", "Misplaced timestamp entry"},
    };
    return labels.value(code, code);
}

QString phaseLabel(const QString &phase) {
    if (phase == "open") return "opening the image";
    if (phase == "container") return "reading the container";
    if (phase == "definition") return "reading the disk definition";
    if (phase == "geometry") return "applying the disk geometry";
    if (phase == "directory") return "reading the directory";
    return "checking the filesystem";
}

QString number(const QJsonValue &value, int base = 0) {
    return value.isDouble() ? QString::number(value.toInteger() + base) : QString();
}
}

FilesystemCheckDialog::FilesystemCheckDialog(QString activeImage, QString activeFormat, Request request,
                                             OpenSlot openFindingSlot, QWidget *parent)
    : QDialog(parent), request(std::move(request)), openSlot(std::move(openFindingSlot)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle("Filesystem Check");
    setObjectName("filesystemCheckDialog");
    resize(860, 470);

    auto *layout = new QVBoxLayout(this);
    auto *source = new QFormLayout;
    mode = new QComboBox(this);
    mode->setObjectName("filesystemCheckMode");
    mode->addItem(activeImage.isEmpty() ? "External image" :
                  QString("Active tab · %1 · %2").arg(activeImage, activeFormat),
                  activeImage.isEmpty() ? externalMode : workingMode);
    if (!activeImage.isEmpty()) mode->addItem("External image", externalMode);
    source->addRow("Check", mode);
    layout->addLayout(source);

    external = new QWidget(this);
    auto *fields = new QFormLayout(external);
    fields->setContentsMargins(0, 0, 0, 0);
    auto *file = new QHBoxLayout;
    path = new QLineEdit(external);
    path->setObjectName("filesystemCheckPath");
    file->addWidget(path, 1);
    auto *browse = new QPushButton("Browse…", external);
    browse->setAutoDefault(false);
    file->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto selected = QFileDialog::getOpenFileName(this, "Choose a disk image to check", path->text(),
            "Disk images (*.raw *.img *.dsk *.imd *.jv3 *.dmk *.td0 *.TD0);;All files (*)");
        if (!selected.isEmpty()) path->setText(selected);
    });
    fields->addRow("Image file", file);
    formats = new QComboBox(external);
    formats->setObjectName("filesystemCheckFormat");
    formats->addItem("Choose a disk definition");
    fields->addRow("Disk definition", formats);
    layout->addWidget(external);

    identity = new QLabel(this);
    identity->setObjectName("filesystemCheckIdentity");
    identity->setTextFormat(Qt::PlainText);
    identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    identity->setWordWrap(true);
    layout->addWidget(identity);
    status = new QLabel(this);
    status->setObjectName("filesystemCheckStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);

    table = new QTableWidget(0, 8, this);
    table->setObjectName("filesystemCheckFindings");
    table->setAccessibleName("Filesystem findings");
    table->setHorizontalHeaderLabels({"Finding", "Slot", "Block", "Pointer", "Related slot",
                                      "Related pointer", "Observed", "Expected"});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    run = buttons->addButton("Run Check", QDialogButtonBox::ActionRole);
    run->setObjectName("filesystemCheckRun");
    run->setDefault(true);
    goToSlot = buttons->addButton("Go to Directory Slot", QDialogButtonBox::ActionRole);
    goToSlot->setObjectName("filesystemCheckGoToSlot");
    goToSlot->setAutoDefault(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(run, &QPushButton::clicked, this, &FilesystemCheckDialog::check);
    connect(goToSlot, &QPushButton::clicked, this, [this] {
        const int row = table->currentRow();
        if (row >= 0 && row < findings.size() && !checkedRevision.isEmpty())
            openSlot(findings.at(row).toObject()["slot"].toInt(), checkedRevision);
    });
    connect(table, &QTableWidget::itemSelectionChanged, this, &FilesystemCheckDialog::updateControls);
    connect(mode, &QComboBox::currentIndexChanged, this, &FilesystemCheckDialog::updateSource);
    connect(path, &QLineEdit::textChanged, this, [this] { clearReport(); });
    connect(formats, &QComboBox::currentIndexChanged, this, [this] { clearReport(); });
    external->setVisible(mode->currentData().toInt() == externalMode);
    updateControls();
    QTimer::singleShot(0, this, [this] { if (isVisible()) updateSource(); });
}

void FilesystemCheckDialog::updateControls() {
    const bool fileMode = mode->currentData().toInt() == externalMode;
    mode->setEnabled(!busy);
    external->setEnabled(!busy);
    run->setEnabled(!busy && (!fileMode || (catalogueLoaded && !path->text().trimmed().isEmpty()
                                           && formats->currentData().isValid())));
    goToSlot->setVisible(!checkedRevision.isEmpty());
    goToSlot->setEnabled(!busy && table->currentRow() >= 0);
}

void FilesystemCheckDialog::clearReport() {
    checkedRevision.clear();
    findings = {};
    table->setRowCount(0);
    identity->clear();
    status->clear();
    updateControls();
}

void FilesystemCheckDialog::updateSource() {
    clearReport();
    const bool fileMode = mode->currentData().toInt() == externalMode;
    external->setVisible(fileMode);
    if (!fileMode || catalogueLoaded) return;
    busy = true;
    updateControls();
    status->setText("Loading disk definitions…");
    request({{"op", "catalogue"}}, [guard = QPointer<FilesystemCheckDialog>(this)]
            (const QJsonObject &result, const QString &error) {
        if (!guard || !guard->isVisible()) return;
        guard->busy = false;
        if (!error.isEmpty() || !result["formats"].isArray()) {
            guard->status->setText(error.isEmpty() ? "Disk definitions could not be loaded." : error);
        } else {
            QMap<QString, QString> entries;
            for (const auto &value : result["formats"].toArray()) {
                const auto row = value.toObject();
                const auto id = row["id"].toString();
                if (!id.isEmpty()) entries.insert(id, row["display_name"].toString(id));
            }
            for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
                const auto label = it.key() == it.value() ? it.key() : it.key() + " · " + it.value();
                guard->formats->addItem(label, it.key());
            }
            guard->catalogueLoaded = true;
            guard->status->clear();
        }
        guard->updateControls();
    });
}

void FilesystemCheckDialog::check() {
    QJsonObject fields{{"op", "filesystem_check"}};
    if (mode->currentData().toInt() == externalMode) {
        fields["image"] = path->text();
        fields["format"] = formats->currentData().toString();
    }
    clearReport();
    busy = true;
    updateControls();
    status->setText("Checking filesystem…");
    request(fields, [guard = QPointer<FilesystemCheckDialog>(this)]
            (const QJsonObject &result, const QString &error) {
        if (!guard || !guard->isVisible()) return;
        guard->busy = false;
        if (error.isEmpty()) guard->showReport(result);
        else guard->status->setText(error);
        guard->updateControls();
    });
}

void FilesystemCheckDialog::showReport(const QJsonObject &report) {
    const auto outcome = report["status"].toString();
    if ((outcome != "clean" && outcome != "findings" && outcome != "incomplete") || !report["findings"].isArray()) {
        status->setText("The image reader returned an incomplete report.");
        return;
    }
    const bool working = report["mode"].toString() == "working";
    const auto format = report["format"].toString();
    if (working) {
        identity->setText(QString("Working image · %1%2").arg(format,
            report["dirty"].toBool() ? " · Unsaved changes included" : ""));
    } else {
        identity->setText(QString("Source %1 · %2 · Definition %3\nSHA-256 %4")
            .arg(report["source_name"].toString(), report["container"].toString().toUpper(), format,
                 report["source_sha256"].toString()));
    }
    if (outcome == "incomplete") {
        status->setText(QString("Check incomplete while %1. %2")
                        .arg(phaseLabel(report["phase"].toString()), report["message"].toString()));
        return;
    }
    if (outcome == "clean") {
        status->setText("No inconsistencies found in the checked directory entries, extents, and allocation references.");
        return;
    }
    const int count = report["finding_count"].toInt();
    const int omitted = report["omitted"].toInt();
    status->setText(omitted ? QString("%1 findings · %2 omitted from this report").arg(count).arg(omitted) :
                    QString(count == 1 ? "%1 finding" : "%1 findings").arg(count));
    if (working) checkedRevision = report["revision"].toString();
    findings = report["findings"].toArray();
    table->setRowCount(findings.size());
    for (int row = 0; row < findings.size(); ++row) {
        const auto item = findings.at(row).toObject();
        const QStringList values{findingLabel(item["code"].toString()), number(item["slot"]),
                                 number(item["block"]), number(item["pointer"], 1), number(item["related_slot"]),
                                 number(item["related_pointer"], 1), number(item["observed"]),
                                 item["expected"].toString()};
        for (int col = 0; col < values.size(); ++col)
            table->setItem(row, col, new QTableWidgetItem(values.at(col)));
    }
    if (!findings.isEmpty()) table->selectRow(0);
}
