#pragma once
#include "document.h"
#include "pack_media.h"
#include <QVariantMap>

namespace studio {
struct BuildRequest {
    Document document;
    QString templatePath, outputDirectory, ffmpeg, runtimeRoot, titleId = "BCES00647";
    int number = 103, categoryBase = 700;
    bool install = false;
};
struct BuildResult {
    QString error, path, installedPath, backupPath, language, sha256;
    int number = 0, questions = 0, skipped = 0, rounds = 0, categories = 0, pictures = 0;
    bool cancelled = false;
    bool ok() const { return error.isEmpty(); }
    QVariantMap map() const;
};
BuildResult buildPack(const BuildRequest &request, const pack::Progress &progress = {},
                      std::atomic_bool *cancel = nullptr);
BuildResult installPack(const QString &source, const QString &runtimeRoot, const QString &titleId,
                        const pack::Progress &progress = {}, std::atomic_bool *cancel = nullptr);
QString defaultFfmpeg();
QVariantMap suggestBuildSettings(const QString &language);
} // namespace studio
