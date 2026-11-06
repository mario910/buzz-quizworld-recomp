#pragma once
#include "ppu_decode.h"

namespace prepare {
// Returns an empty string for instructions handled by another lowering stage.
QString lowerPpuScalar(const PpuInstruction &instruction, quint32 tocBase = 0);
} // namespace prepare
