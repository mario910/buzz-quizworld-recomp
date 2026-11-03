#include "pack_builder.h"
#include "buzz_import.h"
#include "languages.h"
#include "pack_codec.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <future>
#include <mutex>

namespace studio {
using namespace pack;
namespace {
QString directoryName(int number) { return QString("PACK%1").arg(number, 4, 10, QLatin1Char('0')); }
QString internalName(int number, const QString &name) { return QString("p%1_%2").arg(number).arg(name); }
QString languageFolder(Archive &archive, const QString &code) {
    QString exact, base;
    for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it) {
        if (!it.key().endsWith("/SCRIPTS/GENERATED/questioncategories.clu"))
            continue;
        const auto folder = it.key().section('/', 0, 0), normalized = languageCode(folder);
        if (normalized == code)
            exact = folder;
        if (normalized.section('-', 0, 0) == code.section('-', 0, 0))
            base = folder;
    }
    const auto result = exact.isEmpty() ? base : exact;
    if (result.size() != 3)
        fail(QCoreApplication::translate(
            "PackBuilder",
            "Szablon nie zawiera aktywnego języka projektu. Wybierz paczkę PACK0100 w tym języku."));
    return result;
}
QString digestFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(file.errorString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        fail(file.errorString());
    return QString::fromLatin1(hash.result().toHex());
}
void atomicCopy(const QString &source, const QString &destination, const QString &expectedHash = {},
                const std::atomic_bool *cancel = nullptr) {
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly))
        fail(in.errorString());
    QSaveFile out(destination);
    if (!out.open(QIODevice::WriteOnly))
        fail(out.errorString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!in.atEnd()) {
        checkCancelled(cancel);
        const auto block = in.read(1024 * 1024);
        if (block.isEmpty() && in.error() != QFileDevice::NoError)
            fail(in.errorString());
        hash.addData(block);
        if (out.write(block) != block.size())
            fail(out.errorString());
    }
    if (!expectedHash.isEmpty() && QString::fromLatin1(hash.result().toHex()) != expectedHash)
        fail(QCoreApplication::translate("PackBuilder", "Plik paczki zmienił się podczas kopiowania."));
    checkCancelled(cancel);
    if (!out.commit())
        fail(out.errorString());
}
Qt::CaseSensitivity pathCase() {
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}
bool inside(const QString &root, const QString &path) {
    return path.compare(root, pathCase()) == 0 ||
           path.startsWith(root.endsWith('/') ? root : root + "/", pathCase());
}
QString checkedPath(const QString &root, const QString &path) {
    auto current = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList suffix;
    const auto requested = QDir::fromNativeSeparators(current);
    while (!QFileInfo::exists(current)) {
        suffix.prepend(QFileInfo(current).fileName());
        const auto parent = QFileInfo(current).absolutePath();
        if (parent == current)
            fail(QCoreApplication::translate("PackBuilder", "Niepoprawna ścieżka instalacji."));
        current = parent;
    }
    current = QFileInfo(current).canonicalFilePath();
    for (const auto &part : suffix)
        current = QDir(current).filePath(part);
    current = QDir::fromNativeSeparators(QDir::cleanPath(current));
    if (!inside(root, current))
        fail(QCoreApplication::translate(
            "PackBuilder", "Ścieżka instalacji wychodzi poza wybrany katalog runtime (łącze symboliczne)."));
    if (current.compare(requested, pathCase()) != 0)
        fail(QCoreApplication::translate("PackBuilder", "Katalog paczki nie może być łączem symbolicznym."));
    return current;
}
QVector<Category> validatedCategories(Archive &archive, const QString &language) {
    auto categories = readCategories(archive.read(language + "/SCRIPTS/GENERATED/questioncategories.clu"));
    QSet<int> ids;
    QSet<QString> names;
    for (const auto &c : categories) {
        if (c.id < 1 || c.id > 999 || ids.contains(c.id) || names.contains(c.name))
            fail(QCoreApplication::translate("PackBuilder", "Niepoprawne lub powtórzone kategorie paczki."));
        ids.insert(c.id);
        names.insert(c.name);
    }
    for (const auto &c : categories)
        if (c.parent && !ids.contains(c.parent))
            fail(QCoreApplication::translate("PackBuilder",
                                             "Kategoria wskazuje niezarejestrowanego rodzica."));
    return categories;
}
void validateFile(Archive &archive, const QString &name, const QByteArray &bytes, const QString &language,
                  int number) {
    if (!name.startsWith(language + "/"))
        return;
    if (name.endsWith(".clu") && writeLua(readLua(bytes)) != bytes)
        fail(QCoreApplication::translate("PackBuilder", "Walidacja skryptu CLU nie powiodła się."));
    if (name.endsWith(".sdi"))
        validateSdi(bytes);
    if (name.endsWith(".rsf") && name.contains("/SPEECH/"))
        validateRsfSpeech(bytes);
    if (!name.endsWith(".qak"))
        return;
    QMap<QString, int> pictureCounts;
    if (u16(bytes, 18) != number || bytes.mid(0x174, 3) != language.toLatin1())
        fail(QCoreApplication::translate("PackBuilder", "Niepoprawny numer paczki w bazie pytań."));
    if (swapRound(bytes) != archive.read(name.left(name.size() - 4) + ".kaq"))
        fail(QCoreApplication::translate("PackBuilder", "Niepoprawna para baz QAK/KAQ."));
    const auto table = u32(bytes, 12), sections = u32(bytes, 8);
    for (quint32 i = 0; i < sections; ++i) {
        const auto row = table + i * 8, offset = u32(bytes, row);
        const auto count = u16(bytes, row + 4);
        const auto kind = quint8(bytes[row + 6]);
        if (kind == 3) {
            check(bytes, offset, quint64(count) * 8);
            for (int j = 0; j < count; ++j) {
                const auto file = language + "/QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/" +
                                  QString::number(6000000000ll + u32(bytes, offset + j * 8)) + ".rsf";
                if (!archive.entries.contains(file))
                    fail(QCoreApplication::translate("PackBuilder",
                                                     "Brak nagrania wskazanego przez pytanie."));
            }
        }
        if (kind == 0) {
            check(bytes, offset, quint64(count) * 16);
            for (int j = 0; j < count; ++j) {
                const auto p = offset + j * 16;
                const int type = quint8(bytes[p + 12]);
                const auto file = language + "/QUESTIONASSETS/" + (type == 1 ? "MUSIC/STREAMED/" : "VIDEO/") +
                                  (type == 3 ? QString("pics001.sdi")
                                             : QString::number(1000000ll + u32(bytes, p + 4)) +
                                                   (type == 1 ? ".rsf" : ".sdi"));
                if (!archive.entries.contains(file))
                    fail(QCoreApplication::translate("PackBuilder", "Brak mediów wskazanych przez pytanie."));
                if (type == 3) {
                    if (!pictureCounts.contains(file))
                        pictureCounts[file] = u32(archive.read(file), 52, true);
                    const auto slot = u16(bytes, p + 10);
                    if (!slot || slot >= pictureCounts[file])
                        fail(QCoreApplication::translate("PackBuilder",
                                                         "Brak mediów wskazanych przez pytanie."));
                }
            }
        }
    }
}
bool emptyLanguage(const Question &q) {
    if (!q.text.trimmed().isEmpty() || !q.speech.isEmpty() || !q.image.isEmpty() || !q.ttsText.isEmpty())
        return false;
    for (const auto &a : q.answers)
        if (!a.trimmed().isEmpty())
            return false;
    return true;
}
void validateGenerated(const QString &path, const QString &language, const QVector<Question> &source,
                       const QMap<QString, quint32> &ids, int expectedPack, const std::atomic_bool *cancel) {
    Archive archive(path);
    const auto categories = validatedCategories(archive, language);
    for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it) {
        checkCancelled(cancel);
        const auto bytes = archive.read(it.key()); // Full ZIP CRC validation before publishing/installing.
        validateFile(archive, it.key(), bytes, language, expectedPack);
    }
    const auto imported = importBuzz(path, languageCode(language), false);
    if (!imported.ok() || imported.document.questions.size() != source.size())
        fail(QCoreApplication::translate("PackBuilder", "Walidacja zapisanej paczki nie powiodła się: ") +
             imported.error);
    QMap<quint32, Question> byId;
    for (const auto &q : imported.document.questions)
        byId[q.extra["source_question_id"].toDouble()] = q;
    for (const auto &q : source) {
        const auto actual = byId.value(ids.value(q.id));
        QStringList expected{q.answers[q.correct]};
        for (int i = 0; i < 4; ++i)
            if (i != q.correct)
                expected << q.answers[i];
        if (actual.text != q.text || actual.answers.size() != 4 || actual.answers != expected ||
            actual.difficulty != q.difficulty || actual.answers[actual.correct] != q.answers[q.correct])
            fail(QCoreApplication::translate("PackBuilder",
                                             "Treść lub poprawna odpowiedź zmieniła się podczas zapisu."));
    }
    Q_UNUSED(categories);
}
QVector<Call> roundSetup(int number, const QString &dir, const QMap<QString, QString> &games,
                         const QVector<QPair<QString, QString>> &categories) {
    const QMap<QString, QStringList> plans{
        {"1p", {"TriggerFinger", "TriggerFinger", "TriggerFinger"}},
        {"4p",
         {"PointBuilder", "PassTheBomb", "FastestFingerFirst", "PieFight", "PointStealerMix", "BetIt",
          "FinalCountdown"}},
        {"8p", {"PointBuilder", "TriggerFinger", "BetIt", "FinalCountdown"}}};
    const QMap<QString, QString> kinds{
        {"1p", "1PPACKSPECIFIC"}, {"4p", "4PPACKSPECIFIC"}, {"8p", "8PPACKSPECIFIC"}};
    const QMap<QString, QString> maxPlayers{
        {"1p", "Max1Player"}, {"4p", "Max4Players"}, {"8p", "Max8Players"}};
    const QMap<QString, QString> channels{{"Music", "Music"},
                                          {"Movies", "TVCinema"},
                                          {"TV", "TVCinema"},
                                          {"Sport", "Sport"},
                                          {"Leisure", "Lifestyle"}};
    QVector<Call> calls;
    int i = 0;
    for (const auto &mode : {QString("1p"), QString("4p"), QString("8p")})
        calls << Call{{"AddGame"},
                      {games[mode], number * 1000 + i++, kinds[mode], maxPlayers[mode], 0, -1, 1, 0, 4}};
    for (const auto &mode : {QString("1p"), QString("4p"), QString("8p")})
        for (const auto &type : plans[mode])
            calls << Call{{"AddGamePlanRounds"}, {games[mode], type, type == "FinalCountdown" ? 8 : 0}};
    int roundId = number * 1000 + 300;
    for (const auto &category : categories)
        for (const auto &mode : {QString("1p"), QString("4p"), QString("8p")}) {
            QSet<QString> seen;
            for (const auto &type : plans[mode]) {
                if (seen.contains(type))
                    continue;
                seen.insert(type);
                calls << Call{{"AddRoundNew"},
                              {roundId++, games[mode], channels.value(category.first, "Knowledge"), type,
                               internalName(number, category.first), dir, category.second}};
            }
        }
    return calls;
}
} // namespace
QVariantMap BuildResult::map() const {
    return {{"error", error},
            {"path", path},
            {"installedPath", installedPath},
            {"backupPath", backupPath},
            {"language", language},
            {"sha256", sha256},
            {"number", number},
            {"questions", questions},
            {"skipped", skipped},
            {"rounds", rounds},
            {"categories", categories},
            {"pictures", pictures},
            {"cancelled", cancelled}};
}
BuildResult buildPack(const BuildRequest &request, const Progress &progress,
                      std::atomic_bool *externalCancel) {
    BuildResult result;
    result.number = request.number;
    std::atomic_bool localCancel{false};
    auto *cancel = externalCancel ? externalCancel : &localCancel;
    bool mediaFailure = false;
    auto report = [&](int percent, const QString &message) {
        if (progress)
            progress(percent, message);
    };
    try {
        checkCancelled(cancel);
        report(1, QCoreApplication::translate("PackBuilder", "Sprawdzanie projektu i szablonu…"));
        if (request.number < 101 || request.number > 9999 || request.categoryBase < 500 ||
            request.categoryBase > 999)
            fail(QCoreApplication::translate("PackBuilder",
                                             "Numer własnej paczki: 101–9999; baza kategorii: 500–999."));
        if (request.outputDirectory.isEmpty() || request.templatePath.isEmpty())
            fail(QCoreApplication::translate("PackBuilder", "Wybierz szablon gry i katalog wyjściowy."));
        Archive archive(request.templatePath);
        const auto code = languageCode(request.document.language);
        if (code.isEmpty())
            fail(QCoreApplication::translate("PackBuilder", "Niepoprawny język projektu."));
        const auto language = languageFolder(archive, code);
        result.language = language;
        const auto gen = language + "/SCRIPTS/GENERATED";
        const auto sourceCategories = validatedCategories(archive, language);
        QMap<int, Category> cats;
        QMap<QString, int> keys;
        for (const auto &c : sourceCategories) {
            cats[c.id] = c;
            keys[c.key] = c.id;
        }
        auto info = readLua(archive.read(gen + "/addpackinfo.clu"));
        const auto dir = directoryName(request.number);
        QMap<QString, QString> games{{"1p", QString("p%1PackGame1p").arg(request.number)},
                                     {"4p", QString("p%1PackGame4p").arg(request.number)},
                                     {"8p", QString("p%1PackGame8p").arg(request.number)}};
        if (replaceConstants(info,
                             {{"PACK0100", dir},
                              {"PackTitlePk100Pack", QString("PackTitlePk%1Pack").arg(request.number)},
                              {"PackDescPk100Pack", QString("PackDescPk%1Pack").arg(request.number)},
                              {"p100PackGame1p", games["1p"]},
                              {"p100PackGame4p", games["4p"]},
                              {"p100PackGame8p", games["8p"]}},
                             100, request.number) != 7)
            fail(QCoreApplication::translate("PackBuilder",
                                             "To nie jest obsługiwany szablon Quiz World PACK0100."));
        QVector<Question> questions;
        QSet<QString> identities;
        QStringList images;
        QSet<int> roots;
        for (auto q : request.document.questions) {
            q.switchLanguage(code);
            if (emptyLanguage(q)) {
                ++result.skipped;
                continue;
            }
            if (identities.contains(q.id))
                fail(QCoreApplication::translate("PackBuilder", "Powtórzony identyfikator pytania: ") + q.id);
            identities.insert(q.id);
            if (!cats.contains(q.subjectId) || cats[q.subjectId].parent)
                q.subjectId = keys.value(q.extra["source_subject_key"].toString());
            if (!cats.contains(q.categoryId) || cats[q.categoryId].parent != q.subjectId)
                q.categoryId = keys.value(q.extra["source_category_key"].toString());
            if (!q.problems().isEmpty())
                fail(QCoreApplication::translate("PackBuilder", "Pytanie %1: %2")
                         .arg(q.id, q.problems().join(" ")));
            if (!cats.contains(q.subjectId) || cats[q.subjectId].parent || !cats.contains(q.categoryId) ||
                cats[q.categoryId].parent != q.subjectId)
                fail(
                    QCoreApplication::translate(
                        "PackBuilder", "Pytanie %1 wymaga mapowania kategorii na wybrany szablon Quiz World.")
                        .arg(q.id));
            const auto media =
                q.translations.isEmpty() ? q.extra["source_media"] : q.localized()["source_media"];
            if (!media.toArray().isEmpty() && q.image.isEmpty())
                fail(QCoreApplication::translate(
                         "PackBuilder",
                         "Pytanie %1 zawiera nieprzekonwertowane media Buzza. Przypisz obrazek przed "
                         "budowaniem.")
                         .arg(q.id));
            roots.insert(q.subjectId);
            if (!q.image.isEmpty() && !images.contains(q.image))
                images << q.image;
            questions << q;
        }
        if (questions.isEmpty())
            fail(QCoreApplication::translate("PackBuilder", "Brak pytań w wybranym języku do zbudowania."));
        auto usedRoots = roots.values();
        std::sort(usedRoots.begin(), usedRoots.end(), [&](int a, int b) {
            return cats[a].round == cats[b].round ? a < b : cats[a].round < cats[b].round;
        });
        QMap<int, int> remap, roundOf;
        QVector<int> categoryOrder;
        int nextCategory = request.categoryBase;
        for (int r : usedRoots) {
            remap[r] = nextCategory++;
            roundOf[r] = roundOf.size() + 1;
            categoryOrder << r;
            QSet<int> children;
            for (const auto &q : questions)
                if (q.subjectId == r)
                    children.insert(q.categoryId);
            auto list = children.values();
            std::sort(list.begin(), list.end());
            for (int c : list) {
                remap[c] = nextCategory++;
                categoryOrder << c;
            }
        }
        if (nextCategory > 1000 || remap.size() > 99)
            fail(QCoreApplication::translate("PackBuilder",
                                             "Kategorie paczki nie mieszczą się w rejestrze 1–999."));
        for (auto id : remap)
            if (cats.contains(id))
                fail(QCoreApplication::translate("PackBuilder", "Baza kategorii koliduje z szablonem gry."));
        if (images.size() > 427)
            fail(QCoreApplication::translate("PackBuilder",
                                             "Paczka może zawierać najwyżej 427 różnych obrazków."));
        const auto ffmpeg = request.ffmpeg.isEmpty() ? defaultFfmpeg() : request.ffmpeg;
        Entries entries;
        if (!images.isEmpty()) {
            report(5, QCoreApplication::translate("PackBuilder", "Tworzenie planszy obrazków…"));
            const auto sheet = language + "/QUESTIONASSETS/VIDEO/pics002.sdi";
            entries << qMakePair(language + "/QUESTIONASSETS/VIDEO/pics001.sdi",
                                 pictureSheet(archive.read(sheet), images, ffmpeg, cancel));
        }
        report(10, QCoreApplication::translate("PackBuilder", "Kodowanie nagrań…"));
        std::vector<QByteArray> audio(questions.size());
        std::atomic_int nextJob{0}, done{0};
        std::mutex errorMutex;
        QString firstError;
        const double gain = request.document.packExtra["speech_gain_db"].toDouble(0);
        if (!std::isfinite(gain) || gain < -24 || gain > 24)
            fail(QCoreApplication::translate("PackBuilder",
                                             "Wzmocnienie nagrań musi mieścić się w zakresie −24…24 dB."));
        const int workers =
            std::min<int>(questions.size(), std::clamp(QThread::idealThreadCount() - 1, 1, 8));
        std::vector<std::future<void>> tasks;
        for (int w = 0; w < workers; ++w)
            tasks.push_back(std::async(std::launch::async, [&] {
                for (;;) {
                    if (cancel->load())
                        return;
                    const int i = nextJob.fetch_add(1);
                    if (i >= questions.size())
                        return;
                    try {
                        if (!questions[i].speech.isEmpty())
                            audio[i] = encodeSpeech(questions[i].speech, ffmpeg, gain, cancel);
                        const int complete = done.fetch_add(1) + 1;
                        report(10 + 65 * complete / questions.size(),
                               QCoreApplication::translate("PackBuilder", "Nagrania: %1 / %2")
                                   .arg(complete)
                                   .arg(questions.size()));
                    } catch (const std::exception &e) {
                        std::lock_guard<std::mutex> lock(errorMutex);
                        if (!cancel->load()) {
                            firstError = QCoreApplication::translate("PackBuilder", "Pytanie %1: %2")
                                             .arg(questions[i].id, QString::fromUtf8(e.what()));
                            mediaFailure = true;
                            cancel->store(true);
                        }
                        return;
                    }
                }
            }));
        for (auto &task : tasks)
            task.get();
        if (!firstError.isEmpty())
            fail(firstError);
        checkCancelled(cancel);
        const auto speechBase = language + "/QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/";
        QMap<QString, quint32> speech;
        bool silence = false;
        for (int i = 0; i < questions.size(); ++i) {
            speech[questions[i].id] = request.number * 100000 + (audio[i].isEmpty() ? 0 : i + 1);
            if (audio[i].isEmpty())
                silence = true;
            else
                entries << qMakePair(
                    speechBase + QString::number(6000000000ll + speech[questions[i].id]) + ".rsf", audio[i]);
        }
        if (silence)
            entries << qMakePair(speechBase + QString::number(6000000000ll + request.number * 100000) +
                                     ".rsf",
                                 silentSpeech());
        report(78, QCoreApplication::translate("PackBuilder", "Zapisywanie pytań i planów rund…"));
        QMap<int, QPair<int, int>> counts;
        QMap<int, QString> files;
        QMap<QString, quint32> ids;
        QVector<Call> packages;
        int nextQuestion = request.number * 100000;
        QSet<QString> filenames;
        for (int root : usedRoots) {
            QString plain = cats[root].name.toLower();
            plain.remove(QRegularExpression("[^a-z]"));
            const auto name = QString("p%1_%2").arg(request.number).arg(plain).left(24);
            if (filenames.contains(name))
                fail(QCoreApplication::translate("PackBuilder", "Powtórzona nazwa pliku rundy."));
            filenames.insert(name);
            files[root] = name;
            QVector<Question> group;
            for (const auto &q : questions)
                if (q.subjectId == root)
                    group << q;
            std::stable_sort(group.begin(), group.end(), [](const Question &a, const Question &b) {
                return !a.image.isEmpty() && b.image.isEmpty();
            });
            QVector<RoundQuestion> round;
            for (const auto &q : group) {
                QStringList answers{q.answers[q.correct]};
                for (int i = 0; i < 4; ++i)
                    if (i != q.correct)
                        answers << q.answers[i];
                const int slot = q.image.isEmpty() ? 0 : images.indexOf(q.image) + 1;
                const auto id = quint32(++nextQuestion);
                ids[q.id] = id;
                round << RoundQuestion{
                    id,          speech[q.id],
                    q.text,      q.topic.isEmpty() ? request.document.packExtra["topic"].toString() : q.topic,
                    answers,     q.difficulty,
                    remap[root], remap[q.categoryId],
                    slot};
                for (int c : {root, q.categoryId}) {
                    if (slot)
                        ++counts[c].first;
                    else
                        ++counts[c].second;
                }
            }
            const auto data =
                writeRound(name, language, request.number, roundOf[root], usedRoots.size(), round);
            const auto base = language + "/ROUNDS/" + name;
            entries << qMakePair(base + ".hdr", data.header) << qMakePair(base + ".hst", data.stream)
                    << qMakePair(base + ".qak", data.little) << qMakePair(base + ".kaq", data.big);
            packages << Call{{"PackageData", "NewPackage"}, {name}}
                     << Call{{"PackageData", "AddRoundFile"}, {roundOf[root], 0, name, name}};
        }
        QVector<Call> categoryCalls;
        QVector<QPair<QString, QString>> roundCategories;
        for (int id : categoryOrder) {
            const auto &c = cats[id];
            const int root = c.parent ? c.parent : id;
            categoryCalls << Call{{"AddQuestionCategoryItem"},
                                  {remap[id], internalName(request.number, c.name), c.key,
                                   c.parent ? remap[c.parent] : 0, counts[id].first, counts[id].second,
                                   request.number, roundOf[root]}};
            roundCategories << qMakePair(c.name, files[root]);
        }
        entries << qMakePair(gen + "/questioncategories.clu", script(categoryCalls));
        entries << qMakePair(gen + "/roundsetup.clu",
                             script(roundSetup(request.number, dir, games, roundCategories)));
        packages.prepend(Call{{"IncludeScript"}, {"Generated/RoundSetup"}});
        entries << qMakePair(gen + "/roundpackages.clu", script(packages))
                << qMakePair(gen + "/addpackinfo.clu", writeLua(info));
        entries << qMakePair(language + "/SCRIPTS/register.clu",
                             script({Call{{"IncludeScript"}, {"Generated/QuestionCategories"}},
                                     Call{{"IncludeScript"}, {"Generated/AddPackInfo"}}}));
        const auto named = language + "/NAMEDTEXT/default";
        auto texts = decodeHuffman(archive.read(named + ".hdr"), archive.read(named + ".hst"));
        QStringList lines;
        QSet<qint32> hashes;
        for (const auto &line :
             QString::fromUtf8(archive.read(named + ".ndx")).remove(QChar(0xfeff)).split('\n'))
            if (!line.trimmed().isEmpty()) {
                lines << line.trimmed();
                hashes.insert(line.simplified().section(' ', 0, 0).toInt());
            }
        const QVector<QPair<QString, QString>> extra{
            {QString("PackTitlePk%1Pack").arg(request.number), request.document.title},
            {QString("PackDescPk%1Pack").arg(request.number),
             request.document.description.isEmpty() ? request.document.title : request.document.description},
            {games["1p"], request.document.title},
            {games["4p"], request.document.title},
            {games["8p"], request.document.title}};
        for (const auto &item : extra) {
            const auto hash = qint32(crc32(item.first.toUtf8()) ^ 0xffffffffu ^ 0xfabcdef7u);
            if (hashes.contains(hash))
                fail(QCoreApplication::translate("PackBuilder",
                                                 "Klucz tekstowy nowej paczki koliduje z szablonem."));
            hashes.insert(hash);
            texts << item.second;
            lines << QString("%1 %2").arg(hash).arg(texts.size());
        }
        const auto encoded = encodeHuffman(texts);
        entries << qMakePair(named + ".hdr", encoded.header) << qMakePair(named + ".hst", encoded.stream)
                << qMakePair(named + ".ndx", namedIndex(lines));
        for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it)
            if (it.key().startsWith(language + "/") && it.key().contains("/DATA/"))
                entries << qMakePair(it.key(), archive.read(it.key()));
        checkCancelled(cancel);
        if (!QDir().mkpath(request.outputDirectory))
            fail(QCoreApplication::translate("PackBuilder", "Nie można utworzyć katalogu wyjściowego."));
        const auto outputRoot =
            QDir::fromNativeSeparators(QFileInfo(request.outputDirectory).canonicalFilePath());
        if (QRegularExpression("/(dev_hdd0|dev_bdvd|PS3_GAME/USRDIR)(/|$)",
                               QRegularExpression::CaseInsensitiveOption)
                .match(outputRoot)
                .hasMatch())
            fail(QCoreApplication::translate(
                "PackBuilder",
                "Buduj poza katalogami danych gry. Do instalacji użyj instalatora z kopią zapasową."));
        const auto folder = QDir(outputRoot).filePath(dir);
        if (!QDir().mkpath(folder))
            fail(QCoreApplication::translate("PackBuilder", "Nie można utworzyć katalogu paczki."));
        const auto path = checkedPath(outputRoot, QDir(folder).filePath(language + dir + ".edat"));
        if (QFileInfo(path).exists() &&
            QFileInfo(path).canonicalFilePath() == QFileInfo(request.templatePath).canonicalFilePath())
            fail(QCoreApplication::translate("PackBuilder", "Katalog wyjściowy nadpisałby szablon gry."));
        QTemporaryDir temporary(QDir(outputRoot).filePath(".studio-build-XXXXXX"));
        if (!temporary.isValid())
            fail(QCoreApplication::translate("PackBuilder", "Nie można utworzyć katalogu roboczego paczki."));
        const auto candidate = temporary.filePath(language + dir + ".edat");
        report(85, QCoreApplication::translate("PackBuilder", "Zapisywanie archiwum EDAT…"));
        writeArchive(candidate, entries);
        report(90, QCoreApplication::translate("PackBuilder", "Walidacja zapisanej paczki…"));
        validateGenerated(candidate, language, questions, ids, request.number, cancel);
        checkCancelled(cancel);
        result.sha256 = digestFile(candidate);
        atomicCopy(candidate, path, result.sha256, cancel);
        result.path = path;
        result.questions = questions.size();
        result.rounds = usedRoots.size();
        result.categories = remap.size();
        result.pictures = images.size();
        if (request.install) {
            const auto installed = installPack(path, request.runtimeRoot, request.titleId, progress, cancel);
            result.installedPath = installed.installedPath;
            result.backupPath = installed.backupPath;
            if (!installed.ok()) {
                result.error = installed.error;
                result.cancelled = installed.cancelled;
                return result;
            }
        }
        report(100, QCoreApplication::translate("PackBuilder", "Paczka gotowa."));
    } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
        result.cancelled = cancel->load() && !mediaFailure;
    }
    return result;
}
BuildResult installPack(const QString &source, const QString &runtimeRoot, const QString &titleId,
                        const Progress &progress, std::atomic_bool *cancel) {
    BuildResult result;
    result.path = source;
    try {
        checkCancelled(cancel);
        if (!QRegularExpression("^(BCES00647|BCES00644|BCES00645|BCUS98209)$").match(titleId).hasMatch())
            fail(QCoreApplication::translate("PackBuilder",
                                             "Wybierz obsługiwany identyfikator gry Quiz World."));
        const auto root = QDir::fromNativeSeparators(QFileInfo(runtimeRoot).canonicalFilePath());
        if (root.isEmpty() || !QDir(QDir(root).filePath("dev_hdd0")).exists())
            fail(QCoreApplication::translate("PackBuilder", "Wybierz katalog runtime zawierający dev_hdd0."));
        QLockFile lock(QDir(root).filePath(".question-studio-install.lock"));
        if (!lock.tryLock(0))
            fail(lock.error() == QLockFile::PermissionError
                     ? QCoreApplication::translate("PackBuilder", "Brak prawa zapisu w wybranym runtime.")
                     : QCoreApplication::translate("PackBuilder", "Inna instalacja paczki jest w toku."));
        const auto match =
            QRegularExpression("^([A-Z]{3})PACK([0-9]{4})\\.edat$").match(QFileInfo(source).fileName());
        if (!match.hasMatch())
            fail(QCoreApplication::translate("PackBuilder",
                                             "Nazwa paczki musi mieć postać LANGPACKnnnn.edat."));
        const auto language = match.captured(1);
        const int number = match.captured(2).toInt();
        if (number < 101)
            fail(QCoreApplication::translate("PackBuilder",
                                             "Instalator obsługuje własne paczki o numerach od 101."));
        result.language = language;
        result.number = number;
        if (progress)
            progress(94, QCoreApplication::translate("PackBuilder",
                                                     "Sprawdzanie kolizji z zainstalowanymi paczkami…"));
        result.sha256 = digestFile(source);
        Archive archive(source);
        const auto categories = validatedCategories(archive, language);
        QSet<int> categoryIds;
        QSet<QString> categoryNames;
        for (const auto &c : categories) {
            if (c.id < 500)
                fail(QCoreApplication::translate("PackBuilder",
                                                 "Własna paczka używa kategorii zarezerwowanych dla gry."));
            categoryIds.insert(c.id);
            categoryNames.insert(c.name);
            if (c.pack != number)
                fail(QCoreApplication::translate("PackBuilder", "Niepoprawny numer paczki w bazie pytań."));
        }
        const auto imported = importBuzz(source, languageCode(language), false);
        if (!imported.ok() || imported.document.questions.isEmpty())
            fail(QCoreApplication::translate("PackBuilder", "Niepoprawna paczka pytań: ") + imported.error);
        for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it) {
            checkCancelled(cancel);
            const auto bytes = archive.read(it.key());
            validateFile(archive, it.key(), bytes, language, number);
        }
        const auto destination = checkedPath(root, QDir(root).filePath("dev_hdd0/game/" + titleId +
                                                                       "/USRDIR/" + directoryName(number) +
                                                                       "/" + QFileInfo(source).fileName()));
        if (QFileInfo(source).canonicalFilePath().compare(QFileInfo(destination).canonicalFilePath(),
                                                          pathCase()) == 0)
            fail(QCoreApplication::translate("PackBuilder", "Paczka znajduje się już w miejscu instalacji."));
        for (const auto &title : QSet<QString>{titleId, "BCES00195"}) {
            const auto usrdir = checkedPath(root, QDir(root).filePath("dev_hdd0/game/" + title + "/USRDIR"));
            for (const auto &folder :
                 QDir(usrdir).entryInfoList({"PACK????"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
                const auto installed = checkedPath(
                    root, QDir(folder.absoluteFilePath()).filePath(language + folder.fileName() + ".edat"));
                if (!QFileInfo::exists(installed) || installed.compare(destination, pathCase()) == 0)
                    continue;
                if (folder.fileName() == directoryName(number))
                    fail(QCoreApplication::translate(
                        "PackBuilder", "Ta paczka jest już zainstalowana w drugim katalogu gry."));
                Archive other(installed);
                const auto otherCategories = validatedCategories(other, language);
                for (const auto &c : otherCategories) {
                    if (categoryNames.contains(c.name))
                        fail(QCoreApplication::translate(
                                 "PackBuilder",
                                 "Kolizja nazw kategorii z paczką %1. Wybierz inny numer paczki.")
                                 .arg(folder.fileName()));
                    if (categoryIds.contains(c.id))
                        fail(QCoreApplication::translate(
                                 "PackBuilder",
                                 "Kolizja kategorii %1 z paczką %2. Zmień bazę kategorii i zbuduj ponownie.")
                                 .arg(c.id)
                                 .arg(folder.fileName()));
                }
            }
        }
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
            fail(QCoreApplication::translate("PackBuilder", "Nie można utworzyć katalogu instalacji."));
        if (QFileInfo::exists(destination)) {
            const auto oldHash = digestFile(destination);
            const auto backups = checkedPath(
                root, QDir(root).filePath("pack-backups/" + titleId + "/" + directoryName(number)));
            if (!QDir().mkpath(backups))
                fail(QCoreApplication::translate("PackBuilder",
                                                 "Nie można utworzyć kopii zapasowej starej paczki."));
            const auto backup = checkedPath(
                root, QDir(backups).filePath(QFileInfo(source).fileName() + "." +
                                             QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") +
                                             "-" + oldHash.left(12) + ".bak"));
            atomicCopy(destination, backup, oldHash, cancel);
            result.backupPath = backup;
        }
        if (progress)
            progress(98, QCoreApplication::translate("PackBuilder", "Instalowanie paczki…"));
        atomicCopy(source, destination, result.sha256, cancel);
        result.installedPath = destination;
        result.questions = imported.document.questions.size();
        result.categories = categories.size();
        if (progress)
            progress(100, QCoreApplication::translate("PackBuilder",
                                                      "Paczka zainstalowana. Uruchom grę ponownie."));
    } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
        result.cancelled = cancel && cancel->load();
    }
    return result;
}
QString defaultFfmpeg() {
#ifdef Q_OS_WIN
    const auto bundled = QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg.exe");
#else
    const auto bundled = QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg");
#endif
    return QFileInfo::exists(bundled) ? bundled : QStandardPaths::findExecutable("ffmpeg");
}
QVariantMap suggestBuildSettings(const QString &language) {
    QSettings settings;
    QVariantMap options{{"number", settings.value("build/number", 103)},
                        {"categoryBase", settings.value("build/categoryBase", 700)},
                        {"templatePath", settings.value("build/templatePath", QString())},
                        {"outputDirectory",
                         settings.value("build/outputDirectory", QDir(QStandardPaths::writableLocation(
                                                                          QStandardPaths::DocumentsLocation))
                                                                     .filePath("BuzzPacks"))},
                        {"runtimeRoot", settings.value("build/runtimeRoot", QString())},
                        {"titleId", settings.value("build/titleId", "BCES00647")},
                        {"ffmpeg", settings.value("build/ffmpeg", defaultFfmpeg())}};
    QDir base(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 4; ++i, base.cdUp()) {
        if (options["runtimeRoot"].toString().isEmpty() && QDir(base.filePath("runtime/dev_hdd0")).exists())
            options["runtimeRoot"] = base.filePath("runtime");
        QFile config(base.filePath("buzz-paths.json"));
        if (!config.open(QIODevice::ReadOnly))
            continue;
        const auto root = QJsonDocument::fromJson(config.readAll()).object()["game_root"].toString();
        const auto folder = QDir(root).filePath("PS3_GAME/USRDIR/PACK0100");
        for (const auto &file : QDir(folder).entryInfoList({"*PACK0100.PAK"}, QDir::Files)) {
            const auto code = languageCode(file.fileName().left(3));
            if (code.section('-', 0, 0) == language.section('-', 0, 0) &&
                (options["templatePath"].toString().isEmpty() ||
                 languageCode(QFileInfo(options["templatePath"].toString()).fileName().left(3))
                         .section('-', 0, 0) != language.section('-', 0, 0)))
                options["templatePath"] = file.absoluteFilePath();
        }
    }
    return options;
}
} // namespace studio
