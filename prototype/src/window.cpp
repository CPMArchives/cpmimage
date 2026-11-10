// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QKeySequence>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QListWidget>
#include <QSplitter>
#include <QPainter>
#include <QStyle>
#include <QUuid>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <cmath>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QTextEdit>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#endif

Window::Window(QString root, QString python, QString helper, int deadlineMs, int graceMs, QString noticesPath)
    : root(root), python(python), helper(helper), deadlineMs(deadlineMs), graceMs(graceMs) {
#ifndef Q_OS_WIN
    process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#endif
    connect(&process, &QProcess::started, this, [this] {
        if (!containReader()) {
            stopReader(); fail("Cannot contain the image reader process tree."); return;
        }
        sendPending();
    });
    cancellationDeadline.setSingleShot(true);
    connect(&cancellationDeadline, &QTimer::timeout, this, [this] {
        stopReader(); resetImage();
        if (busy) fail("The image reader did not respond to cancellation and was stopped. Open the image again.");
    });
    setWindowTitle("cpmimage — Review prototype");
    auto *help = menuBar()->addMenu("Help");
    auto *licenses = help->addAction("License notices…");
    connect(licenses, &QAction::triggered, this, [this, noticesPath] {
        QDialog dialog(this); dialog.setWindowTitle("License notices"); dialog.resize(760, 540);
        auto *box = new QVBoxLayout(&dialog);
        auto *text = new QTextEdit; text->setReadOnly(true);
        QFile summary(QDir(noticesPath).filePath("THIRD-PARTY-NOTICES.txt"));
        text->setPlainText(summary.open(QIODevice::ReadOnly) ? QString::fromUtf8(summary.readAll()) :
            "cpmimage prototype: GPL-3.0-or-later. See the package's license-notice files.");
        box->addWidget(text);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        box->addWidget(buttons); dialog.exec();
    });
    resize(920, 580);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *title = new QLabel("<h2>CP/M disk images</h2>");
    layout->addWidget(title);
    notice = new QLabel("Browse and export files. The source image is never edited.");
    layout->addWidget(notice);
    auto *toolbar = new QHBoxLayout;
    formats = new QComboBox;
    const QList<QStringList> choices = {
        {"IBM 3740 · raw", "ibm-3740", "raw"},
        {"BetterCP/M CPMSIM · raw", "bettercpm-default", "raw"},
        {"BetterCP/M Model II · raw", "bcm2ss", "raw"},
        {"BetterCP/M Model 4 · raw", "bettercpm-mm-80t-ds-data", "raw"},
        {"Ampro Little Board · raw", "bettercpm-ampro-little-board", "raw"},
        {"Ampro Little Board · standard DSK", "bettercpm-ampro-little-board", "dsk"}};
    for (const auto &choice : choices) formats->addItem(choice[0], choice.mid(1));
    toolbar->addWidget(new QLabel("Image layout:"));
    toolbar->addWidget(formats, 1);
    openButton = new QPushButton("Open image…");
    exportButton = new QPushButton("Export selected…");
    cancelButton = new QPushButton("Cancel");
    openButton->setShortcut(QKeySequence::Open);
    exportButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    openButton->setToolTip("Open image (Ctrl+O / Command+O)");
    exportButton->setToolTip("Export selected (Ctrl+E / Command+E)");
    toolbar->addWidget(openButton);
    toolbar->addWidget(exportButton);
    toolbar->addWidget(cancelButton);
    layout->addLayout(toolbar);
    source = new QLabel("No image open");
    source->setTextFormat(Qt::PlainText);
    source->setWordWrap(true);
    source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(source);
    auto *splitter = new QSplitter;
    splitter->setObjectName("browserSplitter");
    userAreas = new QListWidget;
    userAreas->setObjectName("userAreas");
    userAreas->setSelectionMode(QAbstractItemView::SingleSelection);
    userAreas->setIconSize(QSize(24, 24));
    userAreas->setAccessibleName("CP/M user areas");
    for (int area = 0; area < 16; ++area) {
        auto *item = new QListWidgetItem(QString::number(area), userAreas);
        item->setData(Qt::UserRole, area);
    }
    auto *areaPane = new QWidget;
    auto *areaLayout = new QVBoxLayout(areaPane);
    areaLayout->setContentsMargins(0,0,0,0);
    areaLayout->addWidget(new QLabel("User areas"));
    areaLayout->addWidget(userAreas);
    splitter->addWidget(areaPane);
    table = new QTableWidget(0, 3);
    table->setObjectName("fileList");
    table->setAccessibleName("Files in selected user area");
    table->setHorizontalHeaderLabels({"Name", "Bytes", "Attributes"});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(true);
    table->sortItems(0, Qt::AscendingOrder);
    splitter->addWidget(table);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({160, 720});
    layout->addWidget(splitter, 1);
    auto *selectAll = new QAction("Select all", table);
    selectAll->setShortcut(QKeySequence::SelectAll);
    selectAll->setShortcutContext(Qt::WidgetShortcut);
    table->addAction(selectAll);
    connect(selectAll, &QAction::triggered, table, &QTableWidget::selectAll);
    auto *scope = new QLabel("Supported layouts only; no automatic format detection. Export keeps CP/M record padding.");
    scope->setWordWrap(true);
    layout->addWidget(scope);
    status = new QLabel("Choose the image layout, then open an image.");
    status->setObjectName("selectionStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status);
    setCentralWidget(central);
    connect(openButton, &QPushButton::clicked, this, &Window::chooseImage);
    connect(exportButton, &QPushButton::clicked, this, &Window::chooseExport);
    connect(cancelButton, &QPushButton::clicked, this, &Window::cancel);
    connect(table, &QTableWidget::itemSelectionChanged, this, [this] {
        setBusy(busy); updateSelectionStatus();
    });
    connect(userAreas, &QListWidget::currentItemChanged, this, [this] { showUserArea(); });
    connect(formats, &QComboBox::currentIndexChanged, this, [this] {
        if (process.state() != QProcess::NotRunning) {
            stopReader(); process.waitForFinished(2000);
        }
        resetImage(); status->setText("Layout changed. Open the image again.");
    });
    connect(&process, &QProcess::readyReadStandardOutput, this, &Window::readReply);
    // Discard diagnostics continuously; they never become unbounded retained UI state.
    connect(&process, &QProcess::readyReadStandardError, this, [this] { process.readAllStandardError(); });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (busy && e == QProcess::FailedToStart) fail("Cannot start the isolated image reader. Check the local Python path.");
    });
    connect(&process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        cancellationDeadline.stop(); deadline.stop(); pending.clear();
#ifdef Q_OS_WIN
        if (readerJob) { CloseHandle(static_cast<HANDLE>(readerJob)); readerJob = nullptr; }
#endif
        if (busy) fail("The image reader stopped unexpectedly. Open the image again.");
        resetImage();
        if (closing) close();
    });
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, this, [this] {
        cancel();
        status->setText("Taking too long. Cancelling the operation…");
    });
    resetImage();
}

Window::~Window() {
    process.closeWriteChannel();
    if (!process.waitForFinished(250)) { stopReader(); process.waitForFinished(2000); }
#ifdef Q_OS_WIN
    if (readerJob) CloseHandle(static_cast<HANDLE>(readerJob));
#endif
}
bool Window::containReader() {
#ifdef Q_OS_WIN
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process.processId()));
    const bool ok = child && SetInformationJobObject(job, JobObjectExtendedLimitInformation,
        &limits, sizeof(limits)) && AssignProcessToJobObject(job, child);
    if (child) CloseHandle(child);
    if (!ok) { CloseHandle(job); return false; }
    readerJob = job;
#endif
    return true;
}
void Window::stopReader() {
    pending.clear();
#ifdef Q_OS_WIN
    if (readerJob) TerminateJobObject(static_cast<HANDLE>(readerJob), 1);
#else
    if (process.processId() > 0) ::kill(-pid_t(process.processId()), SIGKILL);
#endif
    process.kill();
}
void Window::sendPending() {
    if (!pending.isEmpty()) { process.write(pending); pending.clear(); }
}
void Window::resetImage() {
    session.clear(); image.clear(); directoryEntries = {}; imageNote.clear();
    { QSignalBlocker block(userAreas); userAreas->setCurrentItem(nullptr); userAreas->clearSelection(); }
    table->setRowCount(0); updateUserAreas();
    source->setText("No image open"); setBusy(busy);
}
void Window::setBusy(bool value) {
    busy = value;
    openButton->setEnabled(!busy);
    formats->setEnabled(!busy);
    table->setEnabled(!busy && !session.isEmpty());
    userAreas->setEnabled(!busy && !session.isEmpty());
    const int selected = table->selectionModel()->selectedRows().size();
    exportButton->setEnabled(!busy && !session.isEmpty() && selected == 1);
    exportButton->setToolTip(selected > 1 ? "Batch export will be added in the next implementation step." :
        "Export selected (Ctrl+E / Command+E)");
    cancelButton->setEnabled(busy);
}
void Window::chooseImage() {
    const QString path = QFileDialog::getOpenFileName(this, "Open CP/M image", image.isEmpty() ? QDir::homePath() : image,
        "Disk images (*.raw *.img *.dsk);;All files (*)");
    if (path.isEmpty()) return;
    const auto choice = formats->currentData().toStringList();
    openImage(path, choice[0], choice[1]);
}
void Window::openImage(const QString &path, const QString &fmt, const QString &type) {
    if (busy) return;
    for (int index = 0; index < formats->count(); ++index) {
        if (formats->itemData(index).toStringList() == QStringList{fmt,type}) {
            QSignalBlocker block(formats); formats->setCurrentIndex(index); break;
        }
    }
    resetImage(); image = QFileInfo(path).absoluteFilePath(); format = fmt; container = type;
    source->setText(image);
    begin({{"op", "open"}, {"image", image}, {"format", fmt}, {"container", type}});
}
int Window::entryCount() const { return table->rowCount(); }
void Window::selectEntry(int row) { table->selectRow(row); }
void Window::updateUserAreas() {
    for (int area = 0; area < 16; ++area) {
        int count = 0;
        for (const auto &value : directoryEntries)
            if (value.toObject()["name"].toString().left(2).toInt() == area) ++count;
        auto *item = userAreas->item(area);
        item->setData(Qt::UserRole + 1, count);
        item->setToolTip(QString("User area %1 · %2 files").arg(area).arg(count));
        auto folder = style()->standardIcon(QStyle::SP_DirIcon).pixmap(24, 24);
        if (count) {
            QPainter painter(&folder);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(palette().color(QPalette::Text), 2));
            painter.drawLine(8, 14, 16, 14);
            painter.drawLine(12, 10, 12, 18);
        }
        item->setIcon(QIcon(folder));
        item->setData(Qt::AccessibleTextRole,
            QString("User area %1, %2 files").arg(area).arg(count));
    }
}
void Window::showUserArea() {
    const QSignalBlocker block(table);
    const auto *area = userAreas->currentItem();
    const int currentArea = area ? area->data(Qt::UserRole).toInt() : -1;
    table->setSortingEnabled(false);
    table->clearSelection(); table->setCurrentItem(nullptr); table->setRowCount(0);
    for (const auto &value : directoryEntries) {
        const auto entry = value.toObject();
        const auto name = entry["name"].toString();
        if (name.left(2).toInt() != currentArea) continue;
        const int row = table->rowCount(); table->insertRow(row);
        auto *file = new QTableWidgetItem(name.mid(2).toUpper());
        file->setData(Qt::UserRole, name); table->setItem(row, 0, file);
        auto *size = new QTableWidgetItem;
        size->setData(Qt::DisplayRole, entry["size"].toInteger());
        table->setItem(row, 1, size);
        const int bits = entry["attr"].toInt(); QStringList flags;
        for (const auto &flag : QList<QPair<int,QString>>{{1,"F1"},{2,"F2"},{4,"F3"},{8,"F4"},{256,"Read-only"},{512,"System"},{1024,"Archive"}})
            if (bits & flag.first) flags << flag.second;
        table->setItem(row, 2, new QTableWidgetItem(flags.isEmpty() ? "—" : flags.join(", ")));
    }
    table->setSortingEnabled(true);
    if (table->rowCount()) table->selectRow(0);
    setBusy(busy); updateSelectionStatus();
}
void Window::updateSelectionStatus() {
    if (busy || session.isEmpty() || !userAreas->currentItem()) return;
    const auto rows = table->selectionModel()->selectedRows();
    qint64 bytes = 0;
    for (const auto &row : rows) bytes += table->item(row.row(), 1)->data(Qt::DisplayRole).toLongLong();
    status->setText(QString("User area %1 · %2 of %3 files selected · %4 bytes%5")
        .arg(userAreas->currentItem()->data(Qt::UserRole).toInt())
        .arg(rows.size()).arg(table->rowCount()).arg(bytes).arg(imageNote));
}
void Window::chooseExport() {
    const auto selected = table->selectionModel()->selectedRows();
    if (busy || session.isEmpty() || selected.size() != 1) return;
    const int row = selected.first().row();
    const auto name = table->item(row, 0)->data(Qt::UserRole).toString();
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!QDir(directory).exists()) directory = QDir::homePath();
    // CP/M '/' is part of the image name; never suggest a host subdirectory.
    QString hostName = name.mid(2); hostName.replace('/', '_');
    const QString suggested = QDir(directory).filePath(QString("user%1-%2").arg(name.left(2), hostName));
    const auto destination = QFileDialog::getSaveFileName(this, "Export file", suggested,
        "All files (*)", nullptr, QFileDialog::DontConfirmOverwrite);
    if (!destination.isEmpty()) exportFile(destination);
}
void Window::exportFile(const QString &destination) {
    const auto selected = table->selectionModel()->selectedRows();
    if (busy || session.isEmpty() || selected.size() != 1) return;
    const int row = selected.first().row();
    begin({{"op", "export"}, {"session", session},
           {"name", table->item(row, 0)->data(Qt::UserRole).toString()},
           {"destination", QFileInfo(destination).absoluteFilePath()}});
}
void Window::begin(QJsonObject request) {
    error.clear(); output.clear();
    operation = request["op"].toString();
    requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    request["v"] = 1; request["id"] = requestId;
    if (process.state() == QProcess::Starting || cancellationDeadline.isActive()) {
        fail("The image reader is restarting. Open the image again."); return;
    }
    setBusy(true);
    status->setText(operation == "open" ? "Reading image…" : "Exporting selected file…");
    deadline.start(deadlineMs);
    pending = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (process.state() == QProcess::NotRunning) {
        session.clear();
        process.setProgram(python);
        process.setArguments({"-I", "-B", "-u", root + "/prototype/broker.py", "--root", root, "--helper", helper});
        process.start();
    } else sendPending();
}
void Window::cancel() {
    if (!busy) return;
    if (!cancellationDeadline.isActive()) cancellationDeadline.start(graceMs);
    const QJsonObject request{{"v",1}, {"id","cancel"}, {"op","cancel"}, {"target",requestId}};
    process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    cancelButton->setEnabled(false);
    status->setText("Cancelling…");
}
void Window::fail(const QString &message) {
    deadline.stop(); cancellationDeadline.stop(); error = message; setBusy(false);
    if (operation == "open") resetImage();
    status->setText(message); emit completed(false);
    if (closing) close();
}
void Window::readReply() {
    // A killed reader can still deliver queued output after its request failed.
    if (!busy) { process.readAllStandardOutput(); return; }
    output += process.readAllStandardOutput();
    if (output.size() > 262144) {
        stopReader(); fail("The image reader exceeded its response limit."); return;
    }
    if (!output.endsWith('\n')) return;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(output.trimmed(), &parseError);
    output.clear();
    const auto reply = document.object();
    if (!busy || parseError.error != QJsonParseError::NoError || !document.isObject() ||
        reply["v"].toInt() != 1 || reply["id"].toString() != requestId || !reply["ok"].isBool()) {
        stopReader(); fail("Invalid response from the image reader."); return;
    }
    if (!reply["ok"].toBool()) {
        const auto code = reply["error"].toString();
        QString message;
        if (code == "destination_exists") message = "That destination already exists. Choose a new filename; nothing was overwritten.";
        else if (code == "cancelled") message = "Operation cancelled.";
        else if (code == "source_changed") { session.clear(); message = "The source image changed. Open it again before exporting."; }
        else message = "Cannot complete the operation (" + code + "). " + reply["message"].toString();
        fail(message); return;
    }
    const auto result = reply["result"].toObject();
    const QRegularExpression hash("^[0-9a-f]{64}$");
    auto invalid = [this] { stopReader(); resetImage(); fail("Invalid response schema from the image reader."); };
    if (!reply["result"].isObject()) { invalid(); return; }
    if (operation == "open") {
        if (!result["entries"].isArray() || result["session"].toString().isEmpty()) {
            stopReader(); fail("Incomplete directory response."); return;
        }
        const auto rows = result["entries"].toArray();
        if (rows.size() > 4096 || result["session"].toString().size() > 128 ||
            !hash.match(result["source_sha256"].toString()).hasMatch()) { invalid(); return; }
        QSet<QString> names;
        const QRegularExpression filename("^(0[0-9]|1[0-5])[a-z0-9!#$%&'()@^_`{}~+/-]{1,8}(\\.[a-z0-9!#$%&'()@^_`{}~+/-]{1,3})?$");
        for (const auto &row : rows) {
            const auto e = row.toObject(); const auto n = e["name"].toString();
            const double size = e["size"].toDouble(-1), attr = e["attr"].toDouble(-1);
            if (!row.isObject() || !filename.match(n).hasMatch() || names.contains(n) ||
                size < 0 || size > 16777216 || std::floor(size) != size ||
                attr < 0 || attr > 2047 || std::floor(attr) != attr || (int(attr) & ~0x70f)) {
                invalid(); return;
            }
            names.insert(n);
        }
        session = result["session"].toString();
        directoryEntries = rows;
        const auto tail = result["unused_tail_filled"];
        if (!tail.isUndefined() && !tail.isBool()) { invalid(); return; }
        imageNote = tail.toBool() ? " · Unused tail filled in temporary copy; original unchanged." : "";
        source->setText(image + "\nSnapshot SHA-256: " + result["source_sha256"].toString());
        updateUserAreas();
        { QSignalBlocker block(userAreas); userAreas->setCurrentItem(userAreas->item(0)); }
        showUserArea();
    } else {
        const double size = result["size"].toDouble(-1);
        if (size < 0 || size > 16777216 || std::floor(size) != size ||
            !hash.match(result["sha256"].toString()).hasMatch()) { invalid(); return; }
        status->setText(QString("Exported %1 bytes. SHA-256: %2")
            .arg(result["size"].toInteger()).arg(result["sha256"].toString()));
    }
    deadline.stop(); cancellationDeadline.stop(); setBusy(false);
    if (operation == "open") updateSelectionStatus();
    emit completed(true);
    if (closing) close();
}
void Window::closeEvent(QCloseEvent *event) {
    if (busy) { closing = true; cancel(); event->ignore(); }
    else { process.closeWriteChannel(); event->accept(); }
}
