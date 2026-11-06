#include "common.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace prepare {
void range(const QByteArray &data, quint64 offset, quint64 size) {
    require(offset <= quint64(data.size()) && size <= quint64(data.size()) - offset,
            "Truncated or invalid PS3 container range.");
}
QByteArray part(const QByteArray &data, quint64 offset, quint64 size) {
    range(data, offset, size);
    return data.mid(qsizetype(offset), qsizetype(size));
}
QByteArray readFile(const QString &path, quint64 limit) {
    require(QFileInfo(path).isFile(), "Expected a regular input file: " + path);
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot open input file: " + path);
    require(file.size() >= 0 && quint64(file.size()) <= limit,
            "Input file exceeds the preparation size limit.");
    const auto data = file.readAll();
    require(file.error() == QFileDevice::NoError && data.size() == file.size(),
            "Input file changed or could not be read.");
    return data;
}
QString fileHash(const QString &path, const Cancel &cancel) {
    require(QFileInfo(path).isFile(), "Expected a regular input file: " + path);
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot open input file: " + path);
    const auto initial = file.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 read = 0;
    while (!file.atEnd()) {
        cancelled(cancel);
        const auto bytes = file.read(8 * 1024 * 1024);
        require(file.error() == QFileDevice::NoError, "Input file could not be read.");
        read += bytes.size();
        hash.addData(bytes);
    }
    require(read == initial && file.size() == initial, "Input changed during hashing.");
    cancelled(cancel);
    return QString::fromLatin1(hash.result().toHex());
}
void rejectLinks(const QString &root, const QString &target) {
    const auto base = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const auto path = QDir::cleanPath(QFileInfo(target).absoluteFilePath());
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    require(path.compare(base, sensitivity) == 0 ||
                path.startsWith(base.endsWith('/') ? base : base + "/", sensitivity),
            "Managed output escapes its preparation directory.");
    auto check = [](const QString &at) {
        bool link = QFileInfo(at).isSymLink();
#ifdef Q_OS_WIN
        const auto native = QDir::toNativeSeparators(at);
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
        link = link || (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
#endif
        require(!link, "Symlink/reparse point in managed output; operation refused.");
    };
    check(base);
    auto current = base;
    if (path.compare(base, sensitivity) == 0)
        return;
    for (const auto &piece : QDir(base).relativeFilePath(path).split('/')) {
        current = QDir(current).filePath(piece);
        check(current);
    }
}
QString archivePath(const QString &name) {
    require(!name.startsWith('/') && !name.contains('\\'), "Unsafe archive path.");
    auto raw = name;
    while (raw.endsWith('/'))
        raw.chop(1);
    auto parts = raw.split('/');
    if (!parts.isEmpty() && parts.first() == ".")
        parts.removeFirst();
    require(!parts.isEmpty() && parts.size() <= 64, "Invalid archive path depth.");
    const QSet<QString> reserved{"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                 "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                 "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const auto &piece : parts) {
        require(!piece.isEmpty() && piece != "." && piece != ".." && !piece.endsWith('.') &&
                    !piece.endsWith(' '),
                "Unsafe archive filename.");
        for (QChar ch : piece)
            require(ch.unicode() >= 32 && !QString("<>:\"/\\|?*").contains(ch), "Unsafe archive filename.");
        require(!reserved.contains(piece.section('.', 0, 0).toUpper()),
                "Reserved Windows filename in archive.");
    }
    return parts.join('/');
}
void atomicWrite(const QString &root, const QString &path, const QByteArray &bytes, const Cancel &cancel) {
    cancelled(cancel);
    rejectLinks(root, path);
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create output directory.");
    rejectLinks(root, path);
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot create output file.");
    for (qsizetype pos = 0; pos < bytes.size(); pos += 1024 * 1024) {
        cancelled(cancel);
        const auto length = std::min<qsizetype>(1024 * 1024, bytes.size() - pos);
        require(file.write(bytes.constData() + pos, length) == length, "Cannot write output file.");
    }
    cancelled(cancel);
    rejectLinks(root, path);
    require(file.commit(), "Cannot publish output file.");
}
QString titleId(const QByteArray &sfo) {
    auto u16 = [&](quint64 at) {
        range(sfo, at, 2);
        return qFromLittleEndian<quint16>(sfo.constData() + at);
    };
    auto u32 = [&](quint64 at) {
        range(sfo, at, 4);
        return qFromLittleEndian<quint32>(sfo.constData() + at);
    };
    require(sfo.size() >= 20 && sfo.left(4) == QByteArray("\0PSF", 4), "Invalid PARAM.SFO.");
    const quint64 keys = u32(8), values = u32(12), count = u32(16);
    require(count <= 256 && 20 + count * 16 <= quint64(sfo.size()) && keys < values &&
                values < quint64(sfo.size()),
            "Corrupt PARAM.SFO table.");
    for (quint64 n = 0; n < count; ++n) {
        const quint64 at = 20 + n * 16, key = keys + u16(at), length = u32(at + 4),
                      offset = values + u32(at + 12);
        require(key < values, "Invalid PARAM.SFO key.");
        const auto end = sfo.indexOf('\0', qsizetype(key));
        require(end >= 0 && quint64(end) < values && length <= u32(at + 8), "Invalid PARAM.SFO field.");
        range(sfo, offset, length);
        if (sfo.mid(qsizetype(key), end - qsizetype(key)) == "TITLE_ID") {
            require(u16(at + 2) == 0x204 && length <= 32, "Invalid TITLE_ID.");
            auto result = part(sfo, offset, length);
            while (result.endsWith('\0'))
                result.chop(1);
            return QString::fromLatin1(result);
        }
    }
    fail("PARAM.SFO has no TITLE_ID.");
}
QString identify(const QString &title, const QString &digest) {
    if (QSet<QString>{"BCES00647", "BCES00644", "BCES00645"}.contains(title) &&
        digest == "6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8")
        return "quizworld-eu100";
    if (title == "BCUS98209" && digest == "fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d")
        return "quizworld-us100";
    if (title == "BCES00647" && digest == "9303ccce9bc223ca6da2eb837256ef08be6f1f460dcc6d5191889de7a4eee241")
        return "quizworld-102-verified";
    fail("Unsupported title/executable SHA-256. Hash guards cannot be bypassed.");
}
QByteArray elfText(const QByteArray &elf) {
    require(elf.left(7) == QByteArray("\x7f"
                                      "ELF\x01\x02\x01",
                                      7),
            "Expected a big-endian ELF32 audio module.");
    const quint64 at = be32(elf, 0x20), size = be16(elf, 0x2e), count = be16(elf, 0x30),
                  names = be16(elf, 0x32);
    require(size == 40 && count > 0 && count <= 4096 && names < count, "Invalid audio module section table.");
    range(elf, at, size * count);
    const auto strings = part(elf, be32(elf, at + names * size + 16), be32(elf, at + names * size + 20));
    for (quint64 i = 0; i < count; ++i) {
        const auto off = be32(elf, at + i * size);
        require(off < quint64(strings.size()), "Invalid ELF section name.");
        const auto end = strings.indexOf('\0', off);
        require(end >= 0, "Truncated ELF section name.");
        if (strings.mid(off, end - off) == ".text")
            return part(elf, be32(elf, at + i * size + 16), be32(elf, at + i * size + 20));
    }
    fail("Audio module has no .text section.");
}
void validateFirmware(const QString &root) {
    const QDir fonts(QDir(root).filePath("data/font"));
    require(fonts.exists() && !fonts.entryList(QDir::Files).isEmpty(),
            "Firmware has no readable font files.");
    const auto audio = readFile(QDir(root).filePath("sys/external/flashATRAC.pic"));
    require(QCryptographicHash::hash(elfText(audio), QCryptographicHash::Sha256).toHex() ==
                "c543ecc87e82beec3159d88fbb73dc4229424d357c0a48be3c4cb55d3fdefadc",
            "Unsupported firmware ATRAC module.");
}
} // namespace prepare
