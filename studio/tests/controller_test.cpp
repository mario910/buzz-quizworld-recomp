#include "controller.h"
#include "pack_codec.h"
#include "media_tools.h"
#include "tts.h"
#include <QAbstractItemModelTester>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QScopeGuard>
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
    void bulkAssignmentCanTargetOnlyTheLatestImportAndKeepsSourceSections() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto document = studio::exampleDocument();
        document.questions.resize(1);
        auto &question = document.questions.first();
        question.id = "earlier";
        question.source = "Imported game";
        question.category = "Original section";
        const auto firstPath = temp.filePath("first.buzz.json");
        QString error;
        QVERIFY2(studio::saveDocument(document, firstPath, &error), qPrintable(error));
        question.id = "latest";
        const auto secondPath = temp.filePath("second.buzz.json");
        QVERIFY2(studio::saveDocument(document, secondPath, &error), qPrintable(error));

        StudioController c;
        c.openFile(QUrl::fromLocalFile(firstPath));
        QTRY_VERIFY(!c.busy());
        c.updateCurrent({{"subject", 199}, {"category", 208}, {"category_name", "Film"}});
        c.openFile(QUrl::fromLocalFile(secondPath), true);
        QTRY_VERIFY(!c.busy());
        QCOMPARE(c.importedSections(false).size(), 1);
        QCOMPARE(c.importedSections(false).first().questions, 2);
        QCOMPARE(c.importedSections(true).first().questions, 1);

        const int subject = c.addPackSubject({{"pl", "Własny temat"}, {"en", "Custom topic"}});
        QVERIFY(subject > 0);
        const int child = c.packCategories().last().toMap()["id"].toInt();
        QCOMPARE(c.assignImportedSection("Imported game", {"Original section"}, subject, child, {}, true), 1);
        c.select("earlier");
        QCOMPARE(c.current()["category"].toInt(), 208);
        c.select("latest");
        QCOMPARE(c.current()["category"].toInt(), child);
        QCOMPARE(c.importedSections(false).first().path, QStringList{"Original section"});
        QCOMPARE(c.assignImportedSection("Imported game", {"Original section"}, subject, child, {}, true), 0);
        c.undo();
        QCOMPARE(c.current()["category"].toInt(), 237);
        c.redo();
        QCOMPARE(c.current()["category"].toInt(), child);
        const auto saved = temp.filePath("saved.buzz.json");
        QVERIFY(c.saveFile(QUrl::fromLocalFile(saved)));
        c.openFile(QUrl::fromLocalFile(saved));
        QTRY_VERIFY(!c.busy());
        QCOMPARE(c.importedSections(false).first().path, QStringList{"Original section"});
        QCOMPARE(c.importedSections(false).first().questions, 2);
    }
    void narrationDoesNotChangeTheQuestionContentType() {
        StudioController controller;
        controller.newProject(true);
        controller.updateCurrent({{"speech", "voice.wav"}});
        controller.setFilter({}, {}, "audio");
        auto *questions = controller.questions();
        QCOMPARE(questions->rowCount(), 1);
        const auto row = questions->index(0, 0);
        QVERIFY(row.data(QuestionModel::Audio).toBool());
        QCOMPARE(row.data(QuestionModel::ContentType).toInt(), int(studio::QuestionContentType::Text));
        controller.updateCurrent({{"image", "picture.png"}});
        QCOMPARE(questions->index(0, 0).data(QuestionModel::ContentType).toInt(), int(studio::QuestionContentType::Picture));
        controller.updateCurrent({{"video", "clip.mp4"}});
        QCOMPARE(questions->index(0, 0).data(QuestionModel::ContentType).toInt(), int(studio::QuestionContentType::Video));
        QVERIFY(questions->index(0, 0).data(QuestionModel::Audio).toBool());

        auto imported = studio::newQuestion();
        imported.speech = "voice.rsf";
        imported.extra["source_media"] = QJsonArray{QJsonObject{{"type", 1}}};
        QCOMPARE(imported.contentType(), studio::QuestionContentType::Text);
        imported.extra["source_media"] = QJsonArray{QJsonObject{{"type", "ps2-picture"}}};
        QCOMPARE(imported.contentType(), studio::QuestionContentType::Picture);
        imported.extra["source_media"] = QJsonArray{QJsonObject{{"type", 2}}};
        QCOMPARE(imported.contentType(), studio::QuestionContentType::Video);
    }
    void reimportRecoversMissingPictureWithoutReplacingEdits() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto makePack = [&](const QString &file, int picture) {
            using namespace studio::pack;
            QVector<RoundQuestion> questions;
            for (int i = 1; i <= 3; ++i)
                questions << RoundQuestion{quint32(i), 0, QString("Question %1?").arg(i), {},
                                           {"A", "B", "C", "D"}, 2, 235, 237, picture};
            const auto round = writeRound("original", "ENG", 100, 1, 1, questions);
            writeArchive(file, {{"ENG/ROUNDS/original.qak", round.little}, {"ENG/ROUNDS/original.hdr", round.header},
                                {"ENG/ROUNDS/original.hst", round.stream}});
        };
        const auto original = temp.filePath("original.pak"), refreshed = temp.filePath("refreshed.pak");
        makePack(original, 0);
        makePack(refreshed, 1);
        const auto custom = temp.filePath("custom.png");
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(custom));

        StudioController c;
        c.openFile(QUrl::fromLocalFile(original), false, "en");
        QTRY_COMPARE(c.totalCount(), 3);
        c.select("buzz:1003:100:2");
        c.updateCurrent({{"image", custom}});
        c.select("buzz:1003:100:3");
        c.updateCurrent({{"question", "Edited question?"}});
        QSignalSpy imported(&c, &StudioController::importFinished);
        c.openFile(QUrl::fromLocalFile(refreshed), true, "en");
        QTRY_COMPARE(imported.size(), 1);
        QCOMPARE(c.totalCount(), 3);
        QCOMPARE(c.sourceMediaStatus("en", true).visualRecoverable, 1);
        c.select("buzz:1003:100:1");
        QCOMPARE(c.current()["question"].toString(), QString("Question 1?"));
        c.select("buzz:1003:100:2");
        QCOMPARE(c.current()["image"].toString(), custom);
        c.select("buzz:1003:100:3");
        QCOMPARE(c.current()["question"].toString(), QString("Edited question?"));
        c.undo();
        QCOMPARE(c.sourceMediaStatus("en", true).visualRecoverable, 0);
    }
    void musicFilterAndRecoveryForOneQuestion() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto document = studio::exampleDocument("en");
        auto &question = document.questions.first();
        const auto source = temp.filePath("music.pak");
        const auto clip = QByteArray::fromHex("06000200bb800006000001800000018000000180000001800000000000000000") +
                          QByteArray(384, 'm');
        studio::pack::writeArchive(source, {{studio::pack::musicEntry("ENG", 42), clip}});
        for (int i : {0, 2}) {
            auto &q = document.questions[i];
            auto localized = q.localized();
            localized["source_archive"] = source;
            localized["source_round"] = "ENG/ROUNDS/q_music";
            localized["source_media"] = QJsonArray{QJsonObject{{"type", 1}, {"asset", 42}}};
            q.translations["en"] = localized;
        }
        document.questions[1].speech = "narration.wav";
        QFile file(temp.filePath("music.buzz.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(studio::serialize(document, temp.path())) > 0);
        file.close();

        StudioController controller;
        controller.openFile(QUrl::fromLocalFile(file.fileName()));
        QTRY_COMPARE(controller.totalCount(), document.questions.size());
        QCOMPARE(controller.musicCount(), 2);
        QCOMPARE(controller.audioCount(), 1);
        controller.setFilter({}, {}, "music");
        auto *questions = controller.questions();
        QCOMPARE(questions->rowCount(), 2);
        const auto row = questions->index(0, 0);
        QCOMPARE(row.data(QuestionModel::Uid).toString(), question.id);
        QCOMPARE(row.data(QuestionModel::ContentType).toInt(), int(studio::QuestionContentType::Text));
        QVERIFY(row.data(QuestionModel::Music).toBool());
        QVERIFY(!row.data(QuestionModel::Audio).toBool());
        QSignalSpy recovered(&controller, &StudioController::currentMusicRecovered);
        controller.select(question.id);
        QVERIFY(controller.current()["canRecoverMusic"].toBool());
        controller.recoverCurrentMusic();
        QTRY_COMPARE(recovered.size(), 1);
        QFile recoveredClip(recovered.first().first().toString());
        QVERIFY(recoveredClip.open(QIODevice::ReadOnly));
        QCOMPARE(recoveredClip.readAll(), clip);
        QVERIFY(controller.current()["hasMusicClip"].toBool());
        controller.select(document.questions[2].id);
        QVERIFY(controller.current()["music"].toString().isEmpty());
        controller.undo();
        QCOMPARE(controller.current()["id"].toString(), question.id);
        QVERIFY(!controller.current()["hasMusicClip"].toBool());
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
#ifdef Q_OS_WIN
    void findsWingetAndFfmpegWithAnUnrefreshedPath() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto restore = qScopeGuard([path = qgetenv("PATH"), localAppData = qgetenv("LOCALAPPDATA")] {
            qputenv("PATH", path);
            qputenv("LOCALAPPDATA", localAppData);
        });
        // The executable names are discoverable, but this test runs neither program.
        const auto winget = temp.filePath("winget.exe");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), winget));
        const auto links = temp.filePath("Microsoft/WinGet/Links");
        QVERIFY(QDir().mkpath(links));
        const auto ffmpeg = QDir(links).filePath("ffmpeg.exe");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), ffmpeg));
        qputenv("PATH", QDir::toNativeSeparators(temp.path()).toLocal8Bit());
        qputenv("LOCALAPPDATA", QDir::toNativeSeparators(temp.path()).toLocal8Bit());
        const auto installer = studio::ffmpegInstallation();
        QCOMPARE(installer.program, winget);
        QVERIFY(installer.arguments.contains("Gyan.FFmpeg"));
        QVERIFY(installer.arguments.contains("user"));
        if (!QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg.exe")))
            QCOMPARE(studio::findMediaFfmpeg(), ffmpeg);
    }
#endif
    void decodesAllClipsOnceAndUndoesAttachments() {
        const auto ffmpeg = studio::configuredMediaFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("FFmpeg not installed.");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto mpeg = temp.filePath("sample.mpg");
        studio::pack::runMedia(ffmpeg, {"-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=160x120:rate=25",
                                  "-f", "lavfi", "-i", "sine=frequency=640", "-t", "0.5",
                                  "-c:v", "mpeg1video", "-c:a", "mp2", "-f", "mpeg", mpeg}, nullptr);
        QFile input(mpeg);
        QVERIFY(input.open(QIODevice::ReadOnly));
        QByteArray movie(2048, '\0');
        movie.replace(64, 8, "SDIVIDEO");
        movie += input.readAll();
        studio::pack::patch(movie, 40, 2048, 4, true);
        studio::pack::patch(movie, 48, movie.size(), 4, true);
        const auto pak = temp.filePath("test.pak");
        const QString entry = "ENG/QUESTIONASSETS/VIDEO/1000123.sdi";
        studio::pack::writeArchive(pak, {{entry, movie}});
        const auto file = temp.filePath("test.buzz.json");
        studio::Document doc;
        doc.title = "Clip batch";
        doc.language = "en";
        for (int i = 0; i < 3; ++i) {
            auto q = studio::newQuestion();
            q.id = QString::number(i);
            q.language = "en";
            q.text = QString("Question %1?").arg(i);
            q.answers = QStringList{"A", "B", "C", "D"};
            auto localized = q.localized();
            localized["source_media"] = QJsonArray{QJsonObject{{"type", 2}, {"entry", entry}, {"archive", pak}}};
            q.translations["en"] = localized;
            if (i == 2)
                q.video = mpeg;
            doc.questions << q;
        }
        QString error;
        QVERIFY2(studio::saveDocument(doc, file, &error), qPrintable(error));
        StudioController c;
        c.openFile(QUrl::fromLocalFile(file));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QSignalSpy progress(&c, &StudioController::sourceMediaProgress);
        QSignalSpy failures(&c, &StudioController::errorOccurred);
        QVERIFY(c.recoverSourceMedia());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 20000);
        QCOMPARE(failures.size(), 0);
        QCOMPARE(progress.size(), 1);
        c.select("0");
        const auto decoded = c.current()["video"].toString();
        QVERIFY(QFileInfo::exists(decoded));
        c.select("1");
        QCOMPARE(c.current()["video"].toString(), decoded);
        c.select("2");
        QCOMPARE(c.current()["video"].toString(), mpeg);
        QVERIFY(!c.recoverSourceMedia());
        c.undo();
        c.select("0");
        QVERIFY(c.current()["video"].toString().isEmpty());
        c.select("2");
        QCOMPARE(c.current()["video"].toString(), mpeg);
        // Cancel at the start signal, before the worker is scheduled.
        connect(&c, &StudioController::busyChanged, &c, [&c] {
            if (c.sourceMediaRecoveryActive())
                c.cancelSourceMediaRecovery();
        });
        QVERIFY(c.recoverSourceMedia());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QCOMPARE(failures.size(), 0);
        QCOMPARE(progress.size(), 1);
        c.select("0");
        QVERIFY(c.current()["video"].toString().isEmpty());
    }
    void recoversStillsInAnEditedProject() {
        const auto ffmpeg = studio::configuredMediaFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("FFmpeg not installed.");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto image = temp.filePath("picture.png");
        QImage picture(64, 64, QImage::Format_RGB32);
        picture.fill(Qt::green);
        QVERIFY(picture.save(image));
        QFile pictureTemplate(QFINDTESTDATA("fixtures/build-sheet-template.sdi"));
        QVERIFY(pictureTemplate.open(QIODevice::ReadOnly));
        const auto sheet = studio::pack::pictureSheet(pictureTemplate.readAll(), {image}, ffmpeg, nullptr);
        const auto pak = temp.filePath("test.pak");
        studio::pack::writeArchive(pak, {{"ENG/QUESTIONASSETS/VIDEO/PICS001.SDI", sheet}});

        studio::Document doc;
        doc.language = "en";
        for (int id = 0; id < 3; ++id) {
            auto q = studio::newQuestion();
            q.id = QString::number(id);
            q.language = "en";
            q.text = "Question as edited";
            q.answers = QStringList{"A", "B", "C", "D"};
            if (id == 1)
                q.image = image;
            auto variant = q.localized();
            variant["source_archive"] = pak;
            variant["source_round"] = "ENG/ROUNDS/round";
            variant["source_media"] = QJsonArray{QJsonObject{{"type", 3}, {"slot", id == 2 ? 0 : 1}}};
            q.translations["en"] = variant;
            doc.questions << q;
        }
        const auto path = temp.filePath("test.buzz.json");
        QString error;
        QVERIFY2(studio::saveDocument(doc, path, &error), qPrintable(error));
        StudioController c;
        c.openFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QCOMPARE(c.unresolvedMediaQuestions().size(), 2);
        QSignalSpy finished(&c, &StudioController::sourceMediaRecoveryFinished);
        QVERIFY(c.recoverSourceMedia());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY2(finished.first().first().toStringList().isEmpty(),
                 qPrintable(finished.first().first().toStringList().join('\n')));
        QCOMPARE(c.unresolvedMediaQuestions().size(), 1);
        QCOMPARE(c.unresolvedMediaQuestions().first().toMap()["id"].toString(), QString("2"));
        c.select("0");
        QCOMPARE(c.current()["question"].toString(), QString("Question as edited"));
        QVERIFY(QFileInfo::exists(c.current()["image"].toString()));
        c.select("1");
        QCOMPARE(c.current()["image"].toString(), image);
        c.undo();
        QCOMPARE(c.unresolvedMediaQuestions().size(), 2);
    }
    void importsNarrationOnlyForMatchingPromptsWithoutAudio() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto voice = temp.filePath("source.rsf"), replacement = temp.filePath("replacement.rsf");
        for (const auto &path : {voice, replacement}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("audio") > 0);
        }
        auto original = studio::exampleDocument();
        original.questions.resize(3);
        auto edited = original;
        edited.questions[1].text += " Edited.";
        edited.questions[2].speech = replacement;
        for (auto &q : original.questions)
            q.speech = voice;
        const auto project = temp.filePath("project.buzz.json"), source = temp.filePath("source.buzz.json");
        QString error;
        QVERIFY2(studio::saveDocument(edited, project, &error), qPrintable(error));
        QVERIFY2(studio::saveDocument(original, source, &error), qPrintable(error));
        StudioController c;
        c.openFile(QUrl::fromLocalFile(project));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        c.openFile(QUrl::fromLocalFile(source), true);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        c.select(edited.questions[0].id);
        QCOMPARE(c.current()["speech"].toString(), voice);
        c.select(edited.questions[1].id);
        QVERIFY(c.current()["speech"].toString().isEmpty());
        QCOMPARE(c.current()["question"].toString(), edited.questions[1].text);
        c.select(edited.questions[2].id);
        QCOMPARE(c.current()["speech"].toString(), replacement);
        c.undo();
        c.select(edited.questions[0].id);
        QVERIFY(c.current()["speech"].toString().isEmpty());
    }
    void customCategoriesAreEditedRemovedAndUndone() {
        StudioController c;
        c.newProject(true);
        const int subject = c.addPackSubject({{"pl", QString::fromUtf8("Święta")}});
        QVERIFY(subject >= 10001);
        auto list = c.packCategories();
        QCOMPARE(list.size(), 2);
        const int sub = list[1].toMap()["id"].toInt();
        QCOMPARE(list[1].toMap()["parent"].toInt(), subject);
        c.updateCurrent({{"subject", subject}, {"category", sub}});
        QCOMPARE(c.current()["category"].toInt(), sub);
        // Renaming shows the new name on the question.
        list[0] = QVariantMap{{"id", subject}, {"parent", 0}, {"names", QVariantMap{{"pl", "Boże Narodzenie"}}}};
        c.setPackCategories(list);
        QCOMPARE(c.current()["category_name"].toString(), QString("Boże Narodzenie"));
        // Removing the subject unmaps its questions; undo brings both back.
        c.setPackCategories({});
        QCOMPARE(c.current()["subject"].toInt(), 0);
        QVERIFY(c.packCategories().isEmpty());
        c.undo();
        QCOMPARE(c.current()["subject"].toInt(), subject);
        QCOMPARE(c.packCategories().size(), 2);
        // A subcategory without its subject is refused.
        c.setPackCategories({QVariantMap{{"id", 10050}, {"parent", 10049}, {"names", QVariantMap{{"pl", "x"}}}}});
        QCOMPARE(c.packCategories().size(), 2);
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
        c.undo(); // A duplicate import leaves the last actual edit at the top of the undo stack.
        QCOMPARE(c.current()["question"].toString(), QString("Which letter comes first?"));
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
