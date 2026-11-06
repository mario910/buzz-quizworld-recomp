#include "disc.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QStringDecoder>
#include <QTemporaryDir>

namespace prepare {
namespace {
constexpr quint64 Block = 2048, MaxGame = 64ull * 1024 * 1024 * 1024;
constexpr int MaxDiscFiles = 200000;
quint32 dual32(const QByteArray &bytes, quint64 at) {
    range(bytes, at, 8);
    const auto little = qFromLittleEndian<quint32>(bytes.constData() + at);
    require(little == be32(bytes, at + 4), "Inconsistent ISO byte orders.");
    return little;
}
quint16 dual16(const QByteArray &bytes, quint64 at) {
    range(bytes, at, 4);
    const auto little = qFromLittleEndian<quint16>(bytes.constData() + at);
    require(little == be16(bytes, at + 2), "Inconsistent ISO byte orders.");
    return little;
}
QString discName(const QByteArray &bytes, bool joliet) {
    QString result;
    if (joliet) {
        require(bytes.size() % 2 == 0, "Invalid Joliet filename length.");
        QStringDecoder decoder(QStringDecoder::Utf16BE, QStringConverter::Flag::Stateless);
        result = decoder.decode(bytes);
        require(!decoder.hasError(), "Invalid Joliet filename encoding.");
    } else {
        for (auto ch : bytes)
            require(quint8(ch) >= 32 && quint8(ch) < 128, "Invalid ISO9660 filename encoding.");
        result = QString::fromLatin1(bytes);
    }
    result.remove(QRegularExpression(";[0-9]+$"));
    require(!result.contains('/'), "Invalid ISO filename component.");
    return archivePath(result);
}
} // namespace
DiscImage::DiscImage(const QString &path) : file_(path) {
    require(QFileInfo(path).isFile(), "Expected a regular game ISO file.");
    require(file_.open(QIODevice::ReadOnly), "Cannot open game ISO.");
    originalSize_ = file_.size();
    require(originalSize_ >= 18 * qint64(Block) && quint64(originalSize_) <= MaxGame,
            "Game ISO is truncated or exceeds 64 GiB.");
}
QByteArray DiscImage::at(quint64 offset, quint64 size) {
    require(offset <= quint64(originalSize_) && size <= quint64(originalSize_) - offset && size <= MaxBinary,
            "Invalid ISO extent.");
    require(file_.seek(qint64(offset)), "Cannot seek game ISO.");
    const auto bytes = file_.read(qint64(size));
    require(quint64(bytes.size()) == size, "Truncated game ISO.");
    return bytes;
}
void DiscImage::verifyUnchanged() const {
    require(file_.size() == originalSize_, "Game ISO changed during preparation.");
}
void DiscImage::verifyExtraction(const DiscPlan &plan, const QString &directory, const Cancel &cancel) {
    QSet<QString> expected, actual;
    for (const auto &entry : plan.files) {
        cancelled(cancel);
        expected.insert(entry.path);
        const auto path = QDir(directory).filePath(entry.path);
        rejectLinks(directory, path);
        require(QFileInfo(path).isFile() && quint64(QFileInfo(path).size()) == entry.size,
                "Incomplete extracted game; use a new session.");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for (const auto &span : entry.extents)
            for (quint64 pos = 0; pos < span.size; pos += 1024 * 1024) {
                cancelled(cancel);
                hash.addData(at(span.offset + pos, std::min<quint64>(1024 * 1024, span.size - pos)));
            }
        require(fileHash(path, cancel) == QString::fromLatin1(hash.result().toHex()),
                "Extracted game changed; use a new session.");
    }
    QDirIterator iterator(directory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        cancelled(cancel);
        const auto path = iterator.next();
        rejectLinks(directory, path);
        if (iterator.fileInfo().isFile())
            actual.insert(QDir(directory).relativeFilePath(path));
    }
    require(actual == expected, "Extracted game has unexpected files; use a new session.");
    verifyUnchanged();
}
QByteArray DiscImage::read(const DiscFile &entry, quint64 limit, const Cancel &cancel) {
    require(entry.size <= limit, "ISO file exceeds its read limit.");
    QByteArray bytes;
    for (const auto &extent : entry.extents)
        for (quint64 pos = 0; pos < extent.size; pos += 1024 * 1024) {
            cancelled(cancel);
            bytes += at(extent.offset + pos, std::min<quint64>(1024 * 1024, extent.size - pos));
        }
    require(quint64(bytes.size()) == entry.size, "ISO file size differs from its extents.");
    verifyUnchanged();
    return bytes;
}
DiscPlan DiscImage::inspect(const Cancel &cancel) {
    QByteArray primary, joliet;
    bool ended = false;
    for (quint64 sector = 16; sector < 272 && (sector + 1) * Block <= quint64(originalSize_); ++sector) {
        cancelled(cancel);
        const auto descriptor = at(sector * Block, Block);
        require(descriptor.mid(1, 5) == "CD001" && quint8(descriptor[6]) == 1,
                "Unsupported ISO descriptor; expected an ISO9660/Joliet bridge.");
        const auto type = quint8(descriptor[0]);
        if (type == 255) {
            ended = true;
            break;
        }
        if (type == 1) {
            require(primary.isEmpty(), "Duplicate ISO primary volume.");
            primary = descriptor;
        }
        if (type == 2 && (descriptor.mid(88, 3) == "%/@" || descriptor.mid(88, 3) == "%/C" ||
                          descriptor.mid(88, 3) == "%/E"))
            joliet = descriptor;
    }
    require(ended && !primary.isEmpty(),
            "No complete ISO9660/Joliet volume. Native UDF-only support is not implemented yet.");
    const bool unicode = !joliet.isEmpty();
    const auto descriptor = unicode ? joliet : primary;
    require(dual16(descriptor, 128) == Block && dual16(descriptor, 120) == 1 && dual16(descriptor, 124) == 1,
            "Unsupported ISO block size or multi-volume disc.");
    const quint64 volume = quint64(dual32(descriptor, 80)) * Block;
    require(volume <= quint64(originalSize_) && volume >= 18 * Block, "Invalid ISO volume size.");
    const auto rootSize = quint8(descriptor[156]);
    require(rootSize >= 34, "Invalid ISO root directory.");
    const auto root = part(descriptor, 156, rootSize);
    struct Directory {
        QString path;
        quint64 offset, size;
    };
    auto extent = [&](const QByteArray &record) {
        require(record.size() >= 34 && quint8(record[0]) == record.size(), "Invalid ISO directory record.");
        const quint64 offset = (quint64(dual32(record, 2)) + quint8(record[1])) * Block,
                      length = dual32(record, 10);
        require(dual16(record, 28) == 1 && record[26] == 0 && record[27] == 0,
                "Unsupported ISO volume/interleaving.");
        require(offset <= volume && length <= volume - offset, "ISO file extent exceeds the volume.");
        return DiscExtent{offset, length};
    };
    const auto rootExtent = extent(root);
    require(quint8(root[25]) & 2, "ISO root is not a directory.");
    QVector<Directory> queue{{{}, rootExtent.offset, rootExtent.size}};
    QSet<quint64> directories;
    QSet<QString> paths;
    DiscPlan plan;
    plan.format = unicode ? "Joliet" : "ISO9660";
    for (qsizetype index = 0; index < queue.size(); ++index) {
        cancelled(cancel);
        const auto directory = queue[index];
        require(!directories.contains(directory.offset) && directories.size() < MaxDiscFiles,
                "Cyclic or oversized ISO directory tree.");
        directories.insert(directory.offset);
        require(directory.size <= MaxBinary, "ISO directory exceeds its size limit.");
        const auto data = at(directory.offset, directory.size);
        QString pending;
        DiscFile multiple;
        for (quint64 pos = 0; pos < quint64(data.size());) {
            cancelled(cancel);
            const auto size = quint8(data[qsizetype(pos)]);
            if (!size) {
                pos = (pos / Block + 1) * Block;
                continue;
            }
            require(size >= 34 && pos % Block + size <= Block, "Invalid ISO directory record length.");
            const auto record = part(data, pos, size);
            pos += size;
            const auto info = extent(record);
            const auto flags = quint8(record[25]), nameSize = quint8(record[32]);
            require(nameSize > 0, "Empty ISO filename.");
            const auto raw = part(record, 33, nameSize);
            if (raw == QByteArray(1, '\0') || raw == QByteArray(1, '\1'))
                continue;
            require(!(flags & 4), "Associated ISO files are unsupported.");
            const auto name = discName(raw, unicode),
                       path = archivePath(directory.path.isEmpty() ? name : directory.path + "/" + name);
            require(path.count('/') < 64, "ISO directory depth exceeds its safety limit.");
            const auto key = path.toCaseFolded();
            if (flags & 2) {
                require(pending.isEmpty() && !(flags & 128) && !paths.contains(key),
                        "Invalid or duplicate ISO directory.");
                paths.insert(key);
                require(queue.size() < MaxDiscFiles, "Too many ISO directories.");
                queue << Directory{path, info.offset, info.size};
                continue;
            }
            if (!pending.isEmpty()) {
                require(pending == path, "Broken ISO multi-extent chain.");
                multiple.extents << info;
                multiple.size += info.size;
            } else {
                require(!paths.contains(key), "Colliding ISO paths on Windows.");
                paths.insert(key);
                multiple = {path, {info}, info.size};
            }
            require(multiple.extents.size() <= MaxDiscFiles && multiple.size <= MaxGame,
                    "Oversized ISO multi-extent chain.");
            if (flags & 128) {
                pending = path;
                continue;
            }
            pending.clear();
            if (path.startsWith("PS3_GAME/")) {
                plan.bytes += multiple.size;
                require(plan.bytes <= MaxGame && plan.files.size() < MaxDiscFiles,
                        "ISO exceeds import safety limits.");
                plan.files << multiple;
            }
        }
        require(pending.isEmpty(), "Incomplete ISO multi-extent chain.");
    }
    bool found = false;
    for (const auto &entry : plan.files)
        if (entry.path == "PS3_GAME/PARAM.SFO") {
            require(entry.size <= 1024 * 1024, "Oversized game PARAM.SFO.");
            plan.title = titleId(read(entry, 1024 * 1024, cancel));
            found = true;
        }
    require(found, "No readable PS3_GAME/PARAM.SFO. The ISO may be encrypted or unsupported.");
    require(QSet<QString>{"BCES00647", "BCES00644", "BCES00645", "BCUS98209"}.contains(plan.title),
            "Unsupported ISO title ID.");
    verifyUnchanged();
    return plan;
}
void DiscImage::extract(const DiscPlan &plan, const QString &destination, const Log &log,
                        const Cancel &cancel) {
    cancelled(cancel);
    const auto parent = QFileInfo(destination).absolutePath();
    rejectLinks(parent, destination);
    require(!QFileInfo::exists(destination),
            "ISO destination already exists; use a new preparation directory.");
    const QStorageInfo storage(parent);
    require(!storage.isValid() || storage.bytesAvailable() < 0 ||
                quint64(storage.bytesAvailable()) >= plan.bytes + 2ull * 1024 * 1024 * 1024,
            "Not enough disk space for ISO extraction and build headroom.");
    require(QDir().mkpath(parent), "Cannot create ISO destination parent.");
    QTemporaryDir staging(QDir(parent).filePath("iso-import-XXXXXX"));
    require(staging.isValid(), "Cannot create ISO staging directory.");
    for (int i = 0; i < plan.files.size(); ++i) {
        cancelled(cancel);
        const auto &entry = plan.files[i];
        const auto target = QDir(staging.path()).filePath(archivePath(entry.path));
        rejectLinks(staging.path(), target);
        require(QDir().mkpath(QFileInfo(target).absolutePath()), "Cannot create ISO output directory.");
        QSaveFile output(target);
        require(output.open(QIODevice::WriteOnly), "Cannot create ISO output file.");
        quint64 written = 0;
        for (const auto &span : entry.extents)
            for (quint64 pos = 0; pos < span.size; pos += 1024 * 1024) {
                cancelled(cancel);
                const auto bytes = at(span.offset + pos, std::min<quint64>(1024 * 1024, span.size - pos));
                require(output.write(bytes) == bytes.size(), "Cannot write extracted ISO file.");
                written += bytes.size();
            }
        require(written == entry.size, "ISO file size differs from its extents.");
        cancelled(cancel);
        require(output.commit(), "Cannot publish extracted ISO file.");
        if (log && (i % 100 == 0 || i + 1 == plan.files.size()))
            log(QString("ISO: %1/%2 files").arg(i + 1).arg(plan.files.size()));
    }
    cancelled(cancel);
    verifyUnchanged();
    rejectLinks(parent, destination);
    require(QDir().rename(staging.path(), destination), "Cannot publish extracted ISO.");
    staging.setAutoRemove(false);
}
} // namespace prepare
