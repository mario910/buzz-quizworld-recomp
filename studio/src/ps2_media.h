#pragma once

#include "document.h"
#include <optional>

namespace studio {
struct Ps2Media {
    QString image, video, error;
};
// A source reference is not decoded until the player explicitly asks for it.
bool hasPs2Media(const Question &question);
Ps2Media decodePs2Media(const Question &question, const QString &ffmpeg);
// Demux one still (zero-based frame index) or all frames of a clip.
// Rejects malformed or unsupported RSV v1 layouts by throwing std::runtime_error.
// A clip can also return its interleaved stereo PCM (signed LE, 24000 Hz).
QByteArray makePs2Ipu(const QByteArray &rsv, std::optional<int> pictureFrame, QByteArray *pcm = nullptr);
} // namespace studio
