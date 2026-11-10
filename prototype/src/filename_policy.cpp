// SPDX-License-Identifier: GPL-3.0-or-later
#include "filename_policy.h"
#include <QSettings>
#include <QSet>
#include <QRegularExpression>
#include <QStringList>
QList<FilenameMapping> FilenamePolicy::defaults() { return {{"/","_"},{":","~"},{"\\","^"}}; }
QString FilenamePolicy::validateMappings(const QList<FilenameMapping> &values) {
    if (values.size()>64) return "Use at most 64 character mappings.";
    QSet<QString> seen;
    for (const auto &m:values) {
        if (m.source.size()!=1 || m.source[0].unicode()<33 || m.source[0].unicode()>126 || m.source==".")
            return "Each source must be one printable ASCII character other than a dot or space.";
        if (seen.contains(m.source)) return "Each source character can appear only once.";
        seen.insert(m.source);
        if (m.replacement.size()!=1 || m.replacement[0].unicode()<33 || m.replacement[0].unicode()>126 ||
            QString("<>:\"/\\|?*+,;=[].").contains(m.replacement))
            return "Each replacement must be one character allowed in Windows, macOS, Linux and DOS filenames.";
    }
    return {};
}
QString FilenamePolicy::validateHostName(const QString &name) {
    if (name.isEmpty() || name=="." || name==".." || name.endsWith('.') || name.endsWith(' '))
        return "Choose a nonempty filename without a trailing dot or space.";
    for (const auto c:name)
        if (c.unicode()<32 || c.unicode()==127 || QString("<>:\"/\\|?*").contains(c))
            return "The exported name contains a character that needs a translation. Review filename settings.";
    static const QRegularExpression device("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$", QRegularExpression::CaseInsensitiveOption);
    if (device.match(name.section('.',0,0)).hasMatch()) return "That filename is reserved on Windows. Choose another filename.";
    return {};
}
QString FilenamePolicy::translate(const QString &name) const {
    QString result;
    for (const auto c:name) {
        QString replacement(c);
        for (const auto &m:mappings) if (m.source==QString(c)) { replacement=m.replacement; break; }
        result+=replacement;
    }
    return result;
}
QString FilenamePolicy::problem(const QString &name,const QStringList &others) const {
    const auto translated=translate(name);
    const auto invalid=validateHostName(translated); if (!invalid.isEmpty()) return invalid;
    for (const auto &other:others)
        if (other!=name && translate(other).toCaseFolded()==translated.toCaseFolded())
            return "More than one file translates to "+translated+". Review filename settings to avoid a collision.";
    return {};
}
bool FilenamePolicy::load(QSettings &settings) {
    if (!settings.contains("filenames/mappings")) return true;
    const auto stored=settings.value("filenames/mappings");
    if (stored.metaType().id()!=QMetaType::QStringList) return false;
    const auto values=stored.toStringList();
    if (values.size()%2 || values.size()>128) return false;
    QList<FilenameMapping> loaded;
    for (int i=0;i<values.size();i+=2) loaded.append({values[i],values[i+1]});
    if (!validateMappings(loaded).isEmpty()) return false;
    mappings=loaded; return true;
}
bool FilenamePolicy::save(QSettings &settings) const {
    if (!validateMappings(mappings).isEmpty()) return false;
    QStringList values; for (const auto &m:mappings) values<<m.source<<m.replacement;
    settings.setValue("filenames/mappings",values);settings.sync();
    return settings.status()==QSettings::NoError;
}
