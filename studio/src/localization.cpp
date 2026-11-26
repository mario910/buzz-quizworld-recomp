#include "localization.h"
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

static void initializeTranslations() { Q_INIT_RESOURCE(studio_translations); }
// Interface languages of the Buzz! games, in their own names. Polish is the source.
const QVector<QPair<QString, QString>> &Localization::languages() {
    static const QVector<QPair<QString, QString>> list = {
        {"pl", "Polski"},  {"en", "English"},  {"de", "Deutsch"},          {"fr", "Français"},
        {"it", "Italiano"}, {"es", "Español"}, {"nl", "Nederlands"},       {"pt", "Português"},
        {"sv", "Svenska"}, {"da", "Dansk"},    {"no", "Norsk"},            {"fi", "Suomi"},
        {"ru", "Русский"}, {"cs", "Čeština"},  {"el", "Ελληνικά"}, {"hr", "Hrvatski"}};
    return list;
}
static bool supported(const QString &language) {
    for (const auto &entry : Localization::languages())
        if (entry.first == language)
            return true;
    return false;
}
// Without a saved choice the interface follows the system language, or English.
QString Localization::defaultLanguage(const QLocale &system) {
    if (system.language() == QLocale::NorwegianBokmal || system.language() == QLocale::NorwegianNynorsk)
        return QStringLiteral("no");
    const auto code = QLocale::languageToCode(system.language());
    return supported(code) ? code : QStringLiteral("en");
}
Localization::Localization(QObject *parent, const QString &initialLanguage) : QObject(parent) {
    initializeTranslations();
    auto selected = initialLanguage.isEmpty() ? QSettings().value("interfaceLanguage").toString() : initialLanguage;
    if (!supported(selected))
        selected = defaultLanguage(QLocale::system());
    setLanguage(selected);
    persist_ = true;
}
void Localization::setLanguage(const QString &language) {
    if (!supported(language) || language == language_)
        return;
    auto *app = QCoreApplication::instance();
    app->removeTranslator(&application_);
    app->removeTranslator(&qt_);
    language_ = language;
    QLocale::setDefault(QLocale(language));
    if (language != "pl" && application_.load(":/i18n/studio_" + language + ".qm"))
        app->installTranslator(&application_);
    const QLocale locale(language);
    if (qt_.load(locale, "qt", "_", app->applicationDirPath() + "/translations") ||
        qt_.load(locale, "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app->installTranslator(&qt_);
    if (persist_) {
        QSettings settings;
        settings.setValue("interfaceLanguage", language);
        settings.sync(); // Kept even if the app does not shut down cleanly.
    }
    emit languageChanged();
}
