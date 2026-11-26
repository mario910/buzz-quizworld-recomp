#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <atomic>

namespace studio {
inline QString findMediaFfmpeg() {
#ifdef Q_OS_WIN
    const auto bundled = QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg.exe");
#else
    const auto bundled = QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg");
#endif
    return QFileInfo::exists(bundled) ? bundled : QStandardPaths::findExecutable("ffmpeg");
}
inline QString configuredMediaFfmpeg() {
    auto path = QSettings().value("build/ffmpeg").toString();
    const QUrl url(path);
    if (url.isLocalFile())
        path = url.toLocalFile();
    if (!path.isEmpty())
        if (const auto executable = QStandardPaths::findExecutable(path); !executable.isEmpty())
            return executable;
    return findMediaFfmpeg();
}
inline QString missingMediaFfmpeg() {
    return QCoreApplication::translate(
        "MediaTools", "Do dekodowania mediów wymagany jest FFmpeg. Zainstaluj go i dodaj do PATH "
                      "lub wskaż plik wykonywalny w ustawieniach budowania: Silnik mediów (FFmpeg).");
}
} // namespace studio

namespace studio::pack {
void checkCancelled(const std::atomic_bool *cancel);
void runMedia(const QString &executable, const QStringList &arguments, const std::atomic_bool *cancel,
              const QString &limitedOutput = {});
} // namespace studio::pack
