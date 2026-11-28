#include "ps2_media.h"
#include "disc_image.h"
#include "pack_format.h"
#include "media_tools.h"
#include "ps3_source.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
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
        R"(^(?:([A-Z0-9_]+/[A-Z0-9_]+/VIDEOS\.PAK):(Videos/(pics[0-9]{3}|[0-9]{7})\.rsv)|(QVIDEOS\.PAK):(questionvideos/[A-Z0-9_-]+\.ipu)|(QIMAGES\.PAK):(questiondata/[A-Z0-9_-]+\.img))$)",
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
        if (!match.hasMatch())
            continue;
        const bool still = match.captured(3).startsWith("pics", Qt::CaseInsensitive);
        if (match.capturedStart(1) >= 0 && ((still && (video || frame < 0)) || (!still && !video)))
            continue;
        if ((match.capturedStart(4) >= 0 && !video) || (match.capturedStart(6) >= 0 && video))
            continue;
        const auto pathGroup = match.capturedStart(1) >= 0 ? 1 : video ? 4 : 6;
        const auto archive = ref["archive"].toString(q.extra["source_archive"].toString());
        if (!archive.isEmpty())
            return {archive, match.captured(pathGroup), match.captured(pathGroup + 1), frame, video};
    }
    return {};
}

// The video PAK can be over a GiB. Read just the central directory and the
// selected entry, using a bounded view of the ISO extent.
class IsoExtent final : public ps3::RangeDevice {
  public:
    IsoExtent(std::unique_ptr<DiscImage> image, DiscImage::File extent)
        : RangeDevice(extent.size), image_(std::move(image)), file_(std::move(extent)) {}
  protected:
    QByteArray readRange(qint64 offset, qint64 length) override {
        return image_->readFile(file_, offset, length);
    }
  private:
    std::unique_ptr<DiscImage> image_;
    DiscImage::File file_;
};

} // namespace

QString ps2DiscFile(const QString &root, const QString &path) {
    QDir directory(root);
    const auto parts = path.split('/');
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const auto &part = parts[i];
        if (part.isEmpty() || part == "." || part == ".." || part.contains('\\') || part.contains(':'))
            return {};
        const bool last = i == parts.size() - 1;
        const QFileInfo exact(directory.filePath(part));
        if (!exact.isSymLink() && (last ? exact.isFile() : exact.isDir())) {
            if (last)
                return exact.absoluteFilePath();
            directory.setPath(exact.absoluteFilePath());
            continue;
        }
        const auto names = directory.entryInfoList((last ? QDir::Files : QDir::Dirs) |
                                                    QDir::NoDotAndDotDot | QDir::NoSymLinks);
        const auto match = std::find_if(names.cbegin(), names.cend(), [&](const QFileInfo &file) {
            return file.fileName().compare(part, Qt::CaseInsensitive) == 0;
        });
        if (match == names.cend())
            return {};
        if (last)
            return match->absoluteFilePath();
        directory.setPath(match->absoluteFilePath());
    }
    return {};
}

std::unique_ptr<Archive> openPs2Archive(const QString &sourcePath, const QString &path) {
    std::unique_ptr<Archive> archive;
    if (DiscImage::isDiscImage(sourcePath)) {
        auto disc = std::make_unique<DiscImage>(sourcePath);
        const auto it = disc->files().constFind(path.toUpper());
        if (it == disc->files().constEnd())
            fail(QCoreApplication::translate("MediaTools", "Płyta źródłowa nie zawiera wskazanego archiwum filmów."));
        const auto extent = it.value();
        archive = std::make_unique<Archive>(std::make_unique<IsoExtent>(std::move(disc), extent));
    } else if (QFileInfo(sourcePath).isDir()) {
        const auto file = ps2DiscFile(sourcePath, path);
        if (file.isEmpty())
            fail(QCoreApplication::translate("MediaTools", "Płyta źródłowa nie zawiera wskazanego archiwum filmów."));
        archive = std::make_unique<Archive>(file);
    } else {
        // A loose language PAK is one directory below the extracted disc root.
        const QFileInfo source(sourcePath);
        QDir root = source.dir();
        if (source.suffix().compare("pak", Qt::CaseInsensitive) != 0)
            fail(QCoreApplication::translate(
                "MediaTools", "Otwórz obraz płyty PS2 lub plik PAK z rundami z rozpakowanej płyty PS2."));
        // Early releases put the language PAK at the disc root.
        if (!QFileInfo(root.filePath(path)).isFile())
            root.cdUp();
        archive = std::make_unique<Archive>(root.filePath(path));
    }
    return archive;
}

namespace {
QByteArray readAsset(Archive &archive, const QString &entry, const QString &container = {}) {
    const auto it = archive.entries.constFind(entry);
    if (it == archive.entries.constEnd() || it->size > 64 * 1024 * 1024)
        fail(QCoreApplication::translate("MediaTools", "Wskazanego pliku RSV brakuje lub jest za duży."));
    // The Australian Big Quiz v2.00 has a stale ZIP CRC on this intact sheet.
    if (container == "BU1/SK_AUS/VIDEOS.PAK" && entry == "Videos/pics001.rsv" &&
        it->size == 25500400 && it->crc == 0xc1a60773u)
        return archive.readWithChecksum(entry, 0x226ed23bu);
    return archive.read(entry);
}

constexpr int blockBytes = 16, samplesPerBlock = 28;

int signed16(quint16 value) { return value < 0x8000 ? int(value) : int(value) - 0x10000; }
int divideFloor(int value, int divisor) {
    return value >= 0 ? value / divisor : (value - (divisor - 1)) / divisor;
}

int decodeAdpcm(const char *block, int j, int &history, int &older) {
    static constexpr int prediction[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    const int filter = quint8(block[0]) / 16, shift = quint8(block[0]) % 16;
    if (filter >= 5 || shift > 12 || block[1] != 0)
        fail(QCoreApplication::translate("MediaTools", "Nieobsługiwany nagłówek bloku audio PS2 RSV."));
    const int nibble = (quint8(block[2 + j / 2]) >> (4 * (j % 2))) & 15;
    const int delta = divideFloor((nibble < 8 ? nibble : nibble - 16) * 4096, 1 << shift);
    const int estimate = divideFloor(history * prediction[filter][0] + older * prediction[filter][1] + 32, 64);
    const int sample = std::clamp(delta + estimate, -32768, 32767);
    older = history;
    history = sample;
    return sample;
}

QByteArray decodeAudio(const QByteArray &rsv, qint64 start, int count, int blockCount, int channels) {
    // RSV stores independent history for each channel followed by alternating
    // PS-ADPCM blocks. The last blocks can include padding samples.
    QByteArray pcm(count * channels * 2, '\0');
    for (int channel = 0; channel < channels; ++channel) {
        int history = signed16(u16(rsv, start + 2 + channel * 4));
        int older = signed16(u16(rsv, start + 4 + channel * 4));
        for (int i = 0; i < blockCount; ++i) {
            const char *b = rsv.constData() + start + 2 + channels * 4 + (i * channels + channel) * blockBytes;
            for (int j = 0; j < samplesPerBlock && i * samplesPerBlock + j < count; ++j) {
                const int sample = decodeAdpcm(b, j, history, older);
                const int index = ((i * samplesPerBlock + j) * channels + channel) * 2;
                qToLittleEndian<quint16>(quint16(sample), pcm.data() + index);
            }
        }
    }
    return pcm;
}

} // namespace

namespace {
QByteArray sportsIpu(QByteArray video, const QByteArray &index) {
    check(video, 0, 16);
    const auto frames = u32(video, 12);
    if (!video.startsWith("ipum") || !frames || frames > 5000 || index.size() != qint64(frames) * 4 ||
        u32(index, 0) != 16)
        fail(QCoreApplication::translate("MediaTools", "Nieobsługiwany nagłówek filmu do pytania."));
    QByteArray stream = video.left(16);
    for (quint32 i = 0; i < frames; ++i) {
        const auto start = u32(index, i * 4);
        const qint64 limit = i + 1 < frames ? u32(index, (i + 1) * 4) : video.size();
        if (start < 16 || limit > video.size() || start >= limit)
            fail(QCoreApplication::translate("MediaTools", "Niepoprawne przesunięcia klatek PS2 RSV."));
        // IPX gives frame starts. Padding and the last sequence-end marker
        // must be removed before sending the stream to FFmpeg's IPU parser.
        const auto frameEnd = video.lastIndexOf(QByteArray("\0\0\1\xb0", 4), limit - 4);
        if (frameEnd <= start || limit - frameEnd > 32)
            fail(QCoreApplication::translate("MediaTools", "Brak znacznika końca klatki PS2 IPU."));
        stream.append(video.constData() + start, frameEnd + 4 - start);
    }
    qToLittleEndian<quint32>(quint32(stream.size() - 16), stream.data() + 4);
    return stream;
}

QByteArray makeIpu(const QByteArray &rsv, const QList<int> &pictureFrames, bool video, QByteArray *pcm) {
    if (pcm)
        pcm->clear();
    QByteArray audio;
    check(rsv, 0, 32);
    const auto width = u16(rsv, 8), height = u16(rsv, 10);
    const auto frames = u32(rsv, 12);
    const auto fps = u32(rsv, 16), sampleRate = u32(rsv, 20), flags = u32(rsv, 24);
    const bool known = (fps == 25 || fps == 29) && u32(rsv, 28) <= 1 &&
                       (video ? (sampleRate == 24000 && flags == 0x10000b) ||
                                    (sampleRate == 44100 && flags == 0x10000a) ||
                                    (sampleRate == 11025 && flags == 0x100000) ||
                                    (sampleRate == 0 && flags == 0x100000)
                              : sampleRate == 0 && flags == 0x100000);
    if (!rsv.startsWith("rsv1") || u32(rsv, 4) != rsv.size() - 32 || !known ||
        width != 320 || height != 240 || frames == 0 || frames > 5000)
        fail(QCoreApplication::translate("MediaTools", "Nieobsługiwana struktura PS2 RSV."));
    const qint64 dataStart = 32 + qint64(frames) * 4;
    check(rsv, 0, dataStart);
    const quint32 count = video ? frames : quint32(pictureFrames.size());
    QByteArray payload;
    for (quint32 ordinal = 0; ordinal < count; ++ordinal) {
        const auto i = video ? ordinal : quint32(pictureFrames[ordinal]);
        if (i >= frames)
            fail(QCoreApplication::translate("MediaTools", "Wskazana klatka obrazu PS2 jest poza arkuszem."));
        auto start = qint64(u32(rsv, 32 + i * 4));
        const qint64 end = i + 1 < frames ? u32(rsv, 32 + (i + 1) * 4) : rsv.size();
        if (start < dataStart || end <= start || end > rsv.size())
            fail(QCoreApplication::translate("MediaTools", "Niepoprawne przesunięcia klatek PS2 RSV."));
        if (video && sampleRate) {
            const int channels = sampleRate == 11025 ? 1 : 2;
            const int samples = u16(rsv, start);
            const int expected = fps == 25 ? sampleRate / 25 : (sampleRate * 1001 + 15000) / 30000;
            const int blocks = (samples + samplesPerBlock - 1) / samplesPerBlock;
            const int audioBytes = (2 + channels * 4 + blocks * channels * blockBytes + 15) & ~15;
            if (end - start <= audioBytes || std::abs(samples - expected) > 1)
                fail(QCoreApplication::translate("MediaTools", "Nieobsługiwana struktura bloku audio PS2 RSV."));
            if (pcm)
                audio += decodeAudio(rsv, start, samples, blocks, channels);
            start += audioBytes;
        }
        // FFmpeg's IPU parser expects the next frame immediately after its
        // sequence-end code, not the RSV container's zero alignment bytes.
        qint64 trimmed = end;
        while (trimmed > start && rsv[trimmed - 1] == '\0')
            --trimmed;
        if (trimmed - start < 5 || rsv.mid(trimmed - 4, 4) != QByteArray("\0\0\1\xb0", 4))
            fail(QCoreApplication::translate("MediaTools", "Brak znacznika końca klatki PS2 IPU."));
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
        fail(QCoreApplication::translate("MediaTools", "FFmpeg nie utworzył zdekodowanych mediów PS2."));
    if (!QFile::rename(source, target) && QFileInfo(target).size() <= 0)
        fail(QCoreApplication::translate("MediaTools", "Nie udało się zapisać zdekodowanych mediów PS2."));
}

QDir ps2Cache() {
    QDir cache(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("ps2-media"));
    if (!QDir().mkpath(cache.absolutePath()))
        fail(QCoreApplication::translate(
            "MediaTools", "Nie udało się utworzyć katalogu pamięci podręcznej mediów PS2."));
    return cache;
}

QString saveSportsImage(const QByteArray &data) {
    constexpr int width = 256, height = 256, paletteStart = width * height;
    if (data.size() != paletteStart + 1024 + 8 || !data.endsWith(QByteArray("AOI\0END!", 8)))
        fail(QCoreApplication::translate("MediaTools", "Nieobsługiwany nagłówek filmu do pytania."));
    const auto path = ps2Cache().filePath("img-" +
        QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) + ".png");
    if (QFileInfo(path).size() > 0)
        return path;
    QImage image(width, height, QImage::Format_RGB32);
    QRgb colors[256];
    for (int i = 0; i < 256; ++i) {
        const int paletteIndex = (i & ~24) | ((i & 8) << 1) | ((i & 16) >> 1);
        const auto *color = reinterpret_cast<const uchar *>(data.constData()) + paletteStart + 4 * paletteIndex;
        colors[i] = qRgb(color[0], color[1], color[2]);
    }
    for (int y = 0; y < height; ++y) {
        auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
        const auto *indices = data.constData() + (height - 1 - y) * width;
        for (int x = 0; x < width; ++x)
            out[x] = colors[quint8(indices[x])];
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit())
        fail(file.errorString());
    return path;
}

QString saveVgp(const QList<QByteArray> &clips) {
    constexpr int sector = 2336, audio = 2304;
    const auto error = QCoreApplication::translate("MediaTools", "Nieobsługiwana struktura bloku audio PS2 RSV.");
    if (clips.isEmpty() || clips.first().size() < sector)
        fail(error);
    const auto rateCode = quint8(clips.first()[sector - 2]);
    const int rate = rateCode == 44 ? 44100 : rateCode == 22 ? 22050 : rateCode * 1000;
    const char mode = clips.first()[sector - 1];
    const int channels = mode == 1 ? 1 : mode == 0 ? 2 : 0;
    if (rate < 8000 || rate > 48000 || !channels)
        fail(error);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    for (const auto &clip : clips) {
        if (clip.isEmpty() || clip.size() % sector || quint8(clip[sector - 2]) != rateCode || clip[sector - 1] != mode)
            fail(error);
        if (clips.size() > 1)
            digest.addData(QByteArray::number(clip.size()) + '\n');
        digest.addData(clip);
    }
    const auto path = ps2Cache().filePath("vgp-v1-" + QString::fromLatin1(digest.result().toHex()) + ".wav");
    if (QFileInfo(path).size() > 0)
        return path;
    const int channelBytes = audio / channels, channelSamples = channelBytes / blockBytes * samplesPerBlock;
    QByteArray wav(44, '\0');
    wav.replace(0, 4, "RIFF");
    wav.replace(8, 8, "WAVEfmt ");
    wav[16] = 16; // PCM format record size.
    wav[20] = 1;
    wav[34] = 16;
    wav.replace(36, 4, "data");
    qToLittleEndian<quint16>(channels, wav.data() + 22);
    qToLittleEndian<quint32>(rate, wav.data() + 24);
    qToLittleEndian<quint32>(rate * channels * 2, wav.data() + 28);
    qToLittleEndian<quint16>(channels * 2, wav.data() + 32);
    for (const auto &data : clips) {
        if (wav.size() > 44)
            wav.append(rate / 2 * channels * 2, '\0');
        for (qsizetype pos = 0; pos < data.size(); pos += sector) {
            const int finalSamples = u16(data, pos + sector - 4);
            const int count = pos + sector == data.size() && finalSamples > 0 && finalSamples <= channelSamples
                                  ? finalSamples : channelSamples;
            const auto outputStart = wav.size();
            wav.resize(outputStart + count * channels * 2);
            for (int c = 0; c < channels; ++c) {
                int history = signed16(u16(data, pos + audio + c * 4));
                int older = signed16(u16(data, pos + audio + c * 4 + 2));
                for (int i = 0; i < count; ++i) {
                    const auto *block = data.constData() + pos + c * channelBytes + i / samplesPerBlock * blockBytes;
                    const int sample = decodeAdpcm(block, i % samplesPerBlock, history, older);
                    qToLittleEndian<quint16>(quint16(sample), wav.data() + outputStart + (i * channels + c) * 2);
                }
            }
        }
    }
    qToLittleEndian<quint32>(wav.size() - 8, wav.data() + 4);
    qToLittleEndian<quint32>(wav.size() - 44, wav.data() + 40);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size() || !file.commit())
        fail(file.errorString());
    return path;
}
} // namespace

QByteArray makePs2Ipu(const QByteArray &rsv, std::optional<int> pictureFrame, QByteArray *pcm) {
    return makeIpu(rsv, pictureFrame ? QList<int>{*pictureFrame} : QList<int>{}, !pictureFrame, pcm);
}

bool hasPs2Media(const Question &question) { return !reference(question).entry.isEmpty(); }
namespace {
QStringList musicEntries(const QJsonObject &ref) {
    QStringList entries{ref["entry"].toString()};
    for (const auto &entry : ref["additional_entries"].toArray())
        entries << entry.toString();
    return entries;
}
}

bool hasPs2Music(const Question &question) {
    static const QRegularExpression entry(R"(^Sound/[A-Z0-9_-]{1,48}\.vgp$)",
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression pack(R"(^(?:SONGCLIP|STREAMED|Q-[A-Z]{2,3}|[A-Z]{2,3}/[A-Z]{2,3}SPEAK)\.PAK$)");
    const auto ref = question.sourceMusic();
    if (ref["type"] != "ps2-audio" || !question.hasUnresolvedMusic() ||
        ref["archive"].toString().isEmpty() || !pack.match(ref["pack"].toString()).hasMatch())
        return false;
    const auto entries = musicEntries(ref);
    return entries.size() <= 4 && std::all_of(entries.cbegin(), entries.cend(), [&](const QString &name) {
        return entry.match(name).hasMatch();
    });
}

bool hasPs2Narration(const Question &question) {
    static const QRegularExpression pack(R"(^Q-[A-Z]{2,3}\.PAK$)"),
        entry(R"(^sound/q[0-9]{1,7}[a-z]{2,3}\.vgp$)");
    const auto ref = question.translations.value(question.language).toObject()["source_speech"].toObject();
    return !QFileInfo(question.speech).isFile() && !ref["archive"].toString().isEmpty() &&
           pack.match(ref["pack"].toString()).hasMatch() && entry.match(ref["entry"].toString()).hasMatch();
}

QString recoverPs2Narration(const Question &question) {
    if (!hasPs2Narration(question))
        return {};
    const auto ref = question.translations.value(question.language).toObject()["source_speech"].toObject();
    auto archive = openPs2Archive(ref["archive"].toString(), ref["pack"].toString());
    return saveVgp({readAsset(*archive, ref["entry"].toString())});
}

void recoverPs2Music(QVector<Question> &questions, QStringList &warnings, const std::atomic_bool *cancel) {
    QMap<QPair<QString, QString>, QMap<QStringList, QList<qsizetype>>> sources;
    for (qsizetype i = 0; i < questions.size(); ++i) {
        if (!hasPs2Music(questions[i]))
            continue;
        const auto ref = questions[i].sourceMusic();
        sources[{ref["archive"].toString(), ref["pack"].toString()}][musicEntries(ref)] << i;
    }
    const auto warn = [&](const QString &asset, const std::exception &e) {
        if (warnings.size() < 5)
            warnings << asset + " · " + QString::fromUtf8(e.what());
    };
    for (auto source = sources.cbegin(); source != sources.cend(); ++source) {
        if (cancel && cancel->load())
            break;
        try {
            auto archive = openPs2Archive(source.key().first, source.key().second);
            for (auto item = source->cbegin(); item != source->cend(); ++item) {
                if (cancel && cancel->load())
                    return;
                try {
                    QList<QByteArray> clips;
                    for (const auto &name : item.key())
                        clips << readAsset(*archive, name);
                    const auto path = saveVgp(clips);
                    for (auto row : item.value()) {
                        auto &q = questions[row];
                        q.music = path;
                        q.translations[q.language] = q.localized();
                    }
                } catch (const std::exception &e) {
                    warn(item.key().join(", "), e);
                }
            }
        } catch (const std::exception &e) {
            warn(source.key().second, e);
        }
    }
}

QString ps2ClipKey(const Question &question) {
    const auto ref = reference(question);
    return ref.video ? ref.archive + '\n' + ref.path + '\n' + ref.entry : QString();
}

int importPs2Pictures(QVector<Question> &questions, const QString &ffmpeg, QStringList &warnings,
                      const std::atomic_bool *cancel, bool includeMegaQuiz) {
    QMap<QString, QMap<int, QList<qsizetype>>> sheets;
    QMap<QString, Reference> refs;
    for (qsizetype i = 0; i < questions.size(); ++i) {
        const auto &question = questions[i];
        if (!question.hasUnresolvedVisualMedia())
            continue;
        const auto ref = reference(question);
        if (ref.entry.isEmpty() || ref.video ||
            (!includeMegaQuiz && ref.path.startsWith("BG2/", Qt::CaseInsensitive)))
            continue;
        const auto key = ref.archive + '\n' + ref.path + ':' + ref.entry;
        refs[key] = ref;
        sheets[key][ref.frame] << i;
    }
    if (sheets.isEmpty())
        return 0;
    const bool needsFfmpeg = std::any_of(refs.cbegin(), refs.cend(), [](const Reference &ref) {
        return ref.entry.endsWith(".rsv", Qt::CaseInsensitive);
    });
    if (ffmpeg.isEmpty() && needsFfmpeg)
        warnings << missingMediaFfmpeg();
    int imported = 0;
    QString activeArchive;
    std::unique_ptr<Archive> archive;
    for (auto sheet = sheets.cbegin(); sheet != sheets.cend(); ++sheet) {
        if (cancel && cancel->load())
            break;
        const auto ref = refs.value(sheet.key());
        try {
            const auto archiveKey = ref.archive + '\n' + ref.path;
            if (activeArchive != archiveKey) {
                archive = openPs2Archive(ref.archive, ref.path);
                activeArchive = archiveKey;
            }
            const auto rsv = readAsset(*archive, ref.entry, ref.path);
            if (ref.entry.endsWith(".img", Qt::CaseInsensitive)) {
                const auto path = saveSportsImage(rsv);
                for (const auto row : sheet->value(-1)) {
                    auto &q = questions[row];
                    q.image = path;
                    q.translations[q.language] = q.localized();
                    ++imported;
                }
                continue;
            }
            if (ffmpeg.isEmpty())
                continue;
            const auto frameCount = u32(rsv, 12);
            // Reject an unsupported sheet once, before trying individual frames.
            makeIpu(rsv, {}, false, nullptr);
            const auto key = QString::fromLatin1(QCryptographicHash::hash(rsv, QCryptographicHash::Sha256).toHex());
            QDir cache(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                           .filePath("ps2-media/hollywood-rsv-v1-" + key));
            if (!QDir().mkpath(cache.absolutePath()))
                fail(QCoreApplication::translate(
                    "MediaTools", "Nie udało się utworzyć katalogu pamięci podręcznej mediów PS2."));
            const auto image = [&](int frame) { return cache.filePath(QString::number(frame) + ".png"); };
            const auto warnFrame = [&](int frame, const QString &error) {
                const auto row = sheet->value(frame).first();
                warnings << QStringLiteral("%1:%2 [%3] · %4\n%5")
                                .arg(ref.path, ref.entry, QString::number(frame), questions[row].id, error);
            };
            QList<int> pending;
            for (auto i = sheet->cbegin(); i != sheet->cend(); ++i) {
                const int frame = i.key();
                if (quint32(frame) >= frameCount)
                    warnFrame(frame, QCoreApplication::translate("MediaTools", "Wskazana klatka obrazu PS2 jest poza arkuszem."));
                else if (QFileInfo(image(frame)).size() <= 0)
                    pending << frame;
            }
            const auto decode = [&](auto &&self, const QList<int> &frames) -> void {
                checkCancelled(cancel);
                QTemporaryDir temp(cache.filePath("decode-XXXXXX"));
                if (!temp.isValid())
                    throw MediaEnvironmentError(temp.errorString());
                QStringList files;
                try {
                    const auto stream = makeIpu(rsv, frames, false, nullptr);
                    QFile file(temp.filePath("sheet.ipu"));
                    if (!file.open(QIODevice::WriteOnly) || file.write(stream) != stream.size() || !file.flush())
                        throw MediaEnvironmentError(file.errorString());
                    file.close();
                    const QStringList base{"-nostdin", "-v", "error", "-xerror", "-f", "ipu", "-i", file.fileName(),
                                           "-threads", "1", "-start_number", "0"};
                    const QStringList output{temp.filePath("frame-%05d.png")};
                    try {
                        runMedia(ffmpeg, base + QStringList{"-fps_mode", "passthrough"} + output, cancel);
                    } catch (const std::exception &e) {
                        if (!QByteArray(e.what()).contains("Unrecognized option 'fps_mode'"))
                            throw;
                        runMedia(ffmpeg, base + QStringList{"-vsync", "0"} + output, cancel);
                    }
                    files = QDir(temp.path()).entryList({"frame-*.png"}, QDir::Files, QDir::Name);
                    if (files.size() != frames.size())
                        fail(QCoreApplication::translate(
                            "MediaTools", "Liczba zdekodowanych obrazków PS2 nie zgadza się z indeksem arkusza."));
                } catch (const MediaEnvironmentError &) {
                    throw;
                } catch (const std::exception &e) {
                    checkCancelled(cancel);
                    const auto error = QString::fromUtf8(e.what());
                    if (error.contains("Unknown input format:") || error.contains("Unknown encoder") ||
                        error.contains("Unrecognized option") || error == QCoreApplication::translate(
                            "PackBuilder", "Silnik mediów przekroczył limit czasu lub długości nagrania (10 minut)."))
                        throw;
                    // Never assign output by position after a partial decode. Split the
                    // request until the bad frame is isolated and good frames can be named.
                    if (frames.size() == 1) {
                        warnFrame(frames.first(), error);
                    } else {
                        const auto half = frames.size() / 2;
                        self(self, frames.mid(0, half));
                        self(self, frames.mid(half));
                    }
                    return;
                }
                for (qsizetype i = 0; i < frames.size(); ++i)
                    publish(temp.filePath(files[i]), image(frames[i]));
            };
            if (!pending.isEmpty()) {
                try {
                    decode(decode, pending);
                } catch (const std::exception &e) {
                    if (cancel && cancel->load())
                        break;
                    warnings << ref.path + ": " + QString::fromUtf8(e.what());
                }
            }
            for (auto i = sheet->cbegin(); i != sheet->cend(); ++i) {
                const auto path = image(i.key());
                if (quint32(i.key()) >= frameCount || QFileInfo(path).size() <= 0)
                    continue;
                for (const auto row : i.value()) {
                    auto &q = questions[row];
                    q.image = path;
                    q.translations[q.language] = q.localized();
                    ++imported;
                }
            }
        } catch (const std::exception &e) {
            if (cancel && cancel->load())
                break;
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
            fail(QCoreApplication::translate(
                "MediaTools", "Pytanie nie ma obsługiwanego odwołania do mediów PS2. "
                              "Zaimportuj ponownie starsze wpisy z filmami."));
        auto archive = openPs2Archive(ref.archive, ref.path);
        const auto data = readAsset(*archive, ref.entry, ref.path);
        if (ref.entry.endsWith(".img", Qt::CaseInsensitive))
            return {saveSportsImage(data), {}, {}};
        const bool indexed = ref.entry.endsWith(".ipu", Qt::CaseInsensitive);
        QByteArray pcm, stream;
        if (indexed)
            stream = sportsIpu(data, readAsset(*archive, ref.entry.chopped(1) + "x"));
        else
            stream = makePs2Ipu(data, ref.video ? std::nullopt : std::optional<int>(ref.frame), ref.video ? &pcm : nullptr);
        const bool columnMajor = ref.video && !indexed && u32(data, 28) == 1;
        const bool ntsc = ref.video && !indexed && u32(data, 16) == 29;
        auto digest = QCryptographicHash(QCryptographicHash::Sha256);
        digest.addData(QByteArray(ref.video ? (columnMajor ? "ps2-rsv1-video-audio-v3" : "ps2-rsv1-video-row-v1")
                                           : "ps2-rsv1-picture-v1"));
        digest.addData(stream);
        digest.addData(pcm);
        if (ntsc)
            digest.addData(QByteArray("ntsc"));
        const auto key = QString::fromLatin1(digest.result().toHex());
        const auto cache = ps2Cache();
        const auto image = cache.filePath(key + ".png");
        const auto video = ref.video ? cache.filePath(key + ".mp4") : QString();
        if (QFileInfo(image).size() > 0 && (!ref.video || QFileInfo(video).size() > 0))
            return {image, video, {}};
        QTemporaryDir temp(cache.filePath("decode-XXXXXX"));
        if (!temp.isValid())
            fail(QCoreApplication::translate("MediaTools", "Nie udało się utworzyć tymczasowego katalogu mediów."));
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
            if (!pcm.isEmpty()) {
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
            auto args = base + format;
            if (ntsc)
                args << "-r" << "30000/1001";
            args << "-i" << ipu;
            if (!pcm.isEmpty()) {
                const auto rate = u32(data, 20);
                args << "-f" << "s16le" << "-ar" << QString::number(rate)
                     << "-ac" << (rate == 11025 ? "1" : "2") << "-i" << audioPath;
            }
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
