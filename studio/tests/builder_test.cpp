#include "buzz_import.h"
#include "kip_import.h"
#include "legacy_import.h"
#include "pack_builder.h"
#include "ps3_source.h"
#include "project_media.h"
#include "ps4_pkg.h"
#include "languages.h"
#include "pack_codec.h"
#include <zlib.h>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cmath>
#include <functional>

using namespace studio::pack;
class BuilderTest final : public QObject {
    Q_OBJECT
    QString fixture(const char *name) const {
        return QTest::qFindTestData(QString("fixtures/") + name, __FILE__, __LINE__);
    }
    QByteArray bytes(const QString &path) const {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return f.readAll();
    }
    bool write(const QString &path, const QByteArray &data) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    }
    studio::BuildRequest request(const QString &output) const {
        studio::BuildRequest r;
        r.document = studio::loadDocument(fixture("build-source-en.json")).document;
        r.templatePath = fixture("build-template.pak");
        r.outputDirectory = output;
        r.ffmpeg = studio::defaultFfmpeg();
        return r;
    }
    QByteArray riff(const QByteArray &rsf) const {
        QByteArray fmt;
        put(fmt, 0x270, 2);
        put(fmt, 1, 2);
        put(fmt, 48000, 4);
        put(fmt, 152 * 48000 / 1024, 4);
        put(fmt, 152, 2);
        put(fmt, 0, 2);
        put(fmt, 14, 2);
        put(fmt, 1, 2);
        put(fmt, 1024, 4);
        for (int v : {0, 0, 1, 0})
            put(fmt, v, 2);
        auto body = QByteArray("WAVEfmt ");
        put(body, fmt.size(), 4);
        body += fmt;
        body += "data";
        put(body, rsf.size() - 32, 4);
        body += rsf.mid(32);
        auto result = QByteArray("RIFF");
        put(result, body.size(), 4);
        return result + body;
    }
    // Minimal ISO 9660 image holding the given files (nested directories allowed).
    static QByteArray isoImage(const QMap<QString, QByteArray> &files) {
        QVector<QByteArray> sectors(18, QByteArray(2048, '\0'));
        auto record = [](quint32 extent, quint32 size, bool directory, const QByteArray &name) {
            QByteArray r(33, '\0');
            qToLittleEndian<quint32>(extent, r.data() + 2);
            qToBigEndian<quint32>(extent, r.data() + 6);
            qToLittleEndian<quint32>(size, r.data() + 10);
            qToBigEndian<quint32>(size, r.data() + 14);
            r[25] = directory ? 2 : 0;
            r[28] = 1;
            r[31] = 1;
            r[32] = char(name.size());
            r += name;
            if (r.size() % 2)
                r += '\0';
            r[0] = char(r.size());
            return r;
        };
        std::function<QPair<quint32, quint32>(const QString &)> directory = [&](const QString &prefix) {
            const auto self = quint32(sectors.size());
            sectors << QByteArray(2048, '\0');
            QByteArray body = record(self, 2048, true, QByteArray(1, '\0')) + record(self, 2048, true, QByteArray(1, '\1'));
            QSet<QString> children;
            for (auto it = files.cbegin(); it != files.cend(); ++it) {
                if (!it.key().startsWith(prefix))
                    continue;
                const auto rest = it.key().mid(prefix.size());
                if (rest.contains('/')) {
                    const auto child = rest.section('/', 0, 0);
                    if (!children.contains(child)) {
                        children.insert(child);
                        const auto sub = directory(prefix + child + '/');
                        body += record(sub.first, sub.second, true, child.toLatin1());
                    }
                } else {
                    const auto start = quint32(sectors.size());
                    for (qint64 at = 0; at < std::max<qint64>(1, it.value().size()); at += 2048)
                        sectors << it.value().mid(at, 2048).leftJustified(2048, '\0');
                    body += record(start, quint32(it.value().size()), false, (rest + ";1").toLatin1());
                }
            }
            sectors[self] = body.leftJustified(2048, '\0');
            return qMakePair(self, quint32(2048));
        };
        const auto root = directory({});
        QByteArray pvd(2048, '\0');
        pvd[0] = 1;
        pvd.replace(1, 5, "CD001");
        pvd[6] = 1;
        pvd.replace(156, 34, record(root.first, root.second, true, QByteArray(1, '\0')));
        sectors[16] = pvd;
        sectors[17][0] = char(255);
        sectors[17].replace(1, 5, "CD001");
        QByteArray out;
        for (const auto &s : sectors)
            out += s;
        return out;
    }
    // CISO v1 with raw-deflate blocks; block 17 is stored to cover both kinds.
    static QByteArray csoImage(const QByteArray &iso) {
        const int blocks = int(iso.size() / 2048);
        QByteArray out("CISO", 4);
        put(out, 0, 4);
        put(out, quint64(iso.size()), 8);
        put(out, 2048, 4);
        out += char(1);
        out += char(0);
        put(out, 0, 2);
        const int indexAt = out.size();
        out += QByteArray((blocks + 1) * 4, '\0');
        for (int i = 0; i < blocks; ++i) {
            const auto block = iso.mid(qint64(i) * 2048, 2048);
            quint32 entry = quint32(out.size());
            if (i == 17) {
                entry |= 0x80000000u;
                out += block;
            } else {
                QByteArray packed(4096, '\0');
                z_stream z{};
                deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
                z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(block.constData()));
                z.avail_in = uInt(block.size());
                z.next_out = reinterpret_cast<Bytef *>(packed.data());
                z.avail_out = uInt(packed.size());
                deflate(&z, Z_FINISH);
                packed.truncate(packed.size() - int(z.avail_out));
                deflateEnd(&z);
                out += packed;
            }
            qToLittleEndian<quint32>(entry, out.data() + indexAt + i * 4);
        }
        qToLittleEndian<quint32>(quint32(out.size()), out.data() + indexAt + blocks * 4);
        return out;
    }
    // PS2 round record: presentation, media, media 2, category byte, 1-based strings.
    static QByteArray ps2Record(int media, int category, int first) {
        QByteArray r;
        for (int w = 0; w < 14; ++w)
            put(r, w == 1 ? media : w == 2 ? 0x7fff : w >= 9 ? first + (w - 9) : 0, 2);
        r[8] = char(category);
        return r;
    }

    // One deflated entry in a ZIP (QuestionImages.bytes may compress its JPEGs).
    static QByteArray deflatedZip(const QString &name, const QByteArray &data) {
        QByteArray packed(int(compressBound(uLong(data.size()))) + 64, '\0');
        z_stream z{};
        deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
        z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
        z.avail_in = uInt(data.size());
        z.next_out = reinterpret_cast<Bytef *>(packed.data());
        z.avail_out = uInt(packed.size());
        deflate(&z, Z_FINISH);
        packed.truncate(packed.size() - int(z.avail_out));
        deflateEnd(&z);
        const auto n = name.toUtf8();
        const quint32 crc = studio::pack::crc32(data);
        QByteArray local("PK\3\4", 4);
        for (auto [v, bytes] : {std::pair<quint64, int>{20, 2}, {0, 2}, {8, 2}, {0, 4}, {crc, 4},
                                {quint64(packed.size()), 4}, {quint64(data.size()), 4}, {quint64(n.size()), 2}, {0, 2}})
            put(local, v, bytes);
        local += n + packed;
        QByteArray central("PK\1\2", 4);
        for (auto [v, bytes] : {std::pair<quint64, int>{20, 2}, {20, 2}, {0, 2}, {8, 2}, {0, 4}, {crc, 4},
                                {quint64(packed.size()), 4}, {quint64(data.size()), 4}, {quint64(n.size()), 2}, {0, 2},
                                {0, 2}, {0, 2}, {0, 2}, {0, 4}, {0, 4}})
            put(central, v, bytes);
        central += n;
        QByteArray end("PK\5\6", 4);
        for (auto [v, bytes] : {std::pair<quint64, int>{0, 2}, {0, 2}, {1, 2}, {1, 2}, {quint64(central.size()), 4},
                                {quint64(local.size()), 4}, {0, 2}})
            put(end, v, bytes);
        return local + central + end;
    }
    // Unity TextAsset as serialized in resources.assets.
    static QByteArray textAsset(const QByteArray &name, const QByteArray &text) {
        QByteArray out;
        put(out, name.size(), 4);
        out += name;
        while (out.size() % 4)
            out += '\0';
        put(out, text.size(), 4);
        out += text;
        while (out.size() % 4) // Unity aligns every field to 4 bytes.
            out += '\0';
        return out + QByteArray(4, '\0');
    }
    static QByteArray utf16Loc(const QStringList &lines) {
        const auto text = QString("String ID\tText\r\n" + lines.join("\r\n") + "\r\n");
        return QByteArray("\xff\xfe", 2) +
               QByteArray(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
    }

  private slots:
    void xtsMatchesReferenceVectors() {
        // IEEE 1619 vector 2 and a multi-block vector computed with OpenSSL.
        QCOMPARE(studio::ps4::xtsDecrypt(
                     QByteArray::fromHex("c454185e6a16936e39334038acef838bfb186fff7480adc4289382ecd6d394f0"),
                     QByteArray(16, '\x11'), QByteArray(16, '\x22'), 0x3333333333ull),
                 QByteArray(32, '\x44'));
        QByteArray key1, key2, plain;
        for (int i = 0; i < 16; ++i) {
            key1 += char(i);
            key2 += char(16 + i);
        }
        for (int i = 0; i < 128; ++i)
            plain += char(i);
        QCOMPARE(studio::ps4::xtsDecrypt(
                     QByteArray::fromHex("978ed82d1e4d10523642ba6fe755165cf5e55da991a81cdfed812846d9ac06c9004f52ced1f356bd"
                                         "0c7323ff0663379c872544e2fb3a56bfb39b033cddff57ef5f33a824b52686695f2d4da6eca972"
                                         "b058a44ee8555b9b358739b9f4662634b00eecd3b0076944d87cbbd773c9c95a517489459d0190"
                                         "0e2c9b0eedbbb4f66181"),
                     key1, key2, 0x0123456789abcdefull),
                 plain);
    }
    void readsPfsDirectoriesAndCompressedFiles() {
        // Unsigned, unencrypted PFS (block size 0x1000): root -> uroot -> plain and PFSC files.
        const qint64 bs = 0x1000;
        const QByteArray plain = QByteArray("plain file contents ").repeated(300);
        QByteArray logical;
        for (int i = 0; i < 2 * 0x1000; ++i)
            logical += char((i * 7) % 251);
        QByteArray pfsc(0x30, '\0');
        qToLittleEndian<qint32>(0x43534650, pfsc.data());
        qToLittleEndian<qint32>(0x1000, pfsc.data() + 12);
        qToLittleEndian<qint64>(0x1000, pfsc.data() + 16);
        qToLittleEndian<qint64>(0x30, pfsc.data() + 24);
        qToLittleEndian<qint64>(logical.size(), pfsc.data() + 40);
        const auto first = qCompress(logical.left(0x1000), 9).mid(4); // zlib stream
        QVector<qint64> map{0x30 + 3 * 8};
        map << map[0] + first.size() << map[0] + first.size() + 0x1000; // second block stored
        for (const auto value : map)
            put(pfsc, quint64(value), 8);
        pfsc += first + logical.mid(0x1000);
        auto blocksFor = [&](qint64 size) { return int((size + bs - 1) / bs); };
        QByteArray image(bs * 4, '\0');
        qToLittleEndian<qint64>(1, image.data());
        qToLittleEndian<qint64>(20130315, image.data() + 8);
        qToLittleEndian<qint32>(qint32(bs), image.data() + 0x20);
        qToLittleEndian<qint64>(4, image.data() + 0x30);
        qToLittleEndian<qint64>(1, image.data() + 0x40);
        const qint64 plainAt = 4, pfscAt = plainAt + blocksFor(plain.size());
        auto inode = [&](int index, quint32 flags, qint64 size, qint64 sizeC, quint32 blocks, qint32 start) {
            char *p = image.data() + bs + index * 0xa8;
            qToLittleEndian<quint32>(flags, p + 4);
            qToLittleEndian<qint64>(size, p + 8);
            qToLittleEndian<qint64>(sizeC, p + 16);
            qToLittleEndian<quint32>(blocks, p + 0x60);
            qToLittleEndian<qint32>(start, p + 0x64);
            for (int k = 1; k < 12; ++k)
                qToLittleEndian<qint32>(-1, p + 0x64 + 4 * k);
        };
        inode(0, 0, bs, bs, 1, 2);
        inode(1, 0, bs, bs, 1, 3);
        inode(2, 0, plain.size(), plain.size(), quint32(blocksFor(plain.size())), qint32(plainAt));
        inode(3, 1, pfsc.size(), logical.size(), quint32(blocksFor(pfsc.size())), qint32(pfscAt));
        auto entry = [](qint32 child, qint32 type, const QByteArray &name) {
            QByteArray e(16, '\0');
            qToLittleEndian<qint32>(child, e.data());
            qToLittleEndian<qint32>(type, e.data() + 4);
            qToLittleEndian<qint32>(qint32(name.size()), e.data() + 8);
            e += name;
            while (e.size() % 8)
                e += '\0';
            qToLittleEndian<qint32>(qint32(e.size()), e.data() + 12);
            return e;
        };
        const auto rootDir = entry(1, 3, "uroot");
        image.replace(2 * bs, rootDir.size(), rootDir);
        const auto dir = entry(2, 2, "plain.txt") + entry(3, 2, "packed.bin");
        image.replace(3 * bs, dir.size(), dir);
        image += plain.leftJustified(blocksFor(plain.size()) * bs, '\0');
        image += pfsc.leftJustified(blocksFor(pfsc.size()) * bs, '\0');
        studio::ps4::Pfs pfs(std::make_shared<studio::ps4::BytesReader>(image));
        QCOMPARE(pfs.files().keys(), QStringList({"uroot/packed.bin", "uroot/plain.txt"}));
        auto file = pfs.open("uroot/plain.txt");
        QCOMPARE(file->read(0, file->size()), plain);
        auto packed = pfs.open("uroot/packed.bin");
        QCOMPARE(packed->size(), logical.size());
        QCOMPARE(packed->read(0, packed->size()), logical);
        QCOMPARE(packed->read(0x0ff0, 0x20), logical.mid(0x0ff0, 0x20)); // Across the two blocks.
        QCOMPARE(pfs.fileSize("uroot/packed.bin"), logical.size());
        // Not a PFS at all.
        bool refused = false;
        try {
            studio::ps4::Pfs empty(std::make_shared<studio::ps4::BytesReader>(QByteArray(0x2000, '\0')));
        } catch (const std::exception &) {
            refused = true;
        }
        QVERIFY(refused);
    }
    void importsKnowledgeIsPowerFromAnExtractedFolder() {
        QTemporaryDir temp;
        const QDir root(temp.path());
        QVERIFY(root.mkpath("Media/StreamingAssets/Localisation"));
        QVERIFY(root.mkpath("Media/StreamingAssets/QuestionImages"));
        QVERIFY(root.mkpath("Media/StreamingAssets/Audio/GeneratedSoundBanks/PS4/Polish"));
        // 21-column trivia rows: id, text key, 4 answer keys, category path, ..., image, frame, tags, difficulty.
        auto row = [](const QString &id, const QString &key, const QString &image, const QString &difficulty) {
            QStringList r(21);
            r[0] = id;
            r[1] = key;
            for (int a = 0; a < 4; ++a)
                r[2 + a] = key + "_A" + QString::number(a + 1);
            r[6] = "Experience";
            r[7] = "ModernWorld";
            r[15] = image;
            r[18] = difficulty;
            return r.join('\t');
        };
        const auto trivia = QStringList{row("TRV00001", "TRV00001", "Pic1", "EASY"),
                                        row("TRV00002", "TRV00002", "", "EXPERT"),
                                        row("TRV00003", "TRV00003", "", "NORMAL")}
                                .join("\r\n");
        const auto local = row("LST00001pl", "LST00001", "", "NORMAL");
        QByteArray assets = QByteArray(64, '\x01');
        assets += textAsset("KIP_Trivia_QuestionText", trivia.toUtf8());
        assets += textAsset("KIP_Trivia_QuestionText_pl", local.toUtf8());
        assets += textAsset("KIP_Trivia_KillList_pl", "TRV00003");
        assets += textAsset("KIP_Finale_QuestionText", QByteArray());
        assets += textAsset("KIP_Sorting_QuestionText", "SRT1\tx\r\nSRT2\ty");
        QVERIFY(write(root.filePath("Media/resources.assets"), assets));
        QStringList pl, en;
        for (const auto &key : {"TRV00001", "TRV00002", "TRV00003", "LST00001"}) {
            pl << QString("%1\tPytanie %1?").arg(key);
            en << QString("%1\tQuestion %1?").arg(key);
            for (int a = 1; a <= 4; ++a) {
                pl << QString("%1_A%2\tOdpowiedź %2").arg(key).arg(a);
                en << QString("%1_A%2\tAnswer %2").arg(key).arg(a);
            }
        }
        QVERIFY(write(root.filePath("Media/StreamingAssets/Localisation/pl_text.loc"), utf16Loc(pl)));
        QVERIFY(write(root.filePath("Media/StreamingAssets/Localisation/en_text.loc"), utf16Loc(en)));
        const QByteArray jpeg = QByteArray("\xff\xd8\xff\xe0 fake jpeg ").repeated(40);
        QVERIFY(write(root.filePath("Media/StreamingAssets/QuestionImages/QuestionImages.bytes"),
                      deflatedZip("Pic1.jpg", jpeg)));
        QVERIFY(write(root.filePath("Media/StreamingAssets/Audio/GeneratedSoundBanks/PS4/Polish/pl_TRV00001.wem"),
                      "RIFF voice"));
        const auto assetsPath = root.filePath("Media/resources.assets");
        QVERIFY(studio::isKipSource(assetsPath));
        QCOMPARE(studio::kipLanguages(assetsPath).codes, QStringList({"en", "pl"}));
        const auto r = studio::importKip(assetsPath, "pl");
        QVERIFY2(r.ok(), qPrintable(r.error));
        // TRV00003 is on the Polish kill list; the local question is Polish only.
        QStringList texts;
        for (const auto &question : r.document.questions)
            texts << question.text;
        QVERIFY2(texts.size() == 3, qPrintable(texts.join(" | ") + " / " + r.warnings.join(" | ")));
        QCOMPARE(texts, QStringList({"Pytanie TRV00001?", "Pytanie TRV00002?", "Pytanie LST00001?"}));
        const auto &q = r.document.questions[0];
        QCOMPARE(q.text, QString("Pytanie TRV00001?"));
        QCOMPARE(q.answers, QStringList({"Odpowiedź 1", "Odpowiedź 2", "Odpowiedź 3", "Odpowiedź 4"}));
        QCOMPARE(q.correct, 0);
        QCOMPARE(q.difficulty, 1);
        QCOMPARE(q.source, QString("Knowledge Is Power"));
        QCOMPARE(bytes(q.image), jpeg);
        QCOMPARE(bytes(q.speech), QByteArray("RIFF voice"));
        QCOMPARE(r.document.questions[1].difficulty, 3);
        QVERIFY(r.document.questions[1].speech.isEmpty());
        QCOMPARE(r.document.questions[2].text, QString("Pytanie LST00001?"));
        QVERIFY(std::any_of(r.warnings.begin(), r.warnings.end(), [](const QString &w) { return w.contains("2"); }));
        // English keeps the killed question and has no local Polish one.
        const auto english = studio::importKip(assetsPath, "en");
        QVERIFY2(english.ok(), qPrintable(english.error));
        QCOMPARE(english.document.questions.size(), 3);
        QCOMPARE(english.document.questions[2].text, QString("Question TRV00003?"));
        // A PS3 package is refused with a hint.
        QVERIFY(write(temp.filePath("dlc.pkg"), QByteArray("\x7fPKG", 4) + QByteArray(0x1000, '\0')));
        QVERIFY(!studio::importKip(temp.filePath("dlc.pkg")).ok());
    }
    void decodesHuffmanTablesStoredOutOfWeightOrder() {
        // Quiz TV DLC tables (e.g. Comedy, PACK0022) list symbols unsorted; the
        // game sorts them by weight (stably) before building the tree.
        const QStringList strings{"Ala ma kota", "Kot ma Alę", "Zażółć gęślą jaźń"};
        const auto encoded = encodeHuffman(strings);
        QVector<QByteArray> entries;
        for (int p = 10; p + 6 <= encoded.header.size() - 2; p += 6)
            entries << encoded.header.mid(p, 6);
        const auto weight = [](const QByteArray &e) { return qFromBigEndian<quint32>(e.constData() + 2); };
        // Move the lightest symbols (in their order) behind all others: a stable
        // sort restores the encoder's order, building the tree in file order does not.
        const auto lightest = weight(entries.first());
        std::stable_partition(entries.begin(), entries.end(), [&](const QByteArray &e) { return weight(e) != lightest; });
        auto header = encoded.header.left(10);
        for (const auto &e : entries)
            header += e;
        header += encoded.header.right(2);
        QVERIFY(header != encoded.header);
        QCOMPARE(studio::decodeHuffman(header, encoded.stream), strings);
    }
    void importsPs2AndPspDiscsAndFiles() {
        QTemporaryDir temp;
        // PS2 Mega Quiz style round package (.rnd + Huffman text) inside POLROUND.PAK.
        const auto sport = encodeHuffman({"Ile graczy ma drużyna?", "11", "10", "9", "12", "Kto to jest?", "Adam",
                                          "Robert", "Kamil", "Piotr"});
        const auto compare = encodeHuffman({"Co jest większe?", "Słoń", "Mysz", "", ""});
        Entries round{{"qe_Sport.rnd", ps2Record(0x7fff, 22, 1) + ps2Record(5, 22, 6)},
                      {"qe_Sport.hdr", sport.header},
                      {"qe_Sport.hst", sport.stream},
                      {"qm_Sport.rnd", ps2Record(0x7fff, 22, 1)},
                      // Read first: the picture question without its media id must
                      // give way to the copy that keeps the picture reference.
                      {"qa_Sport.rnd", ps2Record(0x7fff, 22, 6)},
                      {"qa_Sport.hdr", sport.header},
                      {"qa_Sport.hst", sport.stream},
                      {"qm_Sport.hdr", sport.header},
                      {"qm_Sport.hst", sport.stream},
                      {"qcompare.rnd", ps2Record(0x7fff, 29, 1)},
                      {"qcompare.hdr", compare.header},
                      {"qcompare.hst", compare.stream}};
        writeArchive(temp.filePath("round.pkg"), round);
        QByteArray rri;
        put(rri, 10, 4);
        for (int id = 0; id < 10; ++id)
            for (int v = 0; v < 4; ++v)
                put(rri, v == 1 ? (id == 5 ? 9 : 0) : 0, 2);
        const QByteArray adt = "Id\tResource \tQuestion\tRight Answer\tWrong Answer\tWrong Answer\tWrong Answer\r\n"
                               "7\tnon-asset \tKto wyreżyserował film?\tA1\tB1\tC1\tD1\r\n";
        writeArchive(temp.filePath("POLROUND.PAK"),
                     {{"BG2/SK_POL/Rounds/POL/TEST_ROUND.pkg", bytes(temp.filePath("round.pkg"))},
                      {"BG2/SK_POL/Rounds/rri.dat", rri},
                      {"BHW/SK_POL/Rounds/POL/quickfire_hard.adt", adt},
                      {"BHW/SK_POL/Rounds/POL/toprank_easy.adt", adt}});
        const auto checkPs2 = [](const studio::LoadResult &r) {
            QVERIFY2(r.ok(), qPrintable(r.error));
            QCOMPARE(r.document.language, QString("pl"));
            QCOMPARE(r.document.questions.size(), 3);
            QCOMPARE(r.duplicates, 2);
            // qcompare and the toprank (put-in-order) table are not four-answer questions.
            QVERIFY(std::any_of(r.warnings.begin(), r.warnings.end(),
                                [](const QString &w) { return w.startsWith("Pominięto 2 pytań innych typów"); }));
            QMap<QString, studio::Question> byText;
            for (const auto &q : r.document.questions)
                byText[q.text] = q;
            const auto text = byText["Ile graczy ma drużyna?"];
            QCOMPARE(text.answers, QStringList({"11", "10", "9", "12"}));
            QCOMPARE(text.correct, 0);
            QCOMPARE(text.difficulty, 1);
            QCOMPARE(text.subjectId, 174);
            QCOMPARE(text.categoryId, 247);
            QVERIFY(!text.extra.contains("source_media"));
            QVERIFY(text.problems().isEmpty());
            const auto media = byText["Kto to jest?"].extra["source_media"].toArray()[0].toObject();
            QCOMPARE(media["asset"].toInt(), 5);
            QCOMPARE(media["frame"].toInt(), 9); // Not the Polish retail table: no correction.
            QVERIFY(!media.contains("frame_corrected"));
            const auto film = byText["Kto wyreżyserował film?"];
            QCOMPARE(film.difficulty, 3);
            QCOMPARE(film.subjectId, 199);
        };
        checkPs2(studio::importLegacy(temp.filePath("POLROUND.PAK")));
        QCOMPARE(studio::legacyLanguages(temp.filePath("POLROUND.PAK")).codes, QStringList{"pl"});
        // The same archive on PS2 discs: plain ISO and compressed CSO.
        const auto iso = isoImage({{"SYSTEM.CNF", "BOOT2 = cdrom0:\\SCES_546.90;1\n"},
                                   {"POL/POLROUND.PAK", bytes(temp.filePath("POLROUND.PAK"))}});
        QVERIFY(write(temp.filePath("ps2.iso"), iso));
        QVERIFY(write(temp.filePath("ps2.cso"), csoImage(iso)));
        for (const auto &image : {temp.filePath("ps2.iso"), temp.filePath("ps2.cso")}) {
            const auto r = studio::importLegacy(image, "pl");
            checkPs2(r);
            QCOMPARE(r.document.questions[0].source, QString("Buzz! Mega Quiz"));
        }

        // PSP Master Quiz: MERGED.QDB (24-byte records) + MERGED.TXT lines.
        QVERIFY(QDir().mkpath(temp.filePath("psp/POLISH")));
        QVERIFY(write(temp.filePath("psp/POLISH/MERGED.TXT"),
                      QString("Który klub wygrał?\r\nLegia\r\nLech\r\nWisła\r\nGórnik\r\nJaki to film?\r\nRejs\r\n"
                              "Miś\r\nPotop\r\nKler")
                          .toUtf8()));
        QByteArray qdb("\0BDQ", 4);
        put(qdb, 0, 4);
        put(qdb, 3, 4);
        put(qdb, 0, 4);
        const auto pspRecord = [&](quint32 id, int category, int first, int flags, int level) {
            put(qdb, id, 4);
            put(qdb, 0, 2);
            put(qdb, category, 2);
            for (int k = 0; k < 5; ++k)
                put(qdb, first + k, 2);
            put(qdb, 0, 2);
            put(qdb, flags, 2);
            qdb += char(level);
            qdb += char(0);
        };
        pspRecord(100, 340, 0, 0, 0x21);
        pspRecord(101, 345, 0, 0, 0x11); // Put-in-order round: skipped.
        pspRecord(102, 335, 5, 0x3000, 0x31);
        QVERIFY(write(temp.filePath("psp/POLISH/MERGED.QDB"), qdb));
        const auto psp = studio::importLegacy(temp.filePath("psp/POLISH/MERGED.QDB"));
        QVERIFY2(psp.ok(), qPrintable(psp.error));
        QCOMPARE(psp.document.questions.size(), 2);
        QCOMPARE(psp.document.questions[0].difficulty, 2);
        QCOMPARE(psp.document.questions[0].subjectId, 174);
        QCOMPARE(psp.document.questions[0].answers[0], QString("Legia"));
        QCOMPARE(psp.document.questions[1].subjectId, 199);
        QCOMPARE(psp.document.questions[1].extra["source_media"].toArray()[0].toObject()["type"].toString(),
                 QString("psp-picture"));

        // PSP Quiz World: per-language 1 MB bundle in a numbered .FLZ file.
        QByteArray bundle(0x100000, 'U');
        QByteArray table("\0BDQ", 4);
        put(table, 0, 4);
        put(table, 2, 4);
        put(table, 0, 4);
        for (const auto category : {0xc372a3bcu, 0x539151f3u}) {
            put(table, 1001, 4);
            put(table, 0xffffffffu, 4);
            put(table, 0, 4);
            put(table, category, 4);
            for (int k = 0; k < 5; ++k)
                put(table, k, 2);
            put(table, 0, 2);
            put(table, 0, 2);
            table += char(0x11);
            table += char(0);
        }
        bundle.replace(0x18000, table.size(), table);
        const auto lines = QString("Który żółw płynął najszybciej?\r\nŻółw błotny\r\nŻółw grecki\r\nŻółw "
                                   "stepowy\r\nŻółw olbrzymi")
                               .toUtf8();
        bundle.replace(0x60000, lines.size(), lines);
        const auto pspIso = isoImage({{"PSP_GAME/USRDIR/00000004.FLZ", bundle}});
        QVERIFY(write(temp.filePath("quizworld.cso"), csoImage(pspIso)));
        const auto world = studio::importLegacy(temp.filePath("quizworld.cso"), "pl");
        QVERIFY2(world.ok(), qPrintable(world.error));
        QCOMPARE(world.document.questions.size(), 1);
        QCOMPARE(world.document.questions[0].text, QString("Który żółw płynął najszybciej?"));
        QCOMPARE(world.document.questions[0].difficulty, 2);
        QCOMPARE(world.document.questions[0].subjectId, 174);

        // Not a quiz source.
        QVERIFY(!studio::importLegacy(temp.filePath("missing.iso")).ok());
    }
    // ---- PS3 writers (mirror the documented formats; the reader was checked on retail DLC).
    static QByteArray be32(quint32 v) {
        QByteArray b(4, '\0');
        qToBigEndian<quint32>(v, b.data());
        return b;
    }
    static QByteArray be64(quint64 v) {
        QByteArray b(8, '\0');
        qToBigEndian<quint64>(v, b.data());
        return b;
    }
    static QByteArray paramSfo(const QString &title) {
        const auto key = QByteArray("TITLE\0", 6).leftJustified(8, '\0');
        const auto value = title.toUtf8() + '\0';
        const quint32 keys = 20 + 16, data = keys + quint32(key.size());
        QByteArray sfo("\0PSF", 4);
        for (quint32 v : {0x101u, keys, data, 1u})
            sfo += QByteArray(reinterpret_cast<const char *>(&v), 4); // little-endian hosts
        QByteArray entry(16, '\0');
        qToLittleEndian<quint16>(0x0204, entry.data() + 2);
        qToLittleEndian<quint32>(quint32(value.size()), entry.data() + 4);
        qToLittleEndian<quint32>(quint32(value.size()), entry.data() + 8);
        return sfo + entry + key + value;
    }
    // NPD EDAT, version 2. flags 0x0c: CMAC block hash + wrapped block keys (Quiz TV
    // DLC); 0x3c: HMAC-SHA1 with per-block metadata (Quiz World EDATs).
    static QByteArray npd(const QByteArray &plain, const QByteArray &key, quint32 license, const QByteArray &contentId,
                          quint32 flags, quint32 blockSize = 0x400) {
        using studio::ps4::aesCbc;
        using studio::ps4::aesEcb;
        QByteArray header("NPD\0", 4);
        header += be32(2) + be32(license) + be32(0) + contentId.leftJustified(0x30, '\0');
        header += QByteArray::fromHex("00112233445566778899aabbccddeeff");  // digest = CBC IV
        header += QByteArray(16, '\x5a');                                      // title hash
        header += QByteArray::fromHex("0f1e2d3c4b5a69788796a5b4c3d2e1f0"); // dev hash
        header += QByteArray(16, '\0');
        header += be32(flags) + be32(blockSize) + be64(quint64(plain.size()));
        header = header.leftJustified(0x100, '\0');
        const auto digest = header.mid(0x40, 16), devHash = header.mid(0x60, 16);
        const auto erk = QByteArray::fromHex("be959ca8308defa2e5e180c63712a9ae");
        const int blocks = int((plain.size() + blockSize - 1) / blockSize);
        const bool leads = flags & 0x20;
        QByteArray table, body;
        for (int i = 0; i < blocks; ++i) {
            auto chunk = plain.mid(qint64(i) * blockSize, blockSize);
            while (chunk.size() % 16)
                chunk += '\0';
            auto seed = devHash.left(12) + be32(quint32(i));
            const auto blockKey = aesEcb(seed, key, true);
            const auto hashSeed = (flags & 0x10) ? aesEcb(blockKey, key, true) : blockKey;
            auto cipherKey = blockKey, hashKey = hashSeed;
            if (flags & 0x8) {
                cipherKey = aesEcb(blockKey, erk, false);
                hashKey = aesEcb(hashSeed, erk, false);
            }
            const auto encrypted = aesCbc(chunk, cipherKey, digest, true);
            const auto mac = (flags & 0x10) ? QMessageAuthenticationCode::hash(
                                                  encrypted, leads ? hashKey + QByteArray(4, '\0') : hashKey,
                                                  QCryptographicHash::Sha1)
                                            : studio::ps3::aesCmac(hashKey, encrypted);
            if (leads)
                body += mac.left(16) + QByteArray(16, '\0') + encrypted.leftJustified(i + 1 < blocks ? blockSize : 0, '\0');
            else {
                table += mac.left(16);
                body += encrypted.leftJustified(i + 1 < blocks ? blockSize : 0, '\0');
            }
        }
        return header + table + body;
    }
    // Retail PS3 package: AES-CTR over the item table, names and data.
    static QByteArray ps3Package(const QByteArray &contentId, const QList<QPair<QByteArray, QByteArray>> &files) {
        QByteArray table, names, data;
        const qint64 tableSize = qint64(files.size()) * 32;
        QList<QByteArray> padded;
        for (const auto &file : files)
            names += file.first.leftJustified(int((file.first.size() + 15) / 16 * 16), '\0');
        qint64 nameAt = tableSize, dataAt = tableSize + names.size();
        for (const auto &file : files) {
            const bool directory = file.second.isNull();
            table += be32(quint32(nameAt)) + be32(quint32(file.first.size())) + be64(quint64(dataAt)) +
                     be64(quint64(directory ? 0 : file.second.size())) + be32(directory ? 4 : 3) + be32(0);
            nameAt += (file.first.size() + 15) / 16 * 16;
            const auto content = file.second.leftJustified(int((file.second.size() + 15) / 16 * 16), '\0');
            data += content;
            dataAt += content.size();
        }
        auto plain = table + names + data;
        const auto iv = QByteArray::fromHex("a0a1a2a3a4a5a6a7a8a9aaabacadaeaf");
        QByteArray stream;
        for (qint64 i = 0; i < plain.size() / 16; ++i) {
            auto counter = iv;
            qToBigEndian<quint64>(qFromBigEndian<quint64>(iv.constData() + 8) + quint64(i), counter.data() + 8);
            stream += counter;
        }
        stream = studio::ps4::aesEcb(stream, QByteArray::fromHex("2e7b71d7c9c9a14ea3221f188828b8f8"), true);
        for (qint64 i = 0; i < plain.size(); ++i)
            plain[i] = char(plain[i] ^ stream[i]);
        QByteArray header("\x7fPKG", 4);
        header += QByteArray::fromHex("80000001") + be32(0xc0) + be32(0) + be32(0) + be32(quint32(files.size())) +
                  be64(quint64(0xc0 + plain.size())) + be64(0xc0) + be64(quint64(plain.size()));
        header += contentId.leftJustified(0x30, '\0');
        header = header.leftJustified(0x70, '\0') + iv;
        return header.leftJustified(0xc0, '\0') + plain;
    }
    void projectKeepsItsMediaAndTravelsAsOneArchive() {
        QTemporaryDir temp;
        QVERIFY(QDir().mkpath(temp.filePath("outside/cache")));
        QImage picture(32, 18, QImage::Format_RGB32);
        picture.fill(Qt::red);
        QVERIFY(picture.save(temp.filePath("outside/Paryż wieża.png")));
        QVERIFY(write(temp.filePath("outside/voice.wav"), QByteArray("RIFF-pl")));
        QVERIFY(write(temp.filePath("outside/cache/6000098975.rsf"), QByteArray("RSF-en")));
        QVERIFY(write(temp.filePath("outside/clip.mp4"), QByteArray("video")));
        studio::Document doc;
        auto first = studio::newQuestion();
        first.text = "Co to za miasto?";
        first.answers = QStringList{"Paryż", "Rzym", "Oslo", "Lima"};
        first.image = temp.filePath("outside/Paryż wieża.png");
        first.speech = temp.filePath("outside/voice.wav");
        first.video = temp.filePath("outside/clip.mp4");
        // Another language with its own speech (e.g. imported into the app cache).
        first.translations["en"] = QJsonObject{{"question", "Which city?"},
                                               {"answers", QJsonArray{"Paris", "Rome", "Oslo", "Lima"}},
                                               {"correct_index", 0},
                                               {"image", first.image},
                                               {"speech", temp.filePath("outside/cache/6000098975.rsf")}};
        auto second = first; // Same picture: stored once.
        second.id = "second";
        second.video.clear();
        second.translations = {};
        auto lost = studio::newQuestion();
        lost.image = temp.filePath("outside/gone.png");
        doc.questions = {first, second, lost};

        const auto project = temp.filePath("Moja paczka");
        const auto report = studio::collectMedia(doc, project);
        QVERIFY2(report.ok(), qPrintable(report.error));
        QCOMPARE(report.copied, 4);
        QCOMPARE(report.missing, 1);
        const auto &q = doc.questions[0];
        QVERIFY(q.image.startsWith(QDir(project).filePath("media/images/")));
        QVERIFY(q.speech.startsWith(QDir(project).filePath("media/audio/pl/")));
        QVERIFY(q.video.startsWith(QDir(project).filePath("media/video/")));
        QCOMPARE(doc.questions[1].image, q.image);
        QVERIFY(QFileInfo(q.image).fileName().startsWith("Pary_wie_a-"));
        const auto enSpeech = q.translations["en"].toObject()["speech"].toString();
        QVERIFY(enSpeech.startsWith(QDir(project).filePath("media/audio/en/")));
        // Saving again copies nothing; the JSON only holds relative links.
        QCOMPARE(studio::collectMedia(doc, project).copied, 0);
        const auto json = QDir(project).filePath("Moja paczka.buzz.json");
        QString error;
        QVERIFY2(studio::saveDocument(doc, json, &error), qPrintable(error));
        QVERIFY(!bytes(json).contains(temp.path().toUtf8()));
        QVERIFY(bytes(json).contains("\"media/video/clip-"));

        // Without the original files the project still opens complete.
        QVERIFY(QDir(temp.filePath("outside")).removeRecursively());
        const auto reopened = studio::loadDocument(json);
        QVERIFY2(reopened.ok(), qPrintable(reopened.error));
        QCOMPARE(bytes(reopened.document.questions[0].speech), QByteArray("RIFF-pl"));
        QCOMPARE(bytes(reopened.document.questions[0].video), QByteArray("video"));
        auto en = reopened.document.questions[0];
        en.switchLanguage("en");
        QCOMPARE(bytes(en.speech), QByteArray("RSF-en"));

        // One file for another computer.
        const auto archive = temp.filePath("transfer/Paczka.buzzproject");
        QVERIFY(QDir().mkpath(temp.filePath("transfer")));
        const auto exported = studio::exportProjectArchive(reopened.document, archive);
        QVERIFY2(exported.ok(), qPrintable(exported.error));
        QCOMPARE(exported.copied, 4);
        QCOMPARE(exported.missing, 1);
        QVERIFY(QDir(project).removeRecursively());
        QVERIFY(studio::isProjectArchive(archive));
        const auto opened = studio::loadDocument(archive);
        QVERIFY2(opened.ok(), qPrintable(opened.error));
        QCOMPARE(opened.projectPath, temp.filePath("transfer/Paczka/Paczka.buzz.json"));
        QCOMPARE(opened.document.questions.size(), 3);
        QCOMPARE(bytes(opened.document.questions[0].video), QByteArray("video"));
        QCOMPARE(QImage(opened.document.questions[1].image).size(), QSize(32, 18));
        // Opening it again never overwrites the first copy.
        QCOMPARE(studio::loadDocument(archive).projectPath, temp.filePath("transfer/Paczka (2)/Paczka.buzz.json"));
    }
    void rapLicenseMatchesReference() {
        QCOMPARE(studio::ps3::rapToKey(QByteArray::fromHex("000102030405060708090a0b0c0d0e0f")).toHex(),
                 QByteArray("56b28b74268d9326cf733a522ea280b4"));
        QCOMPARE(studio::ps3::rapToKey(QByteArray(16, '\x11')).toHex(), QByteArray("228f4d28b1bd7823fa1d93abda3b4837"));
        // RFC 4493 example 2 (one block).
        QCOMPARE(studio::ps3::aesCmac(QByteArray::fromHex("2b7e151628aed2a6abf7158809cf4f3c"),
                                      QByteArray::fromHex("6bc1bee22e409f96e93d7e117393172a"))
                     .toHex(),
                 QByteArray("070a16b46b4d4144f79bdd9dd04a287c"));
    }
    void importsPs3DlcPackagesLicensesAndDiscImages() {
        QSettings().remove("ps3Licenses");
        QTemporaryDir temp;
        const auto plainPack = bytes(fixture("build-reference.pak"));
        const auto expected = studio::importBuzz(fixture("build-reference.pak"), "en", false);
        QVERIFY2(expected.ok(), qPrintable(expected.error));
        const QByteArray cid = "EP9000-BCES00647_00-BQWTESTPACK0103";
        const auto rap = QByteArray(16, '\x11');
        const auto key = studio::ps3::rapToKey(rap);
        const auto sameQuestions = [&](const studio::LoadResult &r) {
            QVERIFY2(r.ok(), qPrintable(r.error));
            QCOMPARE(r.document.questions.size(), expected.document.questions.size());
            QCOMPARE(r.document.questions[0].text, expected.document.questions[0].text);
            QCOMPARE(r.document.questions[0].answers, expected.document.questions[0].answers);
        };

        // Installed paid DLC: dev_hdd0/game/<title>/USRDIR/PACKnnnn/<LANG>PACKnnnn.EDAT,
        // license in dev_hdd0/home/00000001/exdata.
        for (const quint32 flags : {0x0cu, 0x3cu}) {
            QTemporaryDir hdd;
            const auto pack = hdd.filePath("game/BCES00647/USRDIR/PACK0103/ENGPACK0103.EDAT");
            QVERIFY(QDir().mkpath(QFileInfo(pack).absolutePath()));
            QVERIFY(write(pack, npd(plainPack, key, 2, cid, flags)));
            QVERIFY(studio::ps3::isPs3Source(pack));
            const auto missing = studio::loadDocument(pack, "en");
            QVERIFY(!missing.ok());
            QCOMPARE(missing.license, QString::fromLatin1(cid));
            QCOMPARE(studio::inspectLanguages(pack).license, QString::fromLatin1(cid));
            QVERIFY(QDir().mkpath(hdd.filePath("home/00000001/exdata")));
            QVERIFY(write(hdd.filePath("home/00000001/exdata/" + cid + ".rap"), rap));
            sameQuestions(studio::loadDocument(pack, "en"));
            // A whole installed DLC folder through its PARAM.SFO.
            QVERIFY(write(hdd.filePath("game/BCES00647/PARAM.SFO"), paramSfo("Test Pack")));
            const auto folder = studio::loadDocument(hdd.filePath("game/BCES00647/PARAM.SFO"), "en");
            sameQuestions(folder);
            QCOMPARE(folder.document.title, QString("Test Pack"));
        }

        // A wrong license is detected by the block hash, never decrypted to noise.
        const auto wrong = temp.filePath("wrong/ENGPACK0103.EDAT");
        QVERIFY(QDir().mkpath(QFileInfo(wrong).absolutePath()));
        QVERIFY(write(wrong, npd(plainPack, key, 2, cid, 0x0c)));
        QVERIFY(write(temp.filePath("wrong/" + cid + ".rap"), QByteArray(16, '\x22')));
        const auto mismatch = studio::loadDocument(wrong, "en");
        QVERIFY(!mismatch.ok());
        QCOMPARE(mismatch.license, QString::fromLatin1(cid));
        // …and a license chosen by the user is remembered for this content ID.
        QVERIFY(write(temp.filePath("chosen.rap"), rap));
        QString error;
        QVERIFY(studio::ps3::rememberLicense(cid, temp.filePath("chosen.rap"), &error));
        QVERIFY(QFile::remove(temp.filePath("wrong/" + cid + ".rap")));
        sameQuestions(studio::loadDocument(wrong, "en"));

        // PSN package with the encrypted pack, license next to the package.
        const auto pkg = temp.filePath("dlc/random-name.pkg");
        QVERIFY(QDir().mkpath(QFileInfo(pkg).absolutePath()));
        QVERIFY(write(pkg, ps3Package(cid, {{"PARAM.SFO", paramSfo("Buzz! DLC")},
                                            {"USRDIR", QByteArray()},
                                            {"USRDIR/PACK0103", QByteArray()},
                                            {"USRDIR/PACK0103/ENGPACK0103.EDAT", npd(plainPack, key, 2, cid, 0x0c)}})));
        QVERIFY(studio::ps3::isPs3Source(pkg));
        const auto languages = studio::inspectLanguages(pkg);
        QVERIFY2(languages.ok(), qPrintable(languages.error)); // remembered license
        QCOMPARE(languages.codes, QStringList{"en"});
        const auto fromPkg = studio::loadDocument(pkg, "en");
        sameQuestions(fromPkg);
        QCOMPARE(fromPkg.document.title, QString("Buzz! DLC"));
        QVERIFY(fromPkg.document.questions[0].extra["source_archive"].toString().endsWith(
            ":USRDIR/PACK0103/ENGPACK0103.EDAT"));

        // A game update package holds no question packs.
        const auto update = temp.filePath("update.pkg");
        QVERIFY(write(update, ps3Package("EP9000-BCES00647_00-UPDATE", {{"PARAM.SFO", paramSfo("Update")},
                                                                        {"USRDIR/EBOOT.BIN", QByteArray(64, 'x')}})));
        const auto noPacks = studio::loadDocument(update);
        QVERIFY(!noPacks.ok());
        QVERIFY(noPacks.license.isEmpty());

        // Decrypted PS3 disc image: every pack of the chosen language.
        const auto iso = temp.filePath("ps3.iso");
        QVERIFY(write(iso, isoImage({{"PS3_GAME/PARAM.SFO", paramSfo("Buzz! Disc")},
                                     {"PS3_GAME/USRDIR/PACK0103/ENGPACK0103.PAK", plainPack}})));
        QVERIFY(studio::ps3::isPs3Source(iso));
        const auto disc = studio::loadDocument(iso, "en");
        sameQuestions(disc);
        QCOMPARE(disc.document.title, QString("Buzz! Disc"));
        // An image whose files are still encrypted says so.
        auto scrambled = plainPack;
        for (auto &c : scrambled)
            c = char(c ^ 0x5a);
        QVERIFY(write(iso, isoImage({{"PS3_GAME/PARAM.SFO", paramSfo("Buzz! Disc")},
                                     {"PS3_GAME/USRDIR/PACK0103/ENGPACK0103.PAK", scrambled}})));
        QVERIFY(!studio::loadDocument(iso, "en").ok());
    }
    void initTestCase() {
        QCoreApplication::setOrganizationName("BuzzStudioTests");
        QCoreApplication::setApplicationName("BuilderTests");
        QStandardPaths::setTestModeEnabled(true);
    }
    void matchesReferencePackEveryEntryAndByte() {
        QTemporaryDir temp;
        const auto r = studio::buildPack(request(temp.path()));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.questions, 2);
        QCOMPARE(r.categories, 4);
        QCOMPARE(r.rounds, 2);
        QVERIFY(r.path.endsWith("PACK0103/ENGPACK0103.edat"));
        Archive actual(r.path), expected(fixture("build-reference.pak"));
        QCOMPARE(actual.entries.keys(), expected.entries.keys());
        for (const auto &name : actual.entries.keys())
            QCOMPARE(actual.read(name), expected.read(name));
        QCOMPARE(bytes(r.path), bytes(fixture("build-reference.pak")));
    }
    void selectedLanguageControlsPrefixAndSkipsMissingTranslations() {
        QTemporaryDir temp;
        auto req = request(temp.path());
        req.document.switchLanguage("pl");
        auto &q = req.document.questions[0];
        q.text = QString::fromUtf8("Która litera jest pierwsza?");
        q.answers = QStringList{"B", "C", "D", "A"};
        q.correct = 3;
        const auto built = studio::buildPack(req);
        QVERIFY2(built.ok(), qPrintable(built.error));
        QCOMPARE(built.questions, 1);
        QCOMPARE(built.skipped, 1);
        QCOMPARE(built.language, QString("POL"));
        QVERIFY(built.path.endsWith("POLPACK0103.edat"));
        const auto parsed = studio::importBuzz(built.path, "pl", false);
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.document.questions[0].text, q.text);
        QCOMPARE(parsed.document.questions[0].answers[0], QString("A"));
        req.document.switchLanguage("fr");
        const auto missing = studio::buildPack(req);
        QVERIFY(!missing.ok());
        QVERIFY(missing.path.isEmpty());
    }
    void failedOrCancelledBuildPreservesPreviousOutput() {
        QTemporaryDir temp;
        auto req = request(temp.path());
        const auto first = studio::buildPack(req);
        QVERIFY(first.ok());
        const auto original = bytes(first.path);
        req.document.questions[0].image = temp.filePath("missing.png");
        QVERIFY(!studio::buildPack(req).ok());
        QCOMPARE(bytes(first.path), original);
        req.document.questions[0].image.clear();
        req.document.questions[0].text = "Changed question";
        std::atomic_bool cancel{false};
        const auto cancelled = studio::buildPack(
            req,
            [&](int percent, const QString &) {
                if (percent >= 75)
                    cancel.store(true);
            },
            &cancel);
        QVERIFY(cancelled.cancelled);
        QCOMPARE(bytes(first.path), original);
        req.categoryBase = 999;
        QVERIFY(!studio::buildPack(req).ok());
        QCOMPARE(bytes(first.path), original);
    }
    void installationMakesBackupAndRejectsCategoryCollisions() {
        QTemporaryDir temp;
        auto req = request(temp.filePath("out"));
        const auto built = studio::buildPack(req);
        QVERIFY2(built.ok(), qPrintable(built.error));
        const auto runtime = temp.filePath("runtime");
        QVERIFY(QDir().mkpath(runtime + "/dev_hdd0"));
        auto installed = studio::installPack(built.path, runtime, "BCES00647");
        QVERIFY2(installed.ok(), qPrintable(installed.error));
        QCOMPARE(bytes(installed.installedPath), bytes(built.path));
        QVERIFY(installed.backupPath.isEmpty());
        QVERIFY(write(installed.installedPath, "previous version"));
        installed = studio::installPack(built.path, runtime, "BCES00647");
        QVERIFY2(installed.ok(), qPrintable(installed.error));
        QCOMPARE(bytes(installed.backupPath), QByteArray("previous version"));
        QCOMPARE(bytes(installed.installedPath), bytes(built.path));
        req.number = 104;
        const auto other = studio::buildPack(req);
        QVERIFY(other.ok());
        const auto collision = studio::installPack(other.path, runtime, "BCES00647");
        QVERIFY(!collision.ok());
        QVERIFY(!QFileInfo::exists(runtime + "/dev_hdd0/game/BCES00647/USRDIR/PACK0104"));
        req.categoryBase = 800;
        req.runtimeRoot = runtime;
        req.install = true;
        const auto combined = studio::buildPack(req);
        QVERIFY2(combined.ok(), qPrintable(combined.error));
        QVERIFY(!combined.installedPath.isEmpty());
        QVERIFY(!studio::installPack(built.path, runtime, "../../escape").ok());
    }
    void gameFolderFromBuzzSetupSuggestsRuntimeAndInstallsBeforeFirstStart() {
        QTemporaryDir temp;
        // BuzzSetup's dist/runtime before the game's first start: no dev_hdd0 yet.
        const auto game = temp.filePath("dist");
        const auto runtime = game + "/runtime";
        const auto packs = runtime + "/disc/PS3_GAME/USRDIR/PACK0100";
        QVERIFY(QDir().mkpath(packs));
        QVERIFY(write(runtime + "/EBOOT.elf", "elf"));
        QVERIFY(QFile::copy(fixture("build-template.pak"), packs + "/ENGPACK0100.PAK"));
        QVERIFY(QFile::copy(fixture("build-template.pak"), packs + "/POLPACK0100.PAK"));
        QSettings().setValue("build/runtimeRoot", temp.filePath("moved-away"));
        QSettings().setValue("build/templatePath", temp.filePath("moved-away/POLPACK0100.PAK"));
        studio::setGameDirectory(game);
        const auto english = studio::suggestBuildSettings("en");
        QCOMPARE(QFileInfo(english["runtimeRoot"].toString()).canonicalFilePath(),
                 QFileInfo(runtime).canonicalFilePath());
        QCOMPARE(QFileInfo(english["templatePath"].toString()).fileName(), QString("ENGPACK0100.PAK"));
        QCOMPARE(QFileInfo(studio::suggestBuildSettings("pl")["templatePath"].toString()).fileName(),
                 QString("POLPACK0100.PAK"));
        studio::setGameDirectory({});
        QSettings().remove("build");
        const auto built = studio::buildPack(request(temp.filePath("out")));
        QVERIFY2(built.ok(), qPrintable(built.error));
        const auto installed = studio::installPack(built.path, runtime, "BCES00647");
        QVERIFY2(installed.ok(), qPrintable(installed.error));
        QVERIFY(installed.installedPath.contains("/dev_hdd0/game/BCES00647/USRDIR/PACK0103/"));
        // A folder that is neither a runtime nor a prepared copy is still refused.
        QVERIFY(QDir().mkpath(temp.filePath("empty")));
        QVERIFY(!studio::installPack(built.path, temp.filePath("empty"), "BCES00647").ok());
        QVERIFY(!QFileInfo::exists(temp.filePath("empty/dev_hdd0")));
    }
    void installerRejectsCorruptSourceAndSymlinkTargets() {
        QTemporaryDir temp;
        auto req = request(temp.filePath("out"));
        const auto built = studio::buildPack(req);
        QVERIFY(built.ok());
        const auto runtime = temp.filePath("runtime");
        QVERIFY(QDir().mkpath(runtime + "/dev_hdd0"));
        auto corrupt = bytes(built.path);
        Archive archive(built.path);
        const auto entry = archive.entries.cbegin().value();
        const int pos = entry.offset + 30 + u16(corrupt, entry.offset + 26) + u16(corrupt, entry.offset + 28);
        corrupt[pos] = char(quint8(corrupt[pos]) ^ 1);
        const auto path = temp.filePath("ENGPACK0103.edat");
        QVERIFY(write(path, corrupt));
        QVERIFY(!studio::installPack(path, runtime, "BCES00647").ok());
        QVERIFY(!QFileInfo::exists(runtime + "/dev_hdd0/game"));
#ifndef Q_OS_WIN
        const auto outside = temp.filePath("outside");
        QVERIFY(QDir().mkpath(outside));
        QVERIFY(QFile::link(outside, runtime + "/dev_hdd0/game"));
        QVERIFY(!studio::installPack(built.path, runtime, "BCES00647").ok());
        QVERIFY(QDir(outside).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
#endif
    }
    void nativeAtracMatchesFrozenNonSilentReference() {
        const auto pcm = bytes(fixture("atrac-input.pcm"));
        QVERIFY(!pcm.isEmpty());
        QCOMPARE(encodeAtracPcm(pcm), bytes(fixture("atrac-reference.rsf")));
    }
    void rewrapsWemExactlyLikeReference() {
        QCOMPARE(unwrapWem(bytes(fixture("wem-input.riff"))), bytes(fixture("wem-at9-reference.riff")));
        QVERIFY_EXCEPTION_THROWN(unwrapWem("not a wave file"), std::runtime_error);
    }
    void buildsAndDecodesPicturesAndSpeech() {
        const auto ffmpeg = studio::defaultFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("FFmpeg not installed.");
        QTemporaryDir temp;
        auto req = request(temp.filePath("out"));
        req.document.questions.resize(1);
        const auto pcm = bytes(fixture("atrac-input.pcm"));
        QByteArray wav("RIFF");
        put(wav, 36 + pcm.size(), 4);
        wav += "WAVEfmt ";
        put(wav, 16, 4);
        put(wav, 1, 2);
        put(wav, 1, 2);
        put(wav, 48000, 4);
        put(wav, 96000, 4);
        put(wav, 2, 2);
        put(wav, 16, 2);
        wav += "data";
        put(wav, pcm.size(), 4);
        wav += pcm;
        const auto audioPath = temp.filePath("voice.wav");
        QVERIFY(write(audioPath, wav));
        QImage portrait(90, 120, QImage::Format_RGB32);
        portrait.fill(Qt::white);
        const auto imagePath = temp.filePath("portrait.png");
        QVERIFY(portrait.save(imagePath));
        req.document.questions[0].speech = audioPath;
        req.document.questions[0].image = imagePath;
        const auto built = studio::buildPack(req);
        QVERIFY2(built.ok(), qPrintable(built.error));
        QCOMPARE(built.pictures, 1);
        Archive archive(built.path);
        const auto speech = archive.read("ENG/QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/6010300001.rsf");
        QCOMPARE(speech, bytes(fixture("atrac-reference.rsf")));
        const auto riffPath = temp.filePath("atrac.wav");
        QVERIFY(write(riffPath, riff(speech)));
        const auto decoded = temp.filePath("decoded.pcm");
        runMedia(ffmpeg, {"-v", "error", "-y", "-i", riffPath, "-f", "s16le", decoded}, nullptr);
        const auto output = bytes(decoded);
        double best = -1;
        int delay = 0;
        for (int d = 0; d <= 2048 && (d + pcm.size() / 2) * 2 <= output.size(); ++d) {
            double correlation = 0;
            for (int i = 0; i < pcm.size() / 2; ++i)
                correlation += double(qFromLittleEndian<qint16>(pcm.constData() + i * 2)) *
                               qFromLittleEndian<qint16>(output.constData() + (i + d) * 2);
            if (correlation > best) {
                best = correlation;
                delay = d;
            }
        }
        double energy = 0, error = 0;
        for (int i = 0; i < pcm.size() / 2; ++i) {
            const double x = qFromLittleEndian<qint16>(pcm.constData() + i * 2),
                         y = qFromLittleEndian<qint16>(output.constData() + (i + delay) * 2);
            energy += x * x;
            error += (x - y) * (x - y);
        }
        QVERIFY2(10 * std::log10(energy / error) > 15,
                 qPrintable(QString("SNR %1 dB").arg(10 * std::log10(energy / error))));
        const auto sheet = temp.filePath("sheet.sdi");
        QVERIFY(write(sheet, archive.read("ENG/QUESTIONASSETS/VIDEO/pics001.sdi")));
        const auto framePath = temp.filePath("frame.png");
        runMedia(ffmpeg,
                 {"-v", "error", "-y", "-skip_initial_bytes", "2048", "-i", sheet, "-vf",
                  "select=eq(n\\,1),scale=640:360", "-frames:v", "1", framePath},
                 nullptr);
        const QImage frame(framePath);
        QVERIFY(!frame.isNull());
        int minX = 640, maxX = -1, minY = 360, maxY = -1;
        for (int y = 0; y < frame.height(); ++y)
            for (int x = 0; x < frame.width(); ++x)
                if (qRed(frame.pixel(x, y)) > 200) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
        QVERIFY(maxX >= minX);
        QVERIFY(std::abs(double(maxX - minX + 1) / (maxY - minY + 1) - .75) < .02);
        const auto imported = studio::importBuzz(built.path, "en", false);
        QVERIFY(imported.ok());
        QCOMPARE(
            imported.document.questions[0].extra["source_media"].toArray()[0].toObject()["aspect"].toInt(),
            4);
    }
};
QTEST_GUILESS_MAIN(BuilderTest)
#include "builder_test.moc"
