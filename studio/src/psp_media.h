#pragma once

#include "disc_image.h"
#include "document.h"
#include <QHash>
#include <atomic>

namespace studio {
using PspAssetIndex = QHash<quint32, DiscImage::File>;
PspAssetIndex readPspAssetIndex(DiscImage &disc);
quint32 pspStringHash(const QByteArray &key);
quint32 pspPathHash(const QString &path, bool lowerCase = false);
QByteArray readPspAsset(const QJsonObject &reference);
bool hasPspPicture(const Question &question);
QString recoverPspPicture(const Question &question);
bool hasPspMusic(const Question &question);
void recoverPspMusic(QVector<Question> &questions, QStringList &warnings, const std::atomic_bool *cancel = nullptr);
bool hasPspNarration(const Question &question);
QString recoverPspNarration(const Question &question);
QHash<QString, QString> recoverPspNarrationsForBuild(QVector<Question> &questions, const std::atomic_bool *cancel);
int recoverPspPictures(QVector<Question> &questions, QStringList &warnings, const std::atomic_bool *cancel = nullptr);
}
