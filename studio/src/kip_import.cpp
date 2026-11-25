#include "kip_import.h"
#include "pack_format.h"
#include "ps4_pkg.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <memory>
#include <zlib.h>

namespace studio {
namespace {
using namespace pack;
QString tr(const char *text) { return QCoreApplication::translate("KipImport", text); }

const QString assetsPath = QStringLiteral("Media/resources.assets");
const QString locDir = QStringLiteral("Media/StreamingAssets/Localisation/");
const QString imagesPath = QStringLiteral("Media/StreamingAssets/QuestionImages/QuestionImages.bytes");
const QString voiceDir = QStringLiteral("Media/StreamingAssets/Audio/GeneratedSoundBanks/PS4/");
// Voice exists only in these languages.
const QMap<QString, QString> voiceFolders{{"en", "English(UK)"}, {"pl", "Polish"}, {"ru", "Russian"}};
// Trivia table columns (21, tab separated). Answer 1 is the correct answer.
constexpr int cId = 0, cKey = 1, cAnswers = 2, cCategory = 6, cImage = 15, cFrame = 16, cTags = 17, cDifficulty = 18;

// The game's files: inside the .pkg, or under a folder extracted from it.
class Source {
  public:
    explicit Source(const QString &path) {
        if (QFileInfo(path).suffix().compare("pkg", Qt::CaseInsensitive) == 0) {
            package_ = std::make_unique<ps4::Package>(path);
            for (const auto &f : package_->files())
                files_.insert(f);
            contentId = package_->contentId;
        } else {
            // .../Media/resources.assets: the root is the folder above Media.
            QDir media = QFileInfo(path).absoluteDir();
            root_ = QDir::cleanPath(media.absoluteFilePath(".."));
            QDirIterator it(media.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
                files_.insert(QDir(root_).relativeFilePath(it.next()));
            contentId = QStringLiteral("CUSA09557");
        }
        if (!files_.contains(assetsPath))
            fail(tr("Nie znaleziono pytań Wiedzy to Potęga (Media/resources.assets)."));
    }
    QString contentId;
    bool contains(const QString &path) const { return files_.contains(path); }
    QStringList list(const QString &prefix) const {
        QStringList out;
        for (const auto &f : files_)
            if (f.startsWith(prefix))
                out << f;
        out.sort();
        return out;
    }
    QByteArray read(const QString &path, qint64 limit = 64 * 1024 * 1024) {
        if (package_)
            return package_->read(path, limit);
        QFile f(QDir(root_).filePath(path));
        if (!f.open(QIODevice::ReadOnly))
            fail(f.errorString());
        if (f.size() > limit)
            fail(tr("Plik %1 jest za duży.").arg(path));
        return f.readAll();
    }

  private:
    std::unique_ptr<ps4::Package> package_;
    QString root_;
    QSet<QString> files_;
};

QVector<QStringList> rows(const QString &text) {
    QVector<QStringList> out;
    for (const auto &line : QString(text).remove('\r').split('\n'))
        if (!line.trimmed().isEmpty())
            out << line.split('\t');
    return out;
}

QMap<QString, QString> loadLoc(const QByteArray &bytes) {
    QString text;
    if (bytes.startsWith("\xff\xfe"))
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData() + 2), (bytes.size() - 2) / 2);
    else
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2);
    QMap<QString, QString> out;
    const auto lines = text.split(QStringLiteral("\r\n"));
    for (int i = 1; i < lines.size(); ++i) {
        const int tab = lines[i].indexOf('\t');
        if (tab > 0)
            out.insert(lines[i].left(tab), lines[i].mid(tab + 1));
    }
    return out;
}

QStringList languagesOf(const Source &source) {
    QStringList codes;
    for (const auto &f : source.list(locDir)) {
        const auto name = QFileInfo(f).fileName();
        if (name.endsWith("_text.loc")) {
            const auto code = languageCode(name.left(name.size() - 9));
            if (!code.isEmpty())
                codes << code;
        }
    }
    codes.removeDuplicates();
    codes.sort();
    return codes;
}

// Stored or deflated entry of a ZIP held in memory.
QByteArray zipEntry(const QByteArray &zip, Archive &archive, const QString &name) {
    const auto e = archive.entries.value(name);
    if (e.method == 0)
        return archive.read(name);
    if (e.method != 8 || (e.flags & 1))
        fail(tr("Nieobsługiwana kompresja obrazków."));
    check(zip, e.offset, 30);
    const qint64 start = qint64(e.offset) + 30 + u16(zip, e.offset + 26) + u16(zip, e.offset + 28);
    const qint64 packed = u32(zip, e.offset + 18) ? u32(zip, e.offset + 18) : zip.size() - start;
    check(zip, start, std::min<qint64>(packed, zip.size() - start));
    QByteArray out(e.size, Qt::Uninitialized);
    z_stream z{};
    if (inflateInit2(&z, -15) != Z_OK)
        fail(tr("Nieobsługiwana kompresja obrazków."));
    z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(zip.constData() + start));
    z.avail_in = uInt(zip.size() - start);
    z.next_out = reinterpret_cast<Bytef *>(out.data());
    z.avail_out = uInt(out.size());
    const int status = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    if (status != Z_STREAM_END || z.avail_out != 0 || crc32(out) != e.crc)
        fail(tr("Uszkodzony obrazek w paczce pytań."));
    return out;
}

void saveOnce(const QString &file, const QByteArray &bytes) {
    if (QFileInfo::exists(file))
        return;
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        fail(out.errorString());
}
} // namespace

QMap<QString, QString> kipTextAssets(const QByteArray &d) {
    // Unity serialized TextAsset: u32 name length, name, align 4, u32 data length, data.
    QMap<QString, QString> out;
    static const QRegularExpression name("^KIP_[A-Za-z]+_(?:QuestionText|KillList)[A-Za-z_\\-]*$");
    for (qsizetype at = d.indexOf("KIP_"); at >= 0; at = d.indexOf("KIP_", at + 4)) {
        if (at < 4)
            continue;
        const auto length = u32(d, at - 4);
        if (length < 5 || length > 0x60 || at + qint64(length) > d.size())
            continue;
        const auto key = QString::fromLatin1(d.mid(at, length));
        if (!name.match(key).hasMatch())
            continue;
        const qint64 p = (at + length + 3) & ~qint64(3);
        if (p + 4 > d.size())
            continue;
        const auto size = u32(d, p);
        if (size == 0 || size >= 20000000 || p + 4 + qint64(size) > d.size())
            continue;
        out.insert(key, QString::fromUtf8(d.mid(p + 4, size)));
    }
    return out;
}

bool isKipSource(const QString &path) {
    const QFileInfo info(path);
    return info.suffix().compare("pkg", Qt::CaseInsensitive) == 0 ||
           info.fileName().compare("resources.assets", Qt::CaseInsensitive) == 0;
}

SourceLanguages kipLanguages(const QString &path) {
    SourceLanguages result;
    try {
        Source source(path);
        result.codes = languagesOf(source);
        result.preferred = preferredLanguage(result.codes);
        if (result.codes.isEmpty())
            fail(tr("Brak plików językowych pytań."));
    } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
    }
    return result;
}

LoadResult importKip(const QString &path, const QString &requestedLanguage) {
    LoadResult out;
    try {
        Source source(path);
        const auto codes = languagesOf(source);
        const auto selected = requestedLanguage.isEmpty() ? preferredLanguage(codes) : languageCode(requestedLanguage);
        if (!codes.contains(selected))
            fail(QCoreApplication::translate("BuzzImport", "Źródło nie zawiera wybranego języka."));
        const auto tables = kipTextAssets(source.read(assetsPath, qint64(1) << 31));
        if (!tables.contains("KIP_Trivia_QuestionText"))
            fail(tr("Nie znaleziono tabel pytań w resources.assets."));
        QMap<QString, QMap<QString, QString>> loc;
        QMap<QString, QSet<QString>> kill;
        for (const auto &code : codes) {
            const auto file = locDir + code + "_text.loc";
            if (!source.contains(file))
                continue;
            loc[code] = loadLoc(source.read(file));
            const auto killList = rows(tables.value("KIP_Trivia_KillList_" + code));
            if (!killList.isEmpty())
                for (const auto &id : killList[0])
                    kill[code].insert(id);
        }
        // Pictures and the selected language's voice go to a cache, as Buzz imports do.
        const QFileInfo input(path);
        const auto identity =
            (input.absoluteFilePath() + QString::number(input.size()) + input.lastModified().toString(Qt::ISODateWithMs))
                .toUtf8();
        const QDir cache(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                             .filePath("imports/" + QString::fromLatin1(QCryptographicHash::hash(
                                                                            identity, QCryptographicHash::Sha256)
                                                                            .toHex())));
        if (!QDir().mkpath(cache.absolutePath()))
            fail(QCoreApplication::translate("BuzzImport", "Nie można utworzyć katalogu mediów."));
        QByteArray imagesZip;
        std::unique_ptr<Archive> images;
        QMap<QString, QString> imageNames;
        if (source.contains(imagesPath)) {
            imagesZip = source.read(imagesPath, 512 * 1024 * 1024);
            images = std::make_unique<Archive>(imagesZip);
            for (auto it = images->entries.cbegin(); it != images->entries.cend(); ++it)
                imageNames.insert(QFileInfo(it.key()).completeBaseName().toLower(), it.key());
        }
        QMap<QString, QString> extracted;
        auto picture = [&](const QString &name) -> QString {
            const auto entry = imageNames.value(name.toLower());
            if (entry.isEmpty() || !images)
                return {};
            if (!extracted.contains(entry)) {
                const auto file = cache.filePath(
                    "kip-" + QString::fromLatin1(QCryptographicHash::hash(entry.toUtf8(), QCryptographicHash::Sha256)
                                                     .toHex()
                                                     .left(24)) +
                    "." + QFileInfo(entry).suffix().toLower());
                saveOnce(file, zipEntry(imagesZip, *images, entry));
                extracted.insert(entry, file);
            }
            return extracted.value(entry);
        };
        int voices = 0, missingVoices = 0;
        auto voice = [&](const QString &code, const QString &key) -> QString {
            if (code != selected || !voiceFolders.contains(code))
                return {};
            const auto file = voiceDir + voiceFolders[code] + '/' + code + '_' + key + ".wem";
            if (!source.contains(file)) {
                ++missingVoices;
                return {};
            }
            const auto target = cache.filePath(code + '_' + key + ".wem");
            saveOnce(target, source.read(file));
            ++voices;
            return target;
        };
        QJsonArray questions;
        auto add = [&](const QStringList &r, const QString &kind, const QString &onlyLanguage) {
            if (r.size() <= cAnswers + 3)
                return;
            const auto key = r[cKey];
            QJsonArray category;
            for (int c = cCategory; c < std::min<int>(r.size(), cCategory + 9); ++c)
                if (!r[c].isEmpty())
                    category << r[c];
            QJsonObject q{{"id", r[cId]},
                          {"kind", kind},
                          {"category", category},
                          {"difficulty", r.value(cDifficulty).isEmpty() ? "NORMAL" : r.value(cDifficulty)},
                          {"tags", QJsonArray::fromStringList(r.value(cTags).isEmpty()
                                                                  ? QStringList()
                                                                  : r.value(cTags).split("|~|"))},
                          {"image_frame", r.value(cFrame).isEmpty() ? QJsonValue() : QJsonValue(r.value(cFrame))}};
            const auto image = r.value(cImage).isEmpty() ? QString() : picture(r.value(cImage));
            if (!image.isEmpty())
                q["image"] = image;
            QJsonObject langs;
            for (auto it = loc.cbegin(); it != loc.cend(); ++it) {
                const auto &code = it.key();
                if ((!onlyLanguage.isEmpty() && code != onlyLanguage) || kill[code].contains(key))
                    continue;
                const auto text = it->value(key);
                QStringList answers;
                for (int a = 0; a < 4; ++a)
                    answers << it->value(r[cAnswers + a]);
                if (text.isEmpty() ||
                    std::any_of(answers.begin(), answers.end(), [](const QString &a) { return a.isEmpty(); }))
                    continue;
                QJsonObject variant{{"question", text},
                                    {"answers", QJsonArray::fromStringList(answers)},
                                    {"correct_index", 0}};
                if (!image.isEmpty())
                    variant["image"] = image;
                const auto speech = voice(code, key);
                if (!speech.isEmpty())
                    variant["speech"] = speech;
                langs.insert(code, variant);
            }
            if (!langs.isEmpty()) {
                q["langs"] = langs;
                questions << q;
            }
        };
        for (const auto &r : rows(tables.value("KIP_Trivia_QuestionText")))
            add(r, "trivia", {});
        for (const auto &code : loc.keys())
            for (const auto &r : rows(tables.value("KIP_Trivia_QuestionText_" + code)))
                add(r, "local", code);
        for (const auto &r : rows(tables.value("KIP_Finale_QuestionText")))
            add(r, "finale", {});
        int skippedTypes = 0;
        static const QRegularExpression other("^KIP_(Sorting|Linking)_QuestionText$");
        for (auto it = tables.cbegin(); it != tables.cend(); ++it)
            if (other.match(it.key()).hasMatch())
                skippedTypes += rows(it.value()).size();
        const auto json = QJsonDocument(QJsonObject{{"source", "Knowledge Is Power (" + source.contentId + ")"},
                                                    {"questions", questions}})
                              .toJson(QJsonDocument::Compact);
        out = parseJson(json, {}, selected);
        if (!out.ok())
            return out;
        out.document.title = QStringLiteral("Wiedza to Potęga");
        if (skippedTypes)
            out.warnings << tr("Pominięto %1 pytań na sortowanie i łączenie — nie mają odpowiednika w Buzz!.")
                                .arg(skippedTypes);
        if (voiceFolders.contains(selected) && missingVoices)
            out.warnings << tr("%1 pytań nie ma nagrania lektora w pakiecie.").arg(missingVoices);
        if (!voiceFolders.contains(selected))
            out.warnings << tr("Gra ma nagrania lektora tylko po angielsku, polsku i rosyjsku.");
        out.warnings << tr("Kategorie Wiedzy to Potęga trzeba przypisać do kategorii Quiz World przed budowaniem.");
        Q_UNUSED(voices)
    } catch (const std::exception &e) {
        out.error = QString::fromUtf8(e.what());
        out.document.questions.clear();
    }
    return out;
}
} // namespace studio
