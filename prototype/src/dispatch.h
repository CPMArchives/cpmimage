// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "runtime_paths.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <cstdio>
#include <algorithm>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

inline std::optional<int> dispatchUtility(int &argc, char **argv) {
    const QStringList utilities{"cpmls", "cpmrm", "cpmcp", "cpmchmod",
                                "cpmchattr", "mkfs.cpm", "fsck.cpm", "fsed.cpm"};
    const auto isSelector = [&](const QString &arg) {
        return arg.startsWith('/') && utilities.contains(arg.mid(1));
    };
    QStringList arguments;
#ifdef Q_OS_WIN
    int count = 0;
    if (auto *original = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) arguments.append(QString::fromWCharArray(original[i]));
        LocalFree(original);
    }
#else
    for (int i = 1; i < argc; ++i) arguments.append(QFile::decodeName(argv[i]));
#endif
    if (std::none_of(arguments.cbegin(), arguments.cend(), isSelector)) return std::nullopt;

    const auto error = [](const QString &message, int status = 2) {
        std::fprintf(stderr, "cpmimage: %s\n", qPrintable(message));
        return status;
    };
    QString utility, catalogue;
    QStringList forwarded;
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const auto arg = arguments.at(i);
        if (isSelector(arg)) {
            if (!utility.isEmpty()) return error("Choose exactly one cpmtools utility.");
            utility = arg.mid(1);
        } else if (arg == "--diskdefs" || arg.startsWith("--diskdefs=") || arg.startsWith("/diskdefs=")) {
            const auto value = arg == "--diskdefs"
                ? (i + 1 < arguments.size() && !isSelector(arguments.at(i + 1)) ? arguments.at(++i) : QString())
                : arg.mid(arg.indexOf('=') + 1);
            if (value.isEmpty()) return error("Missing diskdefs path.");
            const auto absolute = QFileInfo(value).absoluteFilePath();
            if (!catalogue.isEmpty() && catalogue != absolute) return error("Conflicting diskdefs paths.");
            catalogue = absolute;
        } else if (arg.startsWith("/img=") || arg.startsWith("/f=") ||
                   QStringList{"--image", "--format", "--container", "--root", "--python",
                               "--helper", "--diagnostics"}.contains(arg.section('=', 0, 0))) {
            return error("GUI options cannot be combined with a cpmtools utility.");
        } else {
            forwarded.append(arg);
        }
    }

    QCoreApplication core(argc, argv);
    const auto runtime = packagedRuntime(core.applicationDirPath());
    const auto directory = QDir(core.applicationDirPath()).filePath("cpmtools");
#ifdef Q_OS_WIN
    const auto executable = QDir(directory).filePath(utility + ".exe");
#else
    const auto executable = QDir(directory).filePath(utility);
#endif
    if (!QFileInfo(executable).isExecutable()) return error("Bundled utility missing: " + utility, 127);
    if (catalogue.isEmpty()) catalogue = QDir(runtime.root).filePath("fixtures/diskdefs");
    QFile definitions(catalogue);
    if (!QFileInfo(catalogue).isFile() || !definitions.open(QIODevice::ReadOnly))
        return error("Cannot read diskdefs: " + catalogue);
    definitions.close();
#ifdef Q_OS_WIN
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("CPMTOOLS_DISKDEFS", catalogue);
    QProcess child;
    child.setProcessEnvironment(environment);
    child.setProcessChannelMode(QProcess::ForwardedChannels);
    child.setInputChannelMode(QProcess::ForwardedInputChannel);
    child.start(executable, forwarded);
    if (!child.waitForStarted()) return error(child.errorString(), 126);
    if (!child.waitForFinished(-1)) return error(child.errorString(), 126);
    return child.exitStatus() == QProcess::NormalExit ? child.exitCode() : 126;
#else
    if (setenv("CPMTOOLS_DISKDEFS", QFile::encodeName(catalogue).constData(), 1))
        return error("Cannot set the diskdefs path.", 126);
    QList<QByteArray> encoded{QFile::encodeName(executable)};
    for (const auto &arg : forwarded) encoded.append(QFile::encodeName(arg));
    QList<char *> childArguments;
    for (auto &arg : encoded) childArguments.append(arg.data());
    childArguments.append(nullptr);
    execv(encoded.first().constData(), childArguments.data());
    return error("Cannot start bundled utility: " + utility, 126);
#endif
}
