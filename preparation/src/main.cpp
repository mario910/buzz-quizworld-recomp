#include "disc.h"
#include "wizard.h"
#include "elf.h"
#include "hle_nids.h"
#include "import.h"
#include "ppu_decode.h"
#include "ppu_functions.h"
#include "session.h"
#include "spu_decode.h"
#include "spu_images.h"
#include "spu_lift.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <atomic>
#include <csignal>
#include <cstdio>

namespace {
static_assert(std::atomic_bool::is_always_lock_free);
std::atomic_bool interrupted{false};
void interrupt(int) {
    interrupted.store(true, std::memory_order_relaxed);
}
} // namespace

int main(int argc, char **argv) {
    if (argc == 1) return runPreparationWizard(argc, argv);
    QCoreApplication app(argc, argv);
    app.setApplicationName("BuzzPrepare");
    app.setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Native preparation commands. Start without arguments to open the wizard.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions(
        {QCommandLineOption("import-game", "Import a game ISO/dump into a native preparation session.",
                            "path"),
         QCommandLineOption("firmware", "Firmware PUP or installed dev_flash for import.", "path"),
         QCommandLineOption("update", "Optional PS3 update PKG/SELF/ELF.", "path"),
         QCommandLineOption("resume", "Resume the native import session selected with --out."),
         QCommandLineOption("analyze-elf", "Generate PPU load/OPD/import metadata from a decrypted ELF.",
                            "file"),
         QCommandLineOption("extract-spu", "Extract and fingerprint embedded SPU ELF images.", "file"),
         QCommandLineOption("find-ppu-functions", "Discover function bounds in a PPU ELF.", "file"),
         QCommandLineOption("decode-ppu", "Decode raw big-endian PPU instructions to TSV.", "file"),
         QCommandLineOption("ppu-base", "Base address for raw PPU instructions.", "address", "0"),
         QCommandLineOption("decode-spu", "Decode executable instructions from an SPU ELF.", "file"),
         QCommandLineOption("lift-spu", "Discover SPU functions and generate C code from an ELF.", "file"),
         QCommandLineOption("lift-spu-workloads", "Generate SPU C sources and workload registrations.",
                            "directory"),
         QCommandLineOption("lift-spu-raw-jobs", "Generate raw SPURS jobs from a verified game ELF.", "file"),
         QCommandLineOption("lift-multistream", "Generate MultiStream modules from a verified game ELF.",
                            "file"),
         QCommandLineOption("generate-hle-nids", "Generate HLE registrations from SDK library sources.",
                            "sdk"),
         QCommandLineOption("spu-title", "Title label for generated SPU workload registration.", "title",
                            "spu"),
         QCommandLineOption("spu-bounds", "Override discovery with a JSON array of start/end pairs.", "file"),
         QCommandLineOption("symbol-prefix", "Prefix generated SPU symbols.", "prefix"),
         QCommandLineOption("trace-spu", "Add instruction trace probes to generated SPU code."),
         QCommandLineOption("inspect-iso", "Inspect a native ISO9660/Joliet game image.", "file"),
         QCommandLineOption("extract-iso", "Extract a game ISO to a new directory.", "file"),
         QCommandLineOption("decrypt-elf", "Reconstruct ELF from a SELF/NPDRM input.", "file"),
         QCommandLineOption("extract-firmware", "Import and validate a firmware PUP.", "file"),
         QCommandLineOption("extract-update", "Extract a PS3 update package.", "file"),
         QCommandLineOption("validate-firmware", "Validate dev_flash fonts and ATRAC module.", "directory"),
         QCommandLineOption("keys", "Local private key file (values are never logged).", "file"),
         QCommandLineOption("out", "Output file/directory.", "path"),
         QCommandLineOption("title-id", "Expected disc title ID.", "id", "BCES00647")});
    parser.process(app);
    std::signal(SIGINT, interrupt);
    std::signal(SIGTERM, interrupt);
#ifdef Q_OS_WIN
    std::signal(SIGBREAK, interrupt);
#endif
    const prepare::Cancel cancel = [] {
        if (interrupted.load(std::memory_order_relaxed))
            prepare::fail("Preparation cancelled.");
    };
    QJsonObject result;
    auto log = [](const QString &message) {
        const auto bytes = message.toUtf8();
        std::fprintf(stderr, "%s\n", bytes.constData());
    };
    try {
        const int modes = int(parser.isSet("decrypt-elf")) + int(parser.isSet("extract-firmware")) +
                          int(parser.isSet("extract-update")) + int(parser.isSet("validate-firmware")) +
                          int(parser.isSet("inspect-iso")) + int(parser.isSet("extract-iso")) +
                          int(parser.isSet("import-game")) + int(parser.isSet("analyze-elf")) +
                          int(parser.isSet("extract-spu")) + int(parser.isSet("decode-spu")) +
                          int(parser.isSet("decode-ppu")) + int(parser.isSet("find-ppu-functions")) +
                          int(parser.isSet("lift-spu")) + int(parser.isSet("lift-spu-workloads")) +
                          int(parser.isSet("lift-spu-raw-jobs")) + int(parser.isSet("lift-multistream")) +
                          int(parser.isSet("generate-hle-nids"));
        prepare::require(modes == 1, "Select exactly one native import/validation operation.");
        const auto out = parser.value("out");
        if (parser.isSet("generate-hle-nids")) {
            prepare::require(!out.isEmpty(), "Select an HLE table output file.");
            result["handlers"] = prepare::writeHleNids(parser.value("generate-hle-nids"), out, cancel);
            result["path"] = out;
        } else if (parser.isSet("lift-multistream")) {
            prepare::require(!out.isEmpty() && parser.isSet("firmware"),
                             "Select the dev_flash input and MultiStream output directories.");
            result["modules"] =
                prepare::liftMultiStream(prepare::readFile(parser.value("lift-multistream")),
                                         parser.value("title-id"), parser.value("firmware"), out, cancel);
            result["path"] = out;
        } else if (parser.isSet("lift-spu-raw-jobs")) {
            prepare::require(!out.isEmpty(), "Select a raw SPU jobs output directory.");
            result["jobs"] = prepare::liftSpuRawJobs(prepare::readFile(parser.value("lift-spu-raw-jobs")),
                                                     parser.value("title-id"), out, cancel);
            result["path"] = out;
        } else if (parser.isSet("lift-spu-workloads")) {
            prepare::require(!out.isEmpty(), "Select an SPU workload output directory.");
            result["images"] = prepare::liftSpuWorkloads(parser.value("lift-spu-workloads"), out,
                                                         parser.value("spu-title"), cancel);
            result["path"] = out;
        } else if (parser.isSet("lift-spu")) {
            prepare::require(!out.isEmpty(), "Select an SPU output directory.");
            const auto elf = prepare::readFile(parser.value("lift-spu"));
            const auto instructions = prepare::decodeSpuText(elf, cancel);
            QVector<prepare::SpuBound> bounds;
            if (parser.isSet("spu-bounds")) {
                const auto plan = QJsonDocument::fromJson(prepare::readFile(parser.value("spu-bounds")));
                prepare::require(plan.isArray() && !plan.array().isEmpty(),
                                 "Expected an array of SPU bounds.");
                for (const auto &entry : plan.array()) {
                    const auto pair = entry.toArray();
                    prepare::require(pair.size() == 2, "Expected a start/end pair.");
                    bounds.append({pair[0].toInt(-1), pair[1].toInt(-1)});
                }
            } else {
                bounds = prepare::findSpuBounds(elf, instructions, cancel);
            }
            prepare::SpuLiftOptions options;
            options.prefix = parser.value("symbol-prefix");
            options.trace = parser.isSet("trace-spu");
            const auto lifted = prepare::liftSpu(instructions, bounds, options, cancel);
            prepare::atomicWrite(out, out + "/spu_recomp.h", lifted.header, cancel);
            prepare::atomicWrite(out, out + "/spu_recomp.c", lifted.source, cancel);
            result["report"] = lifted.report;
            result["path"] = out;
        } else if (parser.isSet("find-ppu-functions")) {
            prepare::require(!out.isEmpty(), "Select a PPU function output file.");
            const prepare::ElfImage elf(prepare::readFile(parser.value("find-ppu-functions")));
            const auto bounds =
                prepare::findPpuFunctions(elf, prepare::analyzePpu(elf, cancel).functions, cancel);
            prepare::atomicWrite(QFileInfo(out).absolutePath(), out, QJsonDocument(bounds).toJson(), cancel);
            result["functions"] = bounds.size();
            result["path"] = out;
        } else if (parser.isSet("decode-ppu")) {
            prepare::require(!out.isEmpty(), "Select a PPU instruction output file.");
            bool validBase = false;
            const auto base = parser.value("ppu-base").toUInt(&validBase, 0);
            const auto raw = prepare::readFile(parser.value("decode-ppu"));
            prepare::require(validBase && base % 4 == 0 && raw.size() % 4 == 0 &&
                                 quint64(base) + quint64(raw.size()) <= 0x100000000ULL,
                             "Invalid PPU instruction extent.");
            QByteArray tsv;
            for (qsizetype offset = 0; offset < raw.size(); offset += 4) {
                if (offset % 4096 == 0)
                    prepare::cancelled(cancel);
                const auto insn = prepare::decodePpu(prepare::be32(raw, offset), base + offset);
                tsv += insn.mnemonic.toUtf8() + '\t' + insn.operands.toUtf8() + '\t' + insn.comment.toUtf8() +
                       '\n';
            }
            prepare::atomicWrite(QFileInfo(out).absolutePath(), out, tsv, cancel);
            result["instructions"] = raw.size() / 4;
            result["path"] = out;
        } else if (parser.isSet("decode-spu")) {
            QJsonArray instructions;
            for (const auto &instruction :
                 prepare::decodeSpuImage(prepare::readFile(parser.value("decode-spu")), cancel))
                instructions.append(instruction.json());
            result["instructions"] = instructions;
        } else if (parser.isSet("extract-spu")) {
            prepare::require(!out.isEmpty(), "Select an SPU image output directory.");
            result["images"] =
                prepare::extractSpuImages(prepare::readFile(parser.value("extract-spu")), out, cancel);
            result["path"] = out;
        } else if (parser.isSet("analyze-elf")) {
            prepare::require(!out.isEmpty(), "Select an analysis output directory.");
            const auto input = parser.value("analyze-elf");
            const auto analysis = prepare::analyzePpu(prepare::ElfImage(prepare::readFile(input)), cancel);
            prepare::writePpuAnalysis(analysis, out, QFileInfo(input).completeBaseName(), cancel);
            result["loader"] = analysis.loader;
            result["imports"] = analysis.imports.size();
            result["path"] = out;
        } else if (parser.isSet("import-game")) {
            prepare::require(!out.isEmpty() && parser.isSet("firmware"),
                             "Select --firmware and --out for native import.");
            prepare::Inputs inputs;
            inputs.game = QFileInfo(parser.value("import-game")).absoluteFilePath();
            inputs.firmware = QFileInfo(parser.value("firmware")).absoluteFilePath();
            if (parser.isSet("keys"))
                inputs.keys = QFileInfo(parser.value("keys")).absoluteFilePath();
            if (parser.isSet("update"))
                inputs.update = QFileInfo(parser.value("update")).absoluteFilePath();
            result["state"] = prepare::importSession(inputs, out, parser.isSet("resume"), log, cancel);
            result["path"] = QFileInfo(out).absoluteFilePath();
        } else if (parser.isSet("inspect-iso") || parser.isSet("extract-iso")) {
            const bool extract = parser.isSet("extract-iso");
            prepare::DiscImage image(parser.value(extract ? "extract-iso" : "inspect-iso"));
            const auto plan = image.inspect(cancel);
            result["title_id"] = plan.title;
            result["format"] = plan.format;
            result["files"] = plan.files.size();
            result["bytes"] = qint64(plan.bytes);
            if (extract) {
                prepare::require(!out.isEmpty(), "Select an output path.");
                image.extract(plan, out, log, cancel);
                result["path"] = out;
            }
        } else if (parser.isSet("validate-firmware"))
            prepare::validateFirmware(parser.value("validate-firmware"));
        else {
            prepare::require(!out.isEmpty(), "Select an output path.");
            if (parser.isSet("decrypt-elf")) {
                const auto bytes = prepare::decryptElf(
                    parser.value("decrypt-elf"), prepare::KeyStore::fromFile(parser.value("keys")), cancel);
                // Never publish an unrecognized executable into a prepared workspace.
                const auto digest =
                    QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
                result["family"] = prepare::identify(parser.value("title-id"), digest);
                prepare::atomicWrite(QFileInfo(out).absolutePath(), out, bytes, cancel);
                result["sha256"] = digest;
            } else if (parser.isSet("extract-firmware"))
                prepare::extractFirmware(parser.value("extract-firmware"), out,
                                         prepare::KeyStore::fromFile(parser.value("keys")), log, cancel);
            else
                prepare::extractUpdate(parser.value("extract-update"), out, parser.value("title-id"), log,
                                       cancel);
            result["path"] = out;
        }
        result["ok"] = true;
    } catch (const std::exception &error) {
        result["ok"] = false;
        result["error"] = QString::fromUtf8(error.what());
    }
    const auto bytes = QJsonDocument(result).toJson();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    return result["ok"].toBool() ? 0 : interrupted.load(std::memory_order_relaxed) ? 130 : 1;
}
