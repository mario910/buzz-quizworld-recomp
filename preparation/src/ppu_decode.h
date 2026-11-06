#pragma once
#include "common.h"

namespace prepare {
struct PpuInstruction {
    quint32 address, raw;
    QString mnemonic, operands, comment;
};
PpuInstruction decodePpu(quint32 word, quint32 address = 0);
} // namespace prepare
