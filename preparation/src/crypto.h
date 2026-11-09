#pragma once
#include "common.h"
#include <QMap>
#include <QVector>

namespace prepare {
QByteArray aesBlock(const QByteArray &data, const QByteArray &key, const QByteArray &iv, bool cbc,
                    bool encrypt = false);
QByteArray aesCtr(const QByteArray &data, const QByteArray &key, const QByteArray &iv, quint64 startBlock = 0,
                  const Cancel &cancel = {});
QByteArray hmac(const QByteArray &key, const QByteArray &data, QCryptographicHash::Algorithm algorithm);
bool equalDigest(const QByteArray &a, const QByteArray &b);
QByteArray inflateBounded(const QByteArray &data, quint64 limit, const Cancel &cancel = {});
struct RootKey {
    int category = 0, kind = 0, revision = 0;
    QByteArray key, iv;
};
class KeyStore {
  public:
    QVector<RootKey> roots;
    QMap<QString, QByteArray> named;
    static KeyStore fromFile(const QString &path);
    static KeyStore parse(const QByteArray &data);
    QByteArray namedKey(const QString &name, int size) const;
    QVector<RootKey> candidates(int category, int kind, int revision) const;
};
QByteArray systemKeyFile(const QByteArray &database, const QByteArray &pupHmac);
} // namespace prepare
