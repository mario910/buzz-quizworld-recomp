#include "document.h"
#include "buzz_import.h"
#include "languages.h"
#include "kip_import.h"
#include "legacy_import.h"
#include "ps3_source.h"
#include "project_media.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <stdexcept>
#include <utility>

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
bool adaptImportedAnswers(Question &question, const QList<int> &correctAnswers) {
    const auto &answers = question.answers;
    if (answers.size() < 2 || answers.size() > 4 || correctAnswers.isEmpty())
        return false;
    if (std::any_of(correctAnswers.begin(), correctAnswers.end(), [&](int i) { return i < 0 || i >= answers.size(); }))
        return false;
    if (answers.size() == 4 && correctAnswers.size() == 1) {
        question.correct = correctAnswers.first();
        return true;
    }
    struct Labels {
        QString both, neither, completeSet;
    };
    // These strings become part of the imported quiz, in its content language.
    static const QMap<QString, Labels> labels = {
        {"pl", {"Obie odpowiedzi", "Żadna z powyższych", "Wybierz pełny zestaw poprawnych odpowiedzi."}},
        {"en", {"Both answers", "None of the above", "Choose the complete set of correct answers."}},
        {"cs", {"Obě odpovědi", "Žádná z uvedených", "Vyberte úplný soubor správných odpovědí."}},
        {"da", {"Begge svar", "Ingen af ovenstående", "Vælg sættet med alle de rigtige svar."}},
        {"de", {"Beide Antworten", "Keine der genannten Antworten", "Wähle die Kombination mit allen richtigen Antworten."}},
        {"el", {"Και οι δύο απαντήσεις", "Καμία από τις παραπάνω", "Επίλεξε το σύνολο με όλες τις σωστές απαντήσεις."}},
        {"es", {"Ambas respuestas", "Ninguna de las anteriores", "Elige la opción que incluye todas las respuestas correctas."}},
        {"fi", {"Molemmat vastaukset", "Ei mikään edellisistä", "Valitse vaihtoehto, jossa ovat kaikki oikeat vastaukset."}},
        {"fr", {"Les deux réponses", "Aucune des réponses", "Choisissez l’ensemble qui contient toutes les bonnes réponses."}},
        {"hr", {"Oba odgovora", "Ništa od navedenog", "Odaberite skup koji sadrži sve točne odgovore."}},
        {"it", {"Entrambe le risposte", "Nessuna delle precedenti", "Scegli l’opzione che contiene tutte le risposte corrette."}},
        {"nl", {"Beide antwoorden", "Geen van de bovenstaande", "Kies de optie met alle juiste antwoorden."}},
        {"no", {"Begge svarene", "Ingen av alternativene", "Velg alternativet med alle de riktige svarene."}},
        {"pt", {"Ambas as respostas", "Nenhuma das anteriores", "Escolhe a opção com todas as respostas certas."}},
        {"ru", {"Оба ответа", "Ни один из вариантов", "Выберите вариант со всеми правильными ответами."}},
        {"sv", {"Båda svaren", "Inget av alternativen", "Välj alternativet med alla rätta svar."}},
    };
    const auto label = labels.constFind(question.language.section('-', 0, 0));
    if (label == labels.cend())
        return false;
    QSet<QString> unique;
    for (const auto &answer : answers) {
        const auto key = answer.simplified().toCaseFolded();
        if (key.isEmpty() || unique.contains(key))
            return false;
        unique.insert(key);
    }
    if (correctAnswers.size() == 1) {
        if (unique.contains(label->neither.toCaseFolded()) ||
            (answers.size() == 2 && unique.contains(label->both.toCaseFolded())))
            return false;
        if (answers.size() == 2)
            question.answers << label->both;
        question.answers << label->neither;
        question.correct = correctAnswers.first();
        return true;
    }
    int correctMask = 0;
    for (const int index : correctAnswers)
        correctMask |= 1 << index;
    const auto combination = [&](int mask) {
        QStringList parts;
        for (int i = 0; i < answers.size(); ++i)
            if (mask & (1 << i))
                parts << answers[i];
        return parts.isEmpty() ? label->neither : parts.join(QStringLiteral(" + "));
    };
    QStringList choices{combination(correctMask)};
    for (int mask = (1 << answers.size()) - 1; mask >= 0 && choices.size() < 4; --mask) {
        auto choice = combination(mask);
        if (!choices.contains(choice, Qt::CaseInsensitive))
            choices << std::move(choice);
    }
    if (choices.size() != 4)
        return false;
    question.text += '\n' + label->completeSet;
    question.answers = std::move(choices);
    question.correct = 0;
    return true;
}
QString adaptedAnswersWarning(int count) {
    return QCoreApplication::translate("Document", "Pytania przekształcone na jednokrotny wybór: %1.").arg(count);
}
QString unsupportedAnswersWarning(int count) {
    return QCoreApplication::translate("Document", "Pytania pominięte z powodu formatu odpowiedzi: %1.").arg(count);
}
QJsonArray Question::sourceMedia() const {
    return (translations.isEmpty() ? extra.value("source_media")
                                   : translations.value(language).toObject().value("source_media")).toArray();
}
namespace {
bool isMusicReference(const QJsonValue &type) { return type == 1 || type == "ps2-audio" || type == "psp-audio"; }
}
QJsonObject Question::sourceMusic() const {
    for (const auto &entry : sourceMedia()) {
        const auto ref = entry.toObject();
        if (isMusicReference(ref["type"]))
            return ref;
    }
    return {};
}
bool Question::usesMusic() const { return !music.isEmpty() || !sourceMusic().isEmpty(); }
QuestionContentType Question::contentType() const {
    if (!video.isEmpty())
        return QuestionContentType::Video;
    if (!image.isEmpty())
        return QuestionContentType::Picture;
    auto kind = QuestionContentType::Text;
    for (const auto &entry : sourceMedia()) {
        const auto type = entry.toObject().value("type");
        if (type == 2 || type == "ps2-video" || type == "psp-video")
            return QuestionContentType::Video;
        if (type == 3 || type == "ps2-picture" || type == "psp-picture")
            kind = QuestionContentType::Picture;
    }
    return kind;
}
bool Question::hasUnresolvedMusic() const {
    return music.isEmpty() ? !sourceMusic().isEmpty() : !QFileInfo(music).isFile();
}
bool Question::hasUnresolvedVisualMedia() const {
    if (!image.isEmpty() || !video.isEmpty())
        return false;
    const auto refs = sourceMedia();
    return std::any_of(refs.cbegin(), refs.cend(), [](const auto &entry) {
        return !isMusicReference(entry.toObject()["type"]);
    });
}
bool Question::hasUnresolvedSourceMedia() const {
    return hasUnresolvedMusic() || hasUnresolvedVisualMedia();
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
    if (video.isEmpty() && !image.isEmpty() && !QFileInfo::exists(image))
        out << QCoreApplication::translate("Document", "Nie znaleziono obrazka.");
    if (!speech.isEmpty() && !QFileInfo::exists(speech))
        out << QCoreApplication::translate("Document", "Nie znaleziono nagrania.");
    if (!video.isEmpty() && !QFileInfo::exists(video))
        out << QCoreApplication::translate("Document", "Nie znaleziono filmu.");
    if (hasUnresolvedMusic())
        out << QCoreApplication::translate("Document", "Brakuje klipu muzycznego do pytania.");
    if (hasUnresolvedVisualMedia())
        out << QCoreApplication::translate("Document",
                                           "Pytanie korzysta z nierozwiązanych mediów z gry. Przypisz obrazek lub film.");
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
    if (video.isEmpty())
        o.remove("video"); // Older projects stay byte-identical.
    else
        o["video"] = persistedPath(video, baseDir);
    if (music.isEmpty())
        o.remove("music");
    else
        o["music"] = persistedPath(music, baseDir);
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
QStringList Question::sourceCategoryPath() const {
    if (source.isEmpty() || source == "Własne" || source == "JSON")
        return {};
    QStringList path;
    auto original = extra["source_category_labels"].toObject().value(language);
    if (!original.isArray())
        original = extra["source_category_path"];
    if (original.isArray()) {
        for (const auto &part : original.toArray())
            if (const auto name = part.toString().trimmed(); !name.isEmpty())
                path << name;
    } else if (!category.isEmpty() && category != "Bez kategorii") {
        path << category;
    }
    // An imported question can be assigned even if its game supplied no section.
    if (path.isEmpty())
        path << QString();
    return path;
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
    video = next["video"].toString();
    music = next["music"].toString();
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
        if (v.contains("video"))
            v["video"] = persistedPath(v["video"].toString(), baseDir);
        if (v.contains("music"))
            v["music"] = persistedPath(v["music"].toString(), baseDir);
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
    titles[language] = title;
    if (description.isEmpty())
        descriptions.remove(language);
    else
        descriptions[language] = description;
    // A language without its own title keeps the current one until renamed.
    title = titles.value(code).toString(title);
    description = descriptions.value(code).toString();
    language = code;
    refreshCategoryNames();
}
const CustomCategory *Document::customCategory(int id) const {
    for (const auto &c : categories)
        if (c.id == id)
            return &c;
    return nullptr;
}
QString Document::categoryLabel(int id) const {
    if (const auto *c = customCategory(id)) {
        const auto name = c->name(language);
        return name.isEmpty() ? QCoreApplication::translate("Document", "Kategoria bez nazwy") : name;
    }
    return categoryName(id);
}
void Document::refreshCategoryNames() {
    for (auto &q : questions)
        if (isCustomCategory(q.subjectId))
            q.category = categoryLabel(q.subjectId);
}
int nextCustomCategoryId(const Document &document) {
    int next = firstCustomCategory + 1;
    for (const auto &c : document.categories)
        next = std::max(next, c.id + 1);
    return next;
}
QMap<int, int> mergeCustomCategories(Document &into, const QVector<CustomCategory> &incoming) {
    QMap<int, int> remap;
    // Subjects first, so subcategories can find their merged parent.
    for (const bool subjects : {true, false})
        for (auto c : incoming) {
            if ((c.parent == 0) != subjects)
                continue;
            c.parent = c.parent ? remap.value(c.parent) : 0;
            const auto same = std::find_if(into.categories.begin(), into.categories.end(), [&](const auto &e) {
                return e.parent == c.parent &&
                       (c.sourceKey.isEmpty() ? e.sourceKey.isEmpty() && e.names == c.names : e.sourceKey == c.sourceKey);
            });
            if (same != into.categories.end()) {
                remap[c.id] = same->id;
                for (auto it = c.names.constBegin(); it != c.names.constEnd(); ++it)
                    if (!same->names.contains(it.key()))
                        same->names[it.key()] = it.value();
                continue;
            }
            if (into.categories.size() >= 99)
                throw std::runtime_error(
                    QCoreApplication::translate("Document", "Projekt może mieć najwyżej 99 kategorii własnych.")
                        .toStdString());
            const int id = nextCustomCategoryId(into);
            remap[c.id] = id;
            c.id = id;
            into.categories << c;
        }
    return remap;
}

int retainGameCategories(Document &document, qsizetype firstQuestion, const QString &sourceLanguage,
                         QStringList &warnings) {
    QHash<QString, int> bySource;
    for (const auto &category : std::as_const(document.categories))
        if (!category.sourceKey.isEmpty())
            bySource.insert(category.sourceKey, category.id);
    int nextId = nextCustomCategoryId(document);
    int kept = 0, unmapped = 0;
    const auto key = [](const QString &game, const QStringList &path) {
        auto array = QJsonArray::fromStringList(path);
        array.prepend(game);
        return QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(array).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    };
    const auto validName = [](const QString &originalName) {
        // Game packs use BMP text and the Studio category editor caps names at 40 characters.
        QString name;
        for (const QChar character : originalName.simplified()) {
            if (!character.isSurrogate() && character.unicode() != 0xfefe && character.unicode() != 0xfe00)
                name += character;
            if (name.size() == customCategoryNameLimit)
                break;
        }
        return name;
    };
    auto addCategory = [&](const QString &sourceKey, const QString &name, const QJsonObject &labels, int parent,
                           bool create) {
        if (const int id = bySource.value(sourceKey)) {
            auto &category = *std::find_if(document.categories.begin(), document.categories.end(),
                                           [id](const CustomCategory &c) { return c.id == id; });
            for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
                const bool oldSourceLabel = category.sourceName == name && category.names.value(it.key()) == name;
                if (!category.names.contains(it.key()) || oldSourceLabel)
                    category.names[it.key()] = it.value();
            }
            if (category.sourceName == name && labels.contains(sourceLanguage))
                category.sourceName = labels.value(sourceLanguage).toString();
            return id;
        }
        if (!create)
            return 0;
        const int id = nextId++;
        document.categories << CustomCategory{id, parent, labels, sourceKey, labels.value(sourceLanguage).toString(name)};
        bySource.insert(sourceKey, id);
        return id;
    };
    for (qsizetype i = 0; i < document.questions.size(); ++i) {
        auto &question = document.questions[i];
        const bool refreshOnly = i < firstQuestion || question.extra["category_override"].toBool();
        const auto localized = question.extra["source_category_labels"].toObject();
        if (refreshOnly && localized.isEmpty())
            continue;
        if (question.extra["source_uses_quiz_world_categories"].toBool() && question.subjectId > 0 &&
            question.subjectId <= 999 && question.categoryId > 0 && question.categoryId <= 999 &&
            question.categoryId != question.subjectId) {
            if (!refreshOnly)
                ++kept;
            continue;
        }
        QStringList path;
        for (const auto &part : question.extra["source_pack_category_path"].toArray())
            if (!part.toString().isEmpty())
                path << part.toString();
        if (path.isEmpty())
            path = question.sourceCategoryPath();
        if (path.isEmpty())
            continue;
        if (!refreshOnly) {
            question.subjectId = question.categoryId = 0;
            question.extra["category_override"] = true;
        }
        if (path.first().isEmpty()) {
            if (!refreshOnly)
                ++unmapped;
            continue;
        }
        auto identity = path;
        for (const auto *field : {"source_subject_key", "source_section_key"})
            if (const auto value = question.extra.value(field).toString(); !value.isEmpty())
                identity[0] = value;
        if (path.size() > 1)
            if (const auto value = question.extra.value("source_category_key").toString(); !value.isEmpty())
                identity[1] = value;
        const auto subjectKey = key(question.source, identity.first(1));
        auto childIdentity = identity;
        if (childIdentity.size() == 1)
            childIdentity << QString();
        const auto childKey = key(question.source, childIdentity);
        const int needed = !bySource.contains(subjectKey) + !bySource.contains(childKey);
        const auto subjectName = validName(path.first());
        const auto childName = validName(path.size() > 1 ? path.mid(1).join(" / ") : path.first());
        if (subjectName.isEmpty() || childName.isEmpty() ||
            (!refreshOnly && (document.categories.size() + needed > 99 || nextId + needed > 100000))) {
            if (!refreshOnly)
                ++unmapped;
            continue;
        }
        QJsonObject subjectLabels{{sourceLanguage, subjectName}}, childLabels{{sourceLanguage, childName}};
        for (auto it = localized.constBegin(); it != localized.constEnd(); ++it) {
            QStringList parts;
            const auto names = it.value().toArray();
            for (qsizetype n = 0; n < path.size() && n < names.size(); ++n)
                parts << names[n].toString();
            if (parts.size() != path.size())
                continue;
            const auto subject = validName(parts.first());
            const auto child = validName(parts.size() > 1 ? parts.mid(1).join(" / ") : parts.first());
            if (!subject.isEmpty() && !child.isEmpty()) {
                subjectLabels[it.key()] = subject;
                childLabels[it.key()] = child;
            }
        }
        const int subject = addCategory(subjectKey, subjectName, subjectLabels, 0, !refreshOnly);
        const int child = addCategory(childKey, childName, childLabels, subject, !refreshOnly);
        if (refreshOnly)
            continue;
        question.subjectId = subject;
        question.categoryId = child;
        ++kept;
    }
    document.refreshCategoryNames();
    if (unmapped)
        warnings << QCoreApplication::translate("Document",
            "Pytania bez przypisanej kategorii gry: %1. Brakuje nazwy źródłowej lub miejsca na kolejne kategorie.").arg(unmapped);
    return kept;
}

static QVector<CustomCategory> readCustomCategories(const QJsonValue &value) {
    auto invalid = [] {
        throw std::runtime_error(
            QCoreApplication::translate("Document", "Niepoprawna lista kategorii własnych.").toStdString());
    };
    if (value.isUndefined() || value.isNull())
        return {};
    if (!value.isArray() || value.toArray().size() > 99)
        invalid();
    QVector<CustomCategory> out;
    QSet<int> subjects, ids;
    QSet<QString> sourceKeys;
    for (const auto &v : value.toArray()) {
        const auto o = v.toObject();
        CustomCategory c{o["id"].toInt(), o["parent"].toInt(), o["names"].toObject(),
                         o["source_key"].toString(), o["source_name"].toString()};
        if (!isCustomCategory(c.id) || c.id > 99999 || ids.contains(c.id))
            invalid();
        if (c.sourceName.size() > customCategoryNameLimit ||
            (!c.sourceKey.isEmpty() && (c.sourceKey.size() != 64 || sourceKeys.contains(c.sourceKey))))
            invalid();
        if (!c.sourceKey.isEmpty())
            sourceKeys.insert(c.sourceKey);
        for (auto it = c.names.constBegin(); it != c.names.constEnd(); ++it)
            if (!it.value().isString() || it.value().toString().size() > customCategoryNameLimit)
                invalid();
        ids.insert(c.id);
        if (!c.parent)
            subjects.insert(c.id);
        out << c;
    }
    for (const auto &c : out)
        if (c.parent && !subjects.contains(c.parent))
            invalid();
    return out;
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
    if (o.contains("video"))
        o["video"] = localPath(o["video"].toString(), baseDir);
    if (o.contains("music"))
        o["music"] = localPath(o["music"].toString(), baseDir);
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
        out.document.titles = pack["titles"].toObject();
        out.document.descriptions = pack["descriptions"].toObject();
        out.document.categories = readCustomCategories(pack["categories"]);
        for (const char *key : {"titles", "descriptions", "categories"})
            out.document.packExtra.remove(key);
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
                out.gameImport = true;
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
            q.video = t["video"].toString();
            q.music = t["music"].toString();
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
        out.document.title = out.document.titles.value(targetLanguage).toString(out.document.title);
        out.document.description =
            out.document.descriptions.value(targetLanguage).toString(out.document.description);
        out.document.refreshCategoryNames();
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
    const auto gameImport = [](LoadResult result) {
        result.gameImport = true;
        return result;
    };
    if (isProjectArchive(path))
        return openProjectArchive(path);
    if (ps3::isPs3Source(path))
        return gameImport(ps3::importPs3(path, language));
    if (isKipSource(path))
        return gameImport(importKip(path, language));
    if (isLegacySource(path))
        return gameImport(importLegacy(path, language));
    if (path.endsWith(".pak", Qt::CaseInsensitive) || path.endsWith(".edat", Qt::CaseInsensitive))
        return gameImport(importBuzz(path, language));
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
    if (!legacy) {
        auto titles = doc.titles, descriptions = doc.descriptions;
        titles[doc.language] = doc.title;
        if (doc.description.isEmpty())
            descriptions.remove(doc.language);
        else
            descriptions[doc.language] = doc.description;
        // Written only when they say more than the title/description above.
        if (titles.size() > 1)
            pack["titles"] = titles;
        if (descriptions.size() > (descriptions.contains(doc.language) ? 1 : 0))
            pack["descriptions"] = descriptions;
        if (!doc.categories.isEmpty()) {
            QJsonArray categories;
            for (const auto &c : doc.categories) {
                QJsonObject category{{"id", c.id}, {"parent", c.parent}, {"names", c.names}};
                if (!c.sourceKey.isEmpty())
                    category["source_key"] = c.sourceKey;
                if (!c.sourceName.isEmpty())
                    category["source_name"] = c.sourceName;
                categories << category;
            }
            pack["categories"] = categories;
        }
    }
    pack["language"] = doc.language;
    pack["languages"] = QJsonArray::fromStringList(legacy ? QStringList{doc.language} : doc.languages());
    root["pack"] = pack;
    QJsonArray questions;
    for (const auto &q : doc.questions) {
        auto o = q.json(baseDir);
        if (legacy) {
            if (!q.music.isEmpty() || !q.sourceMusic().isEmpty())
                throw std::runtime_error(
                    QCoreApplication::translate("Document", "Eksport JSON pomija klipy muzyczne. Zbuduj paczkę .edat w Studiu.")
                        .toStdString());
            const auto media =
                q.translations.isEmpty() ? q.extra["source_media"] : q.localized()["source_media"];
            if (!media.toArray().isEmpty() && q.image.isEmpty())
                throw std::runtime_error(
                    QCoreApplication::translate(
                        "Document",
                        "Paczka zawiera media SDI, które nie zostały jeszcze przekonwertowane. "
                        "Eksport JSON dla buzzpack pominąłby te media; zapisz projekt lub przypisz obrazki.")
                        .toStdString());
            if (isCustomCategory(q.subjectId) || isCustomCategory(q.categoryId))
                throw std::runtime_error(
                    QCoreApplication::translate("Document",
                                                "Eksport do buzzpack nie obsługuje kategorii własnych. "
                                                "Zbuduj paczkę .edat w Studiu.")
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
Document exampleDocument(const QString &language) {
    Document d;
    d.language = language;
    d.title = "Wieczór z przyjaciółmi";
    d.description = "Przykładowy projekt — pytania własne";
    auto add = [&](QString text, QStringList answers, int subject, int category, int level) {
        auto q = newQuestion();
        q.language = language;
        q.text = text;
        q.answers = answers;
        q.subjectId = subject;
        q.categoryId = category;
        q.category = categoryName(subject);
        q.difficulty = level;
        d.questions << q;
    };
    if (language != "pl") {
        // The examples use numerical choices so the same answer order works in every language.
        static const QHash<QString, QStringList> examples{
            {"en", {"Quiz night", "Sample questions", "How many planets orbit the Sun?",
                    "How many white keys does a standard piano have?", "How many sides does a hexagon have?",
                    "How many acts are in Shakespeare's Hamlet?", "How many players per team are on court in indoor volleyball?"}},
            {"de", {"Quizabend", "Beispielfragen", "Wie viele Planeten umkreisen die Sonne?",
                    "Wie viele weiße Tasten hat ein Standardklavier?", "Wie viele Seiten hat ein Sechseck?",
                    "Wie viele Akte hat Shakespeares Hamlet?", "Wie viele Spieler pro Mannschaft stehen beim Hallenvolleyball auf dem Feld?"}},
            {"fr", {"Soirée quiz", "Questions d’exemple", "Combien de planètes tournent autour du Soleil ?",
                    "Combien de touches blanches compte un piano standard ?", "Combien de côtés a un hexagone ?",
                    "Combien d’actes compte Hamlet de Shakespeare ?", "Combien de joueurs par équipe sont sur le terrain en volley-ball en salle ?"}},
            {"it", {"Serata quiz", "Domande di esempio", "Quanti pianeti orbitano intorno al Sole?",
                    "Quanti tasti bianchi ha un pianoforte standard?", "Quanti lati ha un esagono?",
                    "Quanti atti ha l’Amleto di Shakespeare?", "Quanti giocatori per squadra sono in campo nella pallavolo indoor?"}},
            {"es", {"Noche de quiz", "Preguntas de ejemplo", "¿Cuántos planetas orbitan alrededor del Sol?",
                    "¿Cuántas teclas blancas tiene un piano estándar?", "¿Cuántos lados tiene un hexágono?",
                    "¿Cuántos actos tiene Hamlet de Shakespeare?", "¿Cuántos jugadores por equipo hay en pista en el voleibol de sala?"}},
            {"nl", {"Quizavond", "Voorbeeldvragen", "Hoeveel planeten draaien om de zon?",
                    "Hoeveel witte toetsen heeft een standaardpiano?", "Hoeveel zijden heeft een zeshoek?",
                    "Hoeveel bedrijven heeft Hamlet van Shakespeare?", "Hoeveel spelers per team staan bij zaalvolleybal op het veld?"}},
            {"pt", {"Noite de quiz", "Perguntas de exemplo", "Quantos planetas orbitam o Sol?",
                    "Quantas teclas brancas tem um piano normal?", "Quantos lados tem um hexágono?",
                    "Quantos atos tem Hamlet, de Shakespeare?", "Quantos jogadores por equipa estão em campo no voleibol de pavilhão?"}},
            {"sv", {"Quizkväll", "Exempelfrågor", "Hur många planeter kretsar kring solen?",
                    "Hur många vita tangenter har ett standardpiano?", "Hur många sidor har en sexhörning?",
                    "Hur många akter har Shakespeares Hamlet?", "Hur många spelare per lag är på planen i inomhusvolleyboll?"}},
            {"da", {"Quizaften", "Eksempelspørgsmål", "Hvor mange planeter kredser om Solen?",
                    "Hvor mange hvide tangenter har et almindeligt klaver?", "Hvor mange sider har en sekskant?",
                    "Hvor mange akter har Shakespeares Hamlet?", "Hvor mange spillere pr. hold er på banen i indendørs volleyball?"}},
            {"no", {"Quizkveld", "Eksempelspørsmål", "Hvor mange planeter går i bane rundt sola?",
                    "Hvor mange hvite tangenter har et vanlig piano?", "Hvor mange sider har en sekskant?",
                    "Hvor mange akter har Shakespeares Hamlet?", "Hvor mange spillere per lag er på banen i innendørsvolleyball?"}},
            {"fi", {"Tietovisailta", "Esimerkkikysymyksiä", "Kuinka monta planeettaa kiertää Aurinkoa?",
                    "Kuinka monta valkoista kosketinta tavallisessa pianossa on?", "Kuinka monta sivua kuusikulmiossa on?",
                    "Kuinka monta näytöstä Shakespearen Hamletissa on?", "Kuinka monta pelaajaa joukkueesta on kentällä sisälentopallossa?"}},
            {"ru", {"Вечер викторин", "Примеры вопросов", "Сколько планет обращается вокруг Солнца?",
                    "Сколько белых клавиш у стандартного фортепиано?", "Сколько сторон у шестиугольника?",
                    "Сколько действий в «Гамлете» Шекспира?", "Сколько игроков от каждой команды находится на площадке в классическом волейболе?"}},
            {"cs", {"Kvízový večer", "Ukázkové otázky", "Kolik planet obíhá kolem Slunce?",
                    "Kolik bílých kláves má standardní klavír?", "Kolik stran má šestiúhelník?",
                    "Kolik dějství má Shakespearův Hamlet?", "Kolik hráčů každého týmu je na hřišti při halovém volejbalu?"}},
            {"el", {"Βραδιά κουίζ", "Ενδεικτικές ερωτήσεις", "Πόσοι πλανήτες περιφέρονται γύρω από τον Ήλιο;",
                    "Πόσα λευκά πλήκτρα έχει ένα τυπικό πιάνο;", "Πόσες πλευρές έχει ένα εξάγωνο;",
                    "Πόσες πράξεις έχει ο Άμλετ του Σαίξπηρ;", "Πόσοι παίκτες κάθε ομάδας βρίσκονται στο γήπεδο στο βόλεϊ σάλας;"}},
            {"hr", {"Kviz večer", "Primjeri pitanja", "Koliko planeta kruži oko Sunca?",
                    "Koliko bijelih tipki ima standardni klavir?", "Koliko stranica ima šesterokut?",
                    "Koliko činova ima Shakespeareov Hamlet?", "Koliko je igrača svake ekipe na terenu u dvoranskoj odbojci?"}}};
        const auto sample = examples.value(language, examples.value("en"));
        d.title = sample[0];
        d.description = sample[1];
        add(sample[2], {"8", "7", "9", "10"}, 235, 237, 1);
        add(sample[3], {"52", "36", "44", "60"}, 175, 224, 1);
        add(sample[4], {"6", "5", "7", "8"}, 235, 237, 1);
        add(sample[5], {"5", "3", "4", "6"}, 177, 179, 2);
        add(sample[6], {"6", "5", "7", "8"}, 174, 247, 1);
        for (auto &q : d.questions)
            q.category = QCoreApplication::translate("Main", q.category.toUtf8().constData());
        return d;
    }
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
