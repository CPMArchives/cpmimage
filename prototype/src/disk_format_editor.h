// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

class DiskFormatEditor : public QDialog {
public:
    using Reply = std::function<void(QJsonObject, QString)>;
    using Request = std::function<void(QJsonObject, Reply)>;

    DiskFormatEditor(Request request, const QJsonArray &catalogue,
                     const QString &imagePath, const QString &selectedFormat, QWidget *parent);
    void refreshRecords();
    void importText(const QString &text);
    void setCatalogue(const QJsonArray &rows) { catalogue = rows; }
    void reject() override;
    bool savedDefinition() const { return saved; }

private:
    void send(QJsonObject request, Reply reply);
    void load(const QJsonObject &record);
    void updateDerived();
    void showImportDiagnostics(const QJsonArray &items);
    void duplicateSelected();
    void runDefinition(const QString &operation);
    QJsonObject record() const;
    void showError(const QString &message);
    void invalidateImageCheck();
    void formFieldsEnabled(bool enabled);

    Request request;
    QJsonArray catalogue, records;
    QJsonObject current, checkedMapping, checkedGeometry;
    QComboBox *selection, *os, *bootUnit, *skewMode, *offsetUnit;
    QLineEdit *identifier, *displayName, *libdsk, *slotOrder, *image;
    QSpinBox *sectorSize, *tracks, *sectors, *blockSize, *entries, *directoryBlocks,
        *bootCount, *skewStep, *offset, *extents;
    QLabel *derived, *definitionStatus, *imageStatus, *operationStatus, *diagnostics;
    QPushButton *applyMapping;
    QWidget *form;
    QString preferredSelection;
    int previousSkewStep = 1;
    bool bundledSelection = false;
    bool saved = false, loading = false, pending = false, unresolved = false;
};
