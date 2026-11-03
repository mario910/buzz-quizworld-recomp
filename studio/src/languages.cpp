#include "languages.h"
#include "buzz_import.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>

namespace studio {
QString languageCode(const QString &value) {
    static const QMap<QString, QString> aliases = {
        {"pol", "pl"}, {"eng", "en"}, {"fre", "fr"}, {"fra", "fr"}, {"ger", "de"}, {"deu", "de"},
        {"spa", "es"}, {"ita", "it"}, {"dut", "nl"}, {"nld", "nl"}, {"por", "pt"}, {"bra", "pt-BR"},
        {"rus", "ru"}, {"swe", "sv"}, {"dan", "da"}, {"nor", "no"}, {"fin", "fi"}, {"gre", "el"},
        {"grc", "el"}, {"cro", "hr"}, {"ell", "el"}, {"cze", "cs"}, {"ces", "cs"}, {"hun", "hu"},
        {"tur", "tr"}, {"jpn", "ja"}, {"chi", "zh"}, {"zho", "zh"}, {"kor", "ko"}, {"usa", "en-US"}};
    auto code = value.trimmed().replace('_', '-').toLower();
    if (aliases.contains(code))
        return aliases[code];
    static const QRegularExpression valid("^[a-z]{2,3}(?:-[a-z0-9]{2,8})*$");
    if (!valid.match(code).hasMatch())
        return {};
    auto parts = code.split('-');
    for (int i = 1; i < parts.size(); ++i) {
        if (parts[i].size() == 2)
            parts[i] = parts[i].toUpper();
        else if (parts[i].size() == 4)
            parts[i][0] = parts[i][0].toUpper();
    }
    return parts.join('-');
}
QString languageName(const QString &code) {
    const QLocale locale(code);
    const auto name = locale.nativeLanguageName();
    return name.isEmpty() || locale.language() == QLocale::C ? code : name + " (" + code + ")";
}
QString preferredLanguage(const QStringList &available) {
    for (const auto &code : {QString("pl"), QString("en")})
        if (available.contains(code))
            return code;
    return available.value(0);
}
QVariantList languageChoices(const QStringList &codes) {
    const auto list = codes.isEmpty()
                          ? QStringList{"pl", "en", "de", "fr", "es", "it", "pt", "pt-BR", "nl", "sv", "da",
                                        "no", "fi", "cs", "hu", "el", "ru", "uk", "tr",    "ja", "zh", "ko"}
                          : codes;
    QVariantList result;
    for (const auto &code : list)
        result << QVariantMap{{"code", code}, {"name", languageName(code)}};
    return result;
}
SourceLanguages inspectLanguages(const QString &path) {
    if (path.endsWith(".pak", Qt::CaseInsensitive) || path.endsWith(".edat", Qt::CaseInsensitive))
        return buzzLanguages(path);
    SourceLanguages result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    if (file.size() > 128 * 1024 * 1024) {
        result.error = QCoreApplication::translate("Languages", "Plik JSON przekracza 128 MB.");
        return result;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || (!doc.isArray() && !doc.isObject())) {
        result.error = QCoreApplication::translate("Languages", "Niepoprawny JSON.");
        return result;
    }
    const auto root = doc.object();
    result.project = root["format"] == "buzz-question-studio";
    const auto packLanguage = languageCode(root["pack"].toObject()["language"].toString());
    QSet<QString> codes;
    if (!packLanguage.isEmpty())
        codes.insert(packLanguage);
    const auto questions = doc.isArray() ? doc.array() : root["questions"].toArray();
    for (const auto &v : questions) {
        const auto q = v.toObject();
        // Keep the object alive while traversing its iterators.
        for (const auto &key : {"langs", "translations", "source_languages"}) {
            const auto variants = q[key].toObject();
            for (auto it = variants.constBegin(); it != variants.constEnd(); ++it) {
                const auto code = languageCode(it.key());
                if (!code.isEmpty())
                    codes.insert(code);
            }
        }
    }
    result.codes = codes.values();
    result.codes.sort();
    if (result.codes.isEmpty())
        result.codes << "pl"; // Compatibility with existing single-language buzzpack JSON.
    result.preferred = packLanguage.isEmpty() ? preferredLanguage(result.codes) : packLanguage;
    return result;
}
} // namespace studio
