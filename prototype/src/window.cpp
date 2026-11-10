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
#include <QSettings>
#include <QLineEdit>
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
    formats->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    formats->setMinimumContentsLength(18);
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

    status = new QLabel("Choose the image layout, then open an image.");
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
    setWindowTitle("cpmimage — Review prototype"); setBusy(busy);
}
void Window::setBusy(bool value) {
    busy = value;
    openButton->setEnabled(!busy);
    formats->setEnabled(!busy);
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
    source->setText(QFileInfo(image).fileName()); source->setToolTip(image);
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
    for(int area=0;area<16;++area)
        if(userAreas->item(area)->data(Qt::UserRole+1).toInt()>0) ++populated;
    const auto details=QString("Source image: %1\nImage layout: %2\nFormat identifier: %3\nContainer: %4\nFiles: %5\nPopulated user areas: %6 of 16\n\nSnapshot SHA-256: %7\nRead-only normalization: %8\n\nRead-only: the source image is never edited.\nLayout selection is manual; no automatic format detection.\nExport retains CP/M record padding.")
        .arg(image,formats->currentText(),format,container).arg(directoryEntries.size()).arg(populated).arg(snapshotHash,imageNote);
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
    status->setText(operation == "open" ? "Reading image…" : batchActive ?
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
    deadline.stop(); cancellationDeadline.stop(); error = message;
    if(batchActive) {
        if(batchInFlight && batchIndex<batch.items.size()) batch.items[batchIndex].outcome=
            batchCancelRequested ? "Cancelled: "+message : "Failed: "+message;
        batchInFlight=false; finishBatch(); return;
    }
    setBusy(false);
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
        const QRegularExpression filename(QString::fromLatin1(R"CPM(^(0[0-9]|1[0-5])[A-Z0-9!#$%&'()@^_`{}~+/:\\|"-]{1,8}(\.[A-Z0-9!#$%&'()@^_`{}~+/:\\|"-]{1,3})?$)CPM"));
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
        imageNote = tail.toBool() ? "Unused image tail filled with E5 in the temporary snapshot; original unchanged." : "None.";
        snapshotHash=result["source_sha256"].toString();
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
    else { process.closeWriteChannel(); event->accept(); }
}
