#pragma once
#include "elf.h"

namespace prepare {
QJsonArray findPpuFunctions(const ElfImage &elf, const QJsonArray &externalSeeds, const Cancel &cancel = {});
} // namespace prepare
