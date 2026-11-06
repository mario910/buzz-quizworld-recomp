#include "spu_images.h"
#include <QDir>

namespace prepare {
SpuImage validateSpuImage(const QByteArray &bytes, quint64 offset) {
    constexpr quint64 ls = 256 * 1024;
    range(bytes, offset, 52);
    auto u16 = [&](quint64 at) { return be16(bytes, offset + at); };
    auto u32 = [&](quint64 at) { return be32(bytes, offset + at); };
    auto extent = [&](quint64 at, quint64 length) {
        range(bytes, offset + at, length);
        return at + length;
    };
    require(part(bytes, offset, 7) == QByteArray("\x7f"
                                                 "ELF\x01\x02\x01",
                                                 7) &&
                u16(18) == 23,
            "Expected ELF32 big-endian EM_SPU version 1.");
    require((u16(16) == 2 || u16(16) == 3) && u32(20) == 1 && u16(40) == 52, "Invalid SPU ELF header.");
    const quint32 entry = u32(24);
    const quint64 phoff = u32(28), shoff = u32(32), phsize = u16(42), phcount = u16(44), shsize = u16(46),
                  shcount = u16(48), shstr = u16(50);
    require(phcount > 0 && phcount < 0xffff && phsize == 32 && phoff >= 52,
            "Invalid SPU program header table.");
    const auto phend = extent(phoff, phsize * phcount);
    require(shcount < 0xffff && shstr < 0xffff, "Extended SPU ELF numbering is unsupported.");
    if (shcount)
        require(shsize == 40 && shoff >= 52 && (shstr == 0 || shstr < shcount),
                "Invalid SPU section header table.");
    else
        require(!shoff && !shstr, "SPU section table offset/index without sections.");
    quint64 end = shcount ? extent(shoff, shsize * shcount) : 0;
    QVector<Segment> loads, executable;
    auto aligned = [](quint32 alignment) { return alignment <= 1 || !(alignment & (alignment - 1)); };
    for (quint64 i = 0; i < phcount; ++i) {
        const auto at = phoff + i * phsize;
        Segment s{u32(at), u32(at + 24), u32(at + 4), u32(at + 8), u32(at + 16), u32(at + 20)};
        end = std::max(end, extent(s.offset, s.fileSize));
        if (s.type == 1) {
            require(s.fileSize <= s.memorySize && s.address <= ls && s.memorySize <= ls - s.address,
                    "SPU PT_LOAD exceeds local store or has filesz > memsz.");
            require(aligned(u32(at + 28)), "Invalid SPU segment alignment.");
            loads << s;
            if ((s.flags & 1) && s.fileSize) {
                require(s.address % 4 == 0 && s.fileSize % 4 == 0, "Unaligned executable SPU segment.");
                executable << s;
            }
        }
    }
    require(!loads.isEmpty() && entry < ls && entry % 4 == 0, "Invalid SPU entry or missing PT_LOAD.");
    for (quint64 i = 0; i < shcount; ++i) {
        const quint64 at = shoff + i * shsize, type = u32(at + 4), flags = u32(at + 8),
                      address = u32(at + 12), file = u32(at + 16), size = u32(at + 20);
        if (type != 8)
            end = std::max(end, extent(file, size));
        require(aligned(u32(at + 32)), "Invalid SPU section alignment.");
        if (type == 2)
            require(u32(at + 36) == 16 && size % 16 == 0 && u32(at + 24) < shcount,
                    "Invalid SPU symbol table layout.");
        if ((flags & 4) && size) {
            require(type != 8 && address % 4 == 0 && size % 4 == 0, "Invalid executable SPU section.");
            bool mapped = false;
            for (const auto &s : executable)
                if (address >= s.address && quint64(address) + size <= s.address + s.fileSize &&
                    file >= s.offset && file - s.offset == address - s.address)
                    mapped = true;
            require(mapped, "Executable SPU section is not mapped by executable PT_LOAD.");
        }
    }
    require(end >= phend && end >= 52, "SPU runtime fingerprint extent omits ELF/program headers.");
    return {offset, end, entry, loads};
}
QVector<SpuImage> findSpuImages(const QByteArray &bytes, const Cancel &cancel) {
    QVector<SpuImage> result;
    const QByteArray magic("\x7f"
                           "ELF",
                           4);
    for (qsizetype at = bytes.indexOf(magic); at >= 0; at = bytes.indexOf(magic, at + 4)) {
        cancelled(cancel);
        if (at + 20 > bytes.size() || bytes[at + 18] != 0 || bytes[at + 19] != 23)
            continue;
        try {
            result << validateSpuImage(bytes, at);
        } catch (const std::runtime_error &) { /* Non-image magic occurrence. */
        }
        require(result.size() <= 4096, "Too many embedded SPU images.");
    }
    cancelled(cancel);
    return result;
}
quint64 spuFingerprint(const QByteArray &bytes) {
    // Match the runtime's historical basis (not the standard FNV-1a basis).
    quint64 hash = 1469598103934665603ull;
    for (char byte : bytes)
        hash = (hash ^ quint8(byte)) * 1099511628211ull;
    return hash;
}
QJsonArray extractSpuImages(const QByteArray &bytes, const QString &directory, const Cancel &cancel) {
    const auto images = findSpuImages(bytes, cancel);
    require(!images.isEmpty(), "No valid embedded SPU images.");
    QJsonArray result;
    for (int i = 0; i < images.size(); ++i) {
        const auto &image = images[i];
        const auto name = QString("spu_%1_at_%2.elf")
                              .arg(i, 4, 10, QChar('0'))
                              .arg(QString::number(image.offset, 16).toUpper().rightJustified(8, '0'));
        const auto data = part(bytes, image.offset, image.size);
        atomicWrite(directory, QDir(directory).filePath(name), data, cancel);
        result.append(QJsonObject{
            {"name", name},
            {"offset", qint64(image.offset)},
            {"size", qint64(image.size)},
            {"entry", qint64(image.entry)},
            {"fnv", "0x" + QString::number(spuFingerprint(data), 16)},
            {"sha256",
             QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())}});
    }
    return result;
}
} // namespace prepare
