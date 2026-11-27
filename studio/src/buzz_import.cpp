#include "buzz_import.h"
#include "ps2_media.h"
#include "psp_media.h"
#include "media_tools.h"
#include "pack_format.h"
#include "ps3_source.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariant>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace studio {
namespace {
using namespace pack;
struct Node {
    quint64 weight = 0;
    int symbol = -1, left = -1, right = -1;
};
QString luaString(const QByteArray &b, int &p) {
    const auto n = u32(b, p, true);
    p += 4;
    check(b, p, n);
    auto s = QString::fromLatin1(b.mid(p, n ? n - 1 : 0));
    p += n;
    return s;
}
struct Lua {
    QVector<quint32> code;
    QVector<QVariant> constants;
};
Lua parseLua(const QByteArray &b) {
    if (b.left(12) != QByteArray::fromHex("1b4c75615100000404040400"))
        fail(QCoreApplication::translate("BuzzImport", "Nieobsługiwana wersja skryptu CLU."));
    int p = 12;
    luaString(b, p);
    p += 12;
    const auto n = u32(b, p, true);
    p += 4;
    if (n > 1000000)
        fail(QCoreApplication::translate("BuzzImport", "Za duży skrypt CLU."));
    Lua out;
    for (quint32 i = 0; i < n; ++i) {
        out.code << u32(b, p, true);
        p += 4;
    }
    const auto k = u32(b, p, true);
    p += 4;
    if (k > 1000000)
        fail(QCoreApplication::translate("BuzzImport", "Za dużo stałych CLU."));
    for (quint32 i = 0; i < k; ++i) {
        check(b, p, 1);
        const int type = quint8(b[p++]);
        if (type == 0)
            out.constants << QVariant();
        else if (type == 1) {
            check(b, p, 1);
            out.constants << bool(b[p++]);
        } else if (type == 3) {
            const auto bits = u32(b, p, true);
            float v;
            std::memcpy(&v, &bits, 4);
            p += 4;
            out.constants << double(v);
        } else if (type == 4)
            out.constants << luaString(b, p);
        else
            fail(QCoreApplication::translate("BuzzImport", "Niepoprawna stała CLU."));
    }
    return out;
}
QVector<QVariantList> literalCalls(const Lua &lua, const QString &function) {
    QVector<QVariantList> calls;
    QMap<int, QVariant> regs;
    for (auto ins : lua.code) {
        const auto op = ins & 63, a = (ins >> 6) & 255, bx = ins >> 14;
        if (op == 1 || op == 5) {
            if (bx >= quint32(lua.constants.size()))
                fail(QCoreApplication::translate("BuzzImport", "Niepoprawny indeks CLU."));
            regs[a] = lua.constants[bx];
        } else if (op == 28 && regs.value(a).toString() == function) {
            const int end = a + int((ins >> 23) & 511);
            if (end <= 256) {
                QVariantList args;
                for (int i = a + 1; i < end; ++i)
                    args << regs.value(i);
                calls << args;
            }
        }
    }
    return calls;
}
QMap<int, QString> categoryKeys(const Lua &lua) {
    QMap<int, QString> keys;
    for (const auto &args : literalCalls(lua, "AddQuestionCategoryItem"))
        if (args.size() >= 3)
            keys[args[0].toInt()] = args[2].toString();
    return keys;
}
QString cachedAsset(Archive &archive, const QString &name, const QString &cache, bool streamedMusic = false) {
    const auto entry = archive.entries.constFind(name);
    if (entry == archive.entries.cend() || (streamedMusic && entry->size > maxMusicBytes))
        return {};
    // Only a generated basename is used; archive paths are never extracted verbatim.
    const auto dest = QDir(cache).filePath(
        QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex()) +
        "." + QFileInfo(name).suffix());
    bool cached = QFileInfo::exists(dest);
    if (cached && streamedMusic) {
        QFile file(dest);
        cached = file.size() <= maxMusicBytes && file.open(QIODevice::ReadOnly) && isStreamedMusic(file.readAll());
    }
    if (!cached) {
        const auto bytes = archive.read(name);
        if (streamedMusic && !isStreamedMusic(bytes))
            fail(QCoreApplication::translate("BuzzImport", "Nieprawidłowy klip muzyczny w archiwum gry."));
        QSaveFile file(dest);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            fail(file.errorString());
    }
    return dest;
}
QString importCache(const QString &identity) {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath("imports/" + QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(),
                                                                          QCryptographicHash::Sha256).toHex()));
}
QString musicCache(const QString &source, const QString &archive) {
    return importCache(buzzFilePack(source.isEmpty() ? archive : source).identity + '\n' + archive);
}
} // namespace
QStringList decodeHuffman(const QByteArray &header, const QByteArray &stream) {
    if (header.left(2) != QByteArray::fromHex("00ff") || header.size() < 18)
        fail(QCoreApplication::translate("BuzzImport", "Niepoprawny nagłówek tekstów."));
    const auto count = u32(header, 2, true), total = u32(header, 6, true);
    if (count > 1000000 || total > 64 * 1024 * 1024)
        fail(QCoreApplication::translate("BuzzImport", "Tablica tekstów jest za duża."));
    QVector<Node> nodes;
    QVector<int> work;
    for (int p = 10; p < header.size() - 2; p += 6) {
        const auto symbol = u16(header, p);
        const auto weight = u32(header, p + 2, true);
        if (!weight)
            fail(QCoreApplication::translate("BuzzImport", "Zerowa częstotliwość Huffmana."));
        nodes << Node{weight, int(symbol), -1, -1};
        work << nodes.size() - 1;
    }
    if (work.isEmpty() || u16(header, header.size() - 2) != 0xfe00)
        fail(QCoreApplication::translate("BuzzImport", "Niepoprawna tablica Huffmana."));
    // The game orders symbols by weight (stable for equal weights) before
    // building the tree. Most tables are stored sorted; some Quiz TV DLC
    // tables (e.g. Comedy, PACK0022) are not.
    std::stable_sort(work.begin(), work.end(),
                     [&nodes](int a, int b) { return nodes[a].weight < nodes[b].weight; });
    while (work.size() > 1) {
        const int left = work.takeFirst(), right = work.takeFirst();
        nodes << Node{nodes[left].weight + nodes[right].weight, -1, left, right};
        const int id = nodes.size() - 1;
        int at = 0;
        while (at < work.size() && nodes[work[at]].weight <= nodes[id].weight)
            ++at;
        work.insert(at, id);
    }
    QStringList out;
    quint64 bit = 0, symbols = 0;
    const int root = work[0];
    for (quint32 i = 0; i < count; ++i) {
        QString s;
        int node = root;
        for (;;) {
            if (nodes[node].symbol < 0) {
                if (bit >= quint64(stream.size()) * 8)
                    fail(QCoreApplication::translate("BuzzImport", "Ucięty strumień tekstów."));
                const int b = (quint8(stream[int(bit >> 3)]) >> (7 - (bit & 7))) & 1;
                ++bit;
                node = b ? nodes[node].right : nodes[node].left;
                continue;
            }
            ++symbols;
            if (symbols > total)
                fail(QCoreApplication::translate("BuzzImport", "Niezgodna długość tekstów."));
            if (nodes[node].symbol == 0xfefe)
                break;
            s += QChar(ushort(nodes[node].symbol));
            node = root;
        }
        out << s;
        bit = (bit + 7) & ~quint64(7);
    }
    if (symbols != total || bit / 8 != quint64(stream.size()))
        fail(QCoreApplication::translate("BuzzImport", "Niezgodna długość strumienia Huffmana."));
    return out;
}
namespace {
// Language folders ("POL/ROUNDS/…") of the question databases in one archive.
QMap<QString, QString> archiveLanguages(Archive &archive) {
    QMap<QString, QString> languages;
    for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it)
        if (it.key().endsWith(".qak")) {
            const auto folder = it.key().section('/', 0, 0);
            const auto code = languageCode(folder);
            if (!code.isEmpty())
                languages[code] = folder;
        }
    return languages;
}
enum class UnsupportedAnswer { Count, Pictures, NoCorrectAnswer, CannotAdapt };
struct Tally {
    QHash<QString, qsizetype> ids;
    QHash<QString, UnsupportedAnswer> unsupported;
    int pictures = 0, adapted = 0, duplicates = 0;
    QStringList pictureWarnings, musicWarnings;
    QMap<QString, QString> gameText;
    bool gameTextRead = false;
};
// One stream decode per sheet, with passthrough timing: SDI slots are picture
// ordinals, not timestamps. Only publish if the decoder emits exactly the
// declared number of pictures, so dropped/duplicated frames cannot shift links.
QMap<int, QString> importPictureSheet(const std::function<QByteArray()> &readSheet, const QString &name,
                                      const QSet<int> &requested, const QString &cache, const QString &ffmpeg,
                                      const std::atomic_bool *cancel = nullptr) {
    QMap<int, QString> pictures;
    const auto sheetKey = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    const auto cachePath = [&](int slot) {
        return QDir(cache).filePath(QString("sdi-picture-v1-%1-%2.png").arg(QString::fromLatin1(sheetKey)).arg(slot));
    };
    for (int slot : requested)
        if (QFileInfo(cachePath(slot)).size() > 0)
            pictures[slot] = cachePath(slot);
    if (pictures.size() == requested.size())
        return pictures;
    if (ffmpeg.isEmpty())
        fail(missingMediaFfmpeg());
    checkCancelled(cancel);
    const auto sheet = readSheet();
    check(sheet, 0, 0x50);
    const auto table = u32(sheet, 36, true), offset = u32(sheet, 40, true), count = u32(sheet, 52, true);
    const auto width = u32(sheet, 16, true), height = u32(sheet, 20, true);
    if (sheet.mid(64, 8) != "SDIVIDEO" || u32(sheet, 0, true) != 1 ||
        !count || count > 4096 || !width || !height || quint64(width) * height > 1920u * 1080u ||
        quint64(width) * height * count > 512u * 1024u * 1024u ||
        table < 0x50 || quint64(table) + count * 4 > offset || offset >= quint64(sheet.size()) ||
        u32(sheet, 48, true) != sheet.size())
        fail(QCoreApplication::translate("BuzzImport", "Niepoprawny arkusz obrazków SDI."));
    for (quint32 i = 0; i < count; ++i) {
        const auto frame = u32(sheet, table + i * 4, true);
        if (frame < offset || quint64(frame) + 4 > quint64(sheet.size()) ||
            sheet.mid(frame, 4) != QByteArray::fromHex("000001ba"))
            fail(QCoreApplication::translate("BuzzImport", "Niepoprawny indeks obrazków SDI."));
    }
    qsizetype remaining = 0;
    for (int slot : requested)
        if (quint32(slot) < count && !pictures.contains(slot))
            ++remaining;
    if (!remaining)
        return pictures;
    QTemporaryDir temp(QDir(cache).filePath("sdi-decode-XXXXXX"));
    if (!temp.isValid())
        fail(temp.errorString());
    QFile input(temp.filePath("sheet.sdi"));
    if (!input.open(QIODevice::WriteOnly) || input.write(sheet) != sheet.size() || !input.flush())
        fail(input.errorString());
    input.close();
    // Export the entire sheet into a temporary directory, including frame 0.
    // Retain only referenced frames after verifying all ordinals are present.
    QProcess decoder;
    const auto decode = [&](bool legacy) {
        QStringList args{"-nostdin", "-v", "error", "-xerror", "-y", "-protocol_whitelist", "file,pipe",
                         "-threads", "1", "-filter_threads", "1", "-max_pixels", "2073600",
                         "-skip_initial_bytes", QString::number(offset), "-f", "mpeg", "-i", input.fileName()};
        args << (legacy ? QStringList{"-vsync", "0"} : QStringList{"-fps_mode", "passthrough"});
        args << "-map" << "0:v:0";
        if (width == 640 && height == 368)
            args << "-vf" << "scale=640:360,setsar=1";
        args << "-threads" << "1" << "-start_number" << "0" << "-frames:v" << QString::number(count + 1)
             << temp.filePath("frame-%05d.png");
        decoder.start(ffmpeg, args);
        if (!decoder.waitForStarted(5000))
            fail(decoder.errorString());
        QElapsedTimer timer;
        timer.start();
        while (!decoder.waitForFinished(100)) {
            if (!(cancel && cancel->load()) && timer.elapsed() < 90000)
                continue;
            decoder.kill();
            decoder.waitForFinished(5000);
            checkCancelled(cancel);
            fail(QCoreApplication::translate("BuzzImport", "Przekroczono limit czasu dekodowania obrazków SDI."));
        }
        return decoder.readAllStandardError().trimmed();
    };
    auto error = decode(false);
    if (decoder.exitCode() != 0 && error.contains("Unrecognized option 'fps_mode'"))
        error = decode(true);
    if (decoder.exitStatus() != QProcess::NormalExit || decoder.exitCode() != 0)
        fail(QStringLiteral("FFmpeg: ") + QString::fromUtf8(error.left(800)));
    const auto files = QDir(temp.path()).entryList({"frame-*.png"}, QDir::Files, QDir::Name);
    if (files.size() != qsizetype(count))
        fail(QCoreApplication::translate("BuzzImport", "Liczba zdekodowanych klatek SDI nie zgadza się z indeksem."));
    for (int slot : requested) {
        if (quint32(slot) >= count || pictures.contains(slot))
            continue;
        const auto dest = cachePath(slot);
        if (QFileInfo(dest).size() > 0 || QFile::rename(temp.filePath(files[slot]), dest))
            pictures[slot] = dest;
    }
    return pictures;
}
QMap<QString, QString> readNamedText(Archive &archive, const QString &language) {
    QMap<QString, QString> named;
    const QString nt = language + "/NAMEDTEXT/default";
    if (archive.entries.contains(nt + ".hdr") && archive.entries.contains(nt + ".ndx")) {
        const auto strings = decodeHuffman(archive.read(nt + ".hdr"), archive.read(nt + ".hst"));
        const auto lines = QString::fromUtf8(archive.read(nt + ".ndx")).remove(QChar(0xfeff)).split('\n');
        for (const auto &line : lines) {
            const auto parts = line.simplified().split(' ');
            if (parts.size() == 2) {
                const int i = parts[1].toInt();
                if (i > 0 && i <= strings.size())
                    named[parts[0]] = strings[i - 1];
            }
        }
    }
    return named;
}

QString namedTextHash(const QString &key) {
    return QString::number(qint32(crc32(key.toUtf8()) ^ 0xffffffffu ^ 0xfabcdef7u));
}

void importArchive(Archive &archive, const BuzzPack &source, const QString &selected, bool extractSpeech,
                   Tally &tally, Document &document) {
    const auto languages = archiveLanguages(archive);
    if (!languages.contains(selected))
        return;
    const auto language = languages[selected];
    const auto named = readNamedText(archive, language);
    QMap<int, QString> categories, sourceKeys;
    for (const auto &name : {language + "/SCRIPTS/GENERATED/questioncategories.clu",
                             language + "/SCRIPTS/questioncategories.clu"}) {
        if (!archive.entries.contains(name))
            continue;
        const auto keys = categoryKeys(parseLua(archive.read(name)));
        sourceKeys = keys;
        for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
            categories[it.key()] = named.value(namedTextHash(it.value()),
                it.value().startsWith("CAT_") ? it.value().mid(4).replace('_', ' ') : it.value());
        }
    }
    QHash<QString, QString> roundSubjects;
    QHash<int, QString> packTitles;
    if (sourceKeys.contains(115) || sourceKeys.isEmpty()) {
        const auto gen = language + "/SCRIPTS/GENERATED/";
        if (archive.entries.contains(gen + "roundsetup.clu")) {
            for (const auto &args : literalCalls(parseLua(archive.read(gen + "roundsetup.clu")), "AddRound")) {
                const auto subject = args.value(4).toString();
                const auto round = args.value(6).toString();
                if (!subject.isEmpty() && !round.isEmpty())
                    roundSubjects.insert(round.toCaseFolded(), subject);
            }
        }
        if (archive.entries.contains(gen + "addpackinfo.clu"))
            for (const auto &args : literalCalls(parseLua(archive.read(gen + "addpackinfo.clu")), "AddPackInfo"))
                packTitles.insert(args.value(1).toInt(), args.value(3).toString());
    }
    auto gameLabel = [&](const QString &key) -> QString {
        if (key.isEmpty())
            return {};
        const auto hash = namedTextHash(key);
        if (const auto label = named.value(hash).simplified(); !label.isEmpty())
            return label;
        if (!tally.gameTextRead) {
            tally.gameTextRead = true;
            try {
                if (auto game = ps3::openGameLanguageArchive(source.sourcePath, language))
                    tally.gameText = readNamedText(*game, language);
            } catch (const std::runtime_error &) {
                // The disc's shared labels are optional for importing questions.
            }
        }
        return tally.gameText.value(hash).simplified();
    };
    const auto cache = importCache(source.identity);
    if (extractSpeech && !QDir().mkpath(cache))
        fail(QCoreApplication::translate("BuzzImport", "Nie można utworzyć katalogu mediów."));
    QMap<int, QVector<qsizetype>> pendingPictures;
    QHash<QString, QString> musicPaths;
    QString musicDirectory;
    for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it) {
        const auto name = it.key();
        if (!name.startsWith(language + "/") || !name.endsWith(".qak"))
            continue;
        const auto base = name.left(name.size() - 4);
        const auto b = archive.read(name);
        const auto text = decodeHuffman(archive.read(base + ".hdr"), archive.read(base + ".hst"));
        if (u16(b, 0) != 0xfffe || (u16(b, 4) != 1002 && u16(b, 4) != 1003))
            fail(QCoreApplication::translate("BuzzImport", "Nieobsługiwana wersja QAK."));
        const int version = u16(b, 4), pack = u16(b, 0x12);
        const auto sectionKey = roundSubjects.value(base.section('/', -1).toCaseFolded());
        QString roundParentKey;
        QJsonArray roundCategory;
        if (version == 1002 && !sectionKey.isEmpty()) {
            const auto titleKey = packTitles.value(pack);
            const auto packKey = QString("PACK%1").arg(pack, 4, 10, QLatin1Char('0'));
            const auto rootLabel = gameLabel(titleKey);
            const auto sectionLabel = gameLabel(sectionKey);
            roundParentKey = titleKey.isEmpty() ? packKey : titleKey;
            roundCategory << (rootLabel.isEmpty() ? packKey : rootLabel)
                          << (sectionLabel.isEmpty()
                              ? QString(sectionKey).remove(QRegularExpression("^SUBJECT_")).replace('_', ' ')
                              : sectionLabel);
        }
        const auto table = u32(b, 0x0c);
        check(b, table, 88);
        const int strides[] = {16, 36, 4, 8, 4, 2, 2};
        QVector<QPair<quint32, int>> sections;
        for (int s = 0; s < 7; ++s) {
            const auto offset = u32(b, table + s * 8);
            const int count = u16(b, table + s * 8 + 4);
            check(b, offset, qint64(count) * strides[s]);
            sections << qMakePair(offset, count);
        }
        auto row = [&](int section, int index) {
            if (index < 0 || index >= sections[section].second)
                fail(QCoreApplication::translate("BuzzImport", "Niepoprawne odwołanie do wiersza QAK."));
            return int(sections[section].first) + index * strides[section];
        };
        auto string = [&](int n) {
            if (n < 1 || n > text.size())
                fail(QCoreApplication::translate("BuzzImport", "Niepoprawny numer tekstu QAK."));
            return text[n - 1];
        };
        for (int i = 0; i < sections[1].second; ++i) {
            const int p = row(1, i);
            const auto original = u32(b, p);
            const auto id = QString("buzz:%1:%2:%3").arg(version).arg(pack).arg(original);
            QJsonObject origin{{"archive", source.label}, {"round", base}};
            if (!sectionKey.isEmpty())
                origin["section"] = sectionKey;
            if (const auto existing = tally.ids.constFind(id); existing != tally.ids.cend()) {
                auto &question = document.questions[existing.value()];
                auto localized = question.localized();
                auto rounds = localized["source_rounds"].toArray();
                if (!rounds.contains(origin)) {
                    rounds << origin;
                    localized["source_rounds"] = rounds;
                    question.translations[selected] = localized;
                }
                ++tally.duplicates;
                continue;
            }
            const int answerCount = quint8(b[p + 35]);
            if (answerCount < 2 || answerCount > 4) {
                tally.unsupported.insert(id, UnsupportedAnswer::Count);
                continue;
            }
            // In Quiz TV, answer flag 2 refers to a media-table row, not a string number.
            QList<int> correctAnswers;
            bool pictureAnswers = false;
            for (int a = 0; a < answerCount; ++a) {
                const int ar = row(4, u16(b, p + 24) + a);
                const auto flags = quint8(b[ar + 2]);
                if (flags & 4)
                    correctAnswers << a;
                pictureAnswers |= bool(flags & 2);
            }
            if (pictureAnswers || correctAnswers.isEmpty()) {
                tally.unsupported.insert(id, pictureAnswers ? UnsupportedAnswer::Pictures : UnsupportedAnswer::NoCorrectAnswer);
                continue;
            }
            auto q = newQuestion();
            q.language = selected;
            q.id = id;
            // Pack numbers: Quiz TV discs/DLC 1..49, Brain of... 50..59, Quiz World 100+.
            q.source = pack >= 50 && pack < 60 ? "Buzz! Brain of..."
                       : version == 1002       ? "Buzz! Quiz TV"
                                               : "Buzz! Quiz World";
            q.extra["source_archive"] = source.label;
            q.extra["source_question_id"] = double(original);
            q.extra["source_round"] = base;
            q.difficulty = std::clamp(int(quint8(b[p + 30])), 1, 3);
            q.answers.clear();
            for (int a = 0; a < answerCount; ++a) {
                const int ar = row(4, u16(b, p + 24) + a);
                q.answers << string(u16(b, ar));
            }
            for (int t = 0; t < quint8(b[p + 33]); ++t) {
                const int tr = row(3, u16(b, p + 16) + t);
                const int kind = quint8(b[tr + 6]);
                if (kind == 1) {
                    q.text = string(u16(b, tr + 4));
                    if (extractSpeech)
                        q.speech = cachedAsset(archive,
                                               language + "/QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/" +
                                                   QString::number(6000000000ll + u32(b, tr)) + ".rsf",
                                               cache);
                } else if (kind == 2)
                    q.topic = string(u16(b, tr + 4));
            }
            if (!adaptImportedAnswers(q, correctAnswers)) {
                tally.unsupported.insert(id, UnsupportedAnswer::CannotAdapt);
                continue;
            }
            tally.unsupported.remove(id);
            tally.ids.insert(id, document.questions.size());
            tally.adapted += answerCount != 4 || correctAnswers.size() != 1;
            QJsonArray cats;
            for (int c = 0; c < quint8(b[p + 34]); ++c)
                cats << int(u16(b, row(5, u16(b, p + 18) + c)));
            q.subjectId = cats.size() > 0 ? cats[0].toInt() : 0;
            q.categoryId = cats.size() > 1 ? cats[1].toInt() : q.subjectId;
            q.extra["source_subject_key"] = sourceKeys.value(q.subjectId);
            q.extra["source_category_key"] = sourceKeys.value(q.categoryId);
            q.category = categories.value(q.subjectId, q.topic.isEmpty() ? categoryName(q.subjectId) : q.topic);
            q.extra["source_category_ids"] = cats;
            QJsonArray sourceCategoryPath;
            if (!roundCategory.isEmpty() &&
                (!q.subjectId || q.subjectId == 115 || sourceKeys.value(q.subjectId) == "QC_UNCATEGORISED")) {
                q.category = roundCategory.first().toString();
                q.extra["source_subject_key"] = roundParentKey;
                q.extra["source_category_key"] = sectionKey;
                sourceCategoryPath = roundCategory;
            } else if (q.subjectId) {
                sourceCategoryPath << categories.value(q.subjectId, categoryName(q.subjectId));
                if (q.categoryId && q.categoryId != q.subjectId)
                    sourceCategoryPath << categories.value(q.categoryId, categoryName(q.categoryId));
            }
            q.extra["source_category_path"] = sourceCategoryPath;
            if (version == 1003 && pack == 100)
                q.extra["source_uses_quiz_world_categories"] = true;
            if (quint8(b[p + 32])) {
                QJsonArray media;
                for (int m = 0; m < quint8(b[p + 32]); ++m) {
                    const int link = row(2, u16(b, p + 14) + m), mr = row(0, u16(b, link));
                    const auto type = quint8(b[mr + 12]);
                    const auto asset = u32(b, mr + 4);
                    QJsonObject ref{{"type", int(type)}, {"asset", double(asset)},
                                    {"slot", int(u16(b, mr + 10))}, {"aspect", int(quint8(b[mr + 14]))}};
                    if (type == 1 || type == 2) {
                        const auto entry = type == 1 ? musicEntry(language, asset)
                            : language + "/QUESTIONASSETS/VIDEO/" + QString::number(1000000ull + asset) + ".sdi";
                        if (archive.entries.contains(entry)) {
                            ref["entry"] = entry;
                            ref["archive"] = source.label;
                            ref["source_path"] = source.sourcePath;
                        }
                        if (extractSpeech && type == 1 && q.music.isEmpty()) {
                            if (!musicPaths.contains(entry)) {
                                QString path;
                                try {
                                    if (musicDirectory.isEmpty()) {
                                        const auto directory = musicCache(source.sourcePath, source.label);
                                        if (!QDir().mkpath(directory))
                                            fail(QCoreApplication::translate("BuzzImport", "Nie można utworzyć katalogu mediów."));
                                        musicDirectory = directory;
                                    }
                                    path = cachedAsset(archive, entry, musicDirectory, true);
                                } catch (const std::exception &e) {
                                    if (tally.musicWarnings.size() < 5)
                                        tally.musicWarnings << QString::fromUtf8(e.what());
                                }
                                musicPaths.insert(entry, path);
                            }
                            q.music = musicPaths.value(entry);
                        }
                    }
                    media << ref;
                }
                q.extra["source_media"] = media;
                // Type 3 = still image on the question sheet; slot 0 is the
                // missing-picture placeholder.
                if (extractSpeech)
                    for (const auto &m : media) {
                        const auto ref = m.toObject();
                        const int slot = ref["slot"].toInt();
                        if (ref["type"].toInt() == 3 && slot > 0) {
                            pendingPictures[slot] << document.questions.size();
                            break;
                        }
                    }
            }
            auto localized = q.localized();
            localized["source_media"] = q.extra["source_media"].toArray();
            localized["source_archive"] = source.label;
            localized["source_round"] = base;
            localized["source_rounds"] = QJsonArray{origin};
            q.translations[selected] = localized;
            document.questions << q;
        }
    }
    if (!pendingPictures.isEmpty()) {
        try {
            const QString expected = language + "/QUESTIONASSETS/VIDEO/pics001.sdi";
            QString sheet;
            for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it)
                if (it.key().compare(expected, Qt::CaseInsensitive) == 0) {
                    sheet = it.key();
                    break;
                }
            if (sheet.isEmpty())
                fail(QCoreApplication::translate("BuzzImport", "Nie znaleziono arkusza obrazków do pytań."));
            const auto keys = pendingPictures.keys();
            const auto pictures = importPictureSheet([&] { return archive.read(sheet); }, sheet,
                                                     QSet<int>(keys.cbegin(), keys.cend()),
                                                     cache, configuredMediaFfmpeg());
            for (auto it = pictures.cbegin(); it != pictures.cend(); ++it)
                for (const auto index : pendingPictures.value(it.key())) {
                    auto &q = document.questions[index];
                    q.image = it.value();
                    auto localized = q.translations.value(selected).toObject();
                    localized["image"] = q.image;
                    q.translations[selected] = localized;
                    ++tally.pictures;
                }
        } catch (const std::exception &e) {
            tally.pictureWarnings << source.label + ": " + QString::fromUtf8(e.what());
        }
    }
}
} // namespace

namespace {
struct BuzzPicture {
    QString archive, sheet;
    int slot = 0;
};
static const QRegularExpression innerPath(":(?:PS3_GAME|USRDIR)/", QRegularExpression::CaseInsensitiveOption);

struct BuzzMusic {
    QString archive, source, entry;
};
BuzzMusic buzzMusic(const Question &question) {
    const auto ref = question.sourceMusic();
    if (ref["type"] != 1 || !question.hasUnresolvedMusic())
        return {};
    const auto localized = question.translations.value(question.language).toObject();
    const auto archive = localized["source_archive"].toString(question.extra["source_archive"].toString());
    const auto round = localized["source_round"].toString(question.extra["source_round"].toString());
    const auto language = round.section('/', 0, 0);
    const auto asset = ref["asset"].toInteger(-1);
    if (archive.isEmpty() || !round.contains('/') || languageCode(language) != question.language ||
        asset < 0 || asset > 0xffffffffll)
        return {};
    // Existing projects stored the ISO and the pack name together in source_archive.
    const auto split = innerPath.match(archive).capturedStart();
    auto source = ref["source_path"].toString();
    if (source.isEmpty() && split >= 0)
        source = archive.left(split);
    return {archive, source, musicEntry(language, quint32(asset))};
}

BuzzPicture buzzPicture(const Question &question) {
    const auto localized = question.translations.value(question.language).toObject();
    const auto archive = localized["source_archive"].toString(question.extra["source_archive"].toString());
    const auto round = localized["source_round"].toString(question.extra["source_round"].toString());
    const auto language = round.section('/', 0, 0);
    if (archive.isEmpty() || !round.contains('/') || languageCode(language) != question.language)
        return {};
    const auto refs = question.translations.isEmpty() ? question.extra["source_media"] : localized["source_media"];
    for (const auto &value : refs.toArray()) {
        const auto ref = value.toObject();
        const int slot = ref["slot"].toInt();
        if (ref["type"].toInt() == 3 && slot > 0)
            return {archive, language + "/QUESTIONASSETS/VIDEO/pics001.sdi", slot};
    }
    return {};
}
} // namespace

bool hasBuzzSourcePicture(const Question &question) { return buzzPicture(question).slot > 0; }
bool hasBuzzSourceMusic(const Question &question) {
    return hasPs2Music(question) || hasPspMusic(question) || !buzzMusic(question).entry.isEmpty();
}

void recoverBuzzMusic(QVector<Question> &questions, QStringList &warnings, const std::atomic_bool *cancel) {
    recoverPs2Music(questions, warnings, cancel);
    recoverPspMusic(questions, warnings, cancel);
    QMap<QPair<QString, QString>, QMap<QString, QVector<qsizetype>>> sources;
    for (qsizetype row = 0; row < questions.size(); ++row) {
        const auto music = buzzMusic(questions[row]);
        if (!music.entry.isEmpty())
            sources[{music.source, music.archive}][music.entry] << row;
    }
    const auto warning = [&](const QString &details) {
        if (warnings.size() < 5)
            warnings << QCoreApplication::translate("BuzzImport", "Nie udało się odzyskać klipu muzycznego.") +
                            " " + details;
    };
    for (auto group = sources.cbegin(); group != sources.cend(); ++group) {
        if (cancel && cancel->load())
            return;
        const auto &[source, label] = group.key();
        const auto cache = musicCache(source, label);
        try {
            auto archive = ps3::openQuestionArchive(source, label);
            if (!QDir().mkpath(cache))
                fail(QCoreApplication::translate("BuzzImport", "Nie można utworzyć katalogu mediów."));
            for (auto item = group->cbegin(); item != group->cend(); ++item) {
                if (cancel && cancel->load())
                    return;
                try {
                    const auto path = cachedAsset(*archive, item.key(), cache, true);
                    if (path.isEmpty()) {
                        warning(QFileInfo(item.key()).fileName());
                        continue;
                    }
                    for (const auto row : item.value()) {
                        auto &question = questions[row];
                        question.music = path;
                        question.translations[question.language] = question.localized();
                    }
                } catch (const std::exception &e) {
                    warning(QString::fromUtf8(e.what()));
                }
            }
        } catch (const std::exception &e) {
            warning(QString::fromUtf8(e.what()));
        }
    }
}

int recoverBuzzPictures(QVector<Question> &questions, const QString &ffmpeg, QStringList &warnings,
                       const std::atomic_bool *cancel) {
    QMap<QPair<QString, QString>, QMap<int, QVector<qsizetype>>> sheets;
    for (qsizetype row = 0; row < questions.size(); ++row) {
        const auto &question = questions.at(row);
        if (!question.hasUnresolvedVisualMedia())
            continue;
        const auto picture = buzzPicture(question);
        if (picture.slot > 0)
            sheets[{picture.archive, picture.sheet}][picture.slot] << row;
    }
    int recovered = 0;
    for (auto it = sheets.cbegin(); it != sheets.cend(); ++it) {
        if (cancel && cancel->load())
            break;
        const auto &[archive, sheet] = it.key();
        const auto split = innerPath.match(archive).capturedStart();
        const auto source = split < 0 ? archive : archive.left(split);
        const auto cacheKey = buzzFilePack(source).identity + '\n' + archive;
        const auto cache = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                               .filePath("imports/" + QString::fromLatin1(QCryptographicHash::hash(
                                   cacheKey.toUtf8(), QCryptographicHash::Sha256).toHex()));
        try {
            if (!QDir().mkpath(cache))
                fail(QCoreApplication::translate("BuzzImport", "Nie można utworzyć katalogu mediów."));
            const auto frames = it->keys();
            const auto pictures = importPictureSheet([&] { return ps3::readQuestionMedia(source, archive, sheet); },
                                                     sheet, QSet<int>(frames.cbegin(), frames.cend()), cache, ffmpeg,
                                                     cancel);
            for (auto picture = pictures.cbegin(); picture != pictures.cend(); ++picture)
                for (const auto row : it->value(picture.key())) {
                    auto &question = questions[row];
                    question.image = picture.value();
                    question.translations[question.language] = question.localized();
                    ++recovered;
                }
        } catch (const std::exception &e) {
            if (cancel && cancel->load())
                break;
            warnings << archive + ": " + QString::fromUtf8(e.what());
        }
    }
    return recovered;
}

QStringList buzzArchiveLanguages(Archive &archive) {
    auto codes = archiveLanguages(archive).keys();
    codes.sort();
    return codes;
}

BuzzPack buzzFilePack(const QString &path) {
    const QFileInfo input(path);
    BuzzPack pack;
    pack.label = input.absoluteFilePath();
    pack.identity = input.absoluteFilePath() + QString::number(input.size()) +
                    input.lastModified().toString(Qt::ISODateWithMs);
    pack.open = [path] { return std::make_unique<Archive>(path); };
    return pack;
}

SourceLanguages buzzLanguages(const QString &path) {
    SourceLanguages result;
    try {
        Archive archive(path);
        result.codes = buzzArchiveLanguages(archive);
        result.preferred = preferredLanguage(result.codes);
        if (result.codes.isEmpty())
            fail(QCoreApplication::translate("BuzzImport", "Brak baz pytań QAK w paczce."));
    } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
    }
    return result;
}

LoadResult importBuzzPacks(const QVector<BuzzPack> &packs, const QString &requestedLanguage, bool extractSpeech,
                           const QString &title) {
    LoadResult out;
    try {
        Tally tally;
        QString selected = requestedLanguage.isEmpty() ? QString() : languageCode(requestedLanguage);
        if (selected.isEmpty()) {
            // Prefer Polish/English when the file names already say which packs are which.
            QStringList named;
            for (const auto &source : packs)
                if (!source.language.isEmpty())
                    named << source.language;
            if (named.size() == packs.size())
                selected = preferredLanguage(named);
        }
        bool found = false;
        for (const auto &source : packs) {
            // A pack whose file name already names another language is not opened.
            if (!source.language.isEmpty() && !selected.isEmpty() && source.language != selected)
                continue;
            const auto archive = source.open();
            if (selected.isEmpty())
                selected = preferredLanguage(buzzArchiveLanguages(*archive));
            if (!archiveLanguages(*archive).contains(selected))
                continue;
            found = true;
            ++out.sourcePacks;
            importArchive(*archive, source, selected, extractSpeech, tally, out.document);
        }
        if (packs.isEmpty())
            fail(QCoreApplication::translate("BuzzImport", "Brak baz pytań QAK w paczce."));
        if (!found)
            fail(QCoreApplication::translate("BuzzImport", "Źródło nie zawiera wybranego języka."));
        out.document.title = title;
        out.document.language = selected;
        out.duplicates = tally.duplicates;
        if (out.document.questions.isEmpty())
            fail(QCoreApplication::translate("BuzzImport", "Paczka nie zawiera obsługiwanych pytań."));
        if (tally.adapted)
            out.warnings << adaptedAnswersWarning(tally.adapted);
        if (!tally.unsupported.isEmpty()) {
            const auto count = [&](UnsupportedAnswer reason) {
                return std::count(tally.unsupported.cbegin(), tally.unsupported.cend(), reason);
            };
            out.warnings << QCoreApplication::translate("BuzzImport",
                "Pominięte pytania (%1). Odpowiedzi obrazkowe: %2. Brak poprawnej odpowiedzi: %3. Liczba odpowiedzi poza zakresem 2–4: %4. Nie udało się utworzyć czterech różnych odpowiedzi: %5.")
                .arg(tally.unsupported.size()).arg(count(UnsupportedAnswer::Pictures))
                .arg(count(UnsupportedAnswer::NoCorrectAnswer)).arg(count(UnsupportedAnswer::Count))
                .arg(count(UnsupportedAnswer::CannotAdapt));
        }
        if (tally.pictures)
            out.warnings << QCoreApplication::translate("BuzzImport", "Zaimportowano obrazki do %1 pytań.")
                                .arg(tally.pictures);
        out.warnings << tally.pictureWarnings;
        out.warnings << tally.musicWarnings;
        if (out.duplicates)
            out.warnings << QCoreApplication::translate(
                                "BuzzImport", "Połączono wystąpienia pytań w rundach: %1. Każde pytanie jest zapisane raz, z listą rund źródłowych.")
                                .arg(out.duplicates);
    } catch (const std::exception &e) {
        out.error = QString::fromUtf8(e.what());
        out.document.questions.clear();
    }
    return out;
}

LoadResult importBuzz(const QString &path, const QString &requestedLanguage, bool extractSpeech) {
    return importBuzzPacks({buzzFilePack(path)}, requestedLanguage, extractSpeech, QFileInfo(path).completeBaseName());
}
} // namespace studio
