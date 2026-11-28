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
#include <algorithm>
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
    const auto lines = text.remove('\r').split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const int tab = lines[i].indexOf('\t');
        if (tab <= 0)
            continue;
        const auto value = lines[i].mid(tab + 1);
        const auto trimmed = value.trimmed();
        if (!trimmed.isEmpty() && !(trimmed.startsWith("<Translate:") && trimmed.endsWith('>')))
            out.insert(lines[i].left(tab), value);
    }
    return out;
}

QMap<QString, QMap<QString, QString>> questionLocalizations(Source &source, const QMap<QString, QString> &tables) {
    QMap<QString, QMap<QString, QString>> languages;
    const auto trivia = rows(tables.value("KIP_Trivia_QuestionText"));
    const auto finale = rows(tables.value("KIP_Finale_QuestionText"));
    for (const auto &f : source.list(locDir)) {
        const auto name = QFileInfo(f).fileName();
        if (!name.endsWith("_text.loc"))
            continue;
        const auto code = languageCode(name.chopped(9));
        if (code.isEmpty() || languages.contains(code))
            continue;
        auto loc = loadLoc(source.read(f));
        const auto translated = [&](const QVector<QStringList> &table) {
            return std::any_of(table.cbegin(), table.cend(), [&](const QStringList &row) {
                return row.size() > cAnswers + 3 &&
                       std::all_of(row.cbegin() + cKey, row.cbegin() + cAnswers + 4,
                                   [&](const QString &key) { return loc.contains(key); });
            });
        };
        if (translated(trivia) || translated(finale) || translated(rows(tables.value("KIP_Trivia_QuestionText_" + code))))
            languages.insert(code, std::move(loc));
    }
    return languages;
}

QJsonArray packCategoryPath(const QJsonArray &sourceCategory) {
    // The PS4 taxonomy runs seven levels deep. Its first three levels fit within
    // Quiz World's custom-category budget. Keep the full original path on the question.
    QJsonArray path;
    for (int i = 0; i < std::min<qsizetype>(3, sourceCategory.size()); ++i)
        path.append(sourceCategory[i]);
    if (path.isEmpty())
        path.append(QCoreApplication::translate("LegacyImport", "Wiedza ogólna"));
    return path;
}

QJsonObject categoryLabels(const QJsonArray &path, const QMap<QString, QMap<QString, QString>> &localizations) {
    QJsonObject labels;
    for (auto it = localizations.cbegin(); it != localizations.cend(); ++it) {
        QJsonArray names;
        for (const auto &part : path) {
            const auto key = part.toString();
            names.append(it->value(key, key));
        }
        if (names.isEmpty()) {
            if (it.key() == "pl")
                names.append(QStringLiteral("Wiedza ogólna"));
            else if (it.key().section('-', 0, 0) == "en")
                names.append(QStringLiteral("General Knowledge"));
            else
                continue;
        }
        labels[it.key()] = names;
    }
    return labels;
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
        const auto tables = kipTextAssets(source.read(assetsPath, qint64(1) << 31));
        result.codes = questionLocalizations(source, tables).keys();
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
        const auto tables = kipTextAssets(source.read(assetsPath, qint64(1) << 31));
        if (!tables.contains("KIP_Trivia_QuestionText"))
            fail(tr("Nie znaleziono tabel pytań w resources.assets."));
        const auto loc = questionLocalizations(source, tables);
        const auto codes = loc.keys();
        const auto selected = requestedLanguage.isEmpty() ? preferredLanguage(codes) : languageCode(requestedLanguage);
        if (!codes.contains(selected))
            fail(QCoreApplication::translate("BuzzImport", "Źródło nie zawiera wybranego języka."));
        QMap<QString, QSet<QString>> kill;
        QMap<QString, QSet<QString>> modeKill;
        for (const auto &code : codes) {
            const auto killList = rows(tables.value("KIP_Trivia_KillList_" + code));
            for (const auto &line : killList)
                for (const auto &id : line)
                    kill[code].insert(id);
            for (const auto &prefix : {"KIP_Sorting_KillList_", "KIP_Linking_KillList_"}) {
                const auto list = rows(tables.value(prefix + code));
                for (const auto &line : list)
                    for (const auto &id : line)
                        modeKill[code].insert(id);
            }
        }
        // Pictures and original voices for the retained language variants go to the cache.
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
        // Regional editions and their language updates ship different banks. Match
        // the locale and question key in the WEM name independently of the folder.
        QMap<QString, QHash<QString, QString>> voices;
        for (const auto &file : source.list(voiceDir)) {
            const QFileInfo info(file);
            if (info.suffix().compare("wem", Qt::CaseInsensitive) != 0)
                continue;
            const auto stem = info.completeBaseName();
            for (qsizetype split = stem.lastIndexOf('_'); split > 0; split = stem.lastIndexOf('_', split - 1)) {
                const auto code = languageCode(stem.left(split));
                if (!loc.contains(code))
                    continue;
                auto &bank = voices[code];
                const auto key = stem.mid(split + 1).toCaseFolded();
                // Preserve the UK bank for the original unqualified English locale.
                if (!bank.contains(key) || (code == "en" && file.startsWith(voiceDir + "English(UK)/")))
                    bank.insert(key, file);
                break;
            }
        }
        int missingVoices = 0;
        const auto attachVoices = [&](QJsonObject &variants, const QString &key, bool expectNarration) {
            for (auto it = variants.begin(); it != variants.end(); ++it) {
                const auto code = it.key();
                const auto bank = voices.constFind(code);
                const auto file = bank == voices.cend() ? QString() : bank->value(key.toCaseFolded());
                if (file.isEmpty()) {
                    missingVoices += expectNarration && code == selected;
                    continue;
                }
                const auto target = cache.filePath("voice-" + QString::fromLatin1(
                    QCryptographicHash::hash(file.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".wem");
                if (!QFileInfo::exists(target))
                    saveOnce(target, source.read(file));
                auto variant = it.value().toObject();
                variant["speech"] = target;
                it.value() = variant;
            }
        };
        QJsonArray questions;
        const auto uncategorized = packCategoryPath({});
        const auto uncategorizedLabels = categoryLabels({}, loc);
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
                          {"source_pack_category_path", category.isEmpty() ? uncategorized : packCategoryPath(category)},
                          {"source_category_labels", category.isEmpty() ? uncategorizedLabels
                                                                       : categoryLabels(category, loc)},
                          {"difficulty", r.value(cDifficulty).isEmpty() ? "NORMAL" : r.value(cDifficulty)},
                          {"tags", QJsonArray::fromStringList(r.value(cTags).isEmpty()
                                                                  ? QStringList()
                                                                  : r.value(cTags).split("|~|"))},
                          {"image_frame", r.value(cFrame).isEmpty() ? QJsonValue() : QJsonValue(r.value(cFrame))}};
            if (category.isEmpty())
                q["source_section_key"] = "kip-general";
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
                langs.insert(code, QJsonObject{{"question", text},
                                               {"answers", QJsonArray::fromStringList(answers)},
                                               {"correct_index", 0}});
            }
            if (langs.contains(selected)) {
                const auto image = r.value(cImage).isEmpty() ? QString() : picture(r.value(cImage));
                if (!image.isEmpty())
                    q["image"] = image;
                attachVoices(langs, key, true);
                q["langs"] = langs;
                questions << q;
            }
        };
        for (const auto &r : rows(tables.value("KIP_Trivia_QuestionText")))
            add(r, "trivia", {});
        for (const auto &r : rows(tables.value("KIP_Trivia_QuestionText_" + selected)))
            add(r, "local", selected);
        for (const auto &r : rows(tables.value("KIP_Finale_QuestionText")))
            add(r, "finale", {});
        int adapted = 0, skippedTypes = 0;
        const auto addMode = [&](const QString &table, const QString &onlyLanguage, bool sorting) {
            const auto key = [](const QString &s) { return s.simplified().toCaseFolded(); };
            for (const auto &row : rows(tables.value(table))) {
                if (row.size() < (sorting ? 6 : 4)) {
                    ++skippedTypes;
                    continue;
                }
                const auto killed = [&](const QString &code) {
                    const auto list = modeKill.constFind(code);
                    return list != modeKill.cend() && (list->contains(row[cKey]) || list->contains(row[cId]));
                };
                const bool countThisLanguage = !killed(selected);
                const auto left = row[sorting ? 3 : 2].split("|~|", Qt::SkipEmptyParts);
                const auto right = row[sorting ? 5 : 3].split("|~|", Qt::SkipEmptyParts);
                if (!sorting && left.size() != right.size()) {
                    skippedTypes += countThisLanguage;
                    continue;
                }
                QJsonObject variants;
                for (auto lang = loc.cbegin(); lang != loc.cend(); ++lang) {
                    const auto &code = lang.key();
                    if ((!onlyLanguage.isEmpty() && code != onlyLanguage) || killed(code))
                        continue;
                    const auto text = lang->value(row[cKey]);
                    const auto group = sorting ? lang->value(row[2]) : QString();
                    if (text.isEmpty() || (sorting && group.isEmpty()))
                        continue;
                    // Sorting stores a list per group, linking stores parallel lists of paired keys.
                    for (qsizetype i = 0; i < left.size(); ++i) {
                        const auto item = lang->value(left[i]).trimmed();
                        if (item.isEmpty() || (!sorting && left[i].right(2) != right[i].right(2)))
                            continue;
                        const auto answer = sorting ? item : lang->value(right[i]).trimmed();
                        if (answer.isEmpty())
                            continue;
                        if (sorting && std::any_of(right.begin(), right.end(), [&](const QString &r) {
                                return key(lang->value(r)) == key(answer);
                            }))
                            continue;
                        if (!sorting && std::count_if(left.begin(), left.end(), [&](const QString &l) {
                                return key(lang->value(l)) == key(item);
                            }) != 1)
                            continue;
                        QSet<QString> used{key(answer)};
                        QStringList answers{answer};
                        for (const auto &r : right) {
                            const auto other = lang->value(r).trimmed();
                            const auto value = key(other);
                            if (!value.isEmpty() && !used.contains(value)) {
                                used.insert(value);
                                answers << other;
                                if (answers.size() == 4)
                                    break;
                            }
                        }
                        if (answers.size() == 4) {
                            variants[code] = QJsonObject{
                                {"question", text + (sorting ? "\n" + group + ":" : "\n" + item + " → …")},
                                {"answers", QJsonArray::fromStringList(answers)}, {"correct_index", 0}};
                            break;
                        }
                    }
                }
                if (variants.contains(selected)) {
                    ++adapted;
                    attachVoices(variants, row[cKey], false);
                    questions << QJsonObject{{"id", row[cId]}, {"kind", sorting ? "sorting" : "linking"},
                                             {"source_pack_category_path", uncategorized},
                                             {"source_category_labels", uncategorizedLabels},
                                             {"source_section_key", "kip-general"},
                                             {"langs", variants}};
                } else if (countThisLanguage)
                    ++skippedTypes;
            }
        };
        for (const bool sorting : {true, false}) {
            const QString table = sorting ? "KIP_Sorting_QuestionText" : "KIP_Linking_QuestionText";
            addMode(table, {}, sorting);
            addMode(table + '_' + selected, selected, sorting);
        }
        const auto json = QJsonDocument(QJsonObject{{"source", "Knowledge Is Power (" + source.contentId + ")"},
                                                    {"questions", questions}})
                              .toJson(QJsonDocument::Compact);
        out = parseJson(json, {}, selected);
        if (!out.ok())
            return out;
        out.document.title = QStringLiteral("Wiedza to Potęga");
        if (adapted)
            out.warnings << adaptedAnswersWarning(adapted);
        if (skippedTypes)
            out.warnings << unsupportedAnswersWarning(skippedTypes);
        if (missingVoices)
            out.warnings << tr("%1 pytań nie ma nagrania lektora w pakiecie.").arg(missingVoices);
    } catch (const std::exception &e) {
        out.error = QString::fromUtf8(e.what());
        out.document.questions.clear();
    }
    return out;
}
} // namespace studio
