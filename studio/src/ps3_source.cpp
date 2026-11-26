#include "ps3_source.h"
#include "disc_image.h"
#include "pack_format.h"
#include "ps4_pkg.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageAuthenticationCode>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QtEndian>
#include <cstring>

namespace studio::ps3 {
namespace {
using pack::Archive;
using pack::check;
using pack::fail;
using ps4::aesCbc;
using ps4::aesEcb;

QString tr(const char *text) { return QCoreApplication::translate("Ps3Source", text); }
quint16 be16(const QByteArray &b, qint64 off) {
    check(b, off, 2);
    return qFromBigEndian<quint16>(b.constData() + off);
}
quint32 be32(const QByteArray &b, qint64 off) {
    check(b, off, 4);
    return qFromBigEndian<quint32>(b.constData() + off);
}
quint64 be64(const QByteArray &b, qint64 off) {
    check(b, off, 8);
    return qFromBigEndian<quint64>(b.constData() + off);
}
QByteArray hex(const char *text) { return QByteArray::fromHex(text); }
QByteArray xorBytes(const QByteArray &a, const QByteArray &b) {
    QByteArray out(a.size(), Qt::Uninitialized);
    for (qsizetype i = 0; i < a.size(); ++i)
        out[i] = char(a[i] ^ b[i % b.size()]);
    return out;
}

// Published platform constants (not title or buyer specific). The PKG key is the
// one BuzzSetup already uses for update packages (preparation/src/import.cpp);
// the NPDRM ones match ps3recomp/libs/filesystem/edat.c.
const QByteArray pkgKey = hex("2e7b71d7c9c9a14ea3221f188828b8f8");
const QByteArray omacKey2 = hex("6ba52976efda16ef3c339fb2971e256b");
const QByteArray sdatKey = hex("0d655ef8e674a98ab8505cfa7d012933");
const QByteArray edatKey0 = hex("be959ca8308defa2e5e180c63712a9ae");
const QByteArray edatKey1 = hex("4ca9c14b01c95309969bec68aa0bc081");
const QByteArray freeLicenses[] = {
    hex("72f990788f9cff745725f08e4c128387"), // NP_KLIC_FREE
    hex("52c0b5ca76d6134bb45fc66ca637f2c1"), // NP_PSX_KEY
    hex("2a6afbcf43d1579f7d738741a13bd42e"), // NP_PSP_KEY_1
    hex("0db85732366cd734fc879e743343bb4f"), // NP_PSP_KEY_2
};
// RAP -> key conversion (rap_initial_key, pbox, e1, e2).
const QByteArray rapKey = hex("869f7745c13fd890ccf29188e3cc3edf");
const QByteArray rapPbox = hex("0c030604010b0f08020700050a0e0d09");
const QByteArray rapE1 = hex("a93e1fd67c55a329b75fdda62a95c7a5");
const QByteArray rapE2 = hex("67d45da3296d006a4e7c537bf5538c74");

// AES-CMAC (RFC 4493) through the platform's AES.
QByteArray cmac(const QByteArray &key, const QByteArray &message) {
    const auto shift = [](const QByteArray &in) {
        QByteArray out(16, '\0');
        for (int i = 0; i < 16; ++i)
            out[i] = char((quint8(in[i]) << 1) | (i < 15 ? quint8(in[i + 1]) >> 7 : 0));
        if (quint8(in[0]) & 0x80)
            out[15] = char(out[15] ^ 0x87);
        return out;
    };
    const auto k1 = shift(aesEcb(QByteArray(16, '\0'), key, true));
    const auto k2 = shift(k1);
    QByteArray m = message;
    if (!m.isEmpty() && m.size() % 16 == 0) {
        for (int i = 0; i < 16; ++i)
            m[m.size() - 16 + i] = char(m[m.size() - 16 + i] ^ k1[i]);
    } else {
        m.append('\x80');
        while (m.size() % 16)
            m.append('\0');
        for (int i = 0; i < 16; ++i)
            m[m.size() - 16 + i] = char(m[m.size() - 16 + i] ^ k2[i]);
    }
    return aesCbc(m, key, QByteArray(16, '\0'), true).right(16);
}

// ---------------------------------------------------------------- PARAM.SFO
QMap<QString, QString> paramSfo(const QByteArray &sfo) {
    QMap<QString, QString> out;
    if (!sfo.startsWith(QByteArray("\0PSF", 4)) || sfo.size() < 20)
        return out;
    const auto keys = qFromLittleEndian<quint32>(sfo.constData() + 8);
    const auto data = qFromLittleEndian<quint32>(sfo.constData() + 12);
    const auto count = qFromLittleEndian<quint32>(sfo.constData() + 16);
    for (quint32 i = 0; i < count && 20 + qint64(i + 1) * 16 <= sfo.size(); ++i) {
        const char *e = sfo.constData() + 20 + i * 16;
        const auto keyAt = qint64(keys) + qFromLittleEndian<quint16>(e);
        const auto format = qFromLittleEndian<quint16>(e + 2);
        const auto length = qFromLittleEndian<quint32>(e + 4);
        const auto dataAt = qint64(data) + qFromLittleEndian<quint32>(e + 12);
        if (keyAt >= sfo.size() || dataAt + length > sfo.size() || format != 0x0204)
            continue;
        const auto keyEnd = sfo.indexOf('\0', keyAt);
        if (keyEnd < 0)
            continue;
        const auto key = QString::fromLatin1(sfo.constData() + keyAt, keyEnd - keyAt);
        out[key] = QString::fromUtf8(sfo.mid(dataAt, length)).remove(QChar(0)).simplified();
    }
    return out;
}

// ---------------------------------------------------------------- devices
class FileRange final : public RangeDevice {
  public:
    FileRange(std::shared_ptr<QFile> file, qint64 base, qint64 size)
        : RangeDevice(size), file_(std::move(file)), base_(base) {
        if (base < 0 || size < 0 || base + size > file_->size())
            fail(tr("Plik jest ucięty."));
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

  protected:
    QByteArray readRange(qint64 offset, qint64 length) override {
        if (!file_->seek(base_ + offset))
            fail(file_->errorString());
        const auto out = file_->read(length);
        if (out.size() != length)
            fail(tr("Plik jest ucięty."));
        return out;
    }

  private:
    std::shared_ptr<QFile> file_;
    qint64 base_;
};

std::shared_ptr<QFile> openFile(const QString &path) {
    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::ReadOnly))
        fail(file->errorString());
    return file;
}

// A PS3 package (.pkg). Retail packages use AES-CTR with the published package
// key; debug packages a SHA-1 keystream derived from the header.
class Package {
  public:
    struct Item {
        QString name;
        qint64 offset = 0, size = 0;
        quint32 flags = 0;
    };
    explicit Package(const QString &path) : file_(openFile(path)) {
        header_ = file_->read(0xc0);
        if (!header_.startsWith("\x7f" "PKG") || header_.size() < 0xc0)
            fail(tr("To nie jest pakiet PS3."));
        revision_ = be16(header_, 4);
        const auto type = be16(header_, 6);
        if (type == 2)
            fail(tr("To pakiet PSP, nie PS3."));
        if (type != 1 || (revision_ != 0 && revision_ != 0x8000))
            fail(tr("Nieobsługiwany pakiet PS3."));
        const auto count = be32(header_, 0x14);
        offset_ = qint64(be64(header_, 0x20));
        length_ = qint64(be64(header_, 0x28));
        contentId = QString::fromLatin1(header_.mid(0x30, 0x30)).remove(QChar(0));
        if (count == 0 || count > 100000 || offset_ < 0xc0 || length_ < 0 || offset_ + length_ > file_->size() ||
            qint64(count) * 32 > length_)
            fail(tr("Uszkodzony pakiet PS3."));
        const auto table = read(0, qint64(count) * 32);
        for (quint32 i = 0; i < count; ++i) {
            const auto at = qint64(i) * 32;
            const auto nameAt = qint64(be32(table, at)), nameSize = qint64(be32(table, at + 4));
            Item item;
            item.offset = qint64(be64(table, at + 8));
            item.size = qint64(be64(table, at + 16));
            item.flags = be32(table, at + 24);
            if (nameSize <= 0 || nameSize > 4096 || nameAt < 0 || nameAt + nameSize > length_ || item.offset < 0 ||
                item.size < 0 || item.offset + item.size > length_)
                fail(tr("Uszkodzony pakiet PS3."));
            item.name = QString::fromUtf8(read(nameAt, nameSize)).remove(QChar(0));
            if ((item.flags & 0xff) != 4)
                items.insert(item.name.toUpper(), item);
        }
    }
    QString contentId;
    QMap<QString, Item> items; // Keyed by upper-case path.
    QByteArray read(qint64 at, qint64 amount) {
        if (at < 0 || amount < 0 || at + amount > length_)
            fail(tr("Odczyt poza pakietem PS3."));
        const qint64 first = at / 16, skip = at % 16, blocks = (skip + amount + 15) / 16;
        if (!file_->seek(offset_ + first * 16))
            fail(file_->errorString());
        auto data = file_->read(std::min(blocks * 16, length_ - first * 16));
        if (data.size() < skip + amount)
            fail(tr("Pakiet PS3 jest ucięty."));
        data.resize(blocks * 16);
        QByteArray stream(blocks * 16, Qt::Uninitialized);
        if (revision_ == 0x8000) {
            // Counter = header IV + block number (128-bit big-endian).
            const auto hi = be64(header_, 0x70), lo = be64(header_, 0x78);
            for (qint64 i = 0; i < blocks; ++i) {
                const quint64 n = quint64(first + i), low = lo + n;
                qToBigEndian<quint64>(hi + (low < lo ? 1 : 0), stream.data() + i * 16);
                qToBigEndian<quint64>(low, stream.data() + i * 16 + 8);
            }
            stream = aesEcb(stream, pkgKey, true);
        } else {
            const auto qa = header_.mid(0x60, 16);
            auto input = qa.left(8).repeated(2) + qa.mid(8).repeated(2) + QByteArray(32, '\0');
            for (qint64 i = 0; i < blocks; ++i) {
                qToBigEndian<quint64>(quint64(first + i), input.data() + 0x38);
                std::memcpy(stream.data() + i * 16,
                            QCryptographicHash::hash(input, QCryptographicHash::Sha1).constData(), 16);
            }
        }
        for (qint64 i = 0; i < data.size(); ++i)
            data[i] = char(data[i] ^ stream[i]);
        return data.mid(skip, amount);
    }

  private:
    std::shared_ptr<QFile> file_;
    QByteArray header_;
    int revision_ = 0;
    qint64 offset_ = 0, length_ = 0;
};

class PackageEntry final : public RangeDevice {
  public:
    PackageEntry(std::shared_ptr<Package> package, Package::Item item)
        : RangeDevice(item.size), package_(std::move(package)), item_(std::move(item)) {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

  protected:
    QByteArray readRange(qint64 offset, qint64 length) override {
        return package_->read(item_.offset + offset, length);
    }

  private:
    std::shared_ptr<Package> package_;
    Package::Item item_;
};

// ---------------------------------------------------------------- NPD (EDAT/SDAT)
struct Npd {
    QByteArray header;
    quint32 version = 0, license = 0, flags = 0, blockSize = 0;
    quint64 fileSize = 0;
    QString contentId;
    QByteArray digest() const { return header.mid(0x40, 16); }
    QByteArray devHash() const { return header.mid(0x60, 16); }
};
constexpr quint32 sdatFlag = 0x01000000u, compressedFlag = 0x1u, encryptedKeyFlag = 0x8u, flag10 = 0x10u,
                  flag20 = 0x20u, debugFlag = 0x80000000u;

Npd readNpd(QIODevice &device) {
    Npd npd;
    device.seek(0);
    npd.header = device.read(0x100);
    if (npd.header.size() != 0x100 || !npd.header.startsWith(QByteArray("NPD\0", 4)))
        fail(tr("To nie jest plik EDAT/SDAT (NPD)."));
    npd.version = be32(npd.header, 4);
    npd.license = be32(npd.header, 8);
    npd.contentId = QString::fromLatin1(npd.header.mid(0x10, 0x30)).remove(QChar(0));
    npd.flags = be32(npd.header, 0x80);
    npd.blockSize = be32(npd.header, 0x84);
    npd.fileSize = be64(npd.header, 0x88);
    if (npd.blockSize == 0 || npd.blockSize > 16u * 1024 * 1024 || npd.blockSize % 16 ||
        npd.fileSize > quint64(device.size()))
        fail(tr("Uszkodzony nagłówek EDAT."));
    if (npd.flags & (compressedFlag | debugFlag))
        fail(tr("Skompresowane lub testowe pliki EDAT nie są obsługiwane."));
    return npd;
}

QString rememberedLicense(const QString &contentId) {
    return QSettings().value("ps3Licenses/" + contentId.toUpper()).toString();
}

// Try the license the user chose first, then the files found beside the source
// or in an emulator/console layout (home/<user>/exdata).
QStringList findLicenses(const QString &contentId, const QStringList &folders) {
    QStringList paths;
    const QFileInfo remembered(rememberedLicense(contentId));
    if (remembered.isFile() && remembered.size() == 16)
        paths << remembered.absoluteFilePath();
    const auto wanted = contentId.toUpper();
    const auto matches = [&](const QFileInfo &info) {
        return info.isFile() && info.size() == 16 && info.completeBaseName().toUpper() == wanted;
    };
    for (const auto &folder : folders) {
        // The source's folder, then up to dev_hdd0 (game/<title>/USRDIR/PACKnnnn).
        QDir dir(folder);
        for (int level = 0; level <= 5; ++level) {
            for (const auto &info : dir.entryInfoList({"*.rap", "*.RAP"}, QDir::Files))
                if (matches(info))
                    paths << info.absoluteFilePath();
            QDirIterator users(dir.filePath("home"), QDir::Dirs | QDir::NoDotAndDotDot);
            while (users.hasNext()) {
                const QFileInfo info(QDir(users.next()).filePath("exdata/" + contentId + ".rap"));
                if (matches(info))
                    paths << info.absoluteFilePath();
            }
            if (!dir.cdUp())
                break;
        }
    }
    paths.removeDuplicates();
    return paths;
}

QByteArray embeddedKey(const Npd &npd) {
    if (npd.flags & sdatFlag)
        return xorBytes(npd.devHash(), sdatKey);
    // dev_hash = CMAC(klicensee ^ NP_OMAC_KEY_2) over the first 0x60 header bytes:
    // the file names the free key it is under.
    for (const auto &klic : freeLicenses)
        if (cmac(xorBytes(klic, omacKey2), npd.header.left(0x60)) == npd.devHash())
            return klic;
    fail(tr("Ten plik EDAT jest zabezpieczony kluczem gry, nie licencją. Takie pliki zawierają dane gry, "
            "a nie paczki pytań."));
}

class NpdDevice final : public RangeDevice {
  public:
    NpdDevice(std::unique_ptr<QIODevice> source, Npd npd)
        : RangeDevice(qint64(npd.fileSize)), source_(std::move(source)), npd_(std::move(npd)) {
        blocks_ = (npd_.fileSize + npd_.blockSize - 1) / npd_.blockSize;
        metaSize_ = (npd_.flags & (compressedFlag | flag20)) ? 0x20 : 0x10;
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    bool tryKey(QByteArray key) {
        key_ = std::move(key);
        cachedIndex_ = ~quint64(0);
        return size() == 0 || !block(0).isEmpty();
    }

  private:
    // Decrypts one block, or returns empty when its hash does not match the key.
    QByteArray block(quint64 index) {
        if (index == cachedIndex_)
            return cached_;
        const bool leads = npd_.flags & flag20;
        const quint64 metaAt = leads ? 0x100 + index * (metaSize_ + npd_.blockSize) : 0x100 + index * metaSize_;
        const quint64 dataAt = leads ? metaAt + metaSize_ : 0x100 + blocks_ * metaSize_ + index * npd_.blockSize;
        source_->seek(qint64(metaAt));
        const auto meta = source_->read(metaSize_);
        quint64 length = npd_.blockSize;
        if (index == blocks_ - 1 && npd_.fileSize % npd_.blockSize)
            length = npd_.fileSize % npd_.blockSize;
        const auto padded = qint64((length + 15) & ~quint64(15));
        source_->seek(qint64(dataAt));
        const auto encrypted = source_->read(padded);
        if (meta.size() != qint64(metaSize_) || encrypted.size() != padded)
            fail(tr("Plik EDAT jest ucięty."));
        const auto expected = metaSize_ == 0x20 ? xorBytes(meta.left(16), meta.mid(16, 16)) : meta.left(16);
        QByteArray seed(16, '\0');
        if (npd_.version > 1)
            seed.replace(0, 12, npd_.devHash().left(12));
        qToBigEndian<quint32>(quint32(index), seed.data() + 12);
        const auto blockKey = aesEcb(seed, key_, true);
        const auto hashSeed = (npd_.flags & flag10) ? aesEcb(blockKey, key_, true) : blockKey;
        QByteArray cipherKey = blockKey, hashKey = hashSeed;
        if (npd_.flags & encryptedKeyFlag) {
            const auto &erk = npd_.version == 4 ? edatKey1 : edatKey0;
            cipherKey = aesEcb(blockKey, erk, false);
            hashKey = aesEcb(hashSeed, erk, false);
        }
        QByteArray mac;
        if (!(npd_.flags & flag10))
            mac = cmac(hashKey, encrypted);
        else
            mac = QMessageAuthenticationCode::hash(
                encrypted, (npd_.flags & flag20) ? hashKey + QByteArray(4, '\0') : hashKey,
                QCryptographicHash::Sha1);
        if (mac.left(16) != expected)
            return {};
        cached_ = aesCbc(encrypted, cipherKey, npd_.version <= 1 ? QByteArray(16, '\0') : npd_.digest(), false)
                      .left(qint64(length));
        cachedIndex_ = index;
        return cached_;
    }

    QByteArray readRange(qint64 offset, qint64 length) override {
        QByteArray out;
        out.reserve(length);
        while (length > 0) {
            const auto index = quint64(offset) / npd_.blockSize;
            const auto plain = block(index);
            if (plain.isEmpty())
                fail(tr("Plik EDAT jest uszkodzony (niezgodna suma kontrolna bloku)."));
            const auto at = qint64(quint64(offset) % npd_.blockSize);
            const auto take = std::min<qint64>(length, plain.size() - at);
            out.append(plain.constData() + at, take);
            offset += take;
            length -= take;
        }
        return out;
    }

    std::unique_ptr<QIODevice> source_;
    Npd npd_;
    QByteArray key_;
    quint64 blocks_ = 0, cachedIndex_ = ~quint64(0);
    quint32 metaSize_ = 0x10;
    QByteArray cached_;
};

bool isNpd(QIODevice &device) {
    device.seek(0);
    return device.peek(4) == QByteArray("NPD\0", 4);
}

// ---------------------------------------------------------------- sources
// "POLPACK0100.PAK" -> pack language from the file name.
const QRegularExpression packName("^([A-Za-z]{3})PACK(\\d{4})\\.(PAK|EDAT|SDAT)$",
                                  QRegularExpression::CaseInsensitiveOption);

struct Source {
    QString title;
    QVector<BuzzPack> packs;
};

BuzzPack makePack(const QString &label, const QString &fileName, const QString &identity,
                  std::function<std::unique_ptr<QIODevice>()> device, const QStringList &licenseFolders,
                  bool discImage) {
    BuzzPack pack;
    pack.label = label;
    pack.identity = identity;
    const auto match = packName.match(fileName);
    if (match.hasMatch())
        pack.language = languageCode(match.captured(1));
    pack.open = [device, licenseFolders, discImage]() {
        auto raw = device();
        if (isNpd(*raw))
            raw = openNpd(std::move(raw), licenseFolders);
        else if (discImage) {
            // A PS3 disc image dumped without decrypting keeps encrypted files:
            // there is no ZIP directory at the end.
            raw->seek(std::max<qint64>(0, raw->size() - 65557));
            if (!raw->readAll().contains(QByteArray("PK\5\6", 4)))
                fail(tr("Pliki w tym obrazie PS3 są zaszyfrowane. Użyj odszyfrowanego obrazu albo rozpakowanej gry."));
        }
        return std::make_unique<Archive>(std::move(raw));
    };
    return pack;
}

QString fileIdentity(const QString &path) {
    const QFileInfo info(path);
    return info.absoluteFilePath() + QString::number(info.size()) + info.lastModified().toString(Qt::ISODateWithMs);
}

Source fromPackage(const QString &path) {
    auto package = std::make_shared<Package>(path);
    Source source;
    if (package->items.contains("PARAM.SFO")) {
        const auto &sfo = package->items["PARAM.SFO"];
        if (sfo.size <= 64 * 1024)
            source.title = paramSfo(package->read(sfo.offset, sfo.size)).value("TITLE");
    }
    const QStringList licenses{QFileInfo(path).absolutePath()};
    static const QRegularExpression inPack("^USRDIR/PACK\\d{4}/([^/]+)$", QRegularExpression::CaseInsensitiveOption);
    for (const auto &item : std::as_const(package->items)) {
        const auto match = inPack.match(item.name);
        if (!match.hasMatch() || !packName.match(match.captured(1)).hasMatch())
            continue;
        source.packs << makePack(
            QFileInfo(path).absoluteFilePath() + ":" + item.name, match.captured(1),
            fileIdentity(path) + item.name, [package, item] { return std::make_unique<PackageEntry>(package, item); },
            licenses, false);
    }
    if (source.packs.isEmpty())
        fail(tr("Pakiet %1 nie zawiera paczki pytań — to aktualizacja gry lub dodatek bez pytań.")
                 .arg(source.title.isEmpty() ? package->contentId : source.title));
    return source;
}

Source fromDisc(const QString &path) {
    DiscImage disc(path);
    Source source;
    if (disc.contains("PS3_GAME/PARAM.SFO"))
        source.title = paramSfo(disc.read("PS3_GAME/PARAM.SFO", 64 * 1024)).value("TITLE");
    auto file = std::make_shared<QFile>(path);
    static const QRegularExpression inPack("^PS3_GAME/USRDIR/PACK\\d{4}/([^/]+)$");
    for (auto it = disc.files().cbegin(); it != disc.files().cend(); ++it) {
        const auto match = inPack.match(it.key());
        if (!match.hasMatch() || !packName.match(match.captured(1)).hasMatch())
            continue;
        const auto entry = it.value();
        source.packs << makePack(
            QFileInfo(path).absoluteFilePath() + ":" + it.key(), match.captured(1), fileIdentity(path) + it.key(),
            [path, entry] {
                return std::make_unique<FileRange>(openFile(path), entry.offset, entry.size);
            },
            {QFileInfo(path).absolutePath()}, true);
    }
    if (source.packs.isEmpty())
        fail(tr("Obraz PS3 nie zawiera paczek pytań Buzz! (PS3_GAME/USRDIR/PACKnnnn)."));
    return source;
}

// An extracted game, an installed DLC folder (dev_hdd0/game/<title>) or a single
// EDAT/SDAT file.
Source fromFolder(const QString &path) {
    Source source;
    const QFileInfo info(path);
    QStringList files;
    if (info.fileName().compare("PARAM.SFO", Qt::CaseInsensitive) == 0) {
        QFile sfo(path);
        if (sfo.open(QIODevice::ReadOnly))
            source.title = paramSfo(sfo.read(64 * 1024)).value("TITLE");
        QDirIterator packs(info.absoluteDir().filePath("USRDIR"), QDir::Dirs | QDir::NoDotAndDotDot);
        while (packs.hasNext()) {
            const QDir dir(packs.next());
            if (!dir.dirName().startsWith("PACK", Qt::CaseInsensitive))
                continue;
            for (const auto &file : dir.entryInfoList(QDir::Files, QDir::Name))
                if (packName.match(file.fileName()).hasMatch())
                    files << file.absoluteFilePath();
        }
        if (files.isEmpty())
            fail(tr("W tym folderze gry nie ma paczek pytań (USRDIR/PACKnnnn)."));
    } else {
        files << info.absoluteFilePath();
        source.title = info.completeBaseName();
    }
    for (const auto &file : files)
        source.packs << makePack(
            file, QFileInfo(file).fileName(), fileIdentity(file),
            [file] {
                const auto handle = openFile(file);
                return std::make_unique<FileRange>(handle, 0, handle->size());
            },
            {QFileInfo(file).absolutePath()}, false);
    return source;
}

QByteArray head(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(4) : QByteArray();
}

Source openSource(const QString &path) {
    const auto magic = head(path);
    if (magic == "\x7f" "PKG")
        return fromPackage(path);
    if (DiscImage::isDiscImage(path))
        return fromDisc(path);
    return fromFolder(path);
}

} // namespace

RangeDevice::RangeDevice(qint64 size) : size_(size) {}

qint64 RangeDevice::readData(char *data, qint64 maxSize) {
    const auto at = pos();
    const auto length = std::min(maxSize, size_ - at);
    if (length <= 0)
        return 0;
    try {
        const auto bytes = readRange(at, length);
        std::memcpy(data, bytes.constData(), size_t(bytes.size()));
        return bytes.size();
    } catch (const std::exception &e) {
        // Exceptions must not cross Qt's frames; the reader reports a short read.
        setErrorString(QString::fromUtf8(e.what()));
        return -1;
    }
}

std::unique_ptr<QIODevice> openNpd(std::unique_ptr<QIODevice> encrypted, const QStringList &licenseFolders) {
    const auto npd = readNpd(*encrypted);
    const bool paid = !(npd.flags & sdatFlag) && (npd.license & 3) != 3;
    auto device = std::make_unique<NpdDevice>(std::move(encrypted), npd);
    if (!paid) {
        if (!device->tryKey(embeddedKey(npd)))
            fail(tr("Nie udało się odszyfrować pliku EDAT."));
        return device;
    }

    bool checkedLicense = false;
    for (const auto &path : findLicenses(npd.contentId, licenseFolders)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const auto rap = file.read(16);
        if (rap.size() != 16)
            continue;
        checkedLicense = true;
        if (device->tryKey(rapToKey(rap)))
            return device;
    }
    if (checkedLicense)
        throw LicenseRequired(tr("Plik licencji nie pasuje do paczki %1. Wskaż właściwy plik %1.rap.")
                                  .arg(npd.contentId),
                              npd.contentId);
    throw LicenseRequired(tr("Ta paczka DLC wymaga pliku licencji %1.rap (z Twojej konsoli lub emulatora).")
                              .arg(npd.contentId),
                          npd.contentId);
}

QByteArray rapToKey(const QByteArray &rap) {
    if (rap.size() != 16)
        fail(tr("Plik licencji .rap musi mieć 16 bajtów."));
    auto key = aesEcb(rap, rapKey, false);
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 16; ++i) {
            const int p = rapPbox[i];
            key[p] = char(key[p] ^ rapE1[p]);
        }
        for (int i = 15; i >= 1; --i) {
            const int p = rapPbox[i], pp = rapPbox[i - 1];
            key[p] = char(key[p] ^ key[pp]);
        }
        int carry = 0;
        for (int i = 0; i < 16; ++i) {
            const int p = rapPbox[i];
            const int value = (quint8(key[p]) - carry) & 0xff;
            const int subtract = quint8(rapE2[p]);
            if (carry != 1 || value != 0xff) {
                carry = value < subtract ? 1 : 0;
                key[p] = char(value - subtract);
            } else {
                key[p] = char(value - carry);
            }
        }
    }
    return key;
}

QByteArray aesCmac(const QByteArray &key, const QByteArray &message) { return cmac(key, message); }

bool rememberLicense(const QString &contentId, const QString &rapPath, QString *error) {
    const QFileInfo info(rapPath);
    if (!info.isFile() || info.size() != 16) {
        if (error)
            *error = tr("Plik licencji .rap musi mieć 16 bajtów.");
        return false;
    }
    QSettings().setValue("ps3Licenses/" + contentId.toUpper(), info.absoluteFilePath());
    return true;
}

QString readTitleId(const QString &sfoPath) {
    constexpr qint64 limit = 64 * 1024;
    QFile file(sfoPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit)
        return {};
    return paramSfo(file.read(limit)).value("TITLE_ID");
}

bool isPs3Source(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile())
        return false;
    const auto magic = head(path);
    if (magic == "\x7f" "PKG" || magic == QByteArray("NPD\0", 4))
        return true;
    if (info.fileName().compare("PARAM.SFO", Qt::CaseInsensitive) == 0)
        return true;
    if (info.suffix().compare("iso", Qt::CaseInsensitive) == 0) {
        try {
            return DiscImage(path).contains("PS3_GAME/PARAM.SFO");
        } catch (const std::exception &) {
            return false;
        }
    }
    return false;
}

SourceLanguages ps3Languages(const QString &path) {
    SourceLanguages result;
    try {
        const auto source = openSource(path);
        QSet<QString> codes;
        for (const auto &pack : source.packs) {
            if (!pack.language.isEmpty()) {
                codes.insert(pack.language);
                continue;
            }
            const auto archive = pack.open();
            for (const auto &code : buzzArchiveLanguages(*archive))
                codes.insert(code);
        }
        // Ask for a missing license now, before the language is chosen.
        source.packs.first().open();
        result.codes = codes.values();
        result.codes.sort();
        result.preferred = preferredLanguage(result.codes);
        if (result.codes.isEmpty())
            fail(QCoreApplication::translate("BuzzImport", "Brak baz pytań QAK w paczce."));
    } catch (const LicenseRequired &e) {
        result.error = QString::fromUtf8(e.what());
        result.license = e.contentId;
    } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
    }
    return result;
}

LoadResult importPs3(const QString &path, const QString &language, bool extractSpeech) {
    try {
        auto source = openSource(path);
        // The Buzz importer reports errors as text; keep the content ID of a
        // missing license so the editor can ask for the .rap.
        auto license = std::make_shared<QString>();
        for (auto &pack : source.packs) {
            pack.sourcePath = QFileInfo(path).absoluteFilePath();
            pack.open = [open = pack.open, license] {
                try {
                    return open();
                } catch (const LicenseRequired &e) {
                    *license = e.contentId;
                    throw;
                }
            };
        }
        auto result = importBuzzPacks(source.packs, language, extractSpeech,
                                      source.title.isEmpty() ? QFileInfo(path).completeBaseName() : source.title);
        result.license = *license;
        int used = 0;
        for (const auto &pack : source.packs)
            used += pack.language.isEmpty() || pack.language == result.document.language;
        if (result.ok() && used > 1)
            result.warnings.prepend(
                tr("Zaimportowano pytania ze wszystkich paczek w wybranym języku (%1 plików).").arg(used));
        return result;
    } catch (const LicenseRequired &e) {
        LoadResult out;
        out.error = QString::fromUtf8(e.what());
        out.license = e.contentId;
        return out;
    } catch (const std::exception &e) {
        LoadResult out;
        out.error = QString::fromUtf8(e.what());
        return out;
    }
}

QByteArray readMovie(const QString &sourcePath, const QString &packLabel, const QString &entry) {
    std::unique_ptr<pack::Archive> archive;
    if (sourcePath.isEmpty()) {
        archive = buzzFilePack(packLabel).open();
    } else {
        const auto source = openSource(sourcePath);
        for (const auto &pack : source.packs)
            if (pack.label == packLabel) {
                archive = pack.open();
                break;
            }
    }
    if (!archive)
        fail(QStringLiteral("The original PS3 question archive was not found."));
    const auto item = archive->entries.constFind(entry);
    if (item == archive->entries.constEnd() || item->size > 128 * 1024 * 1024)
        fail(QStringLiteral("The referenced SDI clip is missing or too large."));
    return archive->read(entry);
}
} // namespace studio::ps3
