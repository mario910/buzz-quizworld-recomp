#pragma once
#include <QByteArray>
#include <QFile>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

// Read-only access to fake-signed (fPKG) PS4 packages a user dumped from their
// own game: passcode-derived EKPFS, XTS-AES outer PFS, PFSC-compressed inner
// PFS. A port of the documented LibOrbisPkg algorithms (maxton/LibOrbisPkg,
// MIT) as used by tools/questions/ps4pkg.py. Retail packages are not supported.
namespace studio::ps4 {
// AES-128 ECB through the platform (CNG on Windows, OpenSSL libcrypto elsewhere).
QByteArray aesEcb(const QByteArray &data, const QByteArray &key, bool encrypt);
// AES-128 CBC without padding; an empty IV selects ECB.
QByteArray aesCbc(const QByteArray &data, const QByteArray &key, const QByteArray &iv, bool encrypt);
// One XTS-AES-128 data unit (sector); the sector number is the tweak.
QByteArray xtsDecrypt(const QByteArray &sector, const QByteArray &dataKey, const QByteArray &tweakKey,
                      quint64 sectorNumber);

class Reader {
  public:
    virtual ~Reader() = default;
    virtual qint64 size() const = 0;
    virtual QByteArray read(qint64 offset, qint64 length) = 0;
};
class BytesReader final : public Reader {
  public:
    explicit BytesReader(QByteArray data) : data_(std::move(data)) {}
    qint64 size() const override { return data_.size(); }
    QByteArray read(qint64 offset, qint64 length) override { return data_.mid(offset, length); }

  private:
    QByteArray data_;
};

struct Inode {
    quint32 flags = 0, blocks = 0;
    qint64 size = 0, sizeCompressed = 0;
    QVector<qint32> direct, indirect;
};

class Pfs {
  public:
    // An encrypted PFS (mode bit 4) needs the package's EKPFS.
    explicit Pfs(std::shared_ptr<Reader> reader, const QByteArray &ekpfs = {}, bool newCrypt = false);
    // Paths of regular files, '/' separated, e.g. "uroot/Media/resources.assets".
    const QMap<QString, int> &files() const { return files_; }
    qint64 fileSize(const QString &path) const;
    std::shared_ptr<Reader> open(const QString &path);

  private:
    std::shared_ptr<Reader> reader_;
    quint32 blockSize_ = 0;
    bool signed_ = false;
    QVector<Inode> inodes_;
    QMap<QString, int> files_;
    QVector<qint64> blockList(const Inode &inode);
    void walk(int inode, const QString &path, int depth);
};

class Package {
  public:
    explicit Package(const QString &path, const QByteArray &passcode = QByteArray(32, '0'));
    QString contentId;
    // Files of the game image without the "uroot/" prefix.
    QStringList files() const;
    bool contains(const QString &path) const;
    qint64 fileSize(const QString &path) const;
    QByteArray read(const QString &path, qint64 limit);

  private:
    std::unique_ptr<Pfs> outer_, inner_;
};
} // namespace studio::ps4
