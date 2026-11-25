#pragma once
#include <QByteArray>
#include <QFile>
#include <QMap>
#include <QString>
#include <QVector>

namespace studio {
// Read-only ISO 9660 reader for PS2/PSP disc images (.iso) and PSP compressed
// images (.cso, CISO v1). Only directory records and file extents are read;
// nothing is extracted to disk.
class DiscImage {
  public:
    struct File {
        qint64 offset = 0, size = 0;
    };
    explicit DiscImage(const QString &path);
    DiscImage(const DiscImage &) = delete;
    DiscImage &operator=(const DiscImage &) = delete;
    // Upper-case paths with '/' separators, e.g. "PSP_GAME/PARAM.SFO".
    const QMap<QString, File> &files() const { return files_; }
    bool contains(const QString &path) const { return files_.contains(path.toUpper()); }
    QByteArray read(const QString &path, qint64 limit = 256 * 1024 * 1024);
    static bool isDiscImage(const QString &path);

  private:
    QFile file_;
    bool compressed_ = false;
    quint32 blockSize_ = 2048;
    int align_ = 0;
    qint64 size_ = 0;
    QVector<quint32> index_;
    qint64 cachedBlock_ = -1;
    QByteArray cache_;
    QMap<QString, File> files_;
    QByteArray readAt(qint64 offset, qint64 size);
    QByteArray block(qint64 number);
    void walk(qint64 extent, qint64 size, const QString &prefix, int depth);
};
} // namespace studio
