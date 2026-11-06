#include "crypto.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <algorithm>
#include <cmath>
#include <zlib.h>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>

#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace prepare {
QByteArray aesBlock(const QByteArray &data, const QByteArray &key, const QByteArray &iv, bool cbc,
                    bool encrypt) {
    require((key.size() == 16 || key.size() == 32) && data.size() % 16 == 0 && (!cbc || iv.size() == 16),
            "Invalid AES parameters.");
    if (data.isEmpty())
        return {};
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
    } context;
    require(BCryptOpenAlgorithmProvider(&context.algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0,
            "Native AES provider failed.");
    const auto mode = cbc ? BCRYPT_CHAIN_MODE_CBC : BCRYPT_CHAIN_MODE_ECB;
    require(BCryptSetProperty(context.algorithm, BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(mode)),
                              ULONG((wcslen(mode) + 1) * sizeof(wchar_t)), 0) >= 0,
            "Native AES mode failed.");
    require(BCryptGenerateSymmetricKey(context.algorithm, &context.key, nullptr, 0,
                                       reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())),
                                       ULONG(key.size()), 0) >= 0,
            "Native AES key failed.");
    auto vector = iv;
    QByteArray output(data.size(), Qt::Uninitialized);
    ULONG written = 0;
    const auto action = encrypt ? BCryptEncrypt : BCryptDecrypt;
    const auto status =
        action(context.key, reinterpret_cast<PUCHAR>(const_cast<char *>(data.constData())),
               ULONG(data.size()), nullptr, cbc ? reinterpret_cast<PUCHAR>(vector.data()) : nullptr,
               cbc ? 16 : 0, reinterpret_cast<PUCHAR>(output.data()), ULONG(output.size()), &written, 0);
    require(status >= 0 && written == ULONG(data.size()), "Native AES operation failed.");
    return output;
#else
    const EVP_CIPHER *cipher = key.size() == 16 ? (cbc ? EVP_aes_128_cbc() : EVP_aes_128_ecb())
                                                : (cbc ? EVP_aes_256_cbc() : EVP_aes_256_ecb());
    struct Cipher {
        EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
        ~Cipher() { EVP_CIPHER_CTX_free(context); }
    } context;
    require(context.context != nullptr, "Native AES provider failed.");
    require(EVP_CipherInit_ex(context.context, cipher, nullptr,
                              reinterpret_cast<const unsigned char *>(key.constData()),
                              cbc ? reinterpret_cast<const unsigned char *>(iv.constData()) : nullptr,
                              encrypt ? 1 : 0) == 1 &&
                EVP_CIPHER_CTX_set_padding(context.context, 0) == 1,
            "Native AES setup failed.");
    QByteArray output(data.size() + 16, Qt::Uninitialized);
    int length = 0, tail = 0;
    require(EVP_CipherUpdate(context.context, reinterpret_cast<unsigned char *>(output.data()), &length,
                             reinterpret_cast<const unsigned char *>(data.constData()),
                             int(data.size())) == 1 &&
                EVP_CipherFinal_ex(context.context, reinterpret_cast<unsigned char *>(output.data() + length),
                                   &tail) == 1,
            "Native AES operation failed.");
    output.resize(length + tail);
    require(output.size() == data.size(), "Native AES length mismatch.");
    return output;
#endif
}
QByteArray aesCtr(const QByteArray &data, const QByteArray &key, const QByteArray &iv, quint64 startBlock,
                  const Cancel &cancel) {
    require(iv.size() == 16, "Invalid AES counter length.");
    QByteArray output(data.size(), Qt::Uninitialized), counter = iv;
    auto add = [&](quint64 value) {
        for (int i = 15; i >= 0; --i) {
            const auto sum = quint64(quint8(counter[i])) + (value & 255);
            counter[i] = char(sum & 255);
            value = (value >> 8) + (sum >> 8);
        }
    };
    add(startBlock);
    for (qsizetype at = 0; at < data.size(); at += 1024 * 1024) {
        cancelled(cancel);
        const auto length = std::min<qsizetype>(1024 * 1024, data.size() - at);
        QByteArray counters;
        counters.reserve(((length + 15) / 16) * 16);
        for (qsizetype pos = 0; pos < length; pos += 16) {
            counters += counter;
            add(1);
        }
        const auto stream = aesBlock(counters, key, {}, false, true);
        for (qsizetype i = 0; i < length; ++i)
            output[at + i] = char(quint8(data[at + i]) ^ quint8(stream[i]));
    }
    cancelled(cancel);
    return output;
}
QByteArray hmac(const QByteArray &key, const QByteArray &data, QCryptographicHash::Algorithm algorithm) {
    return QMessageAuthenticationCode::hash(data, key, algorithm);
}
bool equalDigest(const QByteArray &a, const QByteArray &b) {
    if (a.size() != b.size())
        return false;
    quint8 different = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        different |= quint8(a[i]) ^ quint8(b[i]);
    return different == 0;
}
QByteArray inflateBounded(const QByteArray &data, quint64 limit, const Cancel &cancel) {
    require(limit <= MaxBinary, "Compressed section exceeds its output limit.");
    z_stream stream{};
    require(inflateInit(&stream) == Z_OK, "Cannot initialize section decompression.");
    struct End {
        z_stream *stream;
        ~End() { inflateEnd(stream); }
    } end{&stream};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    QByteArray result;
    int status = Z_OK;
    do {
        cancelled(cancel);
        char block[64 * 1024];
        stream.next_out = reinterpret_cast<Bytef *>(block);
        stream.avail_out = sizeof(block);
        status = inflate(&stream, Z_NO_FLUSH);
        require(status == Z_OK || status == Z_STREAM_END, "Invalid or truncated compressed PS3 section.");
        const auto produced = sizeof(block) - stream.avail_out;
        require(quint64(result.size()) + produced <= limit,
                "Compressed PS3 section exceeds its output limit.");
        result.append(block, qsizetype(produced));
    } while (status != Z_STREAM_END);
    require(stream.avail_in == 0, "Trailing data in compressed PS3 section.");
    return result;
}
namespace {
QByteArray decodeHex(const QString &text) {
    auto value = text;
    value.remove(QRegularExpression("\\s"));
    require(value.size() % 2 == 0 && QRegularExpression("^[0-9a-fA-F]*$").match(value).hasMatch(),
            "Invalid key file.");
    return QByteArray::fromHex(value.toLatin1());
}
int integer(const QJsonValue &value, int fallback = 0) {
    if (value.isUndefined())
        return fallback;
    const auto number = value.toDouble(-1);
    require(value.isDouble() && std::isfinite(number) && number >= 0 && number <= 65535 &&
                number == std::floor(number),
            "Invalid key file.");
    return int(number);
}
void addRoot(KeyStore &keys, int category, int kind, int revision, const QString &erk, const QString &riv) {
    const auto key = decodeHex(erk), iv = decodeHex(riv);
    require((category == 1 || category == 3) && kind >= 0 && kind <= 8 && revision >= 0 &&
                revision <= 65535 && key.size() == 32 && iv.size() == 16,
            "Invalid key file.");
    keys.roots.append({category, kind, revision, key, iv});
}
} // namespace
KeyStore KeyStore::fromFile(const QString &path) {
    if (path.isEmpty())
        return {};
    return parse(readFile(path, 2 * 1024 * 1024));
}
KeyStore KeyStore::parse(const QByteArray &bytes) {
    try {
        require(bytes.size() <= 2 * 1024 * 1024, "Invalid key file.");
        QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
        QString text = decoder.decode(bytes);
        require(!decoder.hasError(), "Invalid key file.");
        if (text.startsWith(QChar(0xfeff)))
            text.remove(0, 1);
        KeyStore keys;
        if (text.trimmed().startsWith('{')) {
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(text.toUtf8(), &error);
            require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid key file.");
            const auto root = doc.object();
            require(integer(root["format"], -1) == 1, "Invalid key file.");
            require((root["roots"].isUndefined() || root["roots"].isArray()) &&
                        (root["named"].isUndefined() || root["named"].isObject()),
                    "Invalid key file.");
            for (const auto &value : root["roots"].toArray()) {
                require(value.isObject(), "Invalid key file.");
                const auto entry = value.toObject();
                require(entry["erk"].isString() && entry["riv"].isString(), "Invalid key file.");
                addRoot(keys, integer(entry["category"], -1), integer(entry["self_type"]),
                        integer(entry["revision"]), entry["erk"].toString(), entry["riv"].toString());
            }
            const auto names = root["named"].toObject();
            for (auto it = names.begin(); it != names.end(); ++it) {
                require(it.value().isString(), "Invalid key file.");
                keys.named[it.key().toLower()] = decodeHex(it.value().toString());
            }
        } else {
            QString section;
            QMap<QString, QString> values;
            auto flush = [&] {
                if (section.isEmpty())
                    return;
                const auto type = values.value("type").toUpper();
                if ((type == "SELF" || type == "PKG") && values.contains("erk") && values.contains("riv")) {
                    const QMap<QString, int> types{{"LV0", 1}, {"LV1", 2}, {"LV2", 3},  {"APP", 4},
                                                   {"ISO", 5}, {"LDR", 6}, {"UNK7", 7}, {"NPDRM", 8}};
                    bool ok = false;
                    const int revision = values.value("revision", "0").toInt(&ok, 16);
                    require(ok, "Invalid key file.");
                    addRoot(keys, type == "SELF" ? 1 : 3, types.value(values.value("self_type").toUpper(), 0),
                            revision, values.value("erk"), values.value("riv"));
                } else if (values.contains("key"))
                    keys.named[section.toLower()] = decodeHex(values.value("key"));
            };
            for (const auto &line : text.split('\n')) {
                const auto value = line.trimmed();
                if (value.isEmpty() || value.startsWith('#') || value.startsWith(';'))
                    continue;
                if (value.startsWith('[') && value.endsWith(']')) {
                    flush();
                    section = value.mid(1, value.size() - 2);
                    require(!section.isEmpty(), "Invalid key file.");
                    values.clear();
                    continue;
                }
                const auto equal = value.indexOf('=');
                require(!section.isEmpty() && equal > 0, "Invalid key file.");
                const auto name = value.left(equal).trimmed().toLower();
                require(!values.contains(name), "Invalid key file.");
                values[name] = value.mid(equal + 1).trimmed();
            }
            require(!section.isEmpty(), "Invalid key file.");
            flush();
        }
        return keys;
    } catch (const std::exception &) {
        fail("Invalid decryption key file. See docs/game-import.md for supported formats.");
    }
}
QByteArray KeyStore::namedKey(const QString &name, int size) const {
    const auto result = named.value(name.toLower());
    require(result.size() == size,
            "Private key file is missing " + name + " (" + QString::number(size) + " bytes).");
    return result;
}
QVector<RootKey> KeyStore::candidates(int category, int kind, int revision) const {
    QVector<RootKey> result;
    for (const auto &entry : roots)
        if (entry.category == category && entry.kind == kind)
            result << entry;
    std::stable_sort(result.begin(), result.end(), [&](const RootKey &a, const RootKey &b) {
        return (a.revision != revision) < (b.revision != revision);
    });
    return result;
}
} // namespace prepare
