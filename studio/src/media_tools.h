#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <atomic>
#include <stdexcept>

namespace studio {
QString findMediaFfmpeg();
QString managedOmniVoiceManifest();
QJsonObject managedOmniVoiceRuntime();
QString managedMediaFfmpeg();
bool verifyMediaFfmpeg(const QString &executable);
struct FfmpegInstallation {
    QString manager, program;
    QStringList arguments;
    QString terminalCommand;
};
FfmpegInstallation ffmpegInstallation();
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
        "MediaTools", "Nie znaleziono FFmpeg. Skonfiguruj go w menu Question Studio.");
}
} // namespace studio

namespace studio::pack {
class MediaEnvironmentError final : public std::runtime_error {
  public:
    explicit MediaEnvironmentError(const QString &message) : std::runtime_error(message.toStdString()) {}
};
void checkCancelled(const std::atomic_bool *cancel);
void runMedia(const QString &executable, const QStringList &arguments, const std::atomic_bool *cancel,
              const QString &limitedOutput = {});
} // namespace studio::pack
