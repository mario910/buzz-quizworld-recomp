#include "legacy_import.h"
#include "buzz_import.h"
#include "disc_image.h"
#include "media_tools.h"
#include "pack_format.h"
#include "ps2_media.h"
#include "psp_media.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <QStringDecoder>
#include <algorithm>
#include <memory>

namespace studio {
namespace {
using namespace pack;
QString tr(const char *text) { return QCoreApplication::translate("LegacyImport", text); }

QString firstInOrderPrompt(const QString &language) {
    static const QMap<QString, QString> prompts = {
        {"pl", "Co jest pierwsze w tej kolejności?"}, {"en", "Which comes first in this order?"},
        {"cs", "Co je v tomto pořadí první?"}, {"da", "Hvad kommer først i denne rækkefølge?"},
        {"de", "Was kommt in dieser Reihenfolge zuerst?"}, {"el", "Τι έρχεται πρώτο σε αυτή τη σειρά;"},
        {"es", "¿Qué va primero en este orden?"}, {"fi", "Mikä tulee tässä järjestyksessä ensin?"},
        {"fr", "Quel élément vient en premier dans cet ordre ?"}, {"hr", "Što je prvo u ovom redoslijedu?"},
        {"it", "Quale viene per primo in questo ordine?"}, {"nl", "Wat komt eerst in deze volgorde?"},
        {"no", "Hva kommer først i denne rekkefølgen?"}, {"pt", "Qual vem primeiro nesta ordem?"},
        {"ru", "Что идёт первым в этом порядке?"}, {"sv", "Vad kommer först i den här ordningen?"},
    };
    return prompts.value(language.section('-', 0, 0));
}

enum class Kind { Ps2Rounds, Ps2Sports, Ps2Music, PspQuestions, PspBundle, PspMusic };
constexpr qint64 bundleSize = 0x100000;
struct Set {
    Kind kind;
    QString code, primary, text; // Paths inside the disc image, or on disk.
    QString mediaLocale{};
};
struct Category {
    const char *name;
    int subject = 0, category = 0; // Quiz World subject/subcategory, 0 = map in the editor.
};

// "INE"/"INF" are the PS2/PS3 international English/French folders.
QString folderLanguage(const QString &folder) {
    static const QHash<QString, QString> names = {
        {"INE", "en"},        {"INF", "fr"},       {"UK", "en"},         {"CZR", "cs"},
        {"AUS", "en-AU"},     {"HOL", "nl"},        {"AUSTRALIAN", "en-AU"}, {"ENGLISHUS", "en-US"},
        {"ENGLISHINT", "en"}, {"ENGLISHUK", "en-GB"}, {"IRISH", "en-IE"},
        {"POLISH", "pl"},     {"ENGLISH", "en"},   {"FRENCH", "fr"},
        {"GERMAN", "de"},     {"ITALIAN", "it"},   {"SPANISH", "es"},    {"DUTCH", "nl"},    {"PORTUGUESE", "pt"},
        {"SWEDISH", "sv"},    {"DANISH", "da"},    {"NORWEGIAN", "no"},  {"FINNISH", "fi"},  {"RUSSIAN", "ru"},
        {"CROATIAN", "hr"},   {"CZECH", "cs"},     {"GREEK", "el"},      {"HUNGARIAN", "hu"}};
    const auto upper = folder.toUpper();
    return names.contains(upper) ? names[upper] : languageCode(folder);
}

// PSP regional SKUs name their folders SKU_LOCALE0/1: tell the language from the text.
QString detectLanguage(const QString &text) {
    int polish = 0, croatian = 0, cyrillic = 0, czech = 0, greek = 0, letters = 0;
    for (const auto c : text) {
        letters += c.isLetter();
        const auto u = c.toLower().unicode();
        if (QStringLiteral("ąęłńśźż").contains(QChar(u)))
            ++polish;
        else if (QStringLiteral("čćđš").contains(QChar(u)))
            ++croatian;
        else if (QStringLiteral("řěůť").contains(QChar(u)))
            ++czech;
        else if (u >= 0x400 && u <= 0x4ff)
            ++cyrillic;
        else if (u >= 0x370 && u <= 0x3ff)
            ++greek;
    }
    // Polish/Croatian text has a few percent of these letters; English has none.
    const int best = std::max({polish, croatian, cyrillic, czech, greek});
    if (best == 0 || best * 200 < letters)
        return QStringLiteral("en");
    return best == polish     ? QStringLiteral("pl")
           : best == croatian ? QStringLiteral("hr")
           : best == cyrillic ? QStringLiteral("ru")
           : best == czech    ? QStringLiteral("cs")
                              : QStringLiteral("el");
}

QStringList textLines(const QByteArray &bytes) {
    auto text = QString::fromUtf8(bytes);
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return text.split('\n');
}

// One line of a PS2 .adt table: UTF-8, except a few leftover Windows-1252 lines.
QString tableLine(const QByteArray &line) {
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(line);
    if (!utf8.hasError())
        return text;
    static const char16_t cp1252[32] = {0x20ac, 0xfffd, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                                        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0xfffd, 0x017d, 0xfffd,
                                        0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0xfffd, 0x017e, 0x0178};
    QString out;
    for (const auto c : line) {
        const auto b = quint8(c);
        out += b >= 0x80 && b < 0xa0 ? QChar(cp1252[b - 0x80]) : QChar(char16_t(b));
    }
    return out;
}

// PS2 .adt tables (Hollywood Quiz and later): a UTF-8 or UTF-16 TSV with the
// header "Id, Resource, Question, Right Answer, Wrong Answer x3".
QVector<QStringList> tableRows(const QByteArray &bytes) {
    QList<QByteArray> lines;
    if (bytes.startsWith("\xff\xfe")) {
        const auto text = QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData() + 2),
                                             (bytes.size() - 2) / 2);
        for (const auto &line : text.split(QRegularExpression("\r\n|\r|\n")))
            lines << line.toUtf8();
    } else {
        auto normalized = bytes;
        normalized.replace("\r\n", "\n").replace('\r', '\n');
        lines = normalized.split('\n');
    }
    QVector<QStringList> rows;
    for (const auto &line : lines)
        if (!line.trimmed().isEmpty())
            rows << tableLine(line).split('\t');
    return rows;
}

QString sfoValue(const QByteArray &sfo, const QString &key) {
    if (sfo.size() < 20 || !sfo.startsWith(QByteArray("\0PSF", 4)))
        return {};
    const auto keys = u32(sfo, 8), values = u32(sfo, 12), count = u32(sfo, 16);
    for (quint32 i = 0; i < count && i < 256; ++i) {
        const auto entry = 20 + qint64(i) * 16;
        check(sfo, entry, 16);
        const auto name = sfo.mid(keys + u16(sfo, entry), 32);
        if (QString::fromLatin1(name.left(name.indexOf('\0'))) != key)
            continue;
        const auto value = sfo.mid(values + u32(sfo, entry + 12), u32(sfo, entry + 4));
        return QString::fromUtf8(value.left(value.indexOf('\0') < 0 ? value.size() : value.indexOf('\0')))
            .simplified();
    }
    return {};
}

QString ps2Serial(const QByteArray &systemCnf) {
    static const QRegularExpression boot("([A-Z]{4})_([0-9]{3})\\.([0-9]{2})");
    const auto match = boot.match(QString::fromLatin1(systemCnf).toUpper());
    return match.hasMatch() ? match.captured(1) + '-' + match.captured(2) + match.captured(3) : QString();
}

QString ps2Title(const QString &serial) {
    static const QHash<QString, QString> titles = {
        {"SCES-53304", "Buzz! The Music Quiz"}, {"SCES-53663", "Buzz! The Music Quiz"},
        {"SCES-53880", "Buzz! The Big Quiz"}, {"SCES-53925", "Buzz! The Big Quiz"},
        {"SCES-54258", "Buzz! The Sports Quiz"}, {"SCES-54496", "Buzz! The Mega Quiz"},
        {"SCES-54505", "Buzz! The Mega Quiz"}, {"SCUS-97592", "Buzz! The Mega Quiz"},
        {"SCES-54690", "Buzz! Mega Quiz"}, {"SCES-54852", "Buzz! Hollywood Leffavisa"},
        {"SCES-54856", "Buzz! Hollywood Quiz"}, {"SCES-54941", "Buzz! The Schools Quiz"},
        {"SCES-55093", "Buzz! The Pop Quiz"}, {"SCES-55098", "Buzz! The Pop Quiz"}};
    return titles.value(serial, QStringLiteral("Buzz! (PS2)"));
}

// The category byte of a PS2 round record (checked against the qpp* tables).
Category ps2Category(int value) {
    switch (value) {
    case 17: return {QT_TRANSLATE_NOOP("LegacyImport", "Muzyka"), 175, 224};
    case 18: return {QT_TRANSLATE_NOOP("LegacyImport", "Film"), 199, 208};
    case 19: return {QT_TRANSLATE_NOOP("LegacyImport", "Telewizja"), 257, 271};
    case 20: return {QT_TRANSLATE_NOOP("LegacyImport", "Ciekawostki"), 188, 191};
    case 21: return {QT_TRANSLATE_NOOP("LegacyImport", "Zwierzęta"), 235, 240};
    case 22: return {QT_TRANSLATE_NOOP("LegacyImport", "Sport"), 174, 247};
    case 24: return {QT_TRANSLATE_NOOP("LegacyImport", "Historia"), 182, 184};
    case 25: return {QT_TRANSLATE_NOOP("LegacyImport", "Świat"), 250, 253};
    case 26: return {QT_TRANSLATE_NOOP("LegacyImport", "Nauka"), 235, 237};
    case 27: return {QT_TRANSLATE_NOOP("LegacyImport", "Geografia i przyroda"), 250, 253};
    case 28: return {QT_TRANSLATE_NOOP("LegacyImport", "Sztuka i rozrywka"), 177, 178};
    case 80: return {QT_TRANSLATE_NOOP("LegacyImport", "Gwiazdy"), 230, 232};
    default:
        if (value >= 30 && value <= 79)
            return {QT_TRANSLATE_NOOP("LegacyImport", "Kraje świata"), 250, 253};
        return {QT_TRANSLATE_NOOP("LegacyImport", "Wiedza ogólna"), 0, 0};
    }
}

Category pspCategory(int value) {
    switch (value) {
    case 335: return {QT_TRANSLATE_NOOP("LegacyImport", "Film"), 199, 208};
    case 336: return {QT_TRANSLATE_NOOP("LegacyImport", "Telewizja"), 257, 271};
    case 337: return {QT_TRANSLATE_NOOP("LegacyImport", "Świat i historia"), 250, 253};
    case 338: return {QT_TRANSLATE_NOOP("LegacyImport", "Przyroda"), 235, 240};
    case 339: return {QT_TRANSLATE_NOOP("LegacyImport", "Nauka i technika"), 235, 236};
    case 340: return {QT_TRANSLATE_NOOP("LegacyImport", "Sport"), 174, 247};
    case 341: return {QT_TRANSLATE_NOOP("LegacyImport", "Gwiazdy"), 230, 232};
    case 343: return {QT_TRANSLATE_NOOP("LegacyImport", "Muzyka"), 175, 224};
    case 344: return {QT_TRANSLATE_NOOP("LegacyImport", "Czas wolny"), 188, 191};
    default: return {QT_TRANSLATE_NOOP("LegacyImport", "Wiedza ogólna"), 0, 0};
    }
}

// Text lines of a Quiz World PSP bundle: UTF-8, CRLF, padded with 0x55 bytes.
QStringList bundleLines(const QByteArray &data, qint64 base) {
    auto text = data.mid(base + 0x60000, bundleSize - 0x60000);
    if (const auto end = text.indexOf(QByteArray(8, 'U')); end >= 0)
        text.truncate(end);
    if (const auto end = text.indexOf('\0'); end >= 0)
        text.truncate(end);
    return textLines(text);
}

// Top-level category hashes of Buzz! Quiz World PSP (named from their questions).
Category bundleCategory(quint32 hash) {
    switch (hash) {
    case 0x6e9f7a1d: return {QT_TRANSLATE_NOOP("LegacyImport", "Sztuka i kultura"), 177, 178};
    case 0x757e79d2: return {QT_TRANSLATE_NOOP("LegacyImport", "Film"), 199, 208};
    case 0x8e801ef7: return {QT_TRANSLATE_NOOP("LegacyImport", "Muzyka"), 175, 224};
    case 0xa4fb6732: return {QT_TRANSLATE_NOOP("LegacyImport", "Świat"), 250, 253};
    case 0xb9373ab9: return {QT_TRANSLATE_NOOP("LegacyImport", "Gwiazdy"), 230, 232};
    case 0xc372a3bc: return {QT_TRANSLATE_NOOP("LegacyImport", "Sport"), 174, 247};
    case 0xd9410840: return {QT_TRANSLATE_NOOP("LegacyImport", "Aktualności"), 182, 183};
    case 0xd94a20d8: return {QT_TRANSLATE_NOOP("LegacyImport", "Nauka i przyroda"), 235, 237};
    case 0xee33b90a: return {QT_TRANSLATE_NOOP("LegacyImport", "Telewizja"), 257, 271};
    case 0xf0bf4a04: return {QT_TRANSLATE_NOOP("LegacyImport", "Czas wolny"), 188, 191};
    default: return {QT_TRANSLATE_NOOP("LegacyImport", "Wiedza ogólna"), 0, 0};
    }
}

class Source {
  public:
    QString path, game, serial;
    QVector<Set> sets;
    explicit Source(const QString &file) : path(QFileInfo(file).absoluteFilePath()) {
        if (DiscImage::isDiscImage(path)) {
            disc = std::make_unique<DiscImage>(path);
            scanDisc();
        } else if (QFileInfo(path).fileName().compare("MERGED.QDB", Qt::CaseInsensitive) == 0) {
            scanQuestionFile();
        } else {
            scanRoundArchive();
        }
        // One set per language: the first one found wins.
        QVector<Set> unique;
        QSet<QString> codes;
        for (const auto &set : sets)
            if (!set.code.isEmpty() && !codes.contains(set.code)) {
                codes.insert(set.code);
                unique << set;
            }
        sets = unique;
        if (game.isEmpty())
            game = QStringLiteral("Buzz!");
        if (sets.isEmpty())
            fail(tr("Nie znaleziono pytań Buzz! w wybranym pliku."));
    }
    QByteArray read(const QString &file) {
        if (disc)
            return disc->read(file, 64 * 1024 * 1024);
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            fail(f.errorString());
        if (f.size() > 64 * 1024 * 1024)
            fail(tr("Plik pytań jest za duży."));
        return f.readAll();
    }
    bool hasMedia(const QString &archive, const QString &entry) {
        auto entries = mediaEntries.constFind(archive);
        if (entries == mediaEntries.constEnd()) {
            QSet<QString> available;
            try {
                const auto pack = openPs2Archive(path, archive);
                available = QSet<QString>(pack->entries.keyBegin(), pack->entries.keyEnd());
            } catch (const std::exception &) {
                // Its references stay marked missing. Keep importing the question text.
            }
            entries = mediaEntries.insert(archive, available);
        }
        return entries->contains(entry);
    }
    QString pspFile(const QString &qdb, const QString &localPath, const QString &mergedPath) const {
        const auto parts = qdb.toUpper().split('/');
        if (disc) {
            const int local = parts.indexOf("QUESTION_DATA");
            const int merged = parts.indexOf("QUIZDATA_MERGED");
            const int prefix = local >= 0 ? local : merged;
            if (prefix < 0)
                return {};
            const auto entry = parts.mid(0, prefix).join('/') + '/' + (local >= 0 ? localPath : mergedPath);
            return disc->contains(entry) ? entry : QString();
        }
        const QDir dir = QFileInfo(qdb).dir();
        const bool local = dir.dirName().compare("QUESTION_DATA", Qt::CaseInsensitive) == 0;
        const auto entry = QDir::cleanPath(dir.absoluteFilePath(local ? "../" + localPath : "../../" + mergedPath));
        if (QFileInfo(entry).isFile())
            return entry;
        auto lower = entry;
        lower = lower.left(lower.lastIndexOf('.') + 1) + lower.section('.', -1).toLower();
        return QFileInfo(lower).isFile() ? lower : QString();
    }
    QJsonObject pspRef(const QString &type, const QString &entry, std::optional<quint32> hash = {}) const {
        QJsonObject ref{{"type", type}};
        if (!entry.isEmpty()) {
            ref["archive"] = path;
            ref["entry"] = entry;
            if (hash)
                ref["psp_hash"] = double(*hash);
        }
        return ref;
    }
    QJsonObject hashedPsp(const QString &name, const QString &type, bool ultimate) const {
        if (!disc)
            return pspRef(type, {});
        const auto hash = pspPathHash(name, ultimate);
        const auto ext = name.section('.', -1);
        if (assets.contains(hash))
            return pspRef(type, name, hash);
        const auto file = QStringLiteral("PSP_GAME/USRDIR/%1.%2").arg(hash, 8, 16, QLatin1Char('0')).arg(ext);
        return pspRef(type, disc->contains(file) ? file.toUpper() : QString());
    }
    QJsonObject pspSpeech(const Set &set, quint32 id) const {
        const auto number = QString::number(id);
        if (set.kind == Kind::PspBundle)
            return hashedPsp(QStringLiteral("QuestionSpeech/%1/%2.vag").arg(set.mediaLocale)
                             .arg(id, 5, 10, QLatin1Char('0')), "psp-narration", false);
        if (set.kind == Kind::PspMusic)
            return hashedPsp(QStringLiteral("QuestionSpeech/%1/sfx/%2.sfx").arg(set.mediaLocale)
                             .arg(id, 6, 10, QLatin1Char('0')), "psp-narration", true);
        const auto folder = set.primary.toUpper().section('/', -2, -2);
        const auto prefix = folder == "POLISH" ? "POL" : folder == "ENGLISHUS" ? "USA" : "GBR";
        const auto merged = "COMMENTARY/" + folder + "/O_" + prefix + number + ".VAG";
        auto file = pspFile(set.primary, "COMMENTARY/" + number + ".VAG", merged);
        if (file.isEmpty() && folder == "ENGLISHUS")
            file = pspFile(set.primary, {}, "COMMENTARY/ENGLISHUS/O_GBR" + number + ".VAG");
        return pspRef("psp-narration", file);
    }

  private:
    std::unique_ptr<DiscImage> disc;
    PspAssetIndex assets;
    QHash<QString, QSet<QString>> mediaEntries;

    QString bundleLocale(const QByteArray &data, qint64 table, const QString &fallback) const {
        static const QStringList locales{"English", "EnglishUS", "EnglishInt", "Australian", "Polish", "French",
                                         "German", "Italian", "Spanish", "Dutch", "Portuguese", "Danish",
                                         "Norwegian", "Swedish", "Finnish", "Greek", "Russian", "Czech"};
        QHash<QString, int> score;
        const auto count = u32(data, table + 8);
        for (quint32 i = 0; i < count && i < 48 && table + 48 + qint64(i) * 32 <= data.size(); ++i) {
            const auto id = u32(data, table + 16 + qint64(i) * 32);
            for (const auto &name : locales) {
                const auto entry = QStringLiteral("QuestionSpeech/%1/%2.vag").arg(name).arg(id, 5, 10, QLatin1Char('0'));
                if (assets.contains(pspPathHash(entry)))
                    ++score[name];
            }
        }
        // IDs repeat between languages. Use the question text's language and
        // use the speech index to distinguish English regional names.
        if (fallback != "en")
            return QLocale::languageToString(QLocale(fallback).language());
        if (serial == "NPUG80293")
            return QStringLiteral("EnglishUS");
        if (serial == "UCES01293")
            return table < bundleSize && score.value("Australian") ? QStringLiteral("Australian")
                                                                  : QStringLiteral("EnglishInt");
        return QStringLiteral("English");
    }

    void scanDisc() {
        static const QRegularExpression ps2("^(?:(?:[A-Z]{2,3}|LOCALE)/)?([A-Z]{2,3})ROUND\\.PAK$"),
            merged("(?:^|/)QUIZDATA_MERGED/([A-Z]+)/MERGED\\.QDB$"),
            locale("(?:^|/)SKU_LOCALE[0-9]+/QUESTION_DATA/MERGED\\.QDB$");
        const auto &files = disc->files();
        if (files.contains("SYSTEM.CNF")) {
            serial = ps2Serial(disc->read("SYSTEM.CNF", 4096));
            game = ps2Title(serial);
        }
        if (files.contains("PSP_GAME/PARAM.SFO")) {
            const auto sfo = disc->read("PSP_GAME/PARAM.SFO", 64 * 1024);
            game = sfoValue(sfo, "TITLE");
            serial = sfoValue(sfo, "DISC_ID");
            assets = readPspAssetIndex(*disc);
        }
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const auto name = it.key();
            if (const auto m = ps2.match(name); m.hasMatch()) {
                const auto text = name.chopped(9) + "TEXT.PAK";
                const auto kind = files.contains(text) && files.contains("QVIDEOS.PAK") ? Kind::Ps2Sports
                                  : files.contains(text) && files.contains("SONGCLIP.PAK") &&
                                        (serial == "SCES-53304" || serial == "SCES-53663" ||
                                         !std::any_of(files.keyBegin(), files.keyEnd(), [](const QString &entry) {
                                             return entry.endsWith("/VIDEOS.PAK");
                                         })) ? Kind::Ps2Music : Kind::Ps2Rounds;
                sets << Set{kind, folderLanguage(m.captured(1)), name, text};
            }
            const auto text = name.left(name.size() - 3) + "TXT";
            if (const auto m = merged.match(name); m.hasMatch() && files.contains(text))
                sets << Set{Kind::PspQuestions, folderLanguage(m.captured(1)), name, text};
            else if (locale.match(name).hasMatch() && files.contains(text)) {
                const auto descriptor = name.section('/', 0, -3) + "/LOCALE.TXT";
                auto code = files.contains(descriptor) ? folderLanguage(QString::fromUtf8(disc->read(descriptor, 1024)).trimmed())
                                                      : QString();
                if (code.isEmpty())
                    code = detectLanguage(QString::fromUtf8(disc->read(text)));
                sets << Set{Kind::PspQuestions, code, name, text};
            }
        }
        static const QRegularExpression music(R"(^PSP_GAME/USRDIR/QUESTIONDATA/BM_([A-Z]+)\.QDB$)");
        if (game.contains("Ultimate Music Quiz", Qt::CaseInsensitive))
            for (auto it = files.cbegin(); it != files.cend(); ++it) {
                const auto m = music.match(it.key());
                const auto text = it.key().chopped(3) + "TXT";
                if (!m.hasMatch() || !files.contains(text) || m.captured(1) == "ENGLISHINT")
                    continue;
                const auto name = m.captured(1);
                const Set set{Kind::PspMusic, folderLanguage(name), it.key(), text, name};
                // UMD regions contain shared text tables for other releases.
                // Only the languages pressed on this disc have a speech bank.
                const auto qdb = disc->read(it.key());
                if (!qdb.startsWith(QByteArray("\0BDQ", 4)) || qdb.size() < 16)
                    continue;
                const auto records = std::min<qint64>(u32(qdb, 8), (qdb.size() - 16) / 32);
                for (qint64 i = 0; i < records; ++i)
                    if (pspSpeech(set, u32(qdb, 16 + 32 * i)).contains("entry")) {
                        sets << set;
                        break;
                    }
            }
        if (!sets.isEmpty())
            return;
        if (files.contains("PS3_GAME/PARAM.SFO"))
            fail(tr("To obraz gry PS3. Otwórz plik PACKnnnn/*.PAK z folderu PS3_GAME/USRDIR rozpakowanej gry."));
        // Buzz! Quiz World PSP keeps one 1 MB bundle per language in a numbered
        // .FLZ file: the question table at +0x18000, its text lines at +0x60000.
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            if (!it.key().endsWith(".FLZ") || it->size > 16 * 1024 * 1024)
                continue;
            const auto data = disc->read(it.key());
            for (qint64 base = 0; base + 0x60000 < data.size(); base += bundleSize)
                if (data.mid(base + 0x18000, 4) == QByteArray("\0BDQ", 4)) {
                    const auto language = detectLanguage(bundleLines(data, base).join('\n'));
                    const auto mediaLocale = bundleLocale(data, base + 0x18000, language);
                    sets << Set{Kind::PspBundle, folderLanguage(mediaLocale), it.key(), QString::number(base), mediaLocale};
                }
        }
        if (!sets.isEmpty())
            return;
        if (std::any_of(files.keyBegin(), files.keyEnd(), [](const QString &f) { return f.endsWith(".FLZ"); }))
            fail(tr("Nie znaleziono pytań w archiwach FLZ tego obrazu."));
        if (std::any_of(files.keyBegin(), files.keyEnd(),
                        [](const QString &f) { return f.contains("/PAKS/GAMES/"); }))
            fail(tr("Ta gra zawiera łamigłówki, a nie pytania quizowe — nie ma czego importować."));
    }

  public:
    QHash<quint32, QString> pspMusicCategoryNames(const Set &set) {
        const auto categoryFile = set.primary.chopped(3) + "CTG";
        const auto stringFile = "PSP_GAME/USRDIR/STRING/" + set.mediaLocale + ".STR";
        if (!disc || !disc->contains(categoryFile) || !disc->contains(stringFile))
            return {};
        const auto categories = disc->read(categoryFile, 1024 * 1024);
        const auto strings = disc->read(stringFile, 4 * 1024 * 1024);
        if (categories.size() < 4 || strings.size() < 20 || !strings.startsWith("STR "))
            return {};
        // STR records give the offset of a UTF-16 label by the hash of its string key.
        const qint64 index = 20 + qint64(u32(strings, 8)) * 4;
        const qint64 labels = index + qint64(u32(strings, 4)) * 16;
        if (labels > strings.size())
            return {};
        QHash<quint32, qint64> labelOffsets;
        for (qint64 p = index; p < labels; p += 16)
            labelOffsets.insert(u32(strings, p + 4), labels + u32(strings, p + 8));
        const auto utf16 = [](const QByteArray &data, qint64 &at) {
            const auto start = at;
            for (; at + 2 <= data.size(); at += 2)
                if (u16(data, at) == 0) {
                    const auto bytes = QByteArrayView(data).sliced(start, at - start);
                    at += 2;
                    return QString(QStringDecoder(QStringDecoder::Utf16LE)(bytes));
                }
            return QString();
        };
        QHash<quint32, QString> names;
        qint64 at = 4;
        for (quint32 i = 0, count = u32(categories, 0); i < count && at + 12 <= categories.size(); ++i) {
            const auto key = utf16(categories, at);
            utf16(categories, at); // Optional secondary label.
            if (at + 8 > categories.size())
                break;
            const auto id = u32(categories, at);
            at += 8;
            const auto label = labelOffsets.constFind(pspStringHash(key.toLatin1()));
            if (label != labelOffsets.constEnd()) {
                auto offset = *label;
                const auto name = utf16(strings, offset);
                if (!name.isEmpty())
                    names.insert(id, name);
            }
        }
        return names;
    }

  private:

    void scanRoundArchive() {
        const QFileInfo info(path);
        static const QRegularExpression name("^([A-Za-z]{2,3})ROUND(?:_[A-Za-z0-9]+)?\\.PAK$",
                                             QRegularExpression::CaseInsensitiveOption);
        const auto m = name.match(info.fileName());
        if (!m.hasMatch())
            fail(tr("Wybierz plik <JĘZYK>ROUND.PAK z płyty PS2."));
        const auto text = info.dir().absoluteFilePath(m.captured(1) + "TEXT.PAK");
        const auto kind = QFileInfo::exists(text) && QFileInfo::exists(info.dir().absoluteFilePath("../QVIDEOS.PAK"))
                              ? Kind::Ps2Sports
                          : QFileInfo::exists(text) &&
                                    (QFileInfo::exists(info.dir().absoluteFilePath("../SONGCLIP.PAK")) ||
                                     QFileInfo::exists(info.dir().absoluteFilePath("SONGCLIP.PAK"))) &&
                                    (m.captured(1).size() == 2 || info.dir().dirName() == "CZR")
                              ? Kind::Ps2Music : Kind::Ps2Rounds;
        sets << Set{kind, folderLanguage(m.captured(1)), path, text};
        QFile cnf(info.dir().absoluteFilePath("../SYSTEM.CNF"));
        if (cnf.open(QIODevice::ReadOnly))
            serial = ps2Serial(cnf.read(4096));
        game = ps2Title(serial);
    }

    void scanQuestionFile() {
        const QFileInfo info(path);
        const auto text = info.dir().absoluteFilePath(info.completeBaseName() + ".TXT");
        const auto lower = info.dir().absoluteFilePath(info.completeBaseName() + ".txt");
        const auto txt = QFileInfo::exists(text) ? text : lower;
        if (!QFileInfo::exists(txt))
            fail(tr("Brak pliku MERGED.TXT obok MERGED.QDB."));
        auto code = folderLanguage(info.dir().dirName());
        if (code.isEmpty() || code.size() > 5)
            code = detectLanguage(QString::fromUtf8(read(txt)));
        sets << Set{Kind::PspQuestions, code, path, txt};
        for (QDir dir = info.dir(); dir.cdUp();) {
            QFile sfo(dir.absoluteFilePath("PSP_GAME/PARAM.SFO"));
            if (sfo.open(QIODevice::ReadOnly)) {
                const auto bytes = sfo.read(64 * 1024);
                game = sfoValue(bytes, "TITLE");
                serial = sfoValue(bytes, "DISC_ID");
                break;
            }
        }
        if (game.isEmpty())
            game = QStringLiteral("Buzz! (PSP)");
    }
};

struct Collector {
    LoadResult out;
    QHash<QString, int> byText;
    int adapted = 0, unsupported = 0, media = 0, otherLanguage = 0, correctedPictures = 0;
    // The games repeat questions across rounds (and picture/video variants share
    // text tables): keep one copy. A copy with a media reference wins: such text
    // ("which country makes this drink?") needs its picture, even where another
    // table of the game lost the reference (media id 0x7FFF).
    bool add(Question q, bool hasMedia) {
        const auto key = (q.text + '\n' + q.answers.join('\n')).simplified().toCaseFolded();
        if (const auto it = byText.constFind(key); it != byText.constEnd()) {
            ++out.duplicates;
            auto &existing = out.document.questions[*it];
            if (hasMedia && (!existing.extra.contains("source_media") ||
                             (hasPs2Media(q) && !hasPs2Media(existing)) ||
                             (hasPs2Music(q) && !hasPs2Music(existing)))) {
                media += !existing.extra.contains("source_media");
                existing = q;
            }
            return false;
        }
        media += hasMedia;
        byText.insert(key, out.document.questions.size());
        out.document.questions << q;
        return true;
    }
};

Question makeQuestion(const QString &id, const QString &code, const QString &game, const QString &text,
                      const QStringList &answers, int difficulty, const Category &category) {
    auto q = newQuestion();
    q.id = id;
    q.language = code;
    q.source = game;
    q.text = text.trimmed();
    q.answers.clear();
    for (const auto &a : answers)
        q.answers << a.trimmed();
    q.correct = 0; // These games store the correct answer first.
    q.difficulty = std::clamp(difficulty, 1, 3);
    q.category = tr(category.name);
    q.subjectId = category.subject;
    q.categoryId = category.category;
    q.extra["source_category_path"] = QJsonArray{q.category};
    q.extra["source_section_key"] = QString::fromUtf8(category.name);
    return q;
}

void finish(Question &q, const QJsonObject &extra, const QJsonArray &media) {
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        q.extra[it.key()] = it.value();
    if (!media.isEmpty())
        q.extra["source_media"] = media;
    auto localized = q.localized();
    if (!media.isEmpty())
        localized["source_media"] = media;
    q.translations[q.language] = localized;
}

QString archiveEntry(const Archive &archive, const QString &suffix) {
    const auto entry = std::find_if(archive.entries.keyBegin(), archive.entries.keyEnd(), [&](const QString &name) {
        return name.endsWith(suffix, Qt::CaseInsensitive);
    });
    return entry == archive.entries.keyEnd() ? QString() : *entry;
}

QJsonObject ps2Music(const QString &source, const QString &asset, const QString &pack = QStringLiteral("SONGCLIP.PAK")) {
    return {{"type", "ps2-audio"}, {"archive", source}, {"pack", pack}, {"entry", "Sound/" + asset + ".vgp"}};
}

void checkMediaReference(Source &source, QJsonObject &ref) {
    // A loose round file can be opened before its media archives are copied.
    if (!DiscImage::isDiscImage(source.path))
        return;
    const auto container = ref["container"].toString();
    if (!container.isEmpty() && !source.hasMedia(container.section(':', 0, 0), container.section(':', 1))) {
        ref.remove("container");
        ref["missing"] = true;
    }
    const auto entry = ref["entry"].toString();
    if (!entry.isEmpty() && !source.hasMedia(ref["pack"].toString(), entry)) {
        ref.remove("entry");
        ref["missing"] = true;
    }
}

void importEarlyPs2(Source &source, const Set &set, Collector &c) {
    Archive rounds(source.read(set.primary));
    Archive locale(source.read(set.text));
    const auto rawText = locale.read(archiveEntry(locale, "/quid.str"));
    const auto rri = rounds.read(archiveEntry(rounds, "/rri.dat"));
    const bool sports = set.kind == Kind::Ps2Sports;
    const auto lines = sports ? QStringList() : textLines(rawText);
    const auto count = u32(rri, 4);
    const int stride = sports ? 136 : 12;
    check(rri, 8, qint64(count) * stride);
    const auto textAt = [&](quint32 position) {
        if (!sports)
            return position > 0 && position <= lines.size() ? lines[position - 1] : QString();
        if (position == 0 || position >= rawText.size() || rawText[position - 1] != '\0')
            return QString();
        const auto end = rawText.indexOf('\0', position);
        return end >= 0 ? QString::fromUtf8(rawText.constData() + position, end - position) : QString();
    };
    const auto code = set.primary.section('/', -1).toUpper().section("ROUND", 0, 0).toLower();
    const auto speechPak = "Q-" + code.toUpper() + ".PAK";
    for (const auto &entry : rounds.entries.keys()) {
        const auto table = QFileInfo(entry).completeBaseName();
        if (!entry.endsWith(".rnd", Qt::CaseInsensitive) ||
            (sports && table != "qall_e" && table != "qall_h") || (!sports && table != "qall"))
            continue;
        const auto data = rounds.read(entry);
        const int width = sports ? 28 : 16;
        for (qsizetype pos = 0; pos + width <= data.size(); pos += width) {
            QString title;
            QStringList answers;
            const auto id = sports ? u16(data, pos + 24) : u16(data, pos);
            const auto mediaId = u16(data, pos + 2);
            if (sports) {
                const int banks = u16(data, pos + 16);
                title = textAt(u16(data, pos + 4) + quint32(banks >> 12) * 65536);
                for (int i = 0; i < 4; ++i)
                    answers << textAt(u16(data, pos + 8 + i * 2) + quint32((banks >> (i * 3)) & 7) * 65536);
            } else {
                title = textAt(u16(data, pos + 4));
                for (int i = 0; i < 4; ++i)
                    answers << textAt(u16(data, pos + 8 + i * 2));
            }
            if (title.trimmed().isEmpty() || std::any_of(answers.cbegin(), answers.cend(), [](const QString &answer) {
                    return answer.trimmed().isEmpty();
                })) {
                ++c.unsupported;
                continue;
            }
            auto q = makeQuestion(QStringLiteral("buzz-ps2:%1:%2:%3").arg(source.serial, set.code).arg(id),
                                  set.code, source.game, title, answers, table.endsWith("_h") ? 3 : 2,
                                  ps2Category(sports ? 22 : 17));
            if (!adaptImportedAnswers(q, {0})) {
                ++c.unsupported;
                continue;
            }
            QJsonArray media;
            if (mediaId < count && (sports ? mediaId > 1 : mediaId != 0x7fff)) {
                const auto offset = 8 + qint64(mediaId) * stride;
                const auto name = QString::fromLatin1(rri.constData() + offset + 4,
                                                       sports ? 32 : 8).section(QChar::Null, 0, 0);
                if (sports) {
                    const auto photo = "questiondata/" + name.toLower();
                    const auto clip = "questionvideos/" + name.toLower() + ".ipu";
                    const auto type = u16(rri, offset + 132);
                    if (source.hasMedia("QIMAGES.PAK", photo) && u16(rri, offset + 134) == 1)
                        media << QJsonObject{{"type", "ps2-picture"}, {"archive", source.path},
                                             {"container", "QIMAGES.PAK:" + photo}};
                    else if (source.hasMedia("QVIDEOS.PAK", clip))
                        media << QJsonObject{{"type", "ps2-video"}, {"archive", source.path},
                                             {"container", "QVIDEOS.PAK:" + clip}};
                    else if (type == 1 || type == 2) {
                        const auto pack = type == 1 ? QStringLiteral("STREAMED.PAK") : speechPak;
                        const auto parts = u16(rri, offset + 134);
                        QStringList entries;
                        for (int i = 0; i < parts && i < 4; ++i) {
                            const auto stem = QString::fromLatin1(rri.constData() + offset + 4 + i * 32, 32)
                                                  .section(QChar::Null, 0, 0).toLower();
                            const auto part = "sound/" + stem + ".vgp";
                            if (source.hasMedia(pack, part))
                                entries << part;
                        }
                        if (!entries.isEmpty() && entries.size() == parts) {
                            const auto first = entries.takeFirst();
                            media << QJsonObject{{"type", "ps2-audio"}, {"archive", source.path}, {"pack", pack},
                                                 {"entry", first}, {"additional_entries", QJsonArray::fromStringList(entries)}};
                        } else
                            media << QJsonObject{{"type", "ps2-audio"}, {"missing", true}};
                    } else
                        media << QJsonObject{{"type", type == 9 ? "ps2-video" : "ps2-picture"}, {"missing", true}};
                } else {
                    auto ref = ps2Music(source.path, name);
                    checkMediaReference(source, ref);
                    media << ref;
                }
            }
            finish(q, {{"source_archive", source.path}, {"source_round", table}, {"source_question_id", id}}, media);
            if (sports) {
                const auto voice = QStringLiteral("sound/q%1%2.vgp").arg(id).arg(code);
                if (source.hasMedia(speechPak, voice)) {
                    auto localized = q.localized();
                    localized["source_speech"] = QJsonObject{{"archive", source.path}, {"pack", speechPak}, {"entry", voice}};
                    q.translations[q.language] = localized;
                }
            }
            c.add(q, !media.isEmpty());
        }
    }
}

// Some localized discs keep a few untranslated tables (e.g. English on the
// Polish Hollywood Quiz). Only languages with distinctive letters are checked.
bool otherLanguage(const QStringList &texts, const QString &code) {
    static const QSet<QString> detectable = {"pl", "hr", "ru", "cs", "el"};
    const auto text = texts.join('\n');
    const auto letters = std::count_if(text.begin(), text.end(), [](QChar c) { return c.isLetter(); });
    return detectable.contains(code) && letters >= 200 && detectLanguage(text) != code;
}

// QAK v1000: resolve the question ID through the per-question media link,
// not through the ADT resource number (which is not a sheet ordinal).
QHash<quint32, QJsonObject> qakMedia(const QByteArray &data, const QString &folder, const QString &source) {
    QHash<quint32, QJsonObject> out;
    if (data.size() < 16 || u16(data, 0) != 0xfffe || u32(data, 4) != 1000 || u32(data, 8) < 3)
        return out;
    const qint64 table = u32(data, 12);
    check(data, table, 24);
    const qint64 media = u32(data, table), questions = u32(data, table + 8), links = u32(data, table + 16);
    const int mediaCount = u16(data, table + 4), questionCount = u16(data, table + 12),
              linkCount = u16(data, table + 20);
    if (u16(data, table + 6) != 0 || u16(data, table + 14) != 1 || u16(data, table + 22) != 2 ||
        !mediaCount || !questionCount || !linkCount || media < table + 24 ||
        questions - media != qint64(mediaCount) * 20 || links <= questions)
        return out;
    const auto stride = (links - questions) / questionCount;
    if ((stride != 32 && stride != 36) || links - questions != stride * questionCount)
        return out;
    check(data, links, qint64(linkCount) * 4);
    for (int i = 0; i < questionCount; ++i) {
        const auto record = questions + i * stride;
        const auto link = u16(data, record + 12);
        if (link >= linkCount)
            continue;
        const auto index = u16(data, links + link * 4);
        if (index >= mediaCount)
            continue;
        const auto m = media + index * 20;
        const int type = quint8(data[m + 16]);
        QJsonObject ref{{"asset", double(u32(data, m + 4))}};
        if (type == 3) {
            ref["type"] = "ps2-picture";
            ref["frame"] = u16(data, m + 14);
            ref["container"] = folder + "/VIDEOS.PAK:Videos/pics001.rsv";
        } else if (type == 2) {
            ref["type"] = "ps2-video";
            ref["container"] = folder + "/VIDEOS.PAK:Videos/" + QString::number(1000000ull + u32(data, m + 4)) + ".rsv";
        } else if (type == 1) {
            ref = ps2Music(source, QString::number(1000000ull + u32(data, m + 4)));
            ref["asset"] = double(u32(data, m + 4));
        } else {
            continue;
        }
        out[u32(data, record)] = ref;
    }
    return out;
}

QHash<quint32, QJsonObject> bigQuizMedia(const QByteArray &rri, const QString &folder, const QString &source,
                                       const QString &speechPack) {
    QHash<quint32, QJsonObject> resources;
    if (rri.size() < 4 || !folder.startsWith("BU1/") || qint64(u32(rri, 0)) * 6 != rri.size() - 4)
        return resources;
    for (qsizetype pos = 4; pos + 6 <= rri.size(); pos += 6) {
        const auto id = 1000000u + u16(rri, pos);
        const int type = u16(rri, pos + 4) & 15, frame = u16(rri, pos + 2);
        const auto key = QString::number(id);
        QJsonObject ref;
        if (type == 3)
            ref = {{"type", "ps2-picture"}, {"frame", frame}, {"container", folder + "/VIDEOS.PAK:Videos/pics001.rsv"}};
        else if (type == 2)
            ref = {{"type", "ps2-video"}, {"container", folder + "/VIDEOS.PAK:Videos/" + key + ".rsv"}};
        else if (type == 1)
            ref = ps2Music(source, key);
        else if (type == 4)
            ref = ps2Music(source, QString::number(200000000 + u16(rri, pos) * 100 + 1), speechPack);
        else
            continue;
        resources[id] = ref;
    }
    return resources;
}

void importTable(Source &source, const Set &set, Collector &c, const QString &table, const QByteArray &bytes,
                 const Category &category, const QHash<quint32, QJsonObject> &resources,
                 const QHash<quint32, QJsonObject> &mediaByAsset) {
    const auto rows = tableRows(bytes);
    if (rows.isEmpty() || rows[0].value(0).trimmed() != "Id" || rows[0].size() < 4)
        return; // Not a question table (e.g. namedCharMap).
    const auto lower = table.toLower();
    const bool special = lower.contains("factfiction") || lower.contains("toprank") || lower.contains("compare");
    const auto answerColumn = rows[0].value(3).trimmed().toLower();
    const bool ordered = lower.contains("toprank") &&
        (answerColumn == "first" || answerColumn == "1st" || answerColumn == "first answer");
    const auto prompt = ordered ? firstInOrderPrompt(set.code) : QString();
    if (special && prompt.isEmpty() && answerColumn != "right answer" && answerColumn != "correct answer") {
        c.unsupported += rows.size() - 1;
        return;
    }
    if (!special && rows[0].size() < 7)
        return;
    QStringList all;
    for (int r = 1; r < rows.size(); ++r)
        all << rows[r].mid(2, 5).join('\n');
    if (otherLanguage(all, set.code)) {
        c.otherLanguage += rows.size() - 1;
        return;
    }
    const int difficulty = lower.endsWith("_easy") ? 1 : lower.endsWith("_hard") ? 3 : 2;
    for (int r = 1; r < rows.size(); ++r) {
        const auto &row = rows[r];
        if (row.size() < 5 || row.size() > 7 || (!special && row.size() != 7) || row[2].trimmed().isEmpty()) {
            ++c.unsupported;
            continue;
        }
        const auto id = row[0].trimmed();
        auto q = makeQuestion(QStringLiteral("buzz-ps2:%1:%2:%3")
                                  .arg(source.serial.isEmpty() ? QStringLiteral("disc") : source.serial, set.code, id),
                              set.code, source.game, row[2], row.mid(3, 4), difficulty, category);
        if (special)
            while (!q.answers.isEmpty() && q.answers.last().isEmpty())
                q.answers.removeLast();
        if (std::any_of(q.answers.begin(), q.answers.end(), [](const QString &answer) { return answer.isEmpty(); }) ||
            !adaptImportedAnswers(q, {0})) {
            ++c.unsupported;
            continue;
        }
        if (ordered)
            q.text += '\n' + prompt;
        QJsonArray media;
        const auto resource = row[1].trimmed();
        if (!resource.isEmpty() && resource.compare("non-asset", Qt::CaseInsensitive) != 0) {
            auto ref = resources.value(id.toUInt());
            if (ref.isEmpty() || ref["asset"].toDouble(-1) != resource.toDouble()) {
                ref = mediaByAsset.value(resource.toUInt());
                if (ref.isEmpty()) {
                    const auto kind = lower.contains("_vid") ? "ps2-video"
                                      : lower.contains("_music") ? "ps2-audio" : "ps2-picture";
                    ref = QJsonObject{{"type", kind}, {"asset", resource.toInt()}};
                }
            }
            if (ref.contains("container"))
                ref["archive"] = source.path;
            checkMediaReference(source, ref);
            media << ref;
        }
        finish(q, {{"source_archive", source.path}, {"source_round", table}, {"source_question_id", id}}, media);
        if (c.add(q, !media.isEmpty()) && special)
            ++c.adapted;
    }
}

void importPs2(Source &source, const Set &set, Collector &c) {
    Archive rounds(source.read(set.primary));
    QSet<QString> tables;
    // Hollywood Quiz is entirely about film; Mega Quiz tables carry a category byte.
    const bool hollywood = std::any_of(rounds.entries.keyBegin(), rounds.entries.keyEnd(),
                                       [](const QString &name) { return name.startsWith("BHW/"); });
    // rri.dat maps a media id to its frame in VIDEOS.PAK (u32 count, then
    // 4 x u16 per id; the second value is the frame).
    QByteArray rri;
    QString rriName;
    for (auto it = rounds.entries.cbegin(); it != rounds.entries.cend(); ++it)
        if (it.key().endsWith("/rri.dat", Qt::CaseInsensitive)) {
            rriName = it.key();
            rri = rounds.read(rriName);
            break;
        }
    // The Polish Buzz! Mega Quiz (SCES-54690) points 675 pictures two or three
    // frames too early; the same correction as the PCSX2 patch v0.4
    // (_analysis/pcsx2_patch; every rri.dat entry was reviewed against the
    // decoded questions and RSV frames, _analysis/picture_tail_audit).
    // Only the unmodified table (by CRC) is corrected.
    const bool fixPolishPictures = rri.size() >= 4 && crc32(rri) == 0xe3ab8dacu;
    const auto mediaRecord = [&](int mediaId) -> qint64 {
        const qint64 offset = 4 + qint64(mediaId) * 8;
        return rri.size() >= 4 && mediaId >= 0 && quint32(mediaId) < u32(rri, 0) &&
                       offset + 8 <= rri.size()
                   ? offset : -1;
    };
    const auto pictureFrame = [&](int mediaId, bool &corrected) {
        corrected = false;
        const auto offset = mediaRecord(mediaId);
        if (offset < 0)
            return -1;
        int frame = u16(rri, offset + 2);
        // {first media id, last media id, frames to add}: 653 x +2, 22 x +3.
        static const int ranges[][3] = {
            {817, 819, 2},   {851, 882, 2},   {927, 941, 2},   {947, 959, 2},   {2239, 2241, 2},
            {2244, 2247, 2}, {2249, 2252, 2}, {2508, 2547, 2}, {2549, 2596, 2}, {2601, 2612, 2},
            {2614, 2832, 2}, {2834, 2902, 2}, {3668, 3777, 2}, {3873, 3950, 2}, {3952, 3954, 2},
            {3955, 3966, 3}, {3972, 3972, 3}, {3974, 3982, 3}};
        if (fixPolishPictures)
            for (const auto &range : ranges)
                if (mediaId >= range[0] && mediaId <= range[1]) {
                    frame += range[2];
                    corrected = true;
                }
        return frame;
    };
    const auto folder = rriName.section('/', 0, 1);
    auto locale = QFileInfo(set.primary).baseName().toUpper().section("ROUND", 0, 0);
    if (locale == "INE" || locale == "INF")
        locale = locale == "INE" ? "UK" : "FRA";
    const auto indexedMedia = bigQuizMedia(rri, folder, source.path, locale + '/' + locale + "SPEAK.PAK");
    const auto videos = folder + "/VIDEOS.PAK:Videos/";
    for (auto it = rounds.entries.cbegin(); it != rounds.entries.cend(); ++it) {
        if (it.key().endsWith(".adt", Qt::CaseInsensitive)) {
            const auto table = QFileInfo(it.key()).completeBaseName();
            if (!tables.contains(table)) {
                tables.insert(table);
                QHash<quint32, QJsonObject> resources;
                const auto qak = it.key().left(it.key().size() - 3) + "qak";
                if (rounds.entries.contains(qak)) {
                    try {
                        resources = qakMedia(rounds.read(qak), it.key().section('/', 0, 1), source.path);
                    } catch (const std::exception &e) {
                        c.out.warnings << qak + ": " + QString::fromUtf8(e.what());
                    }
                }
                importTable(source, set, c, table, rounds.read(it.key()),
                            ps2Category(hollywood ? 18 : it.key().startsWith("msc2/", Qt::CaseInsensitive) ? 17 : 0),
                            resources, indexedMedia);
            }
            continue;
        }
        if (!it.key().endsWith(".pkg", Qt::CaseInsensitive))
            continue;
        Archive package(rounds.read(it.key()));
        for (auto e = package.entries.cbegin(); e != package.entries.cend(); ++e) {
            if (!e.key().endsWith(".rnd", Qt::CaseInsensitive))
                continue;
            const auto stem = e.key().left(e.key().size() - 4);
            const auto table = QFileInfo(stem).fileName();
            if (tables.contains(table) || !package.entries.contains(stem + ".hdr") ||
                !package.entries.contains(stem + ".hst"))
                continue;
            tables.insert(table);
            const auto records = package.read(e.key());
            const auto roundName = table.toLower();
            const bool special = roundName == "qcompare" || roundName == "qfactfiction";
            if (roundName == "qorderaat" || (special && records.size() % 28 != 0)) {
                c.unsupported += records.size() / 28;
                continue;
            }
            const auto strings = decodeHuffman(package.read(stem + ".hdr"), package.read(stem + ".hst"));
            const auto prefix = table.section('_', 0, 0).toLower();
            const int difficulty = prefix == "qe" || prefix == "qppe"   ? 1
                                   : prefix == "qh" || prefix == "qpph" ? 3
                                                                        : 2;
            QStringList tableTexts;
            for (int r = 0; r + 28 <= records.size(); r += 28) {
                const int ref = u16(records, r + 18);
                if (ref >= 1 && ref <= strings.size())
                    tableTexts << strings[ref - 1];
            }
            if (otherLanguage(tableTexts, set.code)) {
                c.otherLanguage += records.size() / 28;
                continue;
            }
            const auto kind = table.endsWith("_vid", Qt::CaseInsensitive) ? "ps2-video"
                              : table.contains("music", Qt::CaseInsensitive) ? "ps2-audio"
                                                                             : "ps2-picture";
            const bool mediaRound = table.endsWith("_pic", Qt::CaseInsensitive) ||
                                    table.endsWith("_vid", Qt::CaseInsensitive) ||
                                    table.endsWith("_music", Qt::CaseInsensitive);
            for (int r = 0; r + 28 <= records.size(); r += 28) {
                QStringList text;
                for (int i = 0; i < 5; ++i) {
                    const int ref = u16(records, r + 18 + i * 2);
                    text << (ref >= 1 && ref <= strings.size() ? strings[ref - 1].trimmed() : QString());
                }
                if (special)
                    while (text.size() > 1 && text.last().isEmpty())
                        text.removeLast();
                if (text.size() < 3 || std::any_of(text.begin(), text.end(), [](const QString &s) { return s.isEmpty(); })) {
                    ++c.unsupported;
                    continue;
                }
                const int mediaId = u16(records, r + 2);
                auto q = makeQuestion(QStringLiteral("buzz-ps2:%1:%2:%3:%4")
                                          .arg(source.serial.isEmpty() ? QStringLiteral("disc") : source.serial,
                                               set.code, table)
                                          .arg(r / 28),
                                      set.code, source.game, text[0], text.mid(1), difficulty,
                                      ps2Category(quint8(records[r + 8])));
                if (!adaptImportedAnswers(q, {0})) {
                    ++c.unsupported;
                    continue;
                }
                QJsonArray media;
                if (mediaId != 0x7fff) {
                    QJsonObject reference{{"type", kind}, {"asset", mediaId}};
                    bool corrected = false;
                    if (QString(kind) == "ps2-picture")
                        if (const int frame = pictureFrame(mediaId, corrected); frame >= 0) {
                            reference["frame"] = frame;
                            reference["container"] = videos + "pics001.rsv";
                            if (corrected)
                                reference["frame_corrected"] = true;
                        }
                    if (QString(kind) == "ps2-video") {
                        const auto offset = mediaRecord(mediaId);
                        if (offset >= 0 && u16(rri, offset + 6) == 0) {
                            const auto clipId = u16(rri, offset);
                            if (clipId != 0 && clipId != 0xffff)
                                reference["container"] = videos + QString::number(1000000 + clipId) + ".rsv";
                        }
                    }
                    if (QString(kind) == "ps2-audio") {
                        const auto offset = mediaRecord(mediaId);
                        if (offset >= 0 && (u16(rri, offset + 4) & 15) == 1)
                            reference = ps2Music(source.path, QString::number(1000000 + u16(rri, offset)));
                    }
                    if (reference.contains("container"))
                        reference["archive"] = source.path;
                    checkMediaReference(source, reference);
                    media << reference;
                } else if (mediaRound) {
                    // A picture/video/music round whose record lost its media id
                    // (e.g. all of qppe_Movies_pic): the text still refers to it.
                    media << QJsonObject{{"type", kind}, {"missing", true}};
                }
                finish(q, {{"source_archive", source.path}, {"source_round", table}, {"source_question_id", r / 28}},
                       media);
                if (c.add(q, !media.isEmpty()) && special)
                    ++c.adapted;
            }
        }
    }
}

// Quiz World PSP records (32 bytes): id, picture hash (all ones = none),
// subcategory and category hashes, question + 4 answers (line numbers, correct
// first), a reserved word, media flags and (difficulty - 1) << 4.
void finishPsp(Question &q, const Source &source, const Set &set, quint32 id, const QJsonArray &media) {
    finish(q, {{"source_archive", source.path}, {"source_question_id", double(id)}}, media);
    const auto narration = source.pspSpeech(set, id);
    if (narration.contains("entry")) {
        auto localized = q.localized();
        localized["source_speech"] = narration;
        q.translations[q.language] = localized;
    }
}

void importPspBundle(Source &source, const Set &set, Collector &c) {
    const auto data = source.read(set.primary);
    const bool ultimate = set.kind == Kind::PspMusic;
    const qint64 base = ultimate ? 0 : set.text.toLongLong();
    const auto lines = ultimate ? textLines(source.read(set.text)) : bundleLines(data, base);
    const qint64 table = ultimate ? 0 : base + 0x18000;
    if (data.mid(table, 4) != QByteArray("\0BDQ", 4))
        fail(tr("Nieobsługiwany format MERGED.QDB."));
    const auto count = u32(data, table + 8);
    check(data, table + 16, qint64(count) * 32);
    const auto categories = ultimate ? source.pspMusicCategoryNames(set) : QHash<quint32, QString>{};
    for (quint32 i = 0; i < count; ++i) {
        const qint64 p = table + 16 + qint64(i) * 32;
        const auto category = u32(data, p + 12);
        if (category == 0x539151f3) { // Party challenges ("who can...") without answers.
            ++c.unsupported;
            continue;
        }
        QStringList text;
        for (int k = 0; k < 5; ++k) {
            const int ref = u16(data, p + 16 + k * 2);
            text << (ref < lines.size() ? lines[ref].trimmed() : QString());
        }
        if (std::any_of(text.begin(), text.end(), [](const QString &s) { return s.isEmpty(); })) {
            ++c.unsupported;
            continue;
        }
        const auto id = u32(data, p), picture = u32(data, p + 4);
        const int flags = u16(data, p + 28);
        auto q = makeQuestion(QStringLiteral("buzz-psp:%1:%2:%3")
                                  .arg(source.serial.isEmpty() ? QStringLiteral("disc") : source.serial, set.code)
                                  .arg(id),
                              set.code, source.game, text[0], text.mid(1), (quint8(data[p + 30]) >> 4) + 1,
                              ultimate ? ps2Category(17) : bundleCategory(category));
        const auto label = categories.constFind(category);
        if (label != categories.constEnd()) {
            q.extra["source_category_path"] = QJsonArray{q.category, *label};
            q.extra["source_section_key"] = QStringLiteral("psp-music:%1").arg(category, 8, 16, QLatin1Char('0'));
            q.category = *label;
        }
        QJsonArray media;
        if (flags || picture != 0xffffffffu) {
            const auto kind = flags & 0xf000;
            const auto type = kind == 0x2000 ? "psp-video" : kind == 0x1000 ? "psp-audio" : "psp-picture";
            const auto ext = kind == 0x2000 ? (ultimate ? "fmv" : "pmf")
                                           : kind == 0x1000 ? (ultimate ? "sfx" : "vag") : "tex";
            const auto folder = !ultimate ? "" : kind == 0x2000 ? "movie/" : kind == 0x1000 ? "sfx/" : "";
            const auto entry = QStringLiteral("%1%2.%3").arg(folder).arg(picture, 8, 16, QLatin1Char('0')).arg(ext);
            const auto global = QStringLiteral("QuestionAssets/Global/") + entry;
            auto ref = source.hashedPsp(global, type, ultimate);
            if (ultimate || (flags & 0x40)) {
                const auto local = source.hashedPsp("QuestionAssets/" + set.mediaLocale + '/' + entry, type, ultimate);
                if (!ultimate || local.contains("entry"))
                    ref = local;
            }
            ref["asset"] = QString::number(picture, 16);
            media << ref;
        }
        finishPsp(q, source, set, id, media);
        c.add(q, !media.isEmpty());
    }
}

void importPsp(Source &source, const Set &set, Collector &c) {
    const auto qdb = source.read(set.primary);
    const auto lines = textLines(source.read(set.text));
    if (!qdb.startsWith(QByteArray("\0BDQ", 4)))
        fail(tr("Nieobsługiwany format MERGED.QDB."));
    const auto count = u32(qdb, 8);
    check(qdb, 16, qint64(count) * 24);
    const auto orderPrompt = firstInOrderPrompt(set.code);
    for (quint32 i = 0; i < count; ++i) {
        const int p = 16 + int(i) * 24;
        const int category = u16(qdb, p + 6), flags = u16(qdb, p + 20), level = quint8(qdb[p + 22]);
        const bool ordered = category == 345;
        // The ordering rows store the answers in order and use mode 3.
        // Party challenges (346) have no answer key.
        if (category == 346 || (ordered && (quint8(qdb[p + 23]) != 3 || orderPrompt.isEmpty()))) {
            ++c.unsupported;
            continue;
        }
        QStringList text;
        for (int k = 0; k < 5; ++k) {
            const int ref = u16(qdb, p + 8 + k * 2);
            text << (ref < lines.size() ? lines[ref].trimmed() : QString());
        }
        if (std::any_of(text.begin(), text.end(), [](const QString &s) { return s.isEmpty(); })) {
            ++c.unsupported;
            continue;
        }
        const auto id = u32(qdb, p);
        auto q = makeQuestion(
            QStringLiteral("buzz-psp:%1:%2:%3")
                .arg(source.serial.isEmpty() ? QStringLiteral("disc") : source.serial, set.code)
                .arg(id),
            set.code, source.game, ordered ? text[0] + '\n' + orderPrompt : text[0], text.mid(1), level >> 4,
            pspCategory(category));
        QJsonArray media;
        if (flags) {
            const auto asset = u16(qdb, p + 4);
            const int kind = flags & 0xf000;
            const auto ext = kind == 0x2000 ? "PMF" : kind == 0x3000 ? "TEX" : "VAG";
            const auto name = QStringLiteral("%1.%2").arg(asset, 8, 10, QLatin1Char('0')).arg(ext);
            const auto entry = source.pspFile(set.primary, "QUESTION_ASSETS/" + name, "QUIZASSETS/" + name);
            auto ref = source.pspRef(kind == 0x2000 ? "psp-video" : kind == 0x3000 ? "psp-picture" : "psp-audio",
                                     entry);
            ref["asset"] = int(asset);
            media << ref;
        }
        finishPsp(q, source, set, id, media);
        if (c.add(q, !media.isEmpty()) && ordered)
            ++c.adapted;
    }
}
} // namespace

bool isLegacySource(const QString &path) {
    const auto name = QFileInfo(path).fileName();
    static const QRegularExpression round("^[A-Za-z]{2,3}ROUND(?:_[A-Za-z0-9]+)?\\.PAK$",
                                          QRegularExpression::CaseInsensitiveOption);
    return DiscImage::isDiscImage(path) || name.compare("MERGED.QDB", Qt::CaseInsensitive) == 0 ||
           round.match(name).hasMatch();
}

SourceLanguages legacyLanguages(const QString &path) {
    SourceLanguages result;
    try {
        Source source(path);
        for (const auto &set : source.sets)
            result.codes << set.code;
        result.codes.sort();
        result.preferred = preferredLanguage(result.codes);
    } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
    }
    return result;
}

LoadResult importLegacy(const QString &path, const QString &language) {
    Collector c;
    try {
        Source source(path);
        QStringList codes;
        for (const auto &set : source.sets)
            codes << set.code;
        const auto selected = language.isEmpty() ? preferredLanguage(codes) : languageCode(language);
        const auto set = std::find_if(source.sets.cbegin(), source.sets.cend(),
                                      [&](const Set &s) { return s.code == selected; });
        if (set == source.sets.cend())
            fail(QCoreApplication::translate("BuzzImport", "Źródło nie zawiera wybranego języka."));
        c.out.document.title = source.game;
        c.out.document.language = selected;
        if (set->kind == Kind::Ps2Rounds)
            importPs2(source, *set, c);
        else if (set->kind == Kind::Ps2Sports || set->kind == Kind::Ps2Music)
            importEarlyPs2(source, *set, c);
        else
            set->kind == Kind::PspQuestions ? importPsp(source, *set, c) : importPspBundle(source, *set, c);
        if (c.out.document.questions.isEmpty())
            fail(tr("Źródło nie zawiera obsługiwanych pytań."));
        if (set->kind == Kind::Ps2Rounds || set->kind == Kind::Ps2Sports) {
            const int images = importPs2Pictures(c.out.document.questions, configuredMediaFfmpeg(), c.out.warnings);
            c.media -= images;
            if (images)
                c.out.warnings << tr("Zaimportowano obrazki RSV do %1 pytań.").arg(images);
        }
        if (set->kind == Kind::PspQuestions || set->kind == Kind::PspBundle || set->kind == Kind::PspMusic) {
            recoverPspPictures(c.out.document.questions, c.out.warnings);
            recoverPspMusic(c.out.document.questions, c.out.warnings);
        }
        if (c.adapted)
            c.out.warnings << adaptedAnswersWarning(c.adapted);
        if (c.unsupported)
            c.out.warnings << unsupportedAnswersWarning(c.unsupported);
        if (c.out.duplicates)
            c.out.warnings << tr("Pominięto %1 powtórzonych pytań.").arg(c.out.duplicates);
        // The game version is recognised by its rri.dat (see pictureFrame); a
        // "same picture, different answer" heuristic is not used because the
        // correct English disc reuses hundreds of pictures across questions.
        for (const auto &q : c.out.document.questions)
            for (const auto &m : q.extra["source_media"].toArray())
                c.correctedPictures += m.toObject()["frame_corrected"].toBool();
        if (c.correctedPictures)
            c.out.warnings << tr("Poprawiono przypisanie obrazków w %1 pytaniach (znany błąd polskiej wersji "
                                 "Buzz! Mega Quiz na PS2: obrazki przesunięte o dwie lub trzy klatki).")
                                  .arg(c.correctedPictures);
        if (c.otherLanguage)
            c.out.warnings << tr("Pominięto %1 pytań zapisanych na płycie w innym języku niż wybrany.")
                                  .arg(c.otherLanguage);
    } catch (const std::exception &e) {
        c.out.error = QString::fromUtf8(e.what());
        c.out.document.questions.clear();
    }
    return c.out;
}
} // namespace studio
