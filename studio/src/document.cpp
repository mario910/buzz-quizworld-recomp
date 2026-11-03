#include "document.h"
#include "buzz_import.h"
#include "languages.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <stdexcept>

namespace studio {
QString categoryName(int id) {
    static const QMap<int, QString> names = {{174, "Sport"},
                                             {175, "Muzyka"},
                                             {177, "Sztuka i literatura"},
                                             {178, "Sztuka i artyści"},
                                             {179, "Książki i pisarze"},
                                             {181, "Teatr"},
                                             {182, "Historia i wydarzenia"},
                                             {183, "Aktualności"},
                                             {184, "Historia"},
                                             {187, "Trendy"},
                                             {188, "Czas wolny"},
                                             {189, "Dom"},
                                             {191, "Zainteresowania"},
                                             {196, "Gry i zabawy"},
                                             {199, "Film"},
                                             {208, "Kino"},
                                             {230, "Ludzie"},
                                             {232, "Społeczeństwo"},
                                             {235, "Nauka i przyroda"},
                                             {236, "Internet i technologia"},
                                             {237, "Nauka"},
                                             {238, "Człowiek"},
                                             {239, "Fizyka i chemia"},
                                             {240, "Przyroda"},
                                             {247, "Dyscypliny sportowe"},
                                             {250, "Świat"},
                                             {251, "Wierzenia"},
                                             {253, "Geografia"},
                                             {257, "Telewizja"},
                                             {271, "Programy TV"},
                                             {224, "Muzyka popularna"}};
    return names.value(id, id ? QStringLiteral("Kategoria %1").arg(id) : QStringLiteral("Bez kategorii"));
}
QString localPath(const QString &value, const QString &baseDir) {
    if (value.isEmpty())
        return {};
    const QUrl url(value);
    QString path = url.isLocalFile() ? url.toLocalFile() : value;
    if (QFileInfo(path).isRelative() && !baseDir.isEmpty())
        path = QDir(baseDir).absoluteFilePath(path);
    return QDir::cleanPath(path);
}
static QString persistedPath(const QString &path, const QString &baseDir) {
    if (path.isEmpty() || baseDir.isEmpty())
        return path;
    return QDir::fromNativeSeparators(QDir(baseDir).relativeFilePath(path));
}
Question newQuestion() {
    Question q;
    q.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    q.answers = QStringList{"", "", "", ""};
    q.category = QStringLiteral("Bez kategorii");
    q.source = QStringLiteral("Własne");
    return q;
}
QStringList Question::problems() const {
    QStringList out;
    if (text.trimmed().isEmpty())
        out << QCoreApplication::translate("Document", "Wpisz treść pytania.");
    if (answers.size() != 4 ||
        std::any_of(answers.begin(), answers.end(), [](const QString &a) { return a.trimmed().isEmpty(); }))
        out << QCoreApplication::translate("Document", "Uzupełnij cztery odpowiedzi.");
    if (correct < 0 || correct >= answers.size())
        out << QCoreApplication::translate("Document", "Wskaż poprawną odpowiedź.");
    QSet<QString> seen;
    for (const auto &a : answers) {
        const auto key = a.simplified().toCaseFolded();
        if (!key.isEmpty() && seen.contains(key)) {
            out << QCoreApplication::translate("Document", "Odpowiedzi nie mogą się powtarzać.");
            break;
        }
        seen.insert(key);
    }
    if (!image.isEmpty() && !QFileInfo::exists(image))
        out << QCoreApplication::translate("Document", "Nie znaleziono obrazka.");
    if (!speech.isEmpty() && !QFileInfo::exists(speech))
        out << QCoreApplication::translate("Document", "Nie znaleziono nagrania.");
    return out;
}
QJsonObject Question::localized(const QString &baseDir) const {
    auto o = translations[language].toObject();
    o["question"] = text;
    o["answers"] = QJsonArray::fromStringList(answers);
    o["correct_index"] = correct;
    o.remove("correct");
    o["topic"] = topic;
    o["image"] = persistedPath(image, baseDir);
    o["speech"] = persistedPath(speech, baseDir);
    o["tts_text"] = ttsText;
    return o;
}
QStringList Question::languages() const {
    auto codes = translations.keys();
    if (!codes.contains(language))
        codes << language;
    codes.sort();
    return codes;
}
void Question::switchLanguage(const QString &code) {
    if (language == code)
        return;
    translations[language] = localized();
    const auto next = translations[code].toObject();
    language = code;
    loadLocalized(next);
}
void Question::loadLocalized(const QJsonObject &next) {
    text = next["question"].toString();
    answers.clear();
    const auto nextAnswers = next["answers"].toArray();
    for (int i = 0; i < 4; ++i)
        answers << (i < nextAnswers.size() ? nextAnswers[i].toString() : QString());
    correct = next["correct_index"].toInt();
    topic = next["topic"].toString();
    image = next["image"].toString();
    speech = next["speech"].toString();
    ttsText = next["tts_text"].toString();
}
QJsonObject Question::json(const QString &baseDir) const {
    QJsonObject o = extra;
    if (!translations.isEmpty())
        o.remove("source_media");
    const auto active = localized(baseDir);
    for (auto it = active.constBegin(); it != active.constEnd(); ++it)
        o[it.key()] = it.value();
    auto variants = translations;
    for (auto it = variants.begin(); it != variants.end(); ++it) {
        auto v = it.value().toObject();
        v["image"] = persistedPath(v["image"].toString(), baseDir);
        v["speech"] = persistedPath(v["speech"].toString(), baseDir);
        it.value() = v;
    }
    variants[language] = active;
    o["translations"] = variants;
    o["language"] = language;
    o["id"] = id;
    o["difficulty"] = difficulty;
    o["category_name"] = category;
    o["subject"] = subjectId;
    o["category"] = categoryId;
    o["source"] = source;
    return o;
}
QStringList Document::languages() const {
    QSet<QString> codes{language};
    for (const auto &code : languageCodes)
        codes.insert(code);
    for (const auto &q : questions)
        for (const auto &code : q.languages())
            codes.insert(code);
    auto list = codes.values();
    list.sort();
    return list;
}
void Document::switchLanguage(const QString &code) {
    if (!languageCodes.contains(language))
        languageCodes << language;
    if (!languageCodes.contains(code))
        languageCodes << code;
    for (auto &q : questions)
        q.switchLanguage(code);
    language = code;
}
static QJsonObject readLocalized(QJsonObject o, const QString &baseDir) {
    if (!o["question"].isString() || !o["answers"].isArray() || o["answers"].toArray().size() != 4)
        throw std::runtime_error(
            QCoreApplication::translate("Document",
                                        "Wymagana treść i cztery odpowiedzi w każdej wersji językowej.")
                .toStdString());
    for (const auto &answer : o["answers"].toArray())
        if (!answer.isString())
            throw std::runtime_error(
                QCoreApplication::translate("Document", "Odpowiedź nie jest tekstem.").toStdString());
    const auto correct = o.contains("correct_index") ? o["correct_index"]
                         : o.contains("correct")     ? o["correct"]
                                                     : QJsonValue(0);
    if (!correct.isDouble() || correct.toDouble() != correct.toInt() || correct.toInt() < 0 ||
        correct.toInt() > 3)
        throw std::runtime_error(
            QCoreApplication::translate("Document", "Poprawna odpowiedź musi mieć indeks 0–3.")
                .toStdString());
    o["correct_index"] = correct;
    o.remove("correct");
    o["image"] = localPath(o["image"].toString(), baseDir);
    o["speech"] = localPath(o["speech"].toString(), baseDir);
    return o;
}
static QString idString(const QJsonValue &v) {
    return v.isString() ? v.toString() : (v.isDouble() ? QString::number(v.toDouble(), 'f', 0) : QString());
}
static void addQuestion(LoadResult &out, Question q, QSet<QString> &ids, int row) {
    if (q.id.isEmpty())
        q.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (ids.contains(q.id))
        throw std::runtime_error(
            QCoreApplication::translate("Document", "Pytanie %1: powtórzony identyfikator %2.")
                .arg(row)
                .arg(q.id)
                .toStdString());
    ids.insert(q.id);
    out.document.questions.append(std::move(q));
}
LoadResult parseJson(const QByteArray &bytes, const QString &baseDir, const QString &requestedLanguage) {
    LoadResult out;
    try {
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError)
            throw std::runtime_error(
                QCoreApplication::translate("Document", "Niepoprawny JSON: %1 (bajt %2).")
                    .arg(parseError.errorString())
                    .arg(parseError.offset)
                    .toStdString());
        if (!doc.isObject() && !doc.isArray())
            throw std::runtime_error(
                QCoreApplication::translate("Document", "Oczekiwano projektu lub tablicy pytań.")
                    .toStdString());
        const auto root = doc.object();
        if (root.contains("format") && root["format"].toString() != "buzz-question-studio")
            throw std::runtime_error(
                QCoreApplication::translate("Document", "Nieznany format projektu.").toStdString());
        if (root.contains("format") && root["version"].toInt() != 1 && root["version"].toInt() != 2)
            throw std::runtime_error(
                QCoreApplication::translate("Document",
                                            "Ta wersja projektu nie jest obsługiwana. Zaktualizuj aplikację.")
                    .toStdString());
        if (!doc.isArray() && !root["questions"].isArray())
            throw std::runtime_error(
                QCoreApplication::translate("Document", "Brak tablicy questions.").toStdString());
        out.document.rootExtra = root;
        out.document.rootExtra.remove("questions");
        const auto pack = root["pack"].toObject();
        out.document.packExtra = pack;
        out.document.title = pack["title"].toString(QStringLiteral("Zaimportowana paczka"));
        out.document.description = pack["description"].toString();
        out.document.language = languageCode(pack["language"].toString("pl"));
        if (out.document.language.isEmpty())
            throw std::runtime_error(
                QCoreApplication::translate("Document", "Niepoprawny kod języka paczki.").toStdString());
        for (const auto &v : pack["languages"].toArray()) {
            const auto code = languageCode(v.toString());
            if (code.isEmpty())
                throw std::runtime_error(
                    QCoreApplication::translate("Document", "Niepoprawny kod języka paczki.").toStdString());
            if (!out.document.languageCodes.contains(code))
                out.document.languageCodes << code;
        }
        const auto list = doc.isArray() ? doc.array() : root["questions"].toArray();
        if (list.size() > 100000)
            throw std::runtime_error(
                QCoreApplication::translate("Document", "Projekt przekracza limit 100 000 pytań.")
                    .toStdString());
        QSet<QString> sourceCodes;
        for (const auto &v : list) {
            const auto o = v.toObject();
            const auto langs = o["langs"].toObject();
            for (auto it = langs.constBegin(); it != langs.constEnd(); ++it)
                sourceCodes.insert(languageCode(it.key()));
        }
        sourceCodes.remove(QString());
        QString targetLanguage =
            requestedLanguage.isEmpty() ? out.document.language : languageCode(requestedLanguage);
        if (targetLanguage.isEmpty())
            throw std::runtime_error(
                QCoreApplication::translate("Document", "Niepoprawny kod języka importu.").toStdString());
        if (!root.contains("format") && !sourceCodes.isEmpty() && requestedLanguage.isEmpty())
            targetLanguage = preferredLanguage(sourceCodes.values());
        QSet<QString> ids, availableCodes;
        availableCodes.unite(sourceCodes);
        if (root.contains("format") || sourceCodes.isEmpty())
            availableCodes.insert(out.document.language);
        for (const auto &code : out.document.languageCodes)
            availableCodes.insert(code);
        int missing = 0, skipped = 0;
        for (int i = 0; i < list.size(); ++i) {
            if (!list[i].isObject())
                throw std::runtime_error(
                    QCoreApplication::translate("Document", "Pytanie %1 nie jest obiektem.")
                        .arg(i + 1)
                        .toStdString());
            const auto o = list[i].toObject();
            if (!root.contains("format") && o["langs"].isObject()) {
                const auto variants = o["langs"].toObject();
                bool present = false;
                for (auto it = variants.constBegin(); it != variants.constEnd(); ++it)
                    present |= languageCode(it.key()) == targetLanguage;
                if (!present) {
                    ++skipped;
                    continue;
                }
            }
            Question q = newQuestion();
            q.extra = o;
            q.language = languageCode(o["language"].toString(out.document.language));
            if (q.language.isEmpty())
                throw std::runtime_error(
                    QCoreApplication::translate("Document", "Niepoprawny kod języka pytania.").toStdString());
            q.id = idString(o["id"]);
            q.subjectId = o["subject"].toInt();
            q.categoryId = o["category"].toInt();
            q.category = o["category_name"].toString(
                o["category"].isString() ? o["category"].toString()
                                         : categoryName(q.subjectId ? q.subjectId : q.categoryId));
            q.difficulty = o["difficulty"].toInt(2);
            if (q.difficulty < 1 || q.difficulty > 3)
                throw std::runtime_error(
                    QCoreApplication::translate("Document", "Pytanie %1: trudność musi wynosić 1–3.")
                        .arg(i + 1)
                        .toStdString());
            auto t = o;
            if (o.contains("translations") && !o["translations"].isObject())
                throw std::runtime_error(
                    QCoreApplication::translate("Document", "Niepoprawny lub powtórzony język tłumaczenia.")
                        .toStdString());
            auto variants = o["translations"].toObject();
            if (variants.isEmpty() && o["source_languages"].isObject())
                variants = o["source_languages"].toObject();
            if (!root.contains("format") && o["langs"].isObject()) {
                const auto langs = o["langs"].toObject();
                variants = langs;
                const auto firstKey = preferredLanguage(langs.keys());
                q.language = languageCode(firstKey);
                // KiP pictures are shared; voice references remain per-language metadata.
                for (auto it = variants.begin(); it != variants.end(); ++it) {
                    auto variant = it.value().toObject();
                    if (!variant.contains("image"))
                        variant["image"] = o["image"];
                    it.value() = variant;
                }
                t = variants[firstKey].toObject();
                q.extra["import_language"] = targetLanguage;
                q.source = "Knowledge Is Power";
                q.extra.remove("langs");
                const auto path = o["category"].toArray();
                QStringList names;
                for (const auto &v : path)
                    if (!v.toString().isEmpty())
                        names << v.toString();
                q.category = names.isEmpty() ? QStringLiteral("Bez kategorii") : names.last();
                q.extra["source_category_path"] = QJsonArray::fromStringList(names);
                q.difficulty = o["difficulty"].isString() ? (o["difficulty"] == "EASY"     ? 1
                                                             : o["difficulty"] == "EXPERT" ? 3
                                                                                           : 2)
                                                          : q.difficulty;
            } else
                q.source = o["source"].toString(QStringLiteral("JSON"));
            // Migrate Studio 0.1 Buzz identities to a game/version namespace.
            // Language imports may merge only the same source question.
            if (q.id.startsWith("buzz:") && q.id.count(':') == 2) {
                if (q.source == "Buzz! Quiz TV")
                    q.id = "buzz:1002:" + q.id.mid(5);
                else if (q.source == "Buzz! Quiz World")
                    q.id = "buzz:1003:" + q.id.mid(5);
            }
            q.extra.remove("translations");
            q.extra.remove("source_languages");
            for (auto it = variants.constBegin(); it != variants.constEnd(); ++it) {
                const auto code = languageCode(it.key());
                if (code.isEmpty() || q.translations.contains(code))
                    throw std::runtime_error(QCoreApplication::translate(
                                                 "Document", "Niepoprawny lub powtórzony język tłumaczenia.")
                                                 .toStdString());
                q.translations[code] = readLocalized(it.value().toObject(), baseDir);
                availableCodes.insert(code);
            }
            t = readLocalized(t, baseDir);
            q.text = t["question"].toString();
            q.answers.clear();
            for (const auto &answer : t["answers"].toArray()) {
                if (!answer.isString())
                    throw std::runtime_error(
                        QCoreApplication::translate("Document", "Pytanie %1: odpowiedź nie jest tekstem.")
                            .arg(i + 1)
                            .toStdString());
                q.answers << answer.toString();
            }
            q.correct = t["correct_index"].toInt();
            q.topic = t["topic"].toString();
            q.ttsText = t["tts_text"].toString();
            q.image = t["image"].toString();
            q.speech = t["speech"].toString();
            if (root["version"].toInt(1) <= 1 && q.extra["source_media"].isArray() &&
                !q.translations[q.language].toObject().contains("source_media")) {
                auto localized = q.translations[q.language].toObject();
                localized["source_media"] = q.extra["source_media"];
                q.translations[q.language] = localized;
            }
            availableCodes.insert(q.language);
            if (q.language != targetLanguage && !q.translations.contains(targetLanguage)) {
                if (!root.contains("format")) {
                    ++skipped;
                    continue;
                }
                ++missing;
            }
            q.switchLanguage(targetLanguage);
            addQuestion(out, std::move(q), ids, i + 1);
        }
        if (!requestedLanguage.isEmpty() && !availableCodes.contains(targetLanguage))
            throw std::runtime_error(
                QCoreApplication::translate("Document", "Źródło nie zawiera wybranego języka.")
                    .toStdString());
        out.document.language = targetLanguage;
        if (skipped)
            out.warnings << QCoreApplication::translate("Document", "Pominięto %1 pytań bez wersji %2.")
                                .arg(skipped)
                                .arg(targetLanguage);
        if (missing)
            out.warnings << QCoreApplication::translate(
                                "Document",
                                "%1 pytań nie ma wersji %2. Utworzono puste szkice; inne języki zachowano.")
                                .arg(missing)
                                .arg(targetLanguage);
    } catch (const std::exception &e) {
        out.error = QString::fromUtf8(e.what());
        out.document.questions.clear();
    }
    return out;
}
LoadResult loadDocument(const QString &path, const QString &language) {
    if (path.endsWith(".pak", Qt::CaseInsensitive) || path.endsWith(".edat", Qt::CaseInsensitive))
        return importBuzz(path, language);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        LoadResult r;
        r.error = f.errorString();
        return r;
    }
    if (f.size() > 128 * 1024 * 1024) {
        LoadResult r;
        r.error = QCoreApplication::translate("Document", "Plik JSON przekracza 128 MB.");
        return r;
    }
    return parseJson(f.readAll(), QFileInfo(path).absolutePath(), language);
}
QByteArray serialize(const Document &doc, const QString &baseDir, bool legacy) {
    auto root = doc.rootExtra;
    auto pack = doc.packExtra;
    pack["title"] = doc.title;
    pack["description"] = doc.description;
    pack["language"] = doc.language;
    pack["languages"] = QJsonArray::fromStringList(legacy ? QStringList{doc.language} : doc.languages());
    root["pack"] = pack;
    QJsonArray questions;
    for (const auto &q : doc.questions) {
        auto o = q.json(baseDir);
        if (legacy) {
            const auto media =
                q.translations.isEmpty() ? q.extra["source_media"] : q.localized()["source_media"];
            if (!media.toArray().isEmpty() && q.image.isEmpty())
                throw std::runtime_error(
                    QCoreApplication::translate(
                        "Document",
                        "Paczka zawiera media SDI, które nie zostały jeszcze przekonwertowane. "
                        "Eksport JSON dla buzzpack pominąłby te media; zapisz projekt lub przypisz obrazki.")
                        .toStdString());
            if (q.subjectId == 0 || q.categoryId == 0 || !q.problems().isEmpty())
                throw std::runtime_error(
                    QCoreApplication::translate("Document",
                                                "Eksport do buzzpack wymaga poprawnych pytań i numerycznego "
                                                "mapowania kategorii Quiz World.")
                        .toStdString());
            QStringList ordered;
            ordered << q.answers[q.correct];
            for (int i = 0; i < 4; ++i)
                if (i != q.correct)
                    ordered << q.answers[i];
            o["answers"] = QJsonArray::fromStringList(ordered);
            o.remove("correct_index");
            o.remove("correct");
            o.remove("translations");
        }
        questions << o;
    }
    root["questions"] = questions;
    if (legacy) {
        root.remove("format");
        root.remove("version");
    } else {
        root["format"] = "buzz-question-studio";
        root["version"] = 2;
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
bool saveDocument(const Document &doc, const QString &path, QString *error, bool legacy) {
    try {
        const auto bytes = serialize(doc, QFileInfo(path).absolutePath(), legacy);
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
            if (error)
                *error = f.errorString();
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
Document exampleDocument() {
    Document d;
    d.title = "Wieczór z przyjaciółmi";
    d.description = "Przykładowy projekt — pytania własne";
    auto add = [&](QString text, QStringList answers, int subject, int category, int level) {
        auto q = newQuestion();
        q.text = text;
        q.answers = answers;
        q.subjectId = subject;
        q.categoryId = category;
        q.category = categoryName(subject);
        q.difficulty = level;
        d.questions << q;
    };
    add("Która planeta Układu Słonecznego jest największa?", {"Jowisz", "Saturn", "Ziemia", "Neptun"}, 235,
        237, 1);
    add("Który instrument ma zwykle 88 klawiszy?", {"Fortepian", "Skrzypce", "Flet", "Trąbka"}, 175, 224, 1);
    add("Jak nazywa się stolica Portugalii?", {"Lizbona", "Porto", "Madryt", "Braga"}, 250, 253, 1);
    add("Kto napisał powieść „Lalka”?",
        {"Bolesław Prus", "Henryk Sienkiewicz", "Stefan Żeromski", "Władysław Reymont"}, 177, 179, 2);
    add("Ilu zawodników jednej drużyny przebywa na boisku w siatkówce?",
        {"Sześciu", "Pięciu", "Siedmiu", "Ośmiu"}, 174, 247, 1);
    return d;
}
} // namespace studio
