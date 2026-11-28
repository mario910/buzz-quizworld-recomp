#pragma once
#include "tts.h"
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

namespace studio {
struct Question;
struct VoiceProfile {
    QString recording, transcript;
    bool ready() const;
};
struct VoiceJob {
    QString text, destination;
};
struct VoiceBatch {
    QVector<qsizetype> written;
    QStringList errors;
    bool cancelled = false;
};
QString omniVoicePython();
QString managedOmniVoicePython();
QString managedOmniVoiceBackend();
void setOmniVoicePython(const QString &path);
bool canInstallOmniVoice();
SpeechResult installOmniVoice(const std::atomic_bool &cancel, const std::function<void(int)> &progress);
VoiceProfile omniVoiceProfile(const QString &language);
SpeechResult saveOmniVoiceProfile(const QString &language, const VoiceProfile &profile);
SpeechResult omniVoiceReference(const Question &question);
VoiceProfile omniVoiceInstalledProfile(const QString &language);
VoiceBatch generateOmniVoice(const QString &python, const QString &ffmpeg, const QString &language,
                             const VoiceProfile &profile, const QVector<VoiceJob> &jobs,
                             const std::atomic_bool *cancel, const std::function<void(int, int)> &progress);
} // namespace studio
