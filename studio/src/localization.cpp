#include "localization.h"
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

static void initializeTranslations() { Q_INIT_RESOURCE(studio_translations); }
Localization::Localization(QObject *parent, const QString &initialLanguage) : QObject(parent) {
    initializeTranslations();
    auto selected =
        initialLanguage.isEmpty() ? QSettings().value("interfaceLanguage", "pl").toString() : initialLanguage;
    if (selected != "pl" && selected != "en")
        selected = "pl";
    setLanguage(selected);
    persist_ = true;
}
void Localization::setLanguage(const QString &language) {
    if ((language != "pl" && language != "en") || language == language_)
        return;
    auto *app = QCoreApplication::instance();
    app->removeTranslator(&application_);
    app->removeTranslator(&qt_);
    language_ = language;
    QLocale::setDefault(QLocale(language));
    if (language == "en" && application_.load(":/i18n/studio_en.qm"))
        app->installTranslator(&application_);
    const QLocale locale(language);
    if (qt_.load(locale, "qt", "_", app->applicationDirPath() + "/translations") ||
        qt_.load(locale, "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app->installTranslator(&qt_);
    if (persist_)
        QSettings().setValue("interfaceLanguage", language);
    emit languageChanged();
}
