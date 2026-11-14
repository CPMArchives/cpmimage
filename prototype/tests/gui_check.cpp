// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"
#include <QCryptographicHash>
#include <QFile>
#include <QSignalSpy>
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
#include <QTextEdit>

static QFileDialog *fileDialog() {
    for (auto *w : QApplication::topLevelWidgets())
        if (auto *d = qobject_cast<QFileDialog *>(w); d && d->isVisible()) return d;
    return nullptr;
}

class GuiCheck : public QObject {
    Q_OBJECT
private slots:
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
                chooser->selectFile(destination);
                QMetaObject::invokeMethod(chooser, "accept");
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
            const bool keyboardNavigation = list->hasFocus() && list->currentRow() == firstRow + 1;
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
        auto *navigation=window.findChild<QListWidget *>("userAreas");navigation->setCurrentRow(1);
        window.exportUserArea(areas.path());QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        QVERIFY(QDir(areas.filePath("1")).exists());QVERIFY(QDir(areas.filePath("1")).isEmpty());
        QVERIFY(image.open(QIODevice::ReadOnly));QCOMPARE(image.readAll(),bytes);
        window.openImage(root+"/fixtures/ibm3740-independent.raw","ibm-3740","raw");QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
        navigation->setCurrentRow(7);window.exportUserArea(areas.path());QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());
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
        bool visited=false;
        QTimer poll;poll.setInterval(25);
        connect(&poll,&QTimer::timeout,&window,[&]{
            if(auto *dialog=fileDialog()) { visited=true;dialog->setDirectory(output.path());QMetaObject::invokeMethod(dialog,"accept"); }
        });poll.start();
        window.findChild<QPushButton *>("exportUserArea")->click();QVERIFY(visited);
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
        window.findChild<QPushButton *>("exportUserArea")->click();
        if(done.isEmpty()) QVERIFY(done.wait(15000));QVERIFY(done.takeFirst()[0].toBool());poll.stop();
        QVERIFY(conflict);QVERIFY(window.lastBatchReport().contains("1 skipped"));
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
        window.findChild<QListWidget *>("userAreas")->setCurrentItem(window.findChild<QListWidget *>("userAreas")->item(7));
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
        QCOMPARE(areas->count(), 16); QVERIFY(!areas->isEnabled());
        QSignalSpy done(&window, &Window::completed);
        window.openImage(root + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
        for (int area = 0; area < 16; ++area) {
            auto *item = areas->item(area);
            QCOMPARE(item->text(), QString::number(area)); QVERIFY(!item->icon().isNull());
            const bool populated = area == 0 || area == 7 || area == 15;
            QCOMPARE(item->data(Qt::UserRole + 1).toInt(), populated ? 1 : 0);
            areas->setCurrentItem(item);
            QCOMPARE(files->rowCount(), populated ? 1 : 0);
            QCOMPARE(files->selectionModel()->selectedRows().size(), populated ? 1 : 0);
            QVERIFY(status->text().startsWith(QString("User area %1 ·").arg(area)));
            if (populated) QCOMPARE(files->item(0,0)->data(Qt::UserRole).toString().left(2).toInt(), area);
        }
        QVERIFY(areas->item(0)->icon().pixmap(24,24).toImage() !=
                areas->item(1)->icon().pixmap(24,24).toImage());
        areas->setCurrentItem(areas->item(7));
        QVERIFY(status->text().contains("1 of 1 files selected · 256 bytes"));
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
        for(auto *button:window.findChildren<QPushButton *>()) if(button->text().startsWith("Export selected")) exportButton=button;
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
        areas->setCurrentItem(areas->item(1));
        QCOMPARE(files->rowCount(),0); QVERIFY(!exportButton->isEnabled());
        areas->setCurrentItem(areas->item(0));
        QCOMPARE(files->selectionModel()->selectedRows().size(),1);
        QVERIFY(fixture.open(QIODevice::ReadOnly)); QCOMPARE(fixture.readAll(),bytes);
    }
    void qtDialogFlow() {
        // Exercise real button wiring and QFileDialog acceptance/cancellation.
        // Native platform dialogs are checked separately on the desktop.
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"), qEnvironmentVariable("CPMIMAGE_HELPER"));
        window.show(); QSignalSpy done(&window, &Window::completed);
        auto button = [&](const QString &prefix) -> QPushButton * {
            for (auto *b : window.findChildren<QPushButton *>()) if (b->text().startsWith(prefix)) return b;
            return nullptr;
        };
        QTimer watchdog; watchdog.setInterval(4000);
        connect(&watchdog, &QTimer::timeout, &window, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        }); watchdog.start();
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
        QVERIFY(button("Open")); button("Open")->click(); QVERIFY(visited);
        if (done.isEmpty()) QVERIFY(done.wait(5000));
        QVERIFY(done.takeFirst()[0].toBool()); QCOMPARE(window.entryCount(),1);
        QTemporaryDir destination; QVERIFY(destination.isValid());
        const auto output = destination.filePath("dialog export.bin");
        visited = false;
        QTimer::singleShot(100, &window, [&] {
            auto *dialog = fileDialog();
            if (!dialog) return;
            visited = QDir(dialog->directory()).exists() && !dialog->selectedFiles().isEmpty() && QFileInfo(dialog->selectedFiles().first()).fileName()=="BINARY.BIN"; dialog->setDirectory(destination.path());
            QTimer::singleShot(300, dialog, [dialog, &output] { if (auto *edit = dialog->findChild<QLineEdit *>("fileNameEdit")) { edit->setText(output); QTest::keyClick(edit, Qt::Key_Return); } });
        });
        QVERIFY(button("Export")); button("Export")->click(); QVERIFY(visited);
        if (done.isEmpty()) QVERIFY(done.wait(5000));
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
                      qEnvironmentVariable("CPMIMAGE_HELPER"), 800, 300);
        QSignalSpy done(&window, &Window::completed);
        const auto realRoot = qEnvironmentVariable("CPMIMAGE_ROOT");
        window.openImage(realRoot + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(6000)); QVERIFY(!done.takeFirst()[0].toBool());
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
        QVERIFY(done.wait(6000)); QVERIFY2(done.takeFirst()[0].toBool(), qPrintable(window.lastError())); QCOMPARE(window.entryCount(), 1);
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
};
QTEST_MAIN(GuiCheck)
#include "gui_check.moc"
