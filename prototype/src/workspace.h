// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "window.h"
#include "runtime_paths.h"
#include <QTabWidget>
#include <QMenuBar>
#include <QMenu>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QSettings>
#include <utility>

class Workspace : public QMainWindow {
public:
    explicit Workspace(RuntimePaths paths) : runtime(std::move(paths)) {
        setCentralWidget(tabs);
        setAcceptDrops(true);
        tabs->setTabsClosable(true);
        tabs->setMovable(true);
        auto *workspace = new QMenu("Tabs", this);
        auto *create = workspace->addAction("New Tab");
        create->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
        connect(create, &QAction::triggered, this, [this] { addSession(); });
        auto *closeTab = workspace->addAction("Close Tab");
        closeTab->setShortcut(QKeySequence::Close);
        connect(closeTab, &QAction::triggered, this, [this] {
            if (auto *page = tabs->currentWidget()) page->close();
        });
        workspaceMenu = workspace;
        auto *recent = workspace->addMenu("Recent Images");
        auto *pinned = workspace->addMenu("Pinned Images");
        connect(recent, &QMenu::aboutToShow, this, [this, recent] { populateImages(recent, "recentImages"); });
        connect(pinned, &QMenu::aboutToShow, this, [this, pinned] { populateImages(pinned, "pinnedImages"); });
        pinAction = workspace->addAction("Pin Current Image");
        connect(pinAction, &QAction::triggered, this, [this] {
            const auto path = tabs->currentWidget()->property("imagePath").toString();
            auto paths = settings.value("pinnedImages").toStringList();
            if (!paths.removeAll(path)) paths.prepend(path);
            settings.setValue("pinnedImages", paths);
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
        page->menuBar()->setNativeMenuBar(false);
        page->menuBar()->hide();
        tabs->setCurrentIndex(tabs->addTab(page, "Untitled"));
        connect(page, &QWidget::windowTitleChanged, this, [this, page](const QString &title) {
            tabs->setTabText(tabs->indexOf(page), title.section(" — ", 1));
        });
        connect(page, &Window::sessionClosed, this, [this, page] {
            tabs->removeTab(tabs->indexOf(page));
            page->deleteLater();
            if (!tabs->count()) QTimer::singleShot(0, this, [this] {
                if (!tabs->count()) close();
            });
        });
        connect(page, &Window::imagePicked, this, [this](const QString &path) { openPath(path); });
        connect(page, &Window::imageOpened, this, [this, page](const QString &path) {
            page->setProperty("imagePath", path);
            auto paths = settings.value("recentImages").toStringList();
            paths.removeAll(path);
            paths.prepend(path);
            settings.setValue("recentImages", paths.mid(0, 12));
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
        while (tabs->count()) {
            auto *page = tabs->currentWidget();
            if (!page->close()) { event->ignore(); return; }
        }
        event->accept();
    }

private:
    void openPath(const QString &path) { addSession().inspectImage(path); }

    void populateImages(QMenu *menu, const QString &key) {
        menu->clear();
        for (const auto &path : settings.value(key).toStringList()) {
            auto *action = menu->addAction(path);
            action->setEnabled(QFileInfo(path).isFile());
            connect(action, &QAction::triggered, this, [this, path] { openPath(path); });
        }
    }

    void updatePin() {
        const auto *page = tabs->currentWidget();
        const auto path = page ? page->property("imagePath").toString() : QString();
        pinAction->setEnabled(!path.isEmpty());
        pinAction->setText(settings.value("pinnedImages").toStringList().contains(path)
                           ? "Unpin Current Image" : "Pin Current Image");
    }

    void updateMenus() {
        for (auto *action : menuBar()->actions()) menuBar()->removeAction(action);
        if (auto *page = qobject_cast<Window *>(tabs->currentWidget()))
            for (auto *action : page->menuBar()->actions()) menuBar()->addAction(action);
        menuBar()->addMenu(workspaceMenu);
        updatePin();
    }
    RuntimePaths runtime;
    QTabWidget *tabs = new QTabWidget;
    QMenu *workspaceMenu = nullptr;
    QAction *pinAction = nullptr;
    QSettings settings{"CPMArchives", "cpmimage"};
};
