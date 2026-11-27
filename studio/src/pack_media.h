#pragma once
#include "media_tools.h"
#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <atomic>
#include <functional>

namespace studio::pack {
using Progress = std::function<void(int, const QString &)>;
QByteArray encodeSpeech(const QString &source, const QString &ffmpeg, double gain,
                        const std::atomic_bool *cancel);
QByteArray encodeAtracPcm(const QByteArray &pcm, const std::atomic_bool *cancel = nullptr);
QByteArray encodeStreamedMusicPcm(const QByteArray &pcm, const std::atomic_bool *cancel = nullptr);
QByteArray encodeMusic(const QString &source, const QString &ffmpeg, const std::atomic_bool *cancel);
QByteArray silentSpeech();
QByteArray unwrapWem(const QByteArray &bytes);
QByteArray previewAudioWave(const QString &path);
void validateSdi(const QByteArray &bytes);
void validateRsfSpeech(const QByteArray &bytes);
QByteArray pictureSheet(const QByteArray &templateBytes, const QStringList &images, const QString &ffmpeg,
                        const std::atomic_bool *cancel, QHash<QString, QString> *invalidImages = nullptr);
bool isMovieTemplate(const QByteArray &bytes);
QByteArray encodeMovie(const QByteArray &templateBytes, const QString &source, const QString &ffmpeg,
                       const std::atomic_bool *cancel);
} // namespace studio::pack
