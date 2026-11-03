#pragma once
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <QString>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <stdexcept>

// Shared bounds-checked stored-ZIP reader and binary primitives.
namespace studio::pack {
[[noreturn]] inline void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
inline void check(const QByteArray &b, qint64 off, qint64 size) {
    if (off < 0 || size < 0 || off > b.size() || size > b.size() - off)
        fail(QCoreApplication::translate("BuzzImport", "Uszkodzony plik: odczyt poza zakresem."));
}
inline quint16 u16(const QByteArray &b, qint64 off, bool be = false) {
    check(b, off, 2);
    return be ? qFromBigEndian<quint16>(b.constData() + off)
              : qFromLittleEndian<quint16>(b.constData() + off);
}
inline quint32 u32(const QByteArray &b, qint64 off, bool be = false) {
    check(b, off, 4);
    return be ? qFromBigEndian<quint32>(b.constData() + off)
              : qFromLittleEndian<quint32>(b.constData() + off);
}
inline quint32 crc32(const QByteArray &b) {
    static const auto table = [] {
        std::array<quint32, 256> result{};
        for (quint32 i = 0; i < 256; ++i) {
            auto v = i;
            for (int bit = 0; bit < 8; ++bit)
                v = (v >> 1) ^ ((v & 1) ? 0xedb88320u : 0u);
            result[i] = v;
        }
        return result;
    }();
    quint32 crc = 0xffffffffu;
    for (unsigned char c : b)
        crc = (crc >> 8) ^ table[(crc ^ c) & 255];
    return ~crc;
}
struct Entry {
    quint32 offset = 0, size = 0, crc = 0;
    quint16 method = 0, flags = 0;
};
class Archive {
  public:
    QFile file;
    QMap<QString, Entry> entries;
    explicit Archive(const QString &path) : file(path) {
        if (!file.open(QIODevice::ReadOnly))
            fail(file.errorString());
        if (file.size() < 22)
            fail(QCoreApplication::translate("BuzzImport", "To nie jest archiwum paczki Buzz."));
        file.seek(std::max<qint64>(0, file.size() - 65557));
        const auto tail = file.readAll();
        int e = tail.size() - 22;
        for (; e >= 0; --e)
            if (tail.mid(e, 4) == QByteArray("PK\5\6", 4) && e + 22 + u16(tail, e + 20) == tail.size())
                break;
        if (e < 0)
            fail(QCoreApplication::translate(
                "BuzzImport", "Nie znaleziono katalogu ZIP. Zaszyfrowany EDAT/NPD nie jest obsługiwany."));
        if (u16(tail, e + 4) || u16(tail, e + 6) || u16(tail, e + 8) != u16(tail, e + 10))
            fail(QCoreApplication::translate("BuzzImport", "Archiwum wieloczęściowe nie jest obsługiwane."));
        const auto count = u16(tail, e + 10);
        const auto size = u32(tail, e + 12);
        const auto start = u32(tail, e + 16);
        if (count == 65535 || size > 16 * 1024 * 1024 || qint64(start) + size > file.size())
            fail(QCoreApplication::translate("BuzzImport", "Niepoprawny katalog paczki."));
        file.seek(start);
        const auto central = file.read(size);
        int p = 0;
        for (int i = 0; i < count; ++i) {
            check(central, p, 46);
            if (central.mid(p, 4) != QByteArray("PK\1\2", 4))
                fail(QCoreApplication::translate("BuzzImport", "Uszkodzony katalog ZIP."));
            const int n = u16(central, p + 28), extra = u16(central, p + 30), comment = u16(central, p + 32);
            check(central, p + 46, n + extra + comment);
            const auto name = QString::fromUtf8(central.mid(p + 46, n));
            if (entries.contains(name))
                fail(QCoreApplication::translate("BuzzImport", "Powtórzona ścieżka w paczce."));
            Entry entry;
            entry.offset = u32(central, p + 42);
            entry.size = u32(central, p + 24);
            entry.crc = u32(central, p + 16);
            entry.method = u16(central, p + 10);
            entry.flags = u16(central, p + 8);
            if (u32(central, p + 20) != entry.size && entry.method == 0)
                fail(QCoreApplication::translate("BuzzImport", "Niezgodny rozmiar wpisu ZIP."));
            entries.insert(name, entry);
            p += 46 + n + extra + comment;
        }
    }
    QByteArray read(const QString &name) {
        if (!entries.contains(name))
            fail(QCoreApplication::translate("BuzzImport", "Brak pliku %1.").arg(name));
        const auto e = entries[name];
        if (e.method || (e.flags & 1))
            fail(QCoreApplication::translate("BuzzImport",
                                             "Obsługiwane są nieszyfrowane paczki ZIP bez kompresji."));
        if (e.size > 512u * 1024u * 1024u)
            fail(QCoreApplication::translate("BuzzImport", "Wpis paczki przekracza 512 MB."));
        file.seek(e.offset);
        const auto head = file.read(30);
        if (head.mid(0, 4) != QByteArray("PK\3\4", 4))
            fail(QCoreApplication::translate("BuzzImport", "Uszkodzony nagłówek ZIP."));
        const auto start = qint64(e.offset) + 30 + u16(head, 26) + u16(head, 28);
        if (start + e.size > file.size())
            fail(QCoreApplication::translate("BuzzImport", "Ucięte archiwum paczki."));
        file.seek(start);
        const auto b = file.read(e.size);
        if (b.size() != e.size || crc32(b) != e.crc)
            fail(QCoreApplication::translate("BuzzImport", "Błędna suma kontrolna: %1.").arg(name));
        return b;
    }
};
} // namespace studio::pack
