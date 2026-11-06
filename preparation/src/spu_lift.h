#pragma once
#include "spu_decode.h"
#include <QSet>

namespace prepare {
struct SpuBound {
    int start;
    int end;
};
struct SpuLiftOptions {
    QString prefix;
    QSet<int> forceIndirect;
    bool trace = false;
};
struct SpuLiftResult {
    QByteArray header;
    QByteArray source;
    QJsonObject report;
};
QVector<SpuInstruction> decodeSpuText(const QByteArray &elf, const Cancel &cancel = {});
QVector<SpuBound> findSpuBounds(const QByteArray &elf, const QVector<SpuInstruction> &code,
                                const Cancel &cancel = {});
QVector<SpuBound> findRawSpuBounds(const QVector<SpuInstruction> &code, int entry, const QSet<int> &extra,
                                   const Cancel &cancel = {});
QVector<SpuBound> findSpuModuleBounds(const QByteArray &text, const QVector<SpuInstruction> &code,
                                      const QVector<int> &entries, const Cancel &cancel = {});
SpuLiftResult liftSpu(const QVector<SpuInstruction> &instructions, const QVector<SpuBound> &bounds,
                      const SpuLiftOptions &options = {}, const Cancel &cancel = {});
QJsonArray liftSpuWorkloads(const QString &images, const QString &output, const QString &title,
                            const Cancel &cancel = {});
QJsonArray liftSpuRawJobs(const QByteArray &elf, const QString &titleId, const QString &output,
                          const Cancel &cancel = {});
QJsonArray liftMultiStream(const QByteArray &elf, const QString &titleId, const QString &firmware,
                           const QString &output, const Cancel &cancel = {});
} // namespace prepare
