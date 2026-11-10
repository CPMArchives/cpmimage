// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "filename_policy.h"
#include <QJsonArray>
struct ExportItem {
    QString name, destination, outcome = "Pending";
    qint64 size = 0;
    bool exists = false;
};
struct ExportPlan {
    QString directory, error;
    QList<ExportItem> items;
    QStringList conflicts;
};
// Planning never creates folders or files. An area >= 0 adds a numbered folder.
ExportPlan planExport(const QJsonArray &entries, const QString &destination,
                      int area, const FilenamePolicy &policy);
