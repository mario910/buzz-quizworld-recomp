#include "controller.h"
#include "localization.h"
#include "main_window.h"
#include "source_movie.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>
#include <algorithm>

class UiTest final : public QObject {
    Q_OBJECT
    template <typename T> static T *find(QWidget &window, const char *name) {
        return window.findChild<T *>(QString::fromLatin1(name));
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("BuzzStudioTests");
        QCoreApplication::setApplicationName("UiTests");
        QStandardPaths::setTestModeEnabled(true);
    }
    void interfaceLanguageFollowsTheSystemAndIsRemembered() {
        QCOMPARE(Localization::defaultLanguage(QLocale(QLocale::Polish)), QString("pl"));
        QCOMPARE(Localization::defaultLanguage(QLocale(QLocale::English)), QString("en"));
        QCOMPARE(Localization::defaultLanguage(QLocale(QLocale::German)), QString("de"));
        QCOMPARE(Localization::defaultLanguage(QLocale(QLocale::NorwegianBokmal)), QString("no"));
        QCOMPARE(Localization::defaultLanguage(QLocale(QLocale::Japanese)), QString("en"));
        QSettings().remove("interfaceLanguage");
        {
            Localization first;
            QCOMPARE(first.language(), Localization::defaultLanguage(QLocale::system()));
            first.setLanguage(first.language() == "pl" ? "en" : "pl");
        }
        const auto chosen = QSettings().value("interfaceLanguage").toString();
        QVERIFY(chosen == "pl" || chosen == "en");
        QCOMPARE(Localization().language(), chosen);
        QCOMPARE(Localization(nullptr, "en").language(), QString("en")); // --ui-language is session-only.
        QCOMPARE(QSettings().value("interfaceLanguage").toString(), chosen);
        QSettings().remove("interfaceLanguage");
    }
    void editsIndependentRoundPlansAndSavesTheProject() {
        Localization localization(nullptr, "en");
        StudioController controller;
        MainWindow window(controller, localization);
        window.show();
        auto *action = window.findChild<QAction *>("editRoundPlans");
        QVERIFY(action);
        QCOMPARE(action->text(), QString("Round plans…"));
        action->trigger();
        auto *dialog = find<QDialog>(window, "roundPlansDialog");
        QVERIFY(dialog && dialog->isVisible());
        auto *tabs = find<QTabBar>(*dialog, "roundPlanMode");
        auto *list = find<QListWidget>(*dialog, "roundPlanList");
        auto *type = find<QComboBox>(*dialog, "roundPlanType");
        auto *questionCount = find<QSpinBox>(*dialog, "roundPlanQuestionCount");
        auto *add = find<QPushButton>(*dialog, "roundPlanAdd");
        auto *remove = find<QPushButton>(*dialog, "roundPlanRemove");
        auto *down = find<QPushButton>(*dialog, "roundPlanMoveDown");
        auto *reset = find<QPushButton>(*dialog, "roundPlanReset");
        auto *save = find<QPushButton>(*dialog, "primary");
        QVERIFY(tabs && list && type && questionCount && add && remove && down && reset && save);
        QCOMPARE(add->text(), QString("Add round"));
        QCOMPARE(remove->text(), QString("Remove round"));
        QCOMPARE(questionCount->text(), QString("Default"));
        QCOMPARE(list->count(), 3);
        type->setCurrentIndex(type->findData("StopTheClockChallenge"));
        questionCount->setValue(5);
        QTest::mouseClick(down, Qt::LeftButton);
        QCOMPARE(list->currentRow(), 1);
        QCOMPARE(list->item(1)->text(), QString("Stop the Clock (5)"));
        QCOMPARE(questionCount->value(), 5);
        QTest::mouseClick(add, Qt::LeftButton);
        QCOMPARE(list->count(), 4);
        QCOMPARE(list->currentRow(), 2);
        QCOMPARE(type->currentData().toString(), QString("StopTheClockChallenge"));
        QCOMPARE(questionCount->value(), 0);
        questionCount->setValue(7);
        type->setCurrentIndex(type->findData("OverTheEdgeChallenge"));
        QCOMPARE(questionCount->value(), 7);
        tabs->setCurrentIndex(1);
        QCOMPARE(list->count(), 7);
        QTest::mouseClick(down, Qt::LeftButton);
        QCOMPARE(list->item(1)->text(), QString("Point Builder"));
        questionCount->setValue(3);
        list->setCurrentRow(6);
        QTest::mouseClick(remove, Qt::LeftButton);
        QCOMPARE(list->count(), 6);
        QCOMPARE(list->currentRow(), 5);
        tabs->setCurrentIndex(2);
        QCOMPARE(list->count(), 4);
        QVERIFY(type->findData("PassTheBomb") < 0);
        questionCount->setValue(8);
        for (int removed = 0; removed < 3; ++removed)
            QTest::mouseClick(remove, Qt::LeftButton);
        QCOMPARE(list->count(), 1);
        QVERIFY(!remove->isEnabled());
        QTest::mouseClick(reset, Qt::LeftButton);
        QCOMPARE(list->count(), 4);
        QCOMPARE(type->currentData().toString(), QString("PointBuilder"));
        QCOMPARE(questionCount->value(), 0);
        QTest::mouseClick(add, Qt::LeftButton);
        type->setCurrentIndex(type->findData("PieFight"));
        questionCount->setValue(6);
        QCOMPARE(list->count(), 5);
        tabs->setCurrentIndex(0);
        QCOMPARE(list->count(), 4);
        list->setCurrentRow(1);
        QCOMPARE(questionCount->value(), 5);
        tabs->setCurrentIndex(1);
        QCOMPARE(list->count(), 6);
        list->setCurrentRow(1);
        QCOMPARE(questionCount->value(), 3);
        QCOMPARE(controller.roundPlans(), studio::defaultRoundPlans());
        QTest::mouseClick(save, Qt::LeftButton);
        QTRY_VERIFY(!find<QDialog>(window, "roundPlansDialog"));

        auto expected = studio::defaultRoundPlans();
        expected["1p"][1] = {"StopTheClockChallenge", 5};
        expected["1p"].insert(2, {"OverTheEdgeChallenge", 7});
        expected["4p"].swapItemsAt(0, 1);
        expected["4p"][1].questionCount = 3;
        expected["4p"].removeLast();
        expected["8p"].insert(1, {"PieFight", 6});
        QCOMPARE(controller.roundPlans(), expected);
        QVERIFY(controller.dirty());
        controller.undo();
        QCOMPARE(controller.roundPlans(), studio::defaultRoundPlans());
        controller.redo();
        QCOMPARE(controller.roundPlans(), expected);

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = temp.filePath("rounds.buzz.json");
        QVERIFY(controller.saveFile(QUrl::fromLocalFile(path)));
        QCOMPARE(studio::loadDocument(path).document.roundPlans(), expected);
    }
    void mediaErrorsUseInterfaceLanguage_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("buildError");
        QTest::addColumn<QString>("decodeError");
        QTest::newRow("English") << QString("en")
            << QString("Question %1: This question uses media from the original game. Attach an image or video.")
            << QString("This question has no supported PS2 media reference. Reimport older video entries.");
        QTest::newRow("French") << QString("fr")
            << QString("Question %1 : Cette question utilise des médias du jeu original. Ajoutez une image ou une vidéo.")
            << QString("Cette question n’a pas de référence à un média PS2 pris en charge. Réimportez les anciennes entrées vidéo.");
        QTest::newRow("Polish") << QString("pl")
            << QString("Pytanie %1: Pytanie korzysta z nierozwiązanych mediów z gry. Przypisz obrazek lub film.")
            << QString("Pytanie nie ma obsługiwanego odwołania do mediów PS2. Zaimportuj ponownie starsze wpisy z filmami.");
    }
    void mediaErrorsUseInterfaceLanguage() {
        QFETCH(QString, language);
        QFETCH(QString, buildError);
        QFETCH(QString, decodeError);
        Localization localization(nullptr, language);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        studio::BuildRequest request;
        request.document = studio::exampleDocument();
        QCOMPARE(request.document.language, QString("pl"));
        auto &question = request.document.questions.first();
        auto translation = question.localized();
        translation["source_media"] = QJsonArray{QJsonObject{{"type", "ps2-picture"}}};
        question.translations[question.language] = translation;
        request.templatePath = QFINDTESTDATA("fixtures/build-template.pak");
        request.outputDirectory = temp.path();

        QCOMPARE(studio::buildPack(request).error, buildError.arg(question.id));
        QCOMPARE(studio::decodeSourceQuestionMedia(question, {}).error, decodeError);
    }
    void newProjectsAndExamplesUseTheInterfaceLanguage() {
        Localization localization(nullptr, "en");
        StudioController controller(nullptr, localization.language());
        MainWindow window(controller, localization);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.openProjectLanguages();
        auto *dialog = find<QDialog>(window, "projectLanguageDialog");
        QVERIFY(dialog);
        auto *combo = find<QComboBox>(*dialog, "projectLanguageCombo");
        QCOMPARE(combo->count(), Localization::languages().size());
        QCOMPARE(combo->currentData().toString(), QString("en"));
        dialog->accept();
        QTRY_VERIFY(!find<QDialog>(window, "projectLanguageDialog"));
        QTest::mouseClick(find<QPushButton>(window, "exampleButton"), Qt::LeftButton);
        QCOMPARE(controller.current()["question"].toString(), QString("How many planets orbit the Sun?"));
        controller.newProject();
        controller.setPackLanguage("fr");
        QVERIFY(!controller.dirty());
        QTest::mouseClick(find<QPushButton>(window, "exampleButton"), Qt::LeftButton);
        QCOMPARE(controller.packLanguage(), QString("fr"));
        QCOMPARE(controller.current()["question"].toString(), QString::fromUtf8("Combien de planètes tournent autour du Soleil ?"));
        localization.setLanguage("de");
        QCOMPARE(controller.packLanguage(), QString("fr"));
        controller.newProject(true);
        QCOMPARE(controller.packLanguage(), QString("de"));
        QCOMPARE(controller.current()["question"].toString(), QString("Wie viele Planeten umkreisen die Sonne?"));
    }
    void importRecoveryStaysInTheWizardAndReportsRemainingMedia() {
        if (studio::configuredMediaFfmpeg().isEmpty())
            QSKIP("FFmpeg not installed.");
        Localization localization(nullptr, "en");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto document = studio::loadDocument(QFINDTESTDATA("fixtures/build-source-en.json")).document;
        auto &question = document.questions.first();
        auto variant = question.localized();
        variant["source_media"] = QJsonArray{QJsonObject{{"type", "psp-video"},
            {"archive", temp.filePath("missing.iso")}, {"entry", "missing.pmf"}}};
        question.translations[question.language] = variant;
        const auto path = temp.filePath("media.buzz.json");
        QString error;
        QVERIFY2(studio::saveDocument(document, path, &error), qPrintable(error));
        StudioController controller(nullptr, "en");
        MainWindow window(controller, localization);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mouseClick(find<QPushButton>(window, "emptyImportWizardButton"), Qt::LeftButton);
        auto *wizard = find<QDialog>(window, "importWizard");
        QVERIFY(wizard);
        find<QLineEdit>(*wizard, "importSourcePath")->setText(path);
        auto *next = find<QPushButton>(*wizard, "importNext");
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY(find<QComboBox>(*wizard, "importDestination")->isVisible());
        QTest::mouseClick(next, Qt::LeftButton);
        auto *recover = find<QPushButton>(*wizard, "importRecoverMedia");
        QVERIFY(recover);
        QTRY_VERIFY(recover->isVisible());
        QVERIFY(recover->isDefault());
        QCOMPARE(next->text(), QString("Skip and go to the editor"));
        QCOMPARE(controller.sourceMediaStatus().recoverable, 1);
        QTest::mouseClick(recover, Qt::LeftButton);
        QTRY_COMPARE(next->text(), QString("Go to the editor"));
        QVERIFY(wizard->isVisible());
        QVERIFY(!find<QDialog>(window, "unresolvedMediaDialog"));
        const auto labels = wizard->findChildren<QLabel *>();
        QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel *label) {
            return label->isVisible() && label->text() == "Images or videos still missing after recovery: 1";
        }));
        auto *copy = find<QPushButton>(*wizard, "copyImportReport");
        QVERIFY(copy && copy->isVisible());
        QCOMPARE(copy->text(), QString("Copy import report"));
        QTest::mouseClick(copy, Qt::LeftButton);
        const auto report = QApplication::clipboard()->text();
        QVERIFY(report.contains("Images or videos still missing after recovery: 1"));
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY(!find<QDialog>(window, "importWizard"));
        auto *play = find<QPushButton>(window, "previewVideo");
        QVERIFY(play && play->isVisible());
        QCOMPARE(play->text(), QString("Play"));
        const auto editorLabels = window.findChildren<QLabel *>();
        QVERIFY(std::any_of(editorLabels.cbegin(), editorLabels.cend(), [](const QLabel *label) {
            return label->isVisible() && label->text() == "Click Play next to the video to recover it from the game.";
        }));
    }
    void buildSkipsUnresolvedMediaAndLinksToTheQuestion() {
        Localization localization(nullptr, "en");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto document = studio::loadDocument(QFINDTESTDATA("fixtures/build-source-en.json")).document;
        QCOMPARE(document.questions.size(), 2);
        auto &question = document.questions[1];
        auto variant = question.localized();
        variant["source_media"] = QJsonArray{QJsonObject{{"type", "ps2-picture"}}};
        question.translations[question.language] = variant;
        const auto path = temp.filePath("media.buzz.json");
        QString error;
        QVERIFY2(studio::saveDocument(document, path, &error), qPrintable(error));
        StudioController controller;
        MainWindow window(controller, localization);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QTest::mouseClick(find<QPushButton>(window, "emptyImportWizardButton"), Qt::LeftButton);
        auto *wizard = find<QDialog>(window, "importWizard");
        QVERIFY(wizard && wizard->isVisible());
        find<QLineEdit>(*wizard, "importSourcePath")->setText(QUrl::fromLocalFile(path).toString());
        auto *next = find<QPushButton>(*wizard, "importNext");
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY(find<QComboBox>(*wizard, "importDestination")->isVisible());
        QVERIFY(!find<QComboBox>(*wizard, "importLanguageCombo")->isVisible());
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_COMPARE(next->text(), QString("Go to the editor"));
        QVERIFY(!find<QPushButton>(*wizard, "importRecoverMedia")->isVisible());
        auto *build = find<QPushButton>(*wizard, "importBuildPack");
        QVERIFY(build && build->isVisible());
        QTest::mouseClick(build, Qt::LeftButton);
        QTRY_VERIFY(!find<QDialog>(window, "importWizard"));

        QVERIFY(find<QPushButton>(window, "recoverSourceMedia")->isVisible());
        auto *dialog = find<QDialog>(window, "buildPackDialog");
        QVERIFY(dialog && dialog->isVisible());
        auto *readiness = find<QLabel>(*dialog, "buildReadiness");
        QVERIFY(readiness);
        QTRY_COMPARE(readiness->text(), QString("Ready: 1"));
        QCOMPARE(find<QPushButton>(*dialog, "reviewBuildIssues")->text(), QString("Needs fixes: 1"));
        QVERIFY(!find<QPushButton>(*dialog, "buildRecoverMedia"));
        find<QLineEdit>(*dialog, "buildTemplatePath")->setText(QFINDTESTDATA("fixtures/build-template.pak"));
        find<QLineEdit>(*dialog, "buildOutputPath")->setText(temp.filePath("output"));
        find<QSpinBox>(*dialog, "buildPackNumber")->setValue(103);
        find<QSpinBox>(*dialog, "buildCategoryBase")->setValue(700);
        QSignalSpy finished(&controller, &StudioController::packFinished);
        QTest::mouseClick(find<QPushButton>(*dialog, "startPackBuild"), Qt::LeftButton);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(controller.lastBuild()["questions"].toInt(), 1);
        QCOMPARE(controller.totalCount(), 2);
        dialog = find<QDialog>(window, "packResultDialog");
        QVERIFY(dialog && dialog->isVisible());
        auto *list = find<QListWidget>(*dialog, "buildExcludedQuestions");
        QVERIFY(list);
        QCOMPARE(list->count(), 1);
        QVERIFY(list->item(0)->text().contains("Attach an image or video."));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
        QTRY_COMPARE(controller.current()["id"].toString(), question.id);
        QTRY_VERIFY(!find<QDialog>(window, "packResultDialog"));

        const auto previousPack = controller.lastBuild()["path"].toString();
        QVERIFY(QFileInfo::exists(previousPack));
        controller.select(document.questions.first().id);
        controller.updateCurrent({{"question", ""}});
        window.openBuild();
        dialog = find<QDialog>(window, "buildPackDialog");
        QVERIFY(dialog && dialog->isVisible());
        readiness = find<QLabel>(*dialog, "buildReadiness");
        QVERIFY(readiness);
        QTRY_COMPARE(readiness->text(), QString("Ready: 0"));
        QCOMPARE(find<QPushButton>(*dialog, "reviewBuildIssues")->text(), QString("Needs fixes: 2"));
        QSignalSpy review(&controller, &StudioController::packReviewRequired);
        QTest::mouseClick(find<QPushButton>(*dialog, "startPackBuild"), Qt::LeftButton);
        QTRY_COMPARE(review.size(), 1);
        QCOMPARE(finished.size(), 1);
        auto *report = find<QDialog>(window, "packResultDialog");
        QVERIFY(report && report->isVisible());
        QCOMPARE(report->windowTitle(), QString("Pack was not built"));
        QCOMPARE(find<QListWidget>(*report, "buildExcludedQuestions")->count(), 2);
        QVERIFY(QFileInfo::exists(previousPack));
    }
    void reviewsBuildIssuesAndReturnsToTheSameBuildSettings() {
        Localization localization(nullptr, "en");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto document = studio::loadDocument(QFINDTESTDATA("fixtures/build-source-en.json")).document;
        QCOMPARE(document.questions.size(), 2);
        const auto firstText = document.questions[0].text;
        document.questions[0].text.clear();
        document.questions[1].categoryId = 0;
        const auto project = temp.filePath("review.buzz.json");
        QString error;
        QVERIFY2(studio::saveDocument(document, project, &error), qPrintable(error));
        StudioController controller;
        controller.openFile(QUrl::fromLocalFile(project));
        QTRY_VERIFY(!controller.busy());
        MainWindow window(controller, localization);
        QFile style(":/studio.qss");
        QVERIFY(style.open(QIODevice::ReadOnly));
        window.setStyleSheet(QString::fromUtf8(style.readAll()));
        window.resize(1120, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.openBuild();
        auto *build = find<QDialog>(window, "buildPackDialog");
        QVERIFY(build && build->isVisible());
        const auto templatePath = QFINDTESTDATA("fixtures/build-template.pak");
        const auto output = temp.filePath("keep-this-path");
        find<QLineEdit>(*build, "buildTemplatePath")->setText(templatePath);
        find<QLineEdit>(*build, "buildOutputPath")->setText(output);
        find<QSpinBox>(*build, "buildPackNumber")->setValue(415);
        find<QCheckBox>(*build, "buildAdvanced")->setChecked(true);
        find<QSpinBox>(*build, "buildCategoryBase")->setValue(732);
        QVERIFY(build->height() <= window.height() - 48);
        auto *counter = find<QPushButton>(*build, "reviewBuildIssues");
        QVERIFY(counter);
        QTRY_VERIFY(counter->isVisible());
        QCOMPARE(counter->text(), QString("Needs fixes: 2"));
        QTest::mouseClick(counter, Qt::LeftButton);

        auto *issues = find<QDialog>(window, "buildIssuesDialog");
        QVERIFY(issues && issues->isVisible());
        QVERIFY(issues->height() <= window.height() - 48);
        auto *list = find<QListWidget>(*issues, "buildIssuesList");
        QVERIFY(list);
        QCOMPARE(list->count(), 2);
        auto *reason = find<QLabel>(*issues, "buildIssueDetails");
        QVERIFY(reason && !reason->text().isEmpty());
        const auto firstReason = reason->text();
        QTest::keyClick(list, Qt::Key_Down);
        QCOMPARE(list->currentRow(), 1);
        QTRY_COMPARE(reason->text(), QString("Choose a category and subcategory for this question."));
        QTest::keyClick(list, Qt::Key_Up);
        QCOMPARE(reason->text(), firstReason);
        QTest::keyClick(list, Qt::Key_Return);
        QTRY_VERIFY(!find<QDialog>(window, "buildIssuesDialog"));

        auto *panel = find<QWidget>(window, "buildReviewPanel");
        QVERIFY(panel && panel->isVisible());
        auto *liveReason = find<QLabel>(*panel, "buildReviewReason");
        QVERIFY(liveReason);
        QCOMPARE(liveReason->text(), firstReason);
        auto *next = find<QPushButton>(*panel, "nextBuildIssue");
        auto *previous = find<QPushButton>(*panel, "previousBuildIssue");
        QVERIFY(next && next->isEnabled() && previous && !previous->isEnabled());
        find<QPlainTextEdit>(window, "questionText")->setPlainText(firstText);
        QTest::mouseClick(next, Qt::LeftButton);
        QCOMPARE(controller.current()["id"].toString(), document.questions[1].id);
        QCOMPARE(liveReason->text(), QString("Choose a category and subcategory for this question."));
        QVERIFY(!next->isEnabled());
        QTest::mouseClick(previous, Qt::LeftButton);
        QCOMPARE(controller.current()["question"].toString(), firstText);
        QCOMPARE(liveReason->text(), QString("Fixed"));

        QTest::mouseClick(find<QPushButton>(*panel, "resumeBuildAfterReview"), Qt::LeftButton);
        QTRY_VERIFY((build = find<QDialog>(window, "buildPackDialog")) && build->isVisible());
        QVERIFY(!panel->isVisible());
        QCOMPARE(find<QLineEdit>(*build, "buildTemplatePath")->text(), templatePath);
        QCOMPARE(find<QLineEdit>(*build, "buildOutputPath")->text(), output);
        QCOMPARE(find<QSpinBox>(*build, "buildPackNumber")->value(), 415);
        QCOMPARE(find<QSpinBox>(*build, "buildCategoryBase")->value(), 732);
        QVERIFY(find<QCheckBox>(*build, "buildAdvanced")->isChecked());
        counter = find<QPushButton>(*build, "reviewBuildIssues");
        QTRY_VERIFY(counter->isVisible());
        QCOMPARE(counter->text(), QString("Needs fixes: 1"));
    }
    void assignsAnEntireSourceSectionFromTheImportSummary() {
        Localization localization(nullptr, "en");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto gameFile = temp.filePath("game.json");
        const auto question = [](const QString &id, const QString &section, const QString &subcategory) {
            const QJsonObject text{{"question", id}, {"answers", QJsonArray{"A", "B", "C", "D"}}, {"correct_index", 0}};
            return QJsonObject{{"id", id}, {"category", QJsonArray{section, subcategory}},
                               {"langs", QJsonObject{{"en", text}}}};
        };
        const QJsonArray questions{question("one", "World", "Geography"),
                                   question("two", "World", "History"),
                                   question("three", "Music", "Classical")};
        QFile source(gameFile);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QVERIFY(source.write(QJsonDocument(QJsonObject{{"questions", questions}}).toJson()) > 0);
        source.close();
        StudioController controller;
        MainWindow window(controller, localization);
        window.resize(1120, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mouseClick(find<QPushButton>(window, "emptyImportWizardButton"), Qt::LeftButton);
        auto *wizard = find<QDialog>(window, "importWizard");
        QVERIFY(wizard && wizard->isVisible());
        find<QLineEdit>(*wizard, "importSourcePath")->setText(gameFile);
        auto *next = find<QPushButton>(*wizard, "importNext");
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY(find<QComboBox>(*wizard, "importDestination")->isVisible());
        QTest::mouseClick(next, Qt::LeftButton);
        auto *entry = find<QPushButton>(*wizard, "assignImportedCategories");
        QVERIFY(entry);
        QTRY_VERIFY(entry->isVisible());
        QCOMPARE(entry->text(), QString("Change category assignments…"));
        QCOMPARE(controller.importedQuestionsWithGameCategory(), 3);
        controller.select("one");
        const int firstOriginal = controller.current()["category"].toInt();
        controller.select("two");
        const int secondOriginal = controller.current()["category"].toInt();
        controller.select("three");
        const int musicOriginal = controller.current()["category"].toInt();
        QVERIFY(studio::isCustomCategory(firstOriginal) && studio::isCustomCategory(secondOriginal) &&
                studio::isCustomCategory(musicOriginal));
        QTest::mouseClick(entry, Qt::LeftButton);

        auto *dialog = find<QDialog>(*wizard, "importedCategoriesDialog");
        QVERIFY(dialog && dialog->isVisible());
        QVERIFY(dialog->height() <= window.height() - 48);
        auto *tree = find<QTreeWidget>(*dialog, "importedCategorySections");
        QVERIFY(tree && tree->topLevelItemCount() == 1);
        auto *game = tree->topLevelItem(0);
        QCOMPARE(game->text(0), QString("Knowledge Is Power"));
        QCOMPARE(game->childCount(), 2);
        auto *section = game->child(1);
        QCOMPARE(section->text(0), QString("World"));
        QCOMPARE(section->childCount(), 2);
        tree->setCurrentItem(section);
        auto *apply = find<QPushButton>(*dialog, "applyImportedCategory");
        QVERIFY(apply && !apply->isEnabled());
        auto *subject = find<QComboBox>(*dialog, "importedCategorySubject");
        subject->setCurrentIndex(subject->findData(250));
        auto *child = find<QComboBox>(*dialog, "importedCategoryChild");
        child->setCurrentIndex(child->findData(253));
        QVERIFY(apply->isEnabled());
        QCOMPARE(apply->text(), QString("Assign category (2)"));
        QTest::mouseClick(apply, Qt::LeftButton);
        for (const QString &id : {QString("one"), QString("two")}) {
            controller.select(id);
            QCOMPARE(controller.current()["subject"].toInt(), 250);
            QCOMPARE(controller.current()["category"].toInt(), 253);
        }
        controller.select("three");
        QCOMPARE(controller.current()["category"].toInt(), musicOriginal);
        dialog->reject();
        QTRY_VERIFY(wizard->isVisible());
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY(!find<QDialog>(window, "importWizard"));
        controller.undo();
        controller.select("one");
        QCOMPARE(controller.current()["category"].toInt(), firstOriginal);
        controller.select("two");
        QCOMPARE(controller.current()["category"].toInt(), secondOriginal);
    }
    void importWizardReviewsAndAppendsWithoutReplacingTheProject() {
        Localization localization(nullptr, "en");
        StudioController controller;
        controller.newProject(true);
        controller.updateCurrent({{"question", "An unsaved edit"}});
        const auto originalId = controller.current()["id"].toString();
        const int originalCount = controller.totalCount();
        MainWindow window(controller, localization);
        window.resize(1120, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *open = find<QPushButton>(window, "openImportWizardButton");
        QVERIFY(open && open->isVisible());
        QTest::mouseClick(open, Qt::LeftButton);
        auto *wizard = find<QDialog>(window, "importWizard");
        QVERIFY(wizard && wizard->isVisible());
        QCOMPARE(wizard->windowTitle(), QString("Import questions"));
        auto *source = find<QLineEdit>(*wizard, "importSourcePath");
        auto *next = find<QPushButton>(*wizard, "importNext");
        auto *back = find<QPushButton>(*wizard, "importBack");
        auto *languages = find<QComboBox>(*wizard, "importLanguageCombo");
        auto *destination = find<QComboBox>(*wizard, "importDestination");
        auto *error = find<QLabel>(*wizard, "issues");
        QVERIFY(source && next && back && languages && destination && error);
        QVERIFY(!next->isEnabled());

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        source->setText(temp.filePath("missing.pak"));
        QTest::mouseClick(next, Qt::LeftButton);
        QVERIFY(error->isVisible());
        QCOMPARE(error->text(), QString("Choose an existing question file or supported game folder."));
        QFile corrupt(temp.filePath("invalid.json"));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("invalid file");
        corrupt.close();
        source->setText(corrupt.fileName());
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY(error->isVisible());
        QCOMPARE(error->text(), QString("Invalid JSON."));

        const auto pack = QFINDTESTDATA("fixtures/native-multilingual.pak");
        QVERIFY(!pack.isEmpty());
        source->setText('"' + QDir::toNativeSeparators(pack) + '"');
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY(languages->isVisible());
        QCOMPARE(languages->count(), 2);
        QCOMPARE(languages->currentData().toString(), QString("pl"));
        QVERIFY(destination->currentData().toBool());
        QVERIFY(wizard->height() <= window.height() - 48);
        QTest::mouseClick(back, Qt::LeftButton);
        QVERIFY(source->isVisible());
        QCOMPARE(source->text(), '"' + QDir::toNativeSeparators(pack) + '"');
        QCOMPARE(controller.totalCount(), originalCount);

        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY(languages->isVisible());
        languages->setCurrentIndex(languages->findData("en"));
        QSignalSpy finished(&controller, &StudioController::importFinished);

        destination->setCurrentIndex(destination->findData(false));
        bool askedToSave = false;
        QTimer::singleShot(0, wizard, [&] {
            if (auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                askedToSave = prompt->parentWidget() == wizard;
                prompt->reject();
            }
        });
        QTest::mouseClick(next, Qt::LeftButton);
        QVERIFY(askedToSave);
        QCOMPARE(finished.size(), 0);
        QCOMPARE(controller.current()["question"].toString(), QString("An unsaved edit"));
        QVERIFY(languages->isVisible());

        destination->setCurrentIndex(destination->findData(true));
        languages->setCurrentIndex(languages->findData("en"));
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.takeFirst(), QVariantList({1, 0, 0}));
        QCOMPARE(controller.totalCount(), originalCount + 1);
        QCOMPARE(controller.current()["id"].toString(), originalId);
        QCOMPARE(next->text(), QString("Go to the editor"));
        QVERIFY(!find<QPushButton>(*wizard, "importBuildPack")->isVisible());
        const auto summaryLabels = wizard->findChildren<QLabel *>();
        QVERIFY(std::any_of(summaryLabels.cbegin(), summaryLabels.cend(), [](const auto *label) {
            return label->isVisible() && label->text().startsWith("To see this version of the questions");
        }));
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY(!find<QDialog>(window, "importWizard"));

        // Import the same variant again. It reports the duplicate and leaves the saved project clean.
        QVERIFY(controller.saveFile(QUrl::fromLocalFile(temp.filePath("current.buzz.json"))));
        QTest::mouseClick(open, Qt::LeftButton);
        wizard = find<QDialog>(window, "importWizard");
        QVERIFY(wizard && wizard->isVisible());
        find<QLineEdit>(*wizard, "importSourcePath")->setText(pack);
        next = find<QPushButton>(*wizard, "importNext");
        QTest::mouseClick(next, Qt::LeftButton);
        languages = find<QComboBox>(*wizard, "importLanguageCombo");
        QTRY_VERIFY(languages->isVisible());
        languages->setCurrentIndex(languages->findData("en"));
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.takeFirst(), QVariantList({0, 0, 1}));
        QVERIFY(!controller.dirty());
    }
    void importWizardFindsThePacksInAnExtractedPs3Folder() {
        Localization localization(nullptr, "en");
        StudioController controller;
        MainWindow window(controller, localization);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *start = find<QPushButton>(window, "emptyImportWizardButton");
        QVERIFY(start && start->isVisible());
        QTest::mouseClick(start, Qt::LeftButton);
        auto *wizard = find<QDialog>(window, "importWizard");
        QVERIFY(wizard && wizard->isVisible());

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkpath("PS3_GAME/USRDIR/PACK0002"));
        QFile sfo(temp.filePath("PS3_GAME/PARAM.SFO"));
        QVERIFY(sfo.open(QIODevice::WriteOnly));
        sfo.close();
        QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/native-multilingual.pak"),
                            temp.filePath("PS3_GAME/USRDIR/PACK0002/ENGPACK0002.PAK")));
        find<QLineEdit>(*wizard, "importSourcePath")->setText(temp.path());
        auto *next = find<QPushButton>(*wizard, "importNext");
        QTest::mouseClick(next, Qt::LeftButton);
        auto *languages = find<QComboBox>(*wizard, "importLanguageCombo");
        QTRY_VERIFY(languages->isVisible());
        QCOMPARE(languages->currentData().toString(), QString("en"));
        QVERIFY(!find<QComboBox>(*wizard, "importDestination")->currentData().toBool());
        QSignalSpy finished(&controller, &StudioController::importFinished);
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(controller.totalCount(), 1);
        QCOMPARE(controller.current()["question"].toString(), QString("Which letter comes first?"));
        QVERIFY(find<QPushButton>(*wizard, "importBuildPack")->isVisible());
    }
    void editingImportingAndBuildingThroughTheWindow() {
        StudioController controller;
        Localization localization;
        localization.setLanguage("pl");
        connect(&localization, &Localization::languageChanged, &controller, &StudioController::refreshTranslations);
        controller.newProject(true);
        const auto firstId = controller.current()["id"].toString();
        MainWindow window(controller, localization);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        window.openProjectLanguages();
        QDialog *dialog = nullptr;
        QTRY_VERIFY((dialog = find<QDialog>(window, "projectLanguageDialog")) && dialog->isVisible());
        QCOMPARE(find<QComboBox>(window, "projectLanguageCombo")->currentData().toString(), QString("pl"));
        dialog->close();

        // Typing, then picking another question, commits the edit first.
        auto *text = find<QPlainTextEdit>(window, "questionText");
        auto *list = find<QListView>(window, "questionList");
        QVERIFY(text && list);
        const auto secondIndex = list->model()->index(1, 0);
        const auto secondId = secondIndex.data(QuestionModel::Uid).toString();
        controller.select(secondId);
        const auto secondOriginal = controller.current()["question"].toString();
        controller.select(firstId);
        text->setFocus();
        text->selectAll();
        QTest::keyClick(text, Qt::Key_X);
        QCOMPARE(text->toPlainText(), QString("x"));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualRect(secondIndex).center());
        QTRY_COMPARE(controller.current()["id"].toString(), secondId);
        QCOMPARE(controller.current()["question"].toString(), secondOriginal);
        QCOMPARE(text->toPlainText(), secondOriginal);
        controller.select(firstId);
        QCOMPARE(controller.current()["question"].toString(), QString("x"));
        QCOMPARE(text->toPlainText(), QString("x"));
        // Saving while still typing uses the same commit path.
        text->setFocus();
        text->selectAll();
        QTest::keyClick(text, Qt::Key_Y);
        window.commitEdits();
        QCOMPARE(controller.current()["question"].toString(), QString("y"));
        QTemporaryDir temp;
        const auto path = temp.filePath("edited.buzz.json");
        QVERIFY(controller.saveFile(QUrl::fromLocalFile(path)));
        const auto result = studio::loadDocument(path);
        QVERIFY(result.ok());
        QCOMPARE(result.document.questions[0].text, QString("y"));
        QCOMPARE(result.document.questions[1].text, secondOriginal);

        // Interface language switches live and leaves the content untouched.
        const auto beforeTranslation = studio::serialize(studio::loadDocument(path).document);
        localization.setLanguage("en");
        QTRY_COMPARE(find<QPushButton>(window, "openLibraryButton")->text(), QString("Open"));
        QCOMPARE(controller.packLanguage(), QString("pl"));
        QCOMPARE(controller.current()["question"].toString(), QString("y"));
        QVERIFY(!controller.dirty());
        QVERIFY(controller.saveFile(QUrl::fromLocalFile(path)));
        QCOMPARE(studio::serialize(studio::loadDocument(path).document), beforeTranslation);
        localization.setLanguage("pl");
        QTRY_COMPARE(find<QPushButton>(window, "openLibraryButton")->text(), QString::fromUtf8("Otwórz"));
        QTRY_COMPARE(find<QPlainTextEdit>(window, "questionText")->toPlainText(), QString("y"));

        window.openTts();
        QTRY_VERIFY((dialog = find<QDialog>(window, "ttsDialog")) && dialog->isVisible());
        QCOMPARE(find<QPlainTextEdit>(window, "ttsText")->toPlainText(), QString("y"));
        dialog->close();

        // A multilingual source asks for the language to import.
        controller.prepareImport(QUrl::fromLocalFile(QFINDTESTDATA("fixtures/native-multilingual.pak")));
        QTRY_VERIFY((dialog = find<QDialog>(window, "importLanguageDialog")) && dialog->isVisible());
        auto *languages = find<QComboBox>(window, "importLanguageCombo");
        QCOMPARE(languages->currentData().toString(), QString("pl"));
        languages->setCurrentIndex(0); // Sorted detected choices: en, pl.
        QCOMPARE(languages->currentData().toString(), QString("en"));
        dialog->accept();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(controller.packLanguage(), QString("en"));
        QCOMPARE(controller.current()["question"].toString(), QString("Which letter comes first?"));

        // Build through the dialog.
        window.openBuild();
        QTRY_VERIFY((dialog = find<QDialog>(window, "buildPackDialog")) && dialog->isVisible());
        find<QLineEdit>(window, "buildTemplatePath")->setText(QFINDTESTDATA("fixtures/build-template.pak"));
        find<QLineEdit>(window, "buildOutputPath")->setText(temp.filePath("packs"));
        find<QSpinBox>(window, "buildPackNumber")->setValue(103);
        find<QSpinBox>(window, "buildCategoryBase")->setValue(700);
        QSignalSpy built(&controller, &StudioController::packFinished);
        auto *start = find<QPushButton>(window, "startPackBuild");
        QVERIFY(start && start->isEnabled());
        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(built.count(), 1, 10000);
        const auto builtPath = controller.lastBuild()["path"].toString();
        QVERIFY(QFileInfo::exists(builtPath));
        QVERIFY(builtPath.endsWith("ENGPACK0103.edat"));
        QCOMPARE(controller.current()["question"].toString(), QString("Which letter comes first?"));
        QTRY_VERIFY((dialog = find<QDialog>(window, "packResultDialog")) && dialog->isVisible());
        dialog->close();
        // The dialog fits the smallest supported window.
        window.resize(1120, 760);
        window.openBuild();
        QTRY_VERIFY((dialog = find<QDialog>(window, "buildPackDialog")) && dialog->isVisible());
        find<QWidget>(window, "buildAdvanced")->setProperty("checked", true);
        QTRY_VERIFY(dialog->height() <= window.height() - 48);
        dialog->close();
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
