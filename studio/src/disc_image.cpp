#include "disc_image.h"
#include "pack_format.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <zlib.h>

namespace studio {
using namespace pack;
namespace {
constexpr qint64 sector = 2048;
QString tr(const char *text) { return QCoreApplication::translate("DiscImage", text); }
} // namespace

bool DiscImage::isDiscImage(const QString &path) {
    const auto suffix = QFileInfo(path).suffix().toLower();
    return suffix == "iso" || suffix == "cso";
}

DiscImage::DiscImage(const QString &path) : file_(path) {
    if (!file_.open(QIODevice::ReadOnly))
        fail(file_.errorString());
    const auto head = file_.read(24);
    if (head.startsWith("CISO")) {
        // CISO v1: header, then one 32-bit index entry per block (+1). The
        // high bit marks a stored block; others are raw deflate streams.
        compressed_ = true;
        // The header-size field is 0 in many dumps; the index always follows at 24.
        if (head.size() < 24 || u32(head, 16) != 2048 || quint8(head[21]) > 16)
            fail(tr("Nieobsługiwany obraz CSO."));
        size_ = qint64(u32(head, 8)) | (qint64(u32(head, 12)) << 32);
        blockSize_ = u32(head, 16);
        align_ = quint8(head[21]);
        const qint64 blocks = (size_ + blockSize_ - 1) / blockSize_;
        if (size_ <= 0 || blocks > 16 * 1024 * 1024)
            fail(tr("Nieobsługiwany obraz CSO."));
        file_.seek(24);
        const auto raw = file_.read((blocks + 1) * 4);
        if (raw.size() != (blocks + 1) * 4)
            fail(tr("Ucięty obraz CSO."));
        index_.resize(int(blocks + 1));
        for (int i = 0; i <= blocks; ++i)
            index_[i] = u32(raw, qint64(i) * 4);
    } else {
        size_ = file_.size();
    }
    const auto descriptor = readAt(16 * sector, sector);
    if (descriptor.mid(1, 5) != "CD001" || quint8(descriptor[0]) != 1)
        fail(tr("To nie jest obraz płyty ISO 9660."));
    const auto root = descriptor.mid(156, 34);
    walk(u32(root, 2), u32(root, 10), {}, 0);
    if (files_.isEmpty())
        fail(tr("Obraz płyty jest pusty."));
}

QByteArray DiscImage::block(qint64 number) {
    if (number == cachedBlock_)
        return cache_;
    if (number < 0 || number + 1 >= index_.size())
        fail(tr("Odczyt poza obrazem CSO."));
    const bool stored = index_[number] & 0x80000000u;
    const qint64 start = qint64(index_[number] & 0x7fffffffu) << align_;
    const qint64 end = qint64(index_[number + 1] & 0x7fffffffu) << align_;
    if (end < start || end - start > 2 * blockSize_ + 1024 || !file_.seek(start))
        fail(tr("Uszkodzony obraz CSO."));
    const auto data = file_.read(end - start);
    QByteArray out;
    if (stored) {
        out = data.left(blockSize_);
    } else {
        out.resize(blockSize_);
        z_stream stream{};
        if (inflateInit2(&stream, -15) != Z_OK)
            fail(tr("Uszkodzony obraz CSO."));
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
        stream.avail_in = uInt(data.size());
        stream.next_out = reinterpret_cast<Bytef *>(out.data());
        stream.avail_out = uInt(out.size());
        const int status = inflate(&stream, Z_FINISH);
        const auto produced = out.size() - qint64(stream.avail_out);
        inflateEnd(&stream);
        if ((status != Z_STREAM_END && status != Z_BUF_ERROR) || produced <= 0)
            fail(tr("Uszkodzony blok obrazu CSO."));
        out.truncate(produced);
    }
    cachedBlock_ = number;
    cache_ = out;
    return out;
}

QByteArray DiscImage::readAt(qint64 offset, qint64 size) {
    if (offset < 0 || size < 0 || offset > size_ || size > size_ - offset)
        fail(tr("Odczyt poza obrazem płyty."));
    if (!compressed_) {
        if (!file_.seek(offset))
            fail(file_.errorString());
        const auto out = file_.read(size);
        if (out.size() != size)
            fail(tr("Ucięty obraz płyty."));
        return out;
    }
    QByteArray out;
    out.reserve(size);
    while (size > 0) {
        const auto data = block(offset / blockSize_);
        const auto at = offset % blockSize_;
        const auto take = std::min<qint64>(size, data.size() - at);
        if (take <= 0)
            fail(tr("Ucięty obraz CSO."));
        out.append(data.constData() + at, take);
        offset += take;
        size -= take;
    }
    return out;
}

void DiscImage::walk(qint64 extent, qint64 size, const QString &prefix, int depth) {
    if (depth > 16 || size > 16 * 1024 * 1024)
        fail(tr("Niepoprawna struktura katalogów płyty."));
    const auto data = readAt(extent * sector, size);
    qint64 p = 0;
    while (p < data.size()) {
        const int length = quint8(data[p]);
        if (length == 0) { // Records never cross a sector boundary.
            p = (p / sector + 1) * sector;
            continue;
        }
        check(data, p, length);
        if (length < 34)
            fail(tr("Niepoprawna struktura katalogów płyty."));
        const auto record = data.mid(p, length);
        p += length;
        const int nameLength = quint8(record[32]);
        check(record, 33, nameLength);
        const auto rawName = record.mid(33, nameLength);
        if (rawName == QByteArray(1, '\0') || rawName == QByteArray(1, '\1'))
            continue;
        auto name = QString::fromLatin1(rawName).section(';', 0, 0).toUpper();
        if (name.endsWith('.'))
            name.chop(1);
        if (name.isEmpty() || name.contains('/') || name == "." || name == "..")
            continue;
        const qint64 start = u32(record, 2), bytes = u32(record, 10);
        if (quint8(record[25]) & 2)
            walk(start, bytes, prefix + name + '/', depth + 1);
        else
            files_.insert(prefix + name, File{start * sector, bytes});
    }
}

QByteArray DiscImage::read(const QString &path, qint64 limit) {
    const auto it = files_.constFind(path.toUpper());
    if (it == files_.constEnd())
        fail(tr("Brak pliku %1 w obrazie płyty.").arg(path));
    if (it->size > limit)
        fail(tr("Plik %1 w obrazie płyty jest za duży.").arg(path));
    return readAt(it->offset, it->size);
}
} // namespace studio
