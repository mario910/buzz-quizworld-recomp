#include "media_tools.h"
#include "pack_format.h"
#include <QElapsedTimer>
#include <QProcess>
#include <QStorageInfo>
#include <QSysInfo>
#include <utility>
#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

namespace studio {
namespace {
#ifdef Q_OS_WIN
QString directoryFromEnvironment(const char *variable, const QString &relative) {
    const auto root = qEnvironmentVariable(variable);
    return root.isEmpty() ? QString() : QDir(root).filePath(relative);
}
#endif
} // namespace

QString findMediaFfmpeg() {
    const auto besideStudio = QStandardPaths::findExecutable("ffmpeg", {QCoreApplication::applicationDirPath()});
    if (!besideStudio.isEmpty())
        return besideStudio;
    const auto onPath = QStandardPaths::findExecutable("ffmpeg");
    if (!onPath.isEmpty())
        return onPath;
    // GUI apps keep the PATH they started with. Check standard package manager directories as well.
#ifdef Q_OS_WIN
    QStringList directories{directoryFromEnvironment("LOCALAPPDATA", "Microsoft/WinGet/Links"),
                            directoryFromEnvironment("ProgramFiles", "WinGet/Links")};
    directories.removeAll(QString());
    return directories.isEmpty() ? QString() : QStandardPaths::findExecutable("ffmpeg.exe", directories);
#elif defined(Q_OS_MACOS)
    return QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"});
#else
    return QStandardPaths::findExecutable("ffmpeg", {"/usr/bin", "/usr/local/bin", "/bin"});
#endif
}

bool verifyMediaFfmpeg(const QString &executable) {
    if (executable.isEmpty())
        return false;
    QProcess process;
    process.start(executable, {"-version"});
    if (!process.waitForFinished(3000)) {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000);
        }
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
           process.readAllStandardOutput().startsWith("ffmpeg version");
}

FfmpegInstallation ffmpegInstallation() {
#ifdef Q_OS_WIN
    auto winget = QStandardPaths::findExecutable("winget.exe");
    const auto windowsApps = directoryFromEnvironment("LOCALAPPDATA", "Microsoft/WindowsApps");
    if (winget.isEmpty() && !windowsApps.isEmpty()) {
        const auto alias = QDir(windowsApps).filePath("winget.exe");
        if (QFileInfo(alias).isFile())
            winget = alias;
    }
    if (!winget.isEmpty())
        return {"WinGet", winget, {"install", "--id", "Gyan.FFmpeg", "--exact", "--source", "winget",
                                  "--scope", "user", "--disable-interactivity"}, {}};
#elif defined(Q_OS_MACOS)
    auto brew = QStandardPaths::findExecutable("brew");
    if (brew.isEmpty())
        brew = QStandardPaths::findExecutable("brew", {"/opt/homebrew/bin", "/usr/local/bin"});
    if (!brew.isEmpty())
        return {"Homebrew", brew, {"install", "ffmpeg"}, {}};
#elif defined(Q_OS_LINUX)
    if (QSysInfo::productType() == "steamos" || QFileInfo::exists("/run/ostree-booted") ||
        QStorageInfo("/usr").isReadOnly())
        return {};
    const QStringList systemPaths{"/usr/bin", "/bin"};
    const std::pair<QString, QStringList> managers[] = {
        {"apt-get", {"install", "-y", "ffmpeg"}},
        {"dnf", {"install", "-y", "/usr/bin/ffmpeg"}},
        {"pacman", {"-S", "--needed", "--noconfirm", "ffmpeg"}},
    };
    for (const auto &[name, arguments] : managers) {
        const auto manager = QStandardPaths::findExecutable(name, systemPaths);
        if (manager.isEmpty())
            continue;
        const auto command = (QStringList{"sudo", manager} + arguments).join(' ');
        if (geteuid() == 0)
            return {name, manager, arguments, {}};
        const auto pkexec = QStandardPaths::findExecutable("pkexec", systemPaths);
        if (!pkexec.isEmpty())
            return {name, pkexec, QStringList{manager} + arguments, command};
        return {name, {}, {}, command};
    }
#endif
    return {};
}
} // namespace studio

namespace studio::pack {
void checkCancelled(const std::atomic_bool *cancel) {
    if (cancel && cancel->load())
        fail(QCoreApplication::translate("PackBuilder", "Budowanie anulowane."));
}
void runMedia(const QString &executable, const QStringList &arguments, const std::atomic_bool *cancel,
              const QString &limitedOutput) {
    checkCancelled(cancel);
    if (executable.isEmpty())
        throw MediaEnvironmentError(missingMediaFfmpeg());
    QProcess process;
    process.start(executable, arguments);
    if (!process.waitForStarted(5000))
        throw MediaEnvironmentError(process.errorString());
    QElapsedTimer timer;
    timer.start();
    QByteArray errors;
    while (!process.waitForFinished(100)) {
        errors = (errors + process.readAllStandardError()).right(8000);
        if ((cancel && cancel->load()) || timer.elapsed() > 180000 ||
            (!limitedOutput.isEmpty() && QFileInfo(limitedOutput).size() > 48000ll * 2 * 600)) {
            process.kill();
            process.waitForFinished(5000);
            checkCancelled(cancel);
            fail(QCoreApplication::translate(
                "PackBuilder", "Silnik mediów przekroczył limit czasu lub długości nagrania (10 minut)."));
        }
    }
    errors = (errors + process.readAllStandardError()).right(8000);
    checkCancelled(cancel);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        fail(QCoreApplication::translate("PackBuilder", "FFmpeg: ") + QString::fromUtf8(errors));
}
} // namespace studio::pack
