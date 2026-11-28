#pragma once
#include "tts.h"
#include <QStringList>
#include <atomic>
#include <functional>
#include <optional>

namespace studio::omnivoice {
enum class Backend { Cpu, Cuda, Rocm };
QString backendId(Backend backend);
QStringList torchPackages(Backend backend);
bool supportsWindowsRadeon(const QString &adapter, const QString &processor);
bool supportsLinuxRadeon(const QByteArray &properties);
bool hasRocmGpu();
SpeechResult installRuntime(const std::atomic_bool &cancel, const std::function<void(int)> &progress,
                            std::optional<Backend> preferred = std::nullopt);
} // namespace studio::omnivoice
