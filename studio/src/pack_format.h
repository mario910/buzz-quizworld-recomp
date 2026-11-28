#pragma once
#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <QString>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <stdexcept>

// Bounds-checked ZIP and PS2 BPAK reader and binary primitives.
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
inline QString musicEntry(const QString &language, quint32 asset) {
    // Quiz World formats the resource name as "1%06d", including for IDs above six digits.
    return language + "/QUESTIONASSETS/MUSIC/STREAMED/1" + QString::number(asset).rightJustified(6, '0') + ".rsf";
}
constexpr qint64 maxMusicBytes = 16ll * 1024 * 1024;
inline bool isStreamedMusic(const QByteArray &bytes) {
    if (bytes.size() < 32 || bytes.size() > maxMusicBytes || quint8(bytes[0]) != 6 ||
        quint8(bytes[2]) != 2 || u16(bytes, 4, true) != 48000 || u32(bytes, 16, true) != 384)
        return false;
    // The first two lengths can differ on Quiz TV discs. The file can also contain padding.
    const auto first = u32(bytes, 8, true), second = u32(bytes, 12, true);
    const auto stream = u32(bytes, 20, true);
    return first && second && first % 384 == 0 && second % 384 == 0 &&
           stream >= std::max(first, second) && stream % 384 == 0 && stream <= quint64(bytes.size() - 32);
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
    quint32 compressedSize = 0;
};
QByteArray inflateZipMember(const QByteArray &bytes, qsizetype size);
class Archive {
    QFile disk;
    QBuffer memory;
    std::unique_ptr<QIODevice> owned;
    bool rawEntries = false;

  public:
    // Reads go through this device: the file on disk, an archive held in
    // memory (a ZIP nested in another archive) or any random-access device
    // (a file inside a disc image, a decrypted PS3 package entry).
    QIODevice &file;
    QMap<QString, Entry> entries;
    explicit Archive(const QString &path) : disk(path), file(disk) {
        if (!disk.open(QIODevice::ReadOnly))
            fail(disk.errorString());
        readDirectory();
    }
    explicit Archive(const QByteArray &bytes) : file(memory) {
        memory.setData(bytes);
        memory.open(QIODevice::ReadOnly);
        readDirectory();
    }
    explicit Archive(std::unique_ptr<QIODevice> device) : owned(std::move(device)), file(*owned) {
        if (!file.isOpen() && !file.open(QIODevice::ReadOnly))
            fail(file.errorString());
        readDirectory();
    }
    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;

  private:
    void readBpakDirectory(const QByteArray &header) {
        const auto end = u32(header, 4), count = u32(header, 16), start = u32(header, 20);
        const qint64 names = qint64(start) + qint64(count) * 16;
        const auto error = QCoreApplication::translate("BuzzImport", "Niepoprawny katalog paczki.");
        if (start < 28 || names > end || end > 16 * 1024 * 1024 || end > file.size() || !file.seek(0))
            fail(error);
        const auto directory = file.read(end);
        if (directory.size() != end)
            fail(error);
        for (quint32 i = 0; i < count; ++i) {
            const auto row = qint64(start) + i * 16;
            const auto name = u32(directory, row + 4), offset = u32(directory, row + 8),
                       size = u32(directory, row + 12);
            const auto terminator = directory.indexOf('\0', name);
            if (name < names || name >= end || terminator <= name || offset < end ||
                qint64(offset) + size > file.size())
                fail(error);
            const auto key = QString::fromLatin1(directory.mid(name, terminator - name));
            if (entries.contains(key))
                fail(error);
            entries.insert(key, {offset, size});
        }
        rawEntries = true;
    }

    void readDirectory() {
        if (file.size() < 22)
            fail(QCoreApplication::translate("BuzzImport", "To nie jest archiwum paczki Buzz."));
        const auto header = file.peek(28);
        if (header.size() == 28 && header.startsWith("BPAK")) {
            readBpakDirectory(header);
            return;
        }
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
            entry.compressedSize = u32(central, p + 20);
            if (entry.compressedSize != entry.size && entry.method == 0)
                fail(QCoreApplication::translate("BuzzImport", "Niezgodny rozmiar wpisu ZIP."));
            entries.insert(name, entry);
            p += 46 + n + extra + comment;
        }
    }

  public:
    QByteArray read(const QString &name) { return readWithChecksum(name, std::nullopt); }
    QByteArray readDeflated(const QString &name) { return readWithChecksum(name, std::nullopt, true); }

    // One published PS2 disc stores an incorrect CRC for a known asset.
    QByteArray readWithChecksum(const QString &name, std::optional<quint32> correctedChecksum,
                                bool allowDeflate = false) {
        if (!entries.contains(name))
            fail(QCoreApplication::translate("BuzzImport", "Brak pliku %1.").arg(name));
        const auto e = entries[name];
        if ((e.method && !(allowDeflate && e.method == 8)) || (e.flags & 1))
            fail(QCoreApplication::translate("BuzzImport",
                                             "Obsługiwane są nieszyfrowane paczki ZIP bez kompresji."));
        if (e.size > 512u * 1024u * 1024u)
            fail(QCoreApplication::translate("BuzzImport", "Wpis paczki przekracza 512 MB."));
        if (rawEntries) {
            if (!file.seek(e.offset))
                fail(QCoreApplication::translate("BuzzImport", "Ucięte archiwum paczki."));
            const auto bytes = file.read(e.size);
            if (bytes.size() != e.size)
                fail(QCoreApplication::translate("BuzzImport", "Ucięte archiwum paczki."));
            return bytes;
        }
        file.seek(e.offset);
        const auto head = file.read(30);
        if (head.mid(0, 4) != QByteArray("PK\3\4", 4))
            fail(QCoreApplication::translate("BuzzImport", "Uszkodzony nagłówek ZIP."));
        const auto start = qint64(e.offset) + 30 + u16(head, 26) + u16(head, 28);
        if (start + e.compressedSize > file.size())
            fail(QCoreApplication::translate("BuzzImport", "Ucięte archiwum paczki."));
        file.seek(start);
        auto b = file.read(e.compressedSize);
        if (e.method == 8)
            b = inflateZipMember(b, e.size);
        if (b.size() != e.size || crc32(b) != correctedChecksum.value_or(e.crc))
            fail(QCoreApplication::translate("BuzzImport", "Błędna suma kontrolna: %1.").arg(name));
        return b;
    }
};
} // namespace studio::pack
