#include "pack_codec.h"
#include <QSaveFile>
#include <QSet>
#include <cmath>
#include <cstring>
#include <functional>

namespace studio::pack {
void put(QByteArray &out, quint64 value, int bytes, bool be) {
    for (int i = 0; i < bytes; ++i)
        out.append(char(value >> (8 * (be ? bytes - i - 1 : i))));
}
void patch(QByteArray &out, int offset, quint64 value, int bytes, bool be) {
    check(out, offset, bytes);
    for (int i = 0; i < bytes; ++i)
        out[offset + i] = char(value >> (8 * (be ? bytes - i - 1 : i)));
}
Huffman encodeHuffman(const QStringList &strings) {
    QMap<ushort, quint32> frequencies;
    quint64 total = strings.size();
    for (const auto &text : strings)
        for (auto c : text) {
            if (c.isSurrogate() || c.unicode() == 0xfefe || c.unicode() == 0xfe00)
                fail(QCoreApplication::translate("PackBuilder",
                                                 "Tekst zawiera znak nieobsługiwany przez grę (np. emoji)."));
            ++frequencies[c.unicode()];
            ++total;
        }
    frequencies[0xfefe] += strings.size();
    if (frequencies.size() < 2 || total > 64 * 1024 * 1024)
        fail(QCoreApplication::translate("PackBuilder", "Niepoprawny rozmiar tablicy tekstów."));
    struct Node {
        quint64 weight;
        int symbol, left, right;
    };
    QVector<QPair<ushort, quint32>> rows;
    for (auto it = frequencies.cbegin(); it != frequencies.cend(); ++it)
        rows << qMakePair(it.key(), it.value());
    std::sort(rows.begin(), rows.end(),
              [](auto a, auto b) { return a.second == b.second ? a.first < b.first : a.second < b.second; });
    Huffman result;
    result.header = QByteArray::fromHex("00ff");
    put(result.header, strings.size(), 4, true);
    put(result.header, total, 4, true);
    QVector<Node> nodes;
    QVector<int> work;
    for (const auto &row : rows) {
        put(result.header, row.first, 2);
        put(result.header, row.second, 4, true);
        nodes << Node{row.second, row.first, -1, -1};
        work << nodes.size() - 1;
    }
    put(result.header, 0xfe00, 2);
    while (work.size() > 1) {
        const int left = work.takeFirst(), right = work.takeFirst();
        nodes << Node{nodes[left].weight + nodes[right].weight, -1, left, right};
        const int id = nodes.size() - 1;
        int at = 0;
        while (at < work.size() && nodes[work[at]].weight <= nodes[id].weight)
            ++at;
        work.insert(at, id);
    }
    QMap<int, QByteArray> codes;
    std::function<void(int, QByteArray)> walk = [&](int id, QByteArray code) {
        if (nodes[id].symbol >= 0)
            codes[nodes[id].symbol] = code;
        else {
            walk(nodes[id].left, code + '0');
            walk(nodes[id].right, code + '1');
        }
    };
    walk(work[0], {});
    for (const auto &text : strings) {
        int byte = 0, bits = 0;
        auto emitCode = [&](int symbol) {
            for (char bit : codes[symbol]) {
                byte = (byte << 1) | (bit == '1');
                if (++bits == 8) {
                    result.stream.append(char(byte));
                    bits = byte = 0;
                }
            }
        };
        for (auto c : text)
            emitCode(c.unicode());
        emitCode(0xfefe);
        if (bits)
            result.stream.append(char(byte << (8 - bits)));
    }
    return result;
}
namespace {
const auto luaHeader = QByteArray::fromHex("1b4c75615100000404040400");
class LuaReader {
    const QByteArray &data;
    int pos = 12;

  public:
    explicit LuaReader(const QByteArray &b) : data(b) {}
    quint32 number() {
        const auto n = u32(data, pos, true);
        pos += 4;
        return n;
    }
    quint8 byte() {
        check(data, pos, 1);
        return quint8(data[pos++]);
    }
    int count() {
        const auto n = number();
        if (n > 1000000)
            fail(QCoreApplication::translate("PackBuilder", "Za duży skrypt CLU."));
        return int(n);
    }
    QByteArray string() {
        const auto n = number();
        if (!n)
            return {};
        check(data, pos, n);
        if (data[pos + n - 1] != 0)
            fail(QCoreApplication::translate("PackBuilder", "Niepoprawny tekst CLU."));
        const auto value = data.mid(pos, n - 1);
        pos += n;
        return value.isNull() ? QByteArray("") : value;
    }
    LuaProto proto(int depth = 0) {
        if (depth > 64)
            fail(QCoreApplication::translate("PackBuilder", "Za głęboki skrypt CLU."));
        LuaProto p;
        p.source = string();
        p.firstLine = number();
        p.lastLine = number();
        p.upvalues = byte();
        p.parameters = byte();
        p.vararg = byte();
        p.stack = byte();
        for (int n = count(); n--;)
            p.code << number();
        for (int n = count(); n--;) {
            const auto type = byte();
            if (!type)
                p.constants << QVariant();
            else if (type == 1)
                p.constants << bool(byte());
            else if (type == 3) {
                const auto raw = number();
                float value;
                std::memcpy(&value, &raw, 4);
                if (!std::isfinite(value))
                    fail(QCoreApplication::translate("PackBuilder", "Niepoprawna liczba CLU."));
                p.constants << double(value);
            } else if (type == 4)
                p.constants << QString::fromLatin1(string());
            else
                fail(QCoreApplication::translate("PackBuilder", "Niepoprawna stała CLU."));
        }
        for (int n = count(); n--;)
            p.children << proto(depth + 1);
        for (int n = count(); n--;)
            p.lines << number();
        for (int n = count(); n--;) {
            LuaLocal local;
            local.name = string();
            local.start = number();
            local.end = number();
            p.locals << local;
        }
        for (int n = count(); n--;)
            p.names << string();
        return p;
    }
    bool finished() const { return pos == data.size(); }
};
void luaString(QByteArray &out, const QByteArray &s) {
    put(out, s.isNull() ? 0 : s.size() + 1, 4, true);
    if (!s.isNull()) {
        out += s;
        out.append('\0');
    }
}
void luaProto(QByteArray &out, const LuaProto &p) {
    luaString(out, p.source);
    put(out, p.firstLine, 4, true);
    put(out, p.lastLine, 4, true);
    for (auto v : {p.upvalues, p.parameters, p.vararg, p.stack})
        put(out, v, 1);
    put(out, p.code.size(), 4, true);
    for (auto v : p.code)
        put(out, v, 4, true);
    put(out, p.constants.size(), 4, true);
    for (const auto &v : p.constants) {
        if (!v.isValid())
            put(out, 0, 1);
        else if (v.metaType().id() == QMetaType::Bool) {
            put(out, 1, 1);
            put(out, v.toBool(), 1);
        } else if (v.metaType().id() == QMetaType::QString) {
            const auto text = v.toString();
            if (QString::fromLatin1(text.toLatin1()) != text)
                fail(QCoreApplication::translate("PackBuilder", "Nieobsługiwany znak w nazwie skryptu."));
            put(out, 4, 1);
            luaString(out, text.toLatin1());
        } else {
            put(out, 3, 1);
            const float f = v.toFloat();
            quint32 raw;
            std::memcpy(&raw, &f, 4);
            put(out, raw, 4, true);
        }
    }
    put(out, p.children.size(), 4, true);
    for (const auto &child : p.children)
        luaProto(out, child);
    put(out, p.lines.size(), 4, true);
    for (auto v : p.lines)
        put(out, v, 4, true);
    put(out, p.locals.size(), 4, true);
    for (const auto &v : p.locals) {
        luaString(out, v.name);
        put(out, v.start, 4, true);
        put(out, v.end, 4, true);
    }
    put(out, p.names.size(), 4, true);
    for (const auto &v : p.names)
        luaString(out, v);
}
QByteArray index(QVector<QPair<quint32, quint32>> entries, int offset, bool be) {
    if (entries.isEmpty())
        return QByteArray(24, '\0');
    std::sort(entries.begin(), entries.end());
    const int n = entries.size(), stride = int(std::sqrt(n)), buckets = n / stride;
    QByteArray out;
    for (quint32 v : {entries[0].first, entries.last().first, quint32(buckets), quint32(offset + 24 + n * 8),
                      quint32(n), quint32(offset + 24)})
        put(out, v, 4, be);
    for (const auto &entry : entries) {
        put(out, entry.first, 4, be);
        put(out, entry.second, 4, be);
    }
    for (int i = 0; i < buckets; ++i) {
        put(out, entries[i * stride].first, 4, be);
        put(out, i * stride, 4, be);
    }
    return out;
}
} // namespace
LuaProto readLua(const QByteArray &bytes) {
    if (bytes.left(12) != luaHeader)
        fail(QCoreApplication::translate("PackBuilder", "Nieobsługiwana wersja skryptu CLU."));
    LuaReader reader(bytes);
    auto result = reader.proto();
    if (!reader.finished())
        fail(QCoreApplication::translate("PackBuilder", "Nadmiarowe dane CLU."));
    return result;
}
QByteArray writeLua(const LuaProto &proto) {
    auto out = luaHeader;
    luaProto(out, proto);
    return out;
}
QByteArray script(const QVector<Call> &calls) {
    LuaProto p;
    p.vararg = 2;
    p.stack = 2;
    auto constant = [&](QVariant value) {
        if (value.metaType().id() != QMetaType::Bool && value.metaType().id() != QMetaType::QString)
            value = value.toDouble();
        for (int i = 0; i < p.constants.size(); ++i)
            if (p.constants[i].metaType() == value.metaType() && p.constants[i] == value)
                return i;
        p.constants << value;
        return int(p.constants.size() - 1);
    };
    for (const auto &call : calls) {
        if (call.path.isEmpty() || call.arguments.size() > 200)
            fail(QCoreApplication::translate("PackBuilder", "Niepoprawne wywołanie CLU."));
        p.code << quint32(5 | (constant(call.path[0]) << 14));
        for (const auto &field : call.path.mid(1)) {
            const int k = constant(field);
            if (k > 255)
                fail(QCoreApplication::translate("PackBuilder", "Za dużo stałych tabeli CLU."));
            p.code << quint32(6 | ((256 + k) << 14));
        }
        for (int i = 0; i < call.arguments.size(); ++i)
            p.code << quint32(1 | ((i + 1) << 6) | (constant(call.arguments[i]) << 14));
        p.stack = std::max<int>(p.stack, call.arguments.size() + 1);
        p.code << quint32(28 | ((call.arguments.size() + 1) << 23) | (1 << 14));
    }
    p.code << quint32(30 | (1 << 23));
    return writeLua(p);
}
int replaceConstants(LuaProto &proto, const QMap<QString, QString> &strings, int oldPack, int newPack) {
    int count = 0;
    for (auto &v : proto.constants) {
        if (v.metaType().id() == QMetaType::QString && strings.contains(v.toString())) {
            v = strings[v.toString()];
            ++count;
        } else if (v.metaType().id() == QMetaType::Double && v.toDouble() == oldPack) {
            v = double(newPack);
            ++count;
        }
    }
    for (auto &child : proto.children)
        count += replaceConstants(child, strings, oldPack, newPack);
    return count;
}
QVector<Category> readCategories(const QByteArray &bytes) {
    const auto p = readLua(bytes);
    QVector<Category> categories;
    QVariantList current;
    bool reading = false;
    for (auto ins : p.code) {
        const auto op = ins & 63, k = ins >> 14;
        if ((op == 5 || op == 1) && k >= quint32(p.constants.size()))
            fail(QCoreApplication::translate("PackBuilder", "Niepoprawny indeks CLU."));
        if (op == 5) {
            reading = p.constants[k].toString() == "AddQuestionCategoryItem";
            current.clear();
        } else if (op == 1 && reading)
            current << p.constants[k];
        else if (op == 28 && reading) {
            if (current.size() != 8)
                fail(QCoreApplication::translate("PackBuilder", "Niepoprawna tabela kategorii szablonu."));
            categories << Category{current[0].toInt(),    current[3].toInt(),   current[4].toInt(),
                                   current[5].toInt(),    current[6].toInt(),   current[7].toInt(),
                                   current[1].toString(), current[2].toString()};
            reading = false;
        }
    }
    if (categories.isEmpty())
        fail(QCoreApplication::translate("PackBuilder", "Brak kategorii w szablonie."));
    return categories;
}
RoundBytes writeRound(const QString &name, const QString &language, int pack, int round, int rounds,
                      const QVector<RoundQuestion> &questions) {
    if (questions.isEmpty() || questions.size() * 6 > 65535 || name.size() > 24 || language.size() != 3)
        fail(QCoreApplication::translate("PackBuilder", "Przekroczono limit pytań jednej kategorii."));
    QStringList strings;
    QSet<int> pictureSlots;
    for (const auto &q : questions) {
        strings << q.text << q.topic;
        strings << q.answers;
        if (q.picture)
            pictureSlots.insert(q.picture);
    }
    auto pictures = pictureSlots.values();
    std::sort(pictures.begin(), pictures.end());
    auto qak = [&](bool be) {
        QVector<QByteArray> sections(11);
        QVector<int> counts(11);
        QVector<QPair<quint32, quint32>> mediaIndex, questionIndex, catIndex, roundIndex;
        QMap<int, int> categoryCounts;
        auto add = [&](int sec, quint64 value, int bytes) { put(sections[sec], value, bytes, be); };
        for (int slot : pictures) {
            add(0, slot, 4);
            add(0, slot, 4);
            add(0, 0, 2);
            add(0, slot, 2);
            sections[0] += QByteArray::fromHex("03100400");
            mediaIndex << qMakePair(quint32(slot), 0u);
            ++counts[0];
        }
        for (int i = 0; i < questions.size(); ++i) {
            const auto &q = questions[i];
            const int base = i * 6;
            if (q.answers.size() != 4)
                fail(QCoreApplication::translate("PackBuilder", "Wymagane są cztery odpowiedzi."));
            add(1, q.id, 4);
            sections[1] += language.toLatin1() + '\0';
            add(1, 0, 4);
            add(1, i, 2);
            add(1, q.picture ? counts[2] : 0xffff, 2);
            add(1, i * 2, 2);
            add(1, i * 2, 2);
            add(1, 0, 2);
            add(1, 0x7ffe, 2);
            add(1, i * 4, 2);
            add(1, q.picture ? 0 : 8, 2);
            for (int v : {0, 1 + ((i + 1) % 6), q.difficulty, 1, q.picture ? 1 : 0, 2, 2, 4})
                add(1, v, 1);
            ++counts[1];
            questionIndex << qMakePair(q.id, 0u);
            if (q.picture) {
                add(2, pictures.indexOf(q.picture), 2);
                add(2, 1, 1);
                add(2, 0, 1);
                ++counts[2];
            }
            for (int kind : {1, 2}) {
                add(3, q.speech, 4);
                add(3, base + kind, 2);
                add(3, kind, 1);
                add(3, 0, 1);
                ++counts[3];
            }
            for (int a = 0; a < 4; ++a) {
                add(4, base + 3 + a, 2);
                add(4, a ? 1 : 5, 1);
                add(4, 0, 1);
                ++counts[4];
            }
            for (int c : {q.root, q.category}) {
                add(5, c, 2);
                ++counts[5];
                ++categoryCounts[c];
            }
            add(6, round, 2);
            ++counts[6];
        }
        for (auto it = categoryCounts.cbegin(); it != categoryCounts.cend(); ++it)
            catIndex << qMakePair(quint32(it.key()), quint32(it.value()));
        roundIndex << qMakePair(quint32(round), quint32(questions.size()));
        QByteArray out(0x17c + 88, '\0');
        patch(out, 0, 0xfffe, 2, be);
        patch(out, 2, 1, 2, be);
        patch(out, 4, 1003, 2, be);
        patch(out, 8, 11, 4, be);
        patch(out, 12, 0x17c, 4, be);
        patch(out, 16, rounds, 2, be);
        patch(out, 18, pack, 2, be);
        out.replace(0x14, name.size(), name.toLatin1());
        const QByteArray copyright("(C) Copyright Relentless Software ltd., all rights reserved. ");
        out.replace(0xd4, copyright.size(), copyright);
        out.replace(0x174, 3, language.toLatin1());
        out[0x178] = 1;
        for (int s = 0; s < 7; ++s) {
            patch(out, 0x17c + s * 8, counts[s] ? out.size() : 0, 4, be);
            patch(out, 0x180 + s * 8, counts[s], 2, be);
            out[0x182 + s * 8] = char(s);
            out += sections[s];
        }
        out += QByteArray((-out.size()) & 3, '\0');
        for (int s : {8, 7, 9, 10}) {
            patch(out, 0x17c + s * 8, out.size(), 4, be);
            patch(out, 0x180 + s * 8, 1, 2, be);
            out[0x182 + s * 8] = char(s);
            out += index(s == 8   ? questionIndex
                         : s == 7 ? mediaIndex
                         : s == 9 ? catIndex
                                  : roundIndex,
                         out.size(), be);
        }
        return out;
    };
    const auto text = encodeHuffman(strings);
    return {text.header, text.stream, qak(false), qak(true)};
}
QByteArray swapRound(const QByteArray &little) {
    auto out = little;
    auto swap = [&](int offset, int size) {
        check(little, offset, size);
        std::reverse(out.begin() + offset, out.begin() + offset + size);
    };
    for (int off : {0, 2, 4, 6, 16, 18})
        swap(off, 2);
    swap(8, 4);
    swap(12, 4);
    const auto table = u32(little, 12), sections = u32(little, 8);
    if (sections != 11)
        fail(QCoreApplication::translate("PackBuilder", "Nieobsługiwana wersja QAK."));
    const int strides[7] = {16, 36, 4, 8, 4, 2, 2};
    for (quint32 s = 0; s < sections; ++s) {
        const int row = table + s * 8;
        const int offset = u32(little, row), count = u16(little, row + 4);
        check(little, row, 8);
        const int kind = quint8(little[row + 6]);
        swap(row, 4);
        swap(row + 4, 2);
        if (!count)
            continue;
        if (kind < 7) {
            check(little, offset, qint64(count) * strides[kind]);
            for (int i = 0; i < count; ++i) {
                const int p = offset + i * strides[kind];
                if (kind == 0) {
                    swap(p, 4);
                    swap(p + 4, 4);
                    swap(p + 8, 2);
                    swap(p + 10, 2);
                } else if (kind == 1) {
                    swap(p, 4);
                    swap(p + 8, 4);
                    for (int j = 12; j <= 26; j += 2)
                        swap(p + j, 2);
                } else if (kind == 3) {
                    swap(p, 4);
                    swap(p + 4, 2);
                } else
                    swap(p, 2);
            }
        } else if (kind <= 10) {
            const auto buckets = u32(little, offset + 8), bp = u32(little, offset + 12),
                       entries = u32(little, offset + 16), ep = u32(little, offset + 20);
            check(little, bp, quint64(buckets) * 8);
            check(little, ep, quint64(entries) * 8);
            for (int j = 0; j < 24; j += 4)
                swap(offset + j, 4);
            for (quint32 j = 0; j < entries; ++j) {
                swap(ep + j * 8, 4);
                swap(ep + j * 8 + 4, 4);
            }
            for (quint32 j = 0; j < buckets; ++j) {
                swap(bp + j * 8, 4);
                swap(bp + j * 8 + 4, 4);
            }
        } else
            fail(QCoreApplication::translate("PackBuilder", "Nieobsługiwana wersja QAK."));
    }
    return out;
}
void writeArchive(const QString &path, const Entries &entries) {
    if (entries.size() > 65534)
        fail(QCoreApplication::translate("PackBuilder", "Za dużo plików w paczce."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(file.errorString());
    QByteArray central;
    QSet<QString> names;
    auto write = [&](const QByteArray &b) {
        if (file.write(b) != b.size())
            fail(file.errorString());
    };
    for (const auto &entry : entries) {
        const auto name = entry.first.toLatin1();
        const auto &data = entry.second;
        if (QString::fromLatin1(name) != entry.first || names.contains(entry.first))
            fail(QCoreApplication::translate("PackBuilder", "Powtórzona lub niepoprawna ścieżka paczki."));
        names.insert(entry.first);
        write(QByteArray((-(file.pos() + 30 + name.size())) & 15, '\0'));
        const auto offset = file.pos();
        const auto crc = crc32(data);
        QByteArray head;
        put(head, 0x04034b50, 4);
        put(head, 20, 2);
        head += QByteArray(8, '\0');
        put(head, crc, 4);
        put(head, data.size(), 4);
        put(head, data.size(), 4);
        put(head, name.size(), 2);
        put(head, 0, 2);
        write(head + name);
        write(data);
        put(central, 0x02014b50, 4);
        put(central, 20, 2);
        put(central, 20, 2);
        central += QByteArray(8, '\0');
        put(central, crc, 4);
        put(central, data.size(), 4);
        put(central, data.size(), 4);
        put(central, name.size(), 2);
        central += QByteArray(12, '\0');
        put(central, offset, 4);
        central += name;
        if (file.pos() > 0xffffffffll)
            fail(QCoreApplication::translate("PackBuilder", "Paczka przekracza limit ZIP 4 GB."));
    }
    const auto start = file.pos();
    write(central);
    QByteArray end;
    put(end, 0x06054b50, 4);
    end += QByteArray(4, '\0');
    put(end, entries.size(), 2);
    put(end, entries.size(), 2);
    put(end, central.size(), 4);
    put(end, start, 4);
    put(end, 0, 2);
    write(end);
    if (!file.commit())
        fail(file.errorString());
}
QByteArray namedIndex(const QStringList &lines) {
    return QByteArray::fromHex("efbbbf") + (lines.join("\r\n") + "\r\n").toUtf8();
}
} // namespace studio::pack
