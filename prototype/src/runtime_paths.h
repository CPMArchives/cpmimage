// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDir>
#include <QString>

struct RuntimePaths { QString root, python, helper, notices; };
inline RuntimePaths packagedRuntime(const QString &applicationDirectory) {
    const QDir bin(applicationDirectory);
#ifdef Q_OS_MACOS
    const QDir resources(bin.filePath("../Resources"));
    return {resources.filePath("cpmimage"), resources.filePath("runtime/bin/python3.13"),
            bin.filePath("../Helpers/cpmimage-helper"), resources.filePath("notices")};
#elif defined(Q_OS_WIN)
    return {bin.filePath("resources/cpmimage"), bin.filePath("runtime/python.exe"),
            bin.filePath("helpers/cpmimage-helper.exe"), bin.filePath("notices")};
#else
    return {bin.filePath("../share/cpmimage"), bin.filePath("../runtime/bin/python3.13"),
            bin.filePath("../libexec/cpmimage-helper"), bin.filePath("../share/licenses/cpmimage")};
#endif
}
