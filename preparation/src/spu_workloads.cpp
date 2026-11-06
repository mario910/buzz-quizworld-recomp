#include "spu_lift.h"
#include <QDir>
#include <QRegularExpression>
#include <algorithm>
#include <iterator>

namespace prepare {
namespace {
QString hex(quint64 value, int width) {
    return QString::number(value, 16).toUpper().rightJustified(width, '0');
}
QString localIncludes(const QByteArray &source) {
    static const QRegularExpression include(R"rx(#include\s+"(?:\.\./)+runtime/spu/([A-Za-z0-9_]+\.h)")rx");
    return QString::fromUtf8(source).replace(include, "#include \"\\1\"");
}

struct DiscRole {
    int euOffset, usOffset;
    const char *name;
    int id;
};
struct DiscEntry {
    int offset, address;
    quint32 word;
};
struct UpdateJob {
    int offset, size, codeEnd;
    quint64 fingerprint;
    const char *name;
    int id;
};
struct UpdateEntry {
    int id, address;
};
#include "spu_raw_profiles.inc"

struct RawJob {
    QByteArray bytes;
    QString name;
    int id, offset, codeEnd;
    QSet<int> extra;
    QVector<int> probeSizes;
};

QVector<RawJob> discJobs(const QByteArray &elf, bool us, const Cancel &cancel) {
    const auto embedded = findSpuImages(elf, cancel);
    QVector<int> starts, boundaries{int(elf.size())};
    for (const auto &image : embedded)
        boundaries.append(int(image.offset));
    for (int offset = 0x700000; offset < std::min(qsizetype(0x850000), elf.size() - 16); offset += 128) {
        if (offset % 4096 == 0)
            cancelled(cancel);
        if (std::any_of(embedded.cbegin(), embedded.cend(), [=](const SpuImage &image) {
                return quint64(offset) >= image.offset && quint64(offset) - image.offset < image.size;
            }))
            continue;
        bool signature = true;
        for (int word = 0; word < 16; word += 4)
            signature &= (be32(elf, offset + word) & 0xFE00007F) == 0x42000002;
        if (signature)
            starts.append(offset);
    }
    require(starts.size() == qsizetype(std::size(discRoles)), "Unexpected disc raw job count.");
    boundaries += starts;
    std::sort(boundaries.begin(), boundaries.end());
    QVector<RawJob> jobs;
    auto registrations = embedded.size();
    for (const auto &role : discRoles) {
        const int offset = us ? role.usOffset : role.euOffset;
        require(starts.contains(offset), "Unexpected disc raw job layout.");
        const int span = *std::upper_bound(boundaries.cbegin(), boundaries.cend(), offset) - offset;
        require(span <= 0x40000 && span % 4 == 0, "Invalid raw job extent.");
        RawJob job{part(elf, offset, span), QString::fromLatin1(role.name), role.id, offset, 0, {}, {}};
        int lastReturn = span - 4;
        while (lastReturn >= 16 && be32(job.bytes, lastReturn) != 0x35000000)
            lastReturn -= 4;
        require(lastReturn >= 16, "No return boundary in " + job.name);
        job.codeEnd = std::min(span, (lastReturn + 0x100 + 15) & ~15);
        for (int size = span; size >= span - 224 && size > lastReturn; size -= 16)
            job.probeSizes.append(size);
        registrations += job.probeSizes.size();
        if (!us) {
            for (const auto &extra : euEntries) {
                if (extra.offset != offset)
                    continue;
                require(extra.address < job.codeEnd && be32(job.bytes, extra.address) == extra.word,
                        "Reviewed raw job entry changed in " + job.name);
                job.extra.insert(extra.address);
            }
        }
        jobs.append(std::move(job));
    }
    require(registrations <= 256, "Workload registration budget exceeded.");
    return jobs;
}

QVector<RawJob> updateRawJobs(const QByteArray &elf) {
    constexpr int signalSize = 2224;
    const ElfImage image(elf);
    auto signal = image.virtualBytes(0x7E9F80, signalSize);
    require(signal.size() == signalSize, "Unmapped raw signal job.");
    QVector<RawJob> jobs{{std::move(signal), "buzz_signal", 32, -1, 0x8A0, {}, {signalSize}}};
    for (const auto &entry : updateJobs) {
        auto bytes = part(elf, entry.offset, entry.size);
        const auto name = QString::fromLatin1(entry.name);
        require(spuFingerprint(bytes) == entry.fingerprint, "Unexpected raw job contents: " + name);
        RawJob job{std::move(bytes), name, entry.id, entry.offset, entry.codeEnd, {}, {entry.size}};
        for (const auto &extra : updateEntries) {
            if (extra.id == entry.id)
                job.extra.insert(extra.address);
        }
        jobs.append(std::move(job));
    }
    return jobs;
}
} // namespace

QJsonArray liftSpuWorkloads(const QString &images, const QString &output, const QString &title,
                            const Cancel &cancel) {
    require(!title.contains('\n') && !title.contains('\r') && !title.contains("*/"),
            "Invalid SPU registry title.");
    const QDir inputs(images), destination(output);
    const auto files = inputs.entryList({"*.elf"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    require(!files.isEmpty(), "No SPU ELF files found.");
    QSet<QString> prefixes;
    QStringList declarations, registrations;
    QJsonArray reports;
    for (const auto &file : files) {
        cancelled(cancel);
        const auto data = readFile(inputs.filePath(file));
        const auto image = validateSpuImage(data);
        const auto name = file.chopped(4);
        require(QRegularExpression("^[A-Za-z0-9_]+$").match(name).hasMatch(),
                "Invalid SPU workload name: " + file);
        const QString prefix = (name.front().isDigit() ? "img_" : "") + name + "_";
        require(!prefixes.contains(prefix), "Duplicate SPU symbol prefix: " + prefix);
        prefixes.insert(prefix);
        SpuLiftOptions options;
        options.prefix = prefix;
        const auto code = decodeSpuText(data, cancel);
        const auto bounds = findSpuBounds(data, code, cancel);
        const auto generated = liftSpu(code, bounds, options, cancel);
        const auto entry = prefix + "spu_func_" + hex(image.entry, 8);
        require(generated.source.contains(("void " + entry + "(spu_context* ctx) {").toUtf8()),
                "Missing generated SPU workload entry: " + entry);
        atomicWrite(output, destination.filePath(name + "/spu_recomp.h"),
                    localIncludes(generated.header).toUtf8(), cancel);
        atomicWrite(output, destination.filePath(name + "/spu_recomp.c"),
                    localIncludes(generated.source).toUtf8(), cancel);
        declarations.append("extern void " + entry + "(spu_context*);");
        declarations.append("extern void " + prefix + "spu_recomp_register(void);");
        const auto id = QString::number(reports.size() + 1);
        registrations.append("    spu_begin_image(" + id + "); " + prefix + "spu_recomp_register();");
        const auto registerImage = [&](quint64 fingerprint, const QString &label) {
            registrations.append("    spu_workload_register_img(0x" + hex(fingerprint, 16) + "ULL, " + entry +
                                 ", " + id + ", \"" + label + "\");");
        };
        registerImage(spuFingerprint(part(data, 0, image.size)), name);
        for (const auto &segment : image.loads) {
            if (segment.flags & 1) {
                const auto fingerprint = spuFingerprint(part(data, segment.offset, segment.fileSize));
                if (fingerprint)
                    registerImage(fingerprint, name + ".text");
                break;
            }
        }
        auto report = generated.report;
        report["image"] = name;
        reports.append(report);
    }
    QStringList registry{"/* spu_workloads.c -- GENERATED by BuzzPrepare.",
                         " * Registers the lifted SPU images of \"" + title +
                             "\" with the workload dispatch registry. */",
                         "#include \"spu_workload.h\"",
                         "",
                         "extern void spu_begin_image(int image_id);",
                         ""};
    registry.append(declarations);
    registry.append({"", "void spu_register_all(void)", "{"});
    registry.append(registrations);
    registry.append({"    spu_begin_image(0);", "}", "",
                     "__attribute__((constructor)) static void spu_register_all_ctor(void)", "{",
                     "    spu_register_all();", "}", ""});
    atomicWrite(output, destination.filePath("spu_workloads.c"), registry.join('\n').toUtf8(), cancel);
    return reports;
}
QJsonArray liftSpuRawJobs(const QByteArray &elf, const QString &titleId, const QString &output,
                          const Cancel &cancel) {
    const auto digest = QCryptographicHash::hash(elf, QCryptographicHash::Sha256).toHex();
    const auto profile = identify(titleId, QString::fromLatin1(digest));
    const auto jobs = profile == "quizworld-102-verified"
                          ? updateRawJobs(elf)
                          : discJobs(elf, profile == "quizworld-us100", cancel);
    const QDir destination(output);
    QStringList declarations, registrations;
    QJsonArray reports;
    for (const auto &job : jobs) {
        cancelled(cancel);
        QVector<SpuInstruction> instructions;
        instructions.reserve(job.bytes.size() / 4);
        // Return classification uses the entire payload, even past the lifted code.
        for (int address = 0; address + 4 <= job.bytes.size(); address += 4) {
            if (address % 4096 == 0)
                cancelled(cancel);
            instructions.append(decodeSpu(be32(job.bytes, address), address));
        }
        const auto bounds = findRawSpuBounds(instructions.first(job.codeEnd / 4), 0x10, job.extra, cancel);
        SpuLiftOptions options;
        options.prefix = job.name + "_";
        const auto generated = liftSpu(instructions, bounds, options, cancel);
        atomicWrite(output, destination.filePath(job.name + "/spu_recomp.h"),
                    localIncludes(generated.header).toUtf8(), cancel);
        atomicWrite(output, destination.filePath(job.name + "/spu_recomp.c"),
                    localIncludes(generated.source).toUtf8(), cancel);
        const QString entry = options.prefix + "spu_func_00000010";
        declarations.append("extern void " + entry + "(spu_context*);");
        declarations.append("extern void " + options.prefix + "spu_recomp_register(void);");
        const auto id = QString::number(job.id);
        registrations.append("    spu_begin_image(" + id + "); " + options.prefix + "spu_recomp_register();");
        QJsonArray sizes;
        for (int size : job.probeSizes) {
            const auto fingerprint = spuFingerprint(part(job.bytes, 0, size));
            registrations.append("    spu_workload_register_img(0x" + hex(fingerprint, 16) + "ULL, " + entry +
                                 ", " + id + ", \"" + job.name + "\");");
            sizes.append(size);
        }
        reports.append(QJsonObject{{"name", job.name},
                                   {"image_id", job.id},
                                   {"offset", job.offset},
                                   {"span", job.bytes.size()},
                                   {"code_end", job.codeEnd},
                                   {"probe_sizes", sizes}});
    }
    QStringList registry{"/* Generated by BuzzPrepare. */", "#include \"spu_workload.h\"",
                         "extern void spu_begin_image(int);"};
    registry.append(declarations);
    registry.append({"__attribute__((constructor)) static void buzz_raw_jobs_register(void)", "{"});
    registry.append(registrations);
    registry.append({"    spu_begin_image(0);", "}", ""});
    atomicWrite(output, destination.filePath("jobmods_register.c"), registry.join('\n').toUtf8(), cancel);
    return reports;
}
QJsonArray liftMultiStream(const QByteArray &elf, const QString &titleId, const QString &firmware,
                           const QString &output, const Cancel &cancel) {
    const auto digest = QCryptographicHash::hash(elf, QCryptographicHash::Sha256).toHex();
    const auto profile = identify(titleId, QString::fromLatin1(digest));
    const bool update = profile == "quizworld-102-verified";
    const bool us = profile == "quizworld-us100";
    struct Module {
        const char *name;
        quint32 offset;
        const char *digest;
        int base;
        QVector<int> entries;
        int skipLo = 0, skipHi = 0;
    };
    const Module modules[] = {
        {"atrac",
         0,
         "c543ecc87e82beec3159d88fbb73dc4229424d357c0a48be3c4cb55d3fdefadc",
         update ? 0x32B10 : 0x32890,
         {update ? 0x36DD8 : 0x36B58},
         0x53C0,
         0x53D0},
        {"compressor",
         update ? 0x71F480u : (us ? 0x736300u : 0x737100u),
         update ? "7afc1a08a0536176a9a6a7f2ff0749c5581dec1f18caf2aaae7640d5a382c0d7"
                : "64fa006a8073a801d87fd816926189a15cbac8ffb8d67cf5fe78adce1e711f05",
         update ? 0x2FE10 : 0x2FB90,
         {update ? 0x304F0 : 0x30278}},
        {"vumeter", update ? 0x722680u : (us ? 0x739380u : 0x73A180u),
         update ? "e6cd56546fe5ff596e009077fd028f9b5e71ce1d1046832f9f578dbeef205d1a"
                : "c3a67640bca66ef439648e609637d52b3bfe830f6c0182207cc97440c89b89c0",
         update ? 0x2FA10 : 0x2F7F0, update ? QVector<int>{0x2FA70} : QVector<int>{}},
    };
    const QDir destination(output);
    QStringList registry{
        "/* GENERATED by BuzzPrepare: runtime-loaded MultiStream code. */", "#include <stdint.h>",
        "extern void spu_begin_image(int image_id);",
        "extern void spu_runtime_code_register(uint32_t lsa, uint32_t size, uint32_t skip_lo,",
        "                                      uint32_t skip_hi, uint64_t fnv, int image_id);"};
    QStringList calls;
    QJsonArray reports;
    for (const auto &module : modules) {
        cancelled(cancel);
        const auto name = QString::fromLatin1(module.name);
        QByteArray text;
        if (module.offset) {
            const auto image = validateSpuImage(elf, module.offset);
            text = elfText(part(elf, module.offset, image.size));
        } else {
            text = elfText(readFile(QDir(firmware).filePath("sys/external/flashATRAC.pic")));
        }
        require(QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex() == module.digest,
                "Unreviewed MultiStream module: " + name);
        require(!text.isEmpty() && text.size() % 4 == 0 && module.base + text.size() <= 0x40000 &&
                    module.skipHi <= text.size(),
                "Invalid MultiStream module extent: " + name);
        QVector<SpuInstruction> code;
        for (int offset = 0; offset < text.size(); offset += 4) {
            if (offset % 4096 == 0)
                cancelled(cancel);
            code.append(decodeSpu(be32(text, offset), module.base + offset));
        }
        const auto bounds = findSpuModuleBounds(text, code, module.entries, cancel);
        SpuLiftOptions options;
        options.prefix = "buzz_mstream_" + name + "_";
        const auto generated = liftSpu(code, bounds, options, cancel);
        require(!generated.source.contains("spu_unsupported"), "Unsupported MultiStream code: " + name);
        atomicWrite(output, destination.filePath(name + "/spu_recomp.h"), generated.header, cancel);
        atomicWrite(output, destination.filePath(name + "/spu_recomp.c"), generated.source, cancel);

        // Runtime-loaded code uses standard FNV-1a, unlike the image registry.
        quint64 fingerprint = 0xCBF29CE484222325ULL;
        for (qsizetype offset = 0; offset < text.size(); ++offset) {
            if (offset < module.skipLo || offset >= module.skipHi)
                fingerprint = (fingerprint ^ quint8(text.at(offset))) * 0x100000001B3ULL;
        }
        const auto id = QString::number(50 + reports.size());
        const auto registrar = options.prefix + "spu_recomp_register();";
        registry.append("extern void " + options.prefix + "spu_recomp_register(void);");
        calls.append("    spu_begin_image(" + id + "); " + registrar);
        calls.append("    spu_runtime_code_register(0x" + hex(module.base, 5) + "u, 0x" +
                     hex(text.size(), 0) + "u, 0x" + hex(module.skipLo, 0) + "u, 0x" + hex(module.skipHi, 0) +
                     "u, 0x" + hex(fingerprint, 16) + "ull, " + id + ");");
        reports.append(QJsonObject{{"name", name},
                                   {"base", module.base},
                                   {"size", text.size()},
                                   {"fingerprint", hex(fingerprint, 16)}});
    }
    registry.append({"", "static void buzz_mstream_register(void)", "{"});
    registry.append(calls);
    registry.append(
        {"    spu_begin_image(0);", "}",
         "/* Before the game starts MultiStream (as spu_workloads.c registers images). */",
         "__attribute__((constructor)) static void buzz_mstream_init(void) { buzz_mstream_register(); }",
         ""});
    atomicWrite(output, destination.filePath("mstream_register.c"), registry.join('\n').toUtf8(), cancel);
    return reports;
}
} // namespace prepare
