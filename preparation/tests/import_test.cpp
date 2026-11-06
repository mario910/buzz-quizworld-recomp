#include "disc.h"
#include "elf.h"
#include "import.h"
#include "ppu_scalar.h"
#include "session.h"
#include "spu_decode.h"
#include "spu_images.h"
#include "spu_lift.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace prepare;
class ImportTest final : public QObject {
    Q_OBJECT
    QString fixture(const QString &name) const {
        return QTest::qFindTestData("fixtures/" + name, __FILE__, __LINE__);
    }
    KeyStore keys() const { return KeyStore::fromFile(fixture("test-keys.json")); }
    void write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
  private slots:
    void ppuScalarPreservesTocAndRecordForms() {
        QCOMPARE(lowerPpuScalar(decodePpu(0xe8410028), 0x8759d8),
                 "ctx->gpr[2] = 0x008759D8ULL; /*TOCFIX ld r2,N(r1)*/");
        QCOMPARE(lowerPpuScalar(decodePpu(0xe8410028)), "ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);");
        const auto integer = lowerPpuScalar(decodePpu(0x7c642e15));
        QVERIFY(integer.contains("const uint64_t _a = ctx->gpr[4], _b = ctx->gpr[5]"));
        QVERIFY(integer.contains("_ov << 30"));
        QVERIFY(integer.indexOf("_ov << 30") < integer.indexOf("_c << 28"));
        QCOMPARE(lowerPpuScalar(decodePpu(0xfc22182b)),
                 "ctx->fpr[1] = ppu_fadd(ctx->fpr[2], ctx->fpr[3]); "
                 "ctx->cr = (ctx->cr & ~0x0F000000u) | ((ctx->fpscr >> 4) & 0x0F000000u);");
        QVERIFY(lowerPpuScalar(decodePpu(0x4e800020)).isEmpty());
    }

    void spuLiftPreservesCallReturnAndStopSemantics() {
        const QVector<SpuInstruction> instructions{
            decodeSpu(0x33000184, 0), // brsl r4, 0xC
            decodeSpu(0x00000110, 4), // Resuming LV2 stop
            decodeSpu(0x35000000, 8), // bi r0
            decodeSpu(0x35000200, 12) // bi r4
        };
        SpuLiftOptions options;
        options.prefix = "probe";
        const auto lifted = liftSpu(instructions, {{0, 12}, {12, 16}}, options);
        QCOMPARE(lifted.report.value("call_targets").toArray(), QJsonArray{12});
        QVERIFY(lifted.source.contains("ctx->host_depth++; probe_spu_func_0000000C(ctx);"));
        QVERIFY(lifted.source.contains("spu_drain_call(ctx, 0x4);"));
        QVERIFY(lifted.source.contains("if (ctx->status != SPU_STATUS_RUNNING) return;"));
        QVERIFY(lifted.source.contains("SPU_RET_REG(ctx, 4);"));
        QVERIFY(lifted.source.contains("SPU_RET(ctx);"));
        options.forceIndirect.insert(12);
        const auto forced = liftSpu(instructions, {{0, 12}, {12, 16}}, options);
        QVERIFY(forced.source.contains("ctx->pc = ctx->gpr[4]._u32[0];"));
    }

    void spuDecoderMatchesSdkAcrossEveryOpcodePrefix() {
        quint32 state = 0x51c0dec0;
        auto word = [&] {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };
        auto hex = [](quint32 value) {
            return QByteArray::number(value, 16).toUpper().rightJustified(8, '0');
        };
        QCryptographicHash digest(QCryptographicHash::Sha256);
        for (quint32 op = 0; op < 2048; ++op)
            for (int i = 0; i < 8; ++i) {
                const auto raw = (op << 21) | (word() & 0x1fffff), address = word() & 0x3fffc;
                const auto decoded = decodeSpu(raw, address);
                digest.addData(hex(raw) + ':' + hex(address) + ':' + decoded.mnemonic.toUtf8() + ':' +
                               decoded.operands.toUtf8() + '\n');
            }
        QCOMPARE(digest.result().toHex(), readFile(fixture("spu-decode.sha256")).trimmed());
    }
    void extractsSpuImagesWithReferenceExtentsAndFingerprints() {
        QTemporaryDir temp;
        const auto bytes = readFile(fixture("spu-container.fixture"));
        const auto expected = QJsonDocument::fromJson(readFile(fixture("spu-images.json"))).array();
        const auto actual = extractSpuImages(bytes, temp.path());
        QCOMPARE(actual, expected);
        QCOMPARE(actual.size(), 2);
        for (const auto &entry : actual) {
            const auto object = entry.toObject();
            const auto file = temp.filePath(object["name"].toString());
            QCOMPARE(readFile(file), part(bytes, object["offset"].toInt(), object["size"].toInt()));
            QCOMPARE(fileHash(file), object["sha256"].toString());
        }
        QVERIFY_EXCEPTION_THROWN(findSpuImages(bytes, [] { fail("cancelled"); }), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(validateSpuImage(bytes, bytes.size() - 4), std::runtime_error);
    }
    void ppuLoaderMatchesReferenceWithOpdAndFirmwareImports() {
        const auto input = readFile(fixture("ppu-image.fixture"));
        const auto result = analyzePpu(ElfImage(input));
        const auto reference = QJsonDocument::fromJson(readFile(fixture("ppu-analysis.json"))).object();
        QCOMPARE(result.functions, reference["functions"].toArray());
        QCOMPARE(result.image, reference["image"].toArray());
        QCOMPARE(result.imports, reference["imports"].toArray());
        QCOMPARE(result.loader, reference["loader"].toObject());
        QCOMPARE(result.imports.size(), 2);
        QCOMPARE(result.functions.size(), 2);
        QVERIFY_EXCEPTION_THROWN(ElfImage(input.left(88)), std::runtime_error);
        auto invalid = input;
        qToBigEndian<quint64>(0xffffffffffffffffull, invalid.data() + 32);
        QVERIFY_EXCEPTION_THROWN(ElfImage{invalid}, std::runtime_error);
    }
    void completeNativeEncryptedImportAndResumeAfterPublication() {
        QTemporaryDir temp;
        const auto game = temp.filePath("game");
        QVERIFY(QDir().mkpath(game + "/PS3_GAME/USRDIR"));
        write(game + "/PS3_GAME/PARAM.SFO", readFile(fixture("sfo.fixture")));
        write(game + "/PS3_GAME/USRDIR/EBOOT.BIN", "unselected disc");
        const auto firmware = temp.filePath("firmware.pup"), update = temp.filePath("update.pkg");
        write(firmware, readFile(fixture("firmware.fixture")));
        write(update, readFile(fixture("update-certified.fixture")));
        Inputs inputs;
        inputs.game = game;
        inputs.firmware = firmware;
        inputs.keys = fixture("test-keys.json");
        inputs.update = update;
        const auto expected = readFile(fixture("npdrm-expected.fixture"));
        const auto digest =
            QString::fromLatin1(QCryptographicHash::hash(expected, QCryptographicHash::Sha256).toHex());
        ImportHooks hooks;
        hooks.firmware = [](const QString &path) {
            require(readFile(path + "/data/font/test.TTF") == "owned font fixture", "bad firmware");
        };
        hooks.profile = [&](const QString &title, const QString &actual) {
            require(title == "BCES00647" && actual == digest, "unrecognized ELF");
            return QString("owned fixture profile");
        };
        const auto session = temp.filePath("session");
        auto state = importSession(inputs, session, false, {}, {}, hooks);
        QCOMPARE(readFile(session + "/executable/EBOOT.elf"), expected);
        QCOMPARE(state["done"].toArray().size(), 3);
        QCOMPARE(importSession(inputs, session, true, {}, {}, hooks)["done"], state["done"]);
        // Simulate a process ending after the firmware directory was published
        // but before its completion was saved in the ledger.
        auto done = state["done"].toArray();
        done.removeAt(0);
        state["done"] = done;
        auto outputs = state["outputs"].toObject();
        outputs.remove("install firmware");
        state["outputs"] = outputs;
        write(session + "/state.json", QJsonDocument(state).toJson());
        const auto resumed = importSession(inputs, session, true, {}, {}, hooks);
        QCOMPARE(resumed["done"].toArray().size(), 3);
        QCOMPARE(readFile(session + "/executable/EBOOT.elf"), expected);
    }
    void nativeSessionResumesAndRejectsChangedInputsAndOutputs() {
        QTemporaryDir temp;
        const auto game = temp.filePath("game"), flash = temp.filePath("flash");
        QVERIFY(QDir().mkpath(game + "/PS3_GAME/USRDIR"));
        QVERIFY(QDir().mkpath(flash + "/sys/external"));
        write(game + "/PS3_GAME/PARAM.SFO", readFile(fixture("sfo.fixture")));
        write(game + "/PS3_GAME/USRDIR/EBOOT.BIN", readFile(fixture("npdrm.fixture")));
        write(flash + "/sys/external/flashATRAC.pic", "owned module");
        Inputs input;
        input.game = game;
        input.firmware = flash;
        input.keys = fixture("test-keys.json");
        const auto expected = readFile(fixture("npdrm-expected.fixture"));
        const auto expectedHash =
            QString::fromLatin1(QCryptographicHash::hash(expected, QCryptographicHash::Sha256).toHex());
        ImportHooks hooks;
        hooks.firmware = [](const QString &) {};
        hooks.profile = [&](const QString &title, const QString &digest) {
            require(title == "BCES00647" && digest == expectedHash, "unsupported executable");
            return QString("owned test profile");
        };
        const auto session = temp.filePath("session");
        const auto state = importSession(input, session, false, {}, {}, hooks);
        QVERIFY(state["import_complete"].toBool());
        QCOMPARE(readFile(session + "/executable/EBOOT.elf"), expected);
        QCOMPARE(importSession(input, session, true, {}, {}, hooks)["done"], state["done"]);
        QVERIFY_EXCEPTION_THROWN(importSession(input, session, false, {}, {}, hooks), std::runtime_error);
        write(session + "/executable/EBOOT.elf", "changed");
        QVERIFY_EXCEPTION_THROWN(importSession(input, session, true, {}, {}, hooks), std::runtime_error);
        QCOMPARE(readFile(session + "/executable/EBOOT.elf"), QByteArray("changed"));
        const auto another = temp.filePath("another");
        importSession(input, another, false, {}, {}, hooks);
        write(game + "/PS3_GAME/USRDIR/EBOOT.BIN", "changed source");
        QVERIFY_EXCEPTION_THROWN(importSession(input, another, true, {}, {}, hooks), std::runtime_error);
        QVERIFY(!QFileInfo::exists(temp.filePath("last-ready.json")));
    }
    void nativeSessionImportsPupAndUpdateWithoutPublishingUnrecognizedExecutable() {
        QTemporaryDir temp;
        const auto game = temp.filePath("game");
        QVERIFY(QDir().mkpath(game + "/PS3_GAME/USRDIR"));
        write(game + "/PS3_GAME/PARAM.SFO", readFile(fixture("sfo.fixture")));
        write(game + "/PS3_GAME/USRDIR/EBOOT.BIN", "unselected disc");
        const auto firmware = temp.filePath("firmware.pup"), update = temp.filePath("update.pkg");
        write(firmware, readFile(fixture("firmware.fixture")));
        write(update, readFile(fixture("update-0.fixture")));
        Inputs input;
        input.game = game;
        input.firmware = firmware;
        input.keys = fixture("test-keys.json");
        input.update = update;
        ImportHooks hooks;
        hooks.firmware = [](const QString &) {};
        const auto session = temp.filePath("session");
        QVERIFY_EXCEPTION_THROWN(importSession(input, session, false, {}, {}, hooks), std::runtime_error);
        QVERIFY(QFileInfo::exists(session + "/import/dev_flash/data/font/test.TTF"));
        QVERIFY(!QFileInfo::exists(session + "/executable/EBOOT.elf"));
        QVERIFY(QFileInfo::exists(session + "/state.json"));
        QVERIFY_EXCEPTION_THROWN(importSession(input, session, true, {}, {}, hooks), std::runtime_error);
    }
    void readsAndExtractsIsoBridgeAndJoliet() {
        QTemporaryDir temp;
        for (bool udf : {false, true})
            for (bool joliet : {false, true}) {
                const auto name = QString("iso-%1-%2").arg(int(udf)).arg(int(joliet));
                DiscImage image(fixture(name + ".fixture"));
                const auto plan = image.inspect();
                QCOMPARE(plan.title, QString("BCES00647"));
                QCOMPARE(plan.files.size(), 2);
                QCOMPARE(plan.format, joliet ? QString("Joliet") : QString("ISO9660"));
                const auto output = temp.filePath(name);
                image.extract(plan, output);
                QCOMPARE(readFile(output + "/PS3_GAME/PARAM.SFO"), readFile(fixture("sfo.fixture")));
                QCOMPARE(readFile(output + "/PS3_GAME/USRDIR/EBOOT.BIN"), QByteArray("SCE\0synthetic", 13));
                QVERIFY_EXCEPTION_THROWN(image.extract(plan, output), std::runtime_error);
            }
        QVERIFY_EXCEPTION_THROWN(DiscImage(fixture("iso-wrong-title.fixture")).inspect(), std::runtime_error);
        DiscImage image(fixture("iso-0-0.fixture"));
        QVERIFY_EXCEPTION_THROWN(image.inspect([] { fail("cancelled"); }), std::runtime_error);
        const auto plan = image.inspect();
        const auto cancelledOutput = temp.filePath("cancelled");
        QVERIFY_EXCEPTION_THROWN(image.extract(plan, cancelledOutput, {}, [] { fail("cancelled"); }),
                                 std::runtime_error);
        QVERIFY(!QFileInfo::exists(cancelledOutput));
    }
    void certifiedFilesMatchFrozenReference() {
        for (bool compressed : {false, true})
            for (int digest : {2, 3, 6}) {
                const auto name = QString("self-%1-%2").arg(int(compressed)).arg(digest);
                const auto bytes = readFile(fixture(name + ".fixture"));
                QCOMPARE(CertifiedFile(bytes, keys()).elf(), readFile(fixture(name + "-expected.fixture")));
                auto bad = bytes;
                bad[bad.size() - 1] = char(quint8(bad.back()) ^ 1);
                QVERIFY_EXCEPTION_THROWN(CertifiedFile(bad, keys()).elf(), std::runtime_error);
                for (int size : {0, 4, 31, 128})
                    QVERIFY_EXCEPTION_THROWN(CertifiedFile(bytes.left(size), keys()).elf(),
                                             std::runtime_error);
            }
        for (const auto &name : {QString("npdrm"), QString("unsigned")})
            QCOMPARE(CertifiedFile(readFile(fixture(name + ".fixture")), keys()).elf(),
                     readFile(fixture(name + "-expected.fixture")));
    }
    void missingKeysCancellationAndRedactedErrors() {
        const auto bytes = readFile(fixture("self-1-2.fixture"));
        QVERIFY_EXCEPTION_THROWN(CertifiedFile(bytes, {}), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(CertifiedFile(bytes, keys(), [] { fail("cancelled"); }), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(CertifiedFile(readFile(fixture("npdrm.fixture")), {}), std::runtime_error);
        const QByteArray secret = "must-not-be-in-error-message";
        auto doc = QJsonDocument::fromJson(readFile(fixture("test-keys.json"))).object();
        doc["named"] = QJsonObject{{"secret", QString::fromLatin1(secret)}};
        try {
            KeyStore::parse(QJsonDocument(doc).toJson());
            QFAIL("invalid key file accepted");
        } catch (const std::exception &error) {
            QVERIFY(!QByteArray(error.what()).contains(secret));
        }
        QByteArray text;
        for (int i = 0; i < 2; ++i)
            text += "[pkg]\ntype=PKG\nrevision=" + QByteArray::number(i, 16) +
                    "\nerk=" + keys().roots[0].key.toHex() + "\nriv=" + keys().roots[0].iv.toHex() + "\n";
        QCOMPARE(KeyStore::parse(text).roots.size(), 2);
    }
    void authenticatesPupAndPublishesValidatedFirmware() {
        QTemporaryDir temp;
        const auto home = temp.filePath("home");
        int checked = 0;
        auto validate = [&](const QString &path) {
            ++checked;
            QCOMPARE(readFile(QDir(path).filePath("data/font/test.TTF")), QByteArray("owned font fixture"));
            QCOMPARE(readFile(QDir(path).filePath("sys/external/flashATRAC.pic")),
                     QByteArray("owned module fixture"));
        };
        extractFirmware(fixture("firmware.fixture"), home, keys(), {}, {}, validate);
        extractFirmware(fixture("firmware.fixture"), home, keys(), {}, {}, validate);
        QCOMPARE(checked, 2);
        write(home + "/dev_flash/data/font/test.TTF", "modified");
        QVERIFY_EXCEPTION_THROWN(extractFirmware(fixture("firmware.fixture"), home, keys(), {}, {}, validate),
                                 std::runtime_error);
        QCOMPARE(readFile(home + "/dev_flash/data/font/test.TTF"), QByteArray("modified"));
        const auto invalid = temp.filePath("invalid");
        QVERIFY_EXCEPTION_THROWN(extractFirmware(fixture("firmware.fixture"), invalid, keys(), {}, {},
                                                 [](const QString &) { fail("invalid firmware"); }),
                                 std::runtime_error);
        QVERIFY(!QFileInfo::exists(invalid + "/dev_flash"));
        auto pup = readFile(fixture("firmware.fixture"));
        pup[pup.size() - 1] = char(quint8(pup.back()) ^ 1);
        const auto corrupt = temp.filePath("corrupt.fixture");
        write(corrupt, pup);
        QVERIFY_EXCEPTION_THROWN(firmwareArchive(corrupt, keys()), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(firmwareArchive(fixture("firmware.fixture"), {}), std::runtime_error);
    }
    void rejectsUnsafeArchivePathsAndMalformedTar() {
        for (const auto &name :
             {"unsafe-9.fixture", "unsafe-19.fixture", "link-tar.fixture", "duplicate-tar.fixture"})
            QVERIFY_EXCEPTION_THROWN(readTar(readFile(fixture(name)), "dev_flash"), std::runtime_error);
        const auto valid = readFile(fixture("firmware-tar.fixture"));
        QCOMPARE(readTar(valid, "dev_flash").size(), 2);
        auto bad = valid;
        bad[0] = 'x';
        QVERIFY_EXCEPTION_THROWN(readTar(bad), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(readTar(valid.left(511)), std::runtime_error);
        for (const auto &name : {"../escape", "/absolute", "C:/escape", "dev_flash/a:stream", "dev_flash/CON",
                                 "dev_flash/a.", "dev_flash//a"})
            QVERIFY_EXCEPTION_THROWN(archivePath(name), std::runtime_error);
    }
    void extractsRetailAndDebugUpdateAcrossUnalignedChunks() {
        QTemporaryDir temp;
        for (int debug : {0, 1}) {
            const auto home = temp.filePath(QString::number(debug));
            const auto source = fixture(QString("update-%1.fixture").arg(debug));
            const auto before = fileHash(source);
            extractUpdate(source, home, "BCES00647");
            QCOMPARE(readFile(home + "/PARAM.SFO"), readFile(fixture("sfo.fixture")));
            QCOMPARE(readFile(home + "/USRDIR/EBOOT.BIN"), QByteArray("owned update executable"));
            QByteArray expected;
            for (int i = 0; i < 255; ++i)
                expected += char(i);
            QCOMPARE(readFile(home + "/USRDIR/Patched/test.dat"), expected.repeated(5000));
            QCOMPARE(fileHash(source), before);
            const auto wrong = temp.filePath("wrong");
            QVERIFY_EXCEPTION_THROWN(extractUpdate(source, wrong, "BCUS98209"), std::runtime_error);
            QVERIFY(!QFileInfo::exists(wrong));
        }
    }
    void nativeCryptoVectorsLimitsAndAtomicCancellation() {
        const auto key = QByteArray::fromHex("2b7e151628aed2a6abf7158809cf4f3c"),
                   iv = QByteArray::fromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
        const auto plain = QByteArray::fromHex("6bc1bee22e409f96e93d7e117393172a");
        QCOMPARE(aesCtr(plain, key, iv), QByteArray::fromHex("874d6191b620e3261bef6864990db6ce"));
        const auto zipped = QByteArray::fromHex("789c4b4c4a0600024d0127");
        QCOMPARE(inflateBounded(zipped, 3), QByteArray("abc"));
        QVERIFY_EXCEPTION_THROWN(inflateBounded(zipped, 2), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(inflateBounded(zipped.left(zipped.size() - 1), 100), std::runtime_error);
        QTemporaryDir temp;
        const auto output = temp.filePath("kept");
        write(output, "previous");
        QVERIFY_EXCEPTION_THROWN(atomicWrite(temp.path(), output, "new", [] { fail("cancelled"); }),
                                 std::runtime_error);
        QCOMPARE(readFile(output), QByteArray("previous"));
#ifndef Q_OS_WIN
        QTemporaryDir outside;
        QVERIFY(QFile::link(outside.path(), temp.filePath("linked")));
        QVERIFY_EXCEPTION_THROWN(atomicWrite(temp.path(), temp.filePath("linked/file"), "unsafe"),
                                 std::runtime_error);
        QVERIFY(QDir(outside.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
#endif
    }
};
QTEST_GUILESS_MAIN(ImportTest)
#include "import_test.moc"
