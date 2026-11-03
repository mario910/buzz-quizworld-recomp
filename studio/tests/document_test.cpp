#include "buzz_import.h"
#include "document.h"
#include "languages.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>

class DocumentTest final : public QObject {
    Q_OBJECT
    QJsonObject validQuestion() const {
        return {{"id", "test"},
                {"question", "Zażółć gęślą jaźń?"},
                {"answers", QJsonArray{"jeden", "dwa", "trzy", "cztery"}},
                {"correct_index", 2},
                {"subject", 235},
                {"category", 237},
                {"difficulty", 2}};
    }
    studio::LoadResult parse(QJsonObject q) const {
        return studio::parseJson(QJsonDocument(QJsonObject{{"questions", QJsonArray{q}}}).toJson());
    }
  private slots:
    void preservesUnicodeCorrectAnswerAndUnknownFields() {
        auto q = validQuestion();
        q["editor_note"] = "Zachowaj mnie";
        auto result = parse(q);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.document.questions[0].correct, 2);
        const auto reloaded = studio::parseJson(studio::serialize(result.document));
        QVERIFY(reloaded.ok());
        QCOMPARE(reloaded.document.questions[0].text, q["question"].toString());
        QCOMPARE(reloaded.document.questions[0].extra["editor_note"].toString(), QString("Zachowaj mnie"));
    }
    void rejectsInvalidAnswerShapesAndIndices() {
        for (const auto &bad : QList<QJsonValue>{-1, 4, 1.5, "zero", true}) {
            auto q = validQuestion();
            q["correct_index"] = bad;
            QVERIFY(!parse(q).ok());
        }
        auto q = validQuestion();
        q["answers"] = QJsonArray{"a", "b", false, "d"};
        QVERIFY(!parse(q).ok());
        q["answers"] = QJsonArray{"a", "b"};
        QVERIFY(!parse(q).ok());
    }
    void rejectsDuplicateIdsAndUnsupportedVersions() {
        const auto q = validQuestion();
        auto result = studio::parseJson(QJsonDocument(QJsonObject{{"questions", QJsonArray{q, q}}}).toJson());
        QVERIFY(!result.ok());
        QVERIFY(result.document.questions.isEmpty());
        result = studio::parseJson(QJsonDocument(QJsonObject{{"format", "buzz-question-studio"},
                                                             {"version", 99},
                                                             {"questions", QJsonArray{q}}})
                                       .toJson());
        QVERIFY(!result.ok());
    }
    void legacyExportPlacesCorrectAnswerFirst() {
        auto result = parse(validQuestion());
        QVERIFY(result.ok());
        const auto root = QJsonDocument::fromJson(studio::serialize(result.document, {}, true)).object();
        const auto q = root["questions"].toArray()[0].toObject();
        QCOMPARE(q["answers"].toArray(), (QJsonArray{"trzy", "jeden", "dwa", "cztery"}));
        QVERIFY(!q.contains("correct_index"));
        QCOMPARE(result.document.questions[0].correct, 2);
    }
    void legacyExportRemovesAlternativeCorrectIndex() {
        auto q = validQuestion();
        q.remove("correct_index");
        q["correct"] = 2;
        const auto result = parse(q);
        QVERIFY(result.ok());
        const auto reloaded = studio::parseJson(studio::serialize(result.document, {}, true));
        QVERIFY(reloaded.ok());
        QCOMPARE(reloaded.document.questions[0].correct, 0);
        QCOMPARE(reloaded.document.questions[0].answers[0], QString("trzy"));
    }
    void rebaseRelativeMediaWhenSavingAs() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir().mkpath(temp.path() + "/first");
        QDir().mkpath(temp.path() + "/second");
        QFile image(temp.path() + "/first/picture.png");
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write("image");
        image.close();
        auto q = validQuestion();
        q["image"] = "picture.png";
        auto result = studio::parseJson(QJsonDocument(QJsonObject{{"questions", QJsonArray{q}}}).toJson(),
                                        temp.path() + "/first");
        QVERIFY(result.ok());
        QString error;
        const auto dest = temp.path() + "/second/test.buzz.json";
        QVERIFY2(studio::saveDocument(result.document, dest, &error), qPrintable(error));
        auto loaded = studio::loadDocument(dest);
        QVERIFY(loaded.ok());
        QCOMPARE(loaded.document.questions[0].image, QDir::cleanPath(image.fileName()));
        QVERIFY(loaded.document.questions[0].problems().isEmpty());
    }
    void kipImportKeepsEditsOnProjectReload() {
        auto q = validQuestion();
        q.remove("question");
        q.remove("answers");
        q.remove("correct_index");
        q["difficulty"] = "EXPERT";
        q["category"] = QJsonArray{"Experience", "Science", "GeneralScience"};
        q["langs"] = QJsonObject{{"pl", QJsonObject{{"question", "Pytanie z KiP?"},
                                                    {"answers", QJsonArray{"A", "B", "C", "D"}},
                                                    {"correct", 3}}}};
        auto result = parse(q);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.document.questions[0].difficulty, 3);
        QCOMPARE(result.document.questions[0].correct, 3);
        result.document.questions[0].text = "Moja poprawka";
        auto reloaded = studio::parseJson(studio::serialize(result.document));
        QVERIFY(reloaded.ok());
        QCOMPARE(reloaded.document.questions[0].text, QString("Moja poprawka"));
    }
    void invalidSaveDoesNotOverwriteExistingFile() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/test.json";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("original");
        f.close();
        studio::Document doc;
        doc.questions << studio::newQuestion();
        QString error;
        QVERIFY(!studio::saveDocument(doc, path, &error, true));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("original"));
        QVERIFY(!error.isEmpty());
    }
    void rejectsTruncatedNativeInputs() {
        QVERIFY_EXCEPTION_THROWN(studio::decodeHuffman(QByteArray::fromHex("00ff00000001"), {}),
                                 std::runtime_error);
        QTemporaryDir temp;
        const auto path = temp.path() + "/bad.pak";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a zip archive at all");
        f.close();
        auto result = studio::importBuzz(path);
        QVERIFY(!result.ok());
        QVERIFY(result.document.questions.isEmpty());
    }
    void importsQuizTvWithoutMisreadingPictureOrMultipleAnswers() {
        const auto path = QFINDTESTDATA("fixtures/native-quiztv.pak");
        QVERIFY(!path.isEmpty());
        auto result = studio::importBuzz(path);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.document.questions.size(), 1);
        const auto &q = result.document.questions[0];
        QCOMPARE(q.text, QString::fromUtf8("Która litera jest pierwsza?"));
        QCOMPARE(q.topic, QString("Alfabet"));
        QCOMPARE(q.answers, (QStringList{"A", "B", "C", "D"}));
        QCOMPARE(q.correct, 0);
        QCOMPARE(q.subjectId, 235);
        QCOMPARE(q.categoryId, 237);
        QCOMPARE(result.warnings.size(), 1);
        QVERIFY(result.warnings[0].contains("2 pytań"));
    }
    void retainsAndEditsEveryKipLanguage() {
        auto q = validQuestion();
        auto pl = q;
        auto en = q;
        en["question"] = "Which answer?";
        en["answers"] = QJsonArray{"one", "two", "three", "four"};
        en["correct_index"] = 1;
        q["langs"] = QJsonObject{{"pl", pl}, {"en", en}};
        const auto bytes = QJsonDocument(QJsonObject{{"questions", QJsonArray{q}}}).toJson();
        auto result = studio::parseJson(bytes, {}, "en");
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.document.language, QString("en"));
        auto &question = result.document.questions[0];
        QCOMPARE(question.text, QString("Which answer?"));
        QCOMPARE(question.correct, 1);
        question.text = "Edited English?";
        question.speech = "/tmp/English.wav";
        result.document.switchLanguage("pl");
        QCOMPARE(question.text, pl["question"].toString());
        QCOMPARE(question.correct, 2);
        QVERIFY(question.speech.isEmpty());
        question.text = "Poprawiony polski tekst";
        const auto saved = studio::serialize(result.document);
        auto reloaded = studio::parseJson(saved);
        QVERIFY2(reloaded.ok(), qPrintable(reloaded.error));
        QCOMPARE(reloaded.document.questions[0].text, QString("Poprawiony polski tekst"));
        reloaded.document.switchLanguage("en");
        QCOMPARE(reloaded.document.questions[0].text, QString("Edited English?"));
        QCOMPARE(reloaded.document.questions[0].speech, QString("/tmp/English.wav"));
        QCOMPARE(reloaded.document.questions[0].correct, 1);
    }
    void sourceImportSkipsQuestionsWithoutSelectedLanguage() {
        auto q = validQuestion();
        q["langs"] = QJsonObject{{"pl", validQuestion()}};
        auto other = q;
        other["id"] = "other";
        auto en = validQuestion();
        en["question"] = "English question?";
        other["langs"] = QJsonObject{{"en", en}};
        const auto bytes = QJsonDocument(QJsonObject{{"questions", QJsonArray{q, other}}}).toJson();
        auto result = studio::parseJson(bytes, {}, "en");
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.document.questions.size(), 1);
        QCOMPARE(result.document.questions[0].id, QString("other"));
        QCOMPARE(result.document.questions[0].text, QString("English question?"));
        QVERIFY(!result.warnings.isEmpty());
        auto polish = studio::parseJson(bytes, {}, "pl");
        QVERIFY(polish.ok());
        QCOMPARE(polish.document.questions.size(), 1);
        QCOMPARE(polish.document.questions[0].id, QString("test"));
        QVERIFY(!studio::parseJson(bytes, {}, "de").ok());
    }
    void rebasesMediaInInactiveLanguages() {
        QTemporaryDir temp;
        auto result = parse(validQuestion());
        auto &doc = result.document;
        doc.questions[0].image = temp.filePath("polski.png");
        doc.switchLanguage("en");
        doc.questions[0].image = temp.filePath("english.png");
        QDir().mkpath(temp.filePath("project"));
        QString error;
        const auto path = temp.filePath("project/multi.buzz.json");
        QVERIFY2(studio::saveDocument(doc, path, &error), qPrintable(error));
        auto reloaded = studio::loadDocument(path);
        QVERIFY(reloaded.ok());
        QCOMPARE(reloaded.document.questions[0].image, temp.filePath("english.png"));
        reloaded.document.switchLanguage("pl");
        QCOMPARE(reloaded.document.questions[0].image, temp.filePath("polski.png"));
    }
    void selectsNativeLanguageAndRejectsMissingOne() {
        const auto path = QFINDTESTDATA("fixtures/native-multilingual.pak");
        const auto source = studio::inspectLanguages(path);
        QVERIFY(source.ok());
        QCOMPARE(source.codes, (QStringList{"en", "pl"}));
        auto result = studio::loadDocument(path, "ENG");
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.document.language, QString("en"));
        QCOMPARE(result.document.questions[0].language, QString("en"));
        QCOMPARE(result.document.questions[0].text, QString("Which letter comes first?"));
        QVERIFY(!studio::loadDocument(path, "de").ok());
    }
    void versionOneMigratesAndLegacyExportsOnlyActiveLanguage() {
        auto q = validQuestion();
        const auto bytes =
            QJsonDocument(
                QJsonObject{{"format", "buzz-question-studio"}, {"version", 1}, {"questions", QJsonArray{q}}})
                .toJson();
        auto loaded = studio::parseJson(bytes);
        QVERIFY(loaded.ok());
        loaded.document.switchLanguage("en");
        auto &current = loaded.document.questions[0];
        current.text = "Question?";
        current.answers = QStringList{"a", "b", "c", "d"};
        current.correct = 3;
        const auto exported = QJsonDocument::fromJson(studio::serialize(loaded.document, {}, true)).object();
        const auto exportedQuestion = exported["questions"].toArray()[0].toObject();
        QVERIFY(!exportedQuestion.contains("translations"));
        QCOMPARE(exportedQuestion["answers"].toArray()[0].toString(), QString("d"));
        QCOMPARE(exported["pack"].toObject()["language"].toString(), QString("en"));
        QCOMPARE(QJsonDocument::fromJson(studio::serialize(loaded.document)).object()["version"].toInt(), 2);
    }
    void legacyMediaStaysWithItsOriginalLanguageAfterReload() {
        auto q = validQuestion();
        const QJsonArray media{QJsonObject{{"type", 1}, {"file", "POL/only-polish.sdi"}}};
        q["source_media"] = media;
        auto loaded = studio::parseJson(QJsonDocument(QJsonObject{{"format", "buzz-question-studio"},
                                                                  {"version", 1},
                                                                  {"pack", QJsonObject{{"language", "pl"}}},
                                                                  {"questions", QJsonArray{q}}})
                                            .toJson());
        QVERIFY(loaded.ok());
        loaded.document.switchLanguage("en");
        auto &english = loaded.document.questions[0];
        english.text = "English question?";
        english.answers = QStringList{"a", "b", "c", "d"};
        const auto saved = studio::serialize(loaded.document);
        auto reloaded = studio::parseJson(saved);
        QVERIFY(reloaded.ok());
        const auto active = reloaded.document.questions[0].localized();
        QVERIFY(active["source_media"].toArray().isEmpty());
        QVERIFY(reloaded.document.questions[0].extra["source_media"].toArray().isEmpty());
        reloaded.document.switchLanguage("pl");
        QCOMPARE(reloaded.document.questions[0].localized()["source_media"].toArray(), media);
    }
    void migratesLegacyBuzzIdentityToItsGameNamespace() {
        auto q = validQuestion();
        q["id"] = "buzz:991:1";
        q["source"] = "Buzz! Quiz TV";
        auto result = parse(q);
        QVERIFY(result.ok());
        QCOMPARE(result.document.questions[0].id, QString("buzz:1002:991:1"));
        q["source"] = "Buzz! Quiz World";
        result = parse(q);
        QVERIFY(result.ok());
        QCOMPARE(result.document.questions[0].id, QString("buzz:1003:991:1"));
    }
    void normalizesLanguageCodesAndRejectsInvalidTags() {
        QCOMPARE(studio::languageCode("POL"), QString("pl"));
        QCOMPARE(studio::languageCode("ENG"), QString("en"));
        QCOMPARE(studio::languageCode("pt_BR"), QString("pt-BR"));
        QCOMPARE(studio::languageCode("en-GB"), QString("en-GB"));
        QVERIFY(studio::languageCode("x").isEmpty());
        QVERIFY(studio::languageCode("../../en").isEmpty());
    }
    void englishCatalogIsCompleteAndKeepsPlaceholdersAndFileFilters() {
        QFile catalog(QFINDTESTDATA("../i18n/studio_en.ts"));
        QVERIFY(catalog.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&catalog);
        QString source, translated;
        bool unfinished = false;
        int messages = 0;
        const QRegularExpression tokens("%[1-9][0-9]*|%n|\\*\\.[a-zA-Z0-9.]+");
        auto placeholders = [&](const QString &text) {
            QStringList values;
            auto matches = tokens.globalMatch(text);
            while (matches.hasNext())
                values << matches.next().captured();
            values.sort();
            return values;
        };
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("message")) {
                source.clear();
                translated.clear();
                unfinished = false;
            } else if (xml.isStartElement() && xml.name() == QStringLiteral("source")) {
                source = xml.readElementText();
            } else if (xml.isStartElement() && xml.name() == QStringLiteral("translation")) {
                unfinished = xml.attributes().value("type") == QStringLiteral("unfinished");
                translated = xml.readElementText();
            } else if (xml.isEndElement() && xml.name() == QStringLiteral("message")) {
                QVERIFY2(!unfinished && !translated.isEmpty(), qPrintable(source));
                QCOMPARE(placeholders(source), placeholders(translated));
                ++messages;
            }
        }
        QVERIFY(!xml.hasError());
        QVERIFY(messages > 200);
    }
};
QTEST_GUILESS_MAIN(DocumentTest)
#include "document_test.moc"
