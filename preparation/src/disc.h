#pragma once
#include "common.h"
#include <QFile>
#include <QVector>

namespace prepare {
struct DiscExtent {
    quint64 offset = 0, size = 0;
};
struct DiscFile {
    QString path;
    QVector<DiscExtent> extents;
    quint64 size = 0;
};
struct DiscPlan {
    QString format, title;
    QVector<DiscFile> files;
    quint64 bytes = 0;
};
class DiscImage {
  public:
    explicit DiscImage(const QString &path);
    DiscPlan inspect(const Cancel &cancel = {});
    QByteArray read(const DiscFile &entry, quint64 limit = MaxBinary, const Cancel &cancel = {});
    void verifyExtraction(const DiscPlan &plan, const QString &directory, const Cancel &cancel = {});
    void extract(const DiscPlan &plan, const QString &destination, const Log &log = {},
                 const Cancel &cancel = {});

  private:
    QFile file_;
    qint64 originalSize_ = 0;
    QByteArray at(quint64 offset, quint64 size);
    void verifyUnchanged() const;
};
} // namespace prepare
