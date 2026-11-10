// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QList>
#include <QString>
#include <QStringList>
class QSettings;
struct FilenameMapping { QString source, replacement; };
class FilenamePolicy {
public:
    QList<FilenameMapping> mappings = defaults();
    static QList<FilenameMapping> defaults();
    static QString validateMappings(const QList<FilenameMapping> &mappings);
    static QString validateHostName(const QString &name);
    QString translate(const QString &name) const;
    QString problem(const QString &name, const QStringList &otherNames) const;
    bool load(QSettings &settings);
    bool save(QSettings &settings) const;
};
