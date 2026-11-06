#pragma once
#include "certified.h"

namespace prepare {
struct TarEntry {
    QString path;
    QByteArray data;
    bool directory = false;
};
QVector<TarEntry> readTar(const QByteArray &bytes, const QString &prefix = {}, const Cancel &cancel = {});
QByteArray firmwareArchive(const QString &path, const KeyStore &keys, const Cancel &cancel = {});
void extractFirmware(const QString &source, const QString &home, const KeyStore &keys, const Log &log = {},
                     const Cancel &cancel = {},
                     const std::function<void(const QString &)> &validator = validateFirmware);
void extractUpdate(const QString &source, const QString &destination, const QString &title,
                   const Log &log = {}, const Cancel &cancel = {});
} // namespace prepare
