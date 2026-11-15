#include <QPointer>
#include <QDateTime>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include <QMetaMethod>
#include <QLocale>
#include <QCryptographicHash>
#include <algorithm>
#include <utility>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QDrag>
#include <QTextBrowser>
#include <QDesktopServices>
#include <QEventLoop>
#include <QDragEnterEvent>
#include <QTemporaryDir>
#include <QSaveFile>
#include <memory>
#include <QUrl>
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
#include <QKeyEvent>
#include <QInputDialog>
#include <QCheckBox>
#include <QProgressBar>
#include <QMessageBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QFontDatabase>
#include <QStringDecoder>
#include <QScrollBar>
#include <QShortcut>
#include <QTextBlock>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#endif

static constexpr auto cutMime = "application/x-cpmimage-cut";
static constexpr auto receiptMime = "application/x-cpmimage-receipt";

class TransferFiles : public QMimeData {
public:
    QPointer<Window> origin;
    quint64 revision = 0;
    QString session;
    QHash<QString, QString> names;
};

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
    for (const auto &option : QList<QPair<QString,QString>>{
            {"Keep automatic save backups", "saveBackups"},
            {"Save disk definitions beside images", "saveSidecar"},
            {"Show routine disk-definition save warnings", "warnSaveSidecar"},
            {"Convert single-file exports to text", "textExport"}}) {
        auto *action = preferences->addAction(option.first);
        action->setCheckable(true);
        action->setChecked(settings->value(option.second, option.second != "textExport").toBool());
        connect(action, &QAction::toggled, this, [this, option](bool checked) {
            settings->setValue(option.second, checked);
        });
    }
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
    auto *guide = help->addAction("CP/M Image Help");
    guide->setShortcut(QKeySequence::HelpContents);
    connect(guide, &QAction::triggered, this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle("CP/M Image Help");
        dialog.resize(650, 540);
        auto *layout = new QVBoxLayout(&dialog);
        auto *search = new QLineEdit(&dialog);
        search->setPlaceholderText("Find in Help");
        layout->addWidget(search);
        auto *text = new QTextBrowser(&dialog);
        QFile markdown(":/help/guide.md");
        if (markdown.open(QIODevice::ReadOnly))
            text->setMarkdown(QString::fromUtf8(markdown.readAll()));
        layout->addWidget(text);
        connect(search, &QLineEdit::returnPressed, &dialog, [search, text] {
            if (search->text().isEmpty() || text->find(search->text())) return;
            text->moveCursor(QTextCursor::Start);
            text->find(search->text());
        });
        dialog.exec();
    });
    connect(help->addAction("Online Help"), &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl("https://github.com/CPMArchives/cpmimage/blob/main/docs/HELP.md"));
    });
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
    auto *toolbarWidget = new QWidget(central);
    auto *toolbar = new QHBoxLayout(toolbarWidget);
    toolbar->setContentsMargins(0,0,0,0);
    formats = new QPushButton("Choose format…");
    formats->setObjectName("chooseFormat");

    openButton = new QPushButton("Open Image…");
    exportButton = new QPushButton("Export…");
    exportButton->setObjectName("toolbarExport");
    exportButton->setToolTip("Export… (X)");
    cancelButton = new QPushButton("Cancel");
    cancelButton->setObjectName("cancelOperation");
    openButton->setToolTip("Open image (Ctrl+O / Command+O)");
    auto *newButton = new QPushButton("New");
    newButton->setObjectName("toolbarNew");
    auto *saveButton = new QPushButton("Save");
    saveButton->setObjectName("toolbarSave");
    auto *importButton = new QPushButton("Import");
    importButton->setObjectName("toolbarImport");
    for (const auto &[button, icon] : {
             std::pair{openButton, "open"}, std::pair{newButton, "new"},
             std::pair{saveButton, "save"}, std::pair{importButton, "import"},
             std::pair{exportButton, "export"}}) {
        button->setAccessibleName(button->text());
        if (button->toolTip().isEmpty()) button->setToolTip(button->text());
        button->setIcon(QIcon(QString(":/icons/toolbar/%1.png").arg(icon)));
        button->setIconSize(QSize(64, 64));
        button->setText({});
        button->setFlat(true);
        button->setStyleSheet("QPushButton { border: none; background: transparent; padding: 4px; }"
                              "QPushButton:focus { background: palette(mid); }");
    }
    toolbar->addWidget(openButton);
    toolbar->addWidget(newButton);
    toolbar->addWidget(saveButton);
    toolbar->addWidget(importButton);
    toolbar->addWidget(exportButton);
    layout->addWidget(toolbarWidget);
    toolbar->addStretch();

    auto *imageRow=new QHBoxLayout;
    imageRow->addWidget(new QLabel("Image layout:"));
    imageRow->addWidget(formats,1);
    imageInfoButton=new QPushButton;
    imageInfoButton->setObjectName("imageInformation");
    imageInfoButton->setAccessibleName("Image information");
    imageInfoButton->setToolTip("Image information");
    QPixmap informationIcon(64, 64);
    informationIcon.setDevicePixelRatio(2);
    informationIcon.fill(Qt::transparent);
    {
        QPainter painter(&informationIcon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#81929d"), 3.5));
        painter.drawEllipse(QRectF(3, 3, 26, 26));
        painter.setPen(QPen(QColor("#dce6ec"), 2));
        painter.drawEllipse(QRectF(3, 3, 26, 26));
        painter.setPen(QPen(QColor("#00cbe8"), 3.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawPoint(QPointF(16, 10));
        painter.drawLine(QPointF(16, 15), QPointF(16, 22));
    }
    imageInfoButton->setIcon(QIcon(informationIcon));
    imageInfoButton->setIconSize(QSize(32, 32));
    imageInfoButton->setFlat(true);
    imageInfoButton->setStyleSheet("QPushButton { border: none; background: transparent; padding: 4px; }"
                                  "QPushButton:focus { background: palette(mid); }");
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
    class FileTable : public QTableWidget {
    public:
        FileTable() : QTableWidget(0, 8) {}
        std::function<void()> dragFiles;
    protected:
        void startDrag(Qt::DropActions) override { dragFiles(); }
    };
    auto *fileTable = new FileTable;
    fileTable->dragFiles = [this] { offerFiles(true); };
    table = fileTable;
    table->setObjectName("fileList");
    table->setAccessibleName("Files in selected user area");
    table->setHorizontalHeaderLabels({"Filename", "Size (bytes)", "Attributes", "User", "Records", "Created", "Modified", "Accessed"});
    auto *header = table->horizontalHeader();
    header->setSectionsMovable(true);
    header->moveSection(header->visualIndex(4), 1);
    for (int column : {3, 5, 6, 7}) table->hideColumn(column);
    table->setTabKeyNavigation(false);
    for (auto *pane : {static_cast<QAbstractItemView *>(userAreas),
                       static_cast<QAbstractItemView *>(table)}) {
        pane->installEventFilter(this);
        pane->setStyleSheet("QAbstractItemView { border: 2px solid transparent; }"
                            "QAbstractItemView:focus { border-color: palette(highlight); }");
    }
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->setColumnWidth(0, 170);
    header->restoreState(settings->value("fileColumns/layout").toByteArray());
    const auto saveColumns = [this, header] {
        settings->setValue("fileColumns/layout", header->saveState());
    };
    connect(header, &QHeaderView::sectionMoved, this, saveColumns);
    connect(header, &QHeaderView::sectionResized, this, saveColumns);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QHeaderView::customContextMenuRequested, this, [this, header, saveColumns](const QPoint &point) {
        QMenu menu;
        auto *columns = menu.addMenu("Choose Columns");
        for (int column = 1; column < table->columnCount(); ++column) {
            auto *action = columns->addAction(table->horizontalHeaderItem(column)->text());
            action->setCheckable(true);
            action->setChecked(!table->isColumnHidden(column));
            const bool forceUser = column == 3 && userAreas->currentRow() == 0;
            action->setEnabled(!forceUser);
            connect(action, &QAction::toggled, this, [this, column, saveColumns](bool shown) {
                if (column == 3) settings->setValue("fileColumns/user", shown);
                table->setColumnHidden(column, !shown);
                saveColumns();
            });
        }
        menu.exec(header->mapToGlobal(point));
    });
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
    diskSpace = new QLabel;
    diskSpace->setObjectName("diskSpace");
    diskSpace->setToolTip("Free and total CP/M filesystem space across all user areas.");
    diskSpace->hide();
    statusRow->addWidget(diskSpace);
    statusRow->addWidget(cancelButton);
    progress=new QProgressBar; progress->setObjectName("exportProgress"); progress->hide();
    layout->addWidget(progress);
    reportButton=new QPushButton("Export report…"); reportButton->setObjectName("exportReport"); reportButton->setEnabled(false);
    reportButton->hide();
    statusRow->addWidget(reportButton);
    layout->addLayout(statusRow);
    connect(reportButton,&QPushButton::clicked,this,&Window::showExportReport);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table,&QTableWidget::customContextMenuRequested,this,[this](const QPoint &point){
        if(busy || session.isEmpty()) return;
        const auto *item=table->itemAt(point);
        if(item && !item->isSelected()) table->selectRow(item->row());
        QMenu menu; auto *action=menu.addAction("Export…");
        action->setEnabled(!table->selectionModel()->selectedRows().isEmpty());
        auto *preview = menu.addAction("View File…");
        preview->setEnabled(viewFileAction->isEnabled());
        table->setFocus(Qt::MouseFocusReason);
        auto *chosen = menu.exec(table->viewport()->mapToGlobal(point));
        if (chosen == action) exportAction->trigger();
        if (chosen == preview) viewFileAction->trigger();
    });
    userAreas->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(userAreas,&QListWidget::customContextMenuRequested,this,[this](const QPoint &point){
        if(busy || session.isEmpty()) return;
        auto *item=userAreas->itemAt(point); if(!item) return;
        userAreas->setCurrentItem(item);
        userAreas->setFocus(Qt::MouseFocusReason);
        QMenu menu; auto *action=menu.addAction("Export…");
        if(menu.exec(userAreas->viewport()->mapToGlobal(point))==action) exportAction->trigger();
    });
    auto *fileMenu=new QMenu("File",this);
    menuBar()->insertMenu(preferences->menuAction(),fileMenu);
    openAction = fileMenu->addAction("Open Image…");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction,&QAction::triggered,openButton,&QPushButton::click);
    auto *newAction = fileMenu->addAction("New Image…");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &Window::createImage);
    connect(fileMenu->addAction("Format Image…"), &QAction::triggered, this, [this] {
        if (busy || session.isEmpty()) return;
        const auto answer = QMessageBox::warning(this, "Format working image",
            QString("Erase all filesystem contents in %1 using %2? Reserved system tracks will be preserved. Changes stay private until Save.")
                .arg(QFileInfo(image).fileName(), format),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
        auto options = initializeOptions(imageGeometry);
        if (options.isEmpty()) return;
        options["op"] = "format_image";
        options["session"] = session;
        begin(options);
    });
    auto *saveAction = fileMenu->addAction("Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(newButton, &QPushButton::clicked, newAction, &QAction::trigger);
    connect(saveButton, &QPushButton::clicked, saveAction, &QAction::trigger);
    connect(importButton, &QPushButton::clicked, this, [this] { chooseEdit("import"); });
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
    auto *copy = editMenu->addAction("Copy Files");
    copy->setShortcut(QKeySequence::Copy);
    connect(copy, &QAction::triggered, this, [this] { offerFiles(false); });
    auto *cut = editMenu->addAction("Cut Files");
    cut->setShortcut(QKeySequence::Cut);
    connect(cut, &QAction::triggered, this, [this] { offerFiles(false, true); });
    auto *paste = editMenu->addAction("Paste Files");
    paste->setShortcut(QKeySequence::Paste);
    connect(paste, &QAction::triggered, this, [this] {
        const auto *mime = QApplication::clipboard()->mimeData();
        if (!mime) return;
        QStringList paths;
        for (const auto &url : mime->urls())
            if (url.isLocalFile()) paths.append(url.toLocalFile());
        if (!paths.isEmpty()) chooseEdit("paste", paths);
    });
    table->viewport()->setAcceptDrops(true);
    userAreas->viewport()->setAcceptDrops(true);
    table->viewport()->installEventFilter(this);
    userAreas->viewport()->installEventFilter(this);
    table->setDragEnabled(true);
    table->setDragDropMode(QAbstractItemView::DragDrop);
    table->viewport()->setAcceptDrops(true);

    auto *rename = editMenu->addAction("Rename…");
    rename->setShortcut(Qt::Key_F2);
    table->addAction(rename);
    connect(rename, &QAction::triggered, this, [this] { chooseEdit("rename"); });
    for (const auto &item : QList<QPair<QString,QString>>{{"Import files…","import"},
            {"Delete…","delete"},{"Move to user area…","move"},{"Properties…","attrs"},
            {"Export system tracks…","systems_export"},{"Replace system tracks…","systems_import"}}) {
        auto *action = editMenu->addAction(item.first);
        if (item.second == "delete") action->setShortcut(QKeySequence::Delete);
        if (item.second == "attrs") action->setShortcut(Qt::Key_F4);
        if (item.second == "move") action->setShortcut(Qt::Key_F6);
        connect(action, &QAction::triggered, this, [this, item] { chooseEdit(item.second); });
    }
    auto *commentAction = editMenu->addAction("Edit IMD comment…");
    connect(editMenu, &QMenu::aboutToShow, this, [this, commentAction] {
        commentAction->setEnabled(!busy && !session.isEmpty() && container == "imd");
    });
    connect(commentAction, &QAction::triggered, this, [this] {
        if (busy || session.isEmpty() || container != "imd") return;
        bool accepted = false;
        const auto comment = QInputDialog::getMultiLineText(this, "Edit IMD comment",
            "Comment", imdComment, &accepted);
        if (accepted && comment != imdComment)
            begin({{"op","imd_comment"},{"session",session},{"comment",comment}});
    });
    exportAction = fileMenu->addAction("Export…");
    exportAction->setObjectName("exportAction");
    connect(exportAction, &QAction::triggered, this, &Window::chooseContextExport);
    fileMenu->addSeparator();
    imageInfoAction=fileMenu->addAction("Image information…");
    imageInfoAction->setObjectName("imageInformationAction");
    connect(imageInfoAction,&QAction::triggered,this,&Window::showImageInformation);
    connect(imageInfoButton,&QPushButton::clicked,this,&Window::showImageInformation);
    setCentralWidget(central);
    connect(openButton, &QPushButton::clicked, this, &Window::chooseImage);
    connect(exportButton, &QPushButton::clicked, exportAction, &QAction::trigger);
    connect(cancelButton, &QPushButton::clicked, this, &Window::cancel);
    connect(table, &QTableWidget::itemSelectionChanged, this, [this] {
        setBusy(busy); updateSelectionStatus();
    });
    auto *view = menuBar()->addMenu("View");
    viewFileAction = view->addAction("View File…");
    viewFileAction->setObjectName("viewFileAction");
    connect(viewFileAction, &QAction::triggered, this, [this] { viewFile(); });
    connect(table, &QTableWidget::cellDoubleClicked, viewFileAction, &QAction::trigger);
    auto *showToolbar = view->addAction("Toolbar");
    showToolbar->setCheckable(true);
    showToolbar->setChecked(settings->value("toolbarVisible", true).toBool());
    toolbarWidget->setVisible(showToolbar->isChecked());
    connect(showToolbar, &QAction::toggled, this, [this, toolbarWidget](bool visible) {
        toolbarWidget->setVisible(visible);
        settings->setValue("toolbarVisible", visible);
    });
    showAllAction = view->addAction("Show All User Areas");
    showAllAction->setCheckable(true);
    showAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    connect(showAllAction, &QAction::triggered, this, [this](bool checked) {
        userAreas->setCurrentRow(checked ? 0 : previousUserArea + 1);
    });
    connect(userAreas, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item && item->data(Qt::UserRole).toInt() >= 0)
            previousUserArea = item->data(Qt::UserRole).toInt();
        showUserArea();
    });
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
    dirty = false; writeConfirmed = false; unpublished = false;
    session.clear(); image.clear(); directoryEntries = {}; imageNote.clear();
    snapshotHash.clear(); exportDiagnostics.clear();
    diskSpace->hide();
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
    openAction->setEnabled(!busy);
    findChild<QPushButton *>("toolbarNew")->setEnabled(!busy);
    for (const auto *name : {"toolbarSave", "toolbarImport"})
        findChild<QPushButton *>(name)->setEnabled(!busy && !session.isEmpty());
    formats->setEnabled(!busy && !image.isEmpty() && !catalogueRows.isEmpty());
    externalCatalogueAction->setEnabled(!busy);
    bundledCatalogueAction->setEnabled(!busy);
    table->setEnabled(!busy && !session.isEmpty());
    userAreas->setEnabled(!busy && !session.isEmpty());
    showAllAction->setEnabled(userAreas->isEnabled());
    showAllAction->setChecked(userAreas->currentRow() == 0);
    updateExportAction();
    viewFileAction->setEnabled(!busy && !session.isEmpty() &&
                               table->selectionModel()->selectedRows().size() == 1);
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
void Window::chooseEdit(const QString &action, QStringList paths, const TransferFiles *transfer) {
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
    QPointer<Window> cutOrigin;
    quint64 cutRevision = 0;
    QString cutSession;
    QJsonArray removedNames, importedPaths;
    QByteArray transferToken;
    bool accepted = false;
    if (action == "import" || action == "paste") {
        request["action"] = "import";
        QHash<QString, QString> cutNames;
        const auto *cut = transfer;
        if (!cut && action == "paste")
            cut = dynamic_cast<const TransferFiles *>(QApplication::clipboard()->mimeData());
        if (action == "paste" && !cut) {
            const auto *clipboard = QApplication::clipboard()->mimeData();
            if (clipboard) transferToken = clipboard->data(cutMime);
        }
        if (cut) {
            cutOrigin = cut->origin;
            cutRevision = cut->revision;
            cutSession = cut->session;
            cutNames = cut->names;
        }
        if (paths.isEmpty()) paths = QFileDialog::getOpenFileNames(this, "Import files");
        if (paths.isEmpty() || !userAreas->currentItem()) return;
        const QRegularExpression valid(R"CPM(^[A-Z0-9!#$%&'()@^_`{}~+/:\\|"-]{1,8}(\.[A-Z0-9!#$%&'()@^_`{}~+/:\\|"-]{1,3})?$)CPM");
        int area = userAreas->currentItem()->data(Qt::UserRole).toInt();
        if (area < 0) {
            bool accepted = false;
            area = QInputDialog::getInt(this, "Import destination", "User area",
                previousUserArea, 0, userAreas->count() - 2, 1, &accepted);
            if (!accepted) return;
        }
        const auto prefix = QString("%1").arg(area, 2, 10, QLatin1Char('0'));
        QSet<QString> existing;
        for (const auto &entry : directoryEntries) existing.insert(entry.toObject()["name"].toString());
        QDialog review(this);
        review.setWindowTitle("Review import");
        QVBoxLayout layout(&review);
        QTableWidget names(paths.size(), 2);
        names.setHorizontalHeaderLabels({"Host file", "CP/M filename"});
        names.horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        for (int row = 0; row < paths.size(); ++row) {
            const auto original = QFileInfo(paths[row]).fileName();
            auto *host = new QTableWidgetItem(original);
            host->setFlags(host->flags() & ~Qt::ItemIsEditable);
            names.setItem(row, 0, host);
            names.setItem(row, 1, new QTableWidgetItem(original.toUpper()));
        }
        QCheckBox text("Convert text to CP/M line endings and EOF marker");
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout.addWidget(&names);
        layout.addWidget(&text);
        layout.addWidget(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &review, &QDialog::reject);
        connect(&buttons, &QDialogButtonBox::accepted, &review, [&] {
            QSet<QString> used;
            for (int row = 0; row < paths.size(); ++row) {
                const auto name = names.item(row, 1)->text().toUpper();
                if (!valid.match(name).hasMatch() || used.contains(name)) {
                    QMessageBox::warning(&review, "Check import names", "Give each file a distinct CP/M 8.3 name.");
                    names.setCurrentCell(row, 1);
                    return;
                }
                used.insert(name);
            }
            review.accept();
        });
        if (review.exec() != QDialog::Accepted) return;
        QJsonArray items;
        QString defaultConflict;
        for (int row = 0; row < paths.size(); ++row) {
            const auto key = prefix + names.item(row, 1)->text().toUpper();
            const bool conflict = existing.contains(key);
            auto choice = defaultConflict;
            if (conflict && choice.isEmpty()) {
                QMessageBox prompt(QMessageBox::Question, "Import conflict",
                    key.mid(2) + " already exists.", QMessageBox::Cancel, this);
                auto *replace = prompt.addButton("Replace", QMessageBox::AcceptRole);
                auto *skip = prompt.addButton("Skip", QMessageBox::RejectRole);
                prompt.setDefaultButton(skip);
                auto *all = new QCheckBox("Apply to All");
                prompt.setCheckBox(all);
                prompt.exec();
                if (prompt.clickedButton() == replace) choice = "replace";
                else if (prompt.clickedButton() == skip) choice = "skip";
                else return;
                if (all->isChecked()) defaultConflict = choice;
            }
            if (conflict && choice == "skip") continue;
            const auto oldName = cutNames.value(paths[row]);
            if (cutOrigin == this && cutNames.values().contains(key)) continue;
            if (!oldName.isEmpty()) removedNames.append(oldName);
            importedPaths.append(paths[row]);
            items.append(QJsonObject{{"name",key},{"path",paths[row]},{"replace",conflict},{"text",text.isChecked()}});
        }
        if (items.isEmpty()) return;
        request["items"] = items;
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
    if (transferToken.size() == 36) {
        connect(this, &Window::completed, this, [transferToken, importedPaths](bool ok) {
            if (!ok) return;
            auto *clipboard = QApplication::clipboard();
            const auto *offer = clipboard->mimeData();
            if (!offer || offer->data(cutMime) != transferToken) return;
            auto *receipt = new QMimeData;
            receipt->setUrls(offer->urls());
            receipt->setData(receiptMime, QJsonDocument(QJsonObject{
                {"token", QString::fromLatin1(transferToken)}, {"files", importedPaths}
            }).toJson(QJsonDocument::Compact));
            clipboard->setMimeData(receipt);
        }, Qt::SingleShotConnection);
    }
    if (cutOrigin && !removedNames.isEmpty()) {
        connect(this, &Window::completed, this, [this, cutOrigin, cutRevision, cutSession, removedNames](bool ok) {
            if (!ok) return;
            const auto expected = cutRevision + (cutOrigin == this ? 1 : 0);
            if (!cutOrigin || cutOrigin->busy || cutOrigin->session != cutSession ||
                cutOrigin->imageRevision != expected || !cutOrigin->confirmWrite()) {
                status->setText("Files copied. Source files were retained.");
                return;
            }
            connect(cutOrigin, &Window::completed, this, [this](bool removed) {
                status->setText(removed ? "Files moved in the working images." :
                                          "Files copied. Source removal failed; duplicates remain.");
            }, Qt::SingleShotConnection);
            cutOrigin->begin({{"op","edit"},{"session",cutSession},{"action","delete_many"},
                              {"names",removedNames},{"confirmed",true}});
        }, Qt::SingleShotConnection);
    }
    begin(request);
}
QJsonObject Window::initializeOptions(const QJsonObject &geometry) {
    QDialog dialog(this);
    dialog.setWindowTitle("Filesystem initialization");
    QVBoxLayout layout(&dialog);
    QLabel details(QString("%1 tracks · %2 sectors per track · %3 bytes per sector")
        .arg(geometry["tracks"].toInt()).arg(geometry["sectors_per_track"].toInt())
        .arg(geometry["sector_size"].toInt()));
    QLineEdit label;
    label.setMaxLength(11);
    label.setPlaceholderText("Optional CP/M Plus disk label");
    QCheckBox timestamps("Initialize CP/M Plus timestamps");
    layout.addWidget(&details);
    if (geometry["cpm_plus"].toBool()) {
        layout.addWidget(&label);
        layout.addWidget(&timestamps);
    }
    QLineEdit bootPath;
    bootPath.setReadOnly(true);
    bootPath.setPlaceholderText("No system-track file selected");
    QPushButton bootButton("Choose system-track file…");
    QCheckBox pad("Pad short input with E5");
    if (geometry["boot_sectors"].toInt() > 0) {
        auto *bootDescription = new QLabel("Optional. Copy a binary file (such as a .bin) into the reserved system sectors.");
        bootDescription->setWordWrap(true);
        layout.addWidget(bootDescription);
        layout.addWidget(&bootButton);
        layout.addWidget(&bootPath);
        layout.addWidget(&pad);
        connect(&bootButton, &QPushButton::clicked, &dialog, [&] {
            bootPath.setText(QFileDialog::getOpenFileName(&dialog, "Choose file for reserved system sectors", {},
                "Binary files (*.bin);;All files (*)"));
        });
    }
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return {};
    return {{"confirmed", true}, {"label", label.text()}, {"timestamps", timestamps.isChecked()},
            {"boot_path", bootPath.text()}, {"pad", pad.isChecked()}};
}
void Window::chooseSave(bool saveAs) {
    saveAs = saveAs || unpublished;
    if (busy || session.isEmpty()) return;
    QString destination, selectedFilter;
    if (saveAs) {
        destination = QFileDialog::getSaveFileName(this, "Save CP/M image as", image,
            "RAW disk images (*.raw *.img);;Standard CPCEMU DSK (*.dsk);;ImageDisk (*.imd);;JV3 (*.jv3);;DMK (*.dmk)", &selectedFilter, QFileDialog::DontConfirmOverwrite);
        if (destination.isEmpty()) { afterSave = {}; return; }
    }
    pendingSave = {{"op","save_plan"}, {"session",session}, {"destination",destination},
                   {"backups",settings->value("saveBackups",true).toBool()},
                   {"sidecar",settings->value("saveSidecar",true).toBool()}};
    if (saveAs) {
        QString type = "raw";
        if (selectedFilter.startsWith("Standard")) type = "dsk";
        else if (selectedFilter.startsWith("ImageDisk")) type = "imd";
        else if (selectedFilter.startsWith("JV3")) type = "jv3";
        else if (selectedFilter.startsWith("DMK")) type = "dmk";
        pendingSave["container"] = type;
    }
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
void Window::createImage() {
    if (busy) return;
    if (!session.isEmpty() && isSignalConnected(QMetaMethod::fromSignal(&Window::newImageRequested))) {
        emit newImageRequested();
        return;
    }
    if (!resolveEdits([this] { createImage(); })) return;
    newImagePicker = true;
    if (catalogueRows.isEmpty()) loadCatalogue(cataloguePath);
    else chooseFormat({}, false);
}

void Window::chooseImage() {
    if (receivers(SIGNAL(imagePicked(QString))) && (!session.isEmpty() || busy)) {
        const auto path = QFileDialog::getOpenFileName(this, "Open CP/M image", image,
            "Disk images (*.raw *.img *.dsk *.imd *.jv3 *.dmk);;All files (*)");
        if (!path.isEmpty()) emit imagePicked(path);
        return;
    }
    openFromPicker();
}
void Window::openFromPicker() {
    if (!resolveEdits([this] { chooseImage(); })) return;
    const QString path = QFileDialog::getOpenFileName(this, "Open CP/M image", image.isEmpty() ? QDir::homePath() : image,
        "Disk images (*.raw *.img *.dsk *.imd *.jv3 *.dmk);;All files (*)");
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
    begin({{"op", "inspect"}, {"image", requestedImage}, {"explicit_format", requestedFormat}, {"container", requestedContainer}});
}
void Window::openImage(const QString &path, const QString &fmt, const QString &type) {
    if (busy) return;
    requestedImage.clear();
    requestedFormat.clear();
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
    class FormatSearch : public QLineEdit {
    public:
        QListWidget *results = nullptr;
    protected:
        void keyPressEvent(QKeyEvent *event) override {
            if (event->key() == Qt::Key_Down && event->modifiers() == Qt::NoModifier &&
                results && results->count()) {
                results->setFocus(Qt::ShortcutFocusReason);
                event->accept();
                return;
            }
            QLineEdit::keyPressEvent(event);
        }
    };
    auto *search = new FormatSearch;
    search->setPlaceholderText("Search disk definitions");
    layout->addWidget(search);
    auto *group = new QComboBox;
    group->addItems({"Plausible candidates", "All formats", "Manufacturer and model", "System family", "Favorites"});
    layout->addWidget(group);
    auto *list = new QListWidget;
    search->results = list;
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
    const auto diagnostic = inspection["detection_warning"].toString();
    if (!diagnostic.isEmpty()) {
        auto *hint = new QLabel(diagnostic, &dialog);
        hint->setWordWrap(true);
        layout->insertWidget(0, hint);
    }
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
    const bool creating = std::exchange(newImagePicker, false);
    if (dialog.exec() == QDialog::Accepted && list->currentItem()) {
        if (creating) {
            newDefinition = list->currentItem()->data(Qt::UserRole).toJsonObject();
            auto options = initializeOptions(newDefinition["geometry"].toObject());
            if (options.isEmpty()) return;
            options["op"] = "new_image";
            options["format"] = newDefinition["id"];
            begin(options);
            return;
        }
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
    if (!userAreas->count() || userAreas->item(0)->data(Qt::UserRole).toInt() != -1) {
        auto *all = new QListWidgetItem("All");
        all->setData(Qt::UserRole, -1);
        userAreas->insertItem(0, all);
    }
    int areas = 16;
    for (const auto &value : directoryEntries)
        areas = qMax(areas, value.toObject()["name"].toString().left(2).toInt() + 1);
    while (userAreas->count() > areas + 1) delete userAreas->takeItem(userAreas->count() - 1);
    while (userAreas->count() < areas + 1) {
        const int area = userAreas->count() - 1;
        auto *item = new QListWidgetItem(QString::number(area), userAreas);
        item->setData(Qt::UserRole, area);
    }
    for (int area = 0; area < areas; ++area) {
        int count = 0;
        for (const auto &value : directoryEntries)
            if (value.toObject()["name"].toString().left(2).toInt() == area) ++count;
        auto *item = userAreas->item(area + 1);
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
    const QSignalBlocker headerBlock(table->horizontalHeader());
    table->setColumnHidden(3, !(area && currentArea == -1) &&
                           !settings->value("fileColumns/user", false).toBool());
    table->setSortingEnabled(false);
    table->clearSelection(); table->setCurrentItem(nullptr); table->setRowCount(0);
    for (const auto &value : directoryEntries) {
        const auto entry = value.toObject();
        const auto name = entry["name"].toString();
        if (!area || (currentArea >= 0 && name.left(2).toInt() != currentArea)) continue;
        const int row = table->rowCount(); table->insertRow(row);
        auto *file = new QTableWidgetItem(name.mid(2).toUpper());
        file->setData(Qt::UserRole, name); table->setItem(row, 0, file);
        auto *size = new QTableWidgetItem;
        const auto records = (entry["size"].toInteger() + 127) / 128;
        auto *recordCount = new QTableWidgetItem;
        recordCount->setData(Qt::DisplayRole, records);
        table->setItem(row, 4, recordCount);
        size->setData(Qt::DisplayRole, records * 128);
        for (const auto &field : QList<QPair<int, QString>>{{5, "ctime"}, {6, "mtime"}, {7, "atime"}}) {
            const auto seconds = entry[field.second].toInteger();
            auto *timestamp = new QTableWidgetItem;
            if (seconds > 0) timestamp->setData(Qt::DisplayRole, QDateTime::fromSecsSinceEpoch(seconds));
            table->setItem(row, field.first, timestamp);
        }
        table->setItem(row, 1, size);
        const int bits = entry["attr"].toInt(); QStringList flags;
        for (const auto &flag : QList<QPair<int,QString>>{{1,"F1"},{2,"F2"},{4,"F3"},{8,"F4"},{256,"Read-only"},{512,"System"},{1024,"Archive"}})
            if (bits & flag.first) flags << flag.second;
        auto *user = new QTableWidgetItem;
        user->setData(Qt::DisplayRole, name.left(2).toInt());
        table->setItem(row, 3, user);
        table->setItem(row, 2, new QTableWidgetItem(flags.isEmpty() ? "—" : flags.join(", ")));
    }
    table->setSortingEnabled(true);
    if (table->rowCount() && currentArea >= 0) table->selectRow(0);
    setBusy(busy); updateSelectionStatus();
}
void Window::updateDiskSpace(const QJsonObject &result) {
    const auto free = result["free_bytes"].toInteger(-1);
    const auto total = result["total_bytes"].toInteger(-1);
    diskSpace->setVisible(free >= 0 && total >= free);
    if (free >= 0 && total >= free) {
        diskSpace->setText(QString("%1/%2 KiB free")
            .arg(QLocale().toString(free / 1024.0, 'f', free % 1024 ? 1 : 0),
                 QLocale().toString(total / 1024.0, 'f', total % 1024 ? 1 : 0)));
    }
}

void Window::updateSelectionStatus() {
    if (busy || session.isEmpty() || !userAreas->currentItem()) return;
    const auto rows = table->selectionModel()->selectedRows();
    qint64 bytes = 0;
    for (const auto &row : rows) bytes += table->item(row.row(), 1)->data(Qt::DisplayRole).toLongLong();
    status->setText(QString("%1 · %2 of %3 files selected · %4 bytes")
        .arg(userAreas->currentRow() == 0 ? QString("All user areas") :
            QString("User area %1").arg(userAreas->currentItem()->data(Qt::UserRole).toInt()))
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
static constexpr qint64 previewLimit = 1024 * 1024;

void Window::viewFile(QString mode) {
    const auto rows = table->selectionModel()->selectedRows();
    if (busy || session.isEmpty() || rows.size() != 1) return;
    const auto name = table->item(rows.first().row(), 0)->data(Qt::UserRole).toString();
    auto directory = std::make_unique<QTemporaryDir>();
    if (!directory->isValid()) {
        status->setText("Cannot create private text preview.");
        return;
    }
    previewName = name;
    previewMode = std::move(mode);
    previewPane = exportPane == userAreas ? static_cast<QWidget *>(userAreas) : table;
    previewDirectory = std::move(directory);
    begin({{"op", "preview"}, {"session", session}, {"name", name},
           {"destination", previewDirectory->filePath("preview")}});
}

static QString previewText(QByteArray bytes, bool truncated, bool highBit, bool *binary = nullptr) {
    if (highBit) for (auto &byte : bytes) byte = char(byte & 0x7f);
    const auto eof = bytes.indexOf('\x1a');
    if (eof >= 0 && !truncated &&
        ((eof > 0 && eof / 128 == (bytes.size() - 1) / 128) ||
         std::all_of(bytes.cbegin() + eof, bytes.cend(), [](char value) {
            return value == '\x1a' || value == '\0' || value == ' ';
        }))) bytes.truncate(eof);

    QStringDecoder decode(QStringDecoder::Utf8, truncated ? QStringDecoder::Flag::Default :
                                               QStringDecoder::Flag::Stateless);
    QString text = decode(bytes);
    // Some CP/M text has the high bit set on the LF byte of a CRLF pair.
    if (decode.hasError() && bytes.contains("\r\x8a")) {
        bytes.replace("\r\x8a", "\r\n");
        decode.resetState();
        text = decode(bytes);
    }
    const bool controls = std::any_of(text.cbegin(), text.cend(), [](QChar value) {
        return value.category() == QChar::Other_Control &&
               value != '\r' && value != '\n' && value != '\t' && value != '\a';
    });
    const bool unsupported = controls || decode.hasError();
    if (binary) *binary = unsupported;
    if (unsupported)
        return "This file contains binary data or unsupported text encoding.";
    return text.replace("\r\n", "\n").replace('\r', '\n').replace(QChar(0x07), QChar(0x2407));
}

static QString hexRow(const QByteArray &bytes, qsizetype offset, int width) {
    static constexpr char digits[] = "0123456789ABCDEF";
    QString row = QString("%1  ").arg(offset, 8, 16, QChar('0')).toUpper();
    QString ascii;
    ascii.reserve(width);
    for (int i = 0; i < width; ++i) {
        if (i && i % 8 == 0) row += ' ';
        if (offset + i < bytes.size()) {
            const auto byte = static_cast<unsigned char>(bytes[offset + i]);
            row += QLatin1Char(digits[byte >> 4]);
            row += QLatin1Char(digits[byte & 15]);
            ascii += (byte >= 32 && byte < 127) ? QLatin1Char(char(byte)) : QLatin1Char('.');
            row += ' ';
        } else {
            row += "   ";
        }
    }
    return row + " |" + ascii + '|';
}

class PreviewTextEdit : public QPlainTextEdit {
public:
    std::function<void()> viewportResized;
    bool hex = false;
    bool highBit = false;
protected:
    bool viewportEvent(QEvent *event) override {
        const auto handled = QPlainTextEdit::viewportEvent(event);
        if (event->type() == QEvent::Resize && viewportResized) viewportResized();
        return handled;
    }
};

void Window::showFilePreview(bool truncated) {
    auto directory = std::move(previewDirectory);
    if (!directory) { fail("Missing private text preview."); return; }
    QFile file(directory->filePath("preview"));
    if (!file.open(QIODevice::ReadOnly)) { fail("Cannot read the text preview."); return; }
    auto bytes = file.read(previewLimit + 1);
    if (file.error() != QFile::NoError || bytes.size() > previewLimit) {
        fail("Invalid text preview size.");
        return;
    }
    file.close();
    const auto name = previewName.mid(2);
    const auto title = QString("%1 · User area %2").arg(name).arg(previewName.left(2).toInt());
    inspectPreview(bytes, name, title, previewPane, truncated, previewMode);
}

void Window::inspectPreview(const QByteArray &bytes, const QString &name, const QString &title,
                            QWidget *pane, bool truncated, QString mode, int member,
                            QPointer<QWidget> directoryView,
                            std::function<void(bool, QByteArray, QByteArray)> extracted) {
    auto work = std::make_shared<QTemporaryDir>();
    const auto fallback = [this, bytes, title, pane, truncated, member, directoryView, extracted] {
        if (directoryView) directoryView->setEnabled(true);
        if (extracted) {
            extracted(false, {}, {});
        } else if (member >= 0) {
            QMessageBox::warning(this, "Cannot open member", "The archive worker could not read this member.");
        } else if (!closing) {
            showPreview(bytes, {}, {{"notice", "Archive worker could not inspect the file. Showing original bytes."}},
                        title, pane, truncated);
            emit completed(true);
        }
    };
    QFile input(work->filePath("input"));
    if (!work->isValid() || !input.open(QIODevice::WriteOnly) || input.write(bytes) != bytes.size()) {
        fallback();
        return;
    }
    input.close();
    auto *worker = new QProcess(this);
    QTimer::singleShot(10000, worker, [worker] { if (worker->state() != QProcess::NotRunning) worker->kill(); });
    connect(worker, &QProcess::errorOccurred, this, [worker, fallback](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        fallback();
        worker->deleteLater();
    });
    connect(worker, &QProcess::finished, this, [=](int code, QProcess::ExitStatus exit) {
        worker->deleteLater();
        if (directoryView) directoryView->setEnabled(true);
        if (closing) return;
        const auto output = worker->readAllStandardOutput();
        const auto document = output.size() <= 512 * 1024 ? QJsonDocument::fromJson(output) : QJsonDocument();
        if (exit != QProcess::NormalExit || code || !document.isObject()) { fallback(); return; }
        auto result = document.object();
        if (member >= 0) result.insert("memberExportName", result.value("name").toString());
        QByteArray original = bytes, decoded;
        if (member >= 0) {
            QFile file(work->filePath("original"));
            if (!file.open(QIODevice::ReadOnly)) { fallback(); return; }
            original = file.read(previewLimit + 1);
            if (file.error() != QFile::NoError) { fallback(); return; }
        }
        QFile file(work->filePath("decoded"));
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly)) { fallback(); return; }
            decoded = file.read(previewLimit + 1);
            if (file.error() != QFile::NoError) { fallback(); return; }
            if (decoded.isNull()) decoded = QByteArray("");
        }
        if (original.size() > previewLimit || decoded.size() > previewLimit) { fallback(); return; }
        if (extracted) { extracted(true, original, decoded); return; }
        const auto displayTitle = member >= 0 ? result["name"].toString(title) : title;
        if (!result["kind"].toString().isEmpty())
            showArchive(original, decoded, result, displayTitle, pane, directoryView);
        else showPreview(original, decoded, result, displayTitle, pane, truncated, directoryView);
        if (member < 0) emit completed(true);
    });
    if (directoryView) directoryView->setEnabled(false);
    worker->start(python, {"-I", "-B", root + "/prototype/archive_view.py", work->path(),
                           name, mode, member < 0 ? QString("-") : QString::number(member),
                           truncated ? "1" : "0"});
}

void Window::showArchive(QByteArray bytes, QByteArray decoded, const QJsonObject &result,
                         const QString &title, QWidget *pane, QPointer<QWidget> returnTo) {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(620, 420);
    auto *layout = new QVBoxLayout(dialog);
    const auto entries = result["members"].toArray();
    auto *members = new QTableWidget(entries.size(), 3);
    members->setObjectName("archiveMembers");
    members->setHorizontalHeaderLabels({"Member", "Size (bytes)", "Stored (bytes)"});
    members->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    members->setSelectionBehavior(QAbstractItemView::SelectRows);
    members->setSelectionMode(QAbstractItemView::ExtendedSelection);
    members->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int row = 0; row < entries.size(); ++row) {
        const auto entry = entries[row].toObject();
        members->setItem(row, 0, new QTableWidgetItem(entry["name"].toString()));
        members->setItem(row, 1, new QTableWidgetItem(QString::number(entry["size"].toInteger())));
        members->setItem(row, 2, new QTableWidgetItem(QString::number(entry["stored"].toInteger())));
    }
    if (members->rowCount()) members->setCurrentCell(0, 0);
    layout->addWidget(members);
    const auto name = result["name"].toString();
    const auto data = decoded.isNull() ? bytes : decoded;
    const auto openMember = [=](int row, int) {
        if (members->isEnabled())
            inspectPreview(data, name, members->item(row, 0)->text(), pane, false, "auto", row, members);
    };
    connect(members, &QTableWidget::cellActivated, dialog, openMember);
    connect(members, &QTableWidget::cellDoubleClicked, dialog, openMember);
    auto *viewMember = new QShortcut(QKeySequence(Qt::Key_V), members);
    viewMember->setContext(Qt::WidgetWithChildrenShortcut);
    connect(viewMember, &QShortcut::activated, dialog, [=] {
        if (members->currentRow() >= 0) openMember(members->currentRow(), 0);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *stream = new QComboBox;
    stream->setObjectName("archiveExportStream");
    stream->setAccessibleName("Archive export byte stream");
    stream->addItems({"Decompressed when available", "Original bytes"});
    layout->addWidget(stream);
    auto *exportStatus = new QLabel;
    exportStatus->setObjectName("archiveExportStatus");
    layout->addWidget(exportStatus);
    auto *exportMembers = buttons->addButton("Export selected…", QDialogButtonBox::ActionRole);
    exportMembers->setObjectName("exportArchiveSelection");
    exportMembers->setAutoDefault(false);
    const auto updateExport = [=] { exportMembers->setEnabled(!members->selectionModel()->selectedRows().isEmpty()); };
    connect(members->selectionModel(), &QItemSelectionModel::selectionChanged, dialog, updateExport);
    updateExport();
    connect(exportMembers, &QPushButton::clicked, dialog, [=] {
        auto selected = members->selectionModel()->selectedRows();
        if (selected.isEmpty()) return;
        std::sort(selected.begin(), selected.end(), [](const QModelIndex &a, const QModelIndex &b) {
            return a.row() < b.row();
        });
        const auto previous = settings->value("exports/directory", QDir::homePath()).toString();
        const auto path = QFileDialog::getExistingDirectory(dialog, "Export archive members", previous);
        if (path.isEmpty()) return;
        settings->setValue("exports/directory", path);
        const QDir destination(path);
        const bool preferDecoded = stream->currentIndex() == 0;
        auto *next = new QTimer(dialog);
        next->setSingleShot(true);
        const QPointer<QTimer> pending(next);
        members->setEnabled(false);
        exportMembers->setEnabled(false);
        stream->setEnabled(false);
        exportStatus->setText("Exporting archive members…");
        connect(next, &QTimer::timeout, dialog, [=, index = 0, written = 0, skipped = 0,
                                               failures = QStringList{}]() mutable {
            while (index < selected.size()) {
                const int row = selected[index++].row();
                const auto memberName = entries[row].toObject()["name"].toString();
                const auto hostName = filenamePolicy.translate(memberName);
                if (!FilenamePolicy::validateHostName(hostName).isEmpty()) {
                    failures << memberName;
                    continue;
                }
                const auto existing = destination.entryList(QDir::AllEntries | QDir::Hidden | QDir::System);
                if (std::any_of(existing.begin(), existing.end(), [&](const QString &entry) {
                    return entry.compare(hostName, Qt::CaseInsensitive) == 0;
                })) {
                    ++skipped;
                    continue;
                }
                inspectPreview(data, name, memberName, pane, false, "auto", row, {},
                    [&written, &failures, pending, dialog, target = destination.filePath(hostName), memberName, preferDecoded]
                    (bool ok, QByteArray original, QByteArray decoded) {
                        if (!pending || !dialog->isVisible()) return;
                        QFile output(target);
                        const auto &payload = preferDecoded && !decoded.isNull() ? decoded : original;
                        if (!ok || !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                            failures << memberName;
                        } else {
                            const bool saved = output.write(payload) == payload.size() && output.flush();
                            output.close();
                            if (saved) ++written;
                            else { output.remove(); failures << memberName; }
                        }
                        pending->start(0);
                    });
                return;
            }
            exportStatus->setText(QString("%1 exported · %2 skipped (already exist) · %3 failed")
                                  .arg(written).arg(skipped).arg(failures.size()));
            exportStatus->setToolTip(failures.join('\n'));
            stream->setEnabled(true);
            members->setEnabled(true);
            members->setFocus();
            updateExport();
            next->deleteLater();
        });
        next->start(0);
    });
    auto *raw = buttons->addButton("View archive bytes…", QDialogButtonBox::ActionRole);
    connect(raw, &QPushButton::clicked, dialog, [=] { showPreview(bytes, decoded, {{"memberExportName", result["memberExportName"]}},
                                                             title, pane, false, members); });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    connect(new QShortcut(QKeySequence(Qt::Key_Q), dialog), &QShortcut::activated, dialog, &QDialog::close);
    members->setFocus();
    finishPreviewDialog(dialog, pane, returnTo);
}

void Window::showPreview(QByteArray bytes, QByteArray decoded, const QJsonObject &result,
                         const QString &title, QWidget *pane, bool truncated, QPointer<QWidget> returnTo) {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    auto *layout = new QVBoxLayout(dialog);
    if (truncated) layout->addWidget(new QLabel("Preview limited to the first 1 MiB."));
    const auto notice = result["notice"].toString();
    if (!notice.isEmpty() && !truncated) {
        auto *label = new QLabel(notice);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        layout->addWidget(label);
    }
    auto *viewer = new PreviewTextEdit;
    viewer->setObjectName("filePreviewText");
    viewer->setReadOnly(true);
    viewer->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(viewer);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *mode = new QPushButton("TEXT/hex (T)");
    mode->setObjectName("previewMode");
    mode->setAutoDefault(false);
    auto *highBit = new QPushButton("Highbit: OFF (7)");
    highBit->setObjectName("previewHighBit");
    highBit->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    auto *controls = new QHBoxLayout;
    controls->addWidget(mode);
    controls->addWidget(highBit);
    QComboBox *stream = nullptr;
    if (!decoded.isNull()) {
        stream = new QComboBox;
        stream->setObjectName("previewStream");
        stream->addItems({"Decompressed", "Original"});
        controls->addWidget(stream);
    }
    const auto activeBytes = [=]() -> const QByteArray & {
        return stream && stream->currentIndex() == 0 ? decoded : bytes;
    };
    const auto memberName = result["memberExportName"].toString();
    if (!memberName.isEmpty()) {
        auto *save = new QPushButton("Export member…");
        save->setObjectName("exportArchiveMember");
        save->setAutoDefault(false);
        controls->addWidget(save);
        connect(save, &QPushButton::clicked, dialog, [=] {
            const auto hostName = filenamePolicy.translate(memberName);
            const auto problem = FilenamePolicy::validateHostName(hostName);
            if (!problem.isEmpty()) {
                QMessageBox::warning(dialog, "Cannot export member", problem);
                return;
            }
            const auto directory = settings->value("exports/directory", QDir::homePath()).toString();
            const QString caption = stream
                ? QString("Export member (%1)").arg(stream->currentText())
                : QString("Export archive member");
            const auto destination = QFileDialog::getSaveFileName(dialog, caption,
                QDir(directory).filePath(hostName), "All files (*)");
            if (destination.isEmpty()) return;
            const QFileInfo info(destination);
            if (info.isSymLink() || (info.exists() && !info.isFile()) ||
                !FilenamePolicy::validateHostName(info.fileName()).isEmpty()) {
                QMessageBox::warning(dialog, "Cannot export member", "Choose a valid regular file destination.");
                return;
            }
            const auto &payload = activeBytes();
            QSaveFile output(destination);
            if (!output.open(QIODevice::WriteOnly) || output.write(payload) != payload.size() || !output.commit()) {
                QMessageBox::warning(dialog, "Cannot export member", output.errorString());
                return;
            }
            settings->setValue("exports/directory", info.absolutePath());
        });
    }
    controls->addStretch();
    controls->addWidget(buttons);
    layout->addLayout(controls);

    const auto rowPixels = [viewer](int width) {
        return viewer->fontMetrics().horizontalAdvance(hexRow(QByteArray(width, 'W'), 0, width))
            + 2 * viewer->document()->documentMargin();
    };
    const auto margins = layout->contentsMargins();
    dialog->resize(qMax(680, qCeil(rowPixels(16)) + margins.left() + margins.right() +
                               viewer->style()->pixelMetric(QStyle::PM_ScrollBarExtent) +
                               2 * viewer->frameWidth()), 480);
    auto *resizeTimer = new QTimer(dialog);
    resizeTimer->setSingleShot(true);
    resizeTimer->setInterval(40);
    auto renderHex = [viewer, rowPixels, activeBytes]() {
        const auto &bytes = activeBytes();
        int next = 8;
        for (int candidate : {16, 32, 64})
            if (rowPixels(candidate) <= viewer->viewport()->width()) next = candidate;
        const int width = viewer->property("hexWidth").toInt();
        if (next == width) return;
        const auto offset = width * viewer->cursorForPosition(QPoint(0, 0)).blockNumber();
        viewer->setProperty("hexWidth", next);
        QString text;
        for (qsizetype position = 0; position < bytes.size(); position += next) {
            if (position) text += '\n';
            text += hexRow(bytes, position, next);
        }
        viewer->setPlainText(text);
        viewer->verticalScrollBar()->setValue(offset / next);
    };
    connect(resizeTimer, &QTimer::timeout, dialog, renderHex);
    viewer->viewportResized = [viewer, resizeTimer] {
        if (viewer->hex) resizeTimer->start();
    };
    const auto renderText = [viewer, activeBytes, truncated] {
        viewer->setPlainText(previewText(activeBytes(), truncated, viewer->highBit));
    };
    connect(highBit, &QPushButton::clicked, dialog, [viewer, highBit, renderText] {
        viewer->highBit = !viewer->highBit;
        highBit->setText(viewer->highBit ? "Highbit: ON (7)" : "Highbit: OFF (7)");
        renderText();
    });
    connect(mode, &QPushButton::clicked, dialog, [=] {
        const bool hex = viewer->hex = !viewer->hex;
        mode->setText(hex ? "text/HEX (T)" : "TEXT/hex (T)");
        highBit->setEnabled(!hex);
        viewer->setLineWrapMode(hex ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
        viewer->setVerticalScrollBarPolicy(hex ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAsNeeded);
        if (hex) resizeTimer->start(0);
        else {
            resizeTimer->stop();
            viewer->setProperty("hexWidth", 0);
            renderText();
        }
    });
    for (const auto &[key, button] : {std::pair{Qt::Key_T, mode}, std::pair{Qt::Key_7, highBit}})
        connect(new QShortcut(QKeySequence(key), dialog), &QShortcut::activated, button, &QPushButton::click);
    connect(new QShortcut(QKeySequence(Qt::Key_Q), dialog), &QShortcut::activated, dialog, &QDialog::close);
    if (stream) connect(stream, &QComboBox::currentIndexChanged, dialog, [=] {
        if (viewer->hex) {
            viewer->setProperty("hexWidth", 0);
            resizeTimer->start(0);
        } else renderText();
    });
    bool binary = false;
    previewText(activeBytes(), truncated, false, &binary);
    if (binary) mode->click();
    else renderText();
    viewer->setFocus();
    finishPreviewDialog(dialog, pane, returnTo);
}

void Window::finishPreviewDialog(QDialog *dialog, QWidget *pane, QPointer<QWidget> returnTo) {
    if (!returnTo) pane->setFocus(Qt::OtherFocusReason);
    connect(dialog, &QDialog::finished, this, [this, pane, returnTo] {
        QTimer::singleShot(0, this, [pane, returnTo] {
            auto *target = returnTo && returnTo->isVisible() && returnTo->isEnabled()
                ? returnTo.data() : pane;
            if (!target->isVisible() || !target->isEnabled()) return;
            target->window()->activateWindow();
            target->setFocus(Qt::OtherFocusReason);
        });
    });
    dialog->show();
    dialog->activateWindow();
    updateSelectionStatus();
}

void Window::updateExportAction() {
    const bool available = !busy && !session.isEmpty() &&
        ((exportPane == userAreas && userAreas->currentItem()) ||
         (exportPane == table && !table->selectionModel()->selectedRows().isEmpty()));
    exportAction->setEnabled(available);
    exportButton->setEnabled(available);
}

void Window::chooseContextExport() {
    if (!exportAction->isEnabled()) return;
    if (exportPane == userAreas) chooseBatch(true);
    else if (exportPane == table) chooseExport();
}

void Window::chooseExport() {
    const auto selected = table->selectionModel()->selectedRows();
    if (busy || session.isEmpty() || selected.isEmpty()) return;
    if(userAreas->currentRow() == 0 || selected.size()>1) { chooseBatch(false); return; }
    QString directory = settings->value("exports/directory",QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
    if (!QDir(directory).exists()) directory = QDir::homePath();
    QString problem;
    const QString hostName = selectedExportName(&problem);
    if (!problem.isEmpty()) { status->setText(problem); return; }
    const QString suggested = QDir(directory).filePath(hostName);
    const auto destination = QFileDialog::getSaveFileName(this, "Export 1 selected file", suggested,
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
           {"destination", QFileInfo(destination).absoluteFilePath()},
           {"text", settings->value("textExport", false).toBool()}});
}
void Window::importFiles(const QStringList &paths) {
    if (!paths.isEmpty()) chooseEdit("import", paths);
}

void Window::offerFiles(bool drag, bool cut) {
    if (busy || session.isEmpty()) return;
    const auto entries = exportEntries(false);
    if (entries.isEmpty()) return;
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/file-transfers";
    if (!QDir().mkpath(cache)) return;
    const QDir transferCache(cache);
    const auto cutoff = QDateTime::currentDateTime().addDays(-7);
    for (const auto &old : transferCache.entryInfoList({"transfer-*"}, QDir::Dirs | QDir::NoSymLinks))
        if (old.lastModified() < cutoff) QDir(old.absoluteFilePath()).removeRecursively();
    auto staging = std::make_shared<QTemporaryDir>(transferCache.filePath("transfer-XXXXXX"));
    if (!staging->isValid()) return;
    auto plan = planExport(entries, staging->path(), -1, filenamePolicy);
    if (!plan.error.isEmpty()) {
        status->setText(plan.error);
        return;
    }


    const auto revision = imageRevision;
    const auto originSession = session;
    if (drag) {
        // The native drag must start before returning from the view's mouse gesture.
        // Service broker replies while staging, but defer further user input.
        QEventLoop stagingLoop;
        bool ready = false;
        const auto completion = connect(this, &Window::completed, &stagingLoop, [&](bool ok) {
            ready = ok;
            stagingLoop.quit();
        });
        startBatch(plan, false);
        if (batchActive) stagingLoop.exec(QEventLoop::ExcludeUserInputEvents);
        disconnect(completion);
        if (!ready || session != originSession || imageRevision != revision) return;

        auto *mime = new TransferFiles;
        QList<QUrl> urls;
        for (const auto &item : plan.items) {
            urls.append(QUrl::fromLocalFile(item.destination));
            mime->names.insert(item.destination, item.name);
        }
        mime->setUrls(urls);
        mime->origin = this;
        mime->revision = revision;
        mime->session = originSession;

        // A native receiver can read these URLs after the gesture has ended.
        staging->setAutoRemove(false);
        auto *gesture = new QDrag(table);
        gesture->setMimeData(mime);
        gesture->exec(Qt::CopyAction);
        gesture->deleteLater();
        return;
    }

    connect(this, &Window::completed, this, [this, cut, staging, plan, revision, originSession](bool ok) {
        if (!ok) return;
        QList<QUrl> urls;
        for (const auto &item : plan.items) urls.append(QUrl::fromLocalFile(item.destination));
        auto *mime = new TransferFiles;
        mime->setUrls(urls);
        if (cut) {
            mime->origin = this;
            mime->revision = revision;
            mime->session = originSession;
            for (const auto &item : plan.items) mime->names.insert(item.destination, item.name);
        }
        // Clipboard consumers can resolve the URLs after this process exits.
        staging->setAutoRemove(false);
        if (cut) {
            const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
            mime->setData(cutMime, token.toLatin1());
            auto *receiptListener = new QTimer(this);
            receiptListener->setInterval(500);
            connect(receiptListener, &QTimer::timeout, this,
                [this, receiptListener, token, plan, revision, originSession] {
                const auto *current = QApplication::clipboard()->mimeData();
                if (current && current->data(cutMime) == token.toLatin1()) return;
                receiptListener->stop();
                receiptListener->deleteLater();
                if (!current || busy || session != originSession || imageRevision != revision) return;
                const auto bytes = current->data(receiptMime);
                if (bytes.size() > 262144) return;
                const auto receipt = QJsonDocument::fromJson(bytes).object();
                if (receipt["token"].toString() != token || !receipt["files"].isArray()) return;
                const auto files = receipt["files"].toArray();
                QJsonArray names;
                for (const auto &item : plan.items)
                    if (files.contains(item.destination)) names.append(item.name);
                if (names.isEmpty() || names.size() != files.size()) return;
                if (QMessageBox::question(this, "Complete file move",
                    QString("The receiving instance confirmed importing %1 file(s). Remove those files from the source working image?")
                        .arg(names.size()), QMessageBox::Yes | QMessageBox::Cancel,
                        QMessageBox::Cancel) != QMessageBox::Yes) return;
                if (busy || session != originSession || imageRevision != revision || !confirmWrite()) return;
                begin({{"op","edit"},{"session",originSession},{"action","delete_many"},
                       {"names",names},{"confirmed",true}});
            });
            receiptListener->start();
        }
        QApplication::clipboard()->setMimeData(mime);
    }, Qt::SingleShotConnection);
    startBatch(plan, false);
}

bool Window::eventFilter(QObject *watched, QEvent *event) {
    if ((watched == userAreas || watched == table) && event->type() == QEvent::FocusIn) {
        exportPane = static_cast<QWidget *>(watched);
        updateExportAction();
    }
    if ((watched == userAreas || watched == table) &&
        event->type() == QEvent::KeyPress && QApplication::focusWidget() == watched) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (watched == table &&
            (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier &&
            (key->key() == Qt::Key_V || key->key() == Qt::Key_Q || key->key() == Qt::Key_Z ||
             key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            if (key->key() == Qt::Key_Q) viewFile("sq");
            else if (key->key() == Qt::Key_Z) viewFile("cr");
            else viewFileAction->trigger();
            return true;
        }
        if (key->key() == Qt::Key_X && key->modifiers() == Qt::NoModifier) {
            exportAction->trigger();
            return true;
        }
        const bool forward = key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier;
        const bool backward = (key->key() == Qt::Key_Backtab ||
                               (key->key() == Qt::Key_Tab && key->modifiers() == Qt::ShiftModifier)) &&
                              !(key->modifiers() & ~Qt::ShiftModifier);
        if (forward || backward) {
            auto *other = watched == table ? static_cast<QWidget *>(userAreas) : table;
            if (other->isEnabled() && other->isVisible()) {
                other->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
                return true;
            }
        }
    }
    if (watched == table->viewport() || watched == userAreas->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *drag = static_cast<QDropEvent *>(event);
            if (!busy && !session.isEmpty() && drag->mimeData()->hasUrls()) {
                drag->setDropAction(Qt::CopyAction);
                drag->accept();
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto *drop = static_cast<QDropEvent *>(event);
            QStringList paths;
            for (const auto &url : drop->mimeData()->urls())
                if (url.isLocalFile()) paths.append(url.toLocalFile());
            if (!busy && !session.isEmpty() && !paths.isEmpty()) {
                if (watched == userAreas->viewport()) {
                    if (auto *item = userAreas->itemAt(drop->position().toPoint()))
                        userAreas->setCurrentItem(item);
                }
                drop->setDropAction(Qt::CopyAction);
                drop->accept();
                const auto *local = dynamic_cast<const TransferFiles *>(drop->mimeData());
                auto transfer = std::make_shared<TransferFiles>();
                if (local && drop->modifiers().testFlag(Qt::ShiftModifier)) {
                    transfer->origin = local->origin;
                    transfer->revision = local->revision;
                    transfer->session = local->session;
                    transfer->names = local->names;
                }
                QTimer::singleShot(0, this, [this, paths, transfer] {
                    chooseEdit("import", paths, transfer.get());
                });
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
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
void Window::exportFlat(const QJsonArray &entries, const QString &directory, const QSet<QString> &textFiles) {
    auto staging = std::make_shared<QTemporaryDir>();
    if (!staging->isValid()) return;
    ExportPlan plan;
    plan.directory = staging->path();
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        ExportItem item;
        item.name = entry["name"].toString();
        item.size = entry["size"].toInteger();
        item.destination = staging->filePath(QString::number(plan.items.size()));
        item.text = textFiles.contains(item.name);
        plan.items.append(item);
    }
    connect(this, &Window::completed, this, [this, staging, plan, directory](bool ok) {
        if (!ok) return;
        QStringList report;
        QString applyAll;
        for (const auto &item : plan.items) {
            QFile input(item.destination);
            if (!input.open(QIODevice::ReadOnly)) { report << item.name + " — Read failed"; continue; }
            const auto payload = input.readAll();
            auto name = filenamePolicy.translate(item.name.mid(2));
            if (!FilenamePolicy::validateHostName(name).isEmpty()) {
                report << item.name + " — Invalid destination name";
                continue;
            }
            QString target;
            while (true) {
                const QDir folder(directory);
                target = folder.filePath(name);
                for (const auto &existing : folder.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
                    if (existing.compare(name, Qt::CaseInsensitive) == 0) {
                        target = folder.filePath(existing);
                        break;
                    }
                }
                const QFileInfo info(target);
                if (!info.exists() && !info.isSymLink()) {
                    if (QFile::copy(item.destination, target)) report << item.name + " — Exported";
                    else report << item.name + " — Write failed";
                    break;
                }
                QFile current(target);
                const bool regular = info.isFile() && !info.isSymLink();
                if (regular && info.size() == payload.size() && current.open(QIODevice::ReadOnly)
                    && current.readAll() == payload) {
                    report << item.name + " — Consolidated identical contents";
                    break;
                }
                current.close();
                QString choice = applyAll;
                if (choice.isEmpty()) {
                    QMessageBox prompt(QMessageBox::Warning, "Export conflict", name + " already exists.",
                                       QMessageBox::Cancel, this);
                    auto *overwrite = prompt.addButton("Overwrite", QMessageBox::DestructiveRole);
                    overwrite->setEnabled(regular);
                    auto *skip = prompt.addButton("Skip", QMessageBox::RejectRole);
                    auto *rename = prompt.addButton("Rename", QMessageBox::ActionRole);
                    auto *all = new QCheckBox("Apply to All");
                    prompt.setCheckBox(all);
                    prompt.setDefaultButton(skip);
                    prompt.exec();
                    if (prompt.clickedButton() == overwrite) choice = "overwrite";
                    else if (prompt.clickedButton() == skip) choice = "skip";
                    else if (prompt.clickedButton() == rename) choice = "rename";
                    else choice = "cancel";
                    if (all->isChecked() && (choice == "overwrite" || choice == "skip")) applyAll = choice;
                }
                if (choice == "rename") {
                    bool accepted = false;
                    const auto replacement = QInputDialog::getText(this, "Rename export", "New filename",
                        QLineEdit::Normal, name, &accepted);
                    if (!accepted) choice = "cancel";
                    else if (replacement.compare(name, Qt::CaseInsensitive) == 0 ||
                             !FilenamePolicy::validateHostName(replacement).isEmpty()) {
                        QMessageBox::warning(this, "Invalid export name", "Enter a distinct valid host filename.");
                        continue;
                    } else { name = replacement; continue; }
                }
                if (choice == "cancel") report << item.name + " — Cancelled";
                else if (choice == "skip" || !regular) report << item.name + " — Skipped";
                else {
                    QSaveFile output(target);
                    if (output.open(QIODevice::WriteOnly) && output.write(payload) == payload.size() && output.commit())
                        report << item.name + " — Exported";
                    else report << item.name + " — Write failed";
                }
                break;
            }
            if (report.last().endsWith(" — Cancelled")) break;
        }
        batchReport = report.join('\n');
        status->setText("Flat export finished. See Export report.");
    }, Qt::SingleShotConnection);
    startBatch(plan, false);
}

void Window::chooseBatch(bool wholeArea) {
    if(busy || session.isEmpty() || !userAreas->currentItem()) return;
    const bool allAreas = wholeArea && userAreas->currentRow() == 0;
    const auto entries = allAreas ? directoryEntries : exportEntries(wholeArea);
    if(!wholeArea && entries.isEmpty()) return;
    QString previous=settings->value("exports/directory",QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
    if(!QDir(previous).exists()) previous=QDir::homePath();
    const int area = wholeArea && !allAreas ? userAreas->currentItem()->data(Qt::UserRole).toInt() : -1;
    const auto title = allAreas ? QString("Export all user areas") :
        wholeArea ? QString("Export user area %1").arg(area) :
        QString("Export %1 selected file(s)").arg(entries.size());
    QSet<QString> textFiles;
    if (!entries.isEmpty()) {
        QDialog review(this);
        review.setObjectName("batchTextExportReview");
        review.setWindowTitle("Review text conversion");
        auto *layout = new QVBoxLayout(&review);
        layout->addWidget(new QLabel("Check the files to convert to text. Unchecked files keep their original bytes."));
        auto *files = new QListWidget;
        files->setObjectName("batchTextExportFiles");
        for (const auto &value : entries) {
            const auto name = value.toObject()["name"].toString();
            auto *item = new QListWidgetItem(QString("%1 (user %2)").arg(name.mid(2)).arg(name.left(2).toInt()), files);
            item->setData(Qt::UserRole, name);
            item->setCheckState(Qt::Unchecked);
        }
        layout->addWidget(files);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &review, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &review, &QDialog::reject);
        layout->addWidget(buttons);
        if (review.exec() != QDialog::Accepted) return;
        for (int row = 0; row < files->count(); ++row) {
            const auto *item = files->item(row);
            if (item->checkState() == Qt::Checked) textFiles.insert(item->data(Qt::UserRole).toString());
        }
    }
    while(true) {
        const auto directory = QFileDialog::getExistingDirectory(this, title, previous);
        if(directory.isEmpty()) return;
        previous=directory;
        if (!wholeArea && userAreas->currentRow() == 0) { exportFlat(entries, directory, textFiles); return; }
        auto plan = planExport(entries, directory, area, filenamePolicy, allAreas);
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
        for (auto &item : plan.items) item.text = textFiles.contains(item.name);
        startBatch(plan,skip); return;
    }
}
void Window::exportSelection(const QString &directory,bool skipExisting) {
    if(busy || session.isEmpty()) return;
    const auto entries=exportEntries(false);
    if(entries.isEmpty()) return;
    if (userAreas->currentRow() == 0) { exportFlat(entries, directory); return; }
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
    for (const auto &folder : plan.areaDirectories) {
        if (!QDir(folder).exists() && !QDir().mkdir(folder)) {
            error = "Cannot create the numbered user-area folder.";
            status->setText(error);
            emit completed(false);
            return;
        }
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
    begin({{"op","export"},{"session",session},{"name",item.name},{"destination",item.destination},{"text",item.text}});
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
    for(int area=1;area<userAreas->count();++area)
        if(userAreas->item(area)->data(Qt::UserRole+1).toInt()>0) ++populated;
    const auto details=QString("Source image: %1\nImage layout: %2\nFormat identifier: %3\nContainer: %4\nFiles: %5\nPopulated user areas: %6 of %9\n\nSnapshot SHA-256: %7\nRead-only normalization: %8\n\nEdits affect a private copy until you save.\nThe selected definition is an interpretation of the source image.\nExport retains CP/M record padding.")
        .arg(image,formats->text(),format,container).arg(directoryEntries.size()).arg(populated).arg(snapshotHash,imageNote).arg(userAreas->count()-1);
    QDialog dialog(this);dialog.setWindowTitle("Image information");dialog.resize(700,420);
    auto *layout=new QVBoxLayout(&dialog);
    auto *text=new QTextEdit;text->setObjectName("imageInformationText");text->setReadOnly(true);
    text->setPlainText(details+(exportDiagnostics.isEmpty()?QString():"\n\n"+exportDiagnostics));layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    if (container == "imd") text->append("\nIMD comment:\n" + imdComment.toHtmlEscaped());
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.exec();
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
        operation == "preview" ? "Reading file preview…" :
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
    previewDirectory.reset();
    afterSave = {};
    newImagePicker = false;
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
        else if (code == "source_changed") {
            if (operation == "export") {
                session.clear();
                message = "The source image changed. Open it again before exporting.";
            } else message = reply["message"].toString();
        }
        else message = "Cannot complete the operation (" + code + "). " + reply["message"].toString();
        fail(message); return;
    }
    const auto result = reply["result"].toObject();
    const QRegularExpression hash("^[0-9a-f]{64}$");
    auto invalid = [this] { stopReader(); resetImage(); fail("Invalid response schema from the image reader."); };
    if (!reply["result"].isObject()) { invalid(); return; }
    if (operation == "preview") {
        if (!result["truncated"].isBool()) { fail("Invalid text preview response."); return; }
        deadline.stop();
        cancellationDeadline.stop();
        setBusy(false);
        showFilePreview(result["truncated"].toBool());
        return;
    }
    if (operation == "new_image" || operation == "format_image" || operation == "edit" || operation == "imd_comment" || operation == "discard" || operation.startsWith("systems_")) {
        deadline.stop();
        cancellationDeadline.stop();
        if (operation == "new_image") {
            resetImage();
            unpublished = true;
            image = "Untitled";
            format = newDefinition["id"].toString();
            container = "raw";
            source->setText(image);
            formats->setText(format);
            session = result["session"].toString();
            writeConfirmed = true;
            imageGeometry = result["geometry"].toObject();
        } else if (result["closed"].toBool()) resetImage();
        if (operation != "systems_export") ++imageRevision;
        dirty = result["dirty"].toBool();
        if (result["imd_comment"].isString()) imdComment = result["imd_comment"].toString();
        if (operation == "edit" || operation == "discard" || operation == "systems_import") writeConfirmed = dirty;
        if (result["entries"].isArray()) {
            directoryEntries = result["entries"].toArray();
            updateDiskSpace(result);
            updateUserAreas();
            if (!userAreas->currentItem() && !session.isEmpty()) userAreas->setCurrentRow(1);
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
            if (result["sidecar_changed"].toBool() && settings->value("warnSaveSidecar", true).toBool() && QMessageBox::question(this, "Confirm Disk Definition",
                    QString("Record %1 in the image's FDF? This records your interpretation and does not verify or convert the filesystem.").arg(format),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) { afterSave = {}; return; }
            pendingSave["op"] = "save";
            pendingSave["plan"] = result;
            pendingSave["replace"] = replace;
            pendingSave["confirm_sidecar"] = true;
            begin(pendingSave);
        } else {
            dirty = false;
            unpublished = false;
            image = result["destination"].toString();
            container = result["container"].toString();
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
            if (newImagePicker) { chooseFormat({}, false); return; }
            if (!requestedImage.isEmpty()) {
                begin({{"op","inspect"}, {"image",requestedImage}, {"explicit_format",requestedFormat}, {"container",requestedContainer}});
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
        updateDiskSpace(result);
        const auto tail = result["unused_tail_filled"];
        if (!tail.isUndefined() && !tail.isBool()) { invalid(); return; }
        imageNote = tail.toBool() ? "Unused image tail filled with E5 in the temporary snapshot; original unchanged." : "None.";
        snapshotHash=result["source_sha256"].toString();
        imageGeometry = result["geometry"].toObject();
        imdComment = result["imd_comment"].toString();
        const auto definitionHash = result["definition_sha256"].toString();
        if ((retainSource || requestedFormat.isEmpty()) && !activeCatalogue.isEmpty() && hash.match(definitionHash).hasMatch()) {
            settings->setValue(associationKey(image, activeCatalogue),
                               QStringList{snapshotHash, format, definitionHash, container});
        }
        source->setText(QFileInfo(image).fileName()); source->setToolTip(image);
        setWindowTitle("cpmimage — "+QFileInfo(image).fileName());
        updateUserAreas();
        { QSignalBlocker block(userAreas); userAreas->setCurrentItem(userAreas->item(1)); }
        showUserArea();
    } else {
        const double size = result["size"].toDouble(-1);
        if (batchActive) {
            const auto &item = batch.items[batchIndex];
            if (size > item.size || (!item.text && size != item.size)) { invalid(); return; }
        }
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
    if (operation == "open") {
        updateSelectionStatus();
        emit imageOpened(image);
    }
    emit completed(true);
    if (closing) close();
}
void Window::closeEvent(QCloseEvent *event) {
    if (busy) { closing = true; cancel(); event->ignore(); }
    else if (!resolveEdits([this] { close(); })) event->ignore();
    else { process.closeWriteChannel(); event->accept(); emit sessionClosed(); }
}
