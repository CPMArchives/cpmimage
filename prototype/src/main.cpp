// SPDX-License-Identifier: GPL-3.0-or-later
#include "workspace.h"
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
#include <functional>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(":/icons/cpmimage.png"));
    QApplication::setApplicationName("cpmimage Review");
    QCommandLineParser parser;
    parser.setApplicationDescription("Browse and edit CP/M disk images");
    parser.addHelpOption();
    parser.addPositionalArgument("image", "Disk images to open in order", "[image…]");
    parser.addOption({"root", "Feasibility directory", "path"});
    parser.addOption({"python", "Python 3 executable override", "path"});
    parser.addOption({"helper", "Read-only native helper executable", "path"});
    parser.addOption({"image", "Open an image at startup", "path"});
    parser.addOption({"format", "Disk definition for subsequent startup images", "name"});
    parser.addOption({"container", "Startup image container", "type"});
    parser.addOption({"diagnostics", "Report package runtime paths and build information"});
    parser.addOption({"diskdefs", "Use an external disk-definition catalogue", "path"});
    const auto arguments = app.arguments();
    struct StartupImage { QString path, format, container; };
    QList<StartupImage> images;
    QString format, container;
    QStringList options{arguments.first()};
    bool positionalOnly = false;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        auto argument = arguments[i];
        if (!positionalOnly) {
            if (argument.startsWith("/diskdefs=")) argument.replace(0, 10, "--diskdefs=");
            else if (argument.startsWith("/f=")) argument.replace(0, 3, "--format=");
            else if (argument.startsWith("/img=")) argument.replace(0, 5, "--image=");
        }
        if (!positionalOnly && argument == "--") {
            positionalOnly = true;
            continue;
        }
        if (positionalOnly || !argument.startsWith('-')) {
            images.append({argument, format, container});
            continue;
        }
        const auto name = argument.section('=', 0, 0);
        options.append(argument);
        if (name == "--image" || name == "--format" || name == "--container" ||
            name == "--diskdefs" || name == "--root" || name == "--helper" || name == "--python") {
            const auto value = argument.contains('=') ? argument.section('=', 1) :
                (i + 1 < arguments.size() ? arguments[++i] : QString());
            if (value.isEmpty()) {
                std::fprintf(stderr, "Missing value for %s.\n", qPrintable(name));
                return 2;
            }
            if (!argument.contains('=')) options.append(value);
            if (name == "--format") format = value;
            if (name == "--container") container = value;
            if (name == "--image") images.append({value, format, container});
        }
    }
    parser.process(options);
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
    workspace.catalogue = parser.value("diskdefs");
    workspace.show();
    qsizetype nextImage = 0;
    std::function<void()> openNext = [&] {
        if (nextImage == images.size()) return;
        const auto image = images.at(nextImage++);
        auto &window = workspace.addSession();
        QObject::connect(&window, &Window::completed, &workspace, [&](bool) {
            QTimer::singleShot(0, &workspace, openNext);
        }, Qt::SingleShotConnection);
        window.inspectImage(image.path, image.format, image.container);
    };
    if (images.isEmpty()) {
        auto &window = workspace.addSession();
        window.loadCatalogue(workspace.catalogue);
    } else {
        QTimer::singleShot(0, &workspace, openNext);
    }
    return app.exec();
}
