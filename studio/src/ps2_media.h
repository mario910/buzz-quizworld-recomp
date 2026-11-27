#pragma once

#include "document.h"
#include <atomic>
#include <memory>
#include <optional>

namespace studio {
namespace pack { class Archive; }
std::unique_ptr<pack::Archive> openPs2Archive(const QString &source, const QString &path);
bool hasPs2Music(const Question &question);
void recoverPs2Music(QVector<Question> &questions, QStringList &warnings, const std::atomic_bool *cancel = nullptr);
bool hasPs2Narration(const Question &question);
QString recoverPs2Narration(const Question &question);
struct Ps2Media {
    QString image, video, error;
};
bool hasPs2Media(const Question &question);
QString ps2ClipKey(const Question &question);
Ps2Media decodePs2Media(const Question &question, const QString &ffmpeg, const std::atomic_bool *cancel = nullptr);
// Batch attach stills only, retaining the original references on failure.
int importPs2Pictures(QVector<Question> &questions, const QString &ffmpeg, QStringList &warnings,
                      const std::atomic_bool *cancel = nullptr, bool includeMegaQuiz = false);
// Demux one still (zero-based frame index) or all frames of a clip.
// Rejects malformed or unsupported RSV v1 layouts by throwing std::runtime_error.
// A clip can also return its interleaved signed 16-bit little-endian PCM.
QByteArray makePs2Ipu(const QByteArray &rsv, std::optional<int> pictureFrame, QByteArray *pcm = nullptr);
} // namespace studio
