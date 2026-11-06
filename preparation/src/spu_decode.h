#pragma once
#include "spu_images.h"

namespace prepare {
struct SpuInstruction {
    quint32 address, raw;
    QString mnemonic, operands;
    QJsonArray json() const;
};
SpuInstruction decodeSpu(quint32 instruction, quint32 address = 0);
QVector<SpuInstruction> decodeSpuImage(const QByteArray &bytes, const Cancel &cancel = {});
} // namespace prepare
