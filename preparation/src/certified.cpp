#include "certified.h"
#include <QSet>

namespace prepare {
CertifiedFile::CertifiedFile(QByteArray data, const KeyStore &keys, Cancel cancel)
    : data_(std::move(data)), cancel_(std::move(cancel)) {
    cancelled(cancel_);
    range(data_, 0, 32);
    revision_ = be16(data_, 8);
    category_ = be16(data_, 10);
    headerSize_ = be64(data_, 16);
    require(data_.left(4) == QByteArray("SCE\0", 4) && be32(data_, 4) == 2 &&
                (category_ == 1 || category_ == 3),
            "Unsupported PS3 Certified File.");
    require(headerSize_ >= 0x20 && headerSize_ <= quint64(data_.size()) && headerSize_ <= 16 * 1024 * 1024,
            "Invalid PS3 Certified File header size.");
    if (category_ == 1) {
        range(data_, 32, 80);
        require(be64(data_, 32) == 3, "Unsupported SELF extended header.");
        appOffset_ = be64(data_, 40);
        elfOffset_ = be64(data_, 48);
        phdrOffset_ = be64(data_, 56);
        shdrOffset_ = be64(data_, 64);
        segmentOffset_ = be64(data_, 72);
        controlOffset_ = be64(data_, 88);
        controlSize_ = be64(data_, 96);
        range(data_, appOffset_, 24);
        kind_ = int(be32(data_, appOffset_ + 12));
    }
    if (revision_ & 0x8000) {
        require(category_ == 1, "Unsupported unsigned firmware package.");
        return;
    }
    const quint64 rootOffset = quint64(be32(data_, 12)) + 0x20;
    require(rootOffset + 0x60 <= headerSize_, "Invalid PS3 metadata offset.");
    auto encrypted = part(data_, rootOffset, 64);
    if (kind_ == 8) {
        checkFreeNpdrm();
        const auto key =
            aesBlock(keys.namedKey("np_klic_free", 16), keys.namedKey("np_klic_key", 16), {}, false);
        encrypted = aesBlock(encrypted, key, QByteArray(16, '\0'), true);
    }
    for (const auto &candidate : keys.candidates(category_, kind_, revision_)) {
        cancelled(cancel_);
        const auto root = aesBlock(encrypted, candidate.key, candidate.iv, true);
        if (root.mid(16, 16) == QByteArray(16, '\0') && root.mid(48, 16) == QByteArray(16, '\0')) {
            const auto metadata = aesCtr(part(data_, rootOffset + 64, headerSize_ - rootOffset - 64),
                                         root.left(16), root.mid(32, 16), 0, cancel_);
            readMetadata(metadata);
            return;
        }
    }
    fail(QString("No matching decryption key: category %1, SELF type %2, revision %3.")
             .arg(category_)
             .arg(kind_)
             .arg(revision_));
}
void CertifiedFile::checkFreeNpdrm() const {
    const auto controls = part(data_, controlOffset_, controlSize_);
    quint64 at = 0;
    while (at < quint64(controls.size())) {
        const auto kind = be32(controls, at), size = be32(controls, at + 4);
        const auto following = be64(controls, at + 8);
        require(size >= 16, "Invalid SELF control record.");
        const auto record = part(controls, at, size);
        if (kind == 3) {
            require(part(record, 16, 4) == QByteArray("NPD\0", 4) && be32(record, 24) == 3,
                    "Only disc games and free-license disc updates are supported.");
            return;
        }
        at += size;
        if (!following)
            break;
    }
    fail("NPDRM SELF has no readable license information.");
}
void CertifiedFile::readMetadata(const QByteArray &metadata) {
    range(metadata, 0, 32);
    const quint64 count = be32(metadata, 12), keyCount = be32(metadata, 16), optional = be32(metadata, 20);
    require(count > 0 && count <= 4096 && keyCount <= 65536, "Invalid decrypted PS3 section count.");
    attributes_ = part(metadata, 32 + count * 48, keyCount * 16);
    range(metadata, 32 + count * 48 + keyCount * 16, optional);
    for (quint64 i = 0; i < count; ++i) {
        const quint64 at = 32 + i * 48;
        Section s{be64(metadata, at),      be64(metadata, at + 8),  be32(metadata, at + 16),
                  be32(metadata, at + 20), be32(metadata, at + 24), be32(metadata, at + 28),
                  be32(metadata, at + 32), be32(metadata, at + 36), be32(metadata, at + 40),
                  be32(metadata, at + 44)};
        require(s.offset >= headerSize_ && s.size <= MaxBinary, "Invalid PS3 section location/size.");
        range(data_, s.offset, s.size);
        require((s.encryption == 1 || s.encryption == 3) && (s.compression == 1 || s.compression == 2) &&
                    (s.signing == 2 || s.signing == 3 || s.signing == 6),
                "Unsupported PS3 section encryption, compression or digest.");
        if (s.encryption == 3) {
            attribute(s.key, 16);
            attribute(s.iv, 16);
        }
        attribute(s.digest, s.signing == 2 ? 96 : s.signing == 6 ? 64 : 32);
        sections_ << s;
    }
}
QByteArray CertifiedFile::attribute(quint32 index, quint64 size) const {
    return part(attributes_, quint64(index) * 16, size);
}
QByteArray CertifiedFile::sectionData(const Section &s, quint64 limit) const {
    cancelled(cancel_);
    auto bytes = part(data_, s.offset, s.size);
    if (s.encryption == 3)
        bytes = aesCtr(bytes, attribute(s.key, 16), attribute(s.iv, 16), 0, cancel_);
    const auto certificate = attribute(s.digest, s.signing == 2 ? 96 : s.signing == 6 ? 64 : 32);
    const auto digest = s.signing == 2   ? hmac(certificate.mid(32, 64), bytes, QCryptographicHash::Sha1)
                        : s.signing == 6 ? hmac(certificate.mid(32, 32), bytes, QCryptographicHash::Sha256)
                                         : QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
    require(equalDigest(digest, certificate.left(digest.size())),
            "PS3 section digest mismatch; input is corrupt or keys do not match.");
    if (s.compression == 2)
        bytes = inflateBounded(bytes, limit, cancel_);
    require(quint64(bytes.size()) <= limit, "PS3 section exceeds its output limit.");
    cancelled(cancel_);
    return bytes;
}
QByteArray CertifiedFile::elf() const {
    require(category_ == 1, "Expected a SELF executable.");
    const auto header = part(data_, elfOffset_, 64);
    require(header.left(7) == QByteArray("\x7f"
                                         "ELF\x02\x02\x01",
                                         7),
            "Expected a PS3 big-endian ELF64 executable.");
    const quint64 phoff = be64(header, 32), shoff = be64(header, 40), phsize = be16(header, 54),
                  phcount = be16(header, 56), shsize = be16(header, 58), shcount = be16(header, 60);
    require(be16(header, 52) == 64 && phsize == 56 && phcount <= 4096 && (!shcount || shsize == 64),
            "Invalid embedded ELF header tables.");
    QByteArray output;
    auto write = [&](quint64 offset, const QByteArray &block) {
        require(offset <= MaxBinary && quint64(block.size()) <= MaxBinary - offset,
                "ELF exceeds the preparation size limit.");
        if (offset + quint64(block.size()) > quint64(output.size()))
            output.append(QByteArray(qsizetype(offset + block.size() - output.size()), '\0'));
        output.replace(qsizetype(offset), block.size(), block);
    };
    write(0, header);
    const auto phdrs = part(data_, phdrOffset_, phsize * phcount);
    write(phoff, phdrs);
    if (shcount)
        write(shoff, part(data_, shdrOffset_, shsize * shcount));
    QSet<quint32> found;
    QVector<QPair<quint64, quint64>> covered;
    auto sections = sections_;
    if (revision_ & 0x8000) {
        sections.clear();
        range(data_, segmentOffset_, phcount * 32);
        for (quint64 i = 0; i < phcount; ++i) {
            cancelled(cancel_);
            const auto at = segmentOffset_ + i * 32;
            const auto encryption = be64(data_, at + 24);
            require(encryption == 0 || encryption == 2, "Unsigned SELF contains an encrypted section.");
            sections << Section{be64(data_, at),     be64(data_, at + 8), 2, quint32(i), 0, 0, 1, 0, 0,
                                be32(data_, at + 16)};
        }
    }
    for (const auto &s : sections) {
        if (s.type != 2)
            continue;
        require(s.index < phcount && !found.contains(s.index),
                "Invalid/duplicate ELF program section index.");
        found.insert(s.index);
        const quint64 at = s.index * phsize, offset = be64(phdrs, at + 8), size = be64(phdrs, at + 32);
        require(size <= MaxBinary, "ELF program exceeds the preparation size limit.");
        QByteArray data;
        if (revision_ & 0x8000) {
            data = part(data_, s.offset, s.size);
            if (s.compression == 2)
                data = inflateBounded(data, size, cancel_);
            else
                require(s.compression == 1, "Unsupported unsigned SELF compression.");
        } else
            data = sectionData(s, size);
        require(quint64(data.size()) == size, "ELF section size does not match its program header.");
        write(offset, data);
        covered << qMakePair(offset, offset + size);
    }
    for (quint64 i = 0; i < phcount; ++i) {
        const auto offset = be64(phdrs, i * phsize + 8), size = be64(phdrs, i * phsize + 32);
        bool alias = false;
        for (const auto &span : covered)
            if (offset >= span.first && offset <= span.second && size <= span.second - offset)
                alias = true;
        require(!size || found.contains(quint32(i)) || alias, "SELF is missing an ELF program section.");
    }
    cancelled(cancel_);
    return output;
}
QByteArray decryptElf(const QString &path, const KeyStore &keys, const Cancel &cancel) {
    cancelled(cancel);
    const auto data = readFile(path);
    require(!data.isEmpty(), "Empty executable.");
    if (data.left(6) == QByteArray("\x7f"
                                   "ELF\x02\x02",
                                   6))
        return data;
    return CertifiedFile(data, keys, cancel).elf();
}
} // namespace prepare
