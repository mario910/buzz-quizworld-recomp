#include "controller.h"
#include "localization.h"
#include "main_window.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

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
