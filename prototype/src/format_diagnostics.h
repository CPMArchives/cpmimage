// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QStringList>

namespace FormatDiagnostics {
inline QString rejection(const QString &stage, const QString &code) {
    if (code == "mapping_profile_required") return "A sector mapping profile is required.";
    if (code == "physical_mapping") return "The sector mapping could not be verified.";
    if (code == "image_size") return "The RAW image size disagrees with the definition.";
    if (stage == "directory") return QString("The CP/M directory check failed (%1).").arg(code);
    return QString("The layout check failed (%1).").arg(code);
}

inline QString describe(const QJsonObject &evaluation) {
    if (evaluation.isEmpty()) return {};
    const auto layout = evaluation["layout"].toObject();
    const auto directory = evaluation["directory"].toObject();
    const auto expected = layout["expected"].toObject();
    const auto observed = layout["observed"].toObject();
    QStringList lines;
    if (layout["status"].toString() == "failed") {
        lines << rejection("layout", layout["code"].toString());
        const auto detail = layout["message"].toString();
        if (!detail.isEmpty()) lines << detail;
    } else if (layout["kind"].toString() == "raw_extent") {
        lines << "RAW extent checked.";
    } else {
        const auto mapping = layout["mapping_source"].toString();
        lines << (mapping.isEmpty() ? QString("Physical layout checked.") :
                  QString("Physical layout matches the %1 RAW-slot mapping.").arg(mapping));
    }
    if (expected.contains("bytes") && observed.contains("bytes"))
        lines << QString("RAW bytes observed %1, expected %2.")
                 .arg(observed["bytes"].toInteger()).arg(expected["bytes"].toInteger());
    if (layout["missing_bytes"].toInteger() > 0)
        lines << QString("%1 bytes short of the expected extent.").arg(layout["missing_bytes"].toInteger());
    if (layout["trailing_bytes"].toInteger() > 0)
        lines << QString("%1 bytes beyond the expected extent.").arg(layout["trailing_bytes"].toInteger());
    if (expected.contains("tracks")) {
        lines << QString("Candidate expects %1 tracks, %2 sectors per track, %3 bytes per sector, offset %4 bytes.")
                 .arg(expected["tracks"].toInteger()).arg(expected["sectors_per_track"].toInteger())
                 .arg(expected["sector_size"].toInteger()).arg(expected["offset"].toInteger());
        if (observed.contains("tracks"))
            lines << QString("Source reports %1 tracks.").arg(observed["tracks"].toInteger());
    }
    const auto directoryStatus = directory["status"].toString();
    if (directoryStatus == "passed") {
        const auto count = directory["entries"].toInteger();
        lines << (count == 0 ? QString("CP/M directory readable and empty. This is limited evidence for the format.") :
                  QString("CP/M directory readable. %1 file(s) listed.").arg(count));
    } else if (directoryStatus == "failed") {
        lines << rejection("directory", directory["code"].toString());
        const auto detail = directory["message"].toString();
        if (!detail.isEmpty()) lines << detail;
    } else {
        lines << "CP/M directory was not checked.";
    }
    return lines.join('\n');
}
}
