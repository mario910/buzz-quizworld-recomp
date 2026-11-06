#pragma once
#include "crypto.h"
#include <array>

namespace prepare {
struct Section {
    quint64 offset, size;
    quint32 type, index, signing, digest, encryption, key, iv, compression;
};
class CertifiedFile {
  public:
    CertifiedFile(QByteArray data, const KeyStore &keys, Cancel cancel = {});
    QByteArray elf() const;
    QByteArray sectionData(const Section &section, quint64 limit = MaxBinary) const;
    int category() const { return category_; }
    const QVector<Section> &sections() const { return sections_; }

  private:
    QByteArray data_, attributes_;
    Cancel cancel_;
    int category_ = 0, revision_ = 0, kind_ = 0;
    quint64 headerSize_ = 0, appOffset_ = 0, elfOffset_ = 0, phdrOffset_ = 0, shdrOffset_ = 0,
            segmentOffset_ = 0, controlOffset_ = 0, controlSize_ = 0;
    QVector<Section> sections_;
    void checkFreeNpdrm() const;
    void readMetadata(const QByteArray &metadata);
    QByteArray attribute(quint32 index, quint64 size) const;
};
QByteArray decryptElf(const QString &path, const KeyStore &keys, const Cancel &cancel = {});
} // namespace prepare
