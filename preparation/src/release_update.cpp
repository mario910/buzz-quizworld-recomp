#include "release_update.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QProcess>
#include <QStandardPaths>
#include <QVersionNumber>
#include <QWidget>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString T(const char* text) { return QCoreApplication::translate("BuzzSetup", text); }
const QString repository = QStringLiteral("mario910/buzz-quizworld-recomp");

QNetworkRequest request(const QUrl& url) {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "BuzzPrepare/" BUZZ_PREPARE_VERSION);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    return req;
}

QString runningAppImage() {
    const QString mount = QFileInfo(qEnvironmentVariable("APPDIR")).canonicalFilePath();
    const QString image = qEnvironmentVariable("APPIMAGE");
    if (!mount.isEmpty() && !image.isEmpty() && QFileInfo(image).isFile() &&
        QCoreApplication::applicationFilePath().startsWith(mount + '/'))
        return QFileInfo(image).canonicalFilePath();
    return {};
}

#ifdef Q_OS_WIN
// The Windows package folder (BuzzSetup.exe next to build-files/release-sources.json)
// is replaced in place by a helper that runs after BuzzSetup exits, because Windows
// keeps the running executable and its Qt libraries locked.
QString packageFolder() {
    const QDir app(QCoreApplication::applicationDirPath());
    return app.exists("build-files/release-sources.json") && app.exists("BuzzSetup.exe") ? app.absolutePath() : QString();
}

QString stagingFolder(const QString& package) { return package + "/.buzz-setup-update"; }

constexpr auto installScript = R"ps1(param([string]$Zip, [string]$Install, [int]$ProcessId)
$ErrorActionPreference = 'Stop'
$work = Split-Path -Parent $Zip
$result = Join-Path $work 'result.txt'
try {
    try { Wait-Process -Id $ProcessId -Timeout 120 } catch {}
    $staged = Join-Path $work 'staged'
    if (Test-Path -LiteralPath $staged) { Remove-Item -LiteralPath $staged -Recurse -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($Zip, $staged)
    $source = Join-Path $staged 'BuzzPrepare'
    if (-not (Test-Path -LiteralPath (Join-Path $source 'BuzzSetup.exe')) -or
        -not (Test-Path -LiteralPath (Join-Path $source 'build-files\release-sources.json'))) {
        throw 'The downloaded package does not contain BuzzSetup.'
    }
    & robocopy.exe $source $Install /E /R:30 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "Some files could not be replaced (robocopy $LASTEXITCODE). Close Question Studio and the game, then update again."
    }
    Remove-Item -LiteralPath $staged -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue
    Set-Content -LiteralPath $result -Encoding UTF8 -Value 'ok'
} catch {
    Set-Content -LiteralPath $result -Encoding UTF8 -Value ("BuzzSetup could not install the update: " + $_.Exception.Message)
}
Start-Process -FilePath (Join-Path $Install 'BuzzSetup.exe') -WorkingDirectory $Install
)ps1";

bool startInstaller(const QString& package, const QString& zip) {
    const QString script = stagingFolder(package) + "/install-update.ps1";
    QSaveFile file(script);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray("\xEF\xBB\xBF") + installScript) < 0 || !file.commit())
        return false;
    QFile::remove(stagingFolder(package) + "/result.txt");
    QProcess helper;
    helper.setProgram(qEnvironmentVariable("SystemRoot", "C:\\Windows") + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    helper.setArguments({"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden",
                         "-File", QDir::toNativeSeparators(script), "-Zip", QDir::toNativeSeparators(zip),
                         "-Install", QDir::toNativeSeparators(package),
                         "-ProcessId", QString::number(QCoreApplication::applicationPid())});
    helper.setWorkingDirectory(QDir::tempPath());
    helper.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags = (args->flags & ~static_cast<DWORD>(CREATE_NEW_CONSOLE)) | CREATE_NO_WINDOW;
    });
    return helper.startDetached();
}
#endif
}

QString ReleaseUpdate::takeInstallResult() {
#ifdef Q_OS_WIN
    const QString package = packageFolder();
    if (package.isEmpty()) return {};
    QFile file(stagingFolder(package) + "/result.txt");
    if (!file.open(QIODevice::ReadOnly)) return {};
    QString result = QString::fromUtf8(file.readAll()).trimmed();
    if (result.startsWith(QChar(0xFEFF))) result.remove(0, 1);
    file.close();
    file.remove();
    return result == "ok" ? T("BuzzSetup %1 installed.").arg(BUZZ_PREPARE_VERSION) : result;
#else
    return {};
#endif
}

ReleaseUpdate::ReleaseUpdate(QWidget* parent) : QObject(parent), window(parent) {}

void ReleaseUpdate::check(std::function<void(const QString&, bool)> finished) {
    auto* reply = network.get(request(QUrl("https://api.github.com/repos/" + repository + "/releases/latest")));
    constexpr qint64 metadataLimit = 1024 * 1024;
    reply->setReadBufferSize(metadataLimit + 1);
    connect(reply, &QIODevice::readyRead, reply, [reply] {
        if (reply->bytesAvailable() > metadataLimit) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, finished] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finished(T("Could not check for updates: %1").arg(reply->errorString()), false);
            return;
        }
        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        const auto tag = release.value("tag_name").toString();
        static const QRegularExpression version("^v[0-9]+[.][0-9]+[.][0-9]+$");
        if (!version.match(tag).hasMatch() || release.value("prerelease").toBool() || release.value("draft").toBool()) {
            finished(T("The release service did not return a stable Buzz Prepare version."), false);
            return;
        }
        if (QVersionNumber::fromString(tag.mid(1)) <= QVersionNumber::fromString(BUZZ_PREPARE_VERSION)) {
            finished(T("Buzz Prepare is up to date (latest stable release: %1).").arg(tag), true);
            return;
        }
        const QString image = runningAppImage();
#ifdef Q_OS_WIN
        const QString asset = "BuzzPrepare-windows-x64.zip";
        const QString package = packageFolder();
        const bool inPlace = !package.isEmpty() && QDir().mkpath(stagingFolder(package));
#else
        const bool inPlace = !image.isEmpty();
        const QString asset = image.isEmpty() ? "BuzzPrepare-linux-x86_64.tar.gz" : "BuzzSetup-x86_64.AppImage";
#endif
        for (const auto& value : release.value("assets").toArray()) {
            const auto entry = value.toObject();
            if (entry.value("name").toString() != asset) continue;
            const auto hash = entry.value("digest").toString();
            const qint64 size = entry.value("size").toInteger();
            static const QRegularExpression sha("^sha256:[0-9a-f]{64}$");
            if (!sha.match(hash).hasMatch() || size <= 0 || size > 512 * 1024 * 1024) break;
#ifdef Q_OS_WIN
            QString target = inPlace ? stagingFolder(package) + "/" + asset : QString();
#else
            QString target = image;
#endif
            if (!inPlace) {
                target = QFileDialog::getSaveFileName(window, T("Download Buzz Prepare %1").arg(tag),
                    QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(asset));
            } else if (QMessageBox::question(window, T("Update Buzz Prepare"),
                        T("Install %1 and restart BuzzSetup? An older installed game will then be rebuilt.").arg(tag)
#ifdef Q_OS_WIN
                        + "\n\n" + T("Close the game and Question Studio first.")
#endif
                        ) != QMessageBox::Yes) {
                target.clear();
            }
            if (target.isEmpty()) { finished(T("Update cancelled."), true); return; }
            download(tag, asset, target, size, QByteArray::fromHex(hash.mid(7).toLatin1()), inPlace, finished);
            return;
        }
        finished(T("The release has no verified package for this platform."), false);
    });
}

void ReleaseUpdate::download(const QString& tag, const QString& asset, const QString& target,
                             qint64 size, const QByteArray& digest, bool inPlace,
                             std::function<void(const QString&, bool)> finished) {
#ifdef Q_OS_WIN
    const bool appImage = false;
#else
    const bool appImage = inPlace;
#endif
    auto file = std::make_shared<QSaveFile>(target);
    if (!file->open(QIODevice::WriteOnly) || (appImage && !file->setPermissions(QFileInfo(target).permissions()))) {
        finished(T("Could not stage the update: %1").arg(file->errorString()), false);
        return;
    }
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto* reply = network.get(request(QUrl("https://github.com/" + repository + "/releases/download/" + tag + "/" + asset)));
    emit downloadProgress(0, size);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, size](qint64 received, qint64) {
        emit downloadProgress(received, size);
    });
    reply->setReadBufferSize(256 * 1024);
    const auto write = [reply, file, hash, size] {
        const auto bytes = reply->readAll();
        if (file->pos() + bytes.size() > size || file->write(bytes) != bytes.size()) return false;
        hash->addData(bytes);
        return true;
    };
    connect(reply, &QIODevice::readyRead, reply, [reply, write] { if (!write()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [=] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || !write() || file->pos() != size || hash->result() != digest) {
            file->cancelWriting();
            finished(T("The update could not be downloaded and verified. Please retry."), false);
        } else if (!file->commit()) {
            finished(T("Could not save the verified update: %1").arg(file->errorString()), false);
        } else if (appImage && QProcess::startDetached(target, {})) {
            finished(T("Restarting BuzzSetup…"), true);
            QCoreApplication::quit();
#ifdef Q_OS_WIN
        } else if (inPlace && startInstaller(packageFolder(), target)) {
            finished(T("Installing Buzz Prepare %1 and restarting BuzzSetup…").arg(tag), true);
            QCoreApplication::quit();
        } else if (inPlace) {
            finished(T("Could not start the update installer. Extract this package over the BuzzPrepare folder:\n%1").arg(target), false);
#endif
        } else {
            finished(appImage ? T("Buzz Prepare %1 installed. Reopen this AppImage to update your installed game.").arg(tag)
                : T("Downloaded and verified %1. Close Buzz Prepare and extract the package over the existing BuzzPrepare folder. Reopen BuzzSetup to update the installed game.\n%2").arg(tag, target), true);
        }
    });
}
