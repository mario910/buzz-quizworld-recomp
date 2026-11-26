#pragma once
#include "buzz_import.h"
#include "document.h"
#include "languages.h"
#include <QIODevice>
#include <QString>
#include <memory>
#include <stdexcept>

// PS3 sources of Buzz! question packs, read in place without extracting:
//  - PSN packages (.pkg, retail and debug), e.g. Quiz TV / Quiz World DLC,
//  - NPDRM EDAT/SDAT files (installed DLC, or inside a package),
//  - decrypted PS3 disc images (.iso) and extracted game or DLC folders
//    (opened through their PARAM.SFO).
// The formats follow the public PKG/EDAT descriptions (psdevwiki) and the
// runtime's ps3recomp/libs/filesystem/edat.c. Paid content is decrypted with
// the user's own .rap license; nothing is written to disk.
namespace studio::ps3 {
// A paid package or EDAT needs the .rap license of this content ID.
class LicenseRequired : public std::runtime_error {
  public:
    LicenseRequired(const QString &message, QString contentId)
        : std::runtime_error(message.toStdString()), contentId(std::move(contentId)) {}
    QString contentId;
};

bool isPs3Source(const QString &path);
SourceLanguages ps3Languages(const QString &path);
LoadResult importPs3(const QString &path, const QString &language = {}, bool extractSpeech = true);
QString readTitleId(const QString &sfoPath);

// Remembers the user's .rap for a content ID (checked when it is used).
bool rememberLicense(const QString &contentId, const QString &rapPath, QString *error);
// RAP license -> EDAT key (the published RAP conversion).
QByteArray rapToKey(const QByteArray &rap);
// AES-CMAC (RFC 4493), as used by NPD headers and blocks.
QByteArray aesCmac(const QByteArray &key, const QByteArray &message);

// Random-access read-only views used by the readers above.
class RangeDevice : public QIODevice {
  public:
    explicit RangeDevice(qint64 size);
    bool isSequential() const override { return false; }
    qint64 size() const override { return size_; }

  protected:
    virtual QByteArray readRange(qint64 offset, qint64 length) = 0;
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *, qint64) override { return -1; }

  private:
    qint64 size_;
};
// An NPD (EDAT/SDAT) file decrypted block by block; every block's hash is checked.
// Throws LicenseRequired when a paid file has no usable .rap.
std::unique_ptr<QIODevice> openNpd(std::unique_ptr<QIODevice> encrypted, const QStringList &licenseFolders);
} // namespace studio::ps3
