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
#include <QtTest>

static QFileDialog *fileDialog() {
    for (auto *w : QApplication::topLevelWidgets())
        if (auto *d = qobject_cast<QFileDialog *>(w); d && d->isVisible()) return d;
    return nullptr;
}

class GuiCheck : public QObject {
    Q_OBJECT
private slots:
    void listingAndExport() {
        const auto root = qEnvironmentVariable("CPMIMAGE_ROOT");
        QVERIFY(!root.isEmpty());
        const auto helper = qEnvironmentVariable("CPMIMAGE_HELPER");
        Window window(root, qEnvironmentVariable("CPMIMAGE_PYTHON", "python3"), helper);
        window.show();
        QSignalSpy done(&window, &Window::completed);
        window.openImage(root + "/fixtures/ibm3740-independent.raw", "ibm-3740", "raw");
        QVERIFY(done.wait(15000)); QVERIFY(done.takeFirst()[0].toBool());
        QCOMPARE(window.entryCount(), 3);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto destination = directory.filePath("user 7 export.bin");
        window.selectEntry(1); window.exportFile(destination);
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
        QCOMPARE(window.entryCount(), 3);
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
            if (auto *dialog = fileDialog()) dialog->reject();
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
        QVERIFY(button("Open")); button("Open")->click(); QVERIFY(visited);
        if (done.isEmpty()) QVERIFY(done.wait(5000));
        QVERIFY(done.takeFirst()[0].toBool()); QCOMPARE(window.entryCount(),3);
        QTemporaryDir destination; QVERIFY(destination.isValid());
        const auto output = destination.filePath("dialog export.bin");
        visited = false;
        QTimer::singleShot(100, &window, [&] {
            auto *dialog = fileDialog();
            if (!dialog) return;
            visited = QDir(dialog->directory()).exists(); dialog->setDirectory(destination.path());
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
        button("Open")->click(); QCOMPARE(window.entryCount(),3); QVERIFY(done.isEmpty());
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
        QTest::newRow("wrong-size-type") << prefix + "print(json.dumps(dict(v=1,id=r['id'],ok=True,result=dict(session='x',source_sha256='a'*64,entries=[dict(name='00x.bin',size='1',attr=0)]))),flush=True)\ntime.sleep(60)\n";
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
        QVERIFY(done.wait(6000)); QVERIFY2(done.takeFirst()[0].toBool(), qPrintable(window.lastError())); QCOMPARE(window.entryCount(), 3);
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
