#include "import.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageAuthenticationCode>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QStringDecoder>
#include <QTemporaryDir>

namespace prepare {
namespace {
QByteArray fixedString(const QByteArray &field) {
    const auto end = field.indexOf('\0');
    return end < 0 ? field : field.left(end);
}
QString utf8(const QByteArray &bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    QString value = decoder.decode(bytes);
    require(!decoder.hasError(), "Invalid archive UTF-8 filename.");
    return value;
}
quint64 octal(const QByteArray &field) {
    const auto text = fixedString(field).trimmed();
    if (text.isEmpty())
        return 0;
    for (char ch : text)
        require(ch >= '0' && ch <= '7', "Invalid TAR numeric field.");
    bool ok = false;
    const auto value = text.toULongLong(&ok, 8);
    require(ok, "Invalid TAR numeric field.");
    return value;
}
void freeSpace(const QString &directory, quint64 size) {
    auto at = QFileInfo(directory).absoluteFilePath();
    while (!QFileInfo::exists(at)) {
        auto parent = QFileInfo(at).absolutePath();
        require(parent != at, "Invalid extraction directory.");
        at = parent;
    }
    const QStorageInfo storage(at);
    require(!storage.isValid() || storage.bytesAvailable() < 0 ||
                quint64(storage.bytesAvailable()) >= size + 1024 * 1024,
            "Not enough disk space for extraction.");
}
QByteArray readAt(QFile &file, quint64 offset, quint64 size) {
    require(offset <= quint64(file.size()) && size <= quint64(file.size()) - offset && size <= MaxBinary,
            "Invalid archive data range.");
    require(file.seek(qint64(offset)), "Cannot seek input archive.");
    const auto bytes = file.read(qint64(size));
    require(quint64(bytes.size()) == size, "Truncated input archive.");
    return bytes;
}
} // namespace
QVector<TarEntry> readTar(const QByteArray &bytes, const QString &prefix, const Cancel &cancel) {
    QVector<TarEntry> result;
    QSet<QString> paths;
    quint64 total = 0, at = 0;
    int count = 0;
    while (at + 512 <= quint64(bytes.size())) {
        cancelled(cancel);
        const auto header = part(bytes, at, 512);
        at += 512;
        if (header == QByteArray(512, '\0')) {
            // Require the two end-of-archive blocks; permit only zero padding afterward.
            require(at + 512 <= quint64(bytes.size()) && part(bytes, at, 512) == QByteArray(512, '\0'),
                    "Truncated TAR end marker.");
            for (quint64 i = at + 512; i < quint64(bytes.size()); ++i)
                require(bytes[qsizetype(i)] == 0, "Trailing data in TAR archive.");
            return result;
        }
        require(++count <= MaxFiles, "TAR archive exceeds its file-count limit.");
        quint64 checksum = 0;
        for (int i = 0; i < 512; ++i)
            checksum += (i >= 148 && i < 156) ? quint8(' ') : quint8(header[i]);
        require(checksum == octal(header.mid(148, 8)), "TAR header checksum mismatch.");
        const auto type = header[156];
        require(type == 0 || type == '0' || type == '5',
                "Link, extended or special TAR entry is unsupported.");
        const quint64 size = octal(header.mid(124, 12));
        require(size <= MaxBinary, "TAR member exceeds its size limit.");
        const auto tag = header.mid(257, 6);
        const auto prefixBytes = (tag == QByteArray("ustar\0", 6) || tag == "ustar ")
                                     ? fixedString(header.mid(345, 155))
                                     : QByteArray();
        const auto raw = (prefixBytes.isEmpty() ? QString() : utf8(prefixBytes) + "/") +
                         utf8(fixedString(header.left(100)));
        if ((raw == "." || raw == "./") && type == '5') {
            require(size == 0, "TAR directory has data.");
            continue;
        }
        const auto path = archivePath(raw), key = path.toCaseFolded();
        require(!paths.contains(key), "Duplicate path in TAR archive.");
        paths.insert(key);
        require(prefix.isEmpty() || path.section('/', 0, 0) == prefix,
                "Firmware package writes outside dev_flash.");
        total += size;
        require(total <= MaxFirmware, "TAR archive exceeds its expanded size limit.");
        require(type != '5' || size == 0, "TAR directory has data.");
        auto data = part(bytes, at, size);
        const quint64 padded = (size + 511) / 512 * 512;
        range(bytes, at, padded);
        at += padded;
        result.append({path, data, type == '5'});
    }
    fail("Truncated TAR archive.");
}
QByteArray firmwareArchive(const QString &path, const KeyStore &keys, const Cancel &cancel) {
    require(QFileInfo(path).isFile(), "Expected a regular firmware PUP file.");
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot open firmware PUP.");
    const auto size = quint64(file.size());
    require(size >= 0x50 && size <= MaxFirmware, "Firmware PUP is truncated or oversized.");
    auto header = readAt(file, 0, 48);
    const auto count = be64(header, 24), headerSize = be64(header, 32), dataSize = be64(header, 40);
    require(header.left(8) == QByteArray("SCEUF\0\0\0", 8) && be64(header, 8) == 1 && count > 0 &&
                count <= 1024,
            "Unsupported firmware PUP header.");
    require(headerSize >= 0x50 + count * 64 && headerSize <= std::min<quint64>(size, 1024 * 1024) &&
                dataSize <= size - headerSize,
            "Invalid firmware PUP header/data sizes.");
    header = readAt(file, 0, headerSize);
    const auto key = keys.namedKey("pup_hmac", 64);
    const auto digestAt = 48 + count * 64;
    require(equalDigest(hmac(key, part(header, 0, digestAt), QCryptographicHash::Sha1),
                        part(header, digestAt, 20)),
            "Firmware PUP header digest mismatch.");
    QMap<quint64, QByteArray> hashes;
    for (quint64 i = 0; i < count; ++i) {
        const auto at = 48 + count * 32 + i * 32, index = be64(header, at);
        require(index < count && !hashes.contains(index), "Invalid firmware PUP digest table.");
        hashes[index] = part(header, at + 8, 20);
    }
    QSet<quint64> identities;
    QVector<QPair<quint64, quint64>> regions;
    quint64 selectedOffset = 0, selectedLength = 0, selectedIndex = count;
    for (quint64 i = 0; i < count; ++i) {
        const auto at = 48 + i * 32, entry = be64(header, at), offset = be64(header, at + 8),
                   length = be64(header, at + 16);
        require(!identities.contains(entry) && offset >= headerSize && offset <= headerSize + dataSize &&
                    length <= headerSize + dataSize - offset && be32(header, at + 24) == 0,
                "Invalid firmware PUP segment.");
        identities.insert(entry);
        regions << qMakePair(offset, offset + length);
        if (entry == 0x300) {
            selectedOffset = offset;
            selectedLength = length;
            selectedIndex = i;
        }
    }
    std::sort(regions.begin(), regions.end());
    for (int i = 1; i < regions.size(); ++i)
        require(regions[i - 1].second <= regions[i].first, "Overlapping firmware PUP segments.");
    require(selectedIndex < count && selectedLength <= MaxBinary,
            "Firmware has no supported update_files.tar segment.");
    QByteArray bytes;
    QMessageAuthenticationCode digest(QCryptographicHash::Sha1, key);
    while (quint64(bytes.size()) < selectedLength) {
        cancelled(cancel);
        const auto chunk = readAt(file, selectedOffset + bytes.size(),
                                  std::min<quint64>(1024 * 1024, selectedLength - bytes.size()));
        bytes += chunk;
        digest.addData(chunk);
    }
    require(equalDigest(digest.result(), hashes.value(selectedIndex)),
            "Firmware PUP update archive digest mismatch.");
    return bytes;
}
void extractFirmware(const QString &source, const QString &home, const KeyStore &keys, const Log &log,
                     const Cancel &cancel, const std::function<void(const QString &)> &validator) {
    rejectLinks(home, QDir(home).filePath("dev_flash"));
    const auto outer = readTar(firmwareArchive(source, keys, cancel), {}, cancel);
    QVector<TarEntry> planned;
    for (const auto &entry : outer)
        if (!entry.directory && !entry.path.contains('/') && entry.path.startsWith("dev_flash_"))
            planned << entry;
    require(!planned.isEmpty(), "Firmware PUP contains no dev_flash packages.");
    require(QDir().mkpath(home), "Cannot create firmware import directory.");
    QTemporaryDir staging(QDir(home).filePath("firmware-import-XXXXXX"));
    require(staging.isValid(), "Cannot create firmware staging directory.");
    quint64 total = 0;
    QSet<QString> seen;
    for (int i = 0; i < planned.size(); ++i) {
        cancelled(cancel);
        if (log)
            log(QString("Firmware: %1/%2 packages").arg(i + 1).arg(planned.size()));
        const CertifiedFile package(planned[i].data, keys, cancel);
        require(package.category() == 3 && package.sections().size() == 3,
                "Unsupported firmware package layout.");
        const auto inner = readTar(package.sectionData(package.sections()[2]), "dev_flash", cancel);
        for (const auto &entry : inner) {
            if (entry.directory)
                continue;
            const auto key = entry.path.toCaseFolded();
            require(!seen.contains(key) && seen.size() < MaxFiles,
                    "Colliding paths or too many firmware files.");
            seen.insert(key);
            total += entry.data.size();
            require(total <= MaxFirmware, "Firmware exceeds its expanded size limit.");
            freeSpace(home, entry.data.size());
            atomicWrite(staging.path(), QDir(staging.path()).filePath(entry.path), entry.data, cancel);
        }
    }
    const auto fresh = QDir(staging.path()).filePath("dev_flash"),
               destination = QDir(home).filePath("dev_flash");
    validator(fresh);
    cancelled(cancel);
    rejectLinks(home, destination);
    if (QFileInfo::exists(destination)) {
        QSet<QString> existing;
        QDirIterator it(destination, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const auto path = it.next();
            cancelled(cancel);
            rejectLinks(home, path);
            if (it.fileInfo().isFile()) {
                const auto relative = QDir(home).relativeFilePath(path);
                existing.insert(relative.toCaseFolded());
                const auto candidate = QDir(staging.path()).filePath(relative);
                require(QFileInfo(candidate).isFile() &&
                            fileHash(path, cancel) == fileHash(candidate, cancel),
                        "Prepared firmware changed. Start a new session.");
            }
        }
        require(existing == seen, "Prepared firmware is incomplete. Start a new session.");
        if (log)
            log("Verified previously published firmware against a fresh extraction.");
        return;
    }
    require(QDir().rename(fresh, destination), "Cannot publish prepared firmware.");
    if (log)
        log("Firmware extracted; required audio module and fonts verified.");
}
void extractUpdate(const QString &source, const QString &destination, const QString &title, const Log &log,
                   const Cancel &cancel) {
    rejectLinks(destination, destination);
    require(QFileInfo(source).isFile(), "Expected a regular update PKG file.");
    QFile file(source);
    require(file.open(QIODevice::ReadOnly), "Cannot open update PKG.");
    const auto header = readAt(file, 0, 0xc0);
    require(header.left(4) == QByteArray("\x7f"
                                         "PKG",
                                         4),
            "Invalid PS3 update PKG.");
    const auto revision = be16(header, 4);
    const quint64 count = be32(header, 0x14), offset = be64(header, 0x20), length = be64(header, 0x28);
    require((revision == 0 || revision == 0x8000) && be16(header, 6) == 1,
            "Only PS3 retail/debug update packages are supported.");
    require(be64(header, 0x18) == quint64(file.size()) && count > 0 && count <= MaxFiles && offset >= 0xc0 &&
                offset <= quint64(file.size()) && length <= quint64(file.size()) - offset &&
                count * 32 <= length,
            "Invalid update PKG sizes/table.");
    auto read = [&](quint64 at, quint64 amount) {
        cancelled(cancel);
        require(at <= length && amount <= length - at && amount <= MaxBinary,
                "Invalid update PKG data range.");
        const quint64 block = at / 16, skip = at % 16, blocks = (skip + amount + 15) / 16;
        const auto raw =
            readAt(file, offset + block * 16, std::min<quint64>(blocks * 16, length - block * 16));
        QByteArray plain;
        if (revision == 0x8000) {
            // Existing SDK's documented PS3 retail-package key, not a user's SELF/firmware key.
            plain = aesCtr(raw, QByteArray::fromHex("2e7b71d7c9c9a14ea3221f188828b8f8"), header.mid(0x70, 16),
                           block, cancel);
        } else {
            const auto qa = header.mid(0x60, 16);
            QByteArray stream;
            for (quint64 i = 0; i < blocks; ++i) {
                if (i % 65536 == 0)
                    cancelled(cancel);
                auto input = qa.left(8).repeated(2) + qa.mid(8).repeated(2) + QByteArray(32, '\0');
                qToBigEndian<quint64>(block + i, input.data() + 0x38);
                stream += QCryptographicHash::hash(input, QCryptographicHash::Sha1).left(16);
            }
            plain.resize(raw.size());
            for (qsizetype i = 0; i < raw.size(); ++i)
                plain[i] = char(quint8(raw[i]) ^ quint8(stream[i]));
        }
        return part(plain, skip, amount);
    };
    const auto table = read(0, count * 32);
    QSet<QString> seen;
    quint64 total = 0;
    QByteArray sfo;
    struct File {
        QString path;
        quint64 offset, size;
    };
    QVector<File> plan;
    for (quint64 i = 0; i < count; ++i) {
        const auto at = i * 32;
        const quint64 nameAt = be32(table, at), nameSize = be32(table, at + 4), dataAt = be64(table, at + 8),
                      dataSize = be64(table, at + 16);
        const auto flags = be32(table, at + 24);
        require(nameSize > 0 && nameSize <= 4096 && nameAt >= quint64(table.size()) && dataAt <= length &&
                    dataSize <= length - dataAt,
                "Invalid update PKG entry range.");
        require(!(flags & 0x10000000) && QSet<quint32>{1, 2, 3, 4, 9}.contains(flags & 255),
                "Unsupported update PKG entry type.");
        const auto path = archivePath(utf8(read(nameAt, nameSize))), key = path.toCaseFolded();
        require(!seen.contains(key), "Colliding paths in update PKG.");
        seen.insert(key);
        if ((flags & 255) == 4) {
            require(dataSize == 0, "Update PKG directory has data.");
            continue;
        }
        total += dataSize;
        require(total <= MaxFirmware, "Update PKG exceeds its extraction limit.");
        plan << File{path, dataAt, dataSize};
        if (path == "PARAM.SFO") {
            require(dataSize <= 1024 * 1024, "Oversized update PARAM.SFO.");
            sfo = read(dataAt, dataSize);
        }
    }
    bool executable = false;
    for (const auto &entry : plan)
        if (entry.path == "USRDIR/EBOOT.BIN")
            executable = true;
    require(!sfo.isEmpty() && titleId(sfo) == title && executable,
            "Update PKG title does not match the selected disc, or has no EBOOT.BIN.");
    freeSpace(destination, total);
    require(QDir().mkpath(destination), "Cannot create update destination.");
    for (int i = 0; i < plan.size(); ++i) {
        const auto &entry = plan[i];
        cancelled(cancel);
        const auto target = QDir(destination).filePath(entry.path);
        rejectLinks(destination, target);
        require(QDir().mkpath(QFileInfo(target).absolutePath()), "Cannot create update directory.");
        rejectLinks(destination, target);
        QSaveFile output(target);
        require(output.open(QIODevice::WriteOnly), "Cannot create update output.");
        for (quint64 pos = 0; pos < entry.size; pos += 1024 * 1024) {
            const auto bytes = read(entry.offset + pos, std::min<quint64>(1024 * 1024, entry.size - pos));
            require(output.write(bytes) == bytes.size(), "Cannot write update file.");
        }
        cancelled(cancel);
        rejectLinks(destination, target);
        require(output.commit(), "Cannot publish update file.");
        if (log)
            log(QString("Update: %1/%2 files").arg(i + 1).arg(plan.size()));
    }
}
} // namespace prepare
