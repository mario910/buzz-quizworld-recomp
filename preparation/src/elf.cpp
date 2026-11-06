#include "elf.h"

namespace prepare {
ElfImage::ElfImage(QByteArray bytes) : bytes_(std::move(bytes)) {
    range(bytes_, 0, 16);
    require(bytes_.left(4) == QByteArray("\x7f"
                                         "ELF",
                                         4) &&
                (bytes_[4] == 1 || bytes_[4] == 2) && bytes_[5] == 2 && bytes_[6] == 1,
            "Expected a big-endian ELF32/ELF64 image.");
    is64_ = bytes_[4] == 2;
    range(bytes_, 0, is64_ ? 64 : 52);
    type_ = be16(bytes_, 16);
    machine_ = be16(bytes_, 18);
    require(be32(bytes_, 20) == 1, "Invalid ELF version.");
    entry_ = is64_ ? be64(bytes_, 24) : be32(bytes_, 24);
    const auto phoff = is64_ ? be64(bytes_, 32) : be32(bytes_, 28),
               shoff = is64_ ? be64(bytes_, 40) : be32(bytes_, 32);
    const quint64 phSize = be16(bytes_, is64_ ? 54 : 42), phCount = be16(bytes_, is64_ ? 56 : 44),
                  shSize = be16(bytes_, is64_ ? 58 : 46), shCount = be16(bytes_, is64_ ? 60 : 48);
    require(be16(bytes_, is64_ ? 52 : 40) == (is64_ ? 64 : 52) && phCount <= 4096 && shCount <= 4096 &&
                (!phCount || phSize == (is64_ ? 56 : 32)) && (!shCount || shSize == (is64_ ? 64 : 40)),
            "Invalid ELF table layout.");
    if (phCount)
        range(bytes_, phoff, phSize * phCount);
    if (shCount)
        range(bytes_, shoff, shSize * shCount);
    for (quint64 i = 0; i < phCount; ++i) {
        const auto at = phoff + i * phSize;
        auto segment = is64_ ? Segment{be32(bytes_, at),      be32(bytes_, at + 4),  be64(bytes_, at + 8),
                                       be64(bytes_, at + 16), be64(bytes_, at + 32), be64(bytes_, at + 40)}
                             : Segment{be32(bytes_, at),     be32(bytes_, at + 24), be32(bytes_, at + 4),
                                       be32(bytes_, at + 8), be32(bytes_, at + 16), be32(bytes_, at + 20)};
        if (segment.type == 1) {
            require(segment.fileSize <= segment.memorySize && segment.address <= 0xffffffffull &&
                        segment.memorySize <= 0x100000000ull - segment.address,
                    "Invalid ELF load segment.");
            if (segment.fileSize)
                range(bytes_, segment.offset, segment.fileSize);
        }
        segments_ << segment;
    }
    for (quint64 i = 0; i < shCount; ++i) {
        const auto at = shoff + i * shSize;
        sections_ << (is64_ ? ElfSection{be64(bytes_, at + 16), be64(bytes_, at + 24), be64(bytes_, at + 32),
                                         be32(bytes_, at + 4)}
                            : ElfSection{be32(bytes_, at + 12), be32(bytes_, at + 16), be32(bytes_, at + 20),
                                         be32(bytes_, at + 4)});
    }
}
QByteArray ElfImage::virtualBytes(quint64 address, quint64 length) const {
    for (const auto &segment : segments_)
        if (segment.type == 1 && segment.fileSize && address >= segment.address) {
            const auto offset = address - segment.address;
            if (offset < segment.fileSize && length <= segment.fileSize - offset)
                return part(bytes_, segment.offset + offset, length);
        }
    return {};
}
} // namespace prepare
