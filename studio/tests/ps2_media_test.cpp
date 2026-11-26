#include "ps2_media.h"
#include "pack_format.h"
#include <QJsonArray>
#include <QtTest>

namespace {
constexpr auto audioBytes = 1136;
const QByteArray first = QByteArray::fromHex("7143fc000001b0");
const QByteArray next = QByteArray::fromHex("7143de000001b0");
QByteArray rsv(bool video) {
    QByteArray out(40, '\0');
    out.replace(0, 4, "rsv1");
    auto set = [&](int off, quint32 value) { qToLittleEndian(value, out.data() + off); };
    qToLittleEndian<quint16>(320, out.data() + 8);
    qToLittleEndian<quint16>(240, out.data() + 10);
    set(12, 2);
    set(16, 25);
    set(20, video ? 24000 : 0);
    set(24, video ? 0x10000b : 0x100000);
    set(28, video ? 1 : 0);
    for (int i = 0; i < 2; ++i) {
        set(32 + i * 4, quint32(out.size()));
        if (video) {
            QByteArray audio(audioBytes, '\0');
            qToLittleEndian<quint16>(960, audio.data());
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
        clip[50] = 0x5c;
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(clip, std::nullopt, &pcm), std::runtime_error);
        QVERIFY(pcm.isEmpty());
    }
    void invalidLayout() {
        auto data = rsv(false);
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(data, 2), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(data, -1), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(studio::makePs2Ipu(data, std::nullopt), std::runtime_error);
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
    }
};

QTEST_GUILESS_MAIN(Ps2MediaTest)
#include "ps2_media_test.moc"
