#include "project_media.h"
#include "pack_format.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <functional>

namespace studio {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("ProjectMedia", text); }
constexpr qint64 maxEntry = 512ll * 1024 * 1024; // Same limit as the archive reader.

// Visits every media link of the document: the active language's fields and
// every other translation. The callback may rewrite the path.
void visitMedia(Document &document, const std::function<void(QString &, const QString &)> &visit) {
    for (auto &q : document.questions) {
        visit(q.image, "media/images");
        visit(q.speech, "media/audio/" + q.language);
        visit(q.video, "media/video");
        visit(q.music, "media/music");
        for (auto it = q.translations.begin(); it != q.translations.end(); ++it) {
            if (it.key() == q.language)
                continue;
            auto variant = it.value().toObject();
            for (const auto &[field, folder] : {std::pair<const char *, QString>{"image", "media/images"},
                                                {"speech", "media/audio/" + it.key()},
                                                {"video", "media/video"},
                                                {"music", "media/music"}}) {
                auto path = variant[field].toString();
                if (path.isEmpty())
                    continue;
                visit(path, folder);
                variant[field] = path;
            }
            it.value() = variant;
        }
    }
}

QString safeFolder(const QString &folder) {
    static const QRegularExpression unsafe("[^A-Za-z0-9_/-]");
    return QString(folder).replace(unsafe, "_");
}

// "<name>-<first 8 hex of SHA-256>.<ext>": readable, and the same content is stored once.
QString mediaName(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        *error = file.errorString();
        return {};
    }
    const QFileInfo info(path);
    static const QRegularExpression unsafe("[^A-Za-z0-9_-]+");
    auto base = info.completeBaseName().replace(unsafe, "_").left(48);
    if (base.isEmpty())
        base = "media";
    const auto suffix = info.suffix().toLower().replace(unsafe, "");
    return base + "-" + QString::fromLatin1(hash.result().toHex().left(8)) + (suffix.isEmpty() ? "" : "." + suffix);
}

bool inside(const QString &path, const QString &dir) {
    const auto file = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const auto root = QDir::cleanPath(QDir(dir).absolutePath()) + "/";
#ifdef Q_OS_WIN
    return file.startsWith(root, Qt::CaseInsensitive);
#else
    return file.startsWith(root);
#endif
}

// ---- stored ZIP writer (media are already compressed formats)
struct ZipWriter {
    QSaveFile file;
    QByteArray central;
    int count = 0;
    explicit ZipWriter(const QString &path) : file(path) {}
    static void put(QByteArray &b, quint32 value, int size) {
        for (int i = 0; i < size; ++i)
            b.append(char((value >> (8 * i)) & 0xff));
    }
    bool add(const QString &name, const QByteArray &data) {
        const auto utf8 = name.toUtf8();
        const auto crc = pack::crc32(data);
        const auto offset = file.pos();
        if (offset > 0xffffffffll || data.size() > 0xffffffffll)
            return false;
        QByteArray local("PK\3\4", 4);
        put(local, 10, 2);     // version needed
        put(local, 0x800, 2);  // UTF-8 names
        put(local, 0, 2);      // stored
        put(local, 0, 2);      // time
        put(local, 0x21, 2);   // date 1980-01-01
        put(local, crc, 4);
        put(local, quint32(data.size()), 4);
        put(local, quint32(data.size()), 4);
        put(local, quint32(utf8.size()), 2);
        put(local, 0, 2);
        if (file.write(local + utf8) < 0 || file.write(data) != data.size())
            return false;
        QByteArray entry("PK\1\2", 4);
        put(entry, 20, 2);
        put(entry, 10, 2);
        put(entry, 0x800, 2);
        put(entry, 0, 2);
        put(entry, 0, 2);
        put(entry, 0x21, 2);
        put(entry, crc, 4);
        put(entry, quint32(data.size()), 4);
        put(entry, quint32(data.size()), 4);
        put(entry, quint32(utf8.size()), 2);
        for (int i = 0; i < 4; ++i)
            put(entry, 0, 2); // extra, comment, disk, internal attributes
        put(entry, 0, 4);     // external attributes
        put(entry, quint32(offset), 4);
        central += entry + utf8;
        ++count;
        return true;
    }
    bool finish() {
        const auto start = file.pos();
        if (count > 0xfffe || start > 0xffffffffll || file.write(central) != central.size())
            return false;
        QByteArray end("PK\5\6", 4);
        put(end, 0, 2);
        put(end, 0, 2);
        put(end, quint32(count), 2);
        put(end, quint32(count), 2);
        put(end, quint32(central.size()), 4);
        put(end, quint32(start), 4);
        put(end, 0, 2);
        return file.write(end) == end.size() && file.commit();
    }
};
} // namespace

MediaReport collectMedia(Document &document, const QString &projectDir) {
    MediaReport report;
    QHash<QString, QString> done; // source -> destination
    visitMedia(document, [&](QString &path, const QString &folder) {
        if (path.isEmpty() || !report.error.isEmpty())
            return;
        if (done.contains(path)) {
            path = done[path];
            return;
        }
        const QFileInfo info(path);
        if (!info.isFile()) {
            ++report.missing;
            return;
        }
        if (inside(path, projectDir))
            return;
        QString error;
        const auto name = mediaName(path, &error);
        if (name.isEmpty()) {
            report.error = error;
            return;
        }
        const QDir dir(QDir(projectDir).filePath(safeFolder(folder)));
        const auto target = dir.filePath(name);
        if (!QFileInfo::exists(target)) {
            if (!QDir().mkpath(dir.absolutePath())) {
                report.error = tr("Nie można utworzyć folderu %1.").arg(dir.absolutePath());
                return;
            }
            const auto part = target + ".part";
            QFile::remove(part);
            if (!QFile::copy(path, part) || !QFile::rename(part, target)) {
                QFile::remove(part);
                report.error = tr("Nie można skopiować %1 do projektu.").arg(info.fileName());
                return;
            }
            ++report.copied;
        }
        done[path] = target;
        path = target;
    });
    return report;
}

MediaReport exportProjectArchive(const Document &source, const QString &archivePath) {
    MediaReport report;
    auto document = source;
    // Media are named exactly as in a project folder, relative to a virtual root.
    const auto root = QDir::cleanPath(QDir::tempPath() + "/buzzproject-root");
    QMap<QString, QString> entries; // archive name -> file
    QHash<QString, QString> done;
    visitMedia(document, [&](QString &path, const QString &folder) {
        if (path.isEmpty() || !report.error.isEmpty())
            return;
        if (done.contains(path)) {
            path = done[path];
            return;
        }
        if (!QFileInfo(path).isFile()) {
            ++report.missing;
            return;
        }
        QString error;
        const auto name = mediaName(path, &error);
        if (name.isEmpty()) {
            report.error = error;
            return;
        }
        const auto entry = safeFolder(folder) + "/" + name;
        entries[entry] = path;
        done[path] = root + "/" + entry;
        path = done[path];
    });
    if (!report.ok())
        return report;
    ZipWriter zip(archivePath);
    if (!zip.file.open(QIODevice::WriteOnly)) {
        report.error = zip.file.errorString();
        return report;
    }
    const auto json = serialize(document, root);
    if (!zip.add(QFileInfo(archivePath).completeBaseName() + ".buzz.json", json)) {
        report.error = zip.file.errorString();
        return report;
    }
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        QFile file(it.value());
        if (file.size() > maxEntry) {
            report.error = tr("Plik %1 jest większy niż 512 MB.").arg(QFileInfo(it.value()).fileName());
            return report;
        }
        if (!file.open(QIODevice::ReadOnly) || !zip.add(it.key(), file.readAll())) {
            report.error = tr("Nie można dodać %1 do archiwum.").arg(QFileInfo(it.value()).fileName());
            return report;
        }
        ++report.copied;
    }
    if (!zip.finish())
        report.error = tr("Nie można zapisać archiwum projektu (limit 4 GB).");
    return report;
}

bool isProjectArchive(const QString &path) { return path.endsWith(".buzzproject", Qt::CaseInsensitive); }

LoadResult openProjectArchive(const QString &archivePath) {
    LoadResult out;
    try {
        pack::Archive archive(archivePath);
        static const QRegularExpression project("^[^/\\\\:]+\\.buzz\\.json$");
        static const QRegularExpression media("^media(/[A-Za-z0-9_.-]+)+$");
        QString json;
        for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it) {
            const auto &name = it.key();
            if (name.endsWith('/'))
                continue;
            if (project.match(name).hasMatch()) {
                if (!json.isEmpty())
                    pack::fail(tr("Archiwum zawiera więcej niż jeden projekt."));
                json = name;
            } else if (!media.match(name).hasMatch() || name.contains("/../") || name.contains("/./")) {
                pack::fail(tr("Archiwum projektu zawiera nieoczekiwaną ścieżkę: %1").arg(name));
            }
        }
        if (json.isEmpty())
            pack::fail(tr("To nie jest archiwum projektu Question Studio."));
        // A new folder next to the archive; an existing one is never overwritten.
        const QFileInfo info(archivePath);
        auto target = info.absoluteDir().filePath(info.completeBaseName());
        for (int n = 2; QFileInfo::exists(target); ++n)
            target = info.absoluteDir().filePath(QString("%1 (%2)").arg(info.completeBaseName()).arg(n));
        for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it) {
            if (it.key().endsWith('/'))
                continue;
            const auto path = QDir(target).filePath(it.key());
            if (!QDir().mkpath(QFileInfo(path).absolutePath()))
                pack::fail(tr("Nie można utworzyć folderu %1.").arg(QFileInfo(path).absolutePath()));
            const auto bytes = archive.read(it.key());
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                pack::fail(file.errorString());
        }
        out = loadDocument(QDir(target).filePath(json));
        if (out.ok()) {
            out.projectPath = QDir(target).filePath(json);
            out.warnings.prepend(tr("Projekt rozpakowano do folderu %1.").arg(QDir::toNativeSeparators(target)));
        }
    } catch (const std::exception &e) {
        out.error = QString::fromUtf8(e.what());
    }
    return out;
}
} // namespace studio
