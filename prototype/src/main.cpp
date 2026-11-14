// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include "runtime_paths.h"
#include <QApplication>
#include <QIcon>
#include <QTabWidget>
#include <QMenuBar>
#include <QMenu>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSysInfo>
#include <cstdio>

class Workspace : public QMainWindow {
public:
    explicit Workspace(RuntimePaths paths) : runtime(std::move(paths)) {
        setCentralWidget(&tabs);
        tabs.setTabsClosable(true);
        tabs.setMovable(true);
        auto *workspace = new QMenu("Tabs", this);
        auto *create = workspace->addAction("New Tab");
        create->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
        connect(create, &QAction::triggered, this, [this] { addSession(); });
        auto *closeTab = workspace->addAction("Close Tab");
        closeTab->setShortcut(QKeySequence::Close);
        connect(closeTab, &QAction::triggered, this, [this] {
            if (auto *page = tabs.currentWidget()) page->close();
        });
        workspaceMenu = workspace;
        connect(&tabs, &QTabWidget::currentChanged, this, [this] { updateMenus(); });
        connect(&tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
            tabs.widget(index)->close();
        });
        resize(620, 640);
        setWindowTitle("cpmimage");
    }

    Window &addSession() {
        auto *page = new Window(runtime.root, runtime.python, runtime.helper, 30000, 2000, runtime.notices);
        page->menuBar()->setNativeMenuBar(false);
        page->menuBar()->hide();
        tabs.setCurrentIndex(tabs.addTab(page, "Untitled"));
        connect(page, &QWidget::windowTitleChanged, this, [this, page](const QString &title) {
            tabs.setTabText(tabs.indexOf(page), title.section(" — ", 1));
        });
        connect(page, &Window::sessionClosed, this, [this, page] {
            tabs.removeTab(tabs.indexOf(page));
            page->deleteLater();
            if (!tabs.count()) QTimer::singleShot(0, this, [this] {
                if (!tabs.count()) close();
            });
        });
        connect(page, &Window::imagePicked, this, [this](const QString &path) {
            auto &newPage = addSession();
            newPage.inspectImage(path);
        });
        if (!catalogue.isEmpty()) page->setCataloguePath(catalogue);
        return *page;
    }
    QString catalogue;

protected:
    void closeEvent(QCloseEvent *event) override {
        while (tabs.count()) {
            auto *page = tabs.currentWidget();
            if (!page->close()) { event->ignore(); return; }
        }
        event->accept();
    }

private:
    void updateMenus() {
        menuBar()->clear();
        if (auto *page = qobject_cast<Window *>(tabs.currentWidget()))
            for (auto *action : page->menuBar()->actions()) menuBar()->addAction(action);
        menuBar()->addMenu(workspaceMenu);
    }
    RuntimePaths runtime;
    QTabWidget tabs;
    QMenu *workspaceMenu = nullptr;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(":/icons/cpmimage.png"));
    QApplication::setApplicationName("cpmimage Review");
    QCommandLineParser parser;
    parser.setApplicationDescription("Read-only CP/M image listing and export — local review prototype");
    parser.addHelpOption();
    parser.addOption({"root", "Feasibility directory", "path"});
    parser.addOption({"python", "Python 3 executable override", "path"});
    parser.addOption({"helper", "Read-only native helper executable", "path"});
    parser.addOption({"image", "Open an image at startup", "path"});
    parser.addOption({"format", "Explicit startup image layout", "name"});
    parser.addOption({"container", "Startup image container", "type"});
    parser.addOption({"diagnostics", "Report package runtime paths and build information"});
    parser.addOption({"diskdefs", "Use an external disk-definition catalogue", "path"});
    auto arguments = app.arguments();
    for (auto &argument : arguments) {
        if (argument.startsWith("/diskdefs=")) argument.replace(0, 10, "--diskdefs=");
        else if (argument.startsWith("/f=")) argument.replace(0, 3, "--format=");
        else if (argument.startsWith("/img=")) argument.replace(0, 5, "--image=");
    }
    parser.process(arguments);
    const auto catalogueOptions = parser.values("diskdefs");
    if (catalogueOptions.contains(QString()) ||
        (!catalogueOptions.isEmpty() && catalogueOptions.count(catalogueOptions.first()) != catalogueOptions.size())) {
        QMessageBox::critical(nullptr, "Invalid diskdefs option", "Choose one diskdefs catalogue.");
        return 2;
    }
    auto paths = packagedRuntime(app.applicationDirPath());
    if (parser.isSet("root")) paths.root = QDir(parser.value("root")).absolutePath();
    if (parser.isSet("python")) paths.python = parser.value("python");
    if (parser.isSet("helper")) paths.helper = QFileInfo(parser.value("helper")).absoluteFilePath();
    const bool complete = QFileInfo::exists(paths.root + "/prototype/broker.py") &&
                          QFileInfo::exists(paths.helper) &&
                          (parser.isSet("python") || QFileInfo::exists(paths.python));
    if (parser.isSet("diagnostics")) {
#ifdef _MSC_VER
        const QString compiler = QString("MSVC %1").arg(_MSC_FULL_VER);
#else
        const QString compiler = QString::fromLatin1(__VERSION__);
#endif
        const QJsonObject report{{"complete", complete}, {"root", paths.root},
            {"python", paths.python}, {"helper", paths.helper}, {"notices", paths.notices},
            {"qt", qVersion()}, {"architecture", QSysInfo::buildCpuArchitecture()}, {"compiler", compiler}};
        const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Compact);
        std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); std::fputc('\n', stdout);
        return complete ? 0 : 2;
    }
    if (!complete) {
        QMessageBox::critical(nullptr, "Incomplete application package",
            "Required application files are missing. Restore the complete application package and try again.");
        return 2;
    }
    Workspace workspace(paths);
    auto &window = workspace.addSession();
    workspace.catalogue = parser.value("diskdefs");
    workspace.show();
    QTimer::singleShot(0, &window, [&] {
        if (!parser.isSet("image")) {
            window.loadCatalogue(parser.value("diskdefs"));
        } else if (parser.isSet("diskdefs")) {
            QObject::connect(&window, &Window::completed, &window, [&](bool ok) {
                if (ok) window.inspectImage(parser.value("image"), parser.value("format"), parser.value("container"));
            }, Qt::SingleShotConnection);
            window.loadCatalogue(parser.value("diskdefs"));
        } else {
            window.inspectImage(parser.value("image"), parser.value("format"), parser.value("container"));
        }
    });
    return app.exec();
}
