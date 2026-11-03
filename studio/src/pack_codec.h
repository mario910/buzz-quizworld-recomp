#pragma once
#include "pack_format.h"
#include <QVariant>
#include <QVector>

namespace studio::pack {
void put(QByteArray &out, quint64 value, int bytes, bool be = false);
void patch(QByteArray &out, int offset, quint64 value, int bytes, bool be = false);
struct Huffman {
    QByteArray header, stream;
};
Huffman encodeHuffman(const QStringList &strings);
struct LuaLocal {
    QByteArray name;
    quint32 start = 0, end = 0;
};
struct LuaProto {
    QByteArray source;
    quint32 firstLine = 0, lastLine = 0;
    quint8 upvalues = 0, parameters = 0, vararg = 0, stack = 0;
    QVector<quint32> code, lines;
    QVector<QVariant> constants;
    QVector<LuaProto> children;
    QVector<LuaLocal> locals;
    QVector<QByteArray> names;
};
LuaProto readLua(const QByteArray &bytes);
QByteArray writeLua(const LuaProto &proto);
struct Call {
    QStringList path;
    QVariantList arguments;
};
QByteArray script(const QVector<Call> &calls);
int replaceConstants(LuaProto &proto, const QMap<QString, QString> &strings, int oldPack, int newPack);
struct Category {
    int id = 0, parent = 0, media = 0, text = 0, pack = 0, round = 0;
    QString name, key;
};
QVector<Category> readCategories(const QByteArray &bytes);
struct RoundQuestion {
    quint32 id = 0, speech = 0;
    QString text, topic;
    QStringList answers; // Correct first.
    int difficulty = 2, root = 0, category = 0, picture = 0;
};
struct RoundBytes {
    QByteArray header, stream, little, big;
};
RoundBytes writeRound(const QString &name, const QString &language, int pack, int round, int rounds,
                      const QVector<RoundQuestion> &questions);
QByteArray swapRound(const QByteArray &little);
using Entries = QVector<QPair<QString, QByteArray>>;
void writeArchive(const QString &path, const Entries &entries);
QByteArray namedIndex(const QStringList &lines);
} // namespace studio::pack
