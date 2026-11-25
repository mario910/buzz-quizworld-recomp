#include "ps4_pkg.h"
#include "pack_format.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QtEndian>
#include <cstring>
#include <zlib.h>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>

#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace studio::ps4 {
namespace {
using pack::check;
using pack::fail;
constexpr qint64 xtsSector = 0x1000;
QString tr(const char *text) { return QCoreApplication::translate("Ps4Package", text); }
qint64 i64(const QByteArray &b, qint64 off) {
    check(b, off, 8);
    return qFromLittleEndian<qint64>(b.constData() + off);
}
qint32 i32(const QByteArray &b, qint64 off) {
    check(b, off, 4);
    return qFromLittleEndian<qint32>(b.constData() + off);
}
quint64 be64(const QByteArray &b, qint64 off) {
    check(b, off, 8);
    return qFromBigEndian<quint64>(b.constData() + off);
}
QByteArray sha256(const QByteArray &data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256); }
QByteArray hmac(const QByteArray &key, const QByteArray &data) {
    return QMessageAuthenticationCode::hash(data, key, QCryptographicHash::Sha256);
}

class FileReader final : public Reader {
  public:
    FileReader(const QString &path, qint64 base, qint64 size) : file_(path), base_(base), size_(size) {
        if (!file_.open(QIODevice::ReadOnly))
            fail(file_.errorString());
        if (base < 0 || size < 0 || base + size > file_.size())
            fail(tr("Pakiet PS4 jest ucięty."));
    }
    qint64 size() const override { return size_; }
    QByteArray read(qint64 offset, qint64 length) override {
        if (offset < 0 || length < 0 || offset + length > size_ || !file_.seek(base_ + offset))
            fail(tr("Odczyt poza pakietem PS4."));
        const auto out = file_.read(length);
        if (out.size() != length)
            fail(tr("Pakiet PS4 jest ucięty."));
        return out;
    }

  private:
    QFile file_;
    qint64 base_, size_;
};

// Decrypts sectors >= start; the PFS header region before it is plain.
class XtsReader final : public Reader {
  public:
    XtsReader(std::shared_ptr<Reader> inner, QByteArray tweakKey, QByteArray dataKey, qint64 startSector)
        : inner_(std::move(inner)), tweak_(std::move(tweakKey)), data_(std::move(dataKey)), start_(startSector) {}
    qint64 size() const override { return inner_->size(); }
    QByteArray read(qint64 offset, qint64 length) override {
        QByteArray out;
        out.reserve(length);
        while (length > 0) {
            const qint64 n = offset / xtsSector, at = offset % xtsSector;
            if (n != cached_) {
                cache_ = inner_->read(n * xtsSector, xtsSector);
                if (n >= start_)
                    cache_ = xtsDecrypt(cache_, data_, tweak_, quint64(n));
                cached_ = n;
            }
            const qint64 take = std::min(length, xtsSector - at);
            out.append(cache_.constData() + at, take);
            offset += take;
            length -= take;
        }
        return out;
    }

  private:
    std::shared_ptr<Reader> inner_;
    QByteArray tweak_, data_, cache_;
    qint64 start_, cached_ = -1;
};

// A PFS file: its block list over the image.
class SpanReader final : public Reader {
  public:
    SpanReader(std::shared_ptr<Reader> inner, QVector<qint64> blocks, qint64 blockSize, qint64 size)
        : inner_(std::move(inner)), blocks_(std::move(blocks)), blockSize_(blockSize), size_(size) {}
    qint64 size() const override { return size_; }
    QByteArray read(qint64 offset, qint64 length) override {
        length = std::min(length, size_ - offset);
        QByteArray out;
        out.reserve(std::max<qint64>(0, length));
        while (length > 0) {
            const qint64 b = offset / blockSize_, at = offset % blockSize_;
            if (b >= blocks_.size())
                fail(tr("Niepoprawna lista bloków PFS."));
            const qint64 take = std::min(length, blockSize_ - at);
            out += inner_->read(blocks_[b] * blockSize_ + at, take);
            offset += take;
            length -= take;
        }
        return out;
    }

  private:
    std::shared_ptr<Reader> inner_;
    QVector<qint64> blocks_;
    qint64 blockSize_, size_;
};

// PFSC: zlib-compressed blocks with an offset table.
class PfscReader final : public Reader {
  public:
    explicit PfscReader(std::shared_ptr<Reader> inner) : inner_(std::move(inner)) {
        const auto h = inner_->read(0, 0x30);
        if (h.size() < 0x30 || i32(h, 0) != 0x43534650 || i32(h, 4) != 0 || qint64(i32(h, 12)) != i64(h, 16))
            fail(tr("Niepoprawny obraz PFSC."));
        blockSize_ = i32(h, 12);
        size_ = i64(h, 40);
        if (blockSize_ <= 0 || size_ < 0)
            fail(tr("Niepoprawny obraz PFSC."));
        const qint64 count = size_ / blockSize_ + 1;
        const auto table = inner_->read(i64(h, 24), 8 * count);
        for (qint64 i = 0; i < count; ++i)
            map_ << i64(table, 8 * i);
    }
    qint64 size() const override { return size_; }
    QByteArray read(qint64 offset, qint64 length) override {
        length = std::min(length, size_ - offset);
        QByteArray out;
        out.reserve(std::max<qint64>(0, length));
        while (length > 0) {
            const qint64 i = offset / blockSize_, at = offset % blockSize_;
            const auto data = block(i);
            const qint64 take = std::min(length, blockSize_ - at);
            out.append(data.constData() + at, take);
            offset += take;
            length -= take;
        }
        return out;
    }

  private:
    std::shared_ptr<Reader> inner_;
    qint64 blockSize_ = 0, size_ = 0, cached_ = -1;
    QVector<qint64> map_;
    QByteArray cache_;
    QByteArray block(qint64 i) {
        if (i == cached_)
            return cache_;
        if (i + 1 >= map_.size())
            fail(tr("Niepoprawny obraz PFSC."));
        const qint64 a = map_[i], b = map_[i + 1];
        if (b - a == blockSize_) {
            cache_ = inner_->read(a, blockSize_);
        } else if (b - a > blockSize_) {
            cache_ = QByteArray(blockSize_, '\0');
        } else {
            const auto packed = inner_->read(a + 2, b - a - 2); // Skip the zlib header.
            cache_ = QByteArray(blockSize_, '\0');
            z_stream z{};
            if (inflateInit2(&z, -15) != Z_OK)
                fail(tr("Niepoprawny obraz PFSC."));
            z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(packed.constData()));
            z.avail_in = uInt(packed.size());
            z.next_out = reinterpret_cast<Bytef *>(cache_.data());
            z.avail_out = uInt(cache_.size());
            const int status = inflate(&z, Z_FINISH);
            inflateEnd(&z);
            if (status != Z_STREAM_END && status != Z_BUF_ERROR)
                fail(tr("Uszkodzony blok PFSC."));
        }
        cached_ = i;
        return cache_;
    }
};
} // namespace

QByteArray aesEcb(const QByteArray &data, const QByteArray &key, bool encrypt) {
    return aesCbc(data, key, {}, encrypt);
}

QByteArray aesCbc(const QByteArray &data, const QByteArray &key, const QByteArray &iv, bool encrypt) {
    // An empty IV selects ECB.
    if (key.size() != 16 || data.size() % 16 || (!iv.isEmpty() && iv.size() != 16))
        fail(tr("Niepoprawne parametry AES."));
    if (data.isEmpty())
        return {};
    const bool cbc = !iv.isEmpty();
#ifdef Q_OS_WIN
    struct Cipher {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_KEY_HANDLE key = nullptr;
        ~Cipher() {
            if (key)
                BCryptDestroyKey(key);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } c;
    const wchar_t *mode = cbc ? BCRYPT_CHAIN_MODE_CBC : BCRYPT_CHAIN_MODE_ECB;
    if (BCryptOpenAlgorithmProvider(&c.algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0 ||
        BCryptSetProperty(c.algorithm, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(mode)),
                          ULONG((wcslen(mode) + 1) * sizeof(wchar_t)), 0) < 0 ||
        BCryptGenerateSymmetricKey(c.algorithm, &c.key, nullptr, 0,
                                   reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())), ULONG(key.size()),
                                   0) < 0)
        fail(tr("Systemowe AES jest niedostępne."));
    QByteArray out(data.size(), Qt::Uninitialized);
    QByteArray chain = iv; // CNG updates the IV buffer in place.
    ULONG written = 0;
    const auto action = encrypt ? BCryptEncrypt : BCryptDecrypt;
    if (action(c.key, reinterpret_cast<PUCHAR>(const_cast<char *>(data.constData())), ULONG(data.size()), nullptr,
               cbc ? reinterpret_cast<PUCHAR>(chain.data()) : nullptr, cbc ? ULONG(chain.size()) : 0,
               reinterpret_cast<PUCHAR>(out.data()), ULONG(out.size()), &written, 0) < 0 ||
        written != ULONG(data.size()))
        fail(tr("Systemowe AES jest niedostępne."));
    return out;
#else
    struct Cipher {
        EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
        ~Cipher() { EVP_CIPHER_CTX_free(context); }
    } c;
    QByteArray out(data.size() + 16, Qt::Uninitialized);
    int length = 0, tail = 0;
    if (!c.context ||
        EVP_CipherInit_ex(c.context, cbc ? EVP_aes_128_cbc() : EVP_aes_128_ecb(), nullptr,
                          reinterpret_cast<const unsigned char *>(key.constData()),
                          cbc ? reinterpret_cast<const unsigned char *>(iv.constData()) : nullptr,
                          encrypt ? 1 : 0) != 1 ||
        EVP_CIPHER_CTX_set_padding(c.context, 0) != 1 ||
        EVP_CipherUpdate(c.context, reinterpret_cast<unsigned char *>(out.data()), &length,
                         reinterpret_cast<const unsigned char *>(data.constData()), int(data.size())) != 1 ||
        EVP_CipherFinal_ex(c.context, reinterpret_cast<unsigned char *>(out.data()) + length, &tail) != 1)
        fail(tr("Systemowe AES jest niedostępne."));
    out.resize(length + tail);
    return out;
#endif
}

QByteArray xtsDecrypt(const QByteArray &sector, const QByteArray &dataKey, const QByteArray &tweakKey,
                      quint64 sectorNumber) {
    if (sector.size() % 16)
        fail(tr("Niepoprawny sektor XTS."));
    QByteArray tweak(16, '\0');
    qToLittleEndian<quint64>(sectorNumber, tweak.data());
    auto t = aesEcb(tweak, tweakKey, true);
    // The tweak for every 16-byte block (multiplied by x in GF(2^128)).
    QByteArray mask(sector.size(), Qt::Uninitialized);
    for (qsizetype block = 0; block < sector.size(); block += 16) {
        std::memcpy(mask.data() + block, t.constData(), 16);
        int carry = 0;
        for (int i = 0; i < 16; ++i) {
            const int next = (quint8(t[i]) >> 7) & 1;
            t[i] = char((quint8(t[i]) << 1) | carry);
            carry = next;
        }
        if (carry)
            t[0] = char(quint8(t[0]) ^ 0x87);
    }
    QByteArray work = sector;
    for (qsizetype i = 0; i < work.size(); ++i)
        work[i] = char(work[i] ^ mask[i]);
    work = aesEcb(work, dataKey, false);
    for (qsizetype i = 0; i < work.size(); ++i)
        work[i] = char(work[i] ^ mask[i]);
    return work;
}

Pfs::Pfs(std::shared_ptr<Reader> reader, const QByteArray &ekpfs, bool newCrypt) : reader_(std::move(reader)) {
    const auto h = reader_->read(0, 0x400);
    if (h.size() < 0x400 || i64(h, 0) != 1 || i64(h, 8) != 20130315)
        fail(tr("Niepoprawny system plików PFS."));
    const int mode = pack::u16(h, 0x1c);
    blockSize_ = quint32(i32(h, 0x20));
    const qint64 inodeCount = i64(h, 0x30), inodeBlocks = i64(h, 0x40);
    if (blockSize_ < 0x1000 || blockSize_ > 0x100000 || inodeCount <= 0 || inodeCount > 10000000 || inodeBlocks <= 0 ||
        inodeBlocks > 100000)
        fail(tr("Niepoprawny system plików PFS."));
    signed_ = mode & 1;
    if (mode & 4) {
        if (ekpfs.isEmpty())
            fail(tr("Zaszyfrowany PFS wymaga klucza pakietu."));
        const auto seed = h.mid(0x370, 16);
        const auto key = newCrypt ? hmac(ekpfs, seed) : ekpfs;
        QByteArray index(4, '\0');
        qToLittleEndian<quint32>(1, index.data());
        const auto keys = hmac(key, index + seed);
        reader_ = std::make_shared<XtsReader>(reader_, keys.left(16), keys.mid(16), blockSize_ / xtsSector);
    }
    const int inodeSize = signed_ ? 0x2c8 : 0xa8, perBlock = int(blockSize_) / inodeSize;
    for (qint64 b = 0; b < inodeBlocks && inodes_.size() < inodeCount; ++b) {
        const auto block = reader_->read(qint64(blockSize_) * (1 + b), blockSize_);
        for (int j = 0; j < perBlock && inodes_.size() < inodeCount; ++j) {
            const auto raw = block.mid(j * inodeSize, inodeSize);
            Inode inode;
            inode.flags = quint32(i32(raw, 4));
            inode.size = i64(raw, 8);
            inode.sizeCompressed = i64(raw, 16);
            inode.blocks = quint32(i32(raw, 0x60));
            for (int k = 0; k < 12; ++k)
                inode.direct << (signed_ ? i32(raw, 0x64 + 36 * k + 32) : i32(raw, 0x64 + 4 * k));
            for (int k = 0; k < 5; ++k)
                inode.indirect << (signed_ ? i32(raw, 0x64 + 36 * (12 + k) + 32) : i32(raw, 0x94 + 4 * k));
            inodes_ << inode;
        }
    }
    walk(0, {}, 0);
    if (!std::any_of(files_.keyBegin(), files_.keyEnd(), [](const QString &p) { return p.startsWith("uroot/"); }))
        fail(tr("Nie udało się odczytać pakietu PS4 (inny klucz lub nieobsługiwany pakiet)."));
}

QVector<qint64> Pfs::blockList(const Inode &inode) {
    const qint64 n = inode.blocks;
    QVector<qint64> blocks;
    if (n <= 1 || inode.direct.value(1) == -1 || !signed_) {
        for (qint64 i = 0; i < std::max<qint64>(n, 1); ++i)
            blocks << inode.direct[0] + i;
        return blocks;
    }
    for (int i = 0; i < std::min<qint64>(12, n); ++i)
        blocks << inode.direct[i];
    const qint64 per = blockSize_ / 36;
    auto signedBlock = [this](qint64 block, qint64 count) {
        const auto raw = reader_->read(block * blockSize_, count * 36);
        QVector<qint64> out;
        for (qint64 k = 0; k < count; ++k)
            out << i32(raw, k * 36 + 32);
        return out;
    };
    if (blocks.size() < n)
        blocks += signedBlock(inode.indirect[0], std::min(n - blocks.size(), per));
    if (blocks.size() < n)
        for (const auto indirect : signedBlock(inode.indirect[1], (n - blocks.size() + per - 1) / per)) {
            blocks += signedBlock(indirect, std::min(n - blocks.size(), per));
            if (blocks.size() >= n)
                break;
        }
    if (blocks.size() < n)
        fail(tr("Niepoprawna lista bloków PFS."));
    return blocks;
}

void Pfs::walk(int number, const QString &path, int depth) {
    if (depth > 32 || number < 0 || number >= inodes_.size())
        fail(tr("Niepoprawny katalog PFS."));
    const auto &inode = inodes_[number];
    for (qint64 b = 0; b < inode.blocks; ++b) {
        const auto block = reader_->read((inode.direct[0] + b) * blockSize_, blockSize_);
        qint64 pos = 0;
        while (pos + 16 <= block.size()) {
            const int child = i32(block, pos), type = i32(block, pos + 4), nameLength = i32(block, pos + 8),
                      entry = i32(block, pos + 12);
            if (entry <= 0)
                break;
            if (nameLength < 0 || nameLength > 255)
                fail(tr("Niepoprawny katalog PFS."));
            const auto name = QString::fromLatin1(block.mid(pos + 16, nameLength));
            const auto full = path.isEmpty() ? name : path + '/' + name;
            if (type == 2)
                files_.insert(full, child);
            else if (type == 3 && name != "." && name != "..")
                walk(child, full, depth + 1);
            pos += entry;
        }
    }
}

qint64 Pfs::fileSize(const QString &path) const {
    const auto it = files_.constFind(path);
    if (it == files_.constEnd())
        return -1;
    const auto &inode = inodes_[*it];
    return inode.flags & 1 ? inode.sizeCompressed : inode.size;
}

std::shared_ptr<Reader> Pfs::open(const QString &path) {
    const auto it = files_.constFind(path);
    if (it == files_.constEnd())
        fail(tr("Brak pliku %1 w pakiecie PS4.").arg(path));
    const auto &inode = inodes_[*it];
    std::shared_ptr<Reader> file = std::make_shared<SpanReader>(reader_, blockList(inode), blockSize_, inode.size);
    if (inode.flags & 1)
        file = std::make_shared<PfscReader>(file);
    return file;
}

Package::Package(const QString &path, const QByteArray &passcode) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(file.errorString());
    const auto h = file.read(0x1000);
    if (h.startsWith("\x7f" "PKG"))
        fail(tr("To pakiet PS3. Zainstaluj go w grze (lub emulatorze) i otwórz plik .edat/.pak z paczką pytań."));
    if (!h.startsWith("\x7f" "CNT"))
        fail(tr("To nie jest pakiet PS4 (PKG)."));
    contentId = QString::fromLatin1(h.mid(0x40, 0x24));
    const auto flags = be64(h, 0x408);
    const auto offset = qint64(be64(h, 0x410)), size = qint64(be64(h, 0x418));
    QByteArray index(4, '\0');
    qToBigEndian<quint32>(1, index.data());
    const auto ekpfs =
        sha256(sha256(index) + sha256(contentId.toLatin1().leftJustified(48, '\0', true)) + passcode);
    outer_ = std::make_unique<Pfs>(std::make_shared<FileReader>(path, offset, size), ekpfs,
                                   flags & 0x2000000000000000ull);
    inner_ = std::make_unique<Pfs>(outer_->open("uroot/pfs_image.dat"));
}

QStringList Package::files() const {
    QStringList out;
    for (auto it = inner_->files().cbegin(); it != inner_->files().cend(); ++it)
        if (it.key().startsWith("uroot/"))
            out << it.key().mid(6);
    return out;
}

bool Package::contains(const QString &path) const { return inner_->files().contains("uroot/" + path); }

qint64 Package::fileSize(const QString &path) const { return inner_->fileSize("uroot/" + path); }

QByteArray Package::read(const QString &path, qint64 limit) {
    auto file = inner_->open("uroot/" + path);
    if (file->size() > limit)
        fail(tr("Plik %1 w pakiecie PS4 jest za duży.").arg(path));
    return file->read(0, file->size());
}
} // namespace studio::ps4
