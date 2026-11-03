#include "buzz_import.h"
#include "pack_builder.h"
#include "pack_codec.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

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
  private slots:
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
