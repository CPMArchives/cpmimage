// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include <QCryptographicHash>
#include <algorithm>
#include <utility>
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
#include <QSettings>
#include <QLineEdit>
#include <QInputDialog>
#include <QCheckBox>
#include <QProgressBar>
#include <QMessageBox>
#include <QTextEdit>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#endif

Window::Window(QString root, QString python, QString helper, int deadlineMs, int graceMs, QString noticesPath, QString settingsFile)
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
    settings = settingsFile.isEmpty() ? new QSettings("CPMArchives", "cpmimage", this) :
        new QSettings(settingsFile, QSettings::IniFormat, this);
    const bool validSettings = filenamePolicy.load(*settings);
    auto *preferences = menuBar()->addMenu("Settings");
    auto *filenameSettings = preferences->addAction("Filename translations…");
    filenameSettings->setObjectName("filenameSettingsAction");
    filenameSettings->setShortcut(QKeySequence::Preferences);
    addAction(filenameSettings);
    connect(filenameSettings, &QAction::triggered, this, &Window::editFilenameSettings);
    auto *formatFavorites = preferences->addAction("Format favorites…");
    connect(formatFavorites, &QAction::triggered, this, [this] {
        QStringList entries = settings->value("formatFavorites").toStringList();
        entries.sort(Qt::CaseInsensitive);
        for (auto &entry : entries) {
            const bool available = std::any_of(catalogueRows.begin(), catalogueRows.end(), [&](const auto &row) {
                return row.toObject()["id"].toString() == entry;
            });
            if (!available) entry += " (unavailable in this catalogue)";
        }
        QMessageBox::information(this, "Format favorites",
            entries.isEmpty() ? QString("No favorites saved.") : entries.join('\n'));
    });
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
    resize(580, 580);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *toolbar = new QHBoxLayout;
    formats = new QPushButton("Choose format…");
    formats->setObjectName("chooseFormat");

    openButton = new QPushButton("Open image…");
    exportButton = new QPushButton("Export selected…");
    areaExportButton = new QPushButton("Export user area…");
    areaExportButton->setObjectName("exportUserArea");
    cancelButton = new QPushButton("Cancel");
    cancelButton->setObjectName("cancelOperation");
    openButton->setShortcut(QKeySequence::Open);
    exportButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    openButton->setToolTip("Open image (Ctrl+O / Command+O)");
    exportButton->setToolTip("Export selected (Ctrl+E / Command+E)");
    toolbar->addWidget(openButton);
    toolbar->addWidget(exportButton);
    toolbar->addWidget(areaExportButton);
    toolbar->addStretch();
    auto *readOnly = new QLabel("Read-only");
    readOnly->setToolTip("Browse and export. The source image is never edited.");
    toolbar->addWidget(readOnly);
    layout->addLayout(toolbar);
    auto *imageRow=new QHBoxLayout;
    imageRow->addWidget(new QLabel("Image layout:"));
    imageRow->addWidget(formats,1);
    imageInfoButton=new QPushButton("Image information…");
    imageInfoButton->setObjectName("imageInformation");
    imageRow->addWidget(imageInfoButton);
    layout->addLayout(imageRow);
    source = new QLabel("No image open");
    source->setTextFormat(Qt::PlainText);
    source->setObjectName("imageName");
    source->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    source->setMinimumWidth(0);
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
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->setColumnWidth(0, 170);
    table->verticalHeader()->hide();
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(true);
    table->sortItems(0, Qt::AscendingOrder);
    splitter->addWidget(table);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({90, 470});
    layout->addWidget(splitter, 1);
    auto *selectAll = new QAction("Select all", table);
    selectAll->setShortcut(QKeySequence::SelectAll);
    selectAll->setShortcutContext(Qt::WidgetShortcut);
    table->addAction(selectAll);
    connect(selectAll, &QAction::triggered, table, &QTableWidget::selectAll);

    status = new QLabel("Open an image to choose its disk definition.");
    status->setObjectName("selectionStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *statusRow=new QHBoxLayout;
    statusRow->addWidget(status,1);
    statusRow->addWidget(cancelButton);
    progress=new QProgressBar; progress->setObjectName("exportProgress"); progress->hide();
    layout->addWidget(progress);
    reportButton=new QPushButton("Export report…"); reportButton->setObjectName("exportReport"); reportButton->setEnabled(false);
    reportButton->hide();
    statusRow->addWidget(reportButton);
    layout->addLayout(statusRow);
    connect(reportButton,&QPushButton::clicked,this,&Window::showExportReport);
    connect(areaExportButton,&QPushButton::clicked,this,[this]{chooseBatch(true);});
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table,&QTableWidget::customContextMenuRequested,this,[this](const QPoint &point){
        if(busy || session.isEmpty()) return;
        const auto *item=table->itemAt(point);
        if(item && !item->isSelected()) table->selectRow(item->row());
        QMenu menu; auto *action=menu.addAction("Export selected…");
        action->setEnabled(!table->selectionModel()->selectedRows().isEmpty());
        if(menu.exec(table->viewport()->mapToGlobal(point))==action) chooseExport();
    });
    userAreas->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(userAreas,&QListWidget::customContextMenuRequested,this,[this](const QPoint &point){
        if(busy || session.isEmpty()) return;
        auto *item=userAreas->itemAt(point); if(!item) return;
        userAreas->setCurrentItem(item);
        QMenu menu; auto *action=menu.addAction("Export user area…");
        if(menu.exec(userAreas->viewport()->mapToGlobal(point))==action) chooseBatch(true);
    });
    auto *fileMenu=new QMenu("File",this);
    menuBar()->insertMenu(preferences->menuAction(),fileMenu);
    auto *openAction=fileMenu->addAction("Open image…");
    connect(openAction,&QAction::triggered,openButton,&QPushButton::click);
    auto *saveAction = fileMenu->addAction("Save");
    saveAction->setShortcut(QKeySequence::Save);
    auto *saveAsAction = fileMenu->addAction("Save As…");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAction, &QAction::triggered, this, [this] { chooseSave(false); });
    connect(saveAsAction, &QAction::triggered, this, [this] { chooseSave(true); });
    connect(fileMenu, &QMenu::aboutToShow, this, [this, saveAction, saveAsAction] {
        saveAction->setEnabled(!busy && !session.isEmpty());
        saveAsAction->setEnabled(!busy && !session.isEmpty());
    });
    connect(fileMenu->addAction("Discard unsaved changes…"), &QAction::triggered, this, [this] {
        if (!busy && dirty && QMessageBox::question(this, "Discard edits", "Discard all unsaved changes?") == QMessageBox::Yes)
            begin({{"op","discard"}, {"session",session}});
    });
    auto *editMenu = menuBar()->addMenu("Edit");
    auto *rename = editMenu->addAction("Rename…");
    rename->setShortcut(Qt::Key_F2);
    table->addAction(rename);
    connect(rename, &QAction::triggered, this, [this] { chooseEdit("rename"); });
    for (const auto &item : QList<QPair<QString,QString>>{{"Import file…","import"},
            {"Delete…","delete"},{"Move to user area…","move"},{"Properties…","attrs"},
            {"Export system tracks…","systems_export"},{"Replace system tracks…","systems_import"}}) {
        connect(editMenu->addAction(item.first), &QAction::triggered, this, [this, item] { chooseEdit(item.second); });
    }
    auto *exportAction=fileMenu->addAction("Export selected…");
    connect(exportAction,&QAction::triggered,exportButton,&QPushButton::click);
    auto *areaAction=fileMenu->addAction("Export user area…");
    connect(areaAction,&QAction::triggered,areaExportButton,&QPushButton::click);
    fileMenu->addSeparator();
    imageInfoAction=fileMenu->addAction("Image information…");
    imageInfoAction->setObjectName("imageInformationAction");
    connect(imageInfoAction,&QAction::triggered,this,&Window::showImageInformation);
    connect(imageInfoButton,&QPushButton::clicked,this,&Window::showImageInformation);
    connect(fileMenu,&QMenu::aboutToShow,this,[=]{openAction->setEnabled(openButton->isEnabled());exportAction->setEnabled(exportButton->isEnabled());areaAction->setEnabled(areaExportButton->isEnabled());});
    setCentralWidget(central);
    connect(openButton, &QPushButton::clicked, this, &Window::chooseImage);
    connect(exportButton, &QPushButton::clicked, this, &Window::chooseExport);
    connect(cancelButton, &QPushButton::clicked, this, &Window::cancel);
    connect(table, &QTableWidget::itemSelectionChanged, this, [this] {
        setBusy(busy); updateSelectionStatus();
    });
    connect(userAreas, &QListWidget::currentItemChanged, this, [this] { showUserArea(); });
    externalCatalogueAction = fileMenu->addAction("Load diskdefs catalogue…");
    bundledCatalogueAction = fileMenu->addAction("Use bundled diskdefs");
    connect(externalCatalogueAction, &QAction::triggered, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Load diskdefs catalogue", cataloguePath);
        if (!path.isEmpty()) loadCatalogue(path);
    });
    connect(bundledCatalogueAction, &QAction::triggered, this, [this] { loadCatalogue(); });
    connect(formats, &QPushButton::clicked, this, [this] {
        inspectedHash = snapshotHash;
        inspectedContainer = container;
        chooseFormat({}, true);
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
    if (!validSettings) status->setText("Saved filename mappings are invalid. Defaults are in use; review Settings → Filename translations.");
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
    dirty = false; writeConfirmed = false;
    session.clear(); image.clear(); directoryEntries = {}; imageNote.clear();
    snapshotHash.clear(); exportDiagnostics.clear();
    { QSignalBlocker block(userAreas); userAreas->setCurrentItem(nullptr); userAreas->clearSelection(); }
    // Selection callbacks can enter widget/accessibility code while rows are being
    // removed. Clear selection before deleting items and notify only after reset.
    {
        const QSignalBlocker block(table);
        table->clearSelection();
        table->setCurrentItem(nullptr);
        table->setSortingEnabled(false);
        table->setRowCount(0);
        table->setSortingEnabled(true);
    }
    updateUserAreas();
    source->setText("No image open"); source->setToolTip({});
    formats->setText("Choose format…");
    setWindowTitle("cpmimage — Review prototype"); setBusy(busy);
}
void Window::setBusy(bool value) {
    busy = value;
    openButton->setEnabled(!busy);
    formats->setEnabled(!busy && !image.isEmpty() && !catalogueRows.isEmpty());
    externalCatalogueAction->setEnabled(!busy);
    bundledCatalogueAction->setEnabled(!busy);
    table->setEnabled(!busy && !session.isEmpty());
    userAreas->setEnabled(!busy && !session.isEmpty());
    const int selected = table->selectionModel()->selectedRows().size();
    exportButton->setEnabled(!busy && !session.isEmpty() && selected > 0);
    exportButton->setToolTip("Export selected (Ctrl+E / Command+E)");
    areaExportButton->setEnabled(!busy && !session.isEmpty() && userAreas->currentItem());
    cancelButton->setEnabled(busy);
    cancelButton->setVisible(busy);
    imageInfoButton->setEnabled(!busy && !session.isEmpty());
    imageInfoAction->setEnabled(!busy && !session.isEmpty());
    progress->setVisible(busy && batchActive);
}
bool Window::confirmWrite() {
    return writeConfirmed || QMessageBox::question(this, "Confirm format for writing",
        QString("Edit the private working image using %1? Confirm that this is the intended interpretation.").arg(format),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
}
void Window::chooseEdit(const QString &action) {
    if (busy || session.isEmpty()) return;
    QJsonObject request{{"op","edit"},{"session",session},{"action",action}};
    if (action.startsWith("systems_")) {
        const bool importing = action == "systems_import";
        const auto path = importing ? QFileDialog::getOpenFileName(this, "Select system-track binary")
                                    : QFileDialog::getSaveFileName(this, "Export system tracks");
        if (path.isEmpty()) return;
        request["op"] = action;
        request["path"] = path;
        if (importing) {
            if (QMessageBox::question(this, "Replace system tracks",
                "Replace the reserved region? Short input will be padded with E5.",
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes || !confirmWrite()) return;
            request["pad"] = true;
            request["confirmed"] = true;
        }
        begin(request);
        return;
    }
    bool accepted = false;
    if (action == "import") {
        const auto path = QFileDialog::getOpenFileName(this, "Import file");
        if (path.isEmpty() || !userAreas->currentItem()) return;
        const auto name = QInputDialog::getText(this, "Import file", "CP/M filename", QLineEdit::Normal,
            QFileInfo(path).fileName().toUpper(), &accepted).toUpper();
        if (!accepted) return;
        const int area = userAreas->currentItem()->data(Qt::UserRole).toInt();
        const auto key = QString("%1").arg(area, 2, 10, QLatin1Char('0')) + name;
        for (const auto &entry : directoryEntries) {
            if (entry.toObject()["name"].toString() == key) {
                if (QMessageBox::question(this, "Import conflict", "Replace the existing file?",
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No) != QMessageBox::Yes) return;
                request["replace"] = true;
                break;
            }
        }
        request["name"] = key;
        request["path"] = path;
    } else {
        const auto rows = table->selectionModel()->selectedRows();
        if (rows.size() != 1) return;
        const auto key = table->item(rows.first().row(),0)->data(Qt::UserRole).toString();
        request["name"] = key;
        if (action == "rename") {
            auto name = QInputDialog::getText(this, "Rename", "CP/M filename", QLineEdit::Normal, key.mid(2), &accepted);
            if (!accepted || name.toUpper() == key.mid(2)) return;
            request["target"] = key.left(2) + name.toUpper();
        } else if (action == "move") {
            const auto area = QInputDialog::getInt(this, "Move file", "User area", key.left(2).toInt(), 0, 31, 1, &accepted);
            if (!accepted || area == key.left(2).toInt()) return;
            request["target"] = QString("%1").arg(area, 2, 10, QLatin1Char('0')) + key.mid(2);
        } else if (action == "attrs") {
            int attributes = 0;
            for (const auto &entry : directoryEntries)
                if (entry.toObject()["name"].toString() == key) attributes = entry.toObject()["attr"].toInt();
            QDialog dialog(this);
            dialog.setWindowTitle("Properties — " + key.mid(2));
            QVBoxLayout layout(&dialog);
            QCheckBox readOnly("Read-only"), system("System"), archive("Archive");
            readOnly.setChecked(attributes & 256);
            system.setChecked(attributes & 512);
            archive.setChecked(attributes & 1024);
            for (auto *check : {&readOnly,&system,&archive}) layout.addWidget(check);
            QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            layout.addWidget(&buttons);
            connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
            connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
            if (dialog.exec() != QDialog::Accepted) return;
            request["attributes"] = (readOnly.isChecked()?256:0) | (system.isChecked()?512:0) | (archive.isChecked()?1024:0);
        } else if (QMessageBox::question(this, "Delete file", "Delete " + key.mid(2) + " from the working image?",
                   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    }
    if (!confirmWrite()) return;
    request["confirmed"] = true;
    begin(request);
}
void Window::chooseSave(bool saveAs) {
    if (busy || session.isEmpty()) return;
    QString destination;
    if (saveAs) {
        destination = QFileDialog::getSaveFileName(this, "Save CP/M image as", image,
            "RAW disk images (*.raw *.img *.dsk);;All files (*)", nullptr, QFileDialog::DontConfirmOverwrite);
        if (destination.isEmpty()) return;
    }
    pendingSave = {{"op","save_plan"}, {"session",session}, {"destination",destination},
                   {"backups",settings->value("saveBackups",true).toBool()},
                   {"sidecar",settings->value("saveSidecar",true).toBool()}};
    begin(pendingSave);
}
bool Window::resolveEdits(std::function<void()> next) {
    if (!dirty) return true;
    const auto answer = QMessageBox::warning(this, "Unsaved changes", "Save changes before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Save) {
        afterSave = std::move(next);
        chooseSave(false);
    } else if (answer == QMessageBox::Discard) {
        afterSave = std::move(next);
        begin({{"op","discard"}, {"session",session}});
    }
    return false;
}
void Window::chooseImage() {
    if (!resolveEdits([this] { chooseImage(); })) return;
    const QString path = QFileDialog::getOpenFileName(this, "Open CP/M image", image.isEmpty() ? QDir::homePath() : image,
        "Disk images (*.raw *.img *.dsk);;All files (*)");
    if (path.isEmpty()) return;
    inspectImage(path);
}
void Window::loadCatalogue(const QString &path) {
    if (busy) return;
    begin({{"op", "catalogue"}, {"path", path}});
}
void Window::inspectImage(const QString &path, const QString &explicitFormat, const QString &type) {
    if (busy) return;
    requestedImage = QFileInfo(path).absoluteFilePath();
    requestedFormat = explicitFormat;
    requestedContainer = type;
    if (catalogueRows.isEmpty() || process.state() == QProcess::NotRunning) {
        loadCatalogue(cataloguePath);
        return;
    }
    begin({{"op", "inspect"}, {"image", requestedImage}, {"explicit_format", requestedFormat}});
}
void Window::openImage(const QString &path, const QString &fmt, const QString &type) {
    if (busy) return;
    requestedImage.clear();
    retainSource = false;
    resetImage();
    image = QFileInfo(path).absoluteFilePath();
    format = fmt;
    container = type;
    formats->setText(fmt);
    source->setText(QFileInfo(image).fileName()); source->setToolTip(image);
    begin({{"op", "open"}, {"image", image}, {"format", fmt}, {"container", type}});
}
static QString associationKey(const QString &image, const QString &catalogue) {
    return "imageFormats/" + QString::fromLatin1(QCryptographicHash::hash(
        (QDir::cleanPath(image) + '\n' + catalogue).toUtf8(), QCryptographicHash::Sha256).toHex());
}
void Window::openDefinition(const QJsonObject &definition, bool sidecar, bool switching) {
    const QString path = switching ? image : requestedImage;
    requestedImage.clear();
    const auto fingerprint = inspectedHash;
    resetImage();
    image = path;
    format = definition["id"].toString();
    container = inspectedContainer;
    snapshotHash = fingerprint;
    formats->setText(format);
    source->setText(QFileInfo(image).fileName()); source->setToolTip(image);
    retainSource = switching;
    QJsonObject request{{"op","open"}, {"image",image}, {"format",format}, {"container",container},
                        {"sidecar",sidecar}};
    if (!fingerprint.isEmpty()) request["source_sha256"] = fingerprint;
    if (definition.contains("definition_sha256")) request["definition_sha256"] = definition["definition_sha256"];
    begin(request);
}
void Window::chooseFormat(const QJsonObject &inspection, bool switching) {
    if (!resolveEdits([this] { formats->click(); })) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Choose disk format");
    dialog.resize(650, 480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search disk definitions");
    layout->addWidget(search);
    auto *group = new QComboBox;
    group->addItems({"Plausible candidates", "All formats", "Manufacturer and model", "System family", "Favorites"});
    layout->addWidget(group);
    auto *list = new QListWidget;
    layout->addWidget(list);
    auto *favorite = new QPushButton("Toggle favorite");
    layout->addWidget(favorite);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    auto updateButtons = [&] {
        const bool enabled = list->currentItem();
        buttons->button(QDialogButtonBox::Open)->setEnabled(enabled);
        favorite->setEnabled(enabled);
    };
    connect(list, &QListWidget::currentItemChanged, &dialog, updateButtons);
    const auto candidates = inspection["candidates"].toArray();
    const QString favoritesKey = "formatFavorites";
    auto favorites = settings->value(favoritesKey).toStringList();
    auto populate = [&] {
        const auto selected = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toJsonObject()["id"].toString()
                                                  : (switching ? format : QString());
        list->clear();
        const int mode = group->currentIndex();
        const auto rows = mode == 0 ? candidates : catalogueRows;
        for (const auto &value : rows) {
            const auto row = value.toObject();
            const auto id = row["id"].toString();
            if (mode == 4 && !favorites.contains(id)) continue;
            const auto maker = row["manufacturer"].toString().isEmpty() ? QString("Unclassified") : row["manufacturer"].toString();
            const auto model = row["model"].toString();
            const auto family = row["family"].toString().isEmpty() ? QString("Unclassified") : row["family"].toString();
            if (!(id + " " + maker + " " + model + " " + family).contains(search->text(), Qt::CaseInsensitive)) continue;
            QString label = id;
            if (mode == 2) label = maker + " / " + model + " — " + id;
            if (mode == 3) label = family + " — " + id;
            if (mode == 0 && row.contains("evidence")) label += " — " + row["evidence"].toString();
            auto *item = new QListWidgetItem(label, list);
            item->setData(Qt::UserRole, row);
            if (favorites.contains(id)) item->setToolTip("Favorite");
            if (id == selected) list->setCurrentItem(item);
        }
        if (mode != 0) list->sortItems();
        if (!list->currentItem() && list->count()) list->setCurrentRow(0);
        updateButtons();
    };
    group->setCurrentIndex(candidates.isEmpty() ? 1 : 0);
    connect(group, &QComboBox::currentIndexChanged, &dialog, populate);
    connect(search, &QLineEdit::textChanged, &dialog, populate);
    connect(favorite, &QPushButton::clicked, &dialog, [&] {
        auto id = list->currentItem()->data(Qt::UserRole).toJsonObject()["id"].toString();
        if (!favorites.removeAll(id)) favorites.append(id);
        settings->setValue(favoritesKey, favorites);
        populate();
    });
    populate();
    if (dialog.exec() == QDialog::Accepted && list->currentItem()) {
        openDefinition(list->currentItem()->data(Qt::UserRole).toJsonObject(), false, switching);
    } else {
        requestedImage.clear();
        status->setText("Format selection cancelled.");
        emit completed(false);
    }
}
int Window::entryCount() const { return table->rowCount(); }
void Window::selectEntry(int row) { table->selectRow(row); }
void Window::updateUserAreas() {
    int areas = 16;
    for (const auto &value : directoryEntries)
        areas = qMax(areas, value.toObject()["name"].toString().left(2).toInt() + 1);
    while (userAreas->count() > areas) delete userAreas->takeItem(userAreas->count() - 1);
    while (userAreas->count() < areas) {
        const int area = userAreas->count();
        auto *item = new QListWidgetItem(QString::number(area), userAreas);
        item->setData(Qt::UserRole, area);
    }
    for (int area = 0; area < areas; ++area) {
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
    status->setText(QString("User area %1 · %2 of %3 files selected · %4 bytes")
        .arg(userAreas->currentItem()->data(Qt::UserRole).toInt())
        .arg(rows.size()).arg(table->rowCount()).arg(bytes));
}
QString Window::selectedExportName(QString *problem) const {
    const auto selected = table->selectionModel()->selectedRows();
    if (selected.size()!=1) return {};
    const auto name = table->item(selected.first().row(),0)->text();
    QStringList names;
    for (int row=0;row<table->rowCount();++row) names<<table->item(row,0)->text();
    *problem=filenamePolicy.problem(name,names);
    return filenamePolicy.translate(name);
}
void Window::editFilenameSettings() {
    if(busy) return;
    QDialog dialog(this); dialog.setWindowTitle("Filename translations"); dialog.resize(610,440);
    auto *layout=new QVBoxLayout(&dialog);
    auto *description=new QLabel("Translate characters when exporting. The same mappings apply on every OS. Original CP/M names stay uppercase. Import reversal is not enabled.");
    description->setWordWrap(true); layout->addWidget(description);
    auto *mappings=new QTableWidget(0,2); mappings->setObjectName("filenameMappings");
    mappings->setHorizontalHeaderLabels({"CP/M character","Exported character"});
    mappings->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(mappings);
    auto fill=[&](const QList<FilenameMapping> &values) {
        const QSignalBlocker blocker(mappings); mappings->setRowCount(values.size());
        for(int i=0;i<values.size();++i) {
            mappings->setItem(i,0,new QTableWidgetItem(values[i].source));
            mappings->setItem(i,1,new QTableWidgetItem(values[i].replacement));
        }
    };
    fill(filenamePolicy.mappings);
    auto *controls=new QHBoxLayout;
    auto *add=new QPushButton("Add"); auto *remove=new QPushButton("Remove"); auto *defaults=new QPushButton("Restore defaults");
    controls->addWidget(add);controls->addWidget(remove);controls->addWidget(defaults);layout->addLayout(controls);
    auto *example=new QLineEdit("SIG/M.LIB");example->setObjectName("filenameExample");
    auto *preview=new QLabel; preview->setObjectName("filenamePreview");preview->setWordWrap(true);
    layout->addWidget(new QLabel("Example CP/M filename:"));layout->addWidget(example);layout->addWidget(preview);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);layout->addWidget(buttons);
    auto read=[&] {
        QList<FilenameMapping> values;
        for(int row=0;row<mappings->rowCount();++row)
            values.append({mappings->item(row,0)?mappings->item(row,0)->text():QString(),
                           mappings->item(row,1)?mappings->item(row,1)->text():QString()});
        return values;
    };
    auto refresh=[&] {
        FilenamePolicy candidate;candidate.mappings=read();
        const auto error=FilenamePolicy::validateMappings(candidate.mappings);
        buttons->button(QDialogButtonBox::Save)->setEnabled(error.isEmpty());
        const auto translated=candidate.translate(example->text());
        const auto invalid=FilenamePolicy::validateHostName(translated);
        preview->setText(!error.isEmpty()?error:example->text()+" → "+translated+
            (invalid.isEmpty()?QString():"\n"+invalid));
    };
    connect(mappings,&QTableWidget::itemChanged,&dialog,[&]{refresh();});
    connect(example,&QLineEdit::textChanged,&dialog,[&]{refresh();});
    connect(add,&QPushButton::clicked,&dialog,[&]{mappings->insertRow(mappings->rowCount());refresh();});
    connect(remove,&QPushButton::clicked,&dialog,[&]{if(mappings->currentRow()>=0)mappings->removeRow(mappings->currentRow());refresh();});
    connect(defaults,&QPushButton::clicked,&dialog,[&]{fill(FilenamePolicy::defaults());refresh();});
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        FilenamePolicy candidate;candidate.mappings=read();
        if(!candidate.save(*settings)){preview->setText("Could not save filename settings. Check the settings location and try again.");return;}
        filenamePolicy=candidate;dialog.accept();
    });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    refresh();dialog.exec();
}
void Window::chooseExport() {
    const auto selected = table->selectionModel()->selectedRows();
    if (busy || session.isEmpty() || selected.isEmpty()) return;
    if(selected.size()>1) { chooseBatch(false); return; }
    QString directory = settings->value("exports/directory",QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
    if (!QDir(directory).exists()) directory = QDir::homePath();
    QString problem;
    const QString hostName = selectedExportName(&problem);
    if (!problem.isEmpty()) { status->setText(problem); return; }
    const QString suggested = QDir(directory).filePath(hostName);
    const auto destination = QFileDialog::getSaveFileName(this, "Export file", suggested,
        "All files (*)", nullptr, QFileDialog::DontConfirmOverwrite);
    if (!destination.isEmpty()) {
        settings->setValue("exports/directory",QFileInfo(destination).absolutePath());
        exportFile(destination);
    }
}
void Window::exportFile(const QString &destination) {
    const auto selected = table->selectionModel()->selectedRows();
    if (busy || session.isEmpty() || selected.size() != 1) return;
    const int row = selected.first().row();
    const auto problem = FilenamePolicy::validateHostName(QFileInfo(destination).fileName());
    if (!problem.isEmpty()) { status->setText(problem); return; }
    begin({{"op", "export"}, {"session", session},
           {"name", table->item(row, 0)->data(Qt::UserRole).toString()},
           {"destination", QFileInfo(destination).absoluteFilePath()}});
}
QJsonArray Window::exportEntries(bool wholeArea) const {
    QJsonArray entries;
    if(wholeArea) {
        if(!userAreas->currentItem()) return entries;
        const int area=userAreas->currentItem()->data(Qt::UserRole).toInt();
        for(const auto &value:directoryEntries)
            if(value.toObject()["name"].toString().left(2).toInt()==area) entries.append(value);
    } else {
        for(const auto &row:table->selectionModel()->selectedRows()) {
            const auto name=table->item(row.row(),0)->data(Qt::UserRole).toString();
            for(const auto &value:directoryEntries)
                if(value.toObject()["name"].toString()==name) { entries.append(value); break; }
        }
    }
    return entries;
}
void Window::chooseBatch(bool wholeArea) {
    if(busy || session.isEmpty() || !userAreas->currentItem()) return;
    const auto entries=exportEntries(wholeArea);
    if(!wholeArea && entries.isEmpty()) return;
    QString previous=settings->value("exports/directory",QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
    if(!QDir(previous).exists()) previous=QDir::homePath();
    while(true) {
        const auto directory=QFileDialog::getExistingDirectory(this,wholeArea ? "Export user area" : "Export selected files",previous);
        if(directory.isEmpty()) return;
        previous=directory;
        const int area=wholeArea?userAreas->currentItem()->data(Qt::UserRole).toInt():-1;
        auto plan=planExport(entries,directory,area,filenamePolicy);
        if(!plan.error.isEmpty()) { QMessageBox::warning(this,"Cannot export",plan.error);return; }
        bool skip=false;
        if(!plan.conflicts.isEmpty()) {
            QMessageBox dialog(QMessageBox::Warning,"Existing destinations",
                QString("%1 destination name(s) already exist. Nothing will be overwritten.").arg(plan.conflicts.size()),QMessageBox::NoButton,this);
            auto *skipButton=dialog.addButton("Skip existing",QMessageBox::AcceptRole);
            auto *another=dialog.addButton("Choose another destination",QMessageBox::ActionRole);
            dialog.addButton(QMessageBox::Cancel); dialog.exec();
            if(dialog.clickedButton()==another) continue;
            if(dialog.clickedButton()!=skipButton) return;
            skip=true;
        }
        settings->setValue("exports/directory",directory);
        startBatch(plan,skip); return;
    }
}
void Window::exportSelection(const QString &directory,bool skipExisting) {
    if(busy || session.isEmpty()) return;
    const auto entries=exportEntries(false);
    if(entries.isEmpty()) return;
    startBatch(planExport(entries,directory,-1,filenamePolicy),skipExisting);
}
void Window::exportUserArea(const QString &directory,bool skipExisting) {
    if(busy || session.isEmpty() || !userAreas->currentItem()) return;
    startBatch(planExport(exportEntries(true),directory,userAreas->currentItem()->data(Qt::UserRole).toInt(),filenamePolicy),skipExisting);
}
void Window::startBatch(ExportPlan plan,bool skipExisting) {
    if(!plan.error.isEmpty() || (!skipExisting && !plan.conflicts.isEmpty())) {
        error=plan.error.isEmpty()?"Existing destinations found. Choose Skip existing or another destination; nothing was exported.":plan.error;
        status->setText(error); emit completed(false); return;
    }
    if(!QDir(plan.directory).exists() && !QDir().mkdir(plan.directory)) {
        error="Cannot create the numbered user-area folder."; status->setText(error);emit completed(false);return;
    }
    error.clear();
    batch=plan; batchIndex=0; batchActive=true; batchInFlight=false; batchCancelRequested=false;
    batchReport.clear(); reportButton->setEnabled(false); reportButton->hide();
    for(auto &item:batch.items) if(item.exists) item.outcome="Skipped: destination exists";
    progress->setRange(0,batch.items.size()); progress->setValue(0); progress->show();
    setBusy(true); emit exportProgress(0,batch.items.size());
    QTimer::singleShot(0,this,&Window::nextBatchFile);
}
void Window::nextBatchFile() {
    if(!batchActive || batchInFlight) return;
    if(batchCancelRequested) { finishBatch();return; }
    while(batchIndex<batch.items.size() && batch.items[batchIndex].outcome.startsWith("Skipped")) ++batchIndex;
    progress->setValue(batchIndex);
    if(batchIndex==batch.items.size()) { finishBatch();return; }
    if(session.isEmpty() || process.state()!=QProcess::Running) {
        error="The image session ended; open the image again."; finishBatch();return;
    }
    batchInFlight=true;
    const auto &item=batch.items[batchIndex];
    begin({{"op","export"},{"session",session},{"name",item.name},{"destination",item.destination}});
}
void Window::finishBatch() {
    if(!batchActive) return;
    int exported=0,skipped=0,failed=0,cancelled=0,remaining=0;
    QStringList lines;
    for(auto &item:batch.items) {
        if(item.outcome=="Exported") ++exported;
        else if(item.outcome.startsWith("Skipped")) ++skipped;
        else if(item.outcome.startsWith("Failed")) ++failed;
        else if(item.outcome.startsWith("Cancelled")) ++cancelled;
        else { item.outcome="Not attempted"; ++remaining; }
        lines<<item.name.mid(2)+" → "+item.destination+" — "+item.outcome;
    }
    const bool success=!batchCancelRequested && error.isEmpty() && failed==0 && remaining==0;
    const auto summary=QString("%1 exported · %2 skipped · %3 failed · %4 cancelled · %5 not attempted")
        .arg(exported).arg(skipped).arg(failed).arg(cancelled).arg(remaining);
    batchReport=summary+"\nDestination: "+batch.directory+"\n"+
        (batchCancelRequested?"Cancelled. Completed exports were retained.\n":QString())+
        (error.isEmpty()?QString():error+"\n")+lines.join('\n');
    batchActive=false; batchInFlight=false;
    deadline.stop(); cancellationDeadline.stop();setBusy(false);
    progress->setRange(0,qMax(1,int(batch.items.size())));
    progress->setValue(exported+skipped+failed+cancelled);
    reportButton->setEnabled(true); reportButton->show(); status->setText(summary);
    emit completed(success);
    if(closing) close();
}
void Window::showImageInformation() {
    if(busy || session.isEmpty()) return;
    int populated=0;
    for(int area=0;area<userAreas->count();++area)
        if(userAreas->item(area)->data(Qt::UserRole+1).toInt()>0) ++populated;
    const auto details=QString("Source image: %1\nImage layout: %2\nFormat identifier: %3\nContainer: %4\nFiles: %5\nPopulated user areas: %6 of %9\n\nSnapshot SHA-256: %7\nRead-only normalization: %8\n\nRead-only: the source image is never edited.\nThe selected definition is an interpretation of the source image.\nExport retains CP/M record padding.")
        .arg(image,formats->text(),format,container).arg(directoryEntries.size()).arg(populated).arg(snapshotHash,imageNote).arg(userAreas->count());
    QDialog dialog(this);dialog.setWindowTitle("Image information");dialog.resize(700,420);
    auto *layout=new QVBoxLayout(&dialog);
    auto *text=new QTextEdit;text->setObjectName("imageInformationText");text->setReadOnly(true);
    text->setPlainText(details+(exportDiagnostics.isEmpty()?QString():"\n\n"+exportDiagnostics));layout->addWidget(text);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);dialog.exec();
}

void Window::showExportReport() {
    QDialog dialog(this);dialog.setWindowTitle("Export report");dialog.resize(750,450);
    auto *layout=new QVBoxLayout(&dialog);
    auto *text=new QTextEdit; text->setReadOnly(true);text->setPlainText(batchReport);layout->addWidget(text);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);dialog.exec();
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
    status->setText(operation == "open" ? "Reading image…" : operation == "catalogue" ? "Loading disk definitions…" :
        operation == "inspect" ? "Checking image format…" :
        operation == "save_plan" ? "Preparing save…" : operation == "save" ? "Publishing save…" : batchActive ?
        QString("Exporting %1 of %2: %3").arg(batchIndex+1).arg(batch.items.size()).arg(batch.items[batchIndex].name.mid(2)) : "Exporting selected file…");
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
    if(batchActive) {
        batchCancelRequested=true;
        if(!batchInFlight) { finishBatch(); return; }
    }
    if (!cancellationDeadline.isActive()) cancellationDeadline.start(graceMs);
    const QJsonObject request{{"v",1}, {"id","cancel"}, {"op","cancel"}, {"target",requestId}};
    process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    cancelButton->setEnabled(false);
    status->setText("Cancelling…");
}
void Window::fail(const QString &message) {
    afterSave = {};
    deadline.stop(); cancellationDeadline.stop(); error = message;
    if(batchActive) {
        if(batchInFlight && batchIndex<batch.items.size()) batch.items[batchIndex].outcome=
            batchCancelRequested ? "Cancelled: "+message : "Failed: "+message;
        batchInFlight=false; finishBatch(); return;
    }
    setBusy(false);
    if (operation == "open" && !retainSource) resetImage();
    if (operation == "catalogue" || operation == "inspect") requestedImage.clear();
    retainSource = false;
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
    if (operation == "edit" || operation == "discard" || operation.startsWith("systems_")) {
        deadline.stop();
        cancellationDeadline.stop();
        dirty = result["dirty"].toBool();
        if (operation != "systems_export") writeConfirmed = dirty;
        if (result["entries"].isArray()) {
            directoryEntries = result["entries"].toArray();
            updateUserAreas();
            showUserArea();
        }
        setWindowTitle("cpmimage — " + QFileInfo(image).fileName() + (dirty ? " *" : ""));
        setBusy(false);
        status->setText(dirty ? "Unsaved changes." : operation == "discard" ? "Changes discarded." : "System tracks exported.");
        if (operation == "discard" && afterSave) QTimer::singleShot(0, this, std::exchange(afterSave, {}));
        emit completed(true);
        return;
    }
    if (operation == "save_plan" || operation == "save") {
        deadline.stop();
        cancellationDeadline.stop();
        setBusy(false);
        if (operation == "save_plan") {
            bool replace = !pendingSave["destination"].toString().isEmpty() && result["expected"].isString();
            if (replace && QMessageBox::question(this, "Replace image", "Replace the existing destination image?",
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) { afterSave = {}; return; }
            if (result["sidecar_changed"].toBool() && QMessageBox::question(this, "Confirm Disk Definition",
                    QString("Record %1 in the image's FDF? This records your interpretation and does not verify or convert the filesystem.").arg(format),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) { afterSave = {}; return; }
            pendingSave["op"] = "save";
            pendingSave["plan"] = result;
            pendingSave["replace"] = replace;
            pendingSave["confirm_sidecar"] = true;
            begin(pendingSave);
        } else {
            dirty = false;
            image = result["destination"].toString();
            snapshotHash = result["source_sha256"].toString();
            source->setText(QFileInfo(image).fileName());
            source->setToolTip(image);
            setWindowTitle("cpmimage — " + QFileInfo(image).fileName());
            status->setText("Save completed.");
            if (afterSave) QTimer::singleShot(0, this, std::exchange(afterSave, {}));
            emit completed(true);
        }
        return;
    }
    if (operation == "catalogue" || operation == "inspect") {
        deadline.stop(); cancellationDeadline.stop();
        setBusy(false);
        if (operation == "catalogue") {
            if (!result["formats"].isArray() || result["catalogue"].toString().isEmpty()) { invalid(); return; }
            catalogueRows = result["formats"].toArray();
            activeCatalogue = result["catalogue"].toString();
            cataloguePath = activeCatalogue;
            if (!requestedImage.isEmpty()) {
                begin({{"op","inspect"}, {"image",requestedImage}, {"explicit_format",requestedFormat}});
            } else {
                setBusy(false);
                status->setText(QString("Loaded %1 disk definitions.").arg(catalogueRows.size()));
                emit completed(true);
            }
            return;
        }
        inspectedHash = result["source_sha256"].toString();
        inspectedContainer = requestedContainer.isEmpty() ? result["container"].toString() : requestedContainer;
        if (!hash.match(inspectedHash).hasMatch() || !result["candidates"].isArray()) { invalid(); return; }
        const auto warning = result["sidecar_warning"].toString();
        if (!warning.isEmpty()) QMessageBox::warning(this, "Invalid image sidecar", warning);
        if (!requestedFormat.isEmpty()) {
            openDefinition({{"id",requestedFormat}}, false, false);
            return;
        }
        if (result["sidecar"].isObject()) {
            openDefinition(result["sidecar"].toObject(), true, false);
            return;
        }
        const auto record = settings->value(associationKey(requestedImage, activeCatalogue)).toStringList();
        if (record.size() == 4 && record[0] == inspectedHash && record[3] == inspectedContainer) {
            for (const auto &value : catalogueRows) {
                auto row = value.toObject();
                if (row["id"].toString() == record[1] && row["definition_sha256"].toString() == record[2]) {
                    openDefinition(row, false, false);
                    return;
                }
            }
        }
        chooseFormat(result, false);
        return;
    }
    if (operation == "open") {
        if (!result["entries"].isArray() || result["session"].toString().isEmpty()) {
            stopReader(); fail("Incomplete directory response."); return;
        }
        const auto rows = result["entries"].toArray();
        if (rows.size() > 4096 || result["session"].toString().size() > 128 ||
            !hash.match(result["source_sha256"].toString()).hasMatch()) { invalid(); return; }
        QSet<QString> names;
        const QRegularExpression filename(QString::fromLatin1(R"CPM(^([012][0-9]|3[01])[A-Z0-9!#$%&'()@^_`{}~+/:\\|"-]{1,8}(\.[A-Z0-9!#$%&'()@^_`{}~+/:\\|"-]{1,3})?$)CPM"));
        for (const auto &row : rows) {
            const auto e = row.toObject(); const auto n = e["name"].toString();
            const double size = e["size"].toDouble(-1), attr = e["attr"].toDouble(-1);
            if (!row.isObject() || !filename.match(n).hasMatch() || names.contains(n) ||
                size < 0 || size > 16777216 || std::floor(size) != size ||
                attr < 0 || attr > 16143 || std::floor(attr) != attr || (int(attr) & ~0x3f0f)) {
                invalid(); return;
            }
            names.insert(n);
        }
        session = result["session"].toString();
        directoryEntries = rows;
        const auto tail = result["unused_tail_filled"];
        if (!tail.isUndefined() && !tail.isBool()) { invalid(); return; }
        imageNote = tail.toBool() ? "Unused image tail filled with E5 in the temporary snapshot; original unchanged." : "None.";
        snapshotHash=result["source_sha256"].toString();
        retainSource = false;
        const auto definitionHash = result["definition_sha256"].toString();
        if (!activeCatalogue.isEmpty() && hash.match(definitionHash).hasMatch()) {
            settings->setValue(associationKey(image, activeCatalogue),
                               QStringList{snapshotHash, format, definitionHash, container});
        }
        source->setText(QFileInfo(image).fileName()); source->setToolTip(image);
        setWindowTitle("cpmimage — "+QFileInfo(image).fileName());
        updateUserAreas();
        { QSignalBlocker block(userAreas); userAreas->setCurrentItem(userAreas->item(0)); }
        showUserArea();
    } else {
        const double size = result["size"].toDouble(-1);
        if(batchActive && size!=batch.items[batchIndex].size) { invalid();return; }
        if (size < 0 || size > 16777216 || std::floor(size) != size ||
            !hash.match(result["sha256"].toString()).hasMatch()) { invalid(); return; }
        exportDiagnostics=QString("Last completed export\nBytes: %1\nSHA-256: %2")
            .arg(result["size"].toInteger()).arg(result["sha256"].toString());
        status->setText(QString("Exported %1 bytes.").arg(result["size"].toInteger()));
    }
    deadline.stop(); cancellationDeadline.stop();
    if(batchActive && operation=="export") {
        batch.items[batchIndex].outcome="Exported";
        batchInFlight=false; ++batchIndex;
        progress->setValue(batchIndex); emit exportProgress(batchIndex,batch.items.size());
        QTimer::singleShot(0,this,&Window::nextBatchFile); return;
    }
    setBusy(false);
    if (operation == "open") updateSelectionStatus();
    emit completed(true);
    if (closing) close();
}
void Window::closeEvent(QCloseEvent *event) {
    if (busy) { closing = true; cancel(); event->ignore(); }
    else if (!resolveEdits([this] { close(); })) event->ignore();
    else { process.closeWriteChannel(); event->accept(); }
}
