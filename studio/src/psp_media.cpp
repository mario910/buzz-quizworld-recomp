#include "psp_media.h"
#include "pack_format.h"
#include "pack_media.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QSaveFile>
#include <QStandardPaths>
#include <map>
#include <zlib.h>

namespace studio {
namespace {
using namespace pack;
constexpr auto tocPath = "PSP_GAME/USRDIR/UMD.TOC";
constexpr qint64 maxAsset = 128 * 1024 * 1024;
QString tr(const char *message) { return QCoreApplication::translate("MediaTools", message); }
QString missingAsset() {
    return tr("Nie znaleziono klipu PSP do pytania w katalogu rozpakowanych danych.");
}

class AssetReader {
  public:
    explicit AssetReader(const QString &source) : source_(source) {
        if (DiscImage::isDiscImage(source))
            disc_ = std::make_unique<DiscImage>(source);
    }

    QByteArray read(const QJsonObject &ref) {
        const auto entry = ref["entry"].toString();
        if (disc_) {
            if (!ref.contains("psp_hash"))
                return disc_->read(entry, maxAsset);
            if (!index_)
                index_ = readPspAssetIndex(*disc_);
            const auto key = ref["psp_hash"].toInteger(-1);
            const auto extent = index_->constFind(quint32(key));
            if (key < 0 || key > 0xffffffffll || extent == index_->constEnd() || extent->size > maxAsset)
                fail(missingAsset());
            return disc_->readAt(extent->offset, extent->size);
        }
        // A loose MERGED.QDB may use media in the same data/locale root.
        auto root = QFileInfo(source_).dir();
        if (QFileInfo(source_).fileName().compare("MERGED.QDB", Qt::CaseInsensitive) != 0 || ref.contains("psp_hash"))
            fail(missingAsset());
        if (root.dirName().compare("QUESTION_DATA", Qt::CaseInsensitive) != 0 && !root.cdUp())
            fail(missingAsset());
        if (!root.cdUp())
            fail(missingAsset());
        const QFileInfo info(entry);
        const auto parent = root.canonicalPath(), file = info.canonicalFilePath();
        if (parent.isEmpty() || file.isEmpty() || !file.startsWith(parent + '/') || info.size() > maxAsset)
            fail(missingAsset());
        QFile input(file);
        if (!input.open(QIODevice::ReadOnly))
            fail(input.errorString());
        return input.readAll();
    }
  private:
    QString source_;
    std::unique_ptr<DiscImage> disc_;
    std::optional<PspAssetIndex> index_;
};

QJsonObject pictureRef(const Question &q) {
    for (const auto &item : q.sourceMedia()) {
        const auto ref = item.toObject();
        if (ref["type"] == "psp-picture" && !ref["entry"].toString().isEmpty() && !ref["archive"].toString().isEmpty())
            return ref;
    }
    return {};
}

QJsonObject narrationRef(const Question &q) {
    const auto ref = q.translations.value(q.language).toObject()["source_speech"].toObject();
    return ref["type"] == "psp-narration" ? ref : QJsonObject{};
}

bool audioRef(const QJsonObject &ref) {
    return !ref["archive"].toString().isEmpty() && !ref["entry"].toString().isEmpty();
}

QString cachePath(const QByteArray &bytes, const QString &suffix) {
    QDir dir(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("psp-media"));
    if (!QDir().mkpath(dir.absolutePath()))
        fail(tr("Nie udało się utworzyć katalogu pamięci podręcznej mediów."));
    return dir.filePath(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) + suffix);
}

QString savePicture(QByteArray data) {
    const auto error = QCoreApplication::translate("PackBuilder", "Nie można odczytać obrazka: ") +
                       QStringLiteral("PSP TEX");
    const auto path = cachePath(data, "-tex-v2.png");
    if (QFileInfo(path).size() > 0)
        return path;
    qint64 pixels = 0, palette = 0;
    int width = 0, height = 0;
    bool swizzled = false;
    if (data.startsWith(QByteArray("TEX\0", 4))) {
        check(data, 0, 16);
        const auto w = u32(data, 4), h = u32(data, 8);
        const auto format = u32(data, 12);
        if ((format != 5 && format != 0x4005 && format != 0x8005) || !w || !h || w > 2048 || h > 2048)
            fail(error);
        width = int(w);
        height = int(h);
        palette = 16;
        pixels = palette + 1024;
        if (format == 0x4005) {
            QByteArray decoded(w * h + 1024, Qt::Uninitialized);
            uLongf size = decoded.size();
            if (uncompress(reinterpret_cast<Bytef *>(decoded.data()), &size,
                           reinterpret_cast<const Bytef *>(data.constData() + 16), data.size() - 16) != Z_OK ||
                size != uLongf(decoded.size()))
                fail(error);
            data.replace(16, data.size() - 16, decoded);
        }
    } else if (data.startsWith("TEX ")) {
        // Ultimate Music Quiz's indexed texture wrapper includes offsets for
        // its pixels and palette instead of a fixed header followed by both.
        check(data, 0, 72);
        width = u16(data, 36);
        height = u16(data, 38);
        swizzled = (u32(data, 32) & 0x10000) != 0;
        pixels = 32ll + u32(data, 40);
        palette = 32ll + u32(data, 48);
    }
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048 || palette < 16 || pixels < 16)
        fail(error);
    if (swizzled && (width % 16 || height % 8))
        fail(error);
    check(data, palette, 1024);
    check(data, pixels, qint64(width) * height);
    QImage image(width, height, QImage::Format_ARGB32);
    QRgb colors[256];
    for (int i = 0; i < 256; ++i) {
        const auto *color = reinterpret_cast<const uchar *>(data.constData()) + palette + 4 * i;
        colors[i] = qRgba(color[0], color[1], color[2], color[3]);
    }
    for (int y = 0; y < height; ++y) {
        auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
        const auto *indices = data.constData() + pixels;
        for (int x = 0; x < width; ++x) {
            const int offset = swizzled ? (y / 8) * width * 8 + (x / 16) * 128 + (y % 8) * 16 + x % 16
                                        : y * width + x;
            out[x] = colors[quint8(indices[offset])];
        }
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || !image.save(&out, "PNG") || !out.commit())
        fail(out.errorString());
    return path;
}

QString saveAudio(QByteArray data) {
    if (data.startsWith("SFX "))
        data.remove(0, 8);
    if (!data.startsWith("VAGp") || data.size() < 48 || u32(data, 12, true) > data.size() - 48)
        fail(missingAsset());
    const auto path = cachePath(data, ".vag");
    if (QFileInfo(path).size() > 0)
        return path;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
        fail(out.errorString());
    return path;
}

int recoverMany(QVector<Question> &questions, bool pictures, QStringList &warnings, const std::atomic_bool *cancel) {
    QMap<QString, QMap<QString, QList<qsizetype>>> jobs;
    for (qsizetype row = 0; row < questions.size(); ++row) {
        const auto &q = questions[row];
        if (pictures ? !q.hasUnresolvedVisualMedia() || !hasPspPicture(q) : !hasPspMusic(q))
            continue;
        const auto ref = pictures ? pictureRef(q) : q.sourceMusic();
        jobs[ref["archive"].toString()][ref["entry"].toString()] << row;
    }
    int recovered = 0;
    const auto warn = [&](const std::exception &e) {
        if (warnings.size() < 5)
            warnings << QString::fromUtf8(e.what());
    };
    for (auto group = jobs.cbegin(); group != jobs.cend(); ++group) {
        if (cancel && cancel->load())
            break;
        try {
            AssetReader reader(group.key());
            for (auto job = group->cbegin(); job != group->cend(); ++job) {
                if (cancel && cancel->load())
                    return recovered;
                const auto &first = questions[job->first()];
                try {
                    const auto bytes = reader.read(pictures ? pictureRef(first) : first.sourceMusic());
                    const auto path = pictures ? savePicture(bytes) : saveAudio(bytes);
                    for (const auto row : job.value()) {
                        auto &q = questions[row];
                        (pictures ? q.image : q.music) = path;
                        q.translations[q.language] = q.localized();
                        ++recovered;
                    }
                } catch (const std::exception &e) {
                    warn(e);
                }
            }
        } catch (const std::exception &e) {
            warn(e);
        }
    }
    return recovered;
}
}

PspAssetIndex readPspAssetIndex(DiscImage &disc) {
    if (!disc.contains(tocPath))
        return {};
    const auto toc = disc.read(tocPath, 16 * 1024 * 1024);
    if (toc.size() < 4)
        fail(missingAsset());
    constexpr qint64 start = 4 + 4096 * 4;
    const auto count = u32(toc, 0);
    if (start + qint64(count) * 12 != toc.size())
        fail(missingAsset());
    PspAssetIndex assets;
    assets.reserve(count);
    for (qint64 p = start; p < toc.size(); p += 12) {
        const auto hash = u32(toc, p);
        const auto offset = qint64(u32(toc, p + 4)) * 2048;
        assets.insert(hash, {offset, u32(toc, p + 8)});
    }
    return assets;
}

quint32 pspStringHash(const QByteArray &key) {
    quint32 hash = 0;
    for (const unsigned char c : key) {
        hash ^= quint32(c) << 24;
        for (int bit = 0; bit < 8; ++bit)
            hash = (hash << 1) ^ (hash & 0x80000000u ? 0x04c11db7u : 0);
    }
    return hash;
}

quint32 pspPathHash(const QString &path, bool lowerCase) {
    auto key = (lowerCase ? path.toLower() : path.toUpper()).toLatin1();
    for (auto &c : key)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            c = '_';
    return pspStringHash(key);
}

QByteArray readPspAsset(const QJsonObject &ref) { return AssetReader(ref["archive"].toString()).read(ref); }
bool hasPspPicture(const Question &q) { return !pictureRef(q).isEmpty(); }
QString recoverPspPicture(const Question &q) { return savePicture(readPspAsset(pictureRef(q))); }
bool hasPspMusic(const Question &q) {
    const auto ref = q.sourceMusic();
    return ref["type"] == "psp-audio" && q.hasUnresolvedMusic() && audioRef(ref);
}
void recoverPspMusic(QVector<Question> &questions, QStringList &warnings, const std::atomic_bool *cancel) {
    recoverMany(questions, false, warnings, cancel);
}
bool hasPspNarration(const Question &q) { return !QFileInfo(q.speech).isFile() && audioRef(narrationRef(q)); }
QString recoverPspNarration(const Question &q) { return saveAudio(readPspAsset(narrationRef(q))); }
QHash<QString, QString> recoverPspNarrationsForBuild(QVector<Question> &questions, const std::atomic_bool *cancel) {
    std::map<QString, std::unique_ptr<AssetReader>> readers;
    QHash<QString, QString> errors;
    for (auto &q : questions) {
        checkCancelled(cancel);
        if (!q.speech.isEmpty())
            continue;
        const auto ref = narrationRef(q);
        const auto source = ref["archive"].toString();
        if (!audioRef(ref) || !QFileInfo(source).isFile())
            continue;
        try {
            auto &reader = readers[source];
            if (!reader)
                reader = std::make_unique<AssetReader>(source);
            q.speech = saveAudio(reader->read(ref));
        } catch (const std::exception &e) {
            errors.insert(q.id, QString::fromUtf8(e.what()));
        }
    }
    return errors;
}
int recoverPspPictures(QVector<Question> &questions, QStringList &warnings, const std::atomic_bool *cancel) {
    return recoverMany(questions, true, warnings, cancel);
}
}
