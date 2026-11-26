#include "buzz_import.h"
#include "pack_format.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QSaveFile>
#include <QStandardPaths>
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
QMap<int, QString> categoryKeys(const Lua &lua) {
    QMap<int, QString> out;
    QMap<int, QVariant> regs;
    for (auto ins : lua.code) {
        const auto op = ins & 63, a = (ins >> 6) & 255, bx = ins >> 14;
        if (op == 1 || op == 5) {
            if (bx >= quint32(lua.constants.size()))
                fail(QCoreApplication::translate("BuzzImport", "Niepoprawny indeks CLU."));
            regs[a] = lua.constants[bx];
        } else if (op == 28 && regs[0].toString() == "AddQuestionCategoryItem")
            out[regs[1].toInt()] = regs[3].toString();
    }
    return out;
}
QString cachedAsset(Archive &archive, const QString &name, const QString &cache) {
    if (!archive.entries.contains(name))
        return {};
    // Only a generated basename is used; archive paths are never extracted verbatim.
    const auto dest = QDir(cache).filePath(
        QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex()) +
        "." + QFileInfo(name).suffix());
    if (!QFileInfo::exists(dest)) {
        const auto bytes = archive.read(name);
        QSaveFile file(dest);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            fail(file.errorString());
    }
    return dest;
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
struct Tally {
    QSet<QString> ids;
    int media = 0, unsupported = 0, duplicates = 0;
};
void importArchive(Archive &archive, const BuzzPack &source, const QString &selected, bool extractSpeech,
                   Tally &tally, Document &document) {
    const auto languages = archiveLanguages(archive);
    if (!languages.contains(selected))
        return;
    const auto language = languages[selected];
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
    QMap<int, QString> categories, sourceKeys;
    for (const auto &name : {language + "/SCRIPTS/GENERATED/questioncategories.clu",
                             language + "/SCRIPTS/questioncategories.clu"}) {
        if (!archive.entries.contains(name))
            continue;
        const auto keys = categoryKeys(parseLua(archive.read(name)));
        sourceKeys = keys;
        for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
            const auto hash = qint32(crc32(it.value().toUtf8()) ^ 0xffffffffu ^ 0xfabcdef7u);
            categories[it.key()] = named.value(
                QString::number(hash), it.value().startsWith("CAT_") ? it.value().mid(4).replace('_', ' ') : it.value());
        }
    }
    const auto cache =
        QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
            .filePath("imports/" + QString::fromLatin1(
                                       QCryptographicHash::hash(source.identity.toUtf8(), QCryptographicHash::Sha256)
                                           .toHex()));
    if (extractSpeech && !QDir().mkpath(cache))
        fail(QCoreApplication::translate("BuzzImport", "Nie można utworzyć katalogu mediów."));
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
            if (quint8(b[p + 35]) != 4) {
                ++tally.unsupported;
                continue;
            }
            const auto original = u32(b, p);
            const auto sourceId = QString("%1:%2").arg(pack).arg(original);
            if (tally.ids.contains(sourceId)) {
                ++tally.duplicates;
                continue;
            }
            // In Quiz TV, answer flag 2 refers to a media-table row, not a
            // string number. Multi-answer rounds also cannot be represented
            // by the editor's single correct_index. Never reinterpret either.
            int correctCount = 0;
            bool pictureAnswers = false;
            for (int a = 0; a < 4; ++a) {
                const int ar = row(4, u16(b, p + 24) + a);
                const auto flags = quint8(b[ar + 2]);
                correctCount += bool(flags & 4);
                pictureAnswers |= bool(flags & 2);
            }
            if (pictureAnswers || correctCount != 1) {
                ++tally.unsupported;
                continue;
            }
            tally.ids.insert(sourceId);
            auto q = newQuestion();
            q.language = selected;
            q.id = QString("buzz:%1:%2").arg(version).arg(sourceId);
            // Pack numbers: Quiz TV discs/DLC 1..49, Brain of... 50..59, Quiz World 100+.
            q.source = pack >= 50 && pack < 60 ? "Buzz! Brain of..."
                       : version == 1002       ? "Buzz! Quiz TV"
                                               : "Buzz! Quiz World";
            q.extra["source_archive"] = source.label;
            q.extra["source_question_id"] = double(original);
            q.extra["source_round"] = base;
            q.difficulty = std::clamp(int(quint8(b[p + 30])), 1, 3);
            q.answers.clear();
            q.correct = -1;
            for (int a = 0; a < 4; ++a) {
                const int ar = row(4, u16(b, p + 24) + a);
                q.answers << string(u16(b, ar));
                if (quint8(b[ar + 2]) & 4)
                    q.correct = a;
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
            QJsonArray cats;
            for (int c = 0; c < quint8(b[p + 34]); ++c)
                cats << int(u16(b, row(5, u16(b, p + 18) + c)));
            q.subjectId = cats.size() > 0 ? cats[0].toInt() : 0;
            q.categoryId = cats.size() > 1 ? cats[1].toInt() : q.subjectId;
            q.extra["source_subject_key"] = sourceKeys.value(q.subjectId);
            q.extra["source_category_key"] = sourceKeys.value(q.categoryId);
            q.category = categories.value(q.subjectId, q.topic.isEmpty() ? categoryName(q.subjectId) : q.topic);
            q.extra["source_category_ids"] = cats;
            if (quint8(b[p + 32])) {
                ++tally.media;
                QJsonArray media;
                for (int m = 0; m < quint8(b[p + 32]); ++m) {
                    const int link = row(2, u16(b, p + 14) + m), mr = row(0, u16(b, link));
                    media << QJsonObject{{"type", int(quint8(b[mr + 12]))},
                                         {"asset", double(u32(b, mr + 4))},
                                         {"slot", int(u16(b, mr + 10))},
                                         {"aspect", int(quint8(b[mr + 14]))}};
                }
                q.extra["source_media"] = media;
            }
            auto localized = q.localized();
            localized["source_media"] = q.extra["source_media"].toArray();
            localized["source_archive"] = source.label;
            localized["source_round"] = base;
            q.translations[selected] = localized;
            document.questions << q;
        }
    }
}
} // namespace

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
        if (tally.unsupported)
            out.warnings << QCoreApplication::translate(
                                "BuzzImport",
                                "Pominięto %1 pytań z odpowiedziami obrazkowymi, wielokrotnego "
                                "wyboru lub bez czterech odpowiedzi. Ten edytor obsługuje jedną "
                                "poprawną odpowiedź tekstową z czterech.")
                                .arg(tally.unsupported);
        if (tally.media)
            out.warnings << QCoreApplication::translate(
                                "BuzzImport",
                                "%1 pytań ma media w SDI/RSF. Zachowano ich odwołania; podgląd tych "
                                "mediów wymaga osobnej konwersji.")
                                .arg(tally.media);
        if (out.duplicates)
            out.warnings << QCoreApplication::translate(
                                "BuzzImport", "Pominięto %1 powtórzeń identyfikatorów między rundami.")
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
