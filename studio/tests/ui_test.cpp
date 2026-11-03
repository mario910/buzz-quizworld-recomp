#include "controller.h"
#include "localization.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>

class UiTest final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("BuzzStudioTests");
        QCoreApplication::setApplicationName("UiTests");
        QStandardPaths::setTestModeEnabled(true);
        QQuickStyle::setStyle("Basic");
    }
    void switchingQuestionsCommitsTextAndSavePreservesIt() {
        StudioController controller;
        Localization localization;
        localization.setLanguage("pl");
        controller.newProject(true);
        const auto firstId = controller.current()["id"].toString();
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.rootContext()->setContextProperty("studio", &controller);
        engine.rootContext()->setContextProperty("i18n", &localization);
        connect(&localization, &Localization::languageChanged, &engine, &QQmlEngine::retranslate);
        connect(&localization, &Localization::languageChanged, &controller,
                &StudioController::refreshTranslations);
        engine.load(QUrl("qrc:/Buzz/TestStudio/Main.qml"));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *initialLanguage = window->findChild<QObject *>("projectLanguageCombo");
        QVERIFY(initialLanguage);
        QCOMPARE(initialLanguage->property("currentValue").toString(), QString("pl"));
        auto *text = window->findChild<QQuickItem *>("questionText");
        QVERIFY(text);
        // ListView delegates belong to the visual tree, not necessarily the
        // window's QObject ownership tree.
        std::function<QQuickItem *(QQuickItem *, const QString &)> findVisual =
            [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (auto *child : item->childItems())
                if (auto *found = findVisual(child, name))
                    return found;
            return nullptr;
        };
        QQuickItem *secondRow = nullptr;
        QTRY_VERIFY((secondRow = findVisual(window->contentItem(), "questionRow_1")));
        const auto secondId = secondRow->property("uid").toString();
        controller.select(secondId);
        const auto secondOriginal = controller.current()["question"].toString();
        controller.select(firstId);

        text->forceActiveFocus();
        QTRY_VERIFY(text->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_X);
        QCOMPARE(text->property("text").toString(), QString("x"));
        const auto click = secondRow->mapToScene(QPointF(secondRow->width() / 2, secondRow->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, click.toPoint());
        QTRY_COMPARE(controller.current()["id"].toString(), secondId);
        QCOMPARE(controller.current()["question"].toString(), secondOriginal);
        QCOMPARE(text->property("text").toString(), secondOriginal);
        controller.select(firstId);
        QCOMPARE(controller.current()["question"].toString(), QString("x"));
        QCOMPARE(text->property("text").toString(), QString("x"));
        // The same commit path is used by the Save command while still typing.
        text->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Y);
        QVERIFY(QMetaObject::invokeMethod(window, "commitEdits"));
        QVERIFY(!text->hasActiveFocus());
        QCOMPARE(controller.current()["question"].toString(), QString("y"));
        QTemporaryDir temp;
        const auto path = temp.filePath("edited.buzz.json");
        QVERIFY(controller.saveFile(QUrl::fromLocalFile(path)));
        const auto result = studio::loadDocument(path);
        QVERIFY(result.ok());
        QCOMPARE(result.document.questions[0].text, QString("y"));
        QCOMPARE(result.document.questions[1].text, secondOriginal);
        auto *openButton = window->findChild<QObject *>("openLibraryButton");
        QVERIFY(openButton);
        const auto beforeTranslation = studio::serialize(studio::loadDocument(path).document);
        localization.setLanguage("en");
        QTRY_COMPARE(openButton->property("text").toString(), QString("Open"));
        QCOMPARE(controller.packLanguage(), QString("pl"));
        QCOMPARE(controller.current()["question"].toString(), QString("y"));
        QVERIFY(!controller.dirty());
        QVERIFY(controller.saveFile(QUrl::fromLocalFile(path)));
        QCOMPARE(studio::serialize(studio::loadDocument(path).document), beforeTranslation);
        localization.setLanguage("pl");
        QTRY_COMPARE(openButton->property("text").toString(), QString::fromUtf8("Otwórz"));
        auto *tts = window->findChild<QObject *>("ttsDialog");
        QVERIFY(tts);
        QVERIFY(QMetaObject::invokeMethod(tts, "open"));
        QTRY_VERIFY(tts->property("visible").toBool());
        auto *ttsText = window->findChild<QObject *>("ttsText");
        QVERIFY(ttsText);
        QTRY_COMPARE(ttsText->property("text").toString(), QString("y"));
        QVERIFY(QMetaObject::invokeMethod(tts, "close"));
        const auto importPath = QFINDTESTDATA("fixtures/native-multilingual.pak");
        controller.prepareImport(QUrl::fromLocalFile(importPath));
        auto *languageDialog = window->findChild<QObject *>("importLanguageDialog");
        auto *languageCombo = window->findChild<QObject *>("importLanguageCombo");
        QVERIFY(languageDialog);
        QVERIFY(languageCombo);
        QTRY_VERIFY(languageDialog->property("visible").toBool());
        QCOMPARE(languageCombo->property("currentValue").toString(), QString("pl"));
        languageCombo->setProperty("currentIndex", 0); // Sorted detected choices: en, pl.
        QCOMPARE(languageCombo->property("currentValue").toString(), QString("en"));
        QVERIFY(QMetaObject::invokeMethod(languageDialog, "accept"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(controller.packLanguage(), QString("en"));
        QCOMPARE(controller.current()["question"].toString(), QString("Which letter comes first?"));
        auto *projectDialog = window->findChild<QObject *>("projectLanguageDialog");
        QVERIFY(projectDialog);
        QVERIFY(QMetaObject::invokeMethod(projectDialog, "open"));
        QTRY_VERIFY(projectDialog->property("visible").toBool());
        auto *projectCombo = window->findChild<QObject *>("projectLanguageCombo");
        QVERIFY(projectCombo);
        QCOMPARE(projectCombo->property("currentValue").toString(), QString("en"));
        QVERIFY(QMetaObject::invokeMethod(projectDialog, "close"));
        auto *panels = window->findChild<QObject *>("packDialogs");
        QVERIFY(panels);
        QVERIFY(QMetaObject::invokeMethod(panels, "openBuild"));
        auto *buildDialog = window->findChild<QObject *>("buildPackDialog");
        QVERIFY(buildDialog);
        QTRY_VERIFY(buildDialog->property("visible").toBool());
        auto *templatePath = window->findChild<QObject *>("buildTemplatePath");
        auto *outputPath = window->findChild<QObject *>("buildOutputPath");
        auto *categoryBase = window->findChild<QObject *>("buildCategoryBase");
        auto *number = window->findChild<QObject *>("buildPackNumber");
        QVERIFY(templatePath && outputPath && categoryBase && number);
        QVERIFY(templatePath->setProperty("value", QFINDTESTDATA("fixtures/build-template.pak")));
        QVERIFY(outputPath->setProperty("value", temp.filePath("packs")));
        categoryBase->setProperty("value", 700);
        number->setProperty("value", 103);
        auto *buildButton = window->findChild<QQuickItem *>("startPackBuild");
        QVERIFY(buildButton);
        QSignalSpy built(&controller, &StudioController::packFinished);
        QTRY_VERIFY(buildButton->width() > 0);
        const auto buildClick =
            buildButton->mapToScene(QPointF(buildButton->width() / 2, buildButton->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, buildClick.toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(built.count(), 1, 10000);
        const auto builtPath = controller.lastBuild()["path"].toString();
        QVERIFY(QFileInfo::exists(builtPath));
        QVERIFY(builtPath.endsWith("ENGPACK0103.edat"));
        QCOMPARE(controller.current()["question"].toString(), QString("Which letter comes first?"));
        auto *resultDialog = window->findChild<QObject *>("packResultDialog");
        QVERIFY(resultDialog);
        QTRY_VERIFY(resultDialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(resultDialog, "close"));
        window->resize(1120, 760);
        QVERIFY(QMetaObject::invokeMethod(panels, "openBuild"));
        auto *advanced = window->findChild<QObject *>("buildAdvanced");
        QVERIFY(advanced);
        advanced->setProperty("checked", true);
        QTRY_VERIFY(buildDialog->property("height").toReal() <= window->height() - 48);
        QVERIFY(QMetaObject::invokeMethod(buildDialog, "close"));
        QCOMPARE(warnings.count(), 0);
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
