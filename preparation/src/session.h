#pragma once
#include "disc.h"
#include "import.h"
#include <QJsonObject>

namespace prepare {
struct Inputs {
    QString game, firmware, keys, update;
    int jobs = 6;
    bool upscaler = false;
    QJsonObject json() const;
    void validate() const;
};
struct ImportHooks {
    std::function<void(const QString &)> firmware = validateFirmware;
    std::function<QString(const QString &, const QString &)> profile = identify;
};
// Imported data stays separate from source inputs. This stage never registers
// a playable build: generation/build must finish before last-ready is published.
QJsonObject importSession(const Inputs &inputs, const QString &session, bool resume = false,
                          const Log &log = {}, const Cancel &cancel = {}, const ImportHooks &hooks = {});
QJsonObject fingerprint(const QString &path, const Cancel &cancel = {});
QJsonObject treeFingerprint(const QString &path, const Cancel &cancel = {});
} // namespace prepare
