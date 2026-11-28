#include "ps2_media.h"
#include "pack_format.h"
#include "pack_codec.h"
#include <QJsonArray>
#include <QtTest>

namespace {
constexpr auto audioBytes = 1136;
const QByteArray first = QByteArray::fromHex("7143fc000001b0");
const QByteArray next = QByteArray::fromHex("7143de000001b0");
QByteArray rsv(bool video, int fps = 25) {
    QByteArray out(40, '\0');
    out.replace(0, 4, "rsv1");
    auto set = [&](int off, quint32 value) { qToLittleEndian(value, out.data() + off); };
    qToLittleEndian<quint16>(320, out.data() + 8);
    qToLittleEndian<quint16>(240, out.data() + 10);
    set(12, 2);
    set(16, fps);
    set(20, video ? 24000 : 0);
    set(24, video ? 0x10000b : 0x100000);
    set(28, video ? 1 : 0);
    for (int i = 0; i < 2; ++i) {
        set(32 + i * 4, quint32(out.size()));
        if (video) {
            QByteArray audio(fps == 29 ? 944 : audioBytes, '\0');
            qToLittleEndian<quint16>(fps == 29 ? 801 : 960, audio.data());
            out.append(audio);
        }
        out.append(i == 0 ? first : next);
        out.append(9, '\0');
    }
    set(4, quint32(out.size() - 32));
    return out;
}
} // namespace

class Ps2MediaTest final : public QObject {
    Q_OBJECT
  private slots:
    void still() {
        const auto result = studio::makePs2Ipu(rsv(false), 1);
        QCOMPARE(result.left(4), QByteArray("ipum"));
        QCOMPARE(studio::pack::u32(result, 4), quint32(next.size()));
        QCOMPARE(studio::pack::u32(result, 12), 1u);
        QCOMPARE(result.mid(16), next);
    }
    void video() {
        auto clip = rsv(true);
        clip[50] = 0x1c; // Left: predictor 1, shift 12, previous sample 64.
        clip[42] = 64;
        clip[66] = 0x0c; // Right: predictor 0, shift 12.
        clip[68] = char(0xe1);
        QByteArray pcm;
        const auto result = studio::makePs2Ipu(clip, std::nullopt, &pcm);
        QCOMPARE(studio::pack::u32(result, 12), 2u);
        QCOMPARE(result.mid(16), first + next);
        QCOMPARE(pcm.size(), 2 * 960 * 4);
        QCOMPARE(studio::pack::u16(pcm, 0), 60u);
        QCOMPARE(studio::pack::u16(pcm, 2), 1u);
        QCOMPARE(studio::pack::u16(pcm, 4), 56u);
        QCOMPARE(studio::pack::u16(pcm, 6), 65534u);
        QCOMPARE(studio::pack::u32(pcm, 960 * 4), 0u);
        // Hollywood uses the row-major layout identifier, with the same audio.
        qToLittleEndian<quint32>(0, clip.data() + 28);
        QCOMPARE(studio::makePs2Ipu(clip, std::nullopt, &pcm), result);
        QCOMPARE(studio::pack::u16(pcm, 0), 60u);
        clip[50] = 0x5c;
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(clip, std::nullopt, &pcm), std::runtime_error);
        QVERIFY(pcm.isEmpty());
    }
    void invalidLayout() {
        auto data = rsv(false);
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(data, 2), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(data, -1), std::runtime_error);
        auto unknownLayout = data;
        qToLittleEndian<quint32>(0, unknownLayout.data() + 24);
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(unknownLayout, std::nullopt), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(data.chopped(1), 0), std::runtime_error);
        qToLittleEndian<quint32>(0, data.data() + 36);
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(data, 0), std::runtime_error);
        auto clip = rsv(true);
        clip[40] = 0;
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(clip, std::nullopt), std::runtime_error);
        clip = rsv(true);
        clip[40 + audioBytes + first.size() - 1] = 1;
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(clip, std::nullopt), std::runtime_error);
    }
    void ntscVideo() {
        QByteArray pcm;
        const auto result = studio::makePs2Ipu(rsv(true, 29), std::nullopt, &pcm);
        QCOMPARE(result.mid(16), first + next);
        QCOMPARE(pcm.size(), 2 * 801 * 4);
    }
    void reportsAnOutOfRangePictureWithItsQuestionId() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto pack = QStringLiteral("BHW/TEST/VIDEOS.PAK");
        const auto member = QStringLiteral("Videos/pics001.rsv");
        QVERIFY(QDir().mkpath(temp.filePath("BHW/TEST")));
        studio::pack::writeArchive(temp.filePath(pack), {{member, rsv(false)}});
        auto q = studio::newQuestion();
        q.id = "missing-picture-id";
        q.extra["source_media"] = QJsonArray{QJsonObject{{"type", "ps2-picture"}, {"archive", temp.path()},
                                                        {"frame", 3}, {"container", pack + ':' + member}}};
        QVector<studio::Question> questions{q};
        QStringList warnings;
        QCOMPARE(studio::importPs2Pictures(questions, "unused", warnings), 0);
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.first().contains("Videos/pics001.rsv [3] · missing-picture-id"));
    }
    void readsSportsArchiveEntries() {
        QByteArray bpak(28 + 16, '\0');
        bpak.replace(0, 4, "BPAK");
        qToLittleEndian<quint32>(1, bpak.data() + 16);
        qToLittleEndian<quint32>(28, bpak.data() + 20);
        qToLittleEndian<quint32>(44, bpak.data() + 32);
        bpak.append("questionvideos/sample.ipu", 26); // Include the terminator.
        const auto start = bpak.size();
        qToLittleEndian<quint32>(start, bpak.data() + 4);
        qToLittleEndian<quint32>(start, bpak.data() + 36);
        qToLittleEndian<quint32>(first.size(), bpak.data() + 40);
        bpak += first;
        studio::pack::Archive archive(bpak);
        QCOMPARE(archive.read("questionvideos/sample.ipu"), first);
        qToLittleEndian<quint32>(start + 1, bpak.data() + 36);
        QVERIFY_EXCEPTION_THROWN(studio::pack::Archive{bpak}, std::runtime_error);
    }
    void rejectsOtherReferences() {
        studio::Question q;
        auto ref = QJsonObject{{"type", "ps2-picture"}, {"frame", 1},
                               {"container", "BG2/SK_BEN/VIDEOS.PAK:Videos/pics001.rsv"}};
        q.extra["source_archive"] = "disc.iso";
        q.extra["source_media"] = QJsonArray{ref};
        QVERIFY(studio::hasPs2Media(q));
        ref["container"] = "BG2/../VIDEOS.PAK:Videos/pics001.rsv";
        q.extra["source_media"] = QJsonArray{ref};
        QVERIFY(!studio::hasPs2Media(q));
        ref["container"] = "BG2/SK_BEN/VIDEOS.PAK:Videos/1002481.rsv";
        ref["type"] = "ps2-video";
        q.extra["source_media"] = QJsonArray{ref};
        QVERIFY(studio::hasPs2Media(q));
        ref["container"] = "QVIDEOS.PAK:questionvideos/82_f_ita_ger-01.ipu";
        q.extra["source_media"] = QJsonArray{ref};
        QVERIFY(studio::hasPs2Media(q));
        ref["type"] = "ps2-picture";
        q.extra["source_media"] = QJsonArray{ref};
        QVERIFY(!studio::hasPs2Media(q));
    }
};

QTEST_GUILESS_MAIN(Ps2MediaTest)
#include "ps2_media_test.moc"
