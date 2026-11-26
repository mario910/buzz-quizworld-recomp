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

namespace {
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
            finished("Could not check for updates: " + reply->errorString(), false);
            return;
        }
        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        const auto tag = release.value("tag_name").toString();
        static const QRegularExpression version("^v[0-9]+[.][0-9]+[.][0-9]+$");
        if (!version.match(tag).hasMatch() || release.value("prerelease").toBool() || release.value("draft").toBool()) {
            finished("The release service did not return a stable Buzz Prepare version.", false);
            return;
        }
        if (QVersionNumber::fromString(tag.mid(1)) <= QVersionNumber::fromString(BUZZ_PREPARE_VERSION)) {
            finished("Buzz Prepare is up to date (latest stable release: " + tag + ").", true);
            return;
        }
        const QString image = runningAppImage();
#ifdef Q_OS_WIN
        const QString asset = "BuzzPrepare-windows-x64.zip";
#else
        const QString asset = image.isEmpty() ? "BuzzPrepare-linux-x86_64.tar.gz" : "BuzzSetup-x86_64.AppImage";
#endif
        for (const auto& value : release.value("assets").toArray()) {
            const auto entry = value.toObject();
            if (entry.value("name").toString() != asset) continue;
            const auto hash = entry.value("digest").toString();
            const qint64 size = entry.value("size").toInteger();
            static const QRegularExpression sha("^sha256:[0-9a-f]{64}$");
            if (!sha.match(hash).hasMatch() || size <= 0 || size > 512 * 1024 * 1024) break;
            QString target = image;
            if (target.isEmpty()) {
                target = QFileDialog::getSaveFileName(window, "Download Buzz Prepare " + tag,
                    QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(asset));
            } else if (QMessageBox::question(window, "Update Buzz Prepare",
                        "Install " + tag + " and restart BuzzSetup? An older installed game will then be rebuilt.") != QMessageBox::Yes) {
                target.clear();
            }
            if (target.isEmpty()) { finished("Update cancelled.", true); return; }
            download(tag, asset, target, size, QByteArray::fromHex(hash.mid(7).toLatin1()), !image.isEmpty(), finished);
            return;
        }
        finished("The release has no verified package for this platform.", false);
    });
}

void ReleaseUpdate::download(const QString& tag, const QString& asset, const QString& target,
                             qint64 size, const QByteArray& digest, bool appImage,
                             std::function<void(const QString&, bool)> finished) {
    auto file = std::make_shared<QSaveFile>(target);
    if (!file->open(QIODevice::WriteOnly) || (appImage && !file->setPermissions(QFileInfo(target).permissions()))) {
        finished("Could not stage the update: " + file->errorString(), false);
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
            finished("The update could not be downloaded and verified. Please retry.", false);
        } else if (!file->commit()) {
            finished("Could not save the verified update: " + file->errorString(), false);
        } else if (appImage && QProcess::startDetached(target, {})) {
            finished("Restarting BuzzSetup…", true);
            QCoreApplication::quit();
        } else {
            finished(appImage ? "Buzz Prepare " + tag + " installed. Reopen this AppImage to update your installed game."
                : "Downloaded and verified " + tag + ". Close Buzz Prepare and extract the package over the existing BuzzPrepare folder. Reopen BuzzSetup to update the installed game.\n" + target, true);
        }
    });
}
