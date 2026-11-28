#include "pack_format.h"
#define ZLIB_CONST
#include <zlib.h>

namespace studio::pack {
QByteArray inflateZipMember(const QByteArray &bytes, qsizetype size) {
    QByteArray output(std::max(qsizetype(1), size), Qt::Uninitialized);
    z_stream stream{};
    stream.next_in = reinterpret_cast<const Bytef *>(bytes.constData());
    stream.avail_in = static_cast<uInt>(bytes.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        fail(QCoreApplication::translate("BuzzImport", "Uszkodzony nagłówek ZIP."));
    const auto result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END || stream.total_out != static_cast<uLong>(size) || stream.avail_in != 0)
        fail(QCoreApplication::translate("BuzzImport", "Ucięte archiwum paczki."));
    output.resize(size);
    return output;
}
} // namespace studio::pack
