#include "ps2_media.h"
#include "disc_image.h"
#include "pack_format.h"
#include "pack_media.h"
#include "ps3_source.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace studio {
namespace {
using namespace pack;

struct Reference {
    QString archive, path, entry;
    int frame = -1;
    bool video = false;
};

Reference reference(const Question &q) {
    static const QRegularExpression container(
        R"(^((?:BG2/[A-Z0-9_]+/)VIDEOS\.PAK):(Videos/(?:pics001|10[0-9]{5})\.rsv)$)",
        QRegularExpression::CaseInsensitiveOption);
    const auto media = q.translations.isEmpty() ? q.extra["source_media"].toArray()
                                                 : q.localized()["source_media"].toArray();
    for (const auto &item : media) {
        const auto ref = item.toObject();
        const bool video = ref["type"] == "ps2-video";
        if (!video && ref["type"] != "ps2-picture")
            continue;
        const auto match = container.match(ref["container"].toString());
        const int frame = ref["frame"].toInt(-1);
        if (!match.hasMatch() || (!video && frame < 0) ||
            (video == match.captured(2).endsWith("/pics001.rsv", Qt::CaseInsensitive)))
            continue;
        const auto archive = ref["archive"].toString(q.extra["source_archive"].toString());
        if (!archive.isEmpty())
            return {archive, match.captured(1), match.captured(2), frame, video};
    }
    return {};
}

// The video PAK can be over a GiB. Read just the central directory and the
// selected entry, using a bounded view of the ISO extent.
class IsoExtent final : public ps3::RangeDevice {
  public:
    IsoExtent(const QString &path, DiscImage::File extent)
        : RangeDevice(extent.size), file_(path), base_(extent.offset) {
        if (!file_.open(QIODevice::ReadOnly))
            fail(file_.errorString());
        if (base_ < 0 || base_ > file_.size() || extent.size < 0 || extent.size > file_.size() - base_)
            fail(QStringLiteral("The video archive extent is outside the disc image."));
    }
  protected:
    QByteArray readRange(qint64 offset, qint64 length) override {
        if (!file_.seek(base_ + offset))
            return {};
        return file_.read(length);
    }
  private:
    QFile file_;
    qint64 base_;
};

QByteArray readRsv(const Reference &ref) {
    const QFileInfo source(ref.archive);
    std::unique_ptr<Archive> archive;
    if (source.suffix().compare("iso", Qt::CaseInsensitive) == 0) {
        DiscImage disc(ref.archive);
        const auto it = disc.files().constFind(ref.path.toUpper());
        if (it == disc.files().constEnd())
            fail(QStringLiteral("The source disc does not contain the referenced video archive."));
        archive = std::make_unique<Archive>(std::make_unique<IsoExtent>(ref.archive, it.value()));
    } else {
        // A loose language PAK is one directory below the extracted disc root.
        QDir root = source.dir();
        if (!root.cdUp() || source.suffix().compare("pak", Qt::CaseInsensitive) != 0)
            fail(QStringLiteral("Open the PS2 disc image or a round PAK from an extracted PS2 disc."));
        archive = std::make_unique<Archive>(root.filePath(ref.path));
    }
    const auto it = archive->entries.constFind(ref.entry);
    if (it == archive->entries.constEnd() || it->size > 64 * 1024 * 1024)
        fail(QStringLiteral("The referenced RSV is missing or too large."));
    return archive->read(ref.entry);
}

constexpr int audioSamples = 960, audioBytes = 1136;
constexpr int blocks = 35, blockBytes = 16, samplesPerBlock = 28;

int signed16(quint16 value) { return value < 0x8000 ? int(value) : int(value) - 0x10000; }
int divideFloor(int value, int divisor) {
    return value >= 0 ? value / divisor : (value - (divisor - 1)) / divisor;
}

QByteArray decodeAudio(const QByteArray &rsv, qint64 start) {
    // RSV resets the two-sample history independently per channel and video
    // frame. There are 35 pairs of alternating PS-ADPCM blocks, of which
    // the last pair carries 20 padding samples per channel.
    static constexpr int prediction[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    QByteArray pcm(audioSamples * 4, '\0');
    for (int channel = 0; channel < 2; ++channel) {
        int history = signed16(u16(rsv, start + 2 + channel * 4));
        int older = signed16(u16(rsv, start + 4 + channel * 4));
        for (int i = 0; i < blocks; ++i) {
            const char *b = rsv.constData() + start + 10 + (i * 2 + channel) * blockBytes;
            const int filter = quint8(b[0]) / 16, shift = quint8(b[0]) % 16;
            if (filter >= 5 || shift > 12 || b[1] != 0)
                fail(QStringLiteral("Unsupported PS2 RSV audio block header."));
            for (int j = 0; j < samplesPerBlock && i * samplesPerBlock + j < audioSamples; ++j) {
                const int nibble = (quint8(b[2 + j / 2]) >> (4 * (j % 2))) & 15;
                const int delta = divideFloor((nibble < 8 ? nibble : nibble - 16) * 4096, 1 << shift);
                const int estimate = divideFloor(history * prediction[filter][0] +
                                                     older * prediction[filter][1] + 32,
                                                 64);
                const int sample = std::clamp(delta + estimate, -32768, 32767);
                const int index = (i * samplesPerBlock + j) * 4 + channel * 2;
                qToLittleEndian<quint16>(quint16(sample), pcm.data() + index);
                older = history;
                history = sample;
            }
        }
    }
    return pcm;
}

} // namespace

QByteArray makePs2Ipu(const QByteArray &rsv, std::optional<int> pictureFrame, QByteArray *pcm) {
    if (pcm)
        pcm->clear();
    QByteArray audio;
    const bool video = !pictureFrame.has_value();
    check(rsv, 0, 32);
    const auto width = u16(rsv, 8), height = u16(rsv, 10);
    const auto frames = u32(rsv, 12);
    const auto fps = u32(rsv, 16), sampleRate = u32(rsv, 20), flags = u32(rsv, 24);
    // Retail Mega Quiz RSV v1 (PAL). Do not guess layouts for other variants.
    const bool known = video ? fps == 25 && sampleRate == 24000 && flags == 0x10000b && u32(rsv, 28) == 1
                             : sampleRate == 0 && flags == 0x100000 && u32(rsv, 28) == 0;
    if (!rsv.startsWith("rsv1") || u32(rsv, 4) != rsv.size() - 32 || !known ||
        width != 320 || height != 240 || frames == 0 || frames > 5000)
        fail(QStringLiteral("Unsupported PS2 RSV layout."));
    const qint64 dataStart = 32 + qint64(frames) * 4;
    check(rsv, 0, dataStart);
    const quint32 first = quint32(pictureFrame.value_or(0));
    if (first >= frames)
        fail(QStringLiteral("The referenced PS2 picture frame is outside the sheet."));
    const quint32 count = video ? frames : 1u;
    QByteArray payload;
    for (quint32 i = first; i < first + count; ++i) {
        auto start = qint64(u32(rsv, 32 + i * 4));
        const qint64 end = i + 1 < frames ? u32(rsv, 32 + (i + 1) * 4) : rsv.size();
        if (start < dataStart || end <= start || end > rsv.size())
            fail(QStringLiteral("Invalid PS2 RSV frame offsets."));
        if (video) {
            // This layout places a 1136-byte audio block before each IPU frame.
            // The first word is the 960 samples at 24 kHz / 25 fps.
            if (end - start <= audioBytes || u16(rsv, start) != audioSamples)
                fail(QStringLiteral("Unsupported PS2 RSV audio block layout."));
            if (pcm)
                audio += decodeAudio(rsv, start);
            start += audioBytes;
        }
        // FFmpeg's IPU parser expects the next frame immediately after its
        // sequence-end code, not the RSV container's zero alignment bytes.
        qint64 trimmed = end;
        while (trimmed > start && rsv[trimmed - 1] == '\0')
            --trimmed;
        if (trimmed - start < 5 || rsv.mid(trimmed - 4, 4) != QByteArray("\0\0\1\xb0", 4))
            fail(QStringLiteral("Missing PS2 IPU end-of-frame marker."));
        payload.append(rsv.constData() + start, trimmed - start);
    }
    QByteArray out(16, '\0');
    out.replace(0, 4, "ipum");
    qToLittleEndian<quint32>(quint32(payload.size()), out.data() + 4);
    qToLittleEndian<quint16>(width, out.data() + 8);
    qToLittleEndian<quint16>(height, out.data() + 10);
    qToLittleEndian<quint32>(count, out.data() + 12);
    out += payload;
    if (pcm)
        *pcm = std::move(audio);
    return out;
}

namespace {
void publish(const QString &source, const QString &target) {
    if (QFileInfo(source).size() <= 0)
        fail(QStringLiteral("FFmpeg did not create the decoded PS2 media."));
    if (!QFile::rename(source, target) && QFileInfo(target).size() <= 0)
        fail(QStringLiteral("Could not save the decoded PS2 media."));
}
} // namespace

bool hasPs2Media(const Question &question) { return !reference(question).entry.isEmpty(); }

Ps2Media decodePs2Media(const Question &question, const QString &ffmpeg) {
    try {
        const auto ref = reference(question);
        if (ref.entry.isEmpty())
            fail(QStringLiteral("This question has no supported PS2 media reference. Reimport older video entries."));
        QByteArray pcm;
        const auto stream = makePs2Ipu(readRsv(ref), ref.video ? std::nullopt : std::optional<int>(ref.frame),
                                       ref.video ? &pcm : nullptr);
        auto digest = QCryptographicHash(QCryptographicHash::Sha256);
        digest.addData(ref.video ? QByteArrayView("ps2-rsv1-video-audio-v3") : QByteArrayView("ps2-rsv1-picture-v1"));
        digest.addData(stream);
        digest.addData(pcm);
        const auto key = QString::fromLatin1(digest.result().toHex());
        QDir cache(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("ps2-media"));
        if (!QDir().mkpath(cache.absolutePath()))
            fail(QStringLiteral("Could not create the PS2 media cache."));
        const auto image = cache.filePath(key + ".png");
        const auto video = ref.video ? cache.filePath(key + ".mp4") : QString();
        if (QFileInfo(image).size() > 0 && (!ref.video || QFileInfo(video).size() > 0))
            return {image, video, {}};
        QTemporaryDir temp(cache.filePath("decode-XXXXXX"));
        if (!temp.isValid())
            fail(QStringLiteral("Could not create a temporary media folder."));
        const auto ipu = temp.filePath("source.ipu");
        {
            QFile file(ipu);
            if (!file.open(QIODevice::WriteOnly) || file.write(stream) != stream.size() || !file.flush())
                fail(file.errorString());
        }
        const QStringList base{"-nostdin", "-v", "error", "-xerror"};
        QString input = ipu;
        QStringList format{"-f", "ipu"};
        if (ref.video) {
            const auto audioPath = temp.filePath("sound.pcm");
            {
                QFile file(audioPath);
                if (!file.open(QIODevice::WriteOnly) || file.write(pcm) != pcm.size() || !file.flush())
                    fail(file.errorString());
            }
            const auto output = temp.filePath("clip.mp4");
            // These Mega Quiz movies store the 20x15 macroblocks top-to-bottom
            // within each column. FFmpeg's IPU decoder places them row-major.
            // Move complete blocks, without rotating their pixels. The two
            // chroma planes use 8x8 blocks for the same 16x16 luma footprint.
            const auto remap = [](const QString &plane, int size) {
                const auto index = QStringLiteral("floor(X/%1)*15+floor(Y/%1)").arg(size);
                return QStringLiteral("%1(mod(X,%2)+%2*mod(%3,20),mod(Y,%2)+%2*floor((%3)/20))")
                    .arg(plane, QString::number(size), index);
            };
            const auto raster = QStringLiteral("geq=lum='%1':cb='%2':cr='%3':interpolation=nearest")
                                    .arg(remap("lum", 16), remap("cb", 8), remap("cr", 8));
            runMedia(ffmpeg, base + format + QStringList{"-i", ipu, "-f", "s16le", "-ar", "24000", "-ac", "2",
                     "-i", audioPath, "-vf", raster, "-c:v", "mpeg4", "-q:v", "3", "-c:a", "aac", "-b:a",
                     "96k", output}, nullptr);
            input = output;
            format.clear();
        }
        const auto png = temp.filePath("picture.png");
        const QStringList thumbnail = ref.video ? QStringList{"-vf", "thumbnail=50"} : QStringList{};
        runMedia(ffmpeg, base + format + QStringList{"-i", input} + thumbnail + QStringList{"-frames:v", "1", png},
                 nullptr);
        if (ref.video)
            publish(input, video);
        publish(png, image);
        return {image, video, {}};
    } catch (const std::exception &e) {
        return {{}, {}, QString::fromUtf8(e.what())};
    }
}
} // namespace studio
