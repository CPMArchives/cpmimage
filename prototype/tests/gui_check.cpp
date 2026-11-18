// SPDX-License-Identifier: GPL-3.0-or-later
#include "workspace.h"
#include "filesystem_check.h"
#include "filesystem_inspector.h"
#include <QCryptographicHash>
#include <QFile>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QDir>
#include <QFileDialog>
#include <QLineEdit>
#include <QApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPushButton>
#include <QListWidget>
#include <QTableWidget>
#include <QSplitter>
#include <QLabel>
#include <QComboBox>
#include <QtTest>
#include <QSettings>
#include <QAction>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <memory>
#include <tuple>
#include <QTextEdit>
#include <QPlainTextEdit>

static QFileDialog *fileDialog() {
    for (auto *w : QApplication::topLevelWidgets())
        if (auto *d = qobject_cast<QFileDialog *>(w); d && d->isVisible()) return d;
    return nullptr;
}

class GuiCheck : public QObject {
    Q_OBJECT
private slots:
    void diskFormatEditorRefreshesPickerAndPinsOpenImage() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {},
                      temp.filePath("settings.ini"), temp.filePath("definitions"));
        QSignalSpy done(&window, &Window::completed);
        window.show();
        window.loadCatalogue();
        QVERIFY(done.wait(15000));
        QVERIFY(done.takeFirst()[0].toBool());
        window.openImage(root + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(15000));
        QVERIFY(done.takeFirst()[0].toBool());
        const auto original = window.entryCount();
        auto *action = window.findChild<QAction *>("diskFormatEditorAction");
        QVERIFY(action);
        bool editorPassed = false;
        QTimer::singleShot(0, &window, [&] {
            auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(editor);
            const auto close = qScopeGuard([editor] { editor->done(QDialog::Rejected); });
            QCOMPARE(editor->objectName(), QString("diskFormatEditor"));
            auto *identifier = editor->findChild<QLineEdit *>("diskFormatIdentifier");
            auto *records = editor->findChild<QComboBox *>("diskFormatRecords");
            auto *validation = editor->findChild<QLabel *>("diskFormatValidation");
            auto *save = editor->findChild<QPushButton *>("diskFormatSave");
            auto *sector = editor->findChild<QSpinBox *>("diskFormatSectorSize");
            auto *boot = editor->findChild<QSpinBox *>("diskFormatBootCount");
            QVERIFY(identifier && records && validation && save && sector && boot);
            QCOMPARE(identifier->text(), QString());
            QCOMPARE(sector->value(), 0);
            QCOMPARE(boot->value(), 0);
            QCOMPARE(editor->findChild<QComboBox *>("diskFormatOs")->currentIndex(), 0);
            QTRY_VERIFY(records->findData("ibm-3740") >= 0);
            editor->findChild<QPushButton *>("diskFormatValidate")->click();
            QTRY_VERIFY(validation->text().contains("invalid_definition"));
            const auto index = records->findData("ibm-3740");
            records->setCurrentIndex(index);
            emit records->activated(index);
            QTRY_VERIFY(validation->text().contains("Catalogue entry"));
            QVERIFY(!identifier->isEnabled());
            editor->findChild<QPushButton *>("diskFormatDuplicate")->click();
            QCOMPARE(identifier->text(), QString());
            QVERIFY(identifier->isEnabled());
            QCOMPARE(sector->value(), 128);
            QVERIFY(editor->findChild<QLabel *>("diskFormatDerived")->text().contains("256256 bytes"));
            identifier->setText("gui-editor-3740");
            editor->findChild<QLineEdit *>("diskFormatDisplayName")->setText("GUI editor test format");
            save->click();
            QTRY_VERIFY(records->findData("gui-editor-3740") >= 0);
            QTRY_VERIFY(save->isEnabled());
            editorPassed = true;
        });
        action->trigger();
        QVERIFY(editorPassed);
        QCOMPARE(window.entryCount(), original);
        auto *picker = window.findChild<QPushButton *>("chooseFormat");
        QVERIFY(picker && picker->isEnabled());
        bool found = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([dialog, &found] {
                if (found) dialog->accept();
                else dialog->reject();
            });
            auto *search = dialog->findChild<QLineEdit *>("formatPickerSearch");
            auto *list = dialog->findChild<QListWidget *>("formatPickerList");
            QVERIFY(search && list);
            search->setText("GUI editor test format");
            QCOMPARE(list->count(), 1);
            QCOMPARE(list->item(0)->data(Qt::UserRole).toJsonObject()["id"].toString(), QString("gui-editor-3740"));
            QVERIFY(!list->item(0)->data(Qt::UserRole).toJsonObject()["definition_sha256"].toString().isEmpty());
            found = true;
        });
        picker->click();
        QVERIFY(found);
        QVERIFY(done.wait(15000));
        QVERIFY(done.takeFirst()[0].toBool());
        auto *areas = window.findChild<QListWidget *>("userAreas");
        auto *files = window.findChild<QTableWidget *>("fileList");
        areas->setCurrentRow(1);
        for (int i = 0; i < files->rowCount(); ++i)
            if (files->item(i, 0)->data(Qt::UserRole).toString() == "00BINARY.BIN") window.selectEntry(i);
        window.exportFile(temp.filePath("before.bin"));
        QVERIFY(done.wait(15000));
        QVERIFY(done.takeFirst()[0].toBool());

        bool revisionSaved = false;
        QTimer::singleShot(0, &window, [&] {
            auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(editor);
            const auto close = qScopeGuard([editor] { editor->done(QDialog::Rejected); });
            auto *records = editor->findChild<QComboBox *>("diskFormatRecords");
            QTRY_VERIFY(records->findData("gui-editor-3740") > 0);
            const int index = records->findData("gui-editor-3740");
            records->setCurrentIndex(index);
            emit records->activated(index);
            editor->findChild<QSpinBox *>("diskFormatBlockSize")->setValue(2048);
            editor->findChild<QPushButton *>("diskFormatSave")->click();
            auto *status = editor->findChild<QLabel *>("diskFormatValidation");
            QTRY_COMPARE(status->text(), QString("Definition saved."));
            QTRY_VERIFY(editor->findChild<QPushButton *>("diskFormatSave")->isEnabled());
            revisionSaved = true;
        });
        action->trigger();
        QVERIFY(revisionSaved);
        window.exportFile(temp.filePath("after.bin"));
        QVERIFY(done.wait(15000));
        QVERIFY(done.takeFirst()[0].toBool());
        QFile before(temp.filePath("before.bin")), after(temp.filePath("after.bin"));
        QVERIFY(before.open(QIODevice::ReadOnly) && after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), before.readAll());
    }

    void diskFormatEditorChecksMappingAndCancelsConversion() {
        const bool nativeDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore = qScopeGuard([nativeDisabled] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled);
        });
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QByteArray imd("IMD 1.18: Qt regression\r\n\x1a");
        for (char cylinder = 0; cylinder < 2; ++cylinder) {
            for (char head = 0; head < 2; ++head) {
                const char track[] = {5, cylinder, head, 4, 2, 1, 2, 3, 4};
                imd.append(track, sizeof track);
                for (int slot = 0; slot < 4; ++slot) {
                    imd.append(char(2));
                    imd.append(char(0xe5));
                }
            }
        }
        const QByteArray legacy = R"({"track_order":[[0,0],[0,1],[1,0],[1,1]],"default":{"mode":5,"size":512,"sectors":[1,3,2,4]},"note":"preserve"})";
        const auto imagePath = temp.filePath("example.imd");
        QFile image(imagePath), sidecar(imagePath + ".mapping.json");
        QVERIFY(image.open(QIODevice::WriteOnly));
        QCOMPARE(image.write(imd), imd.size());
        image.close();
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        QCOMPARE(sidecar.write(legacy), legacy.size());
        sidecar.close();
        QFile definition(temp.filePath("legacy.FDF"));
        QVERIFY(definition.open(QIODevice::WriteOnly));
        definition.write("diskdef gui-imd\n seclen 20000\n tracks 4\n sectrk 4\n blocksize 1024\n"
                         " maxdir 32\n boottrk 1\n mystery 7\nend\n");
        definition.close();
        Window window(qEnvironmentVariable("CPMIMAGE_ROOT"), qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {}, temp.filePath("settings.ini"),
                      temp.filePath("definitions"));
        window.show();
        bool passed = false;
        QTimer::singleShot(0, &window, [&] {
            auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(editor);
            const auto close = qScopeGuard([editor] { editor->done(QDialog::Rejected); });
            auto *records = editor->findChild<QComboBox *>("diskFormatRecords");
            QTRY_VERIFY(records->count() > 1);
            editor->findChild<QPushButton *>("diskFormatImport")->click();
            auto *chooser = editor->findChild<QFileDialog *>("diskFormatImportDialog");
            QVERIFY(chooser);
            // Deliver the selected path without relying on the platform file browser.
            emit chooser->fileSelected(definition.fileName());
            chooser->reject();
            QTRY_VERIFY(editor->findChild<QLabel *>("diskFormatDiagnostics")->text().contains("mystery"));
            QCOMPARE(editor->findChild<QSpinBox *>("diskFormatSectorSize")->value(), 20000);
            editor->findChild<QLineEdit *>("diskFormatCheckPath")->setText(imagePath);
            const QList<QPair<QString, int>> fields{{"SectorSize", 512}, {"Tracks", 4}, {"Sectors", 4},
                {"BlockSize", 1024}, {"Entries", 32}, {"BootCount", 1}};
            for (const auto &[field, value] : fields)
                editor->findChild<QSpinBox *>("diskFormat" + field)->setValue(value);
            auto *apply = editor->findChild<QPushButton *>("diskFormatUseMapping");
            QVERIFY(!apply->isEnabled());
            editor->findChild<QPushButton *>("diskFormatCheck")->click();
            QTRY_VERIFY(apply->isEnabled());
            QVERIFY(editor->findChild<QLabel *>("diskFormatCompatibility")->text().contains("sidecar RAW-slot mapping"));
            editor->findChild<QSpinBox *>("diskFormatSectorSize")->setValue(128);
            editor->findChild<QSpinBox *>("diskFormatTracks")->setValue(8);
            editor->findChild<QSpinBox *>("diskFormatSectors")->setValue(2);
            QVERIFY(apply->isEnabled());
            apply->click();
            for (const auto &[field, value] : fields)
                QCOMPARE(editor->findChild<QSpinBox *>("diskFormat" + field)->value(), value);
            auto *compatibility = editor->findChild<QLabel *>("diskFormatCompatibility");
            editor->findChild<QPushButton *>("diskFormatCheck")->click();
            QTRY_VERIFY(compatibility->text().contains("directory readable and empty"));
            QVERIFY(compatibility->text().contains("4 tracks, 4 sectors per track, 512 bytes per sector"));
            editor->findChild<QSpinBox *>("diskFormatTracks")->setValue(5);
            QVERIFY(!compatibility->text().contains("directory readable"));
            editor->findChild<QPushButton *>("diskFormatCheck")->click();
            QTRY_VERIFY(compatibility->text().contains("The sector mapping could not be verified"));
            QVERIFY(compatibility->text().contains("5 tracks"));
            QVERIFY(compatibility->text().contains("directory was not checked"));
            QVERIFY(!apply->isEnabled());
            editor->findChild<QSpinBox *>("diskFormatTracks")->setValue(4);

            bool previewed = false;
            QTimer previewTimer;
            connect(&previewTimer, &QTimer::timeout, editor, [&] {
                auto *preview = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!preview) return;
                previewed = preview->detailedText().contains("preserve");
                preview->button(QMessageBox::Cancel)->click();
            });
            previewTimer.start(10);
            editor->findChild<QPushButton *>("diskFormatPreviewMapping")->click();
            QTRY_VERIFY(previewed);
            passed = true;
        });
        auto *action = window.findChild<QAction *>("diskFormatEditorAction");
        QVERIFY(action);
        action->trigger();
        QVERIFY(passed);
        QVERIFY(sidecar.open(QIODevice::ReadOnly));
        QCOMPARE(sidecar.readAll(), legacy);
        QVERIFY(image.open(QIODevice::ReadOnly));
        QCOMPARE(image.readAll(), imd);
    }

    void compactToolbarAndImageInformation() {
        const auto root=qEnvironmentVariable("CPMIMAGE_ROOT");
        Window window(root,qEnvironmentVariable("CPMIMAGE_PYTHON","python3"),qEnvironmentVariable("CPMIMAGE_HELPER"));
        window.show();
        auto *information=window.findChild<QPushButton *>("imageInformation");
        auto *action=window.findChild<QAction *>("imageInformationAction");
        auto *name=window.findChild<QLabel *>("imageName");
        auto *cancel=window.findChild<QPushButton *>("cancelOperation");
        QVERIFY(information);QVERIFY(action);QVERIFY(name);QVERIFY(cancel);
        QVERIFY(!information->isEnabled());QVERIFY(!action->isEnabled());QVERIFY(cancel->isHidden());
        QVERIFY2(window.width()<=580,qPrintable(QString::number(window.minimumSizeHint().width())));
        QSignalSpy done(&window,&Window::completed);
        const auto source=root+"/fixtures/ibm3740-independent.raw";
        window.openImage(source,"ibm-3740","raw");
        QVERIFY(!information->isEnabled());QVERIFY(!cancel->isHidden());
        QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        QVERIFY(information->isEnabled());QVERIFY(action->isEnabled());QVERIFY(cancel->isHidden());
        QCOMPARE(name->text(),QString("ibm3740-independent.raw"));QVERIFY(window.windowTitle().contains(name->text()));
        for(auto *label:window.findChildren<QLabel *>()) QVERIFY(!label->text().contains("SHA-256"));
        auto dialogText=[&](bool menu) {
            QString text;
            QTimer::singleShot(100,&window,[&]{
                if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                    if(auto *field=dialog->findChild<QTextEdit *>("imageInformationText")) text=field->toPlainText();
                    dialog->reject();
                }
            });
            if(menu) action->trigger();else information->click();
            return text;
        };
        auto details=dialogText(false);
        QVERIFY(details.contains(QFileInfo(source).absoluteFilePath()));QVERIFY(details.contains("ibm-3740"));
        QVERIFY(details.contains("Container: raw"));QVERIFY(details.contains("Files: 3"));
        QVERIFY(details.contains("Populated user areas: 3 of 16"));
        QFile original(source);QVERIFY(original.open(QIODevice::ReadOnly));const auto bytes=original.readAll();
        QVERIFY(details.contains(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())));
        QVERIFY(details.contains("Read-only normalization: None."));
        QTemporaryDir temp;window.selectEntry(0);window.exportFile(temp.filePath("export.bin"));
        QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        QVERIFY(!window.findChild<QLabel *>("selectionStatus")->text().contains("SHA-256"));
        details=dialogText(true);QVERIFY(details.contains("Last completed export"));
        QVERIFY(details.contains("110009dcee21620b166f3abfecb5eff7a873be729d1c2d53822e7acc5f34eb9b"));
        QFile trimmed(temp.filePath("trimmed.raw"));QVERIFY(trimmed.open(QIODevice::WriteOnly));trimmed.write(bytes.left(bytes.size()-128));trimmed.close();
        window.openImage(trimmed.fileName(),"ibm-3740","raw");QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        details=dialogText(false);QVERIFY(details.contains("tail filled with E5"));QVERIFY(!details.contains("Last completed export"));
        QVERIFY(!window.findChild<QLabel *>("selectionStatus")->text().contains("tail"));
        window.openImage(root+"/fixtures/ibm3740-truncated.raw","ibm-3740","raw");
        QVERIFY(done.wait(15000));QVERIFY(!done.takeFirst()[0].toBool());
        QVERIFY(!information->isEnabled());QVERIFY(name->toolTip().isEmpty());
    }

    void physicalDiskInspectorUsesActiveTab() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        Workspace workspace({root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                             qEnvironmentVariable("CPMIMAGE_HELPER"), {}}, temp.filePath("workspace.ini"));
        workspace.show();
        auto &first = workspace.addSession();
        auto viewAction = [&workspace]() -> QAction * {
            for (auto *action : workspace.menuBar()->actions()) {
                if (auto *menu = action->menu(); menu && menu->title() == "Workbench") {
                    for (auto *item : menu->actions())
                        if (item->objectName() == "physicalDiskInspectorAction") return item;
                }
            }
            return nullptr;
        };
        QVERIFY(viewAction());
        QVERIFY(!viewAction()->isEnabled());
        const auto disk = temp.filePath("Inspector source.dsk");
        QVERIFY(QFile::copy(root + "/fixtures/ampro200-independent.dsk", disk));
        QFile original(disk);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto bytes = original.readAll();
        original.close();

        QSignalSpy firstDone(&first, &Window::completed);
        first.openImage(disk, "bettercpm-ampro-little-board", "dsk");
        QTRY_COMPARE_WITH_TIMEOUT(firstDone.size(), 1, 15000);
        QVERIFY(firstDone.first().first().toBool());
        QVERIFY(viewAction()->isEnabled());
        auto *button = first.findChild<QPushButton *>("workbench-physical-disk");
        QVERIFY(button && button->isEnabled());
        button->click();
        QTRY_VERIFY(first.findChild<QDialog *>("physicalDiskInspector"));
        auto *dialog = first.findChild<QDialog *>("physicalDiskInspector");
        QVERIFY(dialog->isVisible());
        auto *origin = dialog->findChild<QLabel *>("physicalDiskSource");
        auto *track = dialog->findChild<QComboBox *>("physicalDiskTrack");
        auto *sectors = dialog->findChild<QTableWidget *>("physicalDiskSectors");
        QVERIFY(origin && track && sectors);
        QVERIFY(origin->text().contains("Inspector source.dsk"));
        QVERIFY(origin->text().contains("Container: DSK"));
        QVERIFY(origin->text().contains(QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())));
        QCOMPARE(track->count(), 40);
        QTRY_COMPARE(sectors->rowCount(), 10);
        QCOMPARE(sectors->editTriggers(), QAbstractItemView::NoEditTriggers);
        QCOMPARE(sectors->horizontalHeaderItem(0)->text(), QString("Descriptor order"));
        QCOMPARE(sectors->item(0, 1)->text(), QString("1"));
        QCOMPARE(sectors->item(9, 1)->text(), QString("10"));
        QCOMPARE(sectors->item(9, 2)->text(), QString("512"));
        QCOMPARE(sectors->item(9, 4)->text(), QString("Unavailable"));
        track->setCurrentIndex(39);
        QVERIFY(track->currentText().contains("Cylinder 39, head 0"));
        QTRY_COMPARE(sectors->rowCount(), 10);
        sectors->selectRow(0);
        auto *viewSector = dialog->findChild<QPushButton *>("viewSectorHex");
        QVERIFY(viewSector && viewSector->isEnabled());
        viewSector->click();
        auto *hexDialog = dialog->findChild<QDialog *>("diskHexViewer");
        QVERIFY(hexDialog);
        auto *hex = hexDialog->findChild<QPlainTextEdit *>();
        QVERIFY(hex && hex->isReadOnly());
        QTRY_VERIFY(!hex->toPlainText().isEmpty());
        QVERIFY(hexDialog->windowTitle().contains("Cylinder 39"));
        QCOMPARE(hex->toPlainText().split('\n').size(), 32);
        hexDialog->reject();
        QVERIFY(!first.windowTitle().endsWith(" *"));
        dialog->reject();
        QTRY_VERIFY(!first.findChild<QDialog *>("physicalDiskInspector"));

        first.findChild<QPushButton *>("workbench-filesystem")->click();
        auto *filesystem = first.findChild<QDialog *>("filesystemStructureInspector");
        QVERIFY(filesystem);
        auto *allocation = filesystem->findChild<QTableWidget *>("filesystemStructureRows");
        QTRY_VERIFY(allocation->rowCount() > 0);
        filesystem->findChild<QPushButton *>("filesystemBlocks")->click();
        QTRY_COMPARE(allocation->horizontalHeaderItem(0)->text(), QString("Block"));
        allocation->selectRow(2);
        auto *physical = filesystem->findChild<QPushButton *>("filesystemGoToPhysicalSectors");
        QVERIFY(physical && physical->isVisible());
        physical->click();
        QTRY_VERIFY(first.findChild<QDialog *>("physicalDiskInspector"));
        auto *linked = first.findChild<QDialog *>("physicalDiskInspector");
        auto *linkedTrack = linked->findChild<QComboBox *>("physicalDiskTrack");
        auto *linkedSectors = linked->findChild<QTableWidget *>("physicalDiskSectors");
        QVERIFY(linkedTrack && linkedSectors);
        QCOMPARE(linkedTrack->currentIndex(), 2);
        QTRY_COMPARE(linkedSectors->selectionModel()->selectedRows().size(), 2);
        QVERIFY(linkedSectors->item(8, 1)->isSelected() && linkedSectors->item(9, 1)->isSelected());
        linkedTrack->setCurrentIndex(3);
        QTRY_COMPARE(linkedSectors->selectionModel()->selectedRows().size(), 2);
        QVERIFY(linkedSectors->item(0, 1)->isSelected() && linkedSectors->item(1, 1)->isSelected());
        linked->reject();
        filesystem->reject();

        auto &second = workspace.addSession();
        QSignalSpy secondDone(&second, &Window::completed);
        second.openImage(root + "/fixtures/ibm3740-empty.raw", "ibm-3740", "raw");
        QTRY_COMPARE_WITH_TIMEOUT(secondDone.size(), 1, 15000);
        QVERIFY(secondDone.first().first().toBool());
        QVERIFY(viewAction()->isEnabled());
        viewAction()->trigger();
        QTRY_VERIFY(second.findChild<QDialog *>("physicalDiskInspector"));
        auto *raw = second.findChild<QDialog *>("physicalDiskInspector");
        auto *rawOrigin = raw->findChild<QLabel *>("physicalDiskSource");
        QVERIFY(rawOrigin);
        QVERIFY(rawOrigin->text().contains("ibm3740-empty.raw"));
        QVERIFY(rawOrigin->text().contains("Container: RAW"));
        auto *rawSectors = raw->findChild<QTableWidget *>("physicalDiskSectors");
        QVERIFY(rawSectors);
        QTRY_COMPARE(rawSectors->rowCount(), 26);
        QCOMPARE(rawSectors->horizontalHeaderItem(1)->text(), QString("RAW slot"));
        QVERIFY(!first.findChild<QDialog *>("physicalDiskInspector"));
        raw->reject();

        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), bytes);
    }

    void filesystemStructureInspectorLinksWorkingImage() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        Workspace workspace({root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                             qEnvironmentVariable("CPMIMAGE_HELPER"), {}}, temp.filePath("workspace.ini"));
        workspace.show();
        auto &first = workspace.addSession();
        auto viewAction = [&workspace]() -> QAction * {
            for (auto *action : workspace.menuBar()->actions())
                if (auto *menu = action->menu(); menu && menu->title() == "Workbench")
                    for (auto *item : menu->actions())
                        if (item->objectName() == "filesystemStructureInspectorAction") return item;
            return nullptr;
        };
        QVERIFY(viewAction());
        QVERIFY(!viewAction()->isEnabled());
        const auto source = temp.filePath("working.raw");
        QVERIFY(QFile::copy(root + "/fixtures/ibm3740-empty.raw", source));
        QFile disk(source);
        QVERIFY(disk.open(QIODevice::ReadOnly));
        const auto bytes = disk.readAll();
        disk.close();
        QFile payload(temp.filePath("PROBE.BIN"));
        QVERIFY(payload.open(QIODevice::WriteOnly));
        QCOMPARE(payload.write(QByteArray(20000, 'P')), 20000);
        payload.close();

        QSignalSpy done(&first, &Window::completed);
        first.openImage(source, "ibm-3740", "raw");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY(done.takeFirst().first().toBool());
        auto *areas = first.findChild<QListWidget *>("userAreas");
        QVERIFY(areas);
        areas->setCurrentRow(1);
        QTimer approve;
        connect(&approve, &QTimer::timeout, &first, [] {
            auto *modal = QApplication::activeModalWidget();
            if (!modal) return;
            if (auto *message = qobject_cast<QMessageBox *>(modal)) {
                if (auto *yes = message->button(QMessageBox::Yes)) yes->click();
                else message->reject();
            } else if (auto *review = qobject_cast<QDialog *>(modal)) {
                if (review->windowTitle() == "Review import") review->accept();
                else review->reject();
            }
        });
        approve.start(20);
        first.importFiles({payload.fileName()});
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        approve.stop();
        QVERIFY(done.takeFirst().first().toBool());

        QVERIFY(viewAction()->isEnabled());
        auto *workbenchButton = first.findChild<QPushButton *>("workbench-filesystem");
        QVERIFY(workbenchButton && workbenchButton->isEnabled());
        workbenchButton->click();
        auto *inspector = first.findChild<QDialog *>("filesystemStructureInspector");
        QVERIFY(inspector);
        const auto button = [inspector](const char *name) { return inspector->findChild<QPushButton *>(name); };
        auto *table = inspector->findChild<QTableWidget *>("filesystemStructureRows");
        auto *page = inspector->findChild<QLabel *>("filesystemStructurePage");
        auto *summary = inspector->findChild<QLabel *>("filesystemStructureSummary");
        auto *pointers = inspector->findChild<QComboBox *>("filesystemExtentPointers");
        QVERIFY(table && page && summary && pointers);
        QTRY_COMPARE(table->rowCount(), 64);
        QVERIFY(summary->text().contains("ibm-3740"));
        QVERIFY(summary->text().contains("includes unsaved changes"));
        QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
        QVERIFY(table->item(1, 3));
        QCOMPARE(table->item(1, 3)->text(), QString("PROBE.BIN"));
        table->setCurrentCell(1, 0);
        const auto slot = table->item(1, 0)->text();
        button("filesystemGoToExtent")->click();
        QTRY_VERIFY(page->text().startsWith("File extents"));
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->item(table->currentRow(), 0)->text(), slot);
        QCOMPARE(table->item(table->currentRow(), 4)->text(), QString("1"));
        const int block = pointers->currentData().toInt();
        QVERIFY(block > 0);
        button("filesystemGoToBlock")->click();
        QTRY_VERIFY(page->text().startsWith("Allocation blocks"));
        QCOMPARE(table->item(table->currentRow(), 0)->text(), QString::number(block));
        QCOMPARE(table->item(table->currentRow(), 1)->text(), QString("Allocated"));
        QCOMPARE(table->item(0, 1)->text(), QString("Reserved for directory"));

        button("filesystemNext")->click();
        QTRY_VERIFY(page->text().contains("65–128"));
        QCOMPARE(table->item(0, 0)->text(), QString("64"));
        button("filesystemPrevious")->click();
        QTRY_VERIFY(page->text().contains("1–64"));
        QCOMPARE(table->item(0, 0)->text(), QString("0"));
        table->setCurrentCell(block, 0);
        button("filesystemShowReferences")->click();
        QTRY_VERIFY(page->text().startsWith("References to block " + QString::number(block)));
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), slot);
        button("filesystemGoToSlot")->click();
        QTRY_VERIFY(page->text().startsWith("Directory entries"));
        QCOMPARE(table->item(table->currentRow(), 0)->text(), slot);
        QCOMPARE(table->item(table->currentRow(), 3)->text(), QString("PROBE.BIN"));
        inspector->reject();
        QTRY_VERIFY(!first.findChild<QDialog *>("filesystemStructureInspector"));

        auto &second = workspace.addSession();
        QSignalSpy otherDone(&second, &Window::completed);
        second.openImage(root + "/fixtures/ibm3740-empty.raw", "ibm-3740", "raw");
        QTRY_COMPARE_WITH_TIMEOUT(otherDone.size(), 1, 15000);
        QVERIFY(otherDone.first().first().toBool());
        viewAction()->trigger();
        auto *other = second.findChild<QDialog *>("filesystemStructureInspector");
        QVERIFY(other);
        auto *otherTable = other->findChild<QTableWidget *>("filesystemStructureRows");
        QTRY_COMPARE(otherTable->rowCount(), 64);
        QCOMPARE(otherTable->item(0, 1)->text(), QString("Unused slot"));
        QVERIFY(other->findChild<QLabel *>("filesystemStructureSummary")->text().contains("no unsaved changes"));
        other->reject();

        QVERIFY(first.windowTitle().endsWith(" *"));
        QVERIFY(disk.open(QIODevice::ReadOnly));
        QCOMPARE(disk.readAll(), bytes);
        QVERIFY(!QFile::exists(temp.filePath("working.FDF")));
    }

    void compareImagesShowsFilesAndDirectoryDifferences() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        const auto helper = qEnvironmentVariable("CPMIMAGE_HELPER");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QFile::copy(root + "/fixtures/diskdefs", temp.filePath("diskdefs")));
        const auto leftPath = temp.filePath("left.raw");
        const auto rightPath = temp.filePath("right.raw");
        for (const auto &path : {leftPath, rightPath})
            QVERIFY(QFile::copy(root + "/fixtures/ibm3740-empty.raw", path));
        auto edit = [&](const QStringList &arguments) {
            QProcess process;
            process.setWorkingDirectory(temp.path());
            process.start(helper, arguments);
            return process.waitForFinished() && process.exitCode() == 0;
        };
        auto addFile = [&](const QString &image, const QString &name, char byte) {
            QFile input(temp.filePath("input.bin"));
            if (!input.open(QIODevice::WriteOnly)) return false;
            if (input.write(QByteArray(128, byte)) != 128) return false;
            input.close();
            return edit({"edit_import", image, "ibm-3740", "raw", name, input.fileName()});
        };
        QVERIFY(addFile(leftPath, "00SAME.BIN", 'S'));
        QVERIFY(addFile(leftPath, "00CHANGED.BIN", 'L'));
        QVERIFY(addFile(leftPath, "00ATTR.BIN", 'A'));
        QVERIFY(addFile(leftPath, "01MOVED.BIN", 'M'));
        QVERIFY(addFile(rightPath, "00CHANGED.BIN", 'R'));
        QVERIFY(addFile(rightPath, "00SAME.BIN", 'S'));
        QVERIFY(addFile(rightPath, "00ATTR.BIN", 'A'));
        QVERIFY(addFile(rightPath, "02MOVED.BIN", 'M'));
        QVERIFY(edit({"edit_attrs", rightPath, "ibm-3740", "raw", "00ATTR.BIN", "256"}));
        QFile leftDisk(leftPath), rightDisk(rightPath);
        QVERIFY(leftDisk.open(QIODevice::ReadOnly));
        QVERIFY(rightDisk.open(QIODevice::ReadOnly));
        const auto leftBytes = leftDisk.readAll(), rightBytes = rightDisk.readAll();
        leftDisk.close();
        rightDisk.close();

        Workspace workspace({root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"), helper, {}},
                            temp.filePath("workspace.ini"));
        workspace.show();
        auto &left = workspace.addSession();
        auto *action = workspace.findChild<QAction *>("compareImagesAction");
        QMenu *workbenchMenu = nullptr;
        for (auto *item : workspace.menuBar()->actions())
            if (item->menu() && item->menu()->title() == "Workbench")
                workbenchMenu = item->menu();
        QVERIFY(workbenchMenu && workbenchMenu->actions().contains(action));
        QCOMPARE(workbenchMenu->actions().size(), 5);
        QVERIFY(action);
        QVERIFY(!action->isEnabled());
        QSignalSpy leftDone(&left, &Window::completed);
        left.openImage(leftPath, "ibm-3740", "raw");
        QTRY_COMPARE_WITH_TIMEOUT(leftDone.size(), 1, 15000);
        QVERIFY(leftDone.takeFirst().first().toBool());
        QVERIFY(!action->isEnabled());
        auto &right = workspace.addSession();
        QSignalSpy rightDone(&right, &Window::completed);
        right.openImage(rightPath, "ibm-3740", "raw");
        QTRY_COMPARE_WITH_TIMEOUT(rightDone.size(), 1, 15000);
        QVERIFY(rightDone.takeFirst().first().toBool());
        workspace.findChild<QTabWidget *>()->setCurrentWidget(&left);
        QVERIFY(action->isEnabled());
        bool onWorkbenchMenu = false;
        for (auto *item : workspace.menuBar()->actions())
            if (auto *menu = item->menu(); menu && menu->title() == "Workbench")
                onWorkbenchMenu = menu->actions().contains(action);
        QVERIFY(onWorkbenchMenu);

        QFile pending(temp.filePath("PENDING.BIN"));
        QVERIFY(pending.open(QIODevice::WriteOnly));
        QCOMPARE(pending.write(QByteArray(128, 'U')), 128);
        pending.close();
        left.findChild<QListWidget *>("userAreas")->setCurrentRow(1);
        QTimer approve;
        connect(&approve, &QTimer::timeout, &left, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                if (auto *message = qobject_cast<QMessageBox *>(dialog)) {
                    if (auto *yes = message->button(QMessageBox::Yes)) yes->click();
                    else message->reject();
                } else if (dialog->windowTitle() == "Review import") dialog->accept();
                else dialog->reject();
            }
        });
        approve.start(20);
        left.importFiles({pending.fileName()});
        QTRY_COMPARE_WITH_TIMEOUT(leftDone.size(), 1, 15000);
        approve.stop();
        QVERIFY(leftDone.takeFirst().first().toBool());
        auto *workbenchButton = left.findChild<QPushButton *>("workbench-compare-images");
        QVERIFY(workbenchButton && workbenchButton->isEnabled());
        workbenchButton->click();
        auto *comparison = workspace.findChild<QDialog *>("imageComparison");
        QVERIFY(comparison);
        auto *target = comparison->findChild<QComboBox *>("imageComparisonTarget");
        QVERIFY(target);
        QCOMPARE(target->count(), 1);
        QVERIFY(target->currentText().contains("right.raw"));
        comparison->findChild<QPushButton *>("imageComparisonRun")->click();
        auto *files = comparison->findChild<QTableWidget *>("imageComparisonFiles");
        auto *directory = comparison->findChild<QTableWidget *>("imageComparisonDirectory");
        auto *summary = comparison->findChild<QLabel *>("imageComparisonSummary");
        QVERIFY(files && directory && summary);
        QTRY_COMPARE_WITH_TIMEOUT(files->rowCount(), 6, 15000);
        QVERIFY(summary->text().contains("left.raw"));
        QVERIFY(summary->text().contains("Unsaved changes included"));
        QCOMPARE(files->editTriggers(), QAbstractItemView::NoEditTriggers);
        const auto row = [files](const QString &name, int user = 0) {
            for (int index = 0; index < files->rowCount(); ++index)
                if (files->item(index, 0)->text().toInt() == user && files->item(index, 1)->text() == name) return index;
            return -1;
        };
        const int same = row("SAME.BIN"), changed = row("CHANGED.BIN"), attrs = row("ATTR.BIN");
        QVERIFY(same >= 0 && changed >= 0 && attrs >= 0);
        QCOMPARE(files->item(same, 2)->text(), QString("Identical"));
        QVERIFY(files->item(same, 3)->text().contains("Allocation"));
        QCOMPARE(files->item(changed, 2)->text(), QString("Content changed"));
        QCOMPARE(files->item(attrs, 2)->text(), QString("Attributes changed"));
        files->selectRow(changed);
        auto *viewRight = comparison->findChild<QPushButton *>("comparisonViewRight");
        QVERIFY(viewRight && viewRight->isEnabled());
        viewRight->click();
        QTRY_VERIFY(comparison->findChild<QPlainTextEdit *>("filePreviewText"));
        auto *viewer = comparison->findChild<QPlainTextEdit *>("filePreviewText");
        QCOMPARE(viewer->toPlainText(), QString(128, 'R'));
        qobject_cast<QDialog *>(viewer->window())->reject();

        const auto exportedPath = temp.filePath("CHANGED.BIN");
        auto *exportLeft = comparison->findChild<QPushButton *>("comparisonExportLeft");
        QVERIFY(exportLeft && exportLeft->isEnabled());
        const auto chooseExport = [exportLeft](const QString &destination) {
            const bool nativeDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
            const auto restore = qScopeGuard([nativeDisabled] {
                QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled);
            });
            QTimer chooser;
            QObject::connect(&chooser, &QTimer::timeout, exportLeft, [&] {
                if (auto *dialog = fileDialog()) {
                    if (destination.isEmpty()) dialog->reject();
                    else {
                        dialog->setDirectory(QFileInfo(destination).absolutePath());
                        QMetaObject::invokeMethod(dialog, "accept");
                    }
                }
            });
            chooser.start(20);
            exportLeft->click();
        };
        chooseExport({});
        QVERIFY(!QFile::exists(exportedPath));
        chooseExport(exportedPath);
        QTRY_VERIFY(exportLeft->isEnabled());
        QVERIFY2(QFile::exists(exportedPath), qPrintable(comparison->findChild<QLabel *>("imageComparisonStatus")->text()));
        QFile exported(exportedPath);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        QCOMPARE(exported.readAll(), QByteArray(128, 'L'));
        exported.close();
        QTRY_VERIFY(exportLeft->isEnabled());
        chooseExport(exportedPath);
        QTRY_VERIFY(comparison->findChild<QLabel *>("imageComparisonStatus")->text().contains("destination already exists"));
        QVERIFY(exported.open(QIODevice::ReadOnly));
        QCOMPARE(exported.readAll(), QByteArray(128, 'L'));


        files->selectRow(attrs);
        QVERIFY(comparison->findChild<QPlainTextEdit *>("imageComparisonFileDetails")->toPlainText().contains("Attributes RO"));
        auto *userAreas = comparison->findChild<QComboBox *>("imageComparisonArea");
        userAreas->setCurrentIndex(userAreas->findData(2));
        QCOMPARE(files->rowCount(), 1);
        QCOMPARE(files->item(0, 1)->text(), QString("MOVED.BIN"));
        QCOMPARE(files->item(0, 2)->text(), QString("Added"));
        comparison->findChild<QTabWidget *>()->setCurrentIndex(1);
        QVERIFY(directory->rowCount() >= 4);
        QVERIFY(comparison->findChild<QPlainTextEdit *>("imageComparisonSlotDetails")->toPlainText().contains("Blocks"));
        comparison->reject();
        QTRY_VERIFY(!workspace.findChild<QDialog *>("imageComparison"));
        QVERIFY(left.windowTitle().endsWith(" *"));
        QVERIFY(leftDisk.open(QIODevice::ReadOnly));
        QVERIFY(rightDisk.open(QIODevice::ReadOnly));
        QCOMPARE(leftDisk.readAll(), leftBytes);
        QCOMPARE(rightDisk.readAll(), rightBytes);
        QVERIFY(!QFile::exists(temp.filePath("left.FDF")));
        QVERIFY(!QFile::exists(temp.filePath("right.FDF")));
        right.close();
        QTRY_VERIFY(!action->isEnabled());
    }

    void filesystemFindingLinksToAllocationBlock() {
        QWidget parent;
        int block = -1;
        QString revision;
        auto *dialog = new FilesystemCheckDialog("active.raw", "ibm-3740",
            [](QJsonObject, FilesystemCheckDialog::Reply reply) {
                reply({{"status", "findings"}, {"mode", "working"}, {"revision", "checked-revision"},
                       {"block_count", 16}, {"finding_count", 2},
                       {"findings", QJsonArray{
                           QJsonObject{{"code", "reserved_block"}, {"slot", 2}, {"block", 1}},
                           QJsonObject{{"code", "block_out_of_range"}, {"slot", 3}, {"block", 20}}
                       }}}, {});
            }, [](int, QString) {}, [&](int selected, QString token) {
                block = selected;
                revision = token;
            }, &parent);
        dialog->show();
        dialog->findChild<QPushButton *>("filesystemCheckRun")->click();
        auto *go = dialog->findChild<QPushButton *>("filesystemCheckGoToBlock");
        auto *findings = dialog->findChild<QTableWidget *>("filesystemCheckFindings");
        QVERIFY(go && findings);
        QVERIFY(go->isEnabled());
        go->click();
        QCOMPARE(block, 1);
        QCOMPARE(revision, QString("checked-revision"));
        findings->selectRow(1);
        QVERIFY(!go->isEnabled());
        dialog->reject();

        QJsonObject request;
        auto *inspector = new FilesystemInspector(
            [&](QJsonObject fields, FilesystemInspector::Reply reply) {
                request = fields;
                reply({}, "The working image changed. Reload the results.");
            }, "active.raw", &parent, -1, revision, block);
        inspector->show();
        QTRY_COMPARE(request["view"].toString(), QString("blocks"));
        QCOMPARE(request["revision"].toString(), revision);
        auto *status = inspector->findChild<QLabel *>("filesystemStructurePage");
        QVERIFY(status && status->text().contains("working image changed"));
        QCOMPARE(inspector->findChild<QTableWidget *>("filesystemStructureRows")->rowCount(), 0);
        inspector->reject();
    }

    void filesystemRepairPlanIsReadOnly() {
        QWidget parent;
        QStringList operations;
        auto *dialog = new FilesystemCheckDialog("active.raw", "ibm-3740",
            [&](QJsonObject fields, FilesystemCheckDialog::Reply reply) {
                operations << fields["op"].toString();
                reply({{"status", "findings"}, {"mode", "working"}, {"format", "ibm-3740"},
                       {"finding_count", 3}, {"omitted", 1},
                       {"findings", QJsonArray{
                           QJsonObject{{"code", "invalid_extent_low"}, {"slot", 2}, {"observed", 35}},
                           QJsonObject{{"code", "shared_block"}, {"slot", 3}, {"related_slot", 2}, {"block", 8}}
                       }}}, {});
            }, [](int, QString) {}, [](int, QString) {}, &parent);
        dialog->show();
        auto *review = dialog->findChild<QPushButton *>("filesystemCheckReviewPlan");
        QVERIFY(review && !review->isEnabled());
        dialog->findChild<QPushButton *>("filesystemCheckRun")->click();
        QVERIFY(review->isEnabled());
        bool checked = false;
        QTimer::singleShot(0, dialog, [&] {
            auto *plan = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(plan);
            const auto close = qScopeGuard([plan] { plan->reject(); });
            const auto *details = plan->findChild<QPlainTextEdit *>("filesystemRepairPlan");
            QVERIFY(details && details->isReadOnly());
            const auto text = details->toPlainText();
            QVERIFY(text.contains("Partial plan"));
            QVERIFY(text.contains("Slot 2 · Observed 35"));
            QVERIFY(text.contains("35 → 3"));
            QVERIFY(text.contains("Block 8 · Related slot 2"));
            QVERIFY(text.contains("until ownership is verified"));
            checked = true;
        });
        review->click();
        QVERIFY(checked);
        QCOMPARE(operations, QStringList{"filesystem_check"});
        dialog->reject();
    }

    void filesystemCheckActiveFindingLinksToSlot() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QFile fixture(root + "/fixtures/ibm3740-empty.raw");
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        auto bytes = fixture.readAll();
        QByteArray entry(32, '\0');
        entry.replace(1, 11, "FILE    BIN");
        entry[12] = char(32);
        entry[15] = char(1);
        entry[16] = char(2);
        bytes.replace(2 * 26 * 128 + 2 * 32, entry.size(), entry);
        QFile image(temp.filePath("active.raw"));
        QVERIFY(image.open(QIODevice::WriteOnly));
        QCOMPARE(image.write(bytes), bytes.size());
        image.close();

        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {}, temp.filePath("settings.ini"));
        window.show();
        QSignalSpy done(&window, &Window::completed);
        window.openImage(image.fileName(), "ibm-3740", "raw");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY(done.first().first().toBool());
        auto *action = window.findChild<QAction *>("filesystemCheckAction");
        QVERIFY(action && action->isEnabled());
        auto *workbenchButton = window.findChild<QPushButton *>("workbench-check-filesystem");
        QVERIFY(workbenchButton && workbenchButton->isEnabled());
        workbenchButton->click();
        auto *dialog = window.findChild<QDialog *>("filesystemCheckDialog");
        QVERIFY(dialog);
        auto *mode = dialog->findChild<QComboBox *>("filesystemCheckMode");
        QVERIFY(mode && mode->currentText().contains("Active tab"));
        auto *run = dialog->findChild<QPushButton *>("filesystemCheckRun");
        auto *report = dialog->findChild<QTableWidget *>("filesystemCheckFindings");
        auto *identity = dialog->findChild<QLabel *>("filesystemCheckIdentity");
        auto *goToSlot = dialog->findChild<QPushButton *>("filesystemCheckGoToSlot");
        QVERIFY(run && report && identity && goToSlot);
        run->click();
        QTRY_COMPARE(report->rowCount(), 1);
        QVERIFY(identity->text().contains("Working image · ibm-3740"));
        QCOMPARE(report->editTriggers(), QAbstractItemView::NoEditTriggers);
        QCOMPARE(report->item(0, 0)->text(), QString("Invalid low extent byte"));
        QCOMPARE(report->item(0, 1)->text(), QString("2"));
        QCOMPARE(report->item(0, 6)->text(), QString("32"));
        QVERIFY(goToSlot->isVisible() && goToSlot->isEnabled());
        goToSlot->click();
        auto *inspector = window.findChild<QDialog *>("filesystemStructureInspector");
        QVERIFY(inspector);
        auto *directory = inspector->findChild<QTableWidget *>("filesystemStructureRows");
        QVERIFY(directory);
        QTRY_COMPARE(directory->rowCount(), 64);
        QCOMPARE(directory->item(directory->currentRow(), 0)->text(), QString("2"));
        inspector->reject();
        auto *review = dialog->findChild<QPushButton *>("filesystemCheckReviewPlan");
        bool confirmed = false;
        QTimer::singleShot(0, dialog, [&] {
            auto *plan = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(plan);
            const auto close = qScopeGuard([plan] { if (plan->isVisible()) plan->reject(); });
            auto *apply = plan->findChild<QPushButton *>("filesystemRepairApply");
            QVERIFY(apply);
            QTimer::singleShot(0, plan, [&] {
                auto *approval = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(approval);
                approval->button(QMessageBox::Cancel)->click();
            });
            apply->click();
            QVERIFY(plan->isVisible());
            QVERIFY(!window.windowTitle().endsWith(" *"));
            QTimer::singleShot(0, plan, [&] {
                auto *approval = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(approval);
                confirmed = true;
                approval->button(QMessageBox::Yes)->click();
            });
            apply->click();
        });
        review->click();
        QVERIFY(confirmed);
        QTRY_VERIFY(window.windowTitle().endsWith(" *"));
        QTRY_VERIFY(dialog->findChild<QLabel *>("filesystemCheckStatus")->text().contains("Repair applied"));
        dialog->reject();
        QVERIFY(image.open(QIODevice::ReadOnly));
        QCOMPARE(image.readAll(), bytes);
        QVERIFY(!QFile::exists(temp.filePath("active.FDF")));
    }

    void filesystemCheckExternalUnmountableAndIncomplete() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QFile fixture(root + "/fixtures/ibm3740-empty.raw");
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto clean = fixture.readAll();
        auto damaged = clean;
        damaged[2 * 26 * 128] = char(0x7f);
        auto write = [&temp](const QString &name, const QByteArray &bytes) {
            QFile file(temp.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(write("damaged.raw", damaged));
        QVERIFY(write("short.raw", clean.left(6657)));
        QVERIFY(write("clean.raw", clean));
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {}, temp.filePath("settings.ini"));
        window.show();
        QSignalSpy done(&window, &Window::completed);
        window.openImage(temp.filePath("damaged.raw"), "ibm-3740", "raw");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY(!done.first().first().toBool());
        auto *action = window.findChild<QAction *>("filesystemCheckAction");
        QVERIFY(action && action->isEnabled());
        bool onWorkbench = false;
        for (auto *item : window.menuBar()->actions())
            if (auto *menu = item->menu(); menu && menu->title() == "Workbench")
                onWorkbench = menu->actions().contains(action);
        QVERIFY(onWorkbench);
        action->trigger();
        auto *dialog = window.findChild<QDialog *>("filesystemCheckDialog");
        QVERIFY(dialog);
        auto *mode = dialog->findChild<QComboBox *>("filesystemCheckMode");
        auto *format = dialog->findChild<QComboBox *>("filesystemCheckFormat");
        auto *path = dialog->findChild<QLineEdit *>("filesystemCheckPath");
        auto *run = dialog->findChild<QPushButton *>("filesystemCheckRun");
        auto *status = dialog->findChild<QLabel *>("filesystemCheckStatus");
        auto *identity = dialog->findChild<QLabel *>("filesystemCheckIdentity");
        auto *findings = dialog->findChild<QTableWidget *>("filesystemCheckFindings");
        QVERIFY(mode && format && path && run && status && identity && findings);
        QCOMPARE(mode->currentText(), QString("External image"));
        QTRY_VERIFY_WITH_TIMEOUT(format->findData("ibm-3740") >= 0, 15000);
        path->setText(temp.filePath("damaged.raw"));
        QVERIFY(!run->isEnabled());
        format->setCurrentIndex(format->findData("ibm-3740"));
        QVERIFY(run->isEnabled());
        run->click();
        QTRY_COMPARE(findings->rowCount(), 1);
        QVERIFY(identity->text().contains("damaged.raw"));
        QVERIFY(identity->text().contains("SHA-256"));
        QCOMPARE(findings->item(0, 0)->text(), QString("Invalid directory status"));
        QVERIFY(!dialog->findChild<QPushButton *>("filesystemCheckGoToSlot")->isVisible());

        path->setText(temp.filePath("short.raw"));
        QCOMPARE(findings->rowCount(), 0);
        run->click();
        QTRY_VERIFY(status->text().contains("Check incomplete while reading the directory"));
        QCOMPARE(findings->rowCount(), 0);
        path->setText(temp.filePath("clean.raw"));
        run->click();
        QTRY_VERIFY(status->text().contains("No inconsistencies found"));
        dialog->reject();
        QFile original(temp.filePath("damaged.raw"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), damaged);
        QVERIFY(!QFile::exists(temp.filePath("damaged.FDF")));
    }

    void filesystemRepairPlanComparesReferenceSlots() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QFile fixture(root + "/fixtures/ibm3740-empty.raw");
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        auto good = fixture.readAll();
        constexpr int directory = 2 * 26 * 128;
        for (int i = 0; i < 2; ++i) {
            QByteArray entry(32, '\0');
            entry.replace(1, 11, i == 0 ? "FIRST   BIN" : "SECOND  BIN");
            entry[15] = 1;
            entry[16] = char(2 + i);
            good.replace(directory + i * 32, 32, entry);
        }
        auto damaged = good;
        damaged[directory + 12] = 35;
        damaged[directory + 32 + 16] = 2;
        const auto goodPath = temp.filePath("Known Good.raw");
        const auto damagedPath = temp.filePath("Damaged.raw");
        for (const auto &[path, bytes] : {std::pair{goodPath, good}, {damagedPath, damaged}}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
        }
        Workspace workspace({root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                             qEnvironmentVariable("CPMIMAGE_HELPER"), {}}, temp.filePath("workspace.ini"));
        workspace.show();
        auto &reference = workspace.addSession();
        QSignalSpy opened(&reference, &Window::completed);
        reference.openImage(goodPath, "ibm-3740", "raw");
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 15000);
        QVERIFY(opened.first().first().toBool());
        auto &empty = workspace.addSession();
        empty.findChild<QAction *>("filesystemCheckAction")->trigger();
        auto *check = empty.findChild<QDialog *>("filesystemCheckDialog");
        QVERIFY(check);
        auto *format = check->findChild<QComboBox *>("filesystemCheckFormat");
        QTRY_VERIFY_WITH_TIMEOUT(format->findData("ibm-3740") >= 0, 15000);
        format->setCurrentIndex(format->findData("ibm-3740"));
        check->findChild<QLineEdit *>("filesystemCheckPath")->setText(damagedPath);
        check->findChild<QPushButton *>("filesystemCheckRun")->click();
        auto *review = check->findChild<QPushButton *>("filesystemCheckReviewPlan");
        QTRY_VERIFY_WITH_TIMEOUT(review->isEnabled(), 15000);
        bool checked = false;
        QTimer::singleShot(0, check, [&] {
            auto *plan = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(plan);
            const auto close = qScopeGuard([plan] { plan->reject(); });
            auto *referenceChoice = plan->findChild<QComboBox *>("filesystemRepairReference");
            auto *slot = plan->findChild<QComboBox *>("filesystemRepairReferenceSlot");
            auto *evidence = plan->findChild<QPlainTextEdit *>("filesystemRepairReferenceEvidence");
            QVERIFY(referenceChoice && slot && evidence);
            QCOMPARE(referenceChoice->count(), 2);
            referenceChoice->setCurrentIndex(1);
            QTRY_VERIFY(evidence->toPlainText().contains("FIRST.BIN"));
            QVERIFY(evidence->toPlainText().contains("Pointer 1 → block 2"));
            slot->setCurrentIndex(slot->findData(1));
            QTRY_VERIFY(evidence->toPlainText().contains("SECOND.BIN"));
            QVERIFY(evidence->toPlainText().contains("Pointer 1 → block 3"));
            checked = true;
        });
        review->click();
        QVERIFY(checked);
        check->reject();
        for (const auto &[path, bytes] : {std::pair{goodPath, good}, {damagedPath, damaged}}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), bytes);
        }
    }

    void flatExportConsolidatesIdenticalHostFile() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"));
        QSignalSpy done(&window, &Window::completed);
        window.openImage(root + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(15000));
        QVERIFY(done.takeFirst()[0].toBool());
        auto *areas = window.findChild<QListWidget *>("userAreas");
        auto *files = window.findChild<QTableWidget *>("fileList");
        areas->setCurrentRow(0);
        for (int row = 0; row < files->rowCount(); ++row)
            if (files->item(row, 0)->data(Qt::UserRole).toString() == "00BINARY.BIN")
                window.selectEntry(row);
        QTemporaryDir output;
        QVERIFY(output.isValid());
        window.exportSelection(output.path());
        QVERIFY(done.wait(15000));
        QVERIFY(QFile::exists(output.filePath("BINARY.BIN")));
        done.clear();
        window.exportSelection(output.path());
        QVERIFY(done.wait(15000));
        QVERIFY(window.lastBatchReport().contains("Consolidated identical contents"));
    }

    void workspaceSwitchAndClose_data() {
        QTest::addColumn<bool>("saveOnClose");
        QTest::newRow("discard") << false;
        QTest::newRow("save") << true;
    }

    void workspaceSwitchAndClose() {
        QFETCH(bool, saveOnClose);
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        Workspace workspace({root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                             qEnvironmentVariable("CPMIMAGE_HELPER"), {}});
        workspace.show();
        auto &first = workspace.addSession();
        QSignalSpy firstDone(&first, &Window::completed);
        first.openImage(root + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        auto &second = workspace.addSession();
        QSignalSpy secondDone(&second, &Window::completed);
        second.openImage(root + "/fixtures/ibm3740-empty.raw", "ibm-3740", "raw");
        QTRY_COMPARE(firstDone.size(), 1);
        QTRY_COMPARE(secondDone.size(), 1);
        QVERIFY(firstDone.first().first().toBool());
        QVERIFY(secondDone.first().first().toBool());
        auto *tabs = workspace.findChild<QTabWidget *>();
        QCOMPARE(tabs->currentWidget(), &second);
        auto *space = first.findChild<QLabel *>("diskSpace");
        QVERIFY(space);
        QCOMPARE(space->text(), QString("238/243 KiB free"));
        auto *emptySpace = second.findChild<QLabel *>("diskSpace");
        QVERIFY(emptySpace);
        QCOMPARE(emptySpace->text(), QString("241/243 KiB free"));
        QCOMPARE(first.entryCount(), 1);
        QCOMPARE(second.entryCount(), 0);
        tabs->setCurrentWidget(&first);
        auto *areas = first.findChild<QListWidget *>("userAreas");
        areas->setCurrentRow(0);
        tabs->setCurrentWidget(&second);
        QVERIFY(second.close());
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(tabs->currentWidget(), &first);
        QCOMPARE(areas->currentRow(), 0);
        QCOMPARE(first.entryCount(), 3);
        areas->setCurrentRow(1);
        QAction *remove = nullptr;
        for (auto *action : first.findChildren<QAction *>())
            if (action->text() == "Delete…") remove = action;
        QVERIFY(remove);
        QTimer respond;
        QMessageBox::StandardButton choice = QMessageBox::Yes;
        connect(&respond, &QTimer::timeout, &workspace, [&] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!dialog) return;
            const auto answer = choice == QMessageBox::Save && dialog->button(QMessageBox::Yes) ? QMessageBox::Yes : choice;
            if (auto *button = dialog->button(answer)) button->click();
        });
        // Edit a disposable image so ownership and export tests can run independently.
        QTemporaryDir scratch;
        const auto copy = scratch.filePath("dirty.raw");
        QVERIFY(QFile::copy(root + "/fixtures/ibm3740-independent.raw", copy));
        firstDone.clear();
        first.openImage(copy, "ibm-3740", "raw");
        QTRY_COMPARE(firstDone.size(), 1);
        QVERIFY(firstDone.takeFirst().first().toBool());
        respond.start(20);
        remove->trigger();
        QTRY_COMPARE(firstDone.size(), 1);
        QVERIFY(firstDone.takeFirst().first().toBool());
        QCOMPARE(space->text(), QString("239/243 KiB free"));
        QVERIFY(first.windowTitle().endsWith("*"));
        respond.stop();
        QString dialogTitle;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &workspace, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                dialogTitle = dialog->windowTitle();
                dialog->reject();
            }
        });
        auto *create = first.findChild<QPushButton *>("toolbarNew");
        QVERIFY(create);
        dismiss.start(20);
        create->click();
        QTRY_VERIFY(!dialogTitle.isEmpty());
        dismiss.stop();
        QCOMPARE(dialogTitle, QString("Choose disk format"));
        QCOMPARE(tabs->count(), 2);
        QVERIFY(tabs->currentWidget() != &first);
        QVERIFY(first.windowTitle().endsWith("*"));
        QVERIFY(tabs->currentWidget()->close());
        QCOMPARE(tabs->currentWidget(), &first);
        QTimer initialize;
        connect(&initialize, &QTimer::timeout, &workspace, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (dialog->windowTitle() == "Choose disk format") {
                dialog->findChild<QLineEdit *>()->setText("ibm-3740");
                dialog->accept();
            } else if (dialog->windowTitle() == "Filesystem initialization") {
                dialog->accept();
            } else {
                dialog->reject();
            }
        });
        initialize.start(20);
        create->click();
        QCOMPARE(tabs->count(), 2);
        auto *created = tabs->currentWidget();
        QVERIFY(created != &first);
        QTRY_VERIFY(created->windowTitle().endsWith("*"));
        initialize.stop();
        QVERIFY(first.windowTitle().endsWith("*"));
        QCOMPARE(tabs->tabText(0), QString("dirty.raw *"));
        tabs->setCurrentWidget(&first);
        respond.start(20);
        choice = QMessageBox::Discard;
        QVERIFY(!created->close());
        QTRY_COMPARE(tabs->count(), 1);
        choice = QMessageBox::Cancel;
        QVERIFY(!first.close());
        QCOMPARE(tabs->count(), 1);
        QVERIFY(first.windowTitle().endsWith("*"));
        choice = saveOnClose ? QMessageBox::Save : QMessageBox::Discard;
        QVERIFY(!first.close());
        QTRY_COMPARE(tabs->tabText(0), QString("Untitled"));
        QCOMPARE(tabs->count(), 1);
        QVERIFY(workspace.isVisible());
        QVERIFY(workspace.close());
        QCOMPARE(tabs->count(), 0);
        QFile result(copy);
        QFile original(root + "/fixtures/ibm3740-independent.raw");
        QVERIFY(result.open(QIODevice::ReadOnly) && original.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll() != original.readAll(), saveOnClose);
        QCOMPARE(QFile::exists(copy + ".BAK"), saveOnClose);
    }

    void workspaceCloseUnsavedTabs() {
        const auto maslin = qEnvironmentVariable("CPMIMAGE_MASLIN");
        if (maslin.isEmpty()) QSKIP("External Maslin corpus unavailable");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        Workspace workspace({qEnvironmentVariable("CPMIMAGE_ROOT"),
                             qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                             qEnvironmentVariable("CPMIMAGE_HELPER"), {}});
        workspace.show();
        auto *tabs = workspace.findChild<QTabWidget *>();
        QVERIFY(tabs);
        for (int i = 0; i < 3; ++i) {
            const auto source = temp.filePath(QString("XEROX-%1.TD0").arg(i));
            QVERIFY(QFile::copy(maslin + "/1995/sydex/xerox/820sys8s.td0", source));
            auto &page = workspace.addSession();
            page.inspectImage(source, "ibm-3740");
            QTRY_COMPARE_WITH_TIMEOUT(tabs->tabText(i), QString("XEROX-%1.raw *").arg(i), 15000);
        }
        int prompts = 0;
        bool cancelSecond = true;
        QTimer respond;
        connect(&respond, &QTimer::timeout, &workspace, [&] {
            auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!prompt || !prompt->button(QMessageBox::Discard)) return;
            ++prompts;
            prompt->button(cancelSecond && prompts == 2 ? QMessageBox::Cancel : QMessageBox::Discard)->click();
        });
        respond.start(20);
        QVERIFY(!workspace.close());
        QTRY_COMPARE(prompts, 2);
        QVERIFY(workspace.isVisible());
        QCOMPARE(tabs->count(), 2);
        cancelSecond = false;
        QVERIFY(!workspace.close());
        QTRY_VERIFY(!workspace.isVisible());
        QCOMPARE(prompts, 4);
        QCOMPARE(tabs->count(), 0);
    }

    void workbenchToolbarPreservesDraftAndExit() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto profile = temp.filePath("workspace.ini");
        const RuntimePaths paths{qEnvironmentVariable("CPMIMAGE_ROOT"),
                                 qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                                 qEnvironmentVariable("CPMIMAGE_HELPER"), {}};
        Workspace workspace(paths, profile);
        workspace.show();
        auto &page = workspace.addSession();
        auto *edit = page.findChild<QAction *>("diskFormatEditorAction");
        auto *menu = page.findChild<QMenu *>("workbenchMenu");
        auto *toolbarToggle = page.findChild<QAction *>("workbenchToolbarVisible");
        auto *toolbar = page.findChild<QWidget *>("workbenchToolbar");
        QVERIFY(edit && menu && toolbarToggle && toolbar);
        const auto originalToolbar = toolbarToggle->isChecked();
        const auto restoreToolbar = qScopeGuard([originalToolbar] {
            QSettings("CPMArchives", "cpmimage").setValue("workbenchToolbarVisible", originalToolbar);
        });
        QVERIFY(!workspace.findChild<QAction *>("enableWorkbenchAction"));
        QVERIFY(workspace.menuBar()->actions().contains(menu->menuAction()));
        QVERIFY(edit->isVisible() && edit->isEnabled());
        toolbarToggle->setChecked(true);
        QVERIFY(toolbar->isVisible());
        edit->trigger();
        auto *editor = page.findChild<QDialog *>("diskFormatEditor");
        QVERIFY(editor);
        auto *identifier = editor->findChild<QLineEdit *>("diskFormatIdentifier");
        QVERIFY(identifier);
        QTRY_VERIFY(identifier->isEnabled());
        identifier->setText("retained-draft");
        toolbarToggle->setChecked(false);
        QVERIFY(editor->isVisible());
        QVERIFY(toolbar->isHidden());
        QVERIFY(workspace.menuBar()->actions().contains(menu->menuAction()));

        for (const auto *choice : {"Cancel and Return", "Review Draft"}) {
            bool offered = false;
            QTimer::singleShot(0, &workspace, [&] {
                auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(warning);
                QCOMPARE(warning->buttons().size(), 2);
                for (auto *button : warning->buttons()) {
                    if (button->text() != QLatin1String(choice)) continue;
                    offered = true;
                    button->click();
                    return;
                }
                warning->reject();
            });
            QVERIFY(!workspace.close());
            QVERIFY(offered);
            QVERIFY(workspace.isVisible());
            QCOMPARE(identifier->text(), QString("retained-draft"));
            QVERIFY(editor->isVisible());
        }
        identifier->clear();
        workspace.close();
        QTRY_VERIFY(!workspace.isVisible());
    }

    void workbenchDiskAnalysis() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {},
                      temp.filePath("settings.ini"));
        auto *button = window.findChild<QPushButton *>("workbench-analyze-disk");
        QVERIFY(button && !button->isEnabled());
        window.show();
        window.openImage(root + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 15000);
        button->click();
        auto *results = window.findChild<QListWidget *>("diskAnalysisCandidates");
        QVERIFY(results);
        QTRY_VERIFY_WITH_TIMEOUT(results->count() > 0, 30000);
        const auto matches = results->findItems("ibm-3740 —", Qt::MatchStartsWith);
        QVERIFY(!matches.isEmpty());
        auto *search = results->parentWidget()->findChild<QLineEdit *>();
        QVERIFY(search);
        search->setText("no-such-definition");
        QVERIFY(matches.first()->isHidden());
        search->clear();
        QVERIFY(!matches.first()->isHidden());
    }

    void toolbarVisibility() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto check = [&](bool mainVisible, bool workbenchVisible, bool toggle) {
            Window window(qEnvironmentVariable("CPMIMAGE_ROOT"),
                          qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                          qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {},
                          temp.filePath("settings.ini"));
            auto *main = window.findChild<QWidget *>("mainToolbar");
            auto *workbench = window.findChild<QWidget *>("workbenchToolbar");
            QVERIFY(main && workbench);
            QCOMPARE(!main->isHidden(), mainVisible);
            QCOMPARE(!workbench->isHidden(), workbenchVisible);
            if (toggle) {
                for (auto *action : window.findChildren<QAction *>())
                    if (action->text() == "Toolbar" || action->text() == "Workbench Toolbar")
                        action->trigger();
                QVERIFY(main->isHidden());
                QVERIFY(!workbench->isHidden());
            }
        };
        check(true, false, true);
        check(false, true, false);
    }

    void workspaceCloseSavingUnsavedTabs() {
        const auto maslin = qEnvironmentVariable("CPMIMAGE_MASLIN");
        if (maslin.isEmpty()) QSKIP("External Maslin corpus unavailable");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        Workspace workspace({qEnvironmentVariable("CPMIMAGE_ROOT"),
                             qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                             qEnvironmentVariable("CPMIMAGE_HELPER"), {}});
        workspace.show();
        auto *tabs = workspace.findChild<QTabWidget *>();
        QVERIFY(tabs);
        for (int i = 0; i < 4; ++i) {
            const auto source = temp.filePath(QString("XEROX-%1.TD0").arg(i));
            QVERIFY(QFile::copy(maslin + "/1995/sydex/xerox/820sys8s.td0", source));
            workspace.addSession().inspectImage(source, "ibm-3740");
            QTRY_COMPARE_WITH_TIMEOUT(tabs->tabText(i), QString("XEROX-%1.raw *").arg(i), 15000);
        }
        const bool nativeDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore = qScopeGuard([nativeDisabled] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled);
        });
        QVERIFY(QDir(temp.path()).mkdir("saved"));
        const QDir destination(temp.filePath("saved"));
        QStringList saveDirectories;
        int savePrompts = 0;
        QTimer respond;
        connect(&respond, &QTimer::timeout, &workspace, [&] {
            if (auto *chooser = fileDialog()) {
                if (auto *filename = chooser->findChild<QLineEdit *>("fileNameEdit")) {
                    saveDirectories.append(chooser->directory().canonicalPath());
                    filename->setText(destination.filePath(QFileInfo(filename->text()).fileName()));
                    QMetaObject::invokeMethod(chooser, "accept");
                }
            } else if (auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *save = prompt->button(QMessageBox::Save)) {
                    ++savePrompts;
                    save->click();
                } else if (auto *yes = prompt->button(QMessageBox::Yes)) {
                    yes->click();
                }
            }
        });
        respond.start(20);
        QVERIFY(!workspace.close());
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.isVisible(), 15000);
        QCOMPARE(savePrompts, 4);
        QCOMPARE(saveDirectories.size(), 4);
        for (const auto &directory : saveDirectories.mid(1))
            QCOMPARE(directory, destination.canonicalPath());
        QCOMPARE(tabs->count(), 0);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(QFileInfo(destination.filePath(QString("XEROX-%1.raw").arg(i))).size(), qint64(256256));
    }

    void teleDiskImportSave() {
        const auto maslin = qEnvironmentVariable("CPMIMAGE_MASLIN");
        if (maslin.isEmpty()) QSKIP("External Maslin corpus unavailable");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto source = temp.filePath("XEROX.TD0");
        const auto destination = temp.filePath("XEROX.raw");
        QVERIFY(QFile::copy(maslin + "/1995/sydex/xerox/820sys8s.td0", source));
        Workspace workspace({qEnvironmentVariable("CPMIMAGE_ROOT"),
                             qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                             qEnvironmentVariable("CPMIMAGE_HELPER"), {}});
        workspace.show();
        auto &window = workspace.addSession();
        window.inspectImage(source, "ibm-3740");
        auto *tabs = workspace.findChild<QTabWidget *>();
        QVERIFY(tabs);
        QTRY_COMPARE_WITH_TIMEOUT(tabs->tabText(tabs->currentIndex()),
                                  QString("XEROX.raw *"), 15000);
        QVERIFY(!QFile::exists(destination));

        QAction *save = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == "Save") save = action;
        QVERIFY(save && save->isEnabled());
        QString suggestion;
        QTimer confirm;
        connect(&confirm, &QTimer::timeout, &window, [&] {
            if (auto *chooser = fileDialog()) {
                if (auto *filename = chooser->findChild<QLineEdit *>("fileNameEdit")) {
                    suggestion = QFileInfo(filename->text()).fileName();
                    filename->setText(destination);
                    QMetaObject::invokeMethod(chooser, "accept");
                }
            } else if (auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *yes = prompt->button(QMessageBox::Yes)) yes->click();
                else prompt->reject();
            }
        });
        confirm.start(20);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        save->trigger();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
        QCOMPARE(suggestion, QString("XEROX.raw"));
        QTRY_COMPARE_WITH_TIMEOUT(tabs->tabText(tabs->currentIndex()),
                                  QString("XEROX.raw"), 15000);
        QCOMPARE(QFileInfo(destination).size(), qint64(256256));
    }

    void saveWorkflow() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        const auto path = temp.filePath("test.raw");
        QVERIFY(QFile::copy(root + "/fixtures/ibm3740-independent.raw", path));
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
            qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {}, temp.filePath("settings.ini"));
        window.show();
        QSignalSpy done(&window, &Window::completed);
        window.openImage(path, "ibm-3740", "raw");
        QVERIFY(done.wait(15000));
        QVERIFY(done.takeFirst()[0].toBool());
        auto findAction = [&](const QString &label) -> QAction * {
            for (auto *action : window.findChildren<QAction *>())
                if (action->text() == label) return action;
            return nullptr;
        };
        auto *save = findAction("Save");
        auto *saveAs = findAction("Save As…");
        auto *discard = findAction("Discard unsaved changes…");
        auto *remove = findAction("Delete…");
        QVERIFY(saveAs && discard && remove);
        QVERIFY(save);
        QTimer confirm;
        connect(&confirm, &QTimer::timeout, &window, [&] {
            auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (prompt) {
                if (auto *yes = prompt->button(QMessageBox::Yes)) yes->click();
                else prompt->reject();
            }
        });
        const auto destination = temp.filePath("copy.raw");
        connect(&confirm, &QTimer::timeout, &window, [&] {
            if (auto *chooser = fileDialog()) {
                if (auto *filename = chooser->findChild<QLineEdit *>("fileNameEdit")) {
                    filename->setText(destination);
                    QMetaObject::invokeMethod(chooser, "accept");
                }
            }
        });
        confirm.start(20);
        save->trigger();
        QVERIFY(done.wait(15000));
        QVERIFY2(done.takeFirst()[0].toBool(), qPrintable(window.lastError()));
        QVERIFY(QFile::exists(temp.filePath("test.FDF")));
        QVERIFY(!QFile::exists(temp.filePath("test.raw.BAK")));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        saveAs->trigger();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
        QVERIFY(done.wait(15000));
        QVERIFY2(done.takeFirst()[0].toBool(), qPrintable(window.lastError()));
        QVERIFY(QFile::exists(destination));
        QVERIFY(window.windowTitle().contains("copy.raw"));
        const auto entries = window.entryCount();
        QVERIFY(entries > 0);
        window.selectEntry(0);
        remove->trigger();
        QVERIFY(done.wait(15000));
        QVERIFY2(done.takeFirst()[0].toBool(), qPrintable(window.lastError()));
        QCOMPARE(window.entryCount(), entries - 1);
        QVERIFY(window.windowTitle().endsWith("*"));
        discard->trigger();
        QVERIFY(done.wait(15000));
        QVERIFY2(done.takeFirst()[0].toBool(), qPrintable(window.lastError()));
        QCOMPARE(window.entryCount(), entries);
        QVERIFY(!window.windowTitle().endsWith("*"));
        QFile saved(destination), original(path);
        QVERIFY(saved.open(QIODevice::ReadOnly) && original.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), original.readAll());
    }

    void formatPickerExplainsRankAndRejection() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QFile catalogue(temp.filePath("diskdefs"));
        QVERIFY(catalogue.open(QIODevice::WriteOnly));
        for (const auto &[id, tracks] : QList<QPair<QByteArray, int>>{
                 {"alpha", 77}, {"beta", 77}, {"larger", 80}, {"too-small", 70}}) {
            catalogue.write("diskdef " + id + "\n seclen 128\n tracks " + QByteArray::number(tracks)
                            + "\n sectrk 26\n blocksize 1024\n maxdir 64\n skew 6\n bootsec 52\n os 2.2\nend\n");
        }
        catalogue.close();
        const auto path = temp.filePath("short.raw");
        QVERIFY(QFile::copy(root + "/fixtures/ibm3740-independent.raw", path));
        QFile image(path);
        QVERIFY(image.open(QIODevice::ReadWrite));
        QVERIFY(image.resize(image.size() - 128));
        image.close();

        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {}, temp.filePath("settings.ini"));
        window.show();
        QSignalSpy done(&window, &Window::completed);
        window.loadCatalogue(catalogue.fileName());
        QVERIFY(done.wait(30000));
        QVERIFY(done.takeFirst().first().toBool());

        QTimer poll;
        bool checked = false;
        connect(&poll, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->windowTitle() != "Choose disk format") return;
            poll.stop();
            const auto close = qScopeGuard([dialog] { dialog->reject(); });
            auto *list = dialog->findChild<QListWidget *>("formatPickerList");
            auto *findings = dialog->findChild<QPlainTextEdit *>("formatPickerDiagnostics");
            auto *group = dialog->findChild<QComboBox *>();
            QVERIFY(list && findings && group);
            QCOMPARE(list->count(), 3);
            QVERIFY(findings->isReadOnly());
            QVERIFY(findings->toPlainText().contains("Rank 1 of 3"));
            QVERIFY(findings->toPlainText().contains("equal evidence are ordered by format ID"));
            QVERIFY(findings->toPlainText().contains("128 bytes short"));
            QVERIFY(findings->toPlainText().contains("CP/M directory readable"));
            list->setCurrentRow(2);
            QVERIFY(findings->toPlainText().contains("Ranks below beta because fewer missing bytes rank higher"));
            group->setCurrentIndex(1);
            const auto rejected = list->findItems("too-small", Qt::MatchExactly);
            QCOMPARE(rejected.size(), 1);
            list->setCurrentItem(rejected.first());
            QVERIFY(findings->toPlainText().contains("Excluded from plausible candidates"));
            QVERIFY(findings->toPlainText().contains("RAW image size disagrees"));
            checked = true;
        });
        poll.start(20);
        window.inspectImage(path);
        QVERIFY(done.wait(30000));
        QVERIFY(checked);
        QVERIFY(!done.takeFirst().first().toBool());
    }

    void formatPickerOffersDraftsForReview() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QFile catalogue(temp.filePath("diskdefs"));
        QVERIFY(catalogue.open(QIODevice::WriteOnly));
        catalogue.write("diskdef only-ibm\n seclen 128\n tracks 77\n sectrk 26\n"
                        " blocksize 1024\n maxdir 64\n skew 6\n bootsec 52\n os 2.2\nend\n");
        catalogue.close();
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 60000, 2000, {}, temp.filePath("settings.ini"));
        window.show();
        QSignalSpy done(&window, &Window::completed);
        window.loadCatalogue(catalogue.fileName());
        QVERIFY(done.wait(30000));
        QVERIFY(done.takeFirst().first().toBool());

        QTimer poll;
        bool checked = false;
        connect(&poll, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->windowTitle() != "Choose disk format") return;
            poll.stop();
            const auto close = qScopeGuard([dialog] { dialog->reject(); });
            auto *list = dialog->findChild<QListWidget *>("formatPickerList");
            auto *review = dialog->findChild<QPushButton *>("formatPickerEditor");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(list && review && buttons);
            QVERIFY(list->count() > 1);
            QVERIFY(list->currentItem()->text().contains("-byte blocks"));
            QVERIFY(!list->currentItem()->text().contains("candidate-"));
            QVERIFY(review->isEnabled());
            QCOMPARE(review->text(), QString("Review Candidate…"));
            QVERIFY(!buttons->button(QDialogButtonBox::Open)->isEnabled());
            checked = true;
        });
        poll.start(20);
        window.inspectImage(root + "/fixtures/ampro200-independent.dsk");
        QVERIFY(done.wait(60000));
        QVERIFY(checked);
        QVERIFY(!done.takeFirst().first().toBool());
    }

    void formatSelection() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        const auto path = temp.filePath("test.raw");
        QVERIFY(QFile::copy(root + "/fixtures/ibm3740-independent.raw", path));
        auto window = std::make_unique<Window>(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
            qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, QString(), temp.filePath("settings.ini"));
        window->show();
        QSignalSpy done(window.get(), &Window::completed);
        window->loadCatalogue();
        QVERIFY(done.wait(30000));
        QVERIFY2(done.takeFirst()[0].toBool(), qPrintable(window->lastError()));

        QTimer picker;
        bool chose = false;
        connect(&picker, &QTimer::timeout, window.get(), [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->windowTitle() != "Choose disk format") return;
            auto *group = dialog->findChild<QComboBox *>();
            auto *search = dialog->findChild<QLineEdit *>();
            auto *list = dialog->findChild<QListWidget *>();
            group->setCurrentIndex(1);
            search->setText("ibm");
            search->setFocus();
            QTest::keyClick(search, Qt::Key_Down);
            const auto firstRow = list->currentRow();
            QTest::keyClick(list, Qt::Key_Down);
            const bool keyboardNavigation = dialog->focusWidget() == list && list->currentRow() == firstRow + 1;
            search->setText("ibm-3740");
            const auto exact = list->findItems("ibm-3740", Qt::MatchExactly);
            chose = keyboardNavigation && exact.size() == 1;
            if (chose) list->setCurrentItem(exact.first());
            for (auto *button : dialog->findChildren<QPushButton *>())
                if (button->text() == "Toggle favorite") button->click();
            dialog->accept();
            picker.stop();
        });
        picker.start(20);
        window->inspectImage(path);
        QVERIFY(done.wait(30000));
        QVERIFY2(done.takeFirst()[0].toBool(), qPrintable(window->lastError()));
        QVERIFY(chose);
        const auto *source = window->findChild<QLabel *>("imageName");
        QCOMPARE(source->text(), QString("test.raw"));
        auto *format = window->findChild<QPushButton *>("chooseFormat");
        QCOMPARE(format->text(), QString("ibm-3740"));
        QSettings saved(temp.filePath("settings.ini"), QSettings::IniFormat);
        QCOMPARE(saved.value("formatFavorites").toStringList(), QStringList{"ibm-3740"});

        // Reopening uses the remembered choice and skips the picker.
        window->inspectImage(path);
        QVERIFY(done.wait(30000));
        QVERIFY2(done.takeFirst()[0].toBool(), qPrintable(window->lastError()));

        // An incompatible reinterpretation keeps the source selected and disables export.
        QTimer::singleShot(50, window.get(), [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog->findChild<QLineEdit *>()->setText("apple-do");
            dialog->accept();
        });
        format->click();
        QVERIFY(done.wait(30000));
        QVERIFY(!done.takeFirst()[0].toBool());
        QCOMPARE(source->text(), QString("test.raw"));
        QVERIFY(format->isEnabled());
        QCOMPARE(window->entryCount(), 0);
    }

    void exportPlanning() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        FilenamePolicy policy;
        auto entry=[](const QString &name){return QJsonObject{{"name",name},{"size",128}};};
        auto plan=planExport({entry("00SIG/M.LIB"),entry("00A:B.TXT")},dir.path(),3,policy);
        QVERIFY(plan.error.isEmpty()); QCOMPARE(plan.items.size(),2);
        QVERIFY(plan.items[0].destination.endsWith("/3/SIG_M.LIB"));
        QVERIFY(!QDir(dir.filePath("3")).exists()); // Preflight has no side effects.
        QVERIFY(!planExport({entry("00A/B.TXT"),entry("00A_B.TXT")},dir.path(),3,policy).error.isEmpty());
        QVERIFY(!planExport({entry("00A|B.TXT")},dir.path(),3,policy).error.isEmpty());
        QVERIFY(!planExport({entry("00CON.TXT")},dir.path(),3,policy).error.isEmpty());
        QFile existing(dir.filePath("binary.bin")); QVERIFY(existing.open(QIODevice::WriteOnly));existing.write("keep");existing.close();
        plan=planExport({entry("00BINARY.BIN")},dir.path(),-1,policy);
        QCOMPARE(plan.conflicts,QStringList{"BINARY.BIN"}); QVERIFY(plan.items[0].exists);
        QFile numbered(dir.filePath("3"));QVERIFY(numbered.open(QIODevice::WriteOnly));numbered.close();
        QVERIFY(!planExport({},dir.path(),3,policy).error.isEmpty());
    }
    void batchExportWorkflows() {
        const auto root=qEnvironmentVariable("CPMIMAGE_ROOT");
        QFile original(root+"/fixtures/ibm3740-independent.raw");QVERIFY(original.open(QIODevice::ReadOnly));
        auto bytes=original.readAll(); bytes[6656+32]=0;bytes.replace(6656+33,8,"SMALL   ");bytes[6656+64]=0;
        QTemporaryDir temp; QVERIFY(temp.isValid());
        QFile image(temp.filePath("batch.raw"));QVERIFY(image.open(QIODevice::WriteOnly));image.write(bytes);image.close();
        Window window(root,qEnvironmentVariable("CPMIMAGE_PYTHON","python3"),qEnvironmentVariable("CPMIMAGE_HELPER"));
        QSignalSpy done(&window,&Window::completed);
        window.openImage(image.fileName(),"ibm-3740","raw");QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        auto *files=window.findChild<QTableWidget *>("fileList");files->selectAll();
        QTemporaryDir output;QVERIFY(output.isValid());
        window.exportSelection(output.path());QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        QCOMPARE(QDir(output.path()).entryList(QDir::Files).size(),3);
        QFile binary(output.filePath("BINARY.BIN"));QVERIFY(binary.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(binary.readAll(),QCryptographicHash::Sha256).toHex(),QByteArray("110009dcee21620b166f3abfecb5eff7a873be729d1c2d53822e7acc5f34eb9b"));
        for(const auto &oracle:QList<QPair<QString,QByteArray>>{
            {"SMALL.BIN","cd6816b77f68d70001fc3eaa4d42bdd67cb5973b3151cc5292ecc02a3daac6ab"},
            {"TAIL.BIN","e92de1c61e03ecf17afd276aa6bad358b8b2cad5fe4e607a42152df1befd5a6f"}}) {
            QFile file(output.filePath(oracle.first));QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex(),oracle.second);
        }
        QVERIFY(window.lastBatchReport().contains("3 exported"));
        window.exportSelection(output.path());QCOMPARE(done.size(),1);QVERIFY(!done.takeFirst()[0].toBool());
        window.exportSelection(output.path(),true);QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        QVERIFY(window.lastBatchReport().contains("3 skipped"));
        QTemporaryDir mixed; QFile keep(mixed.filePath("binary.bin"));QVERIFY(keep.open(QIODevice::WriteOnly));keep.write("keep");keep.close();
        window.exportSelection(mixed.path(),true);QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        QVERIFY(window.lastBatchReport().contains("2 exported · 1 skipped"));
        QVERIFY(keep.open(QIODevice::ReadOnly));QCOMPARE(keep.readAll(),QByteArray("keep"));
        QTemporaryDir subset;files->clearSelection();window.selectEntry(0);
        files->selectionModel()->select(files->model()->index(2,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);
        window.exportSelection(subset.path());QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        QCOMPARE(QDir(subset.path()).entryList(QDir::Files),QStringList({"BINARY.BIN","TAIL.BIN"}));
        QTemporaryDir areas;files->clearSelection();window.selectEntry(0);window.exportUserArea(areas.path());QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        QCOMPARE(QDir(areas.filePath("0")).entryList(QDir::Files).size(),3);
        auto *navigation=window.findChild<QListWidget *>("userAreas");navigation->setCurrentRow(2);
        window.exportUserArea(areas.path());QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        QVERIFY(QDir(areas.filePath("1")).exists());QVERIFY(QDir(areas.filePath("1")).isEmpty());
        QVERIFY(image.open(QIODevice::ReadOnly));QCOMPARE(image.readAll(),bytes);
        window.openImage(root+"/fixtures/ibm3740-independent.raw","ibm-3740","raw");QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        navigation->setCurrentRow(8);window.exportUserArea(areas.path());QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        QFile seven(areas.filePath("7/BINARY.BIN"));QVERIFY(seven.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(seven.readAll(),QCryptographicHash::Sha256).toHex(),QByteArray("cd6816b77f68d70001fc3eaa4d42bdd67cb5973b3151cc5292ecc02a3daac6ab"));
    }
    void batchCancellationAndFailure() {
        const auto root=qEnvironmentVariable("CPMIMAGE_ROOT");
        QFile original(root+"/fixtures/ibm3740-independent.raw");QVERIFY(original.open(QIODevice::ReadOnly));
        auto bytes=original.readAll();bytes[6656+32]=0;bytes.replace(6656+33,8,"SMALL   ");bytes[6656+64]=0;
        QTemporaryDir temp;QFile image(temp.filePath("batch.raw"));QVERIFY(image.open(QIODevice::WriteOnly));image.write(bytes);image.close();
        Window window(root,qEnvironmentVariable("CPMIMAGE_PYTHON","python3"),qEnvironmentVariable("CPMIMAGE_HELPER"));
        QSignalSpy done(&window,&Window::completed);
        window.openImage(image.fileName(),"ibm-3740","raw");QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        auto *files=window.findChild<QTableWidget *>("fileList");files->selectAll();
        QTemporaryDir output;
        auto connection=connect(&window,&Window::exportProgress,&window,[&](int count,int){
            if(count==1) window.findChild<QPushButton *>("cancelOperation")->click();
        });
        window.exportSelection(output.path());QVERIFY(done.wait(15000));QVERIFY(!done.takeFirst()[0].toBool());
        disconnect(connection);
        QCOMPARE(QDir(output.path()).entryList(QDir::Files|QDir::Hidden).size(),1);
        QVERIFY(window.lastBatchReport().contains("1 exported"));QVERIFY(window.lastBatchReport().contains("2 not attempted"));
        QVERIFY(window.lastBatchReport().contains("Completed exports were retained"));
        // A file created after preflight must still never be replaced.
        QTemporaryDir race;
        connection=connect(&window,&Window::exportProgress,&window,[&](int count,int){
            if(count==0) { QFile file(race.filePath("BINARY.BIN"));if(file.open(QIODevice::WriteOnly)) file.write("raced"); }
        });
        window.exportSelection(race.path());QVERIFY(done.wait(15000));QVERIFY(!done.takeFirst()[0].toBool());disconnect(connection);
        QFile raced(race.filePath("BINARY.BIN"));QVERIFY(raced.open(QIODevice::ReadOnly));QCOMPARE(raced.readAll(),QByteArray("raced"));
        QVERIFY(window.lastBatchReport().contains("1 failed"));QVERIFY(window.lastBatchReport().contains("2 not attempted"));
        QTemporaryDir changed;
        connection=connect(&window,&Window::exportProgress,&window,[&](int count,int){
            if(count==1) { QFile file(image.fileName());if(file.open(QIODevice::WriteOnly)) file.write(bytes.left(bytes.size()-1)+"!"); }
        });
        window.exportSelection(changed.path());QVERIFY(done.wait(15000));QVERIFY(!done.takeFirst()[0].toBool());disconnect(connection);
        QVERIFY2(QDir(changed.path()).entryList(QDir::Files|QDir::Hidden).size()==1,qPrintable(window.lastBatchReport()));
        QVERIFY(window.lastBatchReport().contains("1 exported"));QVERIFY(window.lastBatchReport().contains("1 failed"));
        QVERIFY(window.lastBatchReport().contains("source image changed",Qt::CaseInsensitive));
    }

    void batchCancelDuringExtraction_data() {
        QTest::addColumn<bool>("closeBusy");
        QTest::newRow("cancel")<<false;
        QTest::newRow("close-busy")<<true;
    }
    void batchCancelDuringExtraction() {
#ifdef Q_OS_WIN
        QSKIP("POSIX helper-script fault fixture; normal batch cancellation remains covered on Windows.");
#else
        QFETCH(bool,closeBusy);
        const auto root=qEnvironmentVariable("CPMIMAGE_ROOT");
        QTemporaryDir temp;
        QFile original(root+"/fixtures/ibm3740-independent.raw");QVERIFY(original.open(QIODevice::ReadOnly));
        auto bytes=original.readAll();bytes[6656+32]=0;bytes.replace(6656+33,8,"SMALL   ");bytes[6656+64]=0;
        QFile image(temp.filePath("batch.raw"));QVERIFY(image.open(QIODevice::WriteOnly));image.write(bytes);image.close();
        auto literal=[](const QString &value){auto b=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);return b.mid(1,b.size()-2);};
        const auto marker=temp.filePath("started"), partial=temp.filePath("partial-path");
        QFile wrapper(temp.filePath("slow-helper"));QVERIFY(wrapper.open(QIODevice::WriteOnly));
        wrapper.write("#!/usr/bin/env python3\nimport os,sys,pathlib,time\n"
            "if sys.argv[1]=='export' and sys.argv[5]=='00SMALL.BIN':\n"
            " pathlib.Path(sys.argv[6]).write_bytes(b'incomplete')\n pathlib.Path("+literal(partial)+").write_text(sys.argv[6])\n pathlib.Path("+literal(marker)+").touch()\n time.sleep(60)\n"
            "os.execv("+literal(qEnvironmentVariable("CPMIMAGE_HELPER"))+",["+literal(qEnvironmentVariable("CPMIMAGE_HELPER"))+"]+sys.argv[1:])\n");
        wrapper.close();QVERIFY(wrapper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        Window window(root,qEnvironmentVariable("CPMIMAGE_PYTHON","python3"),wrapper.fileName());
        window.show();QSignalSpy done(&window,&Window::completed);
        window.openImage(image.fileName(),"ibm-3740","raw");QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        window.findChild<QTableWidget *>("fileList")->selectAll();
        QTemporaryDir output;window.exportSelection(output.path());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(marker),5000);
        if(closeBusy) window.close();
        else window.findChild<QPushButton *>("cancelOperation")->click();
        if(done.isEmpty()) QVERIFY(done.wait(5000));QVERIFY(!done.takeFirst()[0].toBool());
        QCOMPARE(QDir(output.path()).entryList(QDir::Files|QDir::Hidden),QStringList{"BINARY.BIN"});
        QVERIFY(window.lastBatchReport().contains("1 exported · 0 skipped · 0 failed · 1 cancelled · 1 not attempted"));
        QFile staged(partial);QVERIFY(staged.open(QIODevice::ReadOnly));
        QVERIFY(!QFile::exists(QString::fromUtf8(staged.readAll())));
        if(closeBusy) QVERIFY(!window.isVisible());
#endif
    }
    void batchDirectoryDialog() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        const auto root=qEnvironmentVariable("CPMIMAGE_ROOT");QTemporaryDir output,config;
        Window window(root,qEnvironmentVariable("CPMIMAGE_PYTHON","python3"),qEnvironmentVariable("CPMIMAGE_HELPER"),30000,2000,{},config.filePath("settings.ini"));
        window.show();QSignalSpy done(&window,&Window::completed);
        window.openImage(root+"/fixtures/ibm3740-independent.raw","ibm-3740","raw");QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        auto *areas = window.findChild<QListWidget *>("userAreas");
        auto *files = window.findChild<QTableWidget *>("fileList");
        auto *action = window.findChild<QAction *>("exportAction");
        QVERIFY(areas && files && action);
        window.activateWindow();
        files->setFocus();
        QTRY_VERIFY(files->hasFocus());
        files->clearSelection();
        QVERIFY(!action->isEnabled());
        areas->setFocus();
        QTRY_VERIFY(action->isEnabled());
        bool visited=false;
        QTimer poll;poll.setInterval(25);
        connect(&poll,&QTimer::timeout,&window,[&]{
            if (auto *review = window.findChild<QDialog *>("batchTextExportReview"); review && review->isVisible()) review->accept();
            if(auto *dialog=fileDialog()) { visited=true;dialog->setDirectory(output.path());QMetaObject::invokeMethod(dialog,"accept"); }
        });poll.start();
        window.findChild<QPushButton *>("toolbarExport")->click();QVERIFY(visited);
        if(done.isEmpty()) QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());poll.stop();
        QVERIFY(QFile::exists(output.filePath("0/BINARY.BIN")));
        QSettings settings(config.filePath("settings.ini"),QSettings::IniFormat);
        QCOMPARE(settings.value("exports/directory").toString(),output.path());
        // Repeating the area export reaches the no-overwrite choice.
        bool conflict=false;
        connect(&poll,&QTimer::timeout,&window,[&]{
            if(auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                conflict=true;
                for(auto *button:box->buttons()) if(button->text()=="Skip existing") { button->click();break; }
            }
        });poll.start();
        window.findChild<QPushButton *>("toolbarExport")->click();
        if(done.isEmpty()) QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());poll.stop();
        QVERIFY(conflict);QVERIFY(window.lastBatchReport().contains("1 skipped"));
        QTemporaryDir all, selected;
        auto destination = all.path();
        QString title;
        QTimer choose;
        connect(&choose, &QTimer::timeout, &window, [&] {
            if (auto *review = window.findChild<QDialog *>("batchTextExportReview"); review && review->isVisible()) review->accept();
            if (auto *dialog = fileDialog()) {
                title = dialog->windowTitle();
                dialog->setDirectory(destination);
                if (dialog->acceptMode() == QFileDialog::AcceptSave)
                    dialog->selectFile(destination + "/BINARY.BIN");
                QMetaObject::invokeMethod(dialog, "accept");
            }
        });
        areas->setCurrentRow(0);
        window.activateWindow();
        areas->setFocus();
        QTRY_VERIFY(areas->hasFocus());
        choose.start(25);
        QTest::keyClick(areas, Qt::Key_X);
        choose.stop();
        QCOMPARE(title, QString("Export all user areas"));
        QTRY_VERIFY(!done.isEmpty());
        QVERIFY(done.takeFirst()[0].toBool());
        QVERIFY(QFile::exists(all.filePath("0/BINARY.BIN")));
        QVERIFY(QDir(all.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() > 1);

        areas->setCurrentRow(1);
        window.activateWindow();
        files->setFocus();
        QTRY_VERIFY(files->hasFocus());
        window.selectEntry(0);
        destination = selected.path();
        choose.start(25);
        action->trigger();
        choose.stop();
        QCOMPARE(title, QString("Export 1 selected file"));
        QTRY_VERIFY(!done.isEmpty());
        QVERIFY(done.takeFirst()[0].toBool());
        QCOMPARE(QDir(selected.path()).entryList(QDir::Files), QStringList{"BINARY.BIN"});
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,false);
    }

    void filenameTranslationPolicy() {
        FilenamePolicy policy;
        QCOMPARE(policy.translate("SIG/M.LIB"),QString("SIG_M.LIB"));
        QCOMPARE(policy.translate("A:B\\C.TXT"),QString("A~B^C.TXT"));
        QCOMPARE(policy.translate("-CATALOG.310"),QString("-CATALOG.310"));
        QCOMPARE(policy.translate("A$+`.TXT"),QString("A$+`.TXT"));
        QVERIFY(!policy.problem("A/B.TXT",{"A/B.TXT","A_B.TXT"}).isEmpty());
        QVERIFY(!policy.problem("A|B.TXT",{}).isEmpty());
        QVERIFY(!FilenamePolicy::validateHostName("CON.TXT").isEmpty());
        QVERIFY(!FilenamePolicy::validateHostName("a.").isEmpty());
        QVERIFY(!FilenamePolicy::validateMappings({{"/","_"},{"/","~"}}).isEmpty());
        QVERIFY(!FilenamePolicy::validateMappings({{"/",":"}}).isEmpty());
        policy.mappings={{"/","_"},{"_","~"}};
        QCOMPARE(policy.translate("A/B_C.TXT"),QString("A_B~C.TXT")); // one pass
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QSettings settings(dir.filePath("names.ini"),QSettings::IniFormat);
        QVERIFY(policy.save(settings));
        QSettings reloaded(dir.filePath("names.ini"),QSettings::IniFormat);
        FilenamePolicy restored; QVERIFY(restored.load(reloaded));
        QCOMPARE(restored.translate("A/B_C.TXT"),QString("A_B~C.TXT"));
        settings.setValue("filenames/mappings",42);
        FilenamePolicy fallback; QVERIFY(!fallback.load(settings));
        QCOMPARE(fallback.translate("SIG/M.LIB"),QString("SIG_M.LIB"));
    }
    void filenameSettingsDialog() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto config=dir.filePath("settings.ini");
        Window window(qEnvironmentVariable("CPMIMAGE_ROOT"),qEnvironmentVariable("CPMIMAGE_PYTHON","python3"),qEnvironmentVariable("CPMIMAGE_HELPER"),30000,2000,{},config);
        auto *action=window.findChild<QAction *>("filenameSettingsAction"); QVERIFY(action);
        bool visited=false;
        QTimer::singleShot(100,&window,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *table=dialog->findChild<QTableWidget *>("filenameMappings");
            auto *preview=dialog->findChild<QLabel *>("filenamePreview");
            visited=table && table->rowCount()==3 && preview && preview->text().contains("SIG_M.LIB");
            if(table) table->item(0,1)->setText("~");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        action->trigger(); QVERIFY(visited);
        QSettings saved(config,QSettings::IniFormat); FilenamePolicy policy;
        QVERIFY(policy.load(saved)); QCOMPARE(policy.translate("SIG/M.LIB"),QString("SIG~M.LIB"));
        QTimer::singleShot(100,&window,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog->findChild<QTableWidget *>("filenameMappings")->item(0,1)->setText("_");
            dialog->reject();
        });
        action->trigger(); saved.sync(); QVERIFY(policy.load(saved));
        QCOMPARE(policy.translate("SIG/M.LIB"),QString("SIG~M.LIB"));
    }
    void listingAndExport() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QVERIFY(!root.isEmpty());
        const auto helper = qEnvironmentVariable("CPMIMAGE_HELPER");
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"), helper);
        window.show();
        QSignalSpy done(&window, &Window::completed);
        window.openImage(root + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
        QCOMPARE(window.entryCount(), 1);
        auto *names = window.findChild<QTableWidget *>("fileList");
        QVERIFY(names);
        QCOMPARE(names->item(0,0)->text(), QString("BINARY.BIN"));
        QCOMPARE(names->item(0,0)->data(Qt::UserRole).toString(), QString("00BINARY.BIN"));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto destination = directory.filePath("user 7 export.bin");
        window.findChild<QListWidget *>("userAreas")->setCurrentItem(window.findChild<QListWidget *>("userAreas")->item(8));
        window.selectEntry(0); window.exportFile(destination);
        QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
        QFile file(destination); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll(); QCOMPARE(bytes.size(), 256);
        QCOMPARE(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),
                 QByteArray("cd6816b77f68d70001fc3eaa4d42bdd67cb5973b3151cc5292ecc02a3daac6ab"));
        file.close();
        window.exportFile(destination);
        QVERIFY(done.wait(15000)); QVERIFY(!done.takeFirst()[0].toBool());
        QVERIFY(window.lastError().contains("already exists"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
        window.openImage(root + "/fixtures/ibm3740-truncated.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(15000)); QVERIFY(!done.takeFirst()[0].toBool());
        QCOMPARE(window.entryCount(), 0); // Never leave the previous directory exportable.
        window.openImage(root + "/fixtures/ibm3740-empty.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
        QCOMPARE(window.entryCount(), 0);
        window.openImage(root + "/fixtures/ampro200-independent.dsk", "bettercpm-ampro-little-board", "dsk");
        QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
        QCOMPARE(window.entryCount(), 1);
    }
    void replaceSelectedImage() {
        const auto root=qEnvironmentVariable("CPMIMAGE_ROOT");
        Window window(root,qEnvironmentVariable("CPMIMAGE_PYTHON","python3"),qEnvironmentVariable("CPMIMAGE_HELPER"));
        window.show(); QSignalSpy done(&window,&Window::completed);
        auto *table=window.findChild<QTableWidget *>("fileList"); QVERIFY(table);
        for(int iteration=0;iteration<6;++iteration) {
            window.openImage(root+"/fixtures/ibm3740-independent.raw","ibm-3740","raw");
            QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
            table->sortItems(iteration%3,Qt::DescendingOrder);
            table->selectAll();
            QSignalSpy selection(table,&QTableWidget::itemSelectionChanged);
            window.openImage(root+"/fixtures/ibm3740-empty.raw","ibm-3740","raw");
            QCOMPARE(selection.count(),0); // No callbacks during row deletion.
            QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
            QCOMPARE(table->rowCount(),0);
        }
    }
    void userAreaNavigation() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"), qEnvironmentVariable("CPMIMAGE_HELPER"));
        window.show();
        auto *areas = window.findChild<QListWidget *>("userAreas");
        auto *files = window.findChild<QTableWidget *>("fileList");
        auto *status = window.findChild<QLabel *>("selectionStatus");
        QVERIFY(areas); QVERIFY(files); QVERIFY(status);
        QCOMPARE(areas->count(), 17); QVERIFY(!areas->isEnabled());
        QSignalSpy done(&window, &Window::completed);
        window.openImage(root + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
        for (int area = 0; area < 16; ++area) {
            auto *item = areas->item(area + 1);
            QCOMPARE(item->text(), QString::number(area)); QVERIFY(!item->icon().isNull());
            const bool populated = area == 0 || area == 7 || area == 15;
            QCOMPARE(item->data(Qt::UserRole + 1).toInt(), populated ? 1 : 0);
            areas->setCurrentItem(item);
            QCOMPARE(files->rowCount(), populated ? 1 : 0);
            QCOMPARE(files->selectionModel()->selectedRows().size(), populated ? 1 : 0);
            QVERIFY(status->text().startsWith(QString("User area %1 ·").arg(area)));
            if (populated) QCOMPARE(files->item(0,0)->data(Qt::UserRole).toString().left(2).toInt(), area);
        }
        QVERIFY(areas->item(1)->icon().pixmap(24,24).toImage() !=
                areas->item(2)->icon().pixmap(24,24).toImage());
        areas->setCurrentItem(areas->item(8));
        QVERIFY(status->text().contains("1 of 1 files selected · 256 bytes"));
        QAction *showAll = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == "Show All User Areas") showAll = action;
        QVERIFY(showAll);
        showAll->trigger();
        QCOMPARE(areas->currentItem()->text(), QString("All"));
        QCOMPARE(files->rowCount(), 3);
        QVERIFY(!files->isColumnHidden(3));
        QVERIFY(files->selectionModel()->selectedRows().isEmpty());
        showAll->trigger();
        QCOMPARE(areas->currentItem()->text(), QString("7"));
        QVERIFY(files->isColumnHidden(3));
        window.openImage(root + "/fixtures/ibm3740-empty.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
        QVERIFY(areas->isEnabled()); QCOMPARE(files->rowCount(),0);
        QVERIFY(status->text().contains("0 of 0 files selected · 0 bytes"));
    }
    void multipleSelectionAndSorting() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        // Derive a disposable, valid three-file user-0 image from the independent fixture.
        QFile original(root + "/fixtures/ibm3740-independent.raw"); QVERIFY(original.open(QIODevice::ReadOnly));
        auto bytes = original.readAll();
        bytes[6656+15] = 8; // 1024 bytes within the existing allocated block.
        bytes[6656+32] = 0; bytes.replace(6656+33,8,"SMALL   ");
        bytes[6656+64] = 0;
        QTemporaryDir temp; QVERIFY(temp.isValid());
        QFile fixture(temp.filePath("selection.raw")); QVERIFY(fixture.open(QIODevice::WriteOnly));
        QCOMPARE(fixture.write(bytes),qint64(bytes.size())); fixture.close();
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"), qEnvironmentVariable("CPMIMAGE_HELPER"));
        window.show(); QSignalSpy done(&window,&Window::completed);
        window.openImage(fixture.fileName(),"ibm-3740","raw");
        QVERIFY(done.wait(15000)); QVERIFY2(done.takeFirst()[0].toBool(),qPrintable(window.lastError()));
        auto *files=window.findChild<QTableWidget *>("fileList");
        auto *areas=window.findChild<QListWidget *>("userAreas");
        auto *status=window.findChild<QLabel *>("selectionStatus");
        QCOMPARE(files->rowCount(),3);
        files->sortItems(1,Qt::AscendingOrder);
        QCOMPARE(files->item(2,1)->data(Qt::DisplayRole).toLongLong(),1024);
        QCOMPARE(files->item(2,0)->data(Qt::UserRole).toString(),QString("00BINARY.BIN"));
        files->sortItems(0,Qt::AscendingOrder);
        auto point=[&](int row) { return files->visualItemRect(files->item(row,0)).center(); };
        QTest::mouseClick(files->viewport(),Qt::LeftButton,Qt::NoModifier,point(0));
        QTest::mouseClick(files->viewport(),Qt::LeftButton,Qt::ShiftModifier,point(2));
        QCOMPARE(files->selectionModel()->selectedRows().size(),3);
        QVERIFY(status->text().contains("3 of 3 files selected · 1536 bytes"));
        QPushButton *exportButton=nullptr;
        for(auto *button:window.findChildren<QPushButton *>()) if(button->objectName() == "toolbarExport") exportButton=button;
        QVERIFY(exportButton); QVERIFY(exportButton->isEnabled());
        window.exportFile(temp.filePath("must-not-export.bin"));
        QVERIFY(done.isEmpty()); QVERIFY(!QFile::exists(temp.filePath("must-not-export.bin")));
        // Qt maps ControlModifier to the Command key on macOS.
        const auto individual=Qt::ControlModifier;
        QTest::mouseClick(files->viewport(),Qt::LeftButton,individual,point(1));
        QCOMPARE(files->selectionModel()->selectedRows().size(),2);
        files->setFocus(); QTest::keySequence(files,QKeySequence::SelectAll);
        QCOMPARE(files->selectionModel()->selectedRows().size(),3);
        files->clearSelection(); QVERIFY(!exportButton->isEnabled());
        QVERIFY(status->text().contains("0 of 3 files selected · 0 bytes"));
        window.selectEntry(0); QVERIFY(exportButton->isEnabled());
        files->sortItems(1,Qt::DescendingOrder);
        const auto output=temp.filePath("sorted.bin"); window.exportFile(output);
        QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
        QFile exported(output); QCOMPARE(exported.size(),qint64(1024));
        areas->setCurrentItem(areas->item(2));
        QCOMPARE(files->rowCount(),0); QVERIFY(!exportButton->isEnabled());
        areas->setCurrentItem(areas->item(1));
        QCOMPARE(files->selectionModel()->selectedRows().size(),1);
        QVERIFY(fixture.open(QIODevice::ReadOnly)); QCOMPARE(fixture.readAll(),bytes);
    }
    void qtDialogFlow() {
        // Exercise real button wiring and QFileDialog acceptance/cancellation.
        // Native platform dialogs are checked separately on the desktop.
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QTemporaryDir config;
        QVERIFY(config.isValid());
        const auto settingsPath = config.filePath("settings.ini");
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("exports/directory", config.path());
        settings.sync();
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {}, settingsPath);
        window.show(); QSignalSpy done(&window, &Window::completed);
        auto button = [&](const QString &prefix) -> QPushButton * {
            for (auto *b : window.findChildren<QPushButton *>()) if (b->text().startsWith(prefix) || b->accessibleName().startsWith(prefix)) return b;
            return nullptr;
        };
        QTimer watchdog; watchdog.setInterval(4000);
        connect(&watchdog, &QTimer::timeout, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        watchdog.setSingleShot(true);
        QTemporaryDir fixtureDirectory; QVERIFY(fixtureDirectory.isValid());
        QVERIFY(QFile::copy(root + "/fixtures/ibm3740-independent.raw", fixtureDirectory.filePath("fixture.raw")));
        bool visited = false;
        QTimer::singleShot(100, &window, [&] {
            auto *dialog = fileDialog();
            if (!dialog) return;
            visited = true; dialog->setDirectory(fixtureDirectory.path()); dialog->selectFile("fixture.raw");
            QTimer::singleShot(300, dialog, [dialog, &fixtureDirectory] { if (auto *edit = dialog->findChild<QLineEdit *>("fileNameEdit")) { edit->setText(fixtureDirectory.filePath("fixture.raw")); QTest::keyClick(edit, Qt::Key_Return); } });
        });
        QTimer picker;
        connect(&picker, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->windowTitle() != "Choose disk format") return;
            dialog->findChild<QComboBox *>()->setCurrentIndex(1);
            auto *list = dialog->findChild<QListWidget *>();
            const auto items = list->findItems("ibm-3740", Qt::MatchExactly);
            if (items.size() == 1) {
                list->setCurrentItem(items.first());
                dialog->accept();
            } else dialog->reject();
            picker.stop();
        });
        picker.start(20);
        watchdog.start();
        QVERIFY(button("Open")); button("Open")->click(); QVERIFY(visited);
        watchdog.stop();
        if (done.isEmpty()) QVERIFY(done.wait(30000));
        QVERIFY(done.takeFirst()[0].toBool()); QCOMPARE(window.entryCount(),1);
        window.activateWindow();
        auto *files = window.findChild<QTableWidget *>("fileList");
        files->setFocus();
        QTRY_VERIFY(files->hasFocus());
        const auto output = config.filePath("dialog export.bin");
        QString selectedOutput;
        visited = false;
        watchdog.start();
        QTimer::singleShot(100, &window, [&] {
            auto *dialog = fileDialog();
            if (!dialog) return;
            visited = dialog->selectedFiles() == QStringList{config.filePath("BINARY.BIN")};
            auto *edit = dialog->findChild<QLineEdit *>("fileNameEdit");
            if (!edit) return;
            edit->setText(QFileInfo(output).fileName());
            selectedOutput = dialog->selectedFiles().value(0);
            QMetaObject::invokeMethod(dialog, "accept");
        });
        QVERIFY(button("Export")); button("Export")->click(); QVERIFY(visited);
        QCOMPARE(selectedOutput, output);
        watchdog.stop();
        if (done.isEmpty()) QVERIFY(done.wait(30000));
        QVERIFY(done.takeFirst()[0].toBool());
        QFile file(output); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.size(),512);
        QCOMPARE(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex(),
                 QByteArray("110009dcee21620b166f3abfecb5eff7a873be729d1c2d53822e7acc5f34eb9b"));
        QTimer::singleShot(100, &window, [&] {
            if (auto *dialog = fileDialog()) dialog->reject();
        });
        button("Open")->click(); QCOMPARE(window.entryCount(),1); QVERIFY(done.isEmpty());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
    }
    void readerFailures_data() {
        QTest::addColumn<QByteArray>("script");
        QTest::newRow("crash") << QByteArray("import sys; sys.stdin.readline(); sys.exit(9)\n");
        QTest::newRow("truncated") << QByteArray("import sys; sys.stdin.readline(); sys.stdout.write('{'); sys.stdout.flush()\n");
        QTest::newRow("bad-json") << QByteArray("import sys; sys.stdin.readline(); print('bad',flush=True); sys.stdin.read()\n");
        QTest::newRow("overflow") << QByteArray("import sys; sys.stdin.readline(); print('x'*270000,flush=True); sys.stdin.read()\n");
        const QByteArray prefix = "import sys,json,time\nr=json.loads(sys.stdin.readline())\n";
        QTest::newRow("bad-schema") << prefix + "print(json.dumps(dict(v=1,id=r['id'],ok=True,result=dict(session='x',source_sha256='a'*64,entries=[dict(name='../../x',size=1,attr=0)]))),flush=True)\ntime.sleep(60)\n";
        QTest::newRow("wrong-size-type") << prefix + "print(json.dumps(dict(v=1,id=r['id'],ok=True,result=dict(session='x',source_sha256='a'*64,entries=[dict(name='00X.BIN',size='1',attr=0)]))),flush=True)\ntime.sleep(60)\n";
        QTest::newRow("unresponsive") << QByteArray("import sys,time,signal\nif hasattr(signal,'SIGTERM'): signal.signal(signal.SIGTERM,signal.SIG_IGN)\nsys.stdin.readline()\ntime.sleep(60)\n");
    }
    void readerFailures() {
        QFETCH(QByteArray, script);
        QTemporaryDir fakeRoot; QVERIFY(fakeRoot.isValid());
        QDir dir(fakeRoot.path()); QVERIFY(dir.mkdir("prototype"));
        QFile broker(dir.filePath("prototype/broker.py")); QVERIFY(broker.open(QIODevice::WriteOnly));
        broker.write(script); broker.close();
        Window window(fakeRoot.path(), qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"),
                      QTest::currentDataTag() == QByteArray("unresponsive") ? 800 : 15000, 300);
        QSignalSpy done(&window, &Window::completed);
        const auto realRoot = qEnvironmentVariable("CPMIMAGE_ROOT");
        window.openImage(realRoot + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(20000)); QVERIFY(!done.takeFirst()[0].toBool());
        QCOMPARE(window.entryCount(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!window.readerRunning(), 3000);
        QCoreApplication::processEvents();
        QCOMPARE(done.count(), 0); // Failure must complete exactly once, including queued pipe output.
        // Restart with the real broker and prove that failure did not poison the UI.
        QFile real(realRoot + "/prototype/broker.py"); QVERIFY(real.open(QIODevice::ReadOnly));
        auto code = real.readAll();
        const auto encoded = QJsonDocument(QJsonArray{realRoot}).toJson(QJsonDocument::Compact);
        code.replace("    args = parser.parse_args()", "    args = parser.parse_args()\n    args.root = Path(" + encoded.mid(1,encoded.size()-2) + ")");
        QVERIFY(broker.open(QIODevice::WriteOnly | QIODevice::Truncate)); broker.write(code); broker.close();
        window.openImage(realRoot + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(20000)); QVERIFY2(done.takeFirst()[0].toBool(), qPrintable(window.lastError())); QCOMPARE(window.entryCount(), 1);
    }
    void processTreeTermination_data() {
        QTest::addColumn<bool>("closeBusy");
        QTest::newRow("deadline") << false;
        QTest::newRow("close-busy") << true;
    }
    void processTreeTermination() {
        QFETCH(bool, closeBusy);
        QTemporaryDir root; QVERIFY(root.isValid()); QDir dir(root.path()); QVERIFY(dir.mkdir("prototype"));
        const auto marker = dir.filePath("escaped"); const auto started = dir.filePath("started");
        auto literal = [](const QString &s) {
            auto j = QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);
            return j.mid(1,j.size()-2);
        };
        QFile broker(dir.filePath("prototype/broker.py")); QVERIFY(broker.open(QIODevice::WriteOnly));
        // Grandchild proves tree containment; a direct broker kill alone cannot pass.
        const QByteArray grandchild = "import pathlib,time; time.sleep(2); pathlib.Path(" + literal(marker) + ").touch()";
        const QByteArray child = "import subprocess,sys,pathlib,time; subprocess.Popen([sys.executable,'-c'," + literal(QString::fromUtf8(grandchild)) + "]); pathlib.Path(" + literal(started) + ").touch(); time.sleep(60)";
        broker.write("import sys,subprocess,time,signal\nif hasattr(signal,'SIGTERM'): signal.signal(signal.SIGTERM,signal.SIG_IGN)\nsys.stdin.readline()\nsubprocess.Popen([sys.executable,'-c'," + literal(QString::fromUtf8(child)) + "])\ntime.sleep(60)\n"); broker.close();
        Window window(root.path(), qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"), "unused", 1200, 300);
        window.show(); QSignalSpy done(&window, &Window::completed);
        window.openImage("unused", "ibm-3740", "raw");
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(started), 1000);
        if (closeBusy) window.close();
        if (done.isEmpty()) QVERIFY(done.wait(5000));
        QVERIFY(!done.takeFirst()[0].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!window.readerRunning(), 2000);
        QTest::qWait(2300); QVERIFY(!QFile::exists(marker));
        if (closeBusy) QVERIFY(!window.isVisible());
    }
    void missingReader() {
        Window window(qEnvironmentVariable("CPMIMAGE_ROOT"), "/no-such-python", "unused");
        QSignalSpy done(&window, &Window::completed);
        window.openImage("unused", "ibm-3740", "raw");
        if (done.isEmpty()) QVERIFY(done.wait(5000));
        QVERIFY(!done.takeFirst()[0].toBool());
        QVERIFY(window.lastError().contains("Cannot start"));
    }

    void browserPaneFocus() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QTemporaryDir scratch;
        QVERIFY(scratch.isValid());
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {},
                      scratch.filePath("settings.ini"));
        window.show();
        window.activateWindow();
        QSignalSpy done(&window, &Window::completed);
        window.openImage(root + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.first().first().toBool());
        auto *areas = window.findChild<QListWidget *>("userAreas");
        auto *files = window.findChild<QTableWidget *>("fileList");
        QVERIFY(areas);
        QVERIFY(files);
        areas->setCurrentRow(0);
        QCOMPARE(files->rowCount(), 3);
        files->selectRow(0);
        files->selectionModel()->select(files->model()->index(2, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        areas->setFocus();
        QTRY_VERIFY(areas->hasFocus());
        const auto selected = files->selectionModel()->selectedRows();
        const auto current = files->currentIndex();
        const auto *area = areas->currentItem();
        for (auto modifier : {Qt::NoModifier, Qt::ShiftModifier}) {
            for (int step = 0; step < 8; ++step) {
                QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab, modifier);
                QCOMPARE(QApplication::focusWidget(), step % 2 == 0 ?
                         static_cast<QWidget *>(files) : areas);
                QCOMPARE(files->selectionModel()->selectedRows(), selected);
                QCOMPARE(files->currentIndex(), current);
                QCOMPARE(areas->currentItem(), area);
            }
        }
        QTest::keyClick(areas, Qt::Key_Down);
        QCOMPARE(areas->currentRow(), 1);
        QTest::keyClick(areas, Qt::Key_Up);
        QCOMPARE(areas->currentRow(), 0);
        QTest::keyClick(areas, Qt::Key_Tab);
        files->selectRow(0);
        QTest::keyClick(files, Qt::Key_Down);
        QCOMPARE(files->currentRow(), 1);
        QTest::keyClick(files, Qt::Key_Up);
        QCOMPARE(files->currentRow(), 0);
    }

    void openImageMenuShortcut() {
        Workspace workspace({qEnvironmentVariable("CPMIMAGE_ROOT"),
                             qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                             qEnvironmentVariable("CPMIMAGE_HELPER"), {}});
        workspace.addSession();
        auto &page = workspace.addSession();
        workspace.show();
        workspace.activateWindow();
        QTRY_VERIFY(workspace.isActiveWindow());
        QAction *open = nullptr;
        for (auto *menu : workspace.menuBar()->actions()) {
            if (!menu->menu() || menu->menu()->title() != "File") continue;
            for (auto *action : menu->menu()->actions())
                if (action->text() == "Open Image…") open = action;
        }
        QVERIFY(open);
        QCOMPARE(open->shortcut(), QKeySequence(QKeySequence::Open));
        QPushButton *toolbar = nullptr;
        for (auto *button : page.findChildren<QPushButton *>())
            if (button->accessibleName() == open->text()) toolbar = button;
        QVERIFY(toolbar);
        QSignalSpy triggered(open, &QAction::triggered);
        bool opened = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &workspace, [&] {
            if (auto *dialog = fileDialog()) {
                opened = dialog->windowTitle() == "Open CP/M image";
                dialog->reject();
            }
        });
        dismiss.start(20);
        const auto shortcut = open->shortcut()[0];
        QTest::keyClick(&workspace, shortcut.key(), shortcut.keyboardModifiers());
        QVERIFY(opened);
        QCOMPARE(triggered.size(), 1);
        opened = false;
        open->trigger();
        QVERIFY(opened);
        opened = false;
        toolbar->click();
        QVERIFY(opened);
    }
    void openMultipleImagesFromPicker() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto first = temp.filePath("First.raw"), second = temp.filePath("Second disk.raw");
        for (const auto &path : {first, second}) {
            QVERIFY(QFile::copy(root + "/fixtures/ibm3740-independent.raw", path));
            QFile definition(QFileInfo(path).absolutePath() + "/" + QFileInfo(path).completeBaseName() + ".FDF");
            QVERIFY(definition.open(QIODevice::WriteOnly));
            QVERIFY(definition.write("diskdef ibm-3740\n seclen 128\n tracks 77\n sectrk 26\n"
                                     " blocksize 1024\n maxdir 64\n skew 6\n boottrk 2\n os 2.2\nend\n") > 0);
        }
        Workspace workspace({root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                             qEnvironmentVariable("CPMIMAGE_HELPER"), {}}, temp.filePath("settings.ini"));
        auto &page = workspace.addSession();
        workspace.show();
        const auto nativeDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore = qScopeGuard([nativeDisabled] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled);
        });
        QTimer choose;
        connect(&choose, &QTimer::timeout, &workspace, [&] {
            if (auto *dialog = fileDialog()) {
                choose.stop();
                QCOMPARE(dialog->fileMode(), QFileDialog::ExistingFiles);
                dialog->setDirectory(temp.path());
                auto *name = dialog->findChild<QLineEdit *>("fileNameEdit");
                QVERIFY(name);
                name->setText(QStringLiteral("\"First.raw\" \"Second disk.raw\""));
                QMetaObject::invokeMethod(dialog, "accept");
            }
        });
        choose.start(20);
        QPushButton *open = nullptr;
        for (auto *button : page.findChildren<QPushButton *>())
            if (button->accessibleName() == "Open Image…") open = button;
        QVERIFY(open);
        open->click();
        auto *tabs = workspace.findChild<QTabWidget *>();
        QCOMPARE(tabs->count(), 2);
        QTRY_COMPARE(tabs->tabText(0), QString("First.raw"));
        QTRY_COMPARE(tabs->tabText(1), QString("Second disk.raw"));
    }
    void fileViewer() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        const auto helper = qEnvironmentVariable("CPMIMAGE_HELPER");
        QTemporaryDir scratch;
        QVERIFY(scratch.isValid());
        const auto image = scratch.filePath("preview.raw");
        QVERIFY(QFile::copy(root + "/fixtures/ibm3740-independent.raw", image));
        QVERIFY(QFile::copy(root + "/fixtures/diskdefs", scratch.filePath("diskdefs")));
        const auto input = scratch.filePath("input");
        for (const auto &name : {QString("00NOTE.TXT"), QString("00EMPTY.TXT"), QString("00HIGH.TXT")}) {
            QFile payload(input);
            QVERIFY(payload.open(QIODevice::WriteOnly));
            if (name.contains("NOTE")) QVERIFY(payload.write("Hello\r\nWorld\x1a stale final record bytes") > 0);
            if (name.contains("HIGH")) QCOMPARE(payload.write(QByteArray::fromHex("c8e9")), 2);
            payload.close();
            QProcess import;
            import.setWorkingDirectory(scratch.path());
            import.start(helper, {"edit_import", image, "ibm-3740", "raw", name, input});
            QVERIFY(import.waitForFinished());
            QCOMPARE(import.exitCode(), 0);
        }
        QFile source(image);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto before = source.readAll();
        source.close();

        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"), helper,
                      30000, 2000, {}, scratch.filePath("settings.ini"));
        window.show();
        window.activateWindow();
        QSignalSpy done(&window, &Window::completed);
        window.openImage(image, "ibm-3740", "raw");
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.takeFirst().first().toBool());
        auto *table = window.findChild<QTableWidget *>("fileList");
        auto *action = window.findChild<QAction *>("viewFileAction");
        QVERIFY(table && action);
        for (const auto &name : {QString("NOTE.TXT"), QString("EMPTY.TXT"), QString("BINARY.BIN"), QString("HIGH.TXT")}) {
            const auto matches = table->findItems(name, Qt::MatchExactly);
            QCOMPARE(matches.size(), 1);
            window.activateWindow();
            table->setFocus();
            table->selectionModel()->select(table->model()->index(matches.first()->row(), 0),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            QTRY_VERIFY(table->hasFocus());
            QVERIFY(action->isEnabled());
            if (name == "NOTE.TXT") QTest::keyClick(table, Qt::Key_V);
            else if (name == "EMPTY.TXT") QTest::keyClick(table, Qt::Key_Return);
            else if (name == "HIGH.TXT") action->trigger();
            else {
                const auto point = table->visualItemRect(matches.first()).center();
                QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, point);
                QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, point);
            }
            QTRY_COMPARE(done.size(), 1);
            QVERIFY(done.takeFirst().first().toBool());
            QDialog *viewer = nullptr;
            for (auto *dialog : window.findChildren<QDialog *>())
                if (dialog->isVisible()) viewer = dialog;
            QVERIFY(viewer);
            QCOMPARE(viewer->windowTitle(), name + " · User area 0");
            auto *text = viewer->findChild<QPlainTextEdit *>("filePreviewText");
            QVERIFY(text && text->isReadOnly());
            QTRY_VERIFY(text->hasFocus());
            auto *mode = viewer->findChild<QPushButton *>("previewMode");
            auto *high = viewer->findChild<QPushButton *>("previewHighBit");
            QVERIFY(mode && high);
            QCOMPARE(high->text(), QString("Highbit: OFF (7)"));
            QCOMPARE(mode->text(), QString(name == "HIGH.TXT" || name == "BINARY.BIN" ? "text/HEX (T)" : "TEXT/hex (T)"));
            QVERIFY(!high->isChecked());
            if (name == "NOTE.TXT") {
                QCOMPARE(text->toPlainText(), QString("Hello\nWorld"));
                QTest::keyClick(text, Qt::Key_T);
                QTRY_VERIFY(text->toPlainText().startsWith("00000000  48 65 6C 6C 6F 0D 0A 57  6F 72 6C 64 1A"));
                QVERIFY(text->toPlainText().contains("1A 20"));
                QCOMPARE(mode->text(), QString("text/HEX (T)"));
                QVERIFY(!mode->isCheckable());
                mode->click();
                QCOMPARE(text->toPlainText(), QString("Hello\nWorld"));
                action->trigger();
                QTRY_COMPARE(done.size(), 1);
                QVERIFY(done.takeFirst().first().toBool());
                QDialog *second = nullptr;
                for (auto *dialog : window.findChildren<QDialog *>())
                    if (dialog != viewer && dialog->isVisible()) second = dialog;
                QVERIFY(second);
                auto *secondText = second->findChild<QPlainTextEdit *>("filePreviewText");
                QVERIFY(secondText);
                QTRY_VERIFY(secondText->hasFocus());
                QTest::keyClick(secondText, Qt::Key_T);
                QTRY_VERIFY(secondText->toPlainText().startsWith("00000000"));
                QCOMPARE(text->toPlainText(), QString("Hello\nWorld"));
                QTest::keyClick(secondText, Qt::Key_Q);
                QTRY_VERIFY(viewer->isVisible());
                auto *close = viewer->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close);
                close->click();
            } else if (name == "HIGH.TXT") {
                mode->click();
                QVERIFY(text->toPlainText().contains("unsupported text encoding"));
                QTest::keyClick(text, Qt::Key_7);
                QCOMPARE(high->text(), QString("Highbit: ON (7)"));
                QVERIFY(text->toPlainText().startsWith("Hi"));
                mode->click();
                QTRY_VERIFY(text->toPlainText().startsWith("00000000  C8 E9"));
                QVERIFY(!high->isEnabled());
                QTest::keyClick(text, Qt::Key_7);
                mode->click();
                QCOMPARE(high->text(), QString("Highbit: ON (7)"));
                QVERIFY(text->toPlainText().startsWith("Hi"));
                QTest::keyClick(text, Qt::Key_Escape);
            } else if (name == "BINARY.BIN") {
                QTRY_VERIFY(text->toPlainText().startsWith("00000000"));
                for (const int width : {8, 16, 32, 64}) {
                    const int cells = 10 + 3 * width + (width - 1) / 8 + 2 + width + 1;
                    viewer->resize(text->fontMetrics().horizontalAdvance(QString(cells, 'W')) + 55, 400);
                    QTRY_COMPARE(text->property("hexWidth").toInt(), width);
                    const auto rows = text->toPlainText().split('\n');
                    QVERIFY(rows.size() >= 2);
                    QCOMPARE(rows[0].indexOf('|'), rows[1].indexOf('|'));
                    QCOMPARE(rows[1].left(8).toInt(nullptr, 16), width);
                }
                QTest::keyClick(text, Qt::Key_Q);
            } else {
                QCOMPARE(text->toPlainText(), QString());
                viewer->close();
            }
            QTRY_VERIFY(!window.findChild<QPlainTextEdit *>("filePreviewText"));
            QTRY_VERIFY(table->hasFocus());
            QCOMPARE(table->selectionModel()->selectedRows().first().row(), matches.first()->row());
        }
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), before);
        QVERIFY(!window.windowTitle().endsWith('*'));
    }

    void sigmPascalViewerAndBatchExport() {
        const auto source = qEnvironmentVariable("CPMIMAGE_SIGM_062");
        if (source.isEmpty()) QSKIP("Set CPMIMAGE_SIGM_062 to test the historical SIG/M image.");
        QTemporaryDir scratch;
        const auto image = scratch.filePath("sigm.raw");
        QVERIFY(QFile::copy(source, image));
        QFile imageFile(image);
        QVERIFY(imageFile.open(QIODevice::ReadOnly));
        const auto before = imageFile.readAll();
        imageFile.close();
        Window window(qEnvironmentVariable("CPMIMAGE_ROOT"), qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"),
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 30000, 2000, {}, scratch.filePath("settings.ini"));
        window.show();
        QSignalSpy done(&window, &Window::completed);
        window.openImage(image, "ibm-3740", "raw");
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.takeFirst().first().toBool());
        auto *files = window.findChild<QTableWidget *>("fileList");
        QVERIFY(files);
        QStringList names{"DOCOMM.PAS", "DOTEXT.PAS", "CRCK.COM"};
        for (const auto &name : names + QStringList{"ABSTRACT.062", "KFORMAT.SUB"}) {
            const auto matches = files->findItems(name, Qt::MatchExactly);
            QCOMPARE(matches.size(), 1);
            window.activateWindow();
            files->setFocus();
            files->setCurrentCell(matches.first()->row(), 0);
            QTRY_VERIFY(files->hasFocus());
            QTest::keyClick(files, Qt::Key_V);
            QTRY_VERIFY(window.findChild<QPlainTextEdit *>("filePreviewText"));
            auto *view = window.findChild<QPlainTextEdit *>("filePreviewText");
            const auto text = view->toPlainText();
            if (name.endsWith(".PAS")) {
                QVERIFY(!text.startsWith("00000000"));
                QVERIFY(!text.contains(QChar(0x1a)));
                QVERIFY(text.contains("KFORMAT", Qt::CaseInsensitive));
            } else if (name == "ABSTRACT.062") {
                QVERIFY(text.contains("first\n     group standard"));
            } else if (name == "KFORMAT.SUB") {
                QCOMPARE(text.count(QChar(0x2407)), 5);
                QVERIFY(text.contains("pascal docomm.bbxB", Qt::CaseInsensitive));
            } else {
                QTRY_VERIFY(view->toPlainText().startsWith("00000000"));
            }
            view->window()->close();
            QTRY_VERIFY(!window.findChild<QPlainTextEdit *>("filePreviewText"));
        }
        done.clear();
        files->clearSelection();
        for (const auto &name : names) {
            const auto row = files->findItems(name, Qt::MatchExactly).first()->row();
            files->selectionModel()->select(files->model()->index(row, 0),
                                           QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        QTimer chooser;
        connect(&chooser, &QTimer::timeout, &window, [&] {
            if (auto *review = window.findChild<QDialog *>("batchTextExportReview"); review && review->isVisible()) {
                auto *choices = review->findChild<QListWidget *>("batchTextExportFiles");
                for (int row = 0; row < choices->count(); ++row)
                    if (choices->item(row)->text().contains(".PAS")) choices->item(row)->setCheckState(Qt::Checked);
                review->accept();
            }
            if (auto *dialog = fileDialog()) {
                dialog->setDirectory(scratch.path());
                QMetaObject::invokeMethod(dialog, "accept");
            }
        });
        chooser.start(25);
        window.findChild<QPushButton *>("toolbarExport")->click();
        chooser.stop();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY2(done.takeFirst().first().toBool(), qPrintable(window.lastBatchReport() + window.lastError()));
        // Reference hashes from standalone cpmcp, with -t for the Pascal sources.
        const QStringList hashes{
            "8fb58afdc3eccdc52dc39674185b6b2902381ce9c6716442372b325e1d1c8ced",
            "645ee7b03e829789e4463160a0a0dc8357042ec89be1d61e7d650ebde52ed938",
            "aa6960f95c1415476bcaebc5aae8ccd08a04e0509d4aa0724a4ffc265453f20f"
        };
        for (qsizetype index = 0; index < names.size(); ++index) {
            const auto &name = names[index];
            QFile exported(scratch.filePath(name));
            QVERIFY(exported.open(QIODevice::ReadOnly));
            const auto bytes = exported.readAll();
            if (name.endsWith(".PAS")) {
                QCOMPARE(bytes.size(), name == "DOCOMM.PAS" ? 4034 : 4712);
                QVERIFY(!bytes.contains('\x1a'));
                QVERIFY(!bytes.contains('\r'));
            } else QCOMPARE(bytes.size(), 1280);
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()),
                     hashes[index]);
        }
        QVERIFY(imageFile.open(QIODevice::ReadOnly));
        QCOMPARE(imageFile.readAll(), before);
    }

    void archiveViewer() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        const auto helper = qEnvironmentVariable("CPMIMAGE_HELPER");
        const auto python = qEnvironmentVariable("CPMIMAGE_PYTHON", "python3");
        QTemporaryDir scratch;
        QVERIFY(scratch.isValid());
        QProcess generator;
        generator.start(python, {"-B", root + "/prototype/tests/archive_check.py", scratch.path()});
        QVERIFY(generator.waitForFinished());
        QCOMPARE(generator.exitCode(), 0);
        const auto image = scratch.filePath("preview.raw");
        QVERIFY(QFile::copy(root + "/fixtures/ibm3740-independent.raw", image));
        QVERIFY(QFile::copy(root + "/fixtures/diskdefs", scratch.filePath("diskdefs")));
        for (const auto &[name, file] : {std::pair{"00NOTE.TQT", "sq"},
                                       std::pair{"00NOTE.TZT", "cr"},
                                       std::pair{"00OUTER.ARK", "ark"}}) {
            QProcess import;
            import.setWorkingDirectory(scratch.path());
            import.start(helper, {"edit_import", image, "ibm-3740", "raw", name, scratch.filePath(file)});
            QVERIFY(import.waitForFinished());
            QCOMPARE(import.exitCode(), 0);
        }
        QFile source(image);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto original = source.readAll();
        source.close();
        Window window(root, python, helper, 30000, 2000, {}, scratch.filePath("settings.ini"));
        window.show();
        window.activateWindow();
        QSignalSpy done(&window, &Window::completed);
        window.openImage(image, "ibm-3740", "raw");
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.takeFirst().first().toBool());
        auto *files = window.findChild<QTableWidget *>("fileList");
        QVERIFY(files);
        const auto opened = [&](const QString &title) -> QDialog * {
            for (auto *dialog : window.findChildren<QDialog *>())
                if (dialog->isVisible() && dialog->windowTitle() == title) return dialog;
            return nullptr;
        };
        const auto open = [&](QString name, Qt::Key key) {
            window.activateWindow();
            files->setFocus();
            const auto matches = files->findItems(name, Qt::MatchExactly);
            QCOMPARE(matches.size(), 1);
            files->selectionModel()->select(files->model()->index(matches.first()->row(), 0),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            QTRY_VERIFY(files->hasFocus());
            QTest::keyClick(files, key);
            QTRY_VERIFY(opened(name + " · User area 0"));
        };
        for (const auto &[name, key, text] : {
                std::tuple{"NOTE.TQT", Qt::Key_V, "A"}, std::tuple{"NOTE.TQT", Qt::Key_Q, "A"},
                std::tuple{"NOTE.TZT", Qt::Key_Z, "B"}}) {
            open(name, key);
            auto *dialog = opened(QString(name) + " · User area 0");
            QVERIFY(dialog);
            auto *view = dialog->findChild<QPlainTextEdit *>("filePreviewText");
            auto *stream = dialog->findChild<QComboBox *>("previewStream");
            QVERIFY(view && stream);
            QTRY_VERIFY(view->hasFocus());
            QCOMPARE(view->toPlainText(), QString(text));
            QTest::keyClick(view, Qt::Key_T);
            stream->setCurrentIndex(1);
            QTRY_VERIFY(view->toPlainText().startsWith(key == Qt::Key_Z ? "00000000  76 FE" : "00000000  76 FF"));
            stream->setCurrentIndex(0);
            QTRY_VERIFY(view->toPlainText().startsWith(key == Qt::Key_Z ? "00000000  42" : "00000000  41"));
            dialog->close();
        }
        open("BINARY.BIN", Qt::Key_Z);
        auto *raw = opened("BINARY.BIN · User area 0");
        QVERIFY(raw && !raw->findChild<QComboBox *>("previewStream"));
        raw->close();
        open("OUTER.ARK", Qt::Key_V);
        auto *outer = opened("OUTER.ARK · User area 0");
        QVERIFY(outer);
        const auto openMember = [](QDialog *dialog, const QString &name) {
            auto *members = dialog->findChild<QTableWidget *>("archiveMembers");
            QVERIFY(members && members->rowCount() > 0);
            QCOMPARE(members->item(0, 0)->text(), name);
            const auto point = members->visualItemRect(members->item(0, 0)).center();
            QTest::mouseClick(members->viewport(), Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseDClick(members->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        };
        auto *outerMembers = outer->findChild<QTableWidget *>("archiveMembers");
        QVERIFY(outerMembers);
        QTRY_VERIFY(outerMembers->hasFocus());
        QCOMPARE(outerMembers->currentRow(), 0);
        QTest::keyClick(outerMembers, Qt::Key_Down);
        QCOMPARE(outerMembers->currentRow(), 1);
        QTest::keyClick(outerMembers, Qt::Key_Up);
        QCOMPARE(outerMembers->currentRow(), 0);
        auto *batchExport = outer->findChild<QPushButton *>("exportArchiveSelection");
        auto *batchStatus = outer->findChild<QLabel *>("archiveExportStatus");
        QVERIFY(batchExport && batchStatus);
        outerMembers->selectionModel()->select(outerMembers->model()->index(1, 0),
                                                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QCOMPARE(outerMembers->selectionModel()->selectedRows().size(), 2);
        QTemporaryDir batchFolder;
        QVERIFY(batchFolder.isValid());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        QTimer batchChooser;
        auto batchDestination = batchFolder.path();
        connect(&batchChooser, &QTimer::timeout, this, [&] {
            if (auto *dialog = fileDialog()) {
                dialog->setDirectory(batchDestination);
                QMetaObject::invokeMethod(dialog, "accept");
            }
        });
        const auto exportBatch = [&] {
            batchChooser.start(25);
            batchExport->click();
            batchChooser.stop();
        };
        exportBatch();
        QTRY_VERIFY(batchStatus->text().startsWith("2 exported · 0 skipped"));
        QFile second(batchFolder.filePath("SECOND.TXT"));
        QVERIFY(second.open(QIODevice::ReadOnly));
        QCOMPARE(second.readAll(), QByteArray("Second member"));
        QFile nested(batchFolder.filePath("INNER.LBR"));
        QVERIFY(nested.open(QIODevice::ReadOnly));
        QFile library(scratch.filePath("lbr"));
        QVERIFY(library.open(QIODevice::ReadOnly));
        QCOMPARE(nested.readAll(), library.readAll());
        second.close();
        nested.close();
        exportBatch();
        QTRY_VERIFY(batchStatus->text().startsWith("0 exported · 2 skipped"));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
        outerMembers->setCurrentCell(0, 0);
        outer->activateWindow();
        outerMembers->setFocus();
        QTRY_VERIFY(outerMembers->hasFocus());
        QTest::keyClick(outerMembers, Qt::Key_V);
        QTRY_VERIFY(opened("INNER.LBR"));
        auto *innerMembers = opened("INNER.LBR")->findChild<QTableWidget *>("archiveMembers");
        QVERIFY(innerMembers);
        QTRY_VERIFY(innerMembers->hasFocus());
        auto *inner = opened("INNER.LBR");
        auto *innerExport = inner->findChild<QPushButton *>("exportArchiveSelection");
        auto *innerStatus = inner->findChild<QLabel *>("archiveExportStatus");
        QVERIFY(innerExport && innerStatus);
        innerMembers->selectAll();
        QCOMPARE(innerMembers->selectionModel()->selectedRows().size(), 2);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        batchChooser.start(25);
        innerExport->click();
        batchChooser.stop();
        QTRY_VERIFY(innerStatus->text().startsWith("2 exported · 0 skipped"));
        QFile hello(batchFolder.filePath("HELLO.TQT"));
        QVERIFY(hello.open(QIODevice::ReadOnly));
        QCOMPARE(hello.readAll(), QByteArray("A"));
        QFile other(batchFolder.filePath("OTHER.TXT"));
        QVERIFY(other.open(QIODevice::ReadOnly));
        QCOMPARE(other.readAll(), QByteArray("Other member"));
        QTemporaryDir originalFolder;
        QVERIFY(originalFolder.isValid());
        batchDestination = originalFolder.path();
        auto *batchStream = inner->findChild<QComboBox *>("archiveExportStream");
        QVERIFY(batchStream);
        batchStream->setCurrentIndex(1);
        batchChooser.start(25);
        innerExport->click();
        batchChooser.stop();
        QTRY_VERIFY(innerStatus->text().startsWith("2 exported · 0 skipped"));
        QFile originalMember(originalFolder.filePath("HELLO.TQT")), expectedMember(scratch.filePath("sq"));
        QVERIFY(originalMember.open(QIODevice::ReadOnly));
        QVERIFY(expectedMember.open(QIODevice::ReadOnly));
        QCOMPARE(originalMember.readAll(), expectedMember.readAll());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
        inner->activateWindow();
        innerMembers->setCurrentCell(0, 0);
        innerMembers->setFocus();
        QTRY_VERIFY(innerMembers->hasFocus());
        QTest::keyClick(innerMembers, Qt::Key_V);
        QTRY_VERIFY(opened("HELLO.TQT"));
        auto *memberText = opened("HELLO.TQT")->findChild<QPlainTextEdit *>("filePreviewText");
        QVERIFY(memberText);
        auto *memberDialog = opened("HELLO.TQT");
        auto *exportMember = memberDialog->findChild<QPushButton *>("exportArchiveMember");
        auto *memberStream = memberDialog->findChild<QComboBox *>("previewStream");
        QVERIFY(exportMember && memberStream);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        for (int index : {0, 1}) {
            memberStream->setCurrentIndex(index);
            const auto destination = scratch.filePath(QString("export-%1").arg(index));
            QTimer chooser;
            connect(&chooser, &QTimer::timeout, this, [&] {
                if (auto *dialog = fileDialog()) {
                    auto *edit = dialog->findChild<QLineEdit *>("fileNameEdit");
                    if (!edit) return;
                    edit->setText(destination);
                    QMetaObject::invokeMethod(dialog, "accept");
                }
            });
            chooser.start(25);
            exportMember->click();
            chooser.stop();
            QFile exported(destination), squeezed(scratch.filePath("sq"));
            QVERIFY2(exported.open(QIODevice::ReadOnly), qPrintable(QDir(scratch.path()).entryList(QDir::Files).join(", ")));
            QVERIFY(squeezed.open(QIODevice::ReadOnly));
            QCOMPARE(exported.readAll(), index == 0 ? QByteArray("A") : squeezed.readAll());
        }
        const auto beforeCancel = QDir(scratch.path()).entryList();
        QTimer cancelExport;
        connect(&cancelExport, &QTimer::timeout, this, [] {
            if (auto *dialog = fileDialog()) dialog->reject();
        });
        cancelExport.start(25);
        exportMember->click();
        cancelExport.stop();
        QCOMPARE(QDir(scratch.path()).entryList(), beforeCancel);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
        QTest::keyClick(memberText, Qt::Key_Escape);
        QTRY_VERIFY(!opened("HELLO.TQT"));
        QTRY_VERIFY(innerMembers->hasFocus());
        QCOMPARE(innerMembers->currentRow(), 0);
        QTest::keyClick(innerMembers, Qt::Key_Escape);
        QTRY_VERIFY(!opened("INNER.LBR"));
        QTRY_VERIFY(outerMembers->hasFocus());
        QCOMPARE(outerMembers->currentRow(), 0);
        openMember(outer, "INNER.LBR");
        QTRY_VERIFY(opened("INNER.LBR"));
        outer->close();
        inner = opened("INNER.LBR");
        QVERIFY(inner);
        openMember(inner, "HELLO.TQT");
        QTRY_VERIFY(opened("HELLO.TQT"));
        inner->close();
        auto *child = opened("HELLO.TQT");
        QVERIFY(child);
        auto *text = child->findChild<QPlainTextEdit *>("filePreviewText");
        QVERIFY(text);
        QCOMPARE(text->toPlainText(), QString("A"));
        child->close();
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), original);
    }

};
QTEST_MAIN(GuiCheck)
#include "gui_check.moc"
