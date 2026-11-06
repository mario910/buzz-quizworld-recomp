#include "elf.h"
#include <QDir>
#include <QJsonDocument>
#include <QMap>
#include <algorithm>
#include <optional>

namespace prepare {
namespace {
QString hex(quint64 value) {
    return "0x" + QString::number(value, 16);
}
std::optional<quint32> read32(const ElfImage &elf, quint64 address) {
    const auto bytes = elf.virtualBytes(address, 4);
    if (bytes.isEmpty())
        return std::nullopt;
    return be32(bytes, 0);
}
QString moduleName(const ElfImage &elf, quint64 address) {
    QByteArray name;
    for (int i = 0; i < 64; ++i) {
        const auto bytes = elf.virtualBytes(address + i, 1);
        if (bytes.isEmpty() || bytes[0] == 0)
            break;
        name += bytes;
    }
    return QString::fromLatin1(name);
}
QJsonArray imports(const ElfImage &elf, const Cancel &cancel) {
    std::optional<quint64> parameter;
    for (const auto &segment : elf.segments())
        if (segment.type == 0x60000002) {
            parameter = segment.address;
            break;
        }
    if (!parameter)
        return {};
    const auto header = elf.virtualBytes(*parameter, 32);
    if (header.isEmpty() || be32(header, 4) != 0x1b434cec)
        return {};
    const quint64 start = be32(header, 24), end = be32(header, 28);
    require(start <= end && end - start <= 16 * 1024 * 1024, "Invalid firmware import table range.");
    struct Library {
        QString name;
        quint64 count, nids, stubs;
    };
    QVector<Library> libraries;
    QVector<quint64> nids, stubs;
    for (auto at = start; at < end;) {
        cancelled(cancel);
        const auto first = elf.virtualBytes(at, 1);
        require(!first.isEmpty(), "Unmapped firmware import descriptor.");
        const auto size = quint8(first[0]);
        if (!size)
            break;
        require(size >= 28 && size <= end - at && libraries.size() < 4096,
                "Invalid firmware import descriptor.");
        const auto row = elf.virtualBytes(at, size);
        require(!row.isEmpty(), "Truncated firmware import descriptor.");
        const quint64 count = be16(row, 6), nid = be32(row, 20), stub = be32(row, 24);
        require(count <= 4096, "Oversized firmware import library.");
        libraries << Library{moduleName(elf, be32(row, 16)), count, nid, stub};
        if (nid)
            nids << nid;
        if (stub)
            stubs << stub;
        at += size;
    }
    std::sort(nids.begin(), nids.end());
    std::sort(stubs.begin(), stubs.end());
    auto span = [](quint64 address, const QVector<quint64> &starts, quint64 fallback) {
        const auto next = std::upper_bound(starts.begin(), starts.end(), address);
        const auto end = next == starts.end() ? fallback : *next;
        return end > address ? (end - address) / 4 : 0;
    };
    QJsonArray result;
    for (auto entry : libraries) {
        cancelled(cancel);
        if (!entry.count && entry.nids && entry.stubs) {
            // PSL1GHT can leave the count zero. The next table in either
            // independently ordered sequence bounds the current library.
            for (const auto candidate : {span(entry.nids, nids, start), span(entry.stubs, stubs, 0)})
                if (candidate && candidate <= 4096 && (!entry.count || candidate < entry.count))
                    entry.count = candidate;
        }
        for (quint64 i = 0; i < entry.count; ++i) {
            const auto nid = read32(elf, entry.nids + i * 4), stub = read32(elf, entry.stubs + i * 4);
            if (nid && stub) {
                require(result.size() < 65536, "Too many firmware imports.");
                result.append(QJsonObject{{"library", entry.name}, {"nid", hex(*nid)}, {"stub", hex(*stub)}});
            }
        }
    }
    return result;
}
} // namespace
PpuAnalysis analyzePpu(const ElfImage &elf, const Cancel &cancel) {
    require(elf.is64() && elf.machine() == 21 && (elf.type() == 2 || elf.type() == 3),
            "Expected a PowerPC64 executable or module.");
    PpuAnalysis result;
    QVector<QPair<quint64, quint64>> code;
    for (const auto &segment : elf.segments())
        if (segment.type == 1) {
            if ((segment.flags & 1) && segment.fileSize)
                code << qMakePair(segment.address, segment.address + segment.fileSize);
            if (segment.memorySize)
                result.image.append(QJsonObject{{"vaddr", hex(segment.address)},
                                                {"filesz", hex(segment.fileSize)},
                                                {"memsz", hex(segment.memorySize)},
                                                {"flags", qint64(segment.flags)},
                                                {"file_offset", hex(segment.offset)}});
        }
    auto codeEnd = [&](quint64 address) -> quint64 {
        for (const auto &span : code)
            if (address >= span.first && address < span.second)
                return span.second;
        return 0;
    };
    const auto entry = elf.entry();
    quint64 entryCode = entry, entryToc = 0, moduleToc = 0, opdBase = 0, opdSize = 0;
    const auto descriptor = elf.virtualBytes(entry, 8);
    if (!descriptor.isEmpty() && codeEnd(be32(descriptor, 0))) {
        entryCode = be32(descriptor, 0);
        entryToc = be32(descriptor, 4);
    }
    for (const auto &section : elf.sections())
        if (section.size && entry >= section.address && entry - section.address < section.size &&
            (!opdSize || section.size < opdSize)) {
            opdBase = section.address;
            opdSize = section.size;
        }
    QMap<quint64, QPair<quint64, quint64>> functions;
    QMap<quint64, int> tocCounts;
    QVector<quint64> tocOrder;
    int descriptors = 0;
    if (opdSize) {
        require(opdSize <= MaxBinary, "ELF OPD region exceeds its size limit.");
        const auto bytes = elf.virtualBytes(opdBase, opdSize);
        for (qsizetype at = 0; at + 8 <= bytes.size(); at += 8) {
            if (at % 32768 == 0)
                cancelled(cancel);
            const auto address = be32(bytes, at), toc = be32(bytes, at + 4);
            if ((!address && !toc) || !codeEnd(address))
                continue;
            require(++descriptors <= 1000000, "ELF OPD exceeds its descriptor limit.");
            if (!tocCounts.contains(toc))
                tocOrder << toc;
            ++tocCounts[toc];
            functions[address] = qMakePair(quint64(toc), opdBase + quint64(at));
        }
    }
    int maxToc = 0;
    for (const auto toc : tocOrder)
        if (tocCounts[toc] > maxToc) {
            moduleToc = toc;
            maxToc = tocCounts[toc];
        }
    if (!entryToc)
        entryToc = moduleToc;
    for (auto it = functions.begin(); it != functions.end(); ++it) {
        cancelled(cancel);
        auto end = codeEnd(it.key());
        const auto next = std::next(it);
        if (next != functions.end() && end && next.key() <= end)
            end = next.key();
        result.functions.append(QJsonObject{{"start", hex(it.key())},
                                            {"end", hex(end ? end : it.key())},
                                            {"toc", hex(it.value().first)},
                                            {"opd", hex(it.value().second)}});
    }
    result.loader = {{"entry_opd", hex(entry)},
                     {"entry_code", hex(entryCode)},
                     {"entry_toc", hex(entryToc)},
                     {"module_toc", hex(moduleToc)},
                     {"opd_base", opdBase ? QJsonValue(hex(opdBase)) : QJsonValue()},
                     {"opd_size", opdSize ? QJsonValue(hex(opdSize)) : QJsonValue()},
                     {"opd_count", descriptors},
                     {"function_count", functions.size()},
                     {"e_type", elf.type()},
                     {"load_segments", result.image.size()}};
    result.imports = imports(elf, cancel);
    return result;
}
void writePpuAnalysis(const PpuAnalysis &analysis, const QString &directory, const QString &name,
                      const Cancel &cancel) {
    require(!name.contains('/') && archivePath(name) == name, "Invalid analysis output basename.");
    for (const auto &entry : {qMakePair(QString("functions"), QJsonDocument(analysis.functions)),
                              qMakePair(QString("image"), QJsonDocument(analysis.image)),
                              qMakePair(QString("loader"), QJsonDocument(analysis.loader)),
                              qMakePair(QString("imports"), QJsonDocument(analysis.imports))})
        atomicWrite(directory, QDir(directory).filePath(name + "." + entry.first + ".json"),
                    entry.second.toJson(), cancel);
}
} // namespace prepare
