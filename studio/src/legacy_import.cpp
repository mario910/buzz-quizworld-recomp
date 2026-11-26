#include "legacy_import.h"
#include "buzz_import.h"
#include "disc_image.h"
#include "pack_format.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringDecoder>
#include <algorithm>
#include <memory>

namespace studio {
namespace {
using namespace pack;
QString tr(const char *text) { return QCoreApplication::translate("LegacyImport", text); }

enum class Kind { Ps2Rounds, PspQuestions, PspBundle };
constexpr qint64 bundleSize = 0x100000;
struct Set {
    Kind kind;
    QString code, primary, text; // Paths inside the disc image, or on disk.
};
struct Category {
    QString name;
    int subject = 0, category = 0; // Quiz World subject/subcategory, 0 = map in the editor.
};

// "INE"/"INF" are the PS2/PS3 international English/French folders.
QString folderLanguage(const QString &folder) {
    static const QHash<QString, QString> names = {
        {"INE", "en"},        {"INF", "fr"},       {"POLISH", "pl"},     {"ENGLISH", "en"},  {"FRENCH", "fr"},
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
    static const QHash<QString, QString> titles = {{"SCES-54505", "Buzz! The Mega Quiz"},
                                                   {"SCES-54690", "Buzz! Mega Quiz"},
                                                   {"SCES-54856", "Buzz! Hollywood Quiz"}};
    return titles.value(serial, QStringLiteral("Buzz! (PS2)"));
}

// The category byte of a PS2 round record (checked against the qpp* tables).
Category ps2Category(int value) {
    switch (value) {
    case 17: return {tr("Muzyka"), 175, 224};
    case 18: return {tr("Film"), 199, 208};
    case 19: return {tr("Telewizja"), 257, 271};
    case 20: return {tr("Ciekawostki"), 188, 191};
    case 21: return {tr("Zwierzęta"), 235, 240};
    case 22: return {tr("Sport"), 174, 247};
    case 24: return {tr("Historia"), 182, 184};
    case 25: return {tr("Świat"), 250, 253};
    case 26: return {tr("Nauka"), 235, 237};
    case 27: return {tr("Geografia i przyroda"), 250, 253};
    case 28: return {tr("Sztuka i rozrywka"), 177, 178};
    case 80: return {tr("Gwiazdy"), 230, 232};
    default:
        if (value >= 30 && value <= 79)
            return {tr("Kraje świata"), 250, 253};
        return {tr("Wiedza ogólna"), 0, 0};
    }
}

Category pspCategory(int value) {
    switch (value) {
    case 335: return {tr("Film"), 199, 208};
    case 336: return {tr("Telewizja"), 257, 271};
    case 337: return {tr("Świat i historia"), 250, 253};
    case 338: return {tr("Przyroda"), 235, 240};
    case 339: return {tr("Nauka i technika"), 235, 236};
    case 340: return {tr("Sport"), 174, 247};
    case 341: return {tr("Gwiazdy"), 230, 232};
    case 343: return {tr("Muzyka"), 175, 224};
    case 344: return {tr("Czas wolny"), 188, 191};
    default: return {tr("Wiedza ogólna"), 0, 0};
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
    case 0x6e9f7a1d: return {tr("Sztuka i kultura"), 177, 178};
    case 0x757e79d2: return {tr("Film"), 199, 208};
    case 0x8e801ef7: return {tr("Muzyka"), 175, 224};
    case 0xa4fb6732: return {tr("Świat"), 250, 253};
    case 0xb9373ab9: return {tr("Gwiazdy"), 230, 232};
    case 0xc372a3bc: return {tr("Sport"), 174, 247};
    case 0xd9410840: return {tr("Aktualności"), 182, 183};
    case 0xd94a20d8: return {tr("Nauka i przyroda"), 235, 237};
    case 0xee33b90a: return {tr("Telewizja"), 257, 271};
    case 0xf0bf4a04: return {tr("Czas wolny"), 188, 191};
    default: return {tr("Wiedza ogólna"), 0, 0};
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

  private:
    std::unique_ptr<DiscImage> disc;

    void scanDisc() {
        static const QRegularExpression ps2("^([A-Z]{3})/\\1ROUND\\.PAK$"),
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
        }
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const auto name = it.key();
            if (const auto m = ps2.match(name); m.hasMatch())
                sets << Set{Kind::Ps2Rounds, folderLanguage(m.captured(1)), name, {}};
            const auto text = name.left(name.size() - 3) + "TXT";
            if (const auto m = merged.match(name); m.hasMatch() && files.contains(text))
                sets << Set{Kind::PspQuestions, folderLanguage(m.captured(1)), name, text};
            else if (locale.match(name).hasMatch() && files.contains(text))
                sets << Set{Kind::PspQuestions, detectLanguage(QString::fromUtf8(disc->read(text))), name, text};
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
            for (qint64 base = 0; base + bundleSize <= data.size(); base += bundleSize)
                if (data.mid(base + 0x18000, 4) == QByteArray("\0BDQ", 4))
                    sets << Set{Kind::PspBundle, detectLanguage(bundleLines(data, base).join('\n')), it.key(),
                                QString::number(base)};
        }
        if (!sets.isEmpty())
            return;
        if (std::any_of(files.keyBegin(), files.keyEnd(), [](const QString &f) { return f.endsWith(".FLZ"); }))
            fail(tr("Nie znaleziono pytań w archiwach FLZ tego obrazu."));
        if (std::any_of(files.keyBegin(), files.keyEnd(),
                        [](const QString &f) { return f.contains("/PAKS/GAMES/"); }))
            fail(tr("Ta gra zawiera łamigłówki, a nie pytania quizowe — nie ma czego importować."));
    }

    void scanRoundArchive() {
        const QFileInfo info(path);
        static const QRegularExpression name("^([A-Za-z]{3})ROUND(?:_[A-Za-z0-9]+)?\\.PAK$",
                                             QRegularExpression::CaseInsensitiveOption);
        const auto m = name.match(info.fileName());
        if (!m.hasMatch())
            fail(tr("Wybierz plik <JĘZYK>ROUND.PAK z płyty PS2."));
        sets << Set{Kind::Ps2Rounds, folderLanguage(m.captured(1)), path, {}};
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
    int unsupported = 0, media = 0, otherLanguage = 0, correctedPictures = 0;
    // The games repeat questions across rounds (and picture/video variants share
    // text tables): keep one copy. A copy with a media reference wins: such text
    // ("which country makes this drink?") needs its picture, even where another
    // table of the game lost the reference (media id 0x7FFF).
    void add(Question q, bool hasMedia) {
        const auto key = (q.text + '\n' + q.answers.join('\n')).simplified().toCaseFolded();
        if (const auto it = byText.constFind(key); it != byText.constEnd()) {
            ++out.duplicates;
            auto &existing = out.document.questions[*it];
            if (hasMedia && !existing.extra.contains("source_media")) {
                existing = q;
                ++media;
            }
            return;
        }
        media += hasMedia;
        byText.insert(key, out.document.questions.size());
        out.document.questions << q;
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
    q.category = category.name;
    q.subjectId = category.subject;
    q.categoryId = category.category;
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

// Some localized discs keep a few untranslated tables (e.g. English on the
// Polish Hollywood Quiz). Only languages with distinctive letters are checked.
bool otherLanguage(const QStringList &texts, const QString &code) {
    static const QSet<QString> detectable = {"pl", "hr", "ru", "cs", "el"};
    const auto text = texts.join('\n');
    const auto letters = std::count_if(text.begin(), text.end(), [](QChar c) { return c.isLetter(); });
    return detectable.contains(code) && letters >= 200 && detectLanguage(text) != code;
}

void importTable(Source &source, const Set &set, Collector &c, const QString &table, const QByteArray &bytes,
                 const Category &category) {
    const auto rows = tableRows(bytes);
    if (rows.isEmpty() || rows[0].value(0).trimmed() != "Id" || rows[0].size() < 7)
        return; // Not a question table (e.g. namedCharMap).
    // True/false and put-in-order rounds have no four alternatives with one answer.
    if (table.contains("factfiction", Qt::CaseInsensitive) || table.contains("toprank", Qt::CaseInsensitive)) {
        c.unsupported += std::max(1, int(rows.size()) - 1);
        return;
    }
    QStringList all;
    for (int r = 1; r < rows.size(); ++r)
        all << rows[r].mid(2, 5).join('\n');
    if (otherLanguage(all, set.code)) {
        c.otherLanguage += rows.size() - 1;
        return;
    }
    const auto lower = table.toLower();
    const int difficulty = lower.endsWith("_easy") ? 1 : lower.endsWith("_hard") ? 3 : 2;
    for (int r = 1; r < rows.size(); ++r) {
        const auto &row = rows[r];
        if (row.size() != 7 || std::any_of(row.begin() + 2, row.end(), [](const QString &v) {
                return v.trimmed().isEmpty();
            })) {
            ++c.unsupported;
            continue;
        }
        const auto id = row[0].trimmed();
        auto q = makeQuestion(QStringLiteral("buzz-ps2:%1:%2:%3")
                                  .arg(source.serial.isEmpty() ? QStringLiteral("disc") : source.serial, set.code, id),
                              set.code, source.game, row[2], row.mid(3, 4), difficulty, category);
        QJsonArray media;
        const auto resource = row[1].trimmed();
        if (!resource.isEmpty() && resource != "non-asset")
            media << QJsonObject{{"type", "ps2-picture"}, {"asset", resource.toInt()}};
        finish(q, {{"source_archive", source.path}, {"source_round", table}, {"source_question_id", id}}, media);
        c.add(q, !media.isEmpty());
    }
}

void importPs2(Source &source, const Set &set, Collector &c) {
    Archive rounds(source.read(set.primary));
    QSet<QString> tables;
    // Comparison, ordering and fact-or-fiction rounds do not have four
    // alternatives with one correct answer.
    static const QSet<QString> skipped = {"qcompare", "qfactfiction", "qorderaat"};
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
    const auto pictureFrame = [&](int mediaId, bool &corrected) {
        corrected = false;
        if (rri.size() < 4 || mediaId < 0 || quint32(mediaId) >= u32(rri, 0) || 4 + qint64(mediaId) * 8 + 8 > rri.size())
            return -1;
        int frame = u16(rri, 4 + mediaId * 8 + 2);
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
    const auto videos = rriName.section('/', 0, 1) + "/VIDEOS.PAK:Videos/pics001.rsv";
    for (auto it = rounds.entries.cbegin(); it != rounds.entries.cend(); ++it) {
        if (it.key().endsWith(".adt", Qt::CaseInsensitive)) {
            const auto table = QFileInfo(it.key()).completeBaseName();
            if (!tables.contains(table)) {
                tables.insert(table);
                importTable(source, set, c, table, rounds.read(it.key()),
                            hollywood ? Category{tr("Film"), 199, 208} : Category{tr("Wiedza ogólna"), 0, 0});
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
            if (skipped.contains(table)) {
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
            const auto kind = table.endsWith("_vid") ? "ps2-video"
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
                if (std::any_of(text.begin(), text.end(), [](const QString &s) { return s.isEmpty(); })) {
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
                QJsonArray media;
                if (mediaId != 0x7fff) {
                    QJsonObject reference{{"type", kind}, {"asset", mediaId}};
                    bool corrected = false;
                    if (QString(kind) == "ps2-picture")
                        if (const int frame = pictureFrame(mediaId, corrected); frame >= 0) {
                            reference["frame"] = frame;
                            reference["container"] = videos;
                            if (corrected)
                                reference["frame_corrected"] = true;
                        }
                    media << reference;
                } else if (mediaRound) {
                    // A picture/video/music round whose record lost its media id
                    // (e.g. all of qppe_Movies_pic): the text still refers to it.
                    media << QJsonObject{{"type", kind}, {"missing", true}};
                }
                finish(q, {{"source_archive", source.path}, {"source_round", table}, {"source_question_id", r / 28}},
                       media);
                c.add(q, !media.isEmpty());
            }
        }
    }
}

// Quiz World PSP records (32 bytes): id, picture hash (all ones = none),
// subcategory and category hashes, question + 4 answers (line numbers, correct
// first), a reserved word, media flags and (difficulty - 1) << 4.
void importPspBundle(Source &source, const Set &set, Collector &c) {
    const auto data = source.read(set.primary);
    const qint64 base = set.text.toLongLong();
    const auto lines = bundleLines(data, base);
    const qint64 table = base + 0x18000;
    if (data.mid(table, 4) != QByteArray("\0BDQ", 4))
        fail(tr("Nieobsługiwany format MERGED.QDB."));
    const auto count = u32(data, table + 8);
    check(data, table + 16, qint64(count) * 32);
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
                              bundleCategory(category));
        QJsonArray media;
        if (flags || picture != 0xffffffffu)
            media << QJsonObject{{"type", flags == 0x2000 ? "psp-audio" : "psp-picture"},
                                 {"asset", QString::number(picture, 16)}};
        finish(q, {{"source_archive", source.path}, {"source_question_id", double(id)}}, media);
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
    for (quint32 i = 0; i < count; ++i) {
        const int p = 16 + int(i) * 24;
        const int category = u16(qdb, p + 6), flags = u16(qdb, p + 20), level = quint8(qdb[p + 22]);
        // 345: put-in-order rounds, 346: party challenges without answers.
        if (category == 345 || category == 346) {
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
            set.code, source.game, text[0], text.mid(1), level >> 4, pspCategory(category));
        QJsonArray media;
        if (flags) // 0x3000 picture, 0x2000 sound; the asset id is stored before the category.
            media << QJsonObject{{"type", flags & 0x1000 ? "psp-picture" : "psp-audio"},
                                 {"asset", int(u16(qdb, p + 4))}};
        finish(q, {{"source_archive", source.path}, {"source_question_id", double(id)}}, media);
        c.add(q, !media.isEmpty());
    }
}
} // namespace

bool isLegacySource(const QString &path) {
    const auto name = QFileInfo(path).fileName();
    static const QRegularExpression round("^[A-Za-z]{3}ROUND(?:_[A-Za-z0-9]+)?\\.PAK$",
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
        else
            set->kind == Kind::PspBundle ? importPspBundle(source, *set, c) : importPsp(source, *set, c);
        if (c.out.document.questions.isEmpty())
            fail(tr("Źródło nie zawiera obsługiwanych pytań."));
        if (c.unsupported)
            c.out.warnings << tr("Pominięto %1 pytań innych typów (porównania, kolejność, prawda/fałsz, "
                                 "zadania bez odpowiedzi). Edytor obsługuje cztery odpowiedzi z jedną poprawną.")
                                  .arg(c.unsupported);
        if (c.media)
            c.out.warnings << tr("%1 pytań wymaga obrazka, filmu lub nagrania z płyty. Zachowano ich odwołania; "
                                 "przed budowaniem paczki przypisz im własny obrazek.")
                                  .arg(c.media);
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
        int unmapped = 0;
        for (const auto &q : c.out.document.questions)
            unmapped += q.subjectId == 0;
        if (unmapped)
            c.out.warnings << tr("%1 pytań z ogólnych kategorii wymaga wybrania kategorii Quiz World.")
                                  .arg(unmapped);
    } catch (const std::exception &e) {
        c.out.error = QString::fromUtf8(e.what());
        c.out.document.questions.clear();
    }
    return c.out;
}
} // namespace studio
