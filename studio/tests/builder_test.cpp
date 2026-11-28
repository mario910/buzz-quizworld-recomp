#include "buzz_import.h"
#include "kip_import.h"
#include "legacy_import.h"
#include "pack_builder.h"
#include "pack_media.h"
#include "ps3_source.h"
#include "source_movie.h"
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
    static QByteArray isoImage(const QMap<QString, QByteArray> &files, const QString &splitFile = {}) {
        QVector<QByteArray> sectors(18, QByteArray(2048, '\0'));
        auto record = [](quint32 extent, quint32 size, bool directory, const QByteArray &name, bool more = false) {
            QByteArray r(33, '\0');
            qToLittleEndian<quint32>(extent, r.data() + 2);
            qToBigEndian<quint32>(extent, r.data() + 6);
            qToLittleEndian<quint32>(size, r.data() + 10);
            qToBigEndian<quint32>(size, r.data() + 14);
            r[25] = directory ? 2 : more ? char(0x80) : 0;
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
                    auto start = quint32(sectors.size());
                    const auto name = (rest + ";1").toLatin1();
                    const bool split = it.key() == splitFile && it->size() > 2048;
                    const int firstExtentSize = split ? 2048 : 0;
                    if (split) {
                        body += record(start, firstExtentSize, false, name, true);
                        sectors << it->left(firstExtentSize) << QByteArray(2048, '\xff');
                        start = quint32(sectors.size());
                    }
                    for (qint64 at = firstExtentSize; at < std::max<qint64>(1, it.value().size()); at += 2048)
                        sectors << it.value().mid(at, 2048).leftJustified(2048, '\0');
                    body += record(start, quint32(it.value().size() - firstExtentSize), false, name);
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
        QVERIFY(root.mkpath("Media/StreamingAssets/Audio/GeneratedSoundBanks/PS4/English(UK)"));
        QVERIFY(root.mkpath("Media/StreamingAssets/Audio/GeneratedSoundBanks/PS4/PatchVoice"));
        // 21-column trivia rows: id, text key, 4 answer keys, category path, ..., image, frame, tags, difficulty.
        auto row = [](const QString &id, const QString &key, const QString &image, const QString &difficulty) {
            QStringList r(21);
            r[0] = id;
            r[1] = key;
            for (int a = 0; a < 4; ++a)
                r[2 + a] = key + "_A" + QString::number(a + 1);
            r[6] = "Experience";
            r[7] = "ModernWorld";
            r[8] = "Society";
            r[9] = "Topic" + id;
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
        assets += textAsset("KIP_Sorting_QuestionText_pl", "LSS1pl\tLSS1\tG1\tCat\tG2\tRose|~|Oak|~|Moss");
        assets += textAsset("KIP_Linking_QuestionText_pl", "LSL1pl\tLSL1\tA01|~|A02|~|A03|~|A04\tB01|~|B02|~|B03|~|B04");
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
        pl << "LSS1\tZwierzę czy roślina?" << "G1\tZwierzę" << "G2\tRoślina" << "Cat\tKot"
           << "Rose\tRóża" << "Oak\tDąb" << "Moss\tMech" << "LSL1\tPołącz państwo ze stolicą."
           << "A01\tPolska" << "A02\tFrancja" << "A03\tHiszpania" << "A04\tNiemcy"
           << "B01\tWarszawa" << "B02\tParyż" << "B03\tMadryt" << "B04\tBerlin";
        pl << "Experience\tDoświadczenie" << "ModernWorld\tŚwiat współczesny" << "Society\tSpołeczeństwo"
           << "TopicTRV00001\tWydarzenia";
        en << "ModernWorld\tThe Modern World";
        QVERIFY(write(root.filePath("Media/StreamingAssets/Localisation/pl_text.loc"), utf16Loc(pl)));
        QVERIFY(write(root.filePath("Media/StreamingAssets/Localisation/en_text.loc"), utf16Loc(en)));
        QVERIFY(write(root.filePath("Media/StreamingAssets/Localisation/en-GB_text.loc"),
                      utf16Loc({"menu	Play"})));
        auto placeholder = en;
        for (auto &line : placeholder)
            line = line.section('\t', 0, 0) + "\t<Translate: " + line.section('\t', 0, 0) + '>';
        QVERIFY(write(root.filePath("Media/StreamingAssets/Localisation/cs_text.loc"), utf16Loc(placeholder)));
        const auto hu = QStringList{"TRV00001\tMelyik a helyes válasz?", "TRV00001_A1\tEgy", "TRV00001_A2\tKettő",
                                    "TRV00001_A3\tHárom", "TRV00001_A4\tNégy"};
        QVERIFY(write(root.filePath("Media/StreamingAssets/Localisation/hu_text.loc"), utf16Loc(hu)));
        const QByteArray jpeg = QByteArray("\xff\xd8\xff\xe0 fake jpeg ").repeated(40);
        QVERIFY(write(root.filePath("Media/StreamingAssets/QuestionImages/QuestionImages.bytes"),
                      deflatedZip("Pic1.jpg", jpeg)));
        QVERIFY(write(root.filePath("Media/StreamingAssets/Audio/GeneratedSoundBanks/PS4/Polish/pl_TRV00001.wem"),
                      "RIFF voice"));
        QVERIFY(write(root.filePath("Media/StreamingAssets/Audio/GeneratedSoundBanks/PS4/English(UK)/en_TRV00001.wem"),
                      "RIFF English voice"));
        QVERIFY(write(root.filePath("Media/StreamingAssets/Audio/GeneratedSoundBanks/PS4/Polish/pl_LSS1.wem"),
                      "RIFF sorting voice"));
        QVERIFY(write(root.filePath("Media/StreamingAssets/Audio/GeneratedSoundBanks/PS4/PatchVoice/hu_TRV00001.wem"),
                      "RIFF Hungarian voice"));
        const auto assetsPath = root.filePath("Media/resources.assets");
        QVERIFY(studio::isKipSource(assetsPath));
        QCOMPARE(studio::kipLanguages(assetsPath).codes, QStringList({"en", "hu", "pl"}));
        const auto hungarian = studio::importKip(assetsPath, "hu");
        QVERIFY2(hungarian.ok(), qPrintable(hungarian.error));
        QCOMPARE(hungarian.document.questions.size(), 1);
        QCOMPARE(hungarian.document.questions.first().text, QString("Melyik a helyes válasz?"));
        QCOMPARE(bytes(hungarian.document.questions.first().speech), QByteArray("RIFF Hungarian voice"));
        const auto r = studio::importKip(assetsPath, "pl");
        QVERIFY2(r.ok(), qPrintable(r.error));
        // TRV00003 is on the Polish kill list; the local question is Polish only.
        QStringList texts;
        for (const auto &question : r.document.questions)
            texts << question.text;
        QVERIFY2(texts.size() == 5, qPrintable(texts.join(" | ") + " / " + r.warnings.join(" | ")));
        QCOMPARE(texts, QStringList({"Pytanie TRV00001?", "Pytanie TRV00002?", "Pytanie LST00001?",
                                    "Zwierzę czy roślina?\nZwierzę:", "Połącz państwo ze stolicą.\nPolska → …"}));
        const auto &q = r.document.questions[0];
        QCOMPARE(q.text, QString("Pytanie TRV00001?"));
        QCOMPARE(q.answers, QStringList({"Odpowiedź 1", "Odpowiedź 2", "Odpowiedź 3", "Odpowiedź 4"}));
        QCOMPARE(q.correct, 0);
        QCOMPARE(q.difficulty, 1);
        QCOMPARE(q.source, QString("Knowledge Is Power"));
        QCOMPARE(q.sourceCategoryPath(),
                 QStringList({"Doświadczenie", "Świat współczesny", "Społeczeństwo", "Wydarzenia"}));
        QCOMPARE(q.extra["source_category_path"].toArray(),
                 QJsonArray({"Experience", "ModernWorld", "Society", "TopicTRV00001"}));
        QCOMPARE(bytes(q.image), jpeg);
        QCOMPARE(bytes(q.speech), QByteArray("RIFF voice"));
        auto englishVariant = q;
        englishVariant.switchLanguage("en");
        QCOMPARE(bytes(englishVariant.speech), QByteArray("RIFF English voice"));
        QCOMPARE(r.document.questions[1].difficulty, 3);
        QVERIFY(r.document.questions[1].speech.isEmpty());
        QCOMPARE(r.document.questions[2].text, QString("Pytanie LST00001?"));
        QCOMPARE(r.document.questions[3].answers, QStringList({"Kot", "Róża", "Dąb", "Mech"}));
        QCOMPARE(bytes(r.document.questions[3].speech), QByteArray("RIFF sorting voice"));
        QCOMPARE(r.document.questions[4].answers, QStringList({"Warszawa", "Paryż", "Madryt", "Berlin"}));
        QVERIFY(r.document.questions[4].speech.isEmpty());
        QVERIFY(r.warnings.contains(studio::adaptedAnswersWarning(2)));
        QVERIFY(r.warnings.contains(studio::unsupportedAnswersWarning(2)));
        auto mapped = r.document;
        QStringList mappingWarnings;
        QCOMPARE(studio::retainGameCategories(mapped, 0, "pl", mappingWarnings), 5);
        QCOMPARE(mapped.categories.size(), 4);
        QCOMPARE(mapped.questions[0].categoryId, mapped.questions[1].categoryId);
        QCOMPARE(mapped.questions[0].sourceCategoryPath(), q.sourceCategoryPath());
        QCOMPARE(mapped.categoryLabel(mapped.questions[0].subjectId), QString("Doświadczenie"));
        QCOMPARE(mapped.categoryLabel(mapped.questions[0].categoryId), QString("Świat współczesny / Społeczeństwo"));
        mapped.switchLanguage("en");
        QCOMPARE(mapped.categoryLabel(mapped.questions[0].categoryId), QString("The Modern World / Society"));
        QVERIFY(mappingWarnings.isEmpty());
        auto oldImport = r.document;
        for (auto &question : oldImport.questions)
            question.extra.remove("source_category_labels");
        studio::retainGameCategories(oldImport, 0, "pl", mappingWarnings);
        QCOMPARE(oldImport.categoryLabel(oldImport.questions[0].subjectId), QString("Experience"));
        const auto previousId = oldImport.questions[0].categoryId;
        for (auto &category : oldImport.categories)
            if (category.id == previousId)
                category.names["pl"] = "Moja podkategoria";
        for (int i = 0; i < oldImport.questions.size(); ++i)
            oldImport.questions[i].extra["source_category_labels"] = r.document.questions[i].extra["source_category_labels"];
        studio::retainGameCategories(oldImport, oldImport.questions.size(), "pl", mappingWarnings);
        QCOMPARE(oldImport.categoryLabel(oldImport.questions[0].subjectId), QString("Doświadczenie"));
        QCOMPARE(oldImport.categoryLabel(previousId), QString("Moja podkategoria"));
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
        const QByteArray order = "Id\tResource\tQuestion\tFirst\tSecond\tThird\tFourth\n"
                                 "8\tnon-asset\tUstaw w kolejności\tA\tB\tC\tD\n";
        const QByteArray unknownOrder = "Id\tResource\tQuestion\tItem\tItem\tItem\tItem\n"
                                        "9\tnon-asset\tUłóż po kolei\tA\tB\tC\tD\n";
        writeArchive(temp.filePath("POLROUND.PAK"),
                     {{"BG2/SK_POL/Rounds/POL/TEST_ROUND.pkg", bytes(temp.filePath("round.pkg"))},
                      {"BG2/SK_POL/Rounds/rri.dat", rri},
                      {"BHW/SK_POL/Rounds/POL/quickfire_hard.adt", adt},
                      {"BHW/SK_POL/Rounds/POL/toprank_easy.adt", order},
                      {"BHW/SK_POL/Rounds/POL/toprank_medium.adt", unknownOrder}});
        const auto checkPs2 = [](const studio::LoadResult &r) {
            QVERIFY2(r.ok(), qPrintable(r.error));
            QCOMPARE(r.document.language, QString("pl"));
            QCOMPARE(r.document.questions.size(), 5);
            QCOMPARE(r.duplicates, 2);
            QVERIFY(r.warnings.contains(studio::adaptedAnswersWarning(2)));
            QVERIFY(r.warnings.contains(studio::unsupportedAnswersWarning(1)));
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
            const auto comparison = byText["Co jest większe?"];
            QCOMPARE(comparison.answers.size(), 4);
            QCOMPARE(comparison.answers[comparison.correct], QString("Słoń"));
            QVERIFY(comparison.problems().isEmpty());
            const auto ordered = byText["Ustaw w kolejności\nCo jest pierwsze w tej kolejności?"];
            QCOMPARE(ordered.answers.value(ordered.correct), QString("A"));
        };
        checkPs2(studio::importLegacy(temp.filePath("POLROUND.PAK")));
        QCOMPARE(studio::legacyLanguages(temp.filePath("POLROUND.PAK")).codes, QStringList{"pl"});
        // The same archive on PS2 discs: plain ISO and compressed CSO.
        const QByteArray discConfig = "BOOT2 = cdrom0:\\SCES_546.90;1\n";
        const auto iso = isoImage({{"SYSTEM.CNF", discConfig},
                                   {"POL/POLROUND.PAK", bytes(temp.filePath("POLROUND.PAK"))}});
        QVERIFY(write(temp.filePath("ps2.iso"), iso));
        QVERIFY(write(temp.filePath("ps2.cso"), csoImage(iso)));
        for (const auto &image : {temp.filePath("ps2.iso"), temp.filePath("ps2.cso")}) {
            const auto r = studio::importLegacy(image, "pl");
            checkPs2(r);
            QCOMPARE(r.document.questions[0].source, QString("Buzz! Mega Quiz"));
        }
        const auto extracted = temp.filePath("ps2-folder");
        QVERIFY(QDir().mkpath(extracted + "/PoL"));
        QVERIFY(write(extracted + "/system.cnf", discConfig));
        QVERIFY(write(extracted + "/PoL/polround.pak", bytes(temp.filePath("POLROUND.PAK"))));
        QVERIFY(studio::isLegacySource(extracted));
        QCOMPARE(studio::legacyLanguages(extracted).codes, QStringList{"pl"});
        const auto folderImport = studio::importLegacy(extracted, "pl");
        checkPs2(folderImport);
        QCOMPARE(folderImport.document.questions[0].source, QString("Buzz! Mega Quiz"));

        // PSP Master Quiz: MERGED.QDB (24-byte records) + MERGED.TXT lines.
        QVERIFY(QDir().mkpath(temp.filePath("psp/POLISH")));
        QVERIFY(write(temp.filePath("psp/POLISH/MERGED.TXT"),
                      QString("Który klub wygrał?\r\nLegia\r\nLech\r\nWisła\r\nGórnik\r\nJaki to film?\r\nRejs\r\n"
                              "Miś\r\nPotop\r\nKler")
                          .toUtf8()));
        QByteArray qdb("\0BDQ", 4);
        put(qdb, 0, 4);
        put(qdb, 4, 4);
        put(qdb, 0, 4);
        const auto pspRecord = [&](quint32 id, int category, int first, int flags, int level, int mode = 0) {
            put(qdb, id, 4);
            put(qdb, 0, 2);
            put(qdb, category, 2);
            for (int k = 0; k < 5; ++k)
                put(qdb, first + k, 2);
            put(qdb, 0, 2);
            put(qdb, flags, 2);
            qdb += char(level);
            qdb += char(mode);
        };
        pspRecord(100, 340, 0, 0, 0x21);
        pspRecord(101, 345, 0, 0, 0x11); // Missing ordering mode: skipped.
        pspRecord(102, 335, 5, 0x3000, 0x31);
        pspRecord(103, 345, 0, 0, 0x11, 3);
        QVERIFY(write(temp.filePath("psp/POLISH/MERGED.QDB"), qdb));
        QVERIFY(QDir().mkpath(temp.filePath("QUIZASSETS")));
        QByteArray texture("TEX\0", 4);
        put(texture, 2, 4);
        put(texture, 1, 4);
        put(texture, 0x4005, 4);
        QByteArray pixels(1026, '\0');
        pixels.replace(4, 4, QByteArray::fromHex("4080ffff"));
        pixels[1024] = 1;
        texture += qCompress(pixels).mid(4);
        QVERIFY(write(temp.filePath("QUIZASSETS/00000000.TEX"), texture));
        const auto psp = studio::importLegacy(temp.filePath("psp/POLISH/MERGED.QDB"));
        QVERIFY2(psp.ok(), qPrintable(psp.error));
        QCOMPARE(psp.document.questions.size(), 3);
        QCOMPARE(psp.document.questions[0].difficulty, 2);
        QCOMPARE(psp.document.questions[0].subjectId, 174);
        QCOMPARE(psp.document.questions[0].answers[0], QString("Legia"));
        QCOMPARE(psp.document.questions[1].subjectId, 199);
        QCOMPARE(psp.document.questions[1].extra["source_media"].toArray()[0].toObject()["type"].toString(),
                 QString("psp-picture"));
        const QImage pspPicture(psp.document.questions[1].image);
        QCOMPARE(pspPicture.size(), QSize(2, 1));
        QCOMPARE(pspPicture.pixel(0, 0), qRgba(64, 128, 255, 255));
        QCOMPARE(pspPicture.pixel(1, 0), qRgba(0, 0, 0, 0));
        QCOMPARE(psp.document.questions[2].text, QString("Który klub wygrał?\nCo jest pierwsze w tej kolejności?"));
        QVERIFY(psp.warnings.contains(studio::adaptedAnswersWarning(1)));

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
    static QByteArray paramSfo(const QString &text, const QByteArray &field = "TITLE") {
        const auto key = (field + '\0').leftJustified(12, '\0');
        const auto value = text.toUtf8() + '\0';
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
        // Choosing the correct license works even while the bad RAP is still next to the EDAT.
        QVERIFY(write(temp.filePath("chosen.rap"), rap));
        QString error;
        QVERIFY(studio::ps3::rememberLicense(cid, temp.filePath("chosen.rap"), &error));
        const auto retried = studio::inspectLanguages(wrong);
        QVERIFY2(retried.ok(), qPrintable(retried.error));
        sameQuestions(studio::loadDocument(wrong, "en"));

        // A bad remembered RAP must not hide a good RAP next to a PSN package.
        const auto pkg = temp.filePath("dlc/random-name.pkg");
        QVERIFY(QDir().mkpath(QFileInfo(pkg).absolutePath()));
        QVERIFY(write(pkg, ps3Package(cid, {{"PARAM.SFO", paramSfo("Buzz! DLC")},
                                            {"USRDIR", QByteArray()},
                                            {"USRDIR/PACK0103", QByteArray()},
                                            {"USRDIR/PACK0103/ENGPACK0103.EDAT", npd(plainPack, key, 2, cid, 0x0c)}})));
        QVERIFY(studio::ps3::isPs3Source(pkg));
        QVERIFY(write(temp.filePath("dlc/" + cid + ".rap"), rap));
        QVERIFY(studio::ps3::rememberLicense(cid, temp.filePath("wrong/" + cid + ".rap"), &error));
        const auto languages = studio::inspectLanguages(pkg);
        QVERIFY2(languages.ok(), qPrintable(languages.error));
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
                                     {"PS3_GAME/USRDIR/PACK0103/ENGPACK0103.PAK", plainPack},
                                     {"PS3_GAME/USRDIR/PACK0104/FRAPACK0104.PAK", plainPack}})));
        QVERIFY(studio::ps3::isPs3Source(iso));
        const auto detected = studio::inspectLanguages(iso);
        QVERIFY2(detected.ok(), qPrintable(detected.error));
        QCOMPARE(detected.codes, QStringList{"en"});
        const auto disc = studio::loadDocument(iso, "en");
        sameQuestions(disc);
        QCOMPARE(disc.document.title, QString("Buzz! Disc"));
        QCOMPARE(disc.duplicates, disc.document.questions.size());
        QCOMPARE(disc.document.questions[0].localized()["source_rounds"].toArray().size(), 2);
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
    void restoresQuizTvCategoriesFromRoundSetupAndSharedNamedText() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto named = [](const QString &key, const QString &label) {
            const auto hash = qint32(crc32(key.toUtf8()) ^ 0xffffffffu ^ 0xfabcdef7u);
            const auto code = encodeHuffman({label});
            const QString base = "POL/NAMEDTEXT/default";
            return Entries{{base + ".hdr", code.header}, {base + ".hst", code.stream},
                           {base + ".ndx", namedIndex({QString::number(hash) + " 1"})}};
        };
        auto questions = named("PackTitleTest", "Music quiz");
        questions << qMakePair(QString("POL/SCRIPTS/questioncategories.clu"),
                               script({Call{{"AddQuestionCategoryItem"}, {115, "UNCATEGORISED", "QC_UNCATEGORISED", 67}}}));
        questions << qMakePair(QString("POL/SCRIPTS/GENERATED/addpackinfo.clu"),
                               script({Call{{"AddPackInfo"}, {1000, 2, 1, "PackTitleTest", "PACK0002", "Music"}}}));
        questions << qMakePair(QString("POL/SCRIPTS/GENERATED/roundsetup.clu"),
                               script({Call{{"AddRound"}, {1, "Quiz", 0, "First", "SUBJECT_RECENTMUSIC", "PACK0002", "recent_first"}},
                                       Call{{"AddRound"}, {2, "Quiz", 0, "Final", "SUBJECT_RECENTMUSIC", "PACK0002", "recent_final"}}}));
        for (const QString &name : {QString("recent_first"), QString("recent_final")}) {
            const auto base = "POL/ROUNDS/" + name;
            auto round = writeRound(name, "POL", 2, 1, 1, {{21, 0, "Question?", {}, {"A", "B", "C", "D"}, 2, 115, 115}});
            patch(round.little, 4, name == "recent_first" ? 1001 : 1002, 2);
            questions << qMakePair(base + ".hdr", round.header) << qMakePair(base + ".hst", round.stream);
            if (name == "recent_first") {
                questions << qMakePair(base + ".kaq", swapRound(round.little));
                patch(round.little, 4, 1000, 2);
            }
            questions << qMakePair(base + ".qak", round.little);
            if (name == "recent_first")
                questions << qMakePair(QString("POL/ROUNDS/unsupported.qak"), round.little);
        }
        const auto questionsPath = temp.filePath("POLPACK0002.PAK");
        const auto gamePath = temp.filePath("POL.PAK");
        writeArchive(questionsPath, questions);
        writeArchive(gamePath, named("SUBJECT_RECENTMUSIC", "Recent music"));
        const auto iso = temp.filePath("quiz-tv.iso");
        const auto questionPack = QStringLiteral("PS3_GAME/USRDIR/PACK0002/POLPACK0002.PAK");
        QVERIFY(QFileInfo(questionsPath).size() > 2048);
        QVERIFY(write(iso, isoImage({{"PS3_GAME/PARAM.SFO", paramSfo("Quiz TV")},
                                     {questionPack, bytes(questionsPath)},
                                     {"PS3_GAME/USRDIR/GAME/POL.PAK", bytes(gamePath)}}, questionPack)));
        auto imported = studio::ps3::importPs3(iso, "pl", false);
        QVERIFY2(imported.ok(), qPrintable(imported.error));
        QCOMPARE(imported.document.questions.size(), 1);
        auto &question = imported.document.questions.first();
        QCOMPARE(question.sourceCategoryPath(), (QStringList{"Music quiz", "Recent music"}));
        QCOMPARE(question.localized()["source_rounds"].toArray().size(), 2);
        QVERIFY(imported.warnings.join('\n').contains("unsupported.qak (QAK 1000"));
        QCOMPARE(studio::retainGameCategories(imported.document, 0, "pl", imported.warnings), 1);
        QCOMPARE(imported.document.categoryLabel(question.subjectId), QString("Music quiz"));
        QCOMPARE(imported.document.categoryLabel(question.categoryId), QString("Recent music"));
        const auto unsupported = temp.filePath("unsupported.pak");
        writeArchive(unsupported, {{"POL/ROUNDS/unsupported.qak", questions.last().second.left(4)}});
        const auto failure = studio::importBuzz(unsupported, "pl", false);
        QVERIFY(!failure.ok());
        QVERIFY(failure.error.contains("unsupported.pak / POL/ROUNDS/unsupported.qak"));
    }
    void previewsLegacyRsfNarrationAndConvertsItForQuizWorld() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto music = encodeStreamedMusicPcm(QByteArray(4096, '\0'), nullptr);
        const auto legacy = QByteArray::fromHex("06000101bb800001000000c0000000c0000000c0000000c00000000000000000")
                            + music.mid(32, 192);
        const auto path = temp.filePath("brain-narration.rsf");
        QVERIFY(write(path, legacy));
        const auto wave = previewAudioWave(path);
        QCOMPARE(u16(wave, 22), 1u);
        QCOMPARE(u16(wave, 32), 192u);
        QVERIFY(wave.endsWith(legacy.mid(32)));
        const auto ffmpeg = studio::defaultFfmpeg();
        if (!ffmpeg.isEmpty()) {
            const auto encoded = encodeSpeech(path, ffmpeg, 0, nullptr);
            validateRsfSpeech(encoded);
            QCOMPARE(u32(encoded, 16, true), 152u);
        }
        QVERIFY(write(path, legacy.chopped(1)));
        QVERIFY_EXCEPTION_THROWN(previewAudioWave(path), std::runtime_error);
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
    void recoversMusicFromQuizWorldIsoAndBuildsItSeparatelyFromNarration() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto header = QByteArray::fromHex("06000200bb800006000001800000030000000180000003000102000000000000");
        const auto first = QByteArray(384, 'a'), second = QByteArray(384, 'b');
        const auto clip = (header + first).leftJustified(65536, '\0') +
                          (header + second).leftJustified(65536, '\0');
        const auto speech = bytes(fixture("atrac-reference.rsf"));
        QVERIFY(!speech.isEmpty());
        const QStringList answers{"One", "Two", "Three", "Four"};
        const auto round = writeRound("q_music", "ENG", 100, 1, 1,
            {{1, 42, "Which song is this?", {}, answers, 2, 235, 237, 0, 0, FirstMusicAsset},
             {2, 42, "Which record is this?", {}, answers, 2, 235, 237, 0, 0, FirstMusicAsset},
             {3, 42, "Which band is this?", {}, answers, 2, 235, 237, 0, 0, FirstMusicAsset + 1}});
        const auto pack = temp.filePath("ENGPACK0100.PAK");
        writeArchive(pack, {{"ENG/ROUNDS/q_music.qak", round.little}, {"ENG/ROUNDS/q_music.hdr", round.header},
                            {"ENG/ROUNDS/q_music.hst", round.stream},
                            {"ENG/QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/6000000042.rsf", speech},
                            {musicEntry("ENG", FirstMusicAsset), clip}});
        const auto iso = temp.filePath("Quiz World.iso");
        QVERIFY(write(iso, isoImage({{"PS3_GAME/USRDIR/PACK0100/ENGPACK0100.PAK", bytes(pack)}})));
        auto imported = studio::ps3::importPs3(iso, "en");
        QVERIFY2(imported.ok(), qPrintable(imported.error));
        QCOMPARE(imported.document.questions.size(), 3);
        QCOMPARE(bytes(imported.document.questions[0].music), clip);
        QCOMPARE(imported.document.questions[1].music, imported.document.questions[0].music);
        QVERIFY(imported.document.questions[2].hasUnresolvedMusic());
        const auto metadataOnly = studio::ps3::importPs3(iso, "en", false);
        QVERIFY2(metadataOnly.ok(), qPrintable(metadataOnly.error));
        QVERIFY(metadataOnly.document.questions[0].hasUnresolvedMusic());
        for (auto &q : imported.document.questions) {
            q.music.clear();
            q.translations["en"] = q.localized();
        }
        auto oldVariant = imported.document.questions[1].localized();
        oldVariant["source_media"] = QJsonArray{QJsonObject{{"type", 1}, {"asset", FirstMusicAsset}}};
        imported.document.questions[1].translations["en"] = oldVariant;
        for (const auto &q : imported.document.questions) {
            QVERIFY(q.hasUnresolvedMusic());
            QVERIFY(!q.hasUnresolvedVisualMedia());
            QVERIFY(studio::hasBuzzSourceMusic(q));
        }

        QStringList warnings;
        studio::recoverBuzzMusic(imported.document.questions, warnings);
        QCOMPARE(warnings.size(), 1);
        const auto &questions = imported.document.questions;
        QCOMPARE(bytes(questions[0].music), clip);
        const auto musicWave = previewAudioWave(questions[0].music);
        QCOMPARE(u16(musicWave, 22), 2);
        QCOMPARE(musicWave.mid(60), first + second);
        QCOMPARE(questions[0].music, questions[1].music);
        QCOMPARE(bytes(questions[0].speech), speech);
        QCOMPARE(previewAudioWave(questions[0].speech).mid(60), speech.mid(32));
        QVERIFY(questions[0].problems().isEmpty());
        QVERIFY(questions[2].hasUnresolvedMusic());
        QCOMPARE(questions[2].problems(), QStringList{QString::fromUtf8("Brakuje klipu muzycznego do pytania.")});

        const auto project = temp.filePath("Music.buzzproject");
        QVERIFY(studio::exportProjectArchive(imported.document, project).ok());
        const auto reopened = studio::openProjectArchive(project);
        QVERIFY2(reopened.ok(), qPrintable(reopened.error));
        QCOMPARE(bytes(reopened.document.questions[0].music), clip);

        auto req = request(temp.path());
        req.document = reopened.document;
        req.skipInvalid = true;
        const auto built = studio::buildPack(req);
        QVERIFY2(built.ok(), qPrintable(built.error));
        QCOMPARE(built.questions, 2);
        QCOMPARE(built.excluded.size(), 1);
        QCOMPARE(built.excluded[0].toMap()["reason"].toString(), questions[2].problems().first());
        const auto builtPack = studio::importBuzz(built.path, "en");
        QVERIFY2(builtPack.ok(), qPrintable(builtPack.error));
        Archive archive(built.path);
        const QJsonValue asset = builtPack.document.questions[0].sourceMusic()["asset"];
        QVERIFY(asset.isDouble());
        QCOMPARE(archive.read(QString("ENG/QUESTIONASSETS/MUSIC/STREAMED/1%1.rsf").arg(asset.toInteger(), 6, 10, QLatin1Char('0'))),
                 clip);
        QCOMPARE(builtPack.document.questions[1].sourceMusic()["asset"], asset);
        QCOMPARE(bytes(builtPack.document.questions[0].speech), speech);
    }
    void encodesStereoMusicForQuizWorld() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        constexpr int samples = 48000 * 4;
        QByteArray pcm(samples * 4, '\0');
        for (int i = 0; i < samples; ++i) {
            const auto left = qint16(i % 100 < 50 ? 5000 : -5000);
            const auto right = qint16(i % 60 < 30 ? 3500 : -3500);
            qToLittleEndian(left, pcm.data() + i * 4);
            qToLittleEndian(right, pcm.data() + i * 4 + 2);
        }
        const auto clip = encodeStreamedMusicPcm(pcm);
        QCOMPARE(clip.size(), 2 * 65536);
        QVERIFY(isStreamedMusic(clip));
        QCOMPARE(u16(clip, 6, true), 2);
        const auto name = temp.filePath("music.rsf");
        QVERIFY(write(name, clip));
        const auto wave = previewAudioWave(name);
        QCOMPARE(u16(wave, 22), 2);
        QCOMPARE(u32(wave, 24), 48000u);
        QCOMPARE(wave.size(), 60 + u32(clip, 20, true));
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
    void roundTripsNarrationForTheRightQuestionAndLanguage() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto req = request(temp.path());
        const auto pcm = bytes(fixture("atrac-input.pcm"));
        QVERIFY(!pcm.isEmpty());
        int recording = 0;
        for (const QString &language : {QString("en"), QString("pl")}) {
            req.document.switchLanguage(language);
            QMap<QString, QByteArray> expected;
            for (auto &q : req.document.questions) {
                q.text = language + ' ' + q.id;
                q.answers = QStringList{"A", "B", "C", "D"};
                q.correct = 0;
                q.speech = temp.filePath(QString::number(++recording) + ".rsf");
                const auto audio = encodeAtracPcm(pcm.repeated(recording));
                QVERIFY(write(q.speech, audio));
                expected.insert(q.text, audio);
            }
            const auto built = studio::buildPack(req);
            QVERIFY2(built.ok(), qPrintable(built.error));
            const auto imported = studio::importBuzz(built.path, language);
            QVERIFY2(imported.ok(), qPrintable(imported.error));
            QCOMPARE(imported.document.questions.size(), expected.size());
            for (const auto &q : imported.document.questions) {
                QVERIFY(expected.contains(q.text));
                QVERIFY(!q.speech.isEmpty());
                QCOMPARE(bytes(q.speech), expected.value(q.text));
            }
        }
    }
    void customCategoriesAreNamedPerLanguageAndTravelWithTheProject() {
        QTemporaryDir temp;
        auto req = request(temp.path());
        auto &doc = req.document;
        doc.categories = {{10001, 0, QJsonObject{{"en", "Harry Potter"}, {"pl", "Harry Potter"}}},
                          {10002, 10001, QJsonObject{{"en", "Spells"}, {"pl", QString::fromUtf8("Zaklęcia")}}}};
        doc.questions[0].subjectId = 10001;
        doc.questions[0].categoryId = 10002;
        doc.title = "Wizarding quiz";
        doc.refreshCategoryNames();
        QCOMPARE(doc.questions[0].category, QString("Harry Potter"));
        // The project keeps the categories and one title per language.
        auto saved = studio::parseJson(studio::serialize(doc));
        QVERIFY2(saved.ok(), qPrintable(saved.error));
        QVERIFY(saved.document.categories == doc.categories);
        saved.document.switchLanguage("pl");
        QCOMPARE(saved.document.title, QString("Wizarding quiz"));
        saved.document.title = QString::fromUtf8("Quiz czarodziejów");
        QCOMPARE(saved.document.questions[0].category, QString("Harry Potter"));
        auto reloaded = studio::parseJson(studio::serialize(saved.document), {}, "en").document;
        QCOMPARE(reloaded.title, QString("Wizarding quiz"));
        reloaded.switchLanguage("pl");
        QCOMPARE(reloaded.title, QString::fromUtf8("Quiz czarodziejów"));
        QVERIFY_EXCEPTION_THROWN(studio::serialize(doc, {}, true), std::runtime_error);

        const auto built = studio::buildPack(req);
        QVERIFY2(built.ok(), qPrintable(built.error));
        QCOMPARE(built.rounds, 2);
        QCOMPARE(built.categories, 4);
        Archive archive(built.path);
        QMap<QString, studio::pack::Category> byKey;
        for (const auto &c : studio::pack::readCategories(
                 archive.read("ENG/SCRIPTS/GENERATED/questioncategories.clu")))
            byKey[c.key] = c;
        QVERIFY(byKey.contains("CAT_PK103_OWN1"));
        QVERIFY(byKey.contains("CAT_PK103_OWN2"));
        QCOMPARE(byKey["CAT_PK103_OWN1"].name, QString("p103_OwnA"));
        QCOMPARE(byKey["CAT_PK103_OWN2"].parent, byKey["CAT_PK103_OWN1"].id);
        QVERIFY(archive.entries.contains("ENG/ROUNDS/p103_owna.qak"));
        // The game shows the names from NAMEDTEXT; reading the pack back resolves them too.
        const auto imported = studio::importBuzz(built.path, "en", false);
        QVERIFY2(imported.ok(), qPrintable(imported.error));
        bool found = false;
        for (const auto &q : imported.document.questions)
            if (q.text == doc.questions[0].text) {
                found = true;
                QCOMPARE(q.category, QString("Harry Potter"));
            }
        QVERIFY(found);

        // A language without a category name stops that language's build.
        req.document.categories[1].names.remove("pl");
        req.document.switchLanguage("pl");
        req.document.questions[0].text = QString::fromUtf8("Które zaklęcie otwiera zamki?");
        req.document.questions[0].answers = QStringList{"Alohomora", "Lumos", "Accio", "Nox"};
        const auto missing = studio::buildPack(req);
        QVERIFY(!missing.ok());
        QVERIFY2(missing.error.contains(studio::languageName("pl")), qPrintable(missing.error));
    }
    void buildsAnImportedCategoryWithoutManualMappingOrTranslation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto req = request(temp.path());
        req.document.questions.resize(1);
        auto &question = req.document.questions.first();
        question.source = "Knowledge Is Power";
        question.subjectId = question.categoryId = 0;
        question.extra["source_category_path"] = QJsonArray{"Experience", "Science", "General Science"};
        QStringList warnings;
        QCOMPARE(studio::retainGameCategories(req.document, 0, "pl", warnings), 1);
        QVERIFY(warnings.isEmpty());
        QCOMPARE(req.document.categories.size(), 2);
        const int subject = question.subjectId;
        QCOMPARE(question.category, QString("Experience"));
        QCOMPARE(req.document.customCategory(question.categoryId)->name("en"), QString("Science / General Science"));
        const auto saved = studio::parseJson(studio::serialize(req.document));
        QVERIFY2(saved.ok(), qPrintable(saved.error));
        QVERIFY(saved.document.categories == req.document.categories);
        req.document = saved.document;
        const auto built = studio::buildPack(req);
        QVERIFY2(built.ok(), qPrintable(built.error));
        const auto reopened = studio::importBuzz(built.path, "en", false);
        QVERIFY2(reopened.ok(), qPrintable(reopened.error));
        QCOMPARE(reopened.document.questions.first().sourceCategoryPath(),
                 QStringList({"Experience", "Science / General Science"}));

        auto other = req.document.questions.first();
        other.id = "another";
        other.extra.remove("category_override");
        req.document.questions << other;
        QCOMPARE(studio::retainGameCategories(req.document, 1, "en", warnings), 1);
        QCOMPARE(req.document.categories.size(), 2);
        QCOMPARE(req.document.questions.last().subjectId, subject);
    }
    void mergingProjectsReusesCategoriesWithTheSameNames() {
        studio::Document into;
        into.categories = {{10001, 0, QJsonObject{{"pl", "Święta"}}}, {10002, 10001, QJsonObject{{"pl", "Kolędy"}}}};
        const QVector<studio::CustomCategory> incoming{{10001, 0, QJsonObject{{"pl", "Święta"}}},
                                                       {10005, 10001, QJsonObject{{"pl", "Potrawy"}}},
                                                       {10002, 0, QJsonObject{{"pl", "Harry Potter"}}}};
        const auto remap = studio::mergeCustomCategories(into, incoming);
        QCOMPARE(remap[10001], 10001);
        QCOMPARE(into.categories.size(), 4);
        QCOMPARE(into.customCategory(remap[10005])->parent, 10001);
        QVERIFY(remap[10002] != 10002);
        QCOMPARE(into.customCategory(remap[10002])->name("pl"), QString("Harry Potter"));
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
    void partialBuildReportsBadQuestionsAndPreservesTheLastPackWhenNoneAreReady() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto req = request(temp.path());
        req.skipInvalid = true;
        const auto valid = req.document.questions[0];
        req.document.questions = {valid};
        auto append = [&](const QString &id) -> studio::Question & {
            auto bad = valid;
            bad.id = id;
            req.document.questions << bad;
            return req.document.questions.last();
        };
        append("incomplete").answers[0].clear();
        append("unsupported-text").text += QString::fromUcs4(U"\U0001F642");
        append("missing-category").categoryId = 0;
        append("missing-image").image = temp.filePath("missing.png");
        const auto badSpeech = temp.filePath("invalid.rsf");
        QVERIFY(write(badSpeech, "invalid RSF"));
        append("broken-recording").speech = badSpeech;
        auto &missingTranslation = append("no-language");
        missingTranslation.switchLanguage("fr");
        missingTranslation.translations.remove("en");

        const auto readiness = studio::checkBuildReadiness(req.document);
        QCOMPARE(readiness.ready, 2); // The invalid RSF is detected when build reads the file.
        QCOMPARE(readiness.issues.size(), 4);
        for (const auto &issue : readiness.issues)
            QVERIFY(!issue.id.isEmpty() && !issue.reason.isEmpty());
        QCOMPARE(readiness.missingLanguage, 1);
        const auto built = studio::buildPack(req);
        QVERIFY2(built.ok(), qPrintable(built.error));
        QCOMPARE(built.questions, 1);
        QCOMPARE(built.skipped, 1);
        QCOMPARE(built.excluded.size(), 5);
        for (const auto &entry : built.excluded) {
            const auto row = entry.toMap();
            QVERIFY(!row["id"].toString().isEmpty());
            QVERIFY(!row["reason"].toString().isEmpty());
        }
        QCOMPARE(built.excluded.last().toMap()["id"].toString(), QString("broken-recording"));
        const auto reopened = studio::importBuzz(built.path, "en", false);
        QVERIFY2(reopened.ok(), qPrintable(reopened.error));
        QCOMPARE(reopened.document.questions.size(), 1);
        QCOMPARE(reopened.document.questions[0].text, valid.text);
        const auto previous = bytes(built.path);

        req.document.questions.removeFirst();
        const auto empty = studio::buildPack(req);
        QVERIFY(!empty.ok());
        QVERIFY(empty.path.isEmpty());
        QCOMPARE(empty.excluded.size(), 5);
        QCOMPARE(bytes(built.path), previous);

        req.document.questions = {valid};
        const auto wave = temp.filePath("voice.wav");
        QVERIFY(write(wave, "invalid WAV"));
        append("requires-ffmpeg").speech = wave;
        req.ffmpeg = temp.filePath("missing-ffmpeg");
        const auto missingTool = studio::buildPack(req);
        QVERIFY(!missingTool.ok());
        QVERIFY(missingTool.excluded.isEmpty());
        QCOMPARE(bytes(built.path), previous);
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
        QSettings().setValue("build/titleId", "BCES00647");
        studio::setGameDirectory(game);
        QCOMPARE(studio::suggestBuildSettings("en")["titleId"].toString(), QString("BCES00647"));
        const auto sfo = runtime + "/disc/PS3_GAME/PARAM.SFO";
        QVERIFY(write(sfo, paramSfo("BLUS00000", "TITLE_ID")));
        QCOMPARE(studio::suggestBuildSettings("en")["titleId"].toString(), QString("BCES00647"));
        QVERIFY(write(sfo, paramSfo("BCUS98209", "TITLE_ID")));
        const auto english = studio::suggestBuildSettings("en");
        QCOMPARE(english["titleId"].toString(), QString("BCUS98209"));
        QCOMPARE(QFileInfo(english["runtimeRoot"].toString()).canonicalFilePath(),
                 QFileInfo(runtime).canonicalFilePath());
        QCOMPARE(QFileInfo(english["templatePath"].toString()).fileName(), QString("ENGPACK0100.PAK"));
        QCOMPARE(QFileInfo(studio::suggestBuildSettings("pl")["templatePath"].toString()).fileName(),
                 QString("POLPACK0100.PAK"));
        studio::setGameDirectory({});
        QSettings().remove("build");
        const auto built = studio::buildPack(request(temp.filePath("out")));
        QVERIFY2(built.ok(), qPrintable(built.error));
        const auto installed = studio::installPack(built.path, english["runtimeRoot"].toString(),
                                                   english["titleId"].toString());
        QVERIFY2(installed.ok(), qPrintable(installed.error));
        QVERIFY(installed.installedPath.contains("/dev_hdd0/game/BCUS98209/USRDIR/PACK0103/"));
        QVERIFY(!QDir(runtime + "/dev_hdd0/game/BCES00647").exists());
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
    void buildsAQuestionMovieWithAudio() {
        const auto ffmpeg = studio::defaultFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("FFmpeg not installed.");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto req = request(temp.filePath("out"));
        // Synthetic SDI metadata and a generated clip: no game media in this test.
        QByteArray movieTemplate(5 * 2048, '\0');
        const QVector<int> header = {0, 48000, 2, 0xa00, 640, 368, 640 * 368, 29970, 6000000,
                                     0x50, 2048, 2048, int(movieTemplate.size()), 0, 1601, 12808};
        for (int i = 0; i < header.size(); ++i)
            patch(movieTemplate, i * 4, header[i], 4, true);
        movieTemplate.replace(64, 8, "SDIVIDEO");
        auto checksumInput = movieTemplate.left(0x4c);
        checksumInput.replace(56, 4, movieTemplate.mid(60, 4));
        checksumInput.replace(60, 4, movieTemplate.mid(56, 4));
        patch(movieTemplate, 76, crc32(checksumInput) ^ 0xffffffffu ^ 0xfabcdef7u, 4, true);
        for (int i = 0; i < 4; ++i)
            movieTemplate.replace((i + 1) * 2048, 12, QByteArray::fromHex("000001ba2100010001882705"));
        movieTemplate.replace(2048 + 12, 15, QByteArray::fromHex("000001bb00098827050620ffc0c004"));
        movieTemplate.replace(4096 + 12, 15, QByteArray::fromHex("000001bb00098827050221ffe0e02e"));
        movieTemplate.replace(6144 + 12, 6, QByteArray::fromHex("000001bf07ee"));
        movieTemplate.replace(6144 + 32, 12, "SofdecStream");
        movieTemplate.replace(8192 + 12, 6, QByteArray::fromHex("000001bf07ee"));
        movieTemplate.replace(8192 + 18, 7, "CRITAGS");
        QVERIFY(isMovieTemplate(movieTemplate));
        Archive stock(req.templatePath);
        Entries files;
        for (const auto &path : stock.entries.keys())
            files << qMakePair(path, stock.read(path));
        files << qMakePair(QString("ENG/QUESTIONASSETS/VIDEO/1000001.sdi"), movieTemplate);
        req.templatePath = temp.filePath("template.pak");
        writeArchive(req.templatePath, files);
        const auto clip = temp.filePath("clip.mp4");
        runMedia(ffmpeg, {"-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=160x120:rate=25",
                          "-itsoffset", "0.25", "-f", "lavfi", "-i", "sine=frequency=880:sample_rate=48000", "-t", "1",
                          "-c:v", "mpeg4", "-c:a", "aac", clip}, nullptr);
        auto &q = req.document.questions[0];
        q.video = clip;
        q.image = temp.filePath("unused-poster.png"); // A movie supersedes the still.
        q.extra["source_media"] = QJsonArray{QJsonObject{{"type", "ps2-video"}}};
        auto broken = q;
        broken.id = "broken-video";
        broken.video = temp.filePath("broken.mp4");
        QVERIFY(write(broken.video, "not a video"));
        req.document.questions.prepend(broken);
        req.skipInvalid = true;
        const auto built = studio::buildPack(req);
        QVERIFY2(built.ok(), qPrintable(built.error));
        QCOMPARE(built.movies, 1);
        QCOMPARE(built.pictures, 0);
        QCOMPARE(built.excluded.size(), 1);
        QCOMPARE(built.excluded.first().toMap()["id"].toString(), broken.id);
        Archive output(built.path);
        const auto movie = output.read("ENG/QUESTIONASSETS/VIDEO/1021024.sdi");
        QVERIFY(isMovieTemplate(movie));
        QCOMPARE(u32(movie, 52, true), 30u);
        const auto sdi = temp.filePath("movie.sdi"), audio = temp.filePath("decoded.pcm");
        QVERIFY(write(sdi, movie));
        runMedia(ffmpeg, {"-v", "error", "-xerror", "-y", "-i", sdi, "-map", "0:a:0", "-f", "s16le",
                          audio, "-map", "0:v:0", "-f", "null", "-"}, nullptr);
        const auto verifyAudioTiming = [&](const QByteArray &samples) {
            QVERIFY(samples.size() > 180000);
            constexpr int stereoFrameBytes = 4, sampleRate = 48000;
            qsizetype offset = 0;
            for (; offset + stereoFrameBytes <= samples.size(); offset += stereoFrameBytes)
                if (std::abs(int(qFromLittleEndian<qint16>(samples.constData() + offset))) > 1000)
                    break;
            const double onset = double(offset) / (stereoFrameBytes * sampleRate);
            QVERIFY2(onset >= 0.215 && onset <= 0.285, qPrintable(QString::number(onset)));
        };
        verifyAudioTiming(bytes(audio));
        const auto imported = studio::importBuzz(built.path, "en", false);
        QVERIFY(imported.ok());
        const auto &questions = imported.document.questions;
        const auto importedMovie = std::find_if(questions.cbegin(), questions.cend(), [&](const auto &item) {
            return item.text == req.document.questions[1].text;
        });
        QVERIFY(importedMovie != questions.cend());
        const auto reference = importedMovie->extra["source_media"].toArray();
        QVERIFY(!reference.isEmpty());
        QCOMPARE(reference[0].toObject()["type"].toInt(), 2);
        QCOMPARE(reference[0].toObject()["slot"].toInt(), 0);
        QCOMPARE(reference[0].toObject()["asset"].toInt(), FirstMovieAsset + 2 * MovieAssetsPerPack);
        QVERIFY(studio::hasSourceQuestionMedia(*importedMovie));
        QVERIFY(!studio::sourceQuestionClipKey(*importedMovie).isEmpty());
        const auto decoded = studio::decodeSourceQuestionMedia(*importedMovie, ffmpeg);
        QVERIFY2(decoded.error.isEmpty(), qPrintable(decoded.error));
        const QImage poster(decoded.image);
        QCOMPARE(poster.size(), QSize(640, 360));
        const auto converted = temp.filePath("converted.pcm");
        runMedia(ffmpeg, {"-v", "error", "-xerror", "-y", "-i", decoded.video, "-map", "0:a:0",
                          "-f", "s16le", converted, "-map", "0:v:0", "-f", "null", "-"}, nullptr);
        verifyAudioTiming(bytes(converted));
    }
    void hollywoodMapsPicturesByQuestion() {
        QTemporaryDir temp;
        const auto path = temp.filePath("POLROUND.PAK");
        const QByteArray adt = "Id\tResource\tQuestion\tRight Answer\tWrong Answer\tWrong Answer\tWrong Answer\n"
                               "71\t123\tCo jest na obrazku?\tA\tB\tC\tD\n"
                               "72\t123\tCo jest na obrazku?\tA\tB\tC\tD\n"
                               "73\t456\tTrzecie pytanie?\tA\tB\tC\tD\n";
        constexpr int table = 0x17c, media = table + 24, questions = media + 60, links = questions + 108;
        QByteArray qak(links + 12, '\0');
        patch(qak, 0, 0xfffe, 2);
        patch(qak, 4, 1000, 4);
        patch(qak, 8, 3, 4);
        patch(qak, 12, table, 4);
        for (int i = 0; i < 3; ++i) {
            patch(qak, table + i * 8, i == 0 ? media : i == 1 ? questions : links, 4);
            patch(qak, table + i * 8 + 4, (i << 16) | 3, 4);
            patch(qak, media + i * 20 + 4, i == 2 ? 456 : 123, 4);
            patch(qak, media + i * 20 + 14, i == 2 ? 0 : i + 7, 2);
            qak[media + i * 20 + 16] = i == 2 ? 2 : 3;
            patch(qak, questions + i * 36, 71 + i, 4);
            patch(qak, questions + i * 36 + 12, 2 - i, 2);
            patch(qak, links + (2 - i) * 4, i, 2);
        }
        writeArchive(path, {{"BHW/SK_POL/Rounds/POL/quickfire.adt", adt},
                            {"BHW/SK_POL/Rounds/POL/quickfire.qak", qak}});
        const auto result = studio::importLegacy(path, "pl");
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.document.questions.size(), 3);
        for (int i = 0; i < 3; ++i) {
            const auto ref = result.document.questions[i].extra["source_media"].toArray()[0].toObject();
            if (i < 2) {
                QCOMPARE(ref["type"].toString(), QString("ps2-picture"));
                QCOMPARE(ref["frame"].toInt(), i + 7);
                QCOMPARE(ref["container"].toString(), QString("BHW/SK_POL/VIDEOS.PAK:Videos/pics001.rsv"));
            } else {
                QCOMPARE(ref["type"].toString(), QString("ps2-video"));
                QCOMPARE(ref["container"].toString(), QString("BHW/SK_POL/VIDEOS.PAK:Videos/1000456.rsv"));
                QVERIFY(!ref.contains("frame"));
            }
        }
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
        auto broken = req.document.questions[0];
        broken.id = "broken-image";
        broken.image = temp.filePath("broken.png");
        QVERIFY(write(broken.image, "not an image"));
        req.document.questions.prepend(broken);
        req.skipInvalid = true;
        const auto built = studio::buildPack(req);
        QVERIFY2(built.ok(), qPrintable(built.error));
        QCOMPARE(built.pictures, 1);
        QCOMPARE(built.excluded.size(), 1);
        QCOMPARE(built.excluded.first().toMap()["id"].toString(), broken.id);
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
        QVERIFY(imported.document.questions[0].image.isEmpty());
        const auto withMedia = studio::importBuzz(built.path, "en");
        const auto warnings = withMedia.warnings.join('\n');
        QVERIFY2(withMedia.ok(), qPrintable(withMedia.error));
        QCOMPARE(withMedia.document.questions.size(), 1);
        const auto &question = withMedia.document.questions[0];
        QVERIFY2(!question.image.isEmpty(), qPrintable(warnings));
        QCOMPARE(question.translations["en"].toObject()["image"].toString(), question.image);
        QCOMPARE(question.extra["source_media"], imported.document.questions[0].extra["source_media"]);
        QCOMPARE(QImage(question.image), frame);
    }
};
QTEST_GUILESS_MAIN(BuilderTest)
#include "builder_test.moc"
