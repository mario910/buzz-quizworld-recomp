#include "source_movie.h"
#include "disc_image.h"
#include "media_tools.h"
#include "pack_format.h"
#include "ps3_source.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>

namespace studio {
namespace {
using namespace pack;
constexpr int maxClip = 128 * 1024 * 1024;
QJsonObject movie(const Question &q) {
    static const QRegularExpression sdi(R"(^[A-Z0-9_]+/QUESTIONASSETS/VIDEO/[0-9]+\.sdi$)",
                                        QRegularExpression::CaseInsensitiveOption);
    const auto media = q.translations.isEmpty() ? q.extra["source_media"] : q.localized()["source_media"];
    for (const auto &item : media.toArray()) {
        const auto ref = item.toObject();
        if (ref["archive"].toString().isEmpty())
            continue;
        const auto entry = ref["entry"].toString();
        if ((ref["type"] == 2 && sdi.match(entry).hasMatch()) ||
            (ref["type"] == "psp-video" && entry.endsWith(".pmf", Qt::CaseInsensitive)))
            return ref;
    }
    return {};
}

QByteArray readPmf(const QString &source, const QString &entry) {
    if (DiscImage::isDiscImage(source))
        return DiscImage(source).read(entry, maxClip);
    // A loose QDB keeps the movie under the same extracted locale/data root.
    auto root = QFileInfo(source).dir();
    if (QFileInfo(source).fileName().compare("MERGED.QDB", Qt::CaseInsensitive) != 0)
        fail(QStringLiteral("Open the original PSP disc image or MERGED.QDB."));
    if (root.dirName().compare("QUESTION_DATA", Qt::CaseInsensitive) != 0 && !root.cdUp())
        fail(QStringLiteral("PSP question archive has no media directory."));
    if (!root.cdUp())
        fail(QStringLiteral("PSP question archive has no media directory."));
    const QFileInfo info(entry);
    const auto parent = root.canonicalPath();
    const auto file = info.canonicalFilePath();
    if (parent.isEmpty() || file.isEmpty() || !file.startsWith(parent + '/') ||
        info.size() > maxClip)
        fail(QStringLiteral("The PSP question clip was not found in the extracted data folder."));
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        fail(f.errorString());
    return f.readAll();
}

// PSMF audio is carried in MPEG-2 private PES packets. FFmpeg's MPEG-PS
// demuxer reads the AVC but doesn't expose the PSP ATRAC3+ substream.
// Repackage its fixed-size codec frames in an unencrypted EA3 header for FFmpeg.
QByteArray psmfAudio(const QByteArray &data, qint64 start, qint64 end, double &delay) {
    qint64 audioPts = -1, videoPts = -1;
    QByteArray packets;
    int channel = -1;
    const auto byte = [&](qint64 i) { return quint8(data[i]); };
    for (auto p = start; p + 4 <= end;) {
        if ((u32(data, p, true) >> 8) != 1)
            fail(QStringLiteral("Invalid PSP PSMF packet boundary."));
        const auto code = byte(p + 3);
        if (code == 0xb9)
            break;
        if (code == 0xba) {
            if (p + 14 > end || (byte(p + 4) & 0xc0) != 0x40)
                fail(QStringLiteral("Invalid PSP PSMF pack header."));
            p += 14 + (byte(p + 13) & 7);
            continue;
        }
        if (p + 6 > end)
            fail(QStringLiteral("Truncated PSP PSMF packet."));
        const auto next = p + 6 + u16(data, p + 4, true);
        if (next > end)
            fail(QStringLiteral("Truncated PSP PSMF packet."));
        if (code == 0xbd || (code >= 0xe0 && code < 0xf0)) {
            if (p + 9 > next || (byte(p + 6) & 0xc0) != 0x80)
                fail(QStringLiteral("Invalid PSP PSMF PES header."));
            const auto at = p + 9 + byte(p + 8);
            if (at > next)
                fail(QStringLiteral("Invalid PSP PSMF PES payload."));
            qint64 pts = -1;
            if ((byte(p + 7) & 0x80) && at >= p + 14)
                pts = (qint64(byte(p + 9) & 0x0e) << 29) | (qint64(byte(p + 10)) << 22) |
                      (qint64(byte(p + 11) & 0xfe) << 14) | (qint64(byte(p + 12)) << 7) |
                      (byte(p + 13) >> 1);
            if (code == 0xbd && at + 4 <= next && (channel == -1 || channel == byte(at))) {
                channel = byte(at);
                if (audioPts < 0)
                    audioPts = pts;
                packets.append(data.constData() + at + 4, next - at - 4);
            } else if (code != 0xbd && videoPts < 0) {
                videoPts = pts;
            }
        }
        p = next;
    }
    if (packets.size() < 8 || u16(packets, 0, true) != 0x0fd0)
        fail(QStringLiteral("The PSP clip has no recognised ATRAC3+ track."));
    const auto config = u16(packets, 2, true);
    const auto frame = 8 + (config & 0x3ff) * 8;
    QByteArray oma(96, '\0');
    oma.replace(0, 8, QByteArray::fromHex("454133010060ffff"));
    oma[32] = 1; // ATRAC3+
    qToBigEndian(config, oma.data() + 34);
    const auto total = frame + 8;
    if (packets.size() % total != 0)
        fail(QStringLiteral("Truncated PSP ATRAC3+ track."));
    for (qsizetype p = 0; p < packets.size(); p += total) {
        if (u16(packets, p, true) != 0x0fd0 || u16(packets, p + 2, true) != config)
            fail(QStringLiteral("Invalid PSP ATRAC3+ frame alignment."));
        oma.append(packets.constData() + p + 8, frame);
    }
    if (audioPts >= 0 && videoPts >= 0)
        delay = (audioPts - videoPts) / 90000.0;
    return oma;
}

void writeOnce(const QString &file, const QByteArray &bytes) {
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        fail(output.errorString());
}
} // namespace

bool hasSourceQuestionMedia(const Question &q) { return !movie(q).isEmpty() || hasPs2Media(q); }

QString sourceQuestionClipKey(const Question &q) {
    const auto ref = movie(q);
    return ref.isEmpty() ? ps2ClipKey(q) : ref["archive"].toString() + '\n' + ref["entry"].toString();
}

Ps2Media decodeSourceQuestionMedia(const Question &q, const QString &ffmpeg, const std::atomic_bool *cancel) {
    const auto ref = movie(q);
    if (ref.isEmpty())
        return decodePs2Media(q, ffmpeg, cancel);
    try {
        checkCancelled(cancel);
        if (ffmpeg.isEmpty())
            fail(missingMediaFfmpeg());
        const auto source = ref["archive"].toString(), entry = ref["entry"].toString();
        const bool psp = ref["type"] == "psp-video";
        const auto data = psp ? readPmf(source, entry) : ps3::readMovie(ref["source_path"].toString(), source, entry);
        check(data, 0, psp ? 16 : 80);
        const qint64 start = u32(data, psp ? 8 : 40, true);
        const qint64 size = psp ? u32(data, 12, true) : data.size() - start;
        if (start < (psp ? 16 : 80) || size <= 0 || start + size > data.size() ||
            (psp ? !data.startsWith("PSMF")
                 : (data.mid(64, 8) != "SDIVIDEO" || u32(data, 0, true) != 0 || u32(data, 48, true) != data.size())))
            fail(QStringLiteral("Unsupported question movie header."));
        const auto key = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        const QDir cache(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("source-clips"));
        if (!QDir().mkpath(cache.absolutePath()))
            fail(QStringLiteral("Could not create a media cache directory."));
        const auto stem = cache.filePath((psp ? "pmf-v1-" : "sdi-v1-") + key);
        if (QFileInfo(stem + ".mp4").size() > 0 && QFileInfo(stem + ".png").size() > 0)
            return {stem + ".png", stem + ".mp4", {}};
        QTemporaryDir temp(cache.filePath("decode-XXXXXX"));
        if (!temp.isValid())
            fail(temp.errorString());
        const auto input = temp.filePath("clip.mpg");
        writeOnce(input, data.mid(start, size));
        const QStringList base{"-nostdin", "-v", "error", "-xerror", "-y", "-protocol_whitelist", "file,pipe"};
        QStringList args = base + QStringList{"-f", "mpeg", "-i", input};
        if (psp) {
            double delay = 0;
            const auto oma = psmfAudio(data, start, start + size, delay);
            const auto audio = temp.filePath("sound.oma");
            writeOnce(audio, oma);
            args << "-itsoffset" << QString::number(delay, 'f', 6) << "-f" << "oma" << "-i" << audio;
        }
        const auto video = temp.filePath("clip.mp4"), image = temp.filePath("poster.png");
        args << "-map" << "0:v:0" << "-map" << (psp ? "1:a:0" : "0:a:0");
        if (!psp && u32(data, 16, true) == 640 && u32(data, 20, true) == 368)
            args << "-vf" << "scale=640:360,setsar=1";
        args << "-c:v" << "mpeg4" << "-q:v" << "3" << "-c:a" << "aac" << "-b:a" << "128k" << video;
        runMedia(ffmpeg, args, cancel, video);
        runMedia(ffmpeg, base + QStringList{"-i", video, "-vf", "thumbnail=50", "-frames:v", "1", image}, cancel);
        for (const auto &pair : {qMakePair(video, stem + ".mp4"), qMakePair(image, stem + ".png")})
            if (!QFile::rename(pair.first, pair.second) && QFileInfo(pair.second).size() <= 0)
                fail(QStringLiteral("FFmpeg did not produce the decoded media."));
        return {stem + ".png", stem + ".mp4", {}};
    } catch (const std::exception &e) {
        return {{}, {}, QString::fromUtf8(e.what())};
    }
}
} // namespace studio
