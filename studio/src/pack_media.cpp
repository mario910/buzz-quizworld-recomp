#include "pack_media.h"
#include "pack_codec.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cmath>

namespace studio::pack {
namespace {
QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(file.errorString());
    if (file.size() > 512 * 1024 * 1024)
        fail(QCoreApplication::translate("PackBuilder", "Plik mediów przekracza 512 MB."));
    return file.readAll();
}
void validateSpeech(const QByteArray &bytes) {
    check(bytes, 0, 32);
    const auto size = u32(bytes, 8, true);
    if ((quint8(bytes[0]) != 5 && quint8(bytes[0]) != 4) || quint8(bytes[2]) != 1 ||
        u16(bytes, 4, true) != 48000 || u32(bytes, 16, true) != 152 || !size || size % 152 ||
        size != u32(bytes, 12, true) || size + 32ull != quint64(bytes.size()))
        fail(QCoreApplication::translate("PackBuilder",
                                         "Nagranie RSF musi być mono ATRAC3 48 kHz, z ramkami 152 bajty."));
}
quint32 sdiChecksum(const QByteArray &header) {
    check(header, 0, 0x50);
    auto bytes = header.left(0x4c);
    const auto a = bytes.mid(0x38, 4), b = bytes.mid(0x3c, 4);
    bytes.replace(0x38, 4, b);
    bytes.replace(0x3c, 4, a);
    return crc32(bytes) ^ 0xffffffffu ^ 0xfabcdef7u;
}
QByteArray timestamp(int prefix, quint64 value) {
    QByteArray result;
    for (int v : {prefix | int((value >> 29) & 14) | 1, int((value >> 22) & 255),
                  int((value >> 14) & 254) | 1, int((value >> 7) & 255), int((value << 1) & 254) | 1})
        result.append(char(v));
    return result;
}
void asciiCount(QByteArray &bytes, int offset, int value) {
    auto text = QByteArray::number(value) + '\0';
    text = text.leftJustified(4, ' ').left(4);
    bytes.replace(offset, 4, text);
}
QByteArray wrapSheet(const QByteArray &source, const QVector<QByteArray> &frames) {
    check(source, 0, 8192);
    if (source.mid(64, 8) != "SDIVIDEO" || u32(source, 0x4c, true) != sdiChecksum(source) ||
        u32(source, 0, true) != 1)
        fail(QCoreApplication::translate("PackBuilder", "Niepoprawny szablon planszy obrazków SDI."));
    auto header = source.left(2048), packs = source.mid(2048, 6144);
    const int count = frames.size(), table = u32(header, 36, true);
    if (count < 1 || table < 0x50 || table + 4 * count > 2048)
        fail(QCoreApplication::translate("PackBuilder", "Za dużo obrazków na jednej planszy SDI."));
    QVector<int> starts;
    QByteArray video;
    int packNo = 3, maximum = 0, total = 0;
    constexpr int muxRate = 0x040f39;
    for (int i = 0; i < count; ++i) {
        const auto &frame = frames[i];
        starts << packNo;
        maximum = std::max(maximum, int(frame.size()));
        total += frame.size();
        int pos = 0;
        bool first = true;
        while (pos < frame.size()) {
            const auto pesHeader =
                first ? QByteArray::fromHex("602e") + timestamp(0x30, i * 3600) + timestamp(0x10, i * 3600)
                      : QByteArray(9, '\xff') + QByteArray::fromHex("602e0f");
            const int room = 2030 - pesHeader.size(), length = std::min(room, int(frame.size() - pos));
            auto pes = pesHeader + frame.mid(pos, length);
            pos += length;
            auto body = QByteArray::fromHex("000001e0");
            put(body, pes.size(), 2, true);
            body += pes;
            if (length < room) {
                const int pad = 2048 - 12 - body.size();
                if (pad >= 6) {
                    body += QByteArray::fromHex("000001be");
                    put(body, pad - 6, 2, true);
                    body += QByteArray(pad - 6, '\xff');
                } else {
                    body.insert(6, QByteArray(pad, '\xff'));
                    patch(body, 4, body.size() - 6, 2, true);
                }
            }
            const auto scr = quint64(std::nearbyint(packNo * 2048. * 90000 / (muxRate * 50.)));
            video += QByteArray::fromHex("000001ba") + timestamp(0x20, scr);
            put(video, (muxRate << 1) | 0x800001, 3, true);
            video += body;
            if (video.size() % 2048)
                fail(QCoreApplication::translate("PackBuilder", "Błąd długości pakietu SDI."));
            first = false;
            ++packNo;
        }
    }
    const auto name = QByteArray("pics001");
    for (int off : {0x40, 0x1c0})
        packs.replace(2048 + off, 8, name + ' ');
    for (auto v : {qMakePair(0xbc, count * 40), qMakePair(0xc0, count), qMakePair(0xc4, maximum),
                   qMakePair(0xc8, total / count)})
        patch(packs, 2048 + v.first, v.second, 4);
    for (int off : {0x68, 0x92, 0x1d5})
        asciiCount(packs, 4096 + off, count);
    asciiCount(packs, 4096 + 0xe6, count - 1);
    const int oldName = packs.indexOf("pics", 4096 + 0x1b0);
    if (oldName >= 4096 + 0x1b0 && oldName < 4096 + 0x1c0)
        packs.replace(oldName, 7, name);
    patch(header, 40, header.size(), 4, true);
    patch(header, 44, header.size(), 4, true);
    patch(header, 48, 2048 + packs.size() + video.size(), 4, true);
    patch(header, 52, count, 4, true);
    header.replace(table, 2048 - table, QByteArray(2048 - table, '\0'));
    for (int i = 0; i < count; ++i)
        patch(header, table + i * 4, 2048 + starts[i] * 2048, 4, true);
    patch(header, 0x4c, sdiChecksum(header), 4, true);
    return header + packs + video;
}
} // namespace
void validateRsfSpeech(const QByteArray &bytes) { validateSpeech(bytes); }
void validateSdi(const QByteArray &bytes) {
    check(bytes, 0, 0x50);
    if (bytes.mid(64, 8) != "SDIVIDEO" || u32(bytes, 0x4c, true) != sdiChecksum(bytes) ||
        u32(bytes, 48, true) != bytes.size())
        fail(QCoreApplication::translate("PackBuilder", "Niepoprawny szablon planszy obrazków SDI."));
    const auto count = u32(bytes, 52, true), offset = u32(bytes, 36, true);
    check(bytes, offset, quint64(count) * 4);
    for (quint32 i = 0; i < count; ++i) {
        const auto packet = u32(bytes, offset + i * 4, true);
        check(bytes, packet, 4);
        if (bytes.mid(packet, 4) != QByteArray::fromHex("000001ba"))
            fail(QCoreApplication::translate("PackBuilder", "Błąd długości pakietu SDI."));
    }
}
namespace {
constexpr int MoviePackSize = 2048, MovieMaxFrames = 900;
constexpr int MovieWidth = 640, MovieHeight = 368, MovieFps = 29970;
constexpr int AdxPairBytes = 36, AdxPairSamples = 32, AudioChunk = 56 * AdxPairBytes;
constexpr int MovieHeaderPacks = 4;

void appendMoviePack(QByteArray &stream, QByteArray body, quint32 mux) {
    // MPEG-1 system clock, in 90 kHz ticks. The encoded rate is in 50 byte/s units.
    const auto rate = ((mux & 0x7fffff) >> 1) * 50ull;
    const auto scr = (quint64(stream.size()) * 90000 + rate / 2) / rate;
    stream += QByteArray::fromHex("000001ba") + timestamp(0x20, scr);
    put(stream, mux, 3, true);
    const int pad = MoviePackSize - 12 - body.size();
    if (pad >= 6) {
        body += QByteArray::fromHex("000001be");
        put(body, pad - 6, 2, true);
        if (pad > 6)
            body += QByteArray(1, '\x0f') + QByteArray(pad - 7, '\xff');
    } else if (pad > 0) {
        body.insert(6, QByteArray(pad, '\xff'));
        patch(body, 4, body.size() - 6, 2, true);
    }
    stream += body;
}

void appendMoviePes(QByteArray &stream, int id, const QByteArray &header, QByteArrayView payload,
                    quint32 mux) {
    QByteArray body = QByteArray::fromHex("000001") + char(id);
    put(body, header.size() + payload.size(), 2, true);
    body += header;
    body.append(payload.data(), payload.size());
    appendMoviePack(stream, std::move(body), mux);
}
} // namespace

bool isMovieTemplate(const QByteArray &bytes) {
    if (bytes.size() < 0x50 || bytes.mid(64, 8) != "SDIVIDEO" ||
        u32(bytes, 0, true) != 0 || u32(bytes, 4, true) != 48000 || u32(bytes, 8, true) != 2 ||
        u32(bytes, 16, true) != MovieWidth || u32(bytes, 20, true) != MovieHeight ||
        u32(bytes, 28, true) != MovieFps || u32(bytes, 48, true) != bytes.size() ||
        u32(bytes, 0x4c, true) != sdiChecksum(bytes))
        return false;
    const auto start = u32(bytes, 40, true);
    if (start < MoviePackSize || start % MoviePackSize ||
        quint64(start) + MovieHeaderPacks * MoviePackSize > quint64(bytes.size()))
        return false;
    const auto mux = u32(bytes, start + 8, true) & 0xffffff;
    return ((mux & 0x7fffff) >> 1) != 0 &&
           bytes.mid(start + 12, 4) == QByteArray::fromHex("000001bb") &&
           bytes.mid(start + MoviePackSize + 12, 4) == QByteArray::fromHex("000001bb") &&
           bytes.mid(start + 2 * MoviePackSize + 0x20, 12) == "SofdecStream" &&
           bytes.mid(start + 3 * MoviePackSize + 0x12, 7) == "CRITAGS";
}

QByteArray encodeMovie(const QByteArray &templateBytes, const QString &source, const QString &ffmpeg,
                       const std::atomic_bool *cancel) {
    if (!isMovieTemplate(templateBytes))
        fail(QStringLiteral("The selected Quiz World template has no compatible SDI movie header."));
    QTemporaryDir temporary;
    if (!temporary.isValid())
        fail(temporary.errorString());
    const auto esPath = temporary.filePath("video.m1v"), adxPath = temporary.filePath("audio.adx");
    const auto input = QStringList{"-nostdin", "-v", "error", "-y", "-protocol_whitelist", "file,pipe",
                                   "-i", QFileInfo(source).absoluteFilePath()};
    runMedia(ffmpeg,
             input + QStringList{"-map", "0:v:0", "-an", "-sn",
                                 // Compute square-pixel dimensions from the display aspect ratio.
                                 // This also works on FFmpeg versions without scale=reset_sar.
                                 "-vf", "scale='max(2,2*trunc(min(640,368*dar)/2))':"
                                        "'max(2,2*trunc(min(368,640/dar)/2))',setsar=1,"
                                        "pad=640:368:(ow-iw)/2:(oh-ih)/2,"
                                        "fps=30000/1001",
                                 "-frames:v", QString::number(MovieMaxFrames + 1), "-threads", "1",
                                 "-c:v", "mpeg1video", "-pix_fmt", "yuv420p", "-g", "1", "-bf", "0",
                                 "-q:v", "4", "-f", "mpeg1video", esPath},
             cancel, esPath);
    auto es = readFile(esPath);
    const auto sequence = QByteArray::fromHex("000001b3");
    QVector<qsizetype> frames;
    for (qsizetype p = es.indexOf(sequence); p >= 0; p = es.indexOf(sequence, p + 4)) {
        check(es, p, 8);
        frames << p;
        // Same MPEG display-aspect code as the game's 640x368 stock movies.
        es[p + 7] = char(0xc0 | (quint8(es[p + 7]) & 15));
    }
    const int count = frames.size();
    if (!count || frames.front() != 0 || count > MovieMaxFrames)
        fail(QStringLiteral("A question movie must contain video and be at most 30 seconds long."));
    frames << es.size();
    const auto duration = QString::number(count * 1001.0 / 30000, 'f', 6);
    runMedia(ffmpeg, input + QStringList{"-map", "0:a:0", "-vn", "-sn", "-af", "apad", "-t", duration,
                                         "-ar", "48000", "-ac", "2", "-c:a", "adpcm_adx",
                                         "-f", "adx", adxPath},
             cancel, adxPath);
    const auto audio = readFile(adxPath);
    check(audio, 0, 20);
    const int adxHeader = u16(audio, 2, true) + 4;
    if (u16(audio, 0, true) != 0x8000 || adxHeader != AdxPairBytes ||
        audio.mid(4, 4) != QByteArray::fromHex("03120402") || u32(audio, 8, true) != 48000 ||
        audio.mid(adxHeader - 6, 6) != "(c)CRI" || audio.size() <= adxHeader)
        fail(QStringLiteral("FFmpeg did not produce the expected stereo 48 kHz ADX stream."));

    // Reuse only the metadata packs from a movie in the user's template.
    const int start = u32(templateBytes, 40, true);
    auto stream = templateBytes.mid(start, MovieHeaderPacks * MoviePackSize);
    const auto mux = u32(stream, 8, true) & 0xffffff;
    const int info = 2 * MoviePackSize, tags = 3 * MoviePackSize;
    for (int off : {0x40, 0x1c0, 0x200})
        stream.replace(info + off, 8, "buzzclip");
    // SFV tags are informational; update fixed-width counts when the template's
    // layout has the expected field delimiters.
    if (stream.mid(tags + 0x5f, 9) == QByteArray("0V029970\0", 9) &&
        stream.mid(tags + 0x1a2, 7) == "SFV:SRC" && stream[tags + 0x6b] == '\0') {
        asciiCount(stream, tags + 0x68, count);
        asciiCount(stream, tags + 0x92, count);
        asciiCount(stream, tags + 0x1d5, count);
        asciiCount(stream, tags + 0xe6, count - 1);
        if (stream.mid(tags + 0x72, 3) == "IPB")
            stream.replace(tags + 0x72, 3, "I  ");
        stream.replace(tags + 0x1b2, 11, "buzzclip.es");
    }

    int a = 0, maximum = 0;
    QVector<int> offsets;
    const auto audioPts = [](int pos) {
        return quint64(pos / AdxPairBytes) * AdxPairSamples * 90000 / 48000;
    };
    auto writeAudio = [&] {
        const int len = std::min(AudioChunk, int(audio.size() - a));
        appendMoviePes(stream, 0xc0, QByteArray::fromHex("4004") + timestamp(0x20, audioPts(a)),
                       QByteArrayView(audio).sliced(a, len), mux);
        a += len;
    };
    for (int i = 0; i < count; ++i) {
        checkCancelled(cancel);
        const quint64 pts = i * 3003ull; // 30000/1001; no B-frames, so DTS == PTS.
        while (a < audio.size() && audioPts(a) <= pts)
            writeAudio();
        offsets << stream.size();
        maximum = std::max(maximum, int(frames[i + 1] - frames[i]));
        for (auto p = frames[i]; p < frames[i + 1];) {
            const auto pes = p == frames[i]
                                 ? QByteArray::fromHex("602e") + timestamp(0x30, pts) + timestamp(0x10, pts)
                                 : QByteArray(9, '\xff') + QByteArray::fromHex("602e0f");
            const auto len = std::min<qsizetype>(2030 - pes.size(), frames[i + 1] - p);
            appendMoviePes(stream, 0xe0, pes, QByteArrayView(es).sliced(p, len), mux);
            p += len;
        }
    }
    while (a < audio.size())
        writeAudio();
    stream += QByteArray::fromHex("000001b9") + QByteArray(MoviePackSize - 4, '\xff');
    const auto durationMs = quint32(count * 1001 / 30);
    for (auto v : {qMakePair(0xbc, durationMs), qMakePair(0xc0, durationMs),
                   qMakePair(0xc4, quint32(count)), qMakePair(0xc8, quint32(maximum)),
                   qMakePair(0xcc, quint32(es.size() / count))})
        patch(stream, info + v.first, v.second, 4);

    constexpr int table = 0x50;
    const int headerSize = ((table + 4 * count + MoviePackSize - 1) / MoviePackSize) * MoviePackSize;
    auto header = templateBytes.left(table);
    header += QByteArray(headerSize - table, '\0');
    patch(header, 36, table, 4, true);
    patch(header, 40, headerSize, 4, true);
    patch(header, 44, headerSize, 4, true);
    patch(header, 48, headerSize + stream.size(), 4, true);
    patch(header, 52, count, 4, true);
    for (int i = 0; i < count; ++i)
        patch(header, table + i * 4, headerSize + offsets[i], 4, true);
    patch(header, 0x4c, sdiChecksum(header), 4, true);
    auto movie = header + stream;
    validateSdi(movie);
    return movie;
}
QByteArray unwrapWem(const QByteArray &bytes) {
    if (bytes.left(4) != "RIFF" || bytes.mid(8, 4) != "WAVE")
        fail(QCoreApplication::translate("PackBuilder", "Niepoprawny kontener nagrania WEM."));
    QMap<QByteArray, QByteArray> chunks;
    int chunkCount = 0;
    for (qint64 p = 12; p + 8 <= bytes.size();) {
        const auto name = bytes.mid(p, 4);
        const auto length = u32(bytes, p + 4);
        check(bytes, p + 8, length);
        if (++chunkCount > 10000)
            fail(QCoreApplication::translate("PackBuilder", "Niepoprawny kontener nagrania WEM."));
        if (name == "fmt " || name == "data") {
            if (chunks.contains(name))
                fail(QCoreApplication::translate("PackBuilder", "Powtórzony blok WEM."));
            chunks[name] = bytes.mid(p + 8, length);
        }
        p += 8ll + length + (length & 1);
    }
    const auto fmt = chunks.value("fmt "), data = chunks.value("data");
    check(fmt, 0, 32);
    if (u16(fmt, 0) != 0xfffc || data.isEmpty())
        fail(QCoreApplication::translate("PackBuilder", "Obsługiwane są nagrania Wwise ATRAC9 z KiP."));
    QByteArray converted;
    put(converted, 0xfffe, 2);
    put(converted, u16(fmt, 2), 2);
    put(converted, u32(fmt, 4), 4);
    put(converted, u32(fmt, 8), 4);
    put(converted, u16(fmt, 12), 2);
    put(converted, 0, 2);
    put(converted, 34, 2);
    put(converted, u16(fmt, 18), 2);
    put(converted, u32(fmt, 20), 4);
    converted += QByteArray::fromHex("d242e147ba368d4d88fc61654f8c836c");
    put(converted, 1, 4);
    converted += fmt.mid(24, 4);
    converted += QByteArray(4, '\0');
    QByteArray body("WAVEfmt ");
    put(body, converted.size(), 4);
    body += converted;
    body += "fact";
    put(body, 12, 4);
    put(body, u32(fmt, 28), 4);
    body += QByteArray(8, '\0');
    body += "data";
    put(body, data.size(), 4);
    body += data;
    QByteArray result("RIFF");
    put(result, body.size(), 4);
    return result + body;
}
QByteArray encodeSpeech(const QString &source, const QString &ffmpeg, double gain,
                        const std::atomic_bool *cancel) {
    checkCancelled(cancel);
    if (source.endsWith(".rsf", Qt::CaseInsensitive)) {
        const auto bytes = readFile(source);
        validateSpeech(bytes);
        return bytes;
    }
    QFile original(source);
    if (!original.open(QIODevice::ReadOnly))
        fail(original.errorString());
    if (original.size() > 512 * 1024 * 1024)
        fail(QCoreApplication::translate("PackBuilder", "Plik mediów przekracza 512 MB."));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData("studio-atrac-v1:");
    hash.addData(QByteArray::number(gain, 'g', 12));
    while (!original.atEnd()) {
        checkCancelled(cancel);
        const auto block = original.read(1024 * 1024);
        if (block.isEmpty() && original.error() != QFileDevice::NoError)
            fail(original.errorString());
        hash.addData(block);
    }
    const auto cacheDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("encoded-speech");
    const auto cached = QDir(cacheDir).filePath(QString::fromLatin1(hash.result().toHex()) + ".rsf");
    if (QFileInfo::exists(cached)) {
        auto bytes = readFile(cached);
        validateSpeech(bytes);
        return bytes;
    }
    QTemporaryDir temp;
    if (!temp.isValid())
        fail(QCoreApplication::translate("PackBuilder", "Nie można utworzyć katalogu roboczego mediów."));
    const auto path = temp.filePath("audio.pcm");
    auto inputPath = source;
    if (source.endsWith(".wem", Qt::CaseInsensitive)) {
        inputPath = temp.filePath("voice.at9");
        const auto bytes = unwrapWem(readFile(source));
        QFile input(inputPath);
        if (!input.open(QIODevice::WriteOnly) || input.write(bytes) != bytes.size())
            fail(input.errorString());
        input.close();
    }
    runMedia(ffmpeg,
             {"-v", "error", "-nostdin", "-y", "-protocol_whitelist", "file,pipe", "-i", inputPath, "-ac",
              "1", "-ar", "48000", "-af", QString("volume=%1dB").arg(gain, 0, 'g', 12), "-f", "s16le", path},
             cancel, path);
    QFile pcm(path);
    if (!pcm.open(QIODevice::ReadOnly) || pcm.size() > 48000ll * 2 * 600)
        fail(QCoreApplication::translate("PackBuilder", "Niepoprawny rozmiar nagrania PCM."));
    const auto encoded = encodeAtracPcm(pcm.readAll(), cancel);
    validateSpeech(encoded);
    if (QDir().mkpath(cacheDir)) {
        QSaveFile out(cached);
        if (out.open(QIODevice::WriteOnly) && out.write(encoded) == encoded.size())
            out.commit();
    }
    return encoded;
}
QByteArray pictureSheet(const QByteArray &templateBytes, const QStringList &images, const QString &ffmpeg,
                        const std::atomic_bool *cancel) {
    if (images.size() > 427)
        fail(QCoreApplication::translate("PackBuilder",
                                         "Paczka może zawierać najwyżej 427 różnych obrazków."));
    QTemporaryDir temp;
    if (!temp.isValid())
        fail(QCoreApplication::translate("PackBuilder", "Nie można utworzyć katalogu roboczego mediów."));
    for (int i = 0; i <= images.size(); ++i) {
        checkCancelled(cancel);
        QImage canvas(640, 360, QImage::Format_RGB32);
        canvas.fill(i ? Qt::black : QColor(0x20, 0x20, 0x60));
        if (i) {
            QImageReader reader(images[i - 1]);
            reader.setAutoTransform(true);
            auto image = reader.read();
            if (image.isNull())
                fail(QCoreApplication::translate("PackBuilder", "Nie można odczytać obrazka: ") +
                     images[i - 1]);
            image = image.scaled(640, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPainter painter(&canvas);
            painter.drawImage((640 - image.width()) / 2, (360 - image.height()) / 2, image);
        }
        if (!canvas.scaled(640, 368, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                 .save(temp.filePath(QString("f%1.ppm").arg(i, 5, 10, QLatin1Char('0'))), "PPM"))
            fail(QCoreApplication::translate("PackBuilder", "Nie można zapisać obrazu roboczego."));
    }
    const auto output = temp.filePath("video.m1v");
    runMedia(ffmpeg, {"-v",         "error",
                      "-nostdin",   "-y",
                      "-framerate", "25",
                      "-i",         temp.filePath("f%05d.ppm"),
                      "-vf",        "format=yuv420p",
                      "-c:v",       "mpeg1video",
                      "-threads",   "1",
                      "-g",         "1",
                      "-q:v",       "2",
                      "-f",         "mpeg1video",
                      output},
             cancel);
    auto elementary = readFile(output);
    QVector<int> starts;
    for (int p = elementary.indexOf(QByteArray::fromHex("000001b3")); p >= 0;
         p = elementary.indexOf(QByteArray::fromHex("000001b3"), p + 4))
        starts << p;
    if (starts.size() != images.size() + 1)
        fail(QCoreApplication::translate("PackBuilder", "Niepoprawna liczba klatek planszy obrazków."));
    QVector<QByteArray> frames;
    for (int i = 0; i < starts.size(); ++i) {
        const int p = starts[i];
        check(elementary, p, 12);
        elementary[p + 7] = char(0xc0 | (quint8(elementary[p + 7]) & 15));
        patch(elementary, p + 8, (u32(elementary, p + 8, true) & ~(0x3ffu << 3)) | (608 << 3), 4, true);
        frames << elementary.mid(p, (i + 1 < starts.size() ? starts[i + 1] : elementary.size()) - p);
    }
    return wrapSheet(templateBytes, frames);
}
} // namespace studio::pack
