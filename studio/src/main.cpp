#include "controller.h"
#include "localization.h"
#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QTranslator>
#include <cstdio>

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        const QByteArray mode(argv[i]);
        if ((mode == "--build" || mode == "--install-pack") && i + 1 < argc) {
            // QImageReader's SVG plugin needs a font database, including in CLI builds.
#ifndef Q_OS_WIN
            if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("DISPLAY") &&
                qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
                qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
            QGuiApplication app(argc, argv);
            app.setOrganizationName("BuzzRecomp");
            app.setApplicationName("QuestionStudio");
            const auto args = app.arguments();
            auto option = [&](const QString &key, const QString &fallback = QString()) {
                const int at = args.indexOf(key);
                return at >= 0 && at + 1 < args.size() ? args[at + 1] : fallback;
            };
            studio::BuildResult result;
            if (mode == "--install-pack")
                result =
                    studio::installPack(args[i + 1], option("--runtime"), option("--title-id", "BCES00647"));
            else {
                const auto loaded = studio::loadDocument(args[i + 1], option("--language"));
                if (!loaded.ok())
                    result.error = loaded.error;
                else {
                    studio::BuildRequest request;
                    request.document = loaded.document;
                    request.templatePath = option("--template");
                    request.outputDirectory = option("--out");
                    request.ffmpeg = option("--ffmpeg", studio::defaultFfmpeg());
                    request.number =
                        option("--pack", QString::number(loaded.document.packExtra["number"].toInt(103)))
                            .toInt();
                    request.categoryBase =
                        option("--category-base",
                               QString::number(loaded.document.packExtra["category_base"].toInt(
                                   500 + (request.number - 101) * 100)))
                            .toInt();
                    request.runtimeRoot = option("--runtime");
                    request.titleId = option("--title-id", "BCES00647");
                    request.install = args.contains("--install");
                    result = studio::buildPack(request);
                }
            }
            auto map = result.map();
            map["ok"] = result.ok();
            const auto bytes = QJsonDocument::fromVariant(map).toJson();
            std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
            return result.ok() ? 0 : 1;
        }
    }
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--inspect" && i + 1 < argc) {
            QCoreApplication app(argc, argv);
            app.setOrganizationName("BuzzRecomp");
            app.setApplicationName("QuestionStudio");
            const auto arguments = app.arguments();
            const int selected = arguments.indexOf("--language");
            const auto language =
                selected >= 0 && selected + 1 < arguments.size() ? arguments[selected + 1] : QString();
            const auto result = studio::loadDocument(arguments[i + 1], language);
            const auto bytes =
                QJsonDocument(
                    QJsonObject{{"ok", result.ok()},
                                {"questions", result.document.questions.size()},
                                {"language", result.document.language},
                                {"languages", QJsonArray::fromStringList(result.document.languages())},
                                {"error", result.error},
                                {"warnings", QJsonArray::fromStringList(result.warnings)}})
                    .toJson();
            std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
            return result.ok() ? 0 : 1;
        }
    QApplication app(argc, argv);
    app.setOrganizationName("BuzzRecomp");
    app.setApplicationName("QuestionStudio");
    app.setApplicationDisplayName("Buzz! Question Studio");
    app.setApplicationVersion(QStringLiteral(STUDIO_VERSION));
    const auto args = app.arguments();
    const int uiLanguage = args.indexOf("--ui-language");
    Localization localization(nullptr, uiLanguage >= 0 && uiLanguage + 1 < args.size() ? args[uiLanguage + 1]
                                                                                       : QString());
#ifdef Q_OS_WIN
    QFont interfaceFont(QStringLiteral("Segoe UI"));
#else
    QFont interfaceFont(QStringLiteral("Sans Serif"));
#endif
    interfaceFont.setPixelSize(13);
    app.setFont(interfaceFont);
    app.setWindowIcon(QIcon(":/studio.svg"));
    QQuickStyle::setStyle("Basic");
    StudioController studio;
    if (args.contains("--demo") || args.contains("--smoke"))
        studio.newProject(true);
    const int open = args.indexOf("--open");
    if (open >= 0 && open + 1 < args.size())
        studio.openFile(QUrl::fromLocalFile(args[open + 1]));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("i18n", &localization);
    engine.rootContext()->setContextProperty("studio", &studio);
    QObject::connect(&localization, &Localization::languageChanged, &engine, &QQmlEngine::retranslate);
    QObject::connect(&localization, &Localization::languageChanged, &studio,
                     &StudioController::refreshTranslations);
    bool warnings = false;
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError> &errors) {
        for (const auto &e : errors)
            qWarning().noquote() << e.toString();
        warnings = true;
    });
    engine.load(QUrl("qrc:/Buzz/Studio/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (args.contains("--preview"))
        engine.rootObjects().first()->setProperty("editorTab", 1);
    if (args.contains("--tts"))
        if (auto *dialog = engine.rootObjects().first()->findChild<QObject *>("ttsDialog"))
            QMetaObject::invokeMethod(dialog, "open");
    if (args.contains("--show-languages"))
        if (auto *dialog = engine.rootObjects().first()->findChild<QObject *>("projectLanguageDialog"))
            QMetaObject::invokeMethod(dialog, "open");
    if (args.contains("--show-build"))
        if (auto *dialog = engine.rootObjects().first()->findChild<QObject *>("packDialogs"))
            QMetaObject::invokeMethod(dialog, "openBuild");
    const int shot = args.indexOf("--screenshot");
    if (args.contains("--smoke") || (shot >= 0 && shot + 1 < args.size()))
        QTimer::singleShot(1200, &app, [&] {
            bool saved = true;
            if (shot >= 0) {
                auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
                saved = window && !window->grabWindow().isNull() && window->grabWindow().save(args[shot + 1]);
            }
            app.exit(warnings || !saved ? 1 : 0);
        });
    return app.exec();
}
