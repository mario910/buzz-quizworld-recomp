#pragma once
#include <QByteArray>
#include <QStringList>
#include <atomic>
#include <functional>

namespace studio::pack {
using Progress = std::function<void(int, const QString &)>;
void checkCancelled(const std::atomic_bool *cancel);
void runMedia(const QString &executable, const QStringList &arguments, const std::atomic_bool *cancel,
              const QString &limitedOutput = {});
QByteArray encodeSpeech(const QString &source, const QString &ffmpeg, double gain,
                        const std::atomic_bool *cancel);
QByteArray encodeAtracPcm(const QByteArray &pcm, const std::atomic_bool *cancel = nullptr);
QByteArray silentSpeech();
QByteArray unwrapWem(const QByteArray &bytes);
void validateSdi(const QByteArray &bytes);
void validateRsfSpeech(const QByteArray &bytes);
QByteArray pictureSheet(const QByteArray &templateBytes, const QStringList &images, const QString &ffmpeg,
                        const std::atomic_bool *cancel);
} // namespace studio::pack
