#pragma once
#include "elf.h"

namespace prepare {
struct SpuImage {
    quint64 offset, size;
    quint32 entry;
    QVector<Segment> loads;
};
SpuImage validateSpuImage(const QByteArray &bytes, quint64 offset = 0);
QVector<SpuImage> findSpuImages(const QByteArray &bytes, const Cancel &cancel = {});
quint64 spuFingerprint(const QByteArray &bytes);
QJsonArray extractSpuImages(const QByteArray &bytes, const QString &directory, const Cancel &cancel = {});
} // namespace prepare
