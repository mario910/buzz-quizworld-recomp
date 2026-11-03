#include "controller.h"
#include "tts.h"
#include <QAbstractItemModelTester>
#include <QFile>
#include <QSemaphore>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>

class ControllerTest final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("BuzzStudioTests");
        QCoreApplication::setApplicationName("ControllerTests");
        QStandardPaths::setTestModeEnabled(true);
    }
    void editsUndoDeleteAndSave() {
        StudioController c;
        QAbstractItemModelTester modelTester(c.questions(),
                                             QAbstractItemModelTester::FailureReportingMode::QtTest);
        c.newProject(true);
        QCOMPARE(c.totalCount(), 5);
        QVERIFY(!c.dirty());
        const auto original = c.current()["question"].toString();
        c.updateCurrent({{"question", "Zmienione pytanie?"}, {"correct_index", 2}});
        QCOMPARE(c.current()["question"].toString(), QString("Zmienione pytanie?"));
        QVERIFY(c.dirty());
        c.undo();
        QCOMPARE(c.current()["question"].toString(), original);
        QVERIFY(!c.dirty());
        c.redo();
        c.duplicateCurrent();
        QCOMPARE(c.totalCount(), 6);
        const auto duplicated = c.current()["id"].toString();
        c.deleteCurrent();
        QCOMPARE(c.totalCount(), 5);
        c.undo();
        QCOMPARE(c.current()["id"].toString(), duplicated);
        QTemporaryDir temp;
        const auto path = temp.path() + "/project.buzz.json";
        QVERIFY(c.saveFile(QUrl::fromLocalFile(path)));
        QVERIFY(!c.dirty());
        auto result = studio::loadDocument(path);
        QVERIFY(result.ok());
        QCOMPARE(result.document.questions.size(), 6);
        QCOMPARE(result.document.questions[1].correct, 2);
    }
    void failedImportPreservesUnsavedWork() {
        StudioController c;
        c.newProject(true);
        c.updateCurrent({{"question", "Nie zgub tej edycji"}});
        QTemporaryDir temp;
        QFile file(temp.path() + "/bad.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{broken");
        file.close();
        QSignalSpy errors(&c, &StudioController::errorOccurred);
        c.openFile(QUrl::fromLocalFile(file.fileName()));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(c.totalCount(), 5);
        QCOMPARE(c.current()["question"].toString(), QString("Nie zgub tej edycji"));
        QVERIFY(c.dirty());
    }
    void importsMergeByIdentityAndCanBeUndone() {
        StudioController c;
        c.newProject(true);
        auto incoming = studio::exampleDocument();
        incoming.questions[0].id = c.current()["id"].toString();
        QTemporaryDir temp;
        QString error;
        const auto path = temp.path() + "/incoming.json";
        QVERIFY(studio::saveDocument(incoming, path, &error));
        c.openFile(QUrl::fromLocalFile(path), true);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QCOMPARE(c.totalCount(), 9);
        QVERIFY(!c.importWarnings().isEmpty());
        c.undo();
        QCOMPARE(c.totalCount(), 5);
        c.redo();
        QCOMPARE(c.totalCount(), 9);
        c.setFilter("Jowisz", {}, {});
        QCOMPARE(c.visibleCount(), 1);
        c.setFilter({}, "Muzyka", {});
        QCOMPARE(c.visibleCount(), 2);
    }
    void speechFailurePreservesExistingOutput() {
        QTemporaryDir temp;
        const auto path = temp.filePath("existing.wav");
        QFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write("keep existing audio");
        output.close();
        QVERIFY(!studio::synthesizeSpeech("", "pl", 0, path).ok());
        QVERIFY(!studio::synthesizeSpeech("Pytanie?", "", 0, path).ok());
        QVERIFY(!studio::synthesizeSpeech("Pytanie?", "studio-nonexistent-voice", 0, path).ok());
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArray("keep existing audio"));
    }
    void generatesWaveWithInstalledLocalVoiceAndSupportsUndo() {
        const auto voices = studio::speechVoices();
        if (voices.isEmpty())
            QSKIP("No local TTS voices installed; provider integration test unavailable.");
        StudioController c;
        c.newProject(true);
        QTemporaryDir temp;
        const auto path = temp.filePath("polskie nagranie.wav");
        QSignalSpy errors(&c, &StudioController::errorOccurred);
        const auto text = QString::fromUtf8("Zażółć gęślą jaźń. Która odpowiedź jest poprawna?");
        c.generateAudio(QUrl::fromLocalFile(path), text, voices[0].toMap()["id"].toString(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 20000);
        QCOMPARE(errors.count(), 0);
        QFile output(path);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QVERIFY(output.size() > 44);
        QCOMPARE(output.read(4), QByteArray("RIFF"));
        output.seek(8);
        QCOMPARE(output.read(4), QByteArray("WAVE"));
        QCOMPARE(c.current()["speech"].toString(), path);
        QCOMPARE(c.current()["tts_text"].toString(), text);
        c.undo();
        QVERIFY(c.current()["speech"].toString().isEmpty());
        c.redo();
        QCOMPARE(c.current()["speech"].toString(), path);
    }
    void switchesProjectLanguageWithoutLosingEditsAndCanUndo() {
        StudioController c;
        c.newProject(true);
        const auto polish = c.current()["question"].toString();
        c.setPackLanguage("en");
        QCOMPARE(c.packLanguage(), QString("en"));
        QVERIFY(c.current()["question"].toString().isEmpty());
        c.updateCurrent({{"question", "My English question"}});
        c.setPackLanguage("pl");
        QCOMPARE(c.current()["question"].toString(), polish);
        c.undo();
        QCOMPARE(c.packLanguage(), QString("en"));
        QCOMPARE(c.current()["question"].toString(), QString("My English question"));
        c.redo();
        QCOMPARE(c.packLanguage(), QString("pl"));
        c.addQuestion();
        QCOMPARE(c.current()["language"].toString(), QString("pl"));
    }
    void importLanguageProbeCanBeCancelledWithoutChangingProject() {
        StudioController c;
        c.newProject(true);
        QSignalSpy requested(&c, &StudioController::importLanguageRequested);
        const auto path = QFINDTESTDATA("fixtures/native-multilingual.pak");
        c.prepareImport(QUrl::fromLocalFile(path));
        QTRY_COMPARE_WITH_TIMEOUT(requested.count(), 1, 5000);
        QCOMPARE(c.totalCount(), 5);
        c.cancelImport();
        QVERIFY(!c.dirty());
        c.prepareImport(QUrl::fromLocalFile(path));
        QTRY_COMPARE_WITH_TIMEOUT(requested.count(), 2, 5000);
        c.finishImport("en");
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QCOMPARE(c.totalCount(), 1);
        QCOMPARE(c.packLanguage(), QString("en"));
        QCOMPARE(c.current()["question"].toString(), QString("Which letter comes first?"));
    }
    void mergesLanguagesByIdentityWithoutOverwritingTranslations() {
        StudioController c;
        const auto path = QFINDTESTDATA("fixtures/native-multilingual.pak");
        c.openFile(QUrl::fromLocalFile(path), false, "pl");
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        c.updateCurrent({{"question", "Moja polska poprawka"}});
        c.openFile(QUrl::fromLocalFile(path), true, "en");
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QCOMPARE(c.totalCount(), 1);
        QCOMPARE(c.current()["question"].toString(), QString("Moja polska poprawka"));
        c.setPackLanguage("en");
        QCOMPARE(c.current()["question"].toString(), QString("Which letter comes first?"));
        c.updateCurrent({{"question", "My correction"}});
        c.openFile(QUrl::fromLocalFile(path), true, "en");
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QCOMPARE(c.current()["question"].toString(), QString("My correction"));
        c.undo(); // Import is one undoable command even if every ID was skipped.
        QCOMPARE(c.current()["question"].toString(), QString("My correction"));
        c.setPackLanguage("pl");
        QCOMPARE(c.current()["question"].toString(), QString("Moja polska poprawka"));
    }
    void buildsAndInstallsAsynchronouslyWithBackupAndCancellation() {
        StudioController c;
        QTemporaryDir temp;
        QVERIFY(QDir().mkpath(temp.filePath("runtime/dev_hdd0")));
        c.openFile(QUrl::fromLocalFile(QFINDTESTDATA("fixtures/build-source-en.json")));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        auto options = c.buildSettings();
        options["templatePath"] = QFINDTESTDATA("fixtures/build-template.pak");
        options["outputDirectory"] = temp.filePath("packs");
        options["runtimeRoot"] = temp.filePath("runtime");
        options["number"] = 103;
        options["categoryBase"] = 700;
        QSignalSpy done(&c, &StudioController::packFinished);
        QSignalSpy errors(&c, &StudioController::errorOccurred);
        c.buildPack(options, true);
        QVERIFY(c.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(done.count(), 1);
        const auto generated = c.lastBuild()["path"].toString();
        const auto installed = c.lastBuild()["installedPath"].toString();
        QVERIFY(QFileInfo::exists(generated));
        QVERIFY(QFileInfo::exists(installed));
        c.installPack(QUrl::fromLocalFile(generated), temp.filePath("runtime"), "BCES00647");
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QCOMPARE(done.count(), 2);
        QVERIFY(QFileInfo::exists(c.lastBuild()["backupPath"].toString()));
        // Keep the job queued until cancellation; a tiny fixture may otherwise
        // finish before the calling thread gets scheduled again.
        auto *pool = QThreadPool::globalInstance();
        const int previousLimit = pool->maxThreadCount();
        pool->waitForDone();
        pool->setMaxThreadCount(1);
        QSemaphore started, release;
        auto blocker = QtConcurrent::run([&] {
            started.release();
            release.acquire();
        });
        const bool queued = started.tryAcquire(1, 5000);
        if (queued) {
            c.buildPack(options, false);
            c.cancelBuild();
        }
        release.release();
        blocker.waitForFinished();
        pool->setMaxThreadCount(previousLimit);
        QVERIFY(queued);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(done.count(), 2);
        QVERIFY(c.lastBuild()["cancelled"].toBool());
        QVERIFY(QFileInfo::exists(installed));
    }
    void fillsAnEmptyTranslationAfterProjectReload() {
        StudioController c;
        const auto path = QFINDTESTDATA("fixtures/native-multilingual.pak");
        c.openFile(QUrl::fromLocalFile(path), false, "en");
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        c.setPackLanguage("pl");
        QTemporaryDir temp;
        const auto project = QUrl::fromLocalFile(temp.filePath("missing.buzz.json"));
        QVERIFY(c.saveFile(project));
        c.openFile(project);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QVERIFY(c.current()["question"].toString().isEmpty());
        c.openFile(QUrl::fromLocalFile(path), true, "pl");
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QCOMPARE(c.totalCount(), 1);
        QCOMPARE(c.current()["question"].toString(), QString::fromUtf8("Która litera jest pierwsza?"));
        c.undo();
        QVERIFY(c.current()["question"].toString().isEmpty());
        c.setPackLanguage("en");
        QCOMPARE(c.current()["question"].toString(), QString("Which letter comes first?"));
    }
};
QTEST_GUILESS_MAIN(ControllerTest)
#include "controller_test.moc"
