// SPDX-License-Identifier: GPL-3.0-or-later
#include "export_plan.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QJsonObject>
ExportPlan planExport(const QJsonArray &entries,const QString &destination,int area,const FilenamePolicy &policy) {
    ExportPlan plan;
    const QFileInfo parent(destination);
    if (!parent.isDir() || area < -1 || area > 15) { plan.error="Choose an existing destination folder."; return plan; }
    plan.directory=area<0?parent.absoluteFilePath():QDir(parent.absoluteFilePath()).filePath(QString::number(area));
    const QFileInfo folder(plan.directory);
    if (area>=0 && (folder.isSymLink() || (folder.exists() && !folder.isDir()))) {
        plan.error="The numbered user-area destination must be a folder, not a file or symbolic link."; return plan;
    }
    QSet<QString> existing;
    for(const auto &info:QDir(plan.directory).entryInfoList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot))
        existing.insert(info.fileName().toCaseFolded());
    QSet<QString> planned;
    for(const auto &value:entries) {
        const auto entry=value.toObject();
        const auto name=entry["name"].toString();
        const auto host=policy.translate(name.mid(2));
        const auto invalid=FilenamePolicy::validateHostName(host);
        if(!invalid.isEmpty()) { plan.error=name.mid(2)+": "+invalid; return plan; }
        const auto key=host.toCaseFolded();
        if(planned.contains(key)) { plan.error="More than one selected file translates to "+host+". Review filename settings; no files were exported."; return plan; }
        planned.insert(key);
        ExportItem item; item.name=name; item.destination=QDir(plan.directory).filePath(host);
        item.size=entry["size"].toInteger(); item.exists=existing.contains(key);
        if(item.exists) plan.conflicts<<host;
        plan.items<<item;
    }
    return plan;
}
