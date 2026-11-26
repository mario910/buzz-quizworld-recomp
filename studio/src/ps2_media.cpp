#include "ps2_media.h"
#include "disc_image.h"
#include "pack_format.h"
#include "media_tools.h"
#include "ps3_source.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QMap>
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
        R"(^((?:BG2|BHW)/[A-Z0-9_]+/VIDEOS\.PAK):(Videos/(?:pics001|10[0-9]{5})\.rsv)$)",
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
    IsoExtent(std::unique_ptr<DiscImage> image, DiscImage::File extent)
        : RangeDevice(extent.size), image_(std::move(image)), base_(extent.offset) {}
  protected:
    QByteArray readRange(qint64 offset, qint64 length) override {
        return image_->readAt(base_ + offset, length);
    }
  private:
    std::unique_ptr<DiscImage> image_;
    qint64 base_;
};

QByteArray readRsv(const Reference &ref) {
    const QFileInfo source(ref.archive);
    std::unique_ptr<Archive> archive;
    if (DiscImage::isDiscImage(ref.archive)) {
        auto disc = std::make_unique<DiscImage>(ref.archive);
        const auto it = disc->files().constFind(ref.path.toUpper());
        if (it == disc->files().constEnd())
            fail(QStringLiteral("The source disc does not contain the referenced video archive."));
        const auto extent = it.value();
        archive = std::make_unique<Archive>(std::make_unique<IsoExtent>(std::move(disc), extent));
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

namespace {
QByteArray makeIpu(const QByteArray &rsv, const QList<int> &pictureFrames, bool video, QByteArray *pcm) {
    if (pcm)
        pcm->clear();
    QByteArray audio;
    check(rsv, 0, 32);
    const auto width = u16(rsv, 8), height = u16(rsv, 10);
    const auto frames = u32(rsv, 12);
    const auto fps = u32(rsv, 16), sampleRate = u32(rsv, 20), flags = u32(rsv, 24);
    // PAL Mega Quiz and Hollywood Quiz use the same per-frame audio layout.
    const bool known = video ? fps == 25 && sampleRate == 24000 && flags == 0x10000b && u32(rsv, 28) <= 1
                             : sampleRate == 0 && flags == 0x100000 && u32(rsv, 28) == 0;
    if (!rsv.startsWith("rsv1") || u32(rsv, 4) != rsv.size() - 32 || !known ||
        width != 320 || height != 240 || frames == 0 || frames > 5000)
        fail(QStringLiteral("Unsupported PS2 RSV layout."));
    const qint64 dataStart = 32 + qint64(frames) * 4;
    check(rsv, 0, dataStart);
    const quint32 count = video ? frames : quint32(pictureFrames.size());
    QByteArray payload;
    for (quint32 ordinal = 0; ordinal < count; ++ordinal) {
        const auto i = video ? ordinal : quint32(pictureFrames[ordinal]);
        if (i >= frames)
            fail(QStringLiteral("The referenced PS2 picture frame is outside the sheet."));
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

void publish(const QString &source, const QString &target) {
    if (QFileInfo(source).size() <= 0)
        fail(QStringLiteral("FFmpeg did not create the decoded PS2 media."));
    if (!QFile::rename(source, target) && QFileInfo(target).size() <= 0)
        fail(QStringLiteral("Could not save the decoded PS2 media."));
}
} // namespace

QByteArray makePs2Ipu(const QByteArray &rsv, std::optional<int> pictureFrame, QByteArray *pcm) {
    return makeIpu(rsv, pictureFrame ? QList<int>{*pictureFrame} : QList<int>{}, !pictureFrame, pcm);
}

bool hasPs2Media(const Question &question) { return !reference(question).entry.isEmpty(); }
QString ps2ClipKey(const Question &question) {
    const auto ref = reference(question);
    return ref.video ? ref.archive + '\n' + ref.path + '\n' + ref.entry : QString();
}

int importPs2Pictures(QVector<Question> &questions, const QString &ffmpeg, QStringList &warnings) {
    QMap<QString, QMap<int, QList<qsizetype>>> sheets;
    QMap<QString, Reference> refs;
    for (qsizetype i = 0; i < questions.size(); ++i) {
        const auto ref = reference(questions[i]);
        if (ref.entry.isEmpty() || ref.video || !ref.path.startsWith("BHW/", Qt::CaseInsensitive))
            continue;
        const auto key = ref.archive + '\n' + ref.path + ':' + ref.entry;
        refs[key] = ref;
        sheets[key][ref.frame] << i;
    }
    if (sheets.isEmpty())
        return 0;
    if (ffmpeg.isEmpty()) {
        warnings << missingMediaFfmpeg();
        return 0;
    }
    int imported = 0;
    for (auto sheet = sheets.cbegin(); sheet != sheets.cend(); ++sheet) {
        const auto ref = refs.value(sheet.key());
        try {
            const auto rsv = readRsv(ref);
            const auto sequence = sheet->keys();
            const auto key = QString::fromLatin1(QCryptographicHash::hash(rsv, QCryptographicHash::Sha256).toHex());
            QDir cache(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                           .filePath("ps2-media/hollywood-rsv-v1-" + key));
            if (!QDir().mkpath(cache.absolutePath()))
                fail(QStringLiteral("Could not create the PS2 media cache."));
            const auto image = [&](int frame) { return cache.filePath(QString::number(frame) + ".png"); };
            QList<int> pending;
            for (int frame : sequence)
                if (QFileInfo(image(frame)).size() <= 0)
                    pending << frame;
            if (!pending.isEmpty()) {
                QTemporaryDir temp(cache.filePath("decode-XXXXXX"));
                if (!temp.isValid())
                    fail(temp.errorString());
                const auto stream = makeIpu(rsv, pending, false, nullptr);
                QFile file(temp.filePath("sheet.ipu"));
                if (!file.open(QIODevice::WriteOnly) || file.write(stream) != stream.size() || !file.flush())
                    fail(file.errorString());
                file.close();
                const QStringList base{"-nostdin", "-v", "error", "-xerror", "-f", "ipu", "-i", file.fileName(),
                                       "-threads", "1", "-start_number", "0"};
                const QStringList output{temp.filePath("frame-%05d.png")};
                try {
                    runMedia(ffmpeg, base + QStringList{"-fps_mode", "passthrough"} + output, nullptr);
                } catch (const std::exception &e) {
                    if (!QByteArray(e.what()).contains("Unrecognized option 'fps_mode'"))
                        throw;
                    runMedia(ffmpeg, base + QStringList{"-vsync", "0"} + output, nullptr);
                }
                const auto files = QDir(temp.path()).entryList({"frame-*.png"}, QDir::Files, QDir::Name);
                if (files.size() != pending.size())
                    fail(QStringLiteral("The decoded PS2 image count does not match the sheet index."));
                for (qsizetype i = 0; i < pending.size(); ++i)
                    publish(temp.filePath(files[i]), image(pending[i]));
            }
            for (auto i = sheet->cbegin(); i != sheet->cend(); ++i)
                for (const auto row : i.value()) {
                    auto &q = questions[row];
                    q.image = image(i.key());
                    q.translations[q.language] = q.localized();
                    ++imported;
                }
        } catch (const std::exception &e) {
            warnings << ref.path + ": " + QString::fromUtf8(e.what());
        }
    }
    return imported;
}

Ps2Media decodePs2Media(const Question &question, const QString &ffmpeg, const std::atomic_bool *cancel) {
    try {
        checkCancelled(cancel);
        const auto ref = reference(question);
        if (ref.entry.isEmpty())
            fail(QStringLiteral("This question has no supported PS2 media reference. Reimport older video entries."));
        QByteArray pcm;
        const auto rsv = readRsv(ref);
        const auto stream = makePs2Ipu(rsv, ref.video ? std::nullopt : std::optional<int>(ref.frame),
                                       ref.video ? &pcm : nullptr);
        const bool columnMajor = ref.video && u32(rsv, 28) == 1;
        auto digest = QCryptographicHash(QCryptographicHash::Sha256);
        digest.addData(QByteArray(ref.video ? (columnMajor ? "ps2-rsv1-video-audio-v3" : "ps2-rsv1-video-row-v1")
                                           : "ps2-rsv1-picture-v1"));
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
        const QStringList base{"-nostdin", "-v", "error", "-xerror", "-y"};
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
            auto args = base + format + QStringList{"-i", ipu, "-f", "s16le", "-ar", "24000", "-ac", "2",
                                                     "-i", audioPath};
            const QStringList encode{"-c:v", "mpeg4", "-q:v", "3", "-c:a", "aac", "-b:a", "96k", output};
            try {
                runMedia(ffmpeg, args + (columnMajor ? QStringList{"-vf", raster} : QStringList{}) + encode, cancel);
            } catch (const std::exception &e) {
                // Field-DCT RSV can store a whole pair of 16-line macroblocks
                // past the visible 240-line raster. Keep nominal geometry for
                // ordinary clips, and retry this alignment only on a decode error.
                checkCancelled(cancel);
                const auto height = u16(stream, 10);
                if (columnMajor || height % 32 == 0 || !(quint8(stream[16]) & 4) ||
                    !QByteArray(e.what()).contains("Invalid data found when processing input"))
                    throw;
                auto coded = stream;
                qToLittleEndian<quint16>((height + 31) & ~31, coded.data() + 10);
                QFile file(ipu);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                    file.write(coded) != coded.size() || !file.flush())
                    fail(file.errorString());
                file.close();
                runMedia(ffmpeg, args + QStringList{"-vf", QStringLiteral("crop=iw:%1:0:0").arg(height)} + encode,
                         cancel);
            }
            input = output;
            format.clear();
        }
        const auto png = temp.filePath("picture.png");
        const QStringList thumbnail = ref.video ? QStringList{"-vf", "thumbnail=50"} : QStringList{};
        runMedia(ffmpeg, base + format + QStringList{"-i", input} + thumbnail + QStringList{"-frames:v", "1", png},
                 cancel);
        if (ref.video)
            publish(input, video);
        publish(png, image);
        return {image, video, {}};
    } catch (const std::exception &e) {
        return {{}, {}, QString::fromUtf8(e.what())};
    }
}
} // namespace studio
