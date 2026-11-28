#include "pack_builder.h"
#include "media_tools.h"
#include "buzz_import.h"
#include "languages.h"
#include "pack_codec.h"
#include "ps3_source.h"
#include "psp_media.h"
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
#include <algorithm>
#include <cmath>
#include <future>
#include <utility>

namespace studio {
using namespace pack;
namespace {
QString directoryName(int number) { return QString("PACK%1").arg(number, 4, 10, QLatin1Char('0')); }
QString internalName(int number, const QString &name) { return QString("p%1_%2").arg(number).arg(name); }
// "OwnA", "OwnB", … "OwnAA": letters only, as round file names keep only letters.
QString customInternalName(int id) {
    QString letters;
    for (int n = id - firstCustomCategory; n > 0; n /= 26) {
        --n;
        letters.prepend(QChar('A' + n % 26));
    }
    return "Own" + letters;
}
int slotPreference(const QString &slot, const QString &contentCode) {
    const auto code = languageCode(slot);
    if (code == contentCode)
        return 3;
    const auto base = code.section('-', 0, 0);
    if (base == contentCode.section('-', 0, 0))
        return 2;
    return base == "en" ? 1 : 0;
}
QString languageFolder(const Archive &archive, const QString &code) {
    QString result;
    int best = -1;
    for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it) {
        const auto folder = it.key().section('/', 0, 0);
        if (folder.size() != 3 || it.key() != folder + "/SCRIPTS/GENERATED/questioncategories.clu")
            continue;
        const auto preference = slotPreference(folder, code);
        if (preference > best) {
            result = folder;
            best = preference;
        }
    }
    if (result.isEmpty())
        fail(QCoreApplication::translate("PackBuilder", "To nie jest obsługiwany szablon Quiz World PACK0100."));
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
    if (name.endsWith(".rsf") && name.contains("/MUSIC/STREAMED/") && !isStreamedMusic(bytes))
        fail(QCoreApplication::translate("PackBuilder", "Plik klipu muzycznego RSF jest uszkodzony."));
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
                const auto file = type == 1 ? musicEntry(language, u32(bytes, p + 4))
                    : language + "/QUESTIONASSETS/VIDEO/" + (type == 3 ? QString("pics001.sdi")
                              : QString::number(1000000ll + u32(bytes, p + 4)) + ".sdi");
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
bool missingBuildLanguage(const Question &q) {
    if (!q.text.trimmed().isEmpty() || !q.speech.isEmpty() || !q.image.isEmpty() || !q.video.isEmpty() || !q.music.isEmpty() ||
        !q.ttsText.isEmpty() || q.hasUnresolvedSourceMedia())
        return false;
    for (const auto &a : q.answers)
        if (!a.trimmed().isEmpty())
            return false;
    return true;
}
QString buildContentProblem(const Question &question) {
    const auto problems = question.problems();
    if (!problems.isEmpty())
        return problems.join(' ');
    const auto text = QStringList{question.text, question.topic} + question.answers;
    if (std::any_of(text.cbegin(), text.cend(), [](const QString &line) {
            return std::any_of(line.cbegin(), line.cend(), unsupportedPackCharacter);
        }))
        return QCoreApplication::translate("PackBuilder", "Tekst zawiera znak nieobsługiwany przez grę (np. emoji).");
    return {};
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
        if (!q.music.isEmpty() && actual.sourceMusic().isEmpty())
            fail(QCoreApplication::translate("PackBuilder", "W zapisanym pytaniu brakuje odwołania do klipu muzycznego."));
    }
    Q_UNUSED(categories);
}
QVector<Call> roundSetup(int number, const QString &dir, const QMap<QString, QString> &games,
                         const QVector<QPair<QString, QString>> &categories, const RoundPlans &plans) {
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
        for (const auto &round : plans[mode]) {
            // Zero lets Quiz World use the round's rules. Keep the stock pack finale's explicit limit.
            const int count = round.questionCount > 0 ? round.questionCount : (round.type == "FinalCountdown" ? 8 : 0);
            calls << Call{{"AddGamePlanRounds"}, {games[mode], round.type, count}};
        }
    int roundId = number * 1000 + 300;
    for (const auto &category : categories)
        for (const auto &mode : {QString("1p"), QString("4p"), QString("8p")}) {
            QSet<QString> seen;
            for (const auto &round : plans[mode]) {
                const auto &type = round.type;
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
QString &configuredGameDirectory() {
    static QString value;
    return value;
}
// BuzzSetup's dist/runtime holds EBOOT.elf and the disc; the game creates
// dev_hdd0 on its first start.
bool preparedRuntime(const QDir &runtime) {
    return QFileInfo(runtime.filePath("EBOOT.elf")).isFile() && QDir(runtime.filePath("disc/PS3_GAME")).exists();
}
bool usableRuntime(const QDir &runtime) {
    return QDir(runtime.filePath("dev_hdd0")).exists() || preparedRuntime(runtime);
}
bool isSupportedTitleId(const QString &titleId) {
    static const QSet<QString> titleIds{"BCES00647", "BCES00644", "BCES00645", "BCUS98209"};
    return titleIds.contains(titleId);
}
} // namespace
QString templateGameLanguage(const QString &path, const QString &contentLanguage) {
    if (QFileInfo(path).isFile()) {
        try {
            return languageCode(languageFolder(Archive(path), languageCode(contentLanguage)));
        } catch (const std::runtime_error &) {
            // The builder reports the specific archive error when the user builds.
        }
    }
    return {};
}
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
            {"excluded", excluded},
            {"rounds", rounds},
            {"categories", categories},
            {"pictures", pictures},
            {"movies", movies},
            {"cancelled", cancelled}};
}
QString preflightBuildProblem(const Question &question) {
    if (const auto problem = buildContentProblem(question); !problem.isEmpty())
        return problem;
    // The source keys can be resolved against a Quiz World template during the build.
    const bool allowsFallback = !question.extra["category_override"].toBool();
    const bool lacksSubject = question.subjectId <= 0 &&
                              (!allowsFallback || question.extra["source_subject_key"].toString().isEmpty());
    const bool lacksCategory = question.categoryId <= 0 &&
                               (!allowsFallback || question.extra["source_category_key"].toString().isEmpty());
    return lacksSubject || lacksCategory
               ? QCoreApplication::translate("PackBuilder", "Wybierz kategorię i podkategorię dla tego pytania.")
               : QString();
}

BuildReadiness checkBuildReadiness(const Document &document) {
    BuildReadiness result;
    const auto language = languageCode(document.language);
    QSet<QString> identities;
    for (auto question : document.questions) {
        question.switchLanguage(language);
        if (missingBuildLanguage(question)) {
            ++result.missingLanguage;
            continue;
        }
        const auto reason = identities.contains(question.id)
                                ? QCoreApplication::translate("PackBuilder", "Powtórzony identyfikator pytania: ") + question.id
                                : preflightBuildProblem(question);
        if (!reason.isEmpty()) {
            result.issues << BuildQuestionIssue{question.id, question.text, reason};
            continue;
        }
        identities.insert(question.id);
        ++result.ready;
    }
    return result;
}

BuildResult buildPack(const BuildRequest &request, const Progress &progress,
                      std::atomic_bool *externalCancel) {
    BuildResult result;
    result.number = request.number;
    std::atomic_bool localCancel{false};
    auto *cancel = externalCancel ? externalCancel : &localCancel;
    auto report = [&](int percent, const QString &message) {
        if (progress)
            progress(percent, message);
    };
    auto exclude = [&](const Question &question, const QString &reason) {
        if (!request.skipInvalid)
            fail(QCoreApplication::translate("PackBuilder", "Pytanie %1: %2").arg(question.id, reason));
        result.excluded << QVariantMap{{"id", question.id}, {"text", question.text}, {"reason", reason}};
    };
    auto requireQuestions = [&](const QVector<Question> &questions) {
        if (!questions.isEmpty())
            return;
        if (!result.excluded.isEmpty())
            fail(QCoreApplication::translate("PackBuilder", "Żadne pytanie nie jest gotowe do zbudowania paczki."));
        fail(QCoreApplication::translate("PackBuilder", "Brak pytań w wybranym języku do zbudowania."));
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
        QSet<QString> templateNames;
        for (const auto &c : sourceCategories) {
            cats[c.id] = c;
            keys[c.key] = c.id;
            templateNames.insert(c.name);
        }
        // The project's own categories: ASCII internal names, display names in NAMEDTEXT.
        for (const auto &own : request.document.categories) {
            Category c;
            c.id = own.id;
            c.parent = own.parent;
            c.pack = request.number;
            c.round = 1000 + own.id; // After the template's subjects.
            c.name = customInternalName(own.id);
            c.key = QString("CAT_PK%1_OWN%2").arg(request.number).arg(own.id - firstCustomCategory);
            if (!isCustomCategory(own.id) || templateNames.contains(c.name) ||
                (own.parent && !request.document.customCategory(own.parent)))
                fail(QCoreApplication::translate("PackBuilder", "Niepoprawne lub powtórzone kategorie paczki."));
            cats[c.id] = c;
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
        for (auto q : request.document.questions) {
            checkCancelled(cancel);
            q.switchLanguage(code);
            if (missingBuildLanguage(q)) {
                ++result.skipped;
                continue;
            }
            if (identities.contains(q.id)) {
                exclude(q, QCoreApplication::translate("PackBuilder", "Powtórzony identyfikator pytania: ") + q.id);
                continue;
            }
            if (!q.extra["category_override"].toBool()) {
                if (!cats.contains(q.subjectId) || cats[q.subjectId].parent)
                    q.subjectId = keys.value(q.extra["source_subject_key"].toString());
                if (!cats.contains(q.categoryId) || cats[q.categoryId].parent != q.subjectId)
                    q.categoryId = keys.value(q.extra["source_category_key"].toString());
            }
            if (const auto problem = buildContentProblem(q); !problem.isEmpty()) {
                exclude(q, problem);
                continue;
            }
            if (!cats.contains(q.subjectId) || cats[q.subjectId].parent || !cats.contains(q.categoryId) ||
                cats[q.categoryId].parent != q.subjectId) {
                exclude(q, QCoreApplication::translate(
                    "PackBuilder", "Pytanie %1 wymaga mapowania kategorii na wybrany szablon Quiz World.").arg(q.id));
                continue;
            }
            identities.insert(q.id);
            questions << q;
        }
        requireQuestions(questions);

        const auto ffmpeg = request.ffmpeg.isEmpty() ? defaultFfmpeg() : request.ffmpeg;
        const double gain = request.document.packExtra["speech_gain_db"].toDouble(0);
        if (!std::isfinite(gain) || gain < -24 || gain > 24)
            fail(QCoreApplication::translate("PackBuilder",
                                             "Wzmocnienie nagrań musi mieścić się w zakresie −24…24 dB."));
        report(5, QCoreApplication::translate("PackBuilder", "Kodowanie nagrań…"));
        const auto sourceNarrationErrors = recoverPspNarrationsForBuild(questions, cancel);
        if (!sourceNarrationErrors.isEmpty()) {
            questions.removeIf([&](const Question &q) {
                const auto error = sourceNarrationErrors.constFind(q.id);
                if (error == sourceNarrationErrors.constEnd())
                    return false;
                exclude(q, *error);
                return true;
            });
            requireQuestions(questions);
        }
        const auto pending = std::as_const(questions).constData();
        const int questionCount = questions.size();
        std::vector<QByteArray> audio(questionCount);
        std::vector<QString> audioErrors(questionCount);
        std::atomic_int nextJob{0}, done{0};
        std::atomic_bool stopJobs{false};
        const int workers = std::min(questionCount, std::clamp(QThread::idealThreadCount() - 1, 1, 8));
        std::vector<std::future<void>> tasks;
        for (int w = 0; w < workers; ++w)
            tasks.push_back(std::async(std::launch::async, [&] {
                while (!cancel->load() && !stopJobs.load()) {
                    const int i = nextJob.fetch_add(1);
                    if (i >= questionCount)
                        return;
                    try {
                        if (!pending[i].speech.isEmpty())
                            audio[i] = encodeSpeech(pending[i].speech, ffmpeg, gain, cancel);
                    } catch (const MediaEnvironmentError &) {
                        stopJobs.store(true);
                        throw;
                    } catch (const std::runtime_error &e) {
                        checkCancelled(cancel);
                        audioErrors[i] = QString::fromUtf8(e.what());
                        if (!request.skipInvalid)
                            stopJobs.store(true);
                    }
                    const int complete = done.fetch_add(1) + 1;
                    report(5 + 55 * complete / questionCount,
                           QCoreApplication::translate("PackBuilder", "Nagrania: %1 / %2")
                               .arg(complete).arg(questionCount));
                }
            }));
        for (auto &task : tasks)
            task.get();
        checkCancelled(cancel);
        QHash<QString, QByteArray> audioById;
        QVector<Question> withAudio;
        for (int i = 0; i < questionCount; ++i) {
            if (!audioErrors[i].isEmpty()) {
                exclude(questions[i], audioErrors[i]);
                continue;
            }
            if (!audio[i].isEmpty())
                audioById.insert(questions[i].id, std::move(audio[i]));
            withAudio << std::move(questions[i]);
        }
        questions = std::move(withAudio);
        requireQuestions(questions);

        if (!QDir().mkpath(request.outputDirectory))
            fail(QCoreApplication::translate("PackBuilder", "Nie można utworzyć katalogu wyjściowego."));
        const auto outputRoot =
            QDir::fromNativeSeparators(QFileInfo(request.outputDirectory).canonicalFilePath());
        if (outputRoot.isEmpty())
            fail(QCoreApplication::translate("PackBuilder", "Nie można utworzyć katalogu wyjściowego."));
        if (QRegularExpression("/(dev_hdd0|dev_bdvd|PS3_GAME/USRDIR)(/|$)",
                               QRegularExpression::CaseInsensitiveOption)
                .match(outputRoot)
                .hasMatch())
            fail(QCoreApplication::translate(
                "PackBuilder",
                "Buduj poza katalogami danych gry. Do instalacji użyj instalatora z kopią zapasową."));
        QTemporaryDir temporary(QDir(outputRoot).filePath(".studio-build-XXXXXX"));
        if (!temporary.isValid())
            fail(QCoreApplication::translate("PackBuilder", "Nie można utworzyć katalogu roboczego paczki."));
        const auto candidate = temporary.filePath(language + dir + ".edat");
        ArchiveWriter writer(candidate);
        Entries entries;
        QHash<QString, QByteArray> musicClips;
        QHash<QString, QString> musicErrors;
        questions.removeIf([&](const Question &q) {
            if (q.music.isEmpty())
                return false;
            checkCancelled(cancel);
            if (!musicClips.contains(q.music) && !musicErrors.contains(q.music)) {
                try {
                    musicClips.insert(q.music, encodeMusic(q.music, ffmpeg, cancel));
                } catch (const MediaEnvironmentError &) {
                    throw;
                } catch (const std::exception &e) {
                    checkCancelled(cancel);
                    musicErrors.insert(q.music, QString::fromUtf8(e.what()));
                }
            }
            if (musicClips.contains(q.music))
                return false;
            exclude(q, musicErrors.value(q.music));
            return true;
        });
        requireQuestions(questions);
        QStringList moviePaths, movies;
        QSet<QString> seenMovies;
        for (const auto &q : questions) {
            if (!q.video.isEmpty() && !seenMovies.contains(q.video)) {
                seenMovies.insert(q.video);
                moviePaths << q.video;
            }
        }
        auto movieAsset = [&](const QString &path) {
            return FirstMovieAsset + (request.number - 101) * MovieAssetsPerPack + movies.indexOf(path);
        };
        if (!moviePaths.isEmpty()) {
            const auto prefix = language + "/QUESTIONASSETS/VIDEO/";
            QByteArray movieTemplate;
            for (auto it = archive.entries.lowerBound(prefix);
                 it != archive.entries.cend() && it.key().startsWith(prefix); ++it) {
                checkCancelled(cancel);
                if (!it.key().endsWith(".sdi") || !it.key().at(prefix.size()).isDigit())
                    continue;
                auto sourceMovie = archive.read(it.key());
                if (isMovieTemplate(sourceMovie)) {
                    movieTemplate = std::move(sourceMovie);
                    break;
                }
            }
            if (movieTemplate.isEmpty())
                fail(QCoreApplication::translate(
                    "PackBuilder", "Wybrany szablon Quiz World nie zawiera zgodnego nagłówka filmu SDI."));
            QHash<QString, QString> invalidMovies;
            for (qsizetype i = 0; i < moviePaths.size(); ++i) {
                checkCancelled(cancel);
                const auto &path = moviePaths[i];
                const auto rejectMovie = [&](const QString &message) {
                    const auto reason = QFileInfo(path).fileName() + ": " + message;
                    if (!request.skipInvalid)
                        fail(reason);
                    invalidMovies.insert(path, reason);
                };
                if (movies.size() >= MovieAssetsPerPack) {
                    rejectMovie(QCoreApplication::translate("PackBuilder", "Paczka zawiera za dużo różnych filmów do pytań."));
                    continue;
                }
                report(60 + int(12 * i / moviePaths.size()),
                       QCoreApplication::translate("PackBuilder", "Kodowanie filmu do pytania %1 z %2…")
                           .arg(i + 1).arg(moviePaths.size()));
                QByteArray bytes;
                try {
                    bytes = encodeMovie(movieTemplate, path, ffmpeg, cancel);
                } catch (const MediaEnvironmentError &) {
                    throw;
                } catch (const std::runtime_error &error) {
                    checkCancelled(cancel);
                    rejectMovie(QString::fromUtf8(error.what()));
                    continue;
                }
                movies << path;
                writer.add(prefix + QString::number(1000000ll + movieAsset(path)) + ".sdi", bytes);
            }
            questions.removeIf([&](const Question &q) {
                const auto it = invalidMovies.constFind(q.video);
                if (it == invalidMovies.cend())
                    return false;
                exclude(q, it.value());
                return true;
            });
        }
        QStringList images;
        QSet<QString> seenImages;
        for (const auto &q : questions) {
            if (q.video.isEmpty() && !q.image.isEmpty() && !seenImages.contains(q.image)) {
                seenImages.insert(q.image);
                images << q.image;
            }
        }
        if (!images.isEmpty()) {
            report(73, QCoreApplication::translate("PackBuilder", "Tworzenie planszy obrazków…"));
            const auto sheet = language + "/QUESTIONASSETS/VIDEO/pics002.sdi";
            QHash<QString, QString> invalidImages;
            auto bytes = pictureSheet(archive.read(sheet), images, ffmpeg, cancel,
                                      request.skipInvalid ? &invalidImages : nullptr);
            if (!bytes.isEmpty())
                entries << qMakePair(language + "/QUESTIONASSETS/VIDEO/pics001.sdi", std::move(bytes));
            images.removeIf([&](const QString &path) { return invalidImages.contains(path); });
            questions.removeIf([&](const Question &q) {
                const auto it = invalidImages.constFind(q.image);
                if (!q.video.isEmpty() || it == invalidImages.cend())
                    return false;
                exclude(q, it.value());
                return true;
            });
        }
        requireQuestions(questions);
        QSet<int> roots;
        for (const auto &q : questions)
            roots.insert(q.subjectId);
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
        QVector<QPair<QString, QString>> ownTexts;
        for (int id : categoryOrder) {
            if (!isCustomCategory(id))
                continue;
            const auto name = request.document.customCategory(id)->name(code).simplified();
            if (name.isEmpty())
                fail(QCoreApplication::translate("PackBuilder",
                                                 "Kategoria własna nie ma nazwy w języku %1. Uzupełnij ją w "
                                                 "oknie Kategorie paczki.")
                         .arg(languageName(code)));
            ownTexts << qMakePair(cats[id].key, name);
        }
        for (auto id : remap)
            if (cats.contains(id))
                fail(QCoreApplication::translate("PackBuilder", "Baza kategorii koliduje z szablonem gry."));
        const auto speechBase = language + "/QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/";
        QMap<QString, quint32> speech;
        bool silence = false;
        for (int i = 0; i < questions.size(); ++i) {
            const auto &id = questions[i].id;
            const auto recording = audioById.take(id);
            speech[id] = request.number * 100000 + (recording.isEmpty() ? 0 : i + 1);
            if (recording.isEmpty())
                silence = true;
            else
                entries << qMakePair(speechBase + QString::number(6000000000ll + speech[id]) + ".rsf", recording);
        }
        if (silence)
            entries << qMakePair(speechBase + QString::number(6000000000ll + request.number * 100000) +
                                     ".rsf",
                                 silentSpeech());
        QHash<QString, int> musicAssets;
        for (const auto &q : questions) {
            if (q.music.isEmpty() || musicAssets.contains(q.music))
                continue;
            checkCancelled(cancel);
            if (musicAssets.size() >= MusicAssetsPerPack)
                fail(QCoreApplication::translate("PackBuilder", "Paczka zawiera za dużo różnych klipów muzycznych."));
            const int asset = FirstMusicAsset + (request.number - 101) * MusicAssetsPerPack + musicAssets.size();
            musicAssets.insert(q.music, asset);
            entries << qMakePair(musicEntry(language, asset), musicClips.take(q.music));
        }
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
                return (!a.image.isEmpty() || !a.video.isEmpty() || !a.music.isEmpty()) &&
                         b.image.isEmpty() && b.video.isEmpty() && b.music.isEmpty();
            });
            QVector<RoundQuestion> round;
            for (const auto &q : group) {
                QStringList answers{q.answers[q.correct]};
                for (int i = 0; i < 4; ++i)
                    if (i != q.correct)
                        answers << q.answers[i];
                const int movie = q.video.isEmpty() ? 0 : movieAsset(q.video);
                const int slot = movie || q.image.isEmpty() ? 0 : images.indexOf(q.image) + 1;
                const int music = musicAssets.value(q.music);
                const auto id = quint32(++nextQuestion);
                ids[q.id] = id;
                round << RoundQuestion{
                    id,          speech[q.id],
                    q.text,      q.topic.isEmpty() ? request.document.packExtra["topic"].toString() : q.topic,
                    answers,     q.difficulty,
                    remap[root], remap[q.categoryId],
                    slot, movie, music};
                for (int c : {root, q.categoryId}) {
                    if (slot || movie || music)
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
                             script(roundSetup(request.number, dir, games, roundCategories,
                                               request.document.roundPlans())));
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
        QVector<QPair<QString, QString>> extra{
            {QString("PackTitlePk%1Pack").arg(request.number), request.document.title},
            {QString("PackDescPk%1Pack").arg(request.number),
             request.document.description.isEmpty() ? request.document.title : request.document.description},
            {games["1p"], request.document.title},
            {games["4p"], request.document.title},
            {games["8p"], request.document.title}};
        extra << ownTexts;
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
        const auto folder = QDir(outputRoot).filePath(dir);
        if (!QDir().mkpath(folder))
            fail(QCoreApplication::translate("PackBuilder", "Nie można utworzyć katalogu paczki."));
        const auto path = checkedPath(outputRoot, QDir(folder).filePath(language + dir + ".edat"));
        if (QFileInfo(path).exists() &&
            QFileInfo(path).canonicalFilePath() == QFileInfo(request.templatePath).canonicalFilePath())
            fail(QCoreApplication::translate("PackBuilder", "Katalog wyjściowy nadpisałby szablon gry."));
        report(85, QCoreApplication::translate("PackBuilder", "Zapisywanie archiwum EDAT…"));
        for (const auto &entry : entries) {
            checkCancelled(cancel);
            writer.add(entry.first, entry.second);
        }
        writer.commit();
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
        result.movies = movies.size();
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
        result.cancelled = cancel->load();
    }
    return result;
}
BuildResult installPack(const QString &source, const QString &runtimeRoot, const QString &titleId,
                        const Progress &progress, std::atomic_bool *cancel) {
    BuildResult result;
    result.path = source;
    try {
        checkCancelled(cancel);
        if (!isSupportedTitleId(titleId))
            fail(QCoreApplication::translate("PackBuilder",
                                             "Wybierz obsługiwany identyfikator gry Quiz World."));
        const auto root = QDir::fromNativeSeparators(QFileInfo(runtimeRoot).canonicalFilePath());
        if (!root.isEmpty() && !QDir(QDir(root).filePath("dev_hdd0")).exists() && preparedRuntime(QDir(root)))
            QDir(root).mkpath("dev_hdd0"); // A prepared copy that has not been started yet.
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
QString defaultFfmpeg() { return findMediaFfmpeg(); }
void setGameDirectory(const QString &directory) {
    configuredGameDirectory() = directory.isEmpty() ? QString() : QDir(directory).absolutePath();
}
QString gameDirectory() { return configuredGameDirectory(); }
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
    // Forget remembered paths that no longer exist (moved or deleted copies).
    if (!QFileInfo(options["templatePath"].toString()).isFile())
        options["templatePath"] = QString();
    if (!QFileInfo(options["runtimeRoot"].toString()).isDir())
        options["runtimeRoot"] = QString();
    if (!QFileInfo(options["ffmpeg"].toString()).isFile())
        options["ffmpeg"] = defaultFfmpeg();
    const auto contentCode = languageCode(language);
    bool gameTemplateFound = false;
    const auto offerTemplates = [&](const QString &folder, bool fromGame) {
        if (gameTemplateFound)
            return;
        const QDir dir(folder);
        const auto current = options["templatePath"].toString();
        int best = current.isEmpty() || (fromGame && QFileInfo(current).absoluteDir() != dir)
                       ? -1 : slotPreference(QFileInfo(current).fileName().left(3), contentCode);
        for (const auto &file : dir.entryInfoList({"*PACK0100.PAK"}, QDir::Files, QDir::Name)) {
            const auto preference = slotPreference(file.fileName().left(3), contentCode);
            if (preference > best) {
                best = preference;
                options["templatePath"] = file.absoluteFilePath();
            }
            gameTemplateFound = fromGame;
        }
    };
    // The game BuzzSetup started us for wins over remembered settings.
    const auto game = gameDirectory();
    if (!game.isEmpty()) {
        const QDir runtime(QDir(game).filePath("runtime"));
        if (usableRuntime(runtime)) {
            options["runtimeRoot"] = runtime.absolutePath();
            offerTemplates(runtime.filePath("disc/PS3_GAME/USRDIR/PACK0100"), true);
        }
    }
    QDir base(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 4; ++i, base.cdUp()) {
        // Developer checkout (runtime/), prepared copy (dist/runtime) and the
        // release package (build-files/dist/runtime).
        for (const auto &relative : {QStringLiteral("runtime"), QStringLiteral("dist/runtime"),
                                     QStringLiteral("build-files/dist/runtime")}) {
            const QDir runtime(base.filePath(relative));
            if (relative == "runtime" ? !QDir(runtime.filePath("dev_hdd0")).exists() : !preparedRuntime(runtime))
                continue;
            if (options["runtimeRoot"].toString().isEmpty())
                options["runtimeRoot"] = runtime.absolutePath();
            offerTemplates(runtime.filePath("disc/PS3_GAME/USRDIR/PACK0100"), false);
        }
        QFile config(base.filePath("buzz-paths.json"));
        if (!config.open(QIODevice::ReadOnly))
            continue;
        const auto root = QJsonDocument::fromJson(config.readAll()).object()["game_root"].toString();
        if (!root.isEmpty())
            offerTemplates(QDir(root).filePath("PS3_GAME/USRDIR/PACK0100"), false);
    }
    const auto runtimeRoot = options["runtimeRoot"].toString();
    if (!runtimeRoot.isEmpty()) {
        const auto titleId = ps3::readTitleId(QDir(runtimeRoot).filePath("disc/PS3_GAME/PARAM.SFO"));
        if (isSupportedTitleId(titleId))
            options["titleId"] = titleId;
    }
    return options;
}
} // namespace studio
