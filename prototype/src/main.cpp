// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include "runtime_paths.h"
#include <QApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSysInfo>
#include <cstdio>

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
    Window window(paths.root, paths.python, paths.helper, 30000, 2000, paths.notices);
    window.show();
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
