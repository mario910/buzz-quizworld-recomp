#pragma once

#include "document.h"
#include <atomic>
#include <optional>

namespace studio {
struct Ps2Media {
    QString image, video, error;
};
bool hasPs2Media(const Question &question);
QString ps2ClipKey(const Question &question);
Ps2Media decodePs2Media(const Question &question, const QString &ffmpeg, const std::atomic_bool *cancel = nullptr);
// Batch attach stills only, retaining the original references on failure.
int importPs2Pictures(QVector<Question> &questions, const QString &ffmpeg, QStringList &warnings);
// Demux one still (zero-based frame index) or all frames of a clip.
// Rejects malformed or unsupported RSV v1 layouts by throwing std::runtime_error.
// A clip can also return its interleaved stereo PCM (signed LE, 24000 Hz).
QByteArray makePs2Ipu(const QByteArray &rsv, std::optional<int> pictureFrame, QByteArray *pcm = nullptr);
} // namespace studio
