// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "window.h"
#include "image_comparison.h"
#include "disk_format_editor.h"
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include "runtime_paths.h"
#include <QTabWidget>
#include <QTabBar>
#include <QColor>
#include <QToolButton>
#include <QMenuBar>
#include <QMenu>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QSettings>
#include <QScopedValueRollback>
#include <utility>
#include <memory>

class Workspace : public QMainWindow {
public:
    explicit Workspace(RuntimePaths paths, const QString &settingsFile = {})
        : runtime(std::move(paths)),
          settings(settingsFile.isEmpty() ? std::make_unique<QSettings>("CPMArchives", "cpmimage")
                                          : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat)) {
        setCentralWidget(tabs);
        setAcceptDrops(true);
        tabs->setTabsClosable(true);
        tabs->setMovable(true);
#ifdef Q_OS_MACOS
        auto palette = tabs->tabBar()->palette();
        const auto base = palette.color(QPalette::Button);
        const auto ink = palette.color(QPalette::ButtonText);
        const QColor selected((base.red() * 4 + ink.red()) / 5,
                              (base.green() * 4 + ink.green()) / 5,
                              (base.blue() * 4 + ink.blue()) / 5);
        palette.setColor(QPalette::Highlight, selected);
        palette.setColor(QPalette::HighlightedText, ink);
        tabs->tabBar()->setPalette(palette);
        tabs->tabBar()->setStyleSheet(
            "QTabBar::tab { padding: 4px 9px; background: palette(button); color: palette(button-text); }"
            "QTabBar::tab:selected { background: palette(highlight); color: palette(highlighted-text); }");
#endif

        compareAction = new QAction("Compare Images…", this);
        compareAction->setObjectName("compareImagesAction");
        compareAction->setEnabled(false);
        connect(compareAction, &QAction::triggered, this, [this] { compareImages(); });
        newTabAction = new QAction("New Tab", this);
        auto *create = newTabAction;
        create->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
        connect(create, &QAction::triggered, this, [this] { addSession(); });
        auto *newTab = new QToolButton(tabs);
        newTab->setText("+");
        auto newTabFont = newTab->font();
        newTabFont.setPixelSize(22);
        newTab->setFont(newTabFont);
        newTab->setMinimumSize(30, 26);
        newTab->setToolTip("New Tab");
        newTab->setAccessibleName("New Tab");
        newTab->setAutoRaise(true);
        tabs->setCornerWidget(newTab, Qt::TopRightCorner);
        connect(newTab, &QToolButton::clicked, create, &QAction::trigger);
        closeTabAction = new QAction("Close Tab", this);
        auto *closeTab = closeTabAction;
        closeTab->setShortcut(QKeySequence::Close);
        connect(closeTab, &QAction::triggered, this, [this] {
            if (auto *page = tabs->currentWidget()) page->close();
        });
        auto *recent = new QMenu("Recent Images", this);
        auto *pinned = new QMenu("Pinned Images", this);
        recentImages = recent;
        pinnedImages = pinned;
        connect(recent, &QMenu::aboutToShow, this, [this, recent] { populateImages(recent, "recentImages"); });
        connect(pinned, &QMenu::aboutToShow, this, [this, pinned] { populateImages(pinned, "pinnedImages"); });
        pinAction = new QAction("Pin Current Image", this);
        connect(pinAction, &QAction::triggered, this, [this] {
            const auto path = tabs->currentWidget()->property("imagePath").toString();
            auto paths = settings->value("pinnedImages").toStringList();
            if (!paths.removeAll(path)) paths.prepend(path);
            settings->setValue("pinnedImages", paths);
            updatePin();
        });
        connect(tabs, &QTabWidget::currentChanged, this, [this] { updateMenus(); });
        connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
            tabs->widget(index)->close();
        });
        resize(620, 640);
        setWindowTitle("cpmimage");
    }

    ~Workspace() override {
        disconnect(tabs, nullptr, this, nullptr);
        for (auto *action : menuBar()->actions()) menuBar()->removeAction(action);
        for (auto *page : tabs->findChildren<Window *>())
            disconnect(page, nullptr, this, nullptr);
        delete takeCentralWidget();
    }

    Window &addSession() {
        auto *page = new Window(runtime.root, runtime.python, runtime.helper, 30000, 2000, runtime.notices);
        page->workspaceManaged = true;
        page->canCloseWorkbench = [this, page] { return canCloseWithWorkbenchDraft(page); };
        page->bindWorkbenchAction("compare-images", compareAction);
        page->repairReferences = [this, page] {
            QList<ImageComparison::Source> references;
            for (auto *target : comparisonTargets(page)) references.append(comparisonSource(target));
            return references;
        };
        page->menuBar()->setNativeMenuBar(false);
        page->menuBar()->hide();
        page->findChild<QWidget *>("imageName")->hide();
        tabs->setCurrentIndex(tabs->addTab(page, "Untitled"));
        connect(page, &QWidget::windowTitleChanged, this, [this, page](const QString &title) {
            tabs->setTabText(tabs->indexOf(page), title.section(" — ", 1));
        });
        connect(page, &Window::comparisonAvailabilityChanged, this, [this] { updateCompareAction(); });
        connect(page, &Window::sessionClosed, this, [this, page] {
            tabs->removeTab(tabs->indexOf(page));
            updateCompareAction();
            page->deleteLater();
            if (!closingWorkspace && !tabs->count()) QTimer::singleShot(0, this, [this] {
                if (!tabs->count() && isVisible()) addSession();
            });
        });
        connect(page, &Window::newImageRequested, this, [this] { addSession().createImage(); });
        connect(page, &Window::imagePicked, this, [this](const QString &path) { openPath(path); });
        connect(page, &Window::imageOpened, this, [this, page](const QString &path) {
            page->setProperty("imagePath", path);
            auto paths = settings->value("recentImages").toStringList();
            paths.removeAll(path);
            paths.prepend(path);
            settings->setValue("recentImages", paths.mid(0, 12));
            updatePin();
        });
        if (!catalogue.isEmpty()) page->setCataloguePath(catalogue);
        return *page;
    }
    QString catalogue;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    }
    void dropEvent(QDropEvent *event) override {
        for (const auto &url : event->mimeData()->urls()) {
            if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile()) continue;
            openPath(url.toLocalFile());
            event->acceptProposedAction();
        }
    }
    void closeEvent(QCloseEvent *event) override {
        if (!canCloseWithWorkbenchDraft(this)) { event->ignore(); return; }
        QScopedValueRollback<bool> closing(closingWorkspace, true);
        while (tabs->count()) {
            auto *page = static_cast<Window *>(tabs->currentWidget());
            if (!page->closeSession([this] { close(); })) { event->ignore(); return; }
        }
        event->accept();
    }

private:
    bool canCloseWithWorkbenchDraft(QWidget *scope) {
        for (auto *dialog : scope->findChildren<QDialog *>()) {
            auto *editor = dynamic_cast<DiskFormatEditor *>(dialog);
            if (!editor || !editor->isVisible() || !editor->hasUnsavedWork()) continue;
            QMessageBox warning(QMessageBox::Warning, "Unsaved Workbench Changes",
                "The Disk Format Editor has an unsaved draft.", QMessageBox::NoButton, this);
            auto *cancel = warning.addButton("Cancel and Return", QMessageBox::RejectRole);
            auto *enable = warning.addButton("Review Draft", QMessageBox::ActionRole);
            warning.setDefaultButton(cancel);
            warning.setEscapeButton(cancel);
            warning.exec();
            if (warning.clickedButton() == enable) { editor->raise(); editor->activateWindow(); }
            return false;
        }
        return true;
    }

    void openPath(const QString &path) { addSession().inspectImage(path); }

    void populateImages(QMenu *menu, const QString &key) {
        menu->clear();
        for (const auto &path : settings->value(key).toStringList()) {
            auto *action = menu->addAction(path);
            action->setEnabled(QFileInfo(path).isFile());
            connect(action, &QAction::triggered, this, [this, path] { openPath(path); });
        }
    }

    void updatePin() {
        const auto *page = tabs->currentWidget();
        const auto path = page ? page->property("imagePath").toString() : QString();
        pinAction->setEnabled(!path.isEmpty());
        pinAction->setText(settings->value("pinnedImages").toStringList().contains(path)
                           ? "Unpin Current Image" : "Pin Current Image");
    }

    QList<Window *> comparisonTargets(const Window *active) const {
        QList<Window *> available;
        if (!active || active->busy) return available;
        for (int index = 0; index < tabs->count(); ++index) {
            auto *page = qobject_cast<Window *>(tabs->widget(index));
            if (page && page != active && !page->busy && !page->session.isEmpty()) available.append(page);
        }
        return available;
    }

    void updateCompareAction() {
        const auto *active = qobject_cast<Window *>(tabs->currentWidget());
        compareAction->setEnabled(active && !active->session.isEmpty() && !comparisonTargets(active).isEmpty());
    }

    ImageComparison::Source comparisonSource(Window *page) const {
        const auto token = page->session;
        const QPointer<Window> guard(page);
        const int index = tabs->indexOf(page);
        return {
            QString("Tab %1 · %2").arg(index + 1).arg(tabs->tabText(index)),
            [guard, token](QJsonObject fields, auto reply) {
                if (!guard || guard->session != token) {
                    reply({}, "An image session changed. Reopen the comparison.");
                    return;
                }
                fields["session"] = token;
                guard->requestBrokerResult(std::move(fields), std::move(reply));
            },
            [guard, token] {
                if (guard && guard->session == token && guard->busy &&
                    (guard->operation.startsWith("comparison_") || guard->operation == "filesystem_structure"))
                    guard->cancel();
            }
        };
    }

    void compareImages() {
        auto *active = qobject_cast<Window *>(tabs->currentWidget());
        if (!active || active->session.isEmpty()) return;
        const auto available = comparisonTargets(active);
        if (available.isEmpty()) return;
        QList<ImageComparison::Source> targets;
        for (auto *page : available) targets.append(comparisonSource(page));
        auto *dialog = new ImageComparison(comparisonSource(active),
                                           std::move(targets), this);
        active->registerWorkbenchDialog(dialog);
        dialog->show();
    }

    void updateMenus() {
        for (auto *action : menuBar()->actions()) menuBar()->removeAction(action);
        if (auto *page = qobject_cast<Window *>(tabs->currentWidget())) {
            const auto actions = page->menuBar()->actions();
            for (const auto &title : {"File", "Edit", "View", "Workbench", "Settings", "Help"}) {
                for (auto *action : actions) {
                    auto *menu = action->menu();
                    if (!menu || menu->title() != QLatin1String(title)) continue;
                    if (QLatin1String(title) == "File" && !menu->actions().contains(pinAction)) {
                        menu->addMenu(recentImages);
                        menu->addMenu(pinnedImages);
                        menu->addAction(pinAction);
                    }
                    if (QLatin1String(title) == "View" && !menu->actions().contains(newTabAction)) {
                        if (!menu->actions().isEmpty()) menu->addSeparator();
                        menu->addAction(newTabAction);
                        menu->addAction(closeTabAction);
                    }
                    if (QLatin1String(title) == "Workbench" && !menu->actions().contains(compareAction))
                        menu->insertAction(page->filesystemCheckAction, compareAction);
                    menuBar()->addAction(action);
                    break;
                }
            }
        }
        updatePin();
        updateCompareAction();
    }
    RuntimePaths runtime;
    QTabWidget *tabs = new QTabWidget;
    bool closingWorkspace = false;
    QAction *compareAction = nullptr;
    QAction *newTabAction = nullptr;
    QAction *closeTabAction = nullptr;
    QMenu *recentImages = nullptr;
    QMenu *pinnedImages = nullptr;
    QAction *pinAction = nullptr;
    std::unique_ptr<QSettings> settings;
};
