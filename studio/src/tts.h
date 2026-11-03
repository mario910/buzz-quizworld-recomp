#pragma once
#include <QString>
#include <QVariantList>

namespace studio {
struct SpeechResult {
    QString path, error;
    bool ok() const { return error.isEmpty(); }
};
// OS adapter boundary: the editor/controller does not depend on a provider SDK.
QVariantList speechVoices();
SpeechResult synthesizeSpeech(const QString &text, const QString &voice, int rate, const QString &path);
} // namespace studio
