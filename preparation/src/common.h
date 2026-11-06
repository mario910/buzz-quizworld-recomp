#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include <QtEndian>
#include <functional>
#include <stdexcept>

namespace prepare {
constexpr quint64 MaxBinary = 256ull * 1024 * 1024;
constexpr quint64 MaxFirmware = 2ull * 1024 * 1024 * 1024;
constexpr int MaxFiles = 20000;
using Cancel = std::function<void()>;
using Log = std::function<void(const QString &)>;
inline void cancelled(const Cancel &cancel) {
    if (cancel)
        cancel();
}
[[noreturn]] inline void fail(const QString &message) {
    throw std::runtime_error(message.toUtf8().constData());
}
inline void require(bool condition, const QString &message) {
    if (!condition)
        fail(message);
}
void range(const QByteArray &data, quint64 offset, quint64 size);
QByteArray part(const QByteArray &data, quint64 offset, quint64 size);
inline quint16 be16(const QByteArray &data, quint64 at) {
    range(data, at, 2);
    return qFromBigEndian<quint16>(data.constData() + at);
}
inline quint32 be32(const QByteArray &data, quint64 at) {
    range(data, at, 4);
    return qFromBigEndian<quint32>(data.constData() + at);
}
inline quint64 be64(const QByteArray &data, quint64 at) {
    range(data, at, 8);
    return qFromBigEndian<quint64>(data.constData() + at);
}
QByteArray readFile(const QString &path, quint64 limit = MaxBinary);
QString fileHash(const QString &path, const Cancel &cancel = {});
void rejectLinks(const QString &root, const QString &target);
QString archivePath(const QString &name);
void atomicWrite(const QString &root, const QString &path, const QByteArray &bytes,
                 const Cancel &cancel = {});
QString titleId(const QByteArray &sfo);
QString identify(const QString &title, const QString &digest);
QByteArray elfText(const QByteArray &elf);
void validateFirmware(const QString &root);
} // namespace prepare
