#pragma once
#include "common.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

namespace prepare {
struct Segment {
    quint32 type, flags;
    quint64 offset, address, fileSize, memorySize;
};
struct ElfSection {
    quint64 address, offset, size;
    quint32 type;
};
class ElfImage {
  public:
    explicit ElfImage(QByteArray bytes);
    bool is64() const { return is64_; }
    quint16 machine() const { return machine_; }
    quint16 type() const { return type_; }
    quint64 entry() const { return entry_; }
    const QVector<Segment> &segments() const { return segments_; }
    const QVector<ElfSection> &sections() const { return sections_; }
    QByteArray virtualBytes(quint64 address, quint64 length) const;
    QByteArray fileBytes(quint64 offset, quint64 length) const { return part(bytes_, offset, length); }

  private:
    QByteArray bytes_;
    bool is64_;
    quint16 machine_, type_;
    quint64 entry_;
    QVector<Segment> segments_;
    QVector<ElfSection> sections_;
};
struct PpuAnalysis {
    QJsonArray functions, image, imports;
    QJsonObject loader;
};
PpuAnalysis analyzePpu(const ElfImage &elf, const Cancel &cancel = {});
void writePpuAnalysis(const PpuAnalysis &analysis, const QString &directory, const QString &name,
                      const Cancel &cancel = {});
} // namespace prepare
