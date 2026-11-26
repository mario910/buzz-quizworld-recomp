#pragma once
#include "ps2_media.h"

namespace studio {
bool hasSourceQuestionMedia(const Question &question);
QString sourceQuestionClipKey(const Question &question);
Ps2Media decodeSourceQuestionMedia(const Question &question, const QString &ffmpeg,
                                  const std::atomic_bool *cancel = nullptr);
} // namespace studio
