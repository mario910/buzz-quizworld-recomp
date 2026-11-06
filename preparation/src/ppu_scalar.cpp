#include "ppu_scalar.h"
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <initializer_list>

namespace prepare {
namespace {
QString hex(quint32 value, int width = 0) {
    return QString::number(value, 16).toUpper().rightJustified(width, '0');
}
QString text(const QString &value) {
    return value;
}
QString text(int value) {
    return QString::number(value);
}
bool oneOf(const QString &value, std::initializer_list<QString> choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}
QString registerIndex(const QString &token) {
    static const QRegularExpression reg("^[rfv]([0-9]+)");
    const auto match = reg.match(token);
    return match.hasMatch() ? match.captured(1) : token;
}
QString immediate(const QString &token) {
    if (token.startsWith("0x") || token.startsWith("-0x"))
        return token;
    bool valid = false;
    const int value = token.toInt(&valid);
    if (!valid)
        return token;
    return value > 9 ? "0x" + hex(value) : QString::number(value);
}
bool isDecimal(const QString &token) {
    return !token.isEmpty() && std::all_of(token.begin(), token.end(), [](QChar ch) { return ch.isDigit(); });
}
QPair<QString, QString> displacementBase(const QString &token) {
    static const QRegularExpression memory("^(-?0x[0-9A-Fa-f]+|[0-9-]+)\\(r([0-9]+)\\)");
    const auto match = memory.match(token);
    return {match.captured(1), match.captured(2)};
}
// In a displacement or indexed address, rA=0 denotes the constant zero.
QString memoryBase(const QString &base) {
    return base == "0" ? QStringLiteral("0") : QStringLiteral("ctx->gpr[%1]").arg(base);
}

QString arithmetic(const PpuInstruction &insn, const QStringList &ops) {
    const auto &mn = insn.mnemonic;
    if (mn == QStringLiteral("li")) {
        auto rd = registerIndex(ops.value(0));
        return (QStringLiteral("ctx->gpr[") + rd + QStringLiteral("] = (int64_t)(int32_t)(") +
                text(immediate(ops.value(1))) + QStringLiteral(");"));
    }
    if (mn == QStringLiteral("lis")) {
        auto rd = registerIndex(ops.value(0));
        return (QStringLiteral("ctx->gpr[") + rd + QStringLiteral("] = (int64_t)(int32_t)((uint32_t)") +
                text(immediate(ops.value(1))) + QStringLiteral(" << 16);"));
    }
    if (mn == QStringLiteral("addi")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + rd + QStringLiteral("] = ctx->gpr[") + ra +
                QStringLiteral("] + (int64_t)(") + text(immediate(ops.value(2))) + QStringLiteral(");"));
    }
    if (mn == QStringLiteral("addis")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + rd + QStringLiteral("] = ctx->gpr[") + ra +
                QStringLiteral("] + (int64_t)(int32_t)((uint32_t)") + text(immediate(ops.value(2))) +
                QStringLiteral(" << 16);"));
    }
    if ((mn == QStringLiteral("addic")) || (mn == QStringLiteral("addic."))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        return (
            QStringLiteral("{ uint64_t _a = ctx->gpr[") + ra +
            QStringLiteral("]; uint64_t _r = _a + (uint64_t)(int64_t)(") + text(immediate(ops.value(2))) +
            QStringLiteral("); ctx->gpr[") + rd +
            QStringLiteral("] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }"));
    }
    if (oneOf(mn, {QStringLiteral("add"), QStringLiteral("add."), QStringLiteral("addo"),
                   QStringLiteral("addo.")})) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + rd + QStringLiteral("] = ctx->gpr[") + ra +
                QStringLiteral("] + ctx->gpr[") + rb + QStringLiteral("];"));
    }
    if (oneOf(mn, {QStringLiteral("subf"), QStringLiteral("subf."), QStringLiteral("subfo"),
                   QStringLiteral("subfo.")})) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + rd + QStringLiteral("] = ctx->gpr[") + rb +
                QStringLiteral("] - ctx->gpr[") + ra + QStringLiteral("];"));
    }
    if (mn == QStringLiteral("subfic")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        return (QStringLiteral("{ uint64_t _a = ctx->gpr[") + ra +
                QStringLiteral("]; uint64_t _b = (uint64_t)(int64_t)(") + text(immediate(ops.value(2))) +
                QStringLiteral("); ctx->gpr[") + rd +
                QStringLiteral(
                    "] = _b - _a; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_b >= _a) ? (1u << 29) : 0u); }"));
    }
    if (oneOf(mn, {QStringLiteral("neg"), QStringLiteral("neg.")})) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + rd + QStringLiteral("] = (uint64_t)0 - ctx->gpr[") + ra +
                QStringLiteral("];"));
    }
    if (mn == QStringLiteral("mulli")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + rd + QStringLiteral("] = ctx->gpr[") + ra +
                QStringLiteral("] * (uint64_t)(int64_t)(") + text(immediate(ops.value(2))) +
                QStringLiteral(");"));
    }
    if (oneOf(mn, {QStringLiteral("mullw"), QStringLiteral("mullw.")})) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + rd +
                QStringLiteral("] = (uint64_t)((int64_t)(int32_t)ctx->gpr[") + ra +
                QStringLiteral("] * (int64_t)(int32_t)ctx->gpr[") + rb + QStringLiteral("]);"));
    }
    if (mn.startsWith(QStringLiteral("mulhw")) && !(mn.startsWith(QStringLiteral("mulhwu")))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + rd +
                QStringLiteral("] = (int64_t)(int32_t)((int32_t)((int64_t)(int32_t)ctx->gpr[") + ra +
                QStringLiteral("] * (int64_t)(int32_t)ctx->gpr[") + rb + QStringLiteral("] >> 32));"));
    }
    if (mn.startsWith(QStringLiteral("mulhwu"))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + rd +
                QStringLiteral("] = (uint32_t)(((uint64_t)(uint32_t)ctx->gpr[") + ra +
                QStringLiteral("] * (uint64_t)(uint32_t)ctx->gpr[") + rb + QStringLiteral("]) >> 32);"));
    }
    if (oneOf(mn, {QStringLiteral("divw"), QStringLiteral("divw.")})) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ int32_t _a=(int32_t)ctx->gpr[") + ra +
                QStringLiteral("], _b=(int32_t)ctx->gpr[") + rb + QStringLiteral("]; ctx->gpr[") + rd +
                QStringLiteral("] = (uint32_t)((_b==0||(_a==(int32_t)0x80000000&&_b==-1))?0:_a/_b); }"));
    }
    if (oneOf(mn, {QStringLiteral("divwu"), QStringLiteral("divwu.")})) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint32_t _b=(uint32_t)ctx->gpr[") + rb + QStringLiteral("]; ctx->gpr[") +
                rd + QStringLiteral("] = (uint32_t)(_b==0?0:(uint32_t)ctx->gpr[") + ra +
                QStringLiteral("]/_b); }"));
    }
    return {};
}

QString logicalAndShift(const PpuInstruction &insn, const QStringList &ops) {
    const auto &mn = insn.mnemonic;
    if (mn == QStringLiteral("ori")) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ctx->gpr[") + rs +
                QStringLiteral("] | ") + text(immediate(ops.value(2))) + QStringLiteral(";"));
    }
    if (mn == QStringLiteral("oris")) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ctx->gpr[") + rs +
                QStringLiteral("] | ((uint64_t)") + text(immediate(ops.value(2))) +
                QStringLiteral(" << 16);"));
    }
    if (mn == QStringLiteral("xori")) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ctx->gpr[") + rs +
                QStringLiteral("] ^ ") + text(immediate(ops.value(2))) + QStringLiteral(";"));
    }
    if (mn == QStringLiteral("xoris")) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ctx->gpr[") + rs +
                QStringLiteral("] ^ ((uint64_t)") + text(immediate(ops.value(2))) +
                QStringLiteral(" << 16);"));
    }
    if (oneOf(mn, {QStringLiteral("andi."), QStringLiteral("andis.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        auto shift = ((mn == QStringLiteral("andis.")) ? QStringLiteral(" << 16") : QStringLiteral(""));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ctx->gpr[") + rs +
                QStringLiteral("] & ((uint64_t)") + text(immediate(ops.value(2))) + text(shift) +
                QStringLiteral(");"));
    }
    if (mn == QStringLiteral("nop")) {
        return QStringLiteral("/* nop */;");
    }
    for (const auto &[log_mn, c_op] :
         std::initializer_list<QPair<QString, QString>>{{QStringLiteral("and"), QStringLiteral("&")},
                                                        {QStringLiteral("or"), QStringLiteral("|")},
                                                        {QStringLiteral("xor"), QStringLiteral("^")}}) {
        if ((mn == log_mn) || (mn == (log_mn + QStringLiteral(".")))) {
            const auto ra = registerIndex(ops.value(0));
            const auto rs = registerIndex(ops.value(1));
            const auto rb = registerIndex(ops.value(2));
            return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ctx->gpr[") + rs +
                    QStringLiteral("] ") + c_op + QStringLiteral(" ctx->gpr[") + rb + QStringLiteral("];"));
        }
    }
    if (oneOf(mn, {QStringLiteral("nand"), QStringLiteral("nand.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ~(ctx->gpr[") + rs +
                QStringLiteral("] & ctx->gpr[") + rb + QStringLiteral("]);"));
    }
    if (oneOf(mn, {QStringLiteral("nor"), QStringLiteral("nor.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ~(ctx->gpr[") + rs +
                QStringLiteral("] | ctx->gpr[") + rb + QStringLiteral("]);"));
    }
    if (oneOf(mn, {QStringLiteral("eqv"), QStringLiteral("eqv.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ~(ctx->gpr[") + rs +
                QStringLiteral("] ^ ctx->gpr[") + rb + QStringLiteral("]);"));
    }
    if (oneOf(mn, {QStringLiteral("andc"), QStringLiteral("andc.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ctx->gpr[") + rs +
                QStringLiteral("] & ~ctx->gpr[") + rb + QStringLiteral("];"));
    }
    if (oneOf(mn, {QStringLiteral("orc"), QStringLiteral("orc.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ctx->gpr[") + rs +
                QStringLiteral("] | ~ctx->gpr[") + rb + QStringLiteral("];"));
    }
    if (oneOf(mn, {QStringLiteral("extsb"), QStringLiteral("extsb.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = (int64_t)(int8_t)ctx->gpr[") + rs +
                QStringLiteral("];"));
    }
    if (oneOf(mn, {QStringLiteral("extsh"), QStringLiteral("extsh.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = (int64_t)(int16_t)ctx->gpr[") + rs +
                QStringLiteral("];"));
    }
    if (oneOf(mn, {QStringLiteral("extsw"), QStringLiteral("extsw.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = (int64_t)(int32_t)ctx->gpr[") + rs +
                QStringLiteral("];"));
    }
    if (oneOf(mn, {QStringLiteral("cntlzw"), QStringLiteral("cntlzw.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = (uint32_t)ctx->gpr[") + rs +
                QStringLiteral("] ? __builtin_clz((uint32_t)ctx->gpr[") + rs + QStringLiteral("]) : 32;"));
    }
    if (oneOf(mn, {QStringLiteral("cntlzd"), QStringLiteral("cntlzd.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ctx->gpr[") + rs +
                QStringLiteral("] ? __builtin_clzll(ctx->gpr[") + rs + QStringLiteral("]) : 64;"));
    }
    if (mn.startsWith(QStringLiteral("rlwinm"))) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto sh = ops.value(2);
        const auto mb = ops.value(3);
        const auto me = ops.value(4);
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ppc_rlw64(ctx->gpr[") + rs +
                QStringLiteral("], ") + text(sh) + QStringLiteral(", ") + text(mb) + QStringLiteral(", ") +
                text(me) + QStringLiteral(", 0, 0);"));
    }
    if (mn.startsWith(QStringLiteral("rlwimi"))) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto sh = ops.value(2);
        const auto mb = ops.value(3);
        const auto me = ops.value(4);
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ppc_rlw64(ctx->gpr[") + rs +
                QStringLiteral("], ") + text(sh) + QStringLiteral(", ") + text(mb) + QStringLiteral(", ") +
                text(me) + QStringLiteral(", ctx->gpr[") + ra + QStringLiteral("], 1);"));
    }
    if (oneOf(mn, {QStringLiteral("slw"), QStringLiteral("slw.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = (uint64_t)((ctx->gpr[") + rb +
                QStringLiteral("] & 0x20) ? 0u : ((uint32_t)ctx->gpr[") + rs +
                QStringLiteral("] << (ctx->gpr[") + rb + QStringLiteral("] & 0x1F)));"));
    }
    if (oneOf(mn, {QStringLiteral("srw"), QStringLiteral("srw.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = (uint64_t)((ctx->gpr[") + rb +
                QStringLiteral("] & 0x20) ? 0u : ((uint32_t)ctx->gpr[") + rs +
                QStringLiteral("] >> (ctx->gpr[") + rb + QStringLiteral("] & 0x1F)));"));
    }
    if (oneOf(mn, {QStringLiteral("sraw"), QStringLiteral("sraw.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra +
                QStringLiteral("] = ppc_sraw(&ctx->xer, (int32_t)ctx->gpr[") + rs +
                QStringLiteral("], (int)(ctx->gpr[") + rb + QStringLiteral("] & 0x3F));"));
    }
    if (oneOf(mn, {QStringLiteral("srawi"), QStringLiteral("srawi.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra +
                QStringLiteral("] = ppc_sraw(&ctx->xer, (int32_t)ctx->gpr[") + rs + QStringLiteral("], ") +
                text(immediate(ops.value(2))) + QStringLiteral(");"));
    }
    if (mn.startsWith(QStringLiteral("rldicl"))) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto sh = ops.value(2).toInt();
        const auto mb = ops.value(3).toInt();
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ppc_rldicl(ctx->gpr[") + rs +
                QStringLiteral("], ") + text(sh) + QStringLiteral(", ") + text(mb) + QStringLiteral(");"));
    }
    if (mn.startsWith(QStringLiteral("rldicr"))) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto sh = ops.value(2).toInt();
        const auto me = ops.value(3).toInt();
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ppc_rldicr(ctx->gpr[") + rs +
                QStringLiteral("], ") + text(sh) + QStringLiteral(", ") + text(me) + QStringLiteral(");"));
    }
    if (mn.startsWith(QStringLiteral("rldic")) && !(mn.startsWith(QStringLiteral("rldicl"))) &&
        !(mn.startsWith(QStringLiteral("rldicr")))) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto sh = ops.value(2).toInt();
        const auto mb = ops.value(3).toInt();
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ppc_rldic(ctx->gpr[") + rs +
                QStringLiteral("], ") + text(sh) + QStringLiteral(", ") + text(mb) + QStringLiteral(");"));
    }
    if (mn.startsWith(QStringLiteral("rldcl"))) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb_r = registerIndex(ops.value(2));
        auto mb = ops.value(3).toInt();
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ppc_rldicl(ctx->gpr[") + rs +
                QStringLiteral("], (int)(ctx->gpr[") + rb_r + QStringLiteral("] & 63), ") + text(mb) +
                QStringLiteral(");"));
    }
    if (mn.startsWith(QStringLiteral("rldcr"))) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb_r = registerIndex(ops.value(2));
        auto me = ops.value(3).toInt();
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ppc_rldicr(ctx->gpr[") + rs +
                QStringLiteral("], (int)(ctx->gpr[") + rb_r + QStringLiteral("] & 63), ") + text(me) +
                QStringLiteral(");"));
    }
    if (mn.startsWith(QStringLiteral("rldimi"))) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto sh = ops.value(2).toInt();
        const auto mb = ops.value(3).toInt();
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = ppc_rldimi(ctx->gpr[") + ra +
                QStringLiteral("], ctx->gpr[") + rs + QStringLiteral("], ") + text(sh) +
                QStringLiteral(", ") + text(mb) + QStringLiteral(");"));
    }
    if (oneOf(mn, {QStringLiteral("sld"), QStringLiteral("sld.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = (ctx->gpr[") + rb +
                QStringLiteral("] & 64) ? 0 : (ctx->gpr[") + rs + QStringLiteral("] << (ctx->gpr[") + rb +
                QStringLiteral("] & 63));"));
    }
    if (oneOf(mn, {QStringLiteral("srd"), QStringLiteral("srd.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra + QStringLiteral("] = (ctx->gpr[") + rb +
                QStringLiteral("] & 64) ? 0 : (ctx->gpr[") + rs + QStringLiteral("] >> (ctx->gpr[") + rb +
                QStringLiteral("] & 63));"));
    }
    if (oneOf(mn, {QStringLiteral("srad"), QStringLiteral("srad.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + ra +
                QStringLiteral("] = (uint64_t)ppc_srad(&ctx->xer, (int64_t)ctx->gpr[") + rs +
                QStringLiteral("], (int)(ctx->gpr[") + rb + QStringLiteral("] & 0x7F));"));
    }
    if (oneOf(mn, {QStringLiteral("sradi"), QStringLiteral("sradi.")})) {
        const auto ra = registerIndex(ops.value(0));
        const auto rs = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->gpr[") + ra +
                QStringLiteral("] = (uint64_t)ppc_srad(&ctx->xer, (int64_t)ctx->gpr[") + rs +
                QStringLiteral("], ") + text(immediate(ops.value(2))) + QStringLiteral(");"));
    }
    return {};
}

QString memory(const PpuInstruction &insn, const QStringList &ops, quint32 tocBase) {
    const auto &mn = insn.mnemonic;
    static const QHash<QString, QPair<QString, bool>> load_map{
        {QStringLiteral("lbz"), {QStringLiteral("vm_read8"), false}},
        {QStringLiteral("lbzu"), {QStringLiteral("vm_read8"), false}},
        {QStringLiteral("lhz"), {QStringLiteral("vm_read16"), false}},
        {QStringLiteral("lhzu"), {QStringLiteral("vm_read16"), false}},
        {QStringLiteral("lha"), {QStringLiteral("vm_read16"), true}},
        {QStringLiteral("lhau"), {QStringLiteral("vm_read16"), true}},
        {QStringLiteral("lwz"), {QStringLiteral("vm_read32"), false}},
        {QStringLiteral("lwzu"), {QStringLiteral("vm_read32"), false}},
        {QStringLiteral("ld"), {QStringLiteral("vm_read64"), false}},
        {QStringLiteral("ldu"), {QStringLiteral("vm_read64"), false}}};
    if (load_map.contains(mn)) {
        const auto [helper, signExtend] = load_map[mn];
        auto rd_i = registerIndex(ops.value(0));
        const auto [disp, base] = displacementBase(ops.value(1));
        if ((mn == QStringLiteral("ld")) && (rd_i == QStringLiteral("2")) && (base == QStringLiteral("1")) &&
            tocBase) {
            return (QStringLiteral("ctx->gpr[2] = 0x") + hex(tocBase, 8) +
                    QStringLiteral("ULL; /*TOCFIX ld r2,N(r1)*/"));
        }
        if (!disp.isNull()) {
            auto expr = (helper + QStringLiteral("(") + text(memoryBase(base)) + QStringLiteral(" + ") +
                         disp + QStringLiteral(")"));
            if (signExtend && helper.contains(QStringLiteral("16"))) {
                expr = (QStringLiteral("(int64_t)(int16_t)") + expr);
            }
            auto line =
                (QStringLiteral("ctx->gpr[") + rd_i + QStringLiteral("] = ") + expr + QStringLiteral(";"));
            if (mn.endsWith(QStringLiteral("u"))) {
                line += (QStringLiteral(" ctx->gpr[") + base + QStringLiteral("] += ") + disp +
                         QStringLiteral(";"));
            }
            return line;
        }
        return (QStringLiteral("/* ") + mn + QStringLiteral(" unhandled operands: ") + insn.operands +
                QStringLiteral(" */;"));
    }
    if (oneOf(mn, {QStringLiteral("lmw"), QStringLiteral("stmw")})) {
        auto r0_i = registerIndex(ops.value(0));
        const auto [disp, base] = displacementBase(ops.value(1));
        if (!disp.isNull() && isDecimal(r0_i)) {
            auto first = r0_i.toInt();
            QString body;
            if (mn == QStringLiteral("lmw")) {
                body = ([&] {
                    QStringList parts;
                    for (int r = first; r < 32; ++r)
                        parts.append((QStringLiteral("ctx->gpr[") + text(r) +
                                      QStringLiteral("] = vm_read32(_ea + ") + text((4 * (r - first))) +
                                      QStringLiteral(");")));
                    return parts.join(QStringLiteral(" "));
                }());
            } else {
                body = ([&] {
                    QStringList parts;
                    for (int r = first; r < 32; ++r)
                        parts.append((QStringLiteral("vm_write32(_ea + ") + text((4 * (r - first))) +
                                      QStringLiteral(", (uint32_t)ctx->gpr[") + text(r) +
                                      QStringLiteral("]);")));
                    return parts.join(QStringLiteral(" "));
                }());
            }
            return (QStringLiteral("{ uint64_t _ea = ctx->gpr[") + base + QStringLiteral("] + ") + disp +
                    QStringLiteral("; ") + body + QStringLiteral(" }"));
        }
        return (QStringLiteral("/* ") + mn + QStringLiteral(" unhandled operands: ") + insn.operands +
                QStringLiteral(" */;"));
    }
    static const QHash<QString, QString> store_map{{QStringLiteral("stb"), QStringLiteral("vm_write8")},
                                                   {QStringLiteral("stbu"), QStringLiteral("vm_write8")},
                                                   {QStringLiteral("sth"), QStringLiteral("vm_write16")},
                                                   {QStringLiteral("sthu"), QStringLiteral("vm_write16")},
                                                   {QStringLiteral("stw"), QStringLiteral("vm_write32")},
                                                   {QStringLiteral("stwu"), QStringLiteral("vm_write32")},
                                                   {QStringLiteral("std"), QStringLiteral("vm_write64")},
                                                   {QStringLiteral("stdu"), QStringLiteral("vm_write64")}};
    if (store_map.contains(mn)) {
        auto helper = store_map[mn];
        auto rs_i = registerIndex(ops.value(0));
        const auto [disp, base] = displacementBase(ops.value(1));
        if (!disp.isNull()) {
            auto line = (helper + QStringLiteral("(") + text(memoryBase(base)) + QStringLiteral(" + ") +
                         disp + QStringLiteral(", ctx->gpr[") + rs_i + QStringLiteral("]);"));
            if (mn.endsWith(QStringLiteral("u"))) {
                line += (QStringLiteral(" ctx->gpr[") + base + QStringLiteral("] += ") + disp +
                         QStringLiteral(";"));
            }
            return line;
        }
        return (QStringLiteral("/* ") + mn + QStringLiteral(" unhandled operands: ") + insn.operands +
                QStringLiteral(" */;"));
    }
    static const QHash<QString, QString> idx_load_map{{QStringLiteral("lbzx"), QStringLiteral("vm_read8")},
                                                      {QStringLiteral("lhzx"), QStringLiteral("vm_read16")},
                                                      {QStringLiteral("lwzx"), QStringLiteral("vm_read32")},
                                                      {QStringLiteral("ldx"), QStringLiteral("vm_read64")},
                                                      {QStringLiteral("lhax"), QStringLiteral("vm_read16")},
                                                      {QStringLiteral("lwax"), QStringLiteral("vm_read32")},
                                                      {QStringLiteral("lbzux"), QStringLiteral("vm_read8")},
                                                      {QStringLiteral("lhzux"), QStringLiteral("vm_read16")},
                                                      {QStringLiteral("lwzux"), QStringLiteral("vm_read32")},
                                                      {QStringLiteral("ldux"), QStringLiteral("vm_read64")}};
    if (idx_load_map.contains(mn)) {
        auto helper = idx_load_map[mn];
        const auto rd_i = registerIndex(ops.value(0));
        const auto ra_i = registerIndex(ops.value(1));
        const auto rb_i = registerIndex(ops.value(2));
        if (mn.endsWith(QStringLiteral("ux")) && (ra_i != QStringLiteral("0"))) {
            auto cast = QStringLiteral("");
            if (oneOf(mn, {QStringLiteral("lhax"), QStringLiteral("lwax")})) {
                cast = (mn.contains(QStringLiteral("h")) ? QStringLiteral("(int64_t)(int16_t)")
                                                         : QStringLiteral("(int64_t)(int32_t)"));
            }
            return (QStringLiteral("{ uint64_t ea = ctx->gpr[") + ra_i + QStringLiteral("] + ctx->gpr[") +
                    rb_i + QStringLiteral("]; ctx->gpr[") + rd_i + QStringLiteral("] = ") + cast + helper +
                    QStringLiteral("(ea); ctx->gpr[") + ra_i + QStringLiteral("] = ea; }"));
        }
        auto ea =
            ((ra_i != QStringLiteral("0")) ? (QStringLiteral("(ctx->gpr[") + ra_i +
                                              QStringLiteral("] + ctx->gpr[") + rb_i + QStringLiteral("])"))
                                           : (QStringLiteral("ctx->gpr[") + rb_i + QStringLiteral("]")));
        auto expr = (helper + QStringLiteral("(") + ea + QStringLiteral(")"));
        if (oneOf(mn, {QStringLiteral("lhax"), QStringLiteral("lwax")})) {
            auto cast = (mn.contains(QStringLiteral("h")) ? QStringLiteral("(int16_t)")
                                                          : QStringLiteral("(int32_t)"));
            expr = (QStringLiteral("(int64_t)") + cast + expr);
        }
        return (QStringLiteral("ctx->gpr[") + rd_i + QStringLiteral("] = ") + expr + QStringLiteral(";"));
    }
    if (mn == QStringLiteral("lwa")) {
        auto rd_i = registerIndex(ops.value(0));
        const auto [disp, base] = displacementBase(ops.value(1));
        if (!disp.isNull()) {
            return (QStringLiteral("ctx->gpr[") + rd_i +
                    QStringLiteral("] = (int64_t)(int32_t)vm_read32(ctx->gpr[") + base +
                    QStringLiteral("] + ") + disp + QStringLiteral(");"));
        }
    }
    static const QHash<QString, QString> idx_store_map{
        {QStringLiteral("stbx"), QStringLiteral("vm_write8")},
        {QStringLiteral("sthx"), QStringLiteral("vm_write16")},
        {QStringLiteral("stwx"), QStringLiteral("vm_write32")},
        {QStringLiteral("stdx"), QStringLiteral("vm_write64")},
        {QStringLiteral("stbux"), QStringLiteral("vm_write8")},
        {QStringLiteral("sthux"), QStringLiteral("vm_write16")},
        {QStringLiteral("stwux"), QStringLiteral("vm_write32")},
        {QStringLiteral("stdux"), QStringLiteral("vm_write64")}};
    if (idx_store_map.contains(mn)) {
        auto helper = idx_store_map[mn];
        const auto rs_i = registerIndex(ops.value(0));
        const auto ra_i = registerIndex(ops.value(1));
        const auto rb_i = registerIndex(ops.value(2));
        if (mn.endsWith(QStringLiteral("ux")) && (ra_i != QStringLiteral("0"))) {
            return (QStringLiteral("{ uint64_t ea = ctx->gpr[") + ra_i + QStringLiteral("] + ctx->gpr[") +
                    rb_i + QStringLiteral("]; ") + helper + QStringLiteral("(ea, ctx->gpr[") + rs_i +
                    QStringLiteral("]); ctx->gpr[") + ra_i + QStringLiteral("] = ea; }"));
        }
        auto ea =
            ((ra_i != QStringLiteral("0")) ? (QStringLiteral("(ctx->gpr[") + ra_i +
                                              QStringLiteral("] + ctx->gpr[") + rb_i + QStringLiteral("])"))
                                           : (QStringLiteral("ctx->gpr[") + rb_i + QStringLiteral("]")));
        return (helper + QStringLiteral("(") + ea + QStringLiteral(", ctx->gpr[") + rs_i +
                QStringLiteral("]);"));
    }
    if (oneOf(mn, {QStringLiteral("lfsx"), QStringLiteral("lfdx"), QStringLiteral("lfsux"),
                   QStringLiteral("lfdux")})) {
        const auto frd = registerIndex(ops.value(0));
        const auto ra_i = registerIndex(ops.value(1));
        const auto rb_i = registerIndex(ops.value(2));
        auto single = oneOf(mn, {QStringLiteral("lfsx"), QStringLiteral("lfsux")});
        if (mn.endsWith(QStringLiteral("ux")) && (ra_i != QStringLiteral("0"))) {
            auto body =
                (single
                     ? (QStringLiteral(
                            "uint32_t tmp = vm_read32(ea); float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[") +
                        frd + QStringLiteral("] = ftmp;"))
                     : (QStringLiteral("uint64_t tmp = vm_read64(ea); memcpy(&ctx->fpr[") + frd +
                        QStringLiteral("], &tmp, 8);")));
            return (QStringLiteral("{ uint64_t ea = ctx->gpr[") + ra_i + QStringLiteral("] + ctx->gpr[") +
                    rb_i + QStringLiteral("]; ") + body + QStringLiteral(" ctx->gpr[") + ra_i +
                    QStringLiteral("] = ea; }"));
        }
        auto ea =
            ((ra_i != QStringLiteral("0")) ? (QStringLiteral("(ctx->gpr[") + ra_i +
                                              QStringLiteral("] + ctx->gpr[") + rb_i + QStringLiteral("])"))
                                           : (QStringLiteral("ctx->gpr[") + rb_i + QStringLiteral("]")));
        if (single) {
            return (QStringLiteral("{ uint32_t tmp = vm_read32(") + ea +
                    QStringLiteral("); float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[") + frd +
                    QStringLiteral("] = ftmp; }"));
        } else {
            return (QStringLiteral("{ uint64_t tmp = vm_read64(") + ea +
                    QStringLiteral("); memcpy(&ctx->fpr[") + frd + QStringLiteral("], &tmp, 8); }"));
        }
    }
    if (oneOf(mn, {QStringLiteral("stfsx"), QStringLiteral("stfdx"), QStringLiteral("stfsux"),
                   QStringLiteral("stfdux")})) {
        const auto frs = registerIndex(ops.value(0));
        const auto ra_i = registerIndex(ops.value(1));
        const auto rb_i = registerIndex(ops.value(2));
        auto single = oneOf(mn, {QStringLiteral("stfsx"), QStringLiteral("stfsux")});
        if (mn.endsWith(QStringLiteral("ux")) && (ra_i != QStringLiteral("0"))) {
            auto body =
                (single ? (QStringLiteral("float ftmp = (float)ctx->fpr[") + frs +
                           QStringLiteral("]; uint32_t tmp; memcpy(&tmp, &ftmp, 4); vm_write32(ea, tmp);"))
                        : (QStringLiteral("uint64_t tmp; memcpy(&tmp, &ctx->fpr[") + frs +
                           QStringLiteral("], 8); vm_write64(ea, tmp);")));
            return (QStringLiteral("{ uint64_t ea = ctx->gpr[") + ra_i + QStringLiteral("] + ctx->gpr[") +
                    rb_i + QStringLiteral("]; ") + body + QStringLiteral(" ctx->gpr[") + ra_i +
                    QStringLiteral("] = ea; }"));
        }
        auto ea =
            ((ra_i != QStringLiteral("0")) ? (QStringLiteral("(ctx->gpr[") + ra_i +
                                              QStringLiteral("] + ctx->gpr[") + rb_i + QStringLiteral("])"))
                                           : (QStringLiteral("ctx->gpr[") + rb_i + QStringLiteral("]")));
        if (single) {
            return (QStringLiteral("{ float ftmp = (float)ctx->fpr[") + frs +
                    QStringLiteral("]; uint32_t tmp; memcpy(&tmp, &ftmp, 4); vm_write32(") + ea +
                    QStringLiteral(", tmp); }"));
        } else {
            return (QStringLiteral("{ uint64_t tmp; memcpy(&tmp, &ctx->fpr[") + frs +
                    QStringLiteral("], 8); vm_write64(") + ea + QStringLiteral(", tmp); }"));
        }
    }
    if (mn == QStringLiteral("stfiwx")) {
        const auto frs = registerIndex(ops.value(0));
        const auto ra_i = registerIndex(ops.value(1));
        const auto rb_i = registerIndex(ops.value(2));
        auto ea =
            ((ra_i != QStringLiteral("0")) ? (QStringLiteral("(ctx->gpr[") + ra_i +
                                              QStringLiteral("] + ctx->gpr[") + rb_i + QStringLiteral("])"))
                                           : (QStringLiteral("ctx->gpr[") + rb_i + QStringLiteral("]")));
        return (QStringLiteral("{ uint64_t tmp; memcpy(&tmp, &ctx->fpr[") + frs +
                QStringLiteral("], 8); vm_write32(") + ea + QStringLiteral(", (uint32_t)tmp); }"));
    }
    return {};
}

QString comparison(const PpuInstruction &insn, const QStringList &ops) {
    const auto &mn = insn.mnemonic;
    if (oneOf(mn, {QStringLiteral("cmpwi"), QStringLiteral("cmpdi")})) {
        int bf;
        QString ra_i;
        QString imm;
        if (ops.value(0).startsWith(QStringLiteral("cr"))) {
            bf = ops.value(0).mid(2).toInt();
            ra_i = registerIndex(ops.value(1));
            imm = immediate(ops.value(2));
        } else {
            bf = 0;
            ra_i = registerIndex(ops.value(0));
            imm = immediate(ops.value(1));
        }
        auto cast =
            ((mn == QStringLiteral("cmpwi")) ? QStringLiteral("(int32_t)") : QStringLiteral("(int64_t)"));
        auto shift = ((7 - bf) * 4);
        return (QStringLiteral("{ int64_t a = ") + cast + QStringLiteral("ctx->gpr[") + ra_i +
                QStringLiteral("]; int64_t b = (int64_t)") + imm +
                QStringLiteral("; uint32_t cr_val = ((a < b) ? 8 : (a > b) ? 4 : 2) | ((ctx->xer >> 31) & "
                               "1u); ctx->cr = (ctx->cr & ~(0xFu << ") +
                text(shift) + QStringLiteral(")) | (cr_val << ") + text(shift) + QStringLiteral("); }"));
    }
    if (oneOf(mn, {QStringLiteral("cmplwi"), QStringLiteral("cmpldi")})) {
        int bf;
        QString ra_i;
        QString imm;
        if (ops.value(0).startsWith(QStringLiteral("cr"))) {
            bf = ops.value(0).mid(2).toInt();
            ra_i = registerIndex(ops.value(1));
            imm = immediate(ops.value(2));
        } else {
            bf = 0;
            ra_i = registerIndex(ops.value(0));
            imm = immediate(ops.value(1));
        }
        auto cast =
            ((mn == QStringLiteral("cmplwi")) ? QStringLiteral("(uint32_t)") : QStringLiteral("(uint64_t)"));
        auto shift = ((7 - bf) * 4);
        return (QStringLiteral("{ uint64_t a = ") + cast + QStringLiteral("ctx->gpr[") + ra_i +
                QStringLiteral("]; uint64_t b = (uint64_t)") + imm +
                QStringLiteral("; uint32_t cr_val = ((a < b) ? 8 : (a > b) ? 4 : 2) | ((ctx->xer >> 31) & "
                               "1u); ctx->cr = (ctx->cr & ~(0xFu << ") +
                text(shift) + QStringLiteral(")) | (cr_val << ") + text(shift) + QStringLiteral("); }"));
    }
    if (oneOf(mn, {QStringLiteral("cmpw"), QStringLiteral("cmpd")})) {
        int bf;
        QString ra_i;
        QString rb_i;
        if (ops.value(0).startsWith(QStringLiteral("cr"))) {
            bf = ops.value(0).mid(2).toInt();
            ra_i = registerIndex(ops.value(1));
            rb_i = registerIndex(ops.value(2));
        } else {
            bf = 0;
            ra_i = registerIndex(ops.value(0));
            rb_i = registerIndex(ops.value(1));
        }
        auto cast =
            ((mn == QStringLiteral("cmpw")) ? QStringLiteral("(int32_t)") : QStringLiteral("(int64_t)"));
        auto shift = ((7 - bf) * 4);
        return (QStringLiteral("{ int64_t a = ") + cast + QStringLiteral("ctx->gpr[") + ra_i +
                QStringLiteral("]; int64_t b = ") + cast + QStringLiteral("ctx->gpr[") + rb_i +
                QStringLiteral("]; uint32_t cr_val = ((a < b) ? 8 : (a > b) ? 4 : 2) | ((ctx->xer >> 31) & "
                               "1u); ctx->cr = (ctx->cr & ~(0xFu << ") +
                text(shift) + QStringLiteral(")) | (cr_val << ") + text(shift) + QStringLiteral("); }"));
    }
    if (oneOf(mn, {QStringLiteral("cmplw"), QStringLiteral("cmpld")})) {
        int bf;
        QString ra_i;
        QString rb_i;
        if (ops.value(0).startsWith(QStringLiteral("cr"))) {
            bf = ops.value(0).mid(2).toInt();
            ra_i = registerIndex(ops.value(1));
            rb_i = registerIndex(ops.value(2));
        } else {
            bf = 0;
            ra_i = registerIndex(ops.value(0));
            rb_i = registerIndex(ops.value(1));
        }
        auto cast =
            ((mn == QStringLiteral("cmplw")) ? QStringLiteral("(uint32_t)") : QStringLiteral("(uint64_t)"));
        auto shift = ((7 - bf) * 4);
        return (QStringLiteral("{ uint64_t a = ") + cast + QStringLiteral("ctx->gpr[") + ra_i +
                QStringLiteral("]; uint64_t b = ") + cast + QStringLiteral("ctx->gpr[") + rb_i +
                QStringLiteral("]; uint32_t cr_val = ((a < b) ? 8 : (a > b) ? 4 : 2) | ((ctx->xer >> 31) & "
                               "1u); ctx->cr = (ctx->cr & ~(0xFu << ") +
                text(shift) + QStringLiteral(")) | (cr_val << ") + text(shift) + QStringLiteral("); }"));
    }
    return {};
}

QString indexedAddress(const QString &ra, const QString &rb) {
    if (ra == "0")
        return QStringLiteral("ctx->gpr[%1]").arg(rb);
    return QStringLiteral("(ctx->gpr[%1] + ctx->gpr[%2])").arg(ra, rb);
}
QString stripSuffix(QString value, const QString &suffix) {
    while (value.endsWith(suffix))
        value.chop(suffix.size());
    return value;
}

QString specialRegisters(const PpuInstruction &insn, const QStringList &ops) {
    const auto &mn = insn.mnemonic;
    if (mn == QStringLiteral("mflr")) {
        auto rd_i = registerIndex(ops.value(0));
        return (QStringLiteral("ctx->gpr[") + text(rd_i) + QStringLiteral("] = ctx->lr;"));
    }
    if (mn == QStringLiteral("mtlr")) {
        auto rs_i = registerIndex(ops.value(0));
        return (QStringLiteral("ctx->lr = ctx->gpr[") + text(rs_i) + QStringLiteral("];"));
    }
    if (mn == QStringLiteral("mfctr")) {
        auto rd_i = registerIndex(ops.value(0));
        return (QStringLiteral("ctx->gpr[") + text(rd_i) + QStringLiteral("] = ctx->ctr;"));
    }
    if (mn == QStringLiteral("mtctr")) {
        auto rs_i = registerIndex(ops.value(0));
        return (QStringLiteral("ctx->ctr = ctx->gpr[") + text(rs_i) + QStringLiteral("];"));
    }
    if (oneOf(mn, {QStringLiteral("mfcr"), QStringLiteral("mfocrf")})) {
        auto rd_i = registerIndex(ops.value(0));
        auto raw = insn.raw;
        if (((raw >> 20) & 1)) {
            auto fxm = ((raw >> 12) & 0xFF);
            quint32 mask = 0;
            for (int f = 0; f < 8; ++f) {
                if (fxm & (0x80 >> f)) {
                    mask |= (0xFu << (28 - (4 * f)));
                }
            }
            return (QStringLiteral("ctx->gpr[") + text(rd_i) + QStringLiteral("] = ctx->cr & 0x") +
                    hex(mask, 8) + QStringLiteral("u;"));
        }
        return (QStringLiteral("ctx->gpr[") + text(rd_i) + QStringLiteral("] = ctx->cr;"));
    }
    if (oneOf(mn, {QStringLiteral("mfspr"), QStringLiteral("mtspr")})) {
        auto spr_op = ((mn == QStringLiteral("mfspr")) ? ops.value(1) : ops.value(0));
        auto gpr_op = ((mn == QStringLiteral("mfspr")) ? ops.value(0) : ops.value(1));
        auto s = text(spr_op).trimmed().toUpper();
        auto spr_field =
            QHash<QString, QString>{
                {QStringLiteral("XER"), QStringLiteral("xer")}, {QStringLiteral("1"), QStringLiteral("xer")},
                {QStringLiteral("LR"), QStringLiteral("lr")},   {QStringLiteral("8"), QStringLiteral("lr")},
                {QStringLiteral("CTR"), QStringLiteral("ctr")}, {QStringLiteral("9"), QStringLiteral("ctr")}}
                .value(s);
        auto g = registerIndex(gpr_op);
        if (oneOf(s, {QStringLiteral("VRSAVE"), QStringLiteral("256")})) {
            if (mn == QStringLiteral("mfspr")) {
                return (QStringLiteral("ctx->gpr[") + text(g) + QStringLiteral("] = ctx->vrsave;"));
            }
            return (QStringLiteral("ctx->vrsave = (uint32_t)ctx->gpr[") + text(g) + QStringLiteral("];"));
        }
        if (spr_field.isNull()) {
            return (QStringLiteral("/* ") + text(mn) + QStringLiteral(" ") + text(insn.operands) +
                    QStringLiteral(": unsupported SPR -- no-op */;"));
        }
        if (mn == QStringLiteral("mfspr")) {
            return (QStringLiteral("ctx->gpr[") + text(g) + QStringLiteral("] = ctx->") + text(spr_field) +
                    QStringLiteral(";"));
        }
        if (spr_field == QStringLiteral("xer")) {
            return (QStringLiteral("ctx->xer = (uint32_t)ctx->gpr[") + text(g) +
                    QStringLiteral("] & 0xE000007Fu;"));
        }
        return (QStringLiteral("ctx->") + text(spr_field) + QStringLiteral(" = ctx->gpr[") + text(g) +
                QStringLiteral("];"));
    }
    if (mn == QStringLiteral("mtvscr")) {
        auto vb = ops.value(0).mid(1).toInt();
        return (QStringLiteral("{ const uint8_t* _b = (const uint8_t*)&ctx->vr[") + text(vb) +
                QStringLiteral("]; ctx->vscr = ((uint32_t)_b[12] << 24 | (uint32_t)_b[13] << 16 | "
                               "(uint32_t)_b[14] << 8 | _b[15]) & 0x00010001u; }"));
    }
    if (mn == QStringLiteral("mfvscr")) {
        auto vd = ops.value(0).mid(1).toInt();
        return (QStringLiteral("{ uint8_t* _d = (uint8_t*)&ctx->vr[") + text(vd) +
                QStringLiteral("]; memset(_d, 0, 12); _d[12] = 0; _d[13] = (uint8_t)(ctx->vscr >> 16); "
                               "_d[14] = 0; _d[15] = (uint8_t)ctx->vscr; }"));
    }
    if (mn == QStringLiteral("mtcr")) {
        return (QStringLiteral("ctx->cr = (uint32_t)ctx->gpr[") +
                text(registerIndex(ops.value(ops.size() - 1))) + QStringLiteral("];"));
    }
    if (mn == QStringLiteral("mtcrf")) {
        auto crm = ((ops.size() > 1) ? ops.value(0).toInt(nullptr, 0) : 0xFF);
        quint32 mask = 0;
        for (int f = 0; f < 8; ++f) {
            if (crm & (0x80 >> f)) {
                mask |= (0xFu << (28 - (4 * f)));
            }
        }
        auto rS = registerIndex(ops.value(ops.size() - 1));
        if (mask == 0xFFFFFFFFu) {
            return (QStringLiteral("ctx->cr = (uint32_t)ctx->gpr[") + text(rS) + QStringLiteral("];"));
        }
        return (QStringLiteral("ctx->cr = (ctx->cr & 0x") + hex(((~mask) & 0xFFFFFFFFu), 8) +
                QStringLiteral("u) | ((uint32_t)ctx->gpr[") + text(rS) + QStringLiteral("] & 0x") +
                hex(mask, 8) + QStringLiteral("u);"));
    }
    if (mn == QStringLiteral("sc")) {
        return QStringLiteral("lv2_syscall(ctx);");
    }
    return {};
}

QString floatingPoint(const PpuInstruction &insn, const QStringList &ops) {
    const auto &mn = insn.mnemonic;
    if (oneOf(mn, {QStringLiteral("lfs"), QStringLiteral("lfsu"), QStringLiteral("lfd"),
                   QStringLiteral("lfdu")})) {
        auto frd = registerIndex(ops.value(0));
        const auto [disp, base] = displacementBase(ops.value(1));
        if (!disp.isNull()) {
            QString line;
            if (oneOf(mn, {QStringLiteral("lfs"), QStringLiteral("lfsu")})) {
                line = (QStringLiteral("{ uint32_t tmp = vm_read32(ctx->gpr[") + text(base) +
                        QStringLiteral("] + ") + text(disp) +
                        QStringLiteral("); float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[") + text(frd) +
                        QStringLiteral("] = ftmp; }"));
            } else {
                line = (QStringLiteral("{ uint64_t tmp = vm_read64(ctx->gpr[") + text(base) +
                        QStringLiteral("] + ") + text(disp) + QStringLiteral("); memcpy(&ctx->fpr[") +
                        text(frd) + QStringLiteral("], &tmp, 8); }"));
            }
            if (mn.endsWith(QStringLiteral("u"))) {
                line += (QStringLiteral(" ctx->gpr[") + text(base) + QStringLiteral("] += ") + text(disp) +
                         QStringLiteral(";"));
            }
            return line;
        }
    }
    if (oneOf(mn, {QStringLiteral("stfs"), QStringLiteral("stfsu"), QStringLiteral("stfd"),
                   QStringLiteral("stfdu")})) {
        auto frs = registerIndex(ops.value(0));
        const auto [disp, base] = displacementBase(ops.value(1));
        if (!disp.isNull()) {
            QString line;
            if (oneOf(mn, {QStringLiteral("stfs"), QStringLiteral("stfsu")})) {
                line = (QStringLiteral("{ float ftmp = (float)ctx->fpr[") + text(frs) +
                        QStringLiteral("]; uint32_t tmp; memcpy(&tmp, &ftmp, 4); vm_write32(ctx->gpr[") +
                        text(base) + QStringLiteral("] + ") + text(disp) + QStringLiteral(", tmp); }"));
            } else {
                line = (QStringLiteral("{ uint64_t tmp; memcpy(&tmp, &ctx->fpr[") + text(frs) +
                        QStringLiteral("], 8); vm_write64(ctx->gpr[") + text(base) + QStringLiteral("] + ") +
                        text(disp) + QStringLiteral(", tmp); }"));
            }
            if (mn.endsWith(QStringLiteral("u"))) {
                line += (QStringLiteral(" ctx->gpr[") + text(base) + QStringLiteral("] += ") + text(disp) +
                         QStringLiteral(";"));
            }
            return line;
        }
    }
    static const QHash<QString, QString> fp_binary{{QStringLiteral("fadd"), QStringLiteral("ppu_fadd")},
                                                   {QStringLiteral("fadds"), QStringLiteral("ppu_fadd")},
                                                   {QStringLiteral("fsub"), QStringLiteral("ppu_fsub")},
                                                   {QStringLiteral("fsubs"), QStringLiteral("ppu_fsub")},
                                                   {QStringLiteral("fmul"), QStringLiteral("ppu_fmul")},
                                                   {QStringLiteral("fmuls"), QStringLiteral("ppu_fmul")},
                                                   {QStringLiteral("fdiv"), QStringLiteral("ppu_fdiv")},
                                                   {QStringLiteral("fdivs"), QStringLiteral("ppu_fdiv")}};
    auto mn_base = stripSuffix(mn, QStringLiteral("."));
    if (fp_binary.contains(mn_base)) {
        auto frd = registerIndex(ops.value(0));
        auto fra = registerIndex(ops.value(1));
        auto frb = registerIndex(ops.value(2));
        auto helper = fp_binary[mn_base];
        auto expr = (text(helper) + QStringLiteral("(ctx->fpr[") + text(fra) +
                     QStringLiteral("], ctx->fpr[") + text(frb) + QStringLiteral("])"));
        if (mn_base.endsWith(QStringLiteral("s"))) {
            expr = (QStringLiteral("ppu_fp_single(") + text(expr) + QStringLiteral(")"));
        }
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = ") + text(expr) +
                QStringLiteral(";"));
    }
    if (oneOf(mn_base, {QStringLiteral("fmr")})) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = ctx->fpr[") + text(frb) +
                QStringLiteral("];"));
    }
    if (oneOf(mn_base, {QStringLiteral("fneg")})) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = -ctx->fpr[") + text(frb) +
                QStringLiteral("];"));
    }
    if (oneOf(mn_base, {QStringLiteral("fabs")})) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = fabs(ctx->fpr[") + text(frb) +
                QStringLiteral("]);"));
    }
    if (oneOf(mn_base, {QStringLiteral("fnabs")})) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = -fabs(ctx->fpr[") + text(frb) +
                QStringLiteral("]);"));
    }
    static const QHash<QString, QPair<int, int>> fma_map{
        {QStringLiteral("fmadd"), {0, 0}},  {QStringLiteral("fmadds"), {0, 0}},
        {QStringLiteral("fmsub"), {1, 0}},  {QStringLiteral("fmsubs"), {1, 0}},
        {QStringLiteral("fnmadd"), {0, 1}}, {QStringLiteral("fnmadds"), {0, 1}},
        {QStringLiteral("fnmsub"), {1, 1}}, {QStringLiteral("fnmsubs"), {1, 1}}};
    if (fma_map.contains(mn_base)) {
        const auto frd = registerIndex(ops.value(0));
        const auto fra = registerIndex(ops.value(1));
        const auto frc = registerIndex(ops.value(2));
        const auto frb = registerIndex(ops.value(3));
        const auto [neg_b, neg_res] = fma_map[mn_base];
        auto expr = (QStringLiteral("ppu_fmadd_core(ctx->fpr[") + text(fra) + QStringLiteral("], ctx->fpr[") +
                     text(frc) + QStringLiteral("], ctx->fpr[") + text(frb) + QStringLiteral("], ") +
                     text(neg_b) + QStringLiteral(", ") + text(neg_res) + QStringLiteral(")"));
        if (mn_base.endsWith(QStringLiteral("s"))) {
            expr = (QStringLiteral("ppu_fp_single(") + text(expr) + QStringLiteral(")"));
        }
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = ") + text(expr) +
                QStringLiteral(";"));
    }
    if (oneOf(mn_base, {QStringLiteral("frsp")})) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = ppu_frsp(ctx->fpr[") +
                text(frb) + QStringLiteral("]);"));
    }
    if (oneOf(mn_base, {QStringLiteral("fctiw"), QStringLiteral("fctiwz")})) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        auto rn = (mn_base.endsWith(QStringLiteral("z")) ? 0 : 1);
        return (QStringLiteral("{ int64_t iv = (int32_t)ppu_f2i32(ctx->fpr[") + text(frb) +
                QStringLiteral("], ") + text(rn) + QStringLiteral("); memcpy(&ctx->fpr[") + text(frd) +
                QStringLiteral("], &iv, 8); }"));
    }
    if (oneOf(mn_base, {QStringLiteral("fctid"), QStringLiteral("fctidz")})) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        auto rn = (mn_base.endsWith(QStringLiteral("z")) ? 0 : 1);
        return (QStringLiteral("{ uint64_t iv = ppu_f2i64(ctx->fpr[") + text(frb) + QStringLiteral("], ") +
                text(rn) + QStringLiteral("); memcpy(&ctx->fpr[") + text(frd) +
                QStringLiteral("], &iv, 8); }"));
    }
    if (oneOf(mn_base, {QStringLiteral("fcfid")})) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        return (QStringLiteral("{ int64_t iv; memcpy(&iv, &ctx->fpr[") + text(frb) +
                QStringLiteral("], 8); ctx->fpr[") + text(frd) + QStringLiteral("] = (double)iv; }"));
    }
    if ((mn_base == QStringLiteral("fsqrt")) || (mn_base == QStringLiteral("fsqrts"))) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        if (mn_base.endsWith(QStringLiteral("s"))) {
            return (QStringLiteral("ctx->fpr[") + text(frd) +
                    QStringLiteral("] = (double)(float)sqrt(ctx->fpr[") + text(frb) + QStringLiteral("]);"));
        }
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = sqrt(ctx->fpr[") + text(frb) +
                QStringLiteral("]);"));
    }
    if (oneOf(mn_base, {QStringLiteral("frsqrte"), QStringLiteral("frsqrtes")})) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        if (mn_base == QStringLiteral("frsqrtes")) {
            return (QStringLiteral("ctx->fpr[") + text(frd) +
                    QStringLiteral("] = (double)(float)(1.0 / sqrt(ctx->fpr[") + text(frb) +
                    QStringLiteral("]));"));
        }
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = 1.0 / sqrt(ctx->fpr[") +
                text(frb) + QStringLiteral("]);"));
    }
    if (oneOf(mn_base, {QStringLiteral("fre"), QStringLiteral("fres")})) {
        auto frd = registerIndex(ops.value(0));
        auto frb = registerIndex(ops.value(1));
        if (mn_base == QStringLiteral("fres")) {
            return (QStringLiteral("ctx->fpr[") + text(frd) +
                    QStringLiteral("] = (double)(float)(1.0 / ctx->fpr[") + text(frb) +
                    QStringLiteral("]);"));
        }
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = 1.0 / ctx->fpr[") + text(frb) +
                QStringLiteral("];"));
    }
    if (mn_base == QStringLiteral("fsel")) {
        const auto frd = registerIndex(ops.value(0));
        const auto fra = registerIndex(ops.value(1));
        const auto frc = registerIndex(ops.value(2));
        const auto frb = registerIndex(ops.value(3));
        return (QStringLiteral("ctx->fpr[") + text(frd) + QStringLiteral("] = (ctx->fpr[") + text(fra) +
                QStringLiteral("] >= 0.0) ? ctx->fpr[") + text(frc) + QStringLiteral("] : ctx->fpr[") +
                text(frb) + QStringLiteral("];"));
    }
    if ((mn_base == QStringLiteral("fcmpu")) || (mn_base == QStringLiteral("fcmpo"))) {
        int bf;
        QString fra, frb;
        if (ops.value(0).startsWith(QStringLiteral("cr"))) {
            bf = ops.value(0).mid(2).toInt();
            fra = registerIndex(ops.value(1));
            frb = registerIndex(ops.value(2));
        } else {
            bf = 0;
            fra = registerIndex(ops.value(0));
            frb = registerIndex(ops.value(1));
        }
        auto shift = ((7 - bf) * 4);
        return (QStringLiteral("{ double a = ctx->fpr[") + text(fra) +
                QStringLiteral("]; double b = ctx->fpr[") + text(frb) +
                QStringLiteral("]; uint32_t cr_val = (a != a || b != b) ? 1 : (a < b) ? 8 : (a > b) ? 4 : 2; "
                               "ctx->cr = (ctx->cr & ~(0xFu << ") +
                text(shift) + QStringLiteral(")) | (cr_val << ") + text(shift) + QStringLiteral("); }"));
    }
    return {};
}

QString extendedArithmetic(const PpuInstruction &insn, const QStringList &ops) {
    const auto &mn = insn.mnemonic;
    if (mn == QStringLiteral("mulld")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + text(rd) + QStringLiteral("] = ctx->gpr[") + text(ra) +
                QStringLiteral("] * ctx->gpr[") + text(rb) + QStringLiteral("];"));
    }
    if (mn == QStringLiteral("mullw")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + text(rd) +
                QStringLiteral("] = (int64_t)(int32_t)((int32_t)ctx->gpr[") + text(ra) +
                QStringLiteral("] * (int32_t)ctx->gpr[") + text(rb) + QStringLiteral("]);"));
    }
    if (mn == QStringLiteral("divd")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ int64_t _a=(int64_t)ctx->gpr[") + text(ra) +
                QStringLiteral("], _b=(int64_t)ctx->gpr[") + text(rb) + QStringLiteral("]; ctx->gpr[") +
                text(rd) +
                QStringLiteral("] = (_b==0||(_a==(int64_t)0x8000000000000000LL&&_b==-1))?0:_a/_b; }"));
    }
    if (mn == QStringLiteral("divdu")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + text(rd) + QStringLiteral("] = (ctx->gpr[") + text(rb) +
                QStringLiteral("] != 0) ? ctx->gpr[") + text(ra) + QStringLiteral("] / ctx->gpr[") +
                text(rb) + QStringLiteral("] : 0;"));
    }
    if (mn.startsWith(QStringLiteral("adde"))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ca = (ctx->xer >> 29) & 1; uint64_t result = ctx->gpr[") +
                text(ra) + QStringLiteral("] + ctx->gpr[") + text(rb) +
                QStringLiteral("] + ca; ctx->xer = (ctx->xer & ~(1u << 29)) | ((result < ctx->gpr[") +
                text(ra) + QStringLiteral("] || (ca && result == ctx->gpr[") + text(ra) +
                QStringLiteral("])) ? (1u << 29) : 0); ctx->gpr[") + text(rd) +
                QStringLiteral("] = result; }"));
    }
    if (oneOf(mn, {QStringLiteral("addze"), QStringLiteral("addze.")})) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        return (
            QStringLiteral("{ uint64_t ca = (ctx->xer >> 29) & 1; uint64_t result = ctx->gpr[") + text(ra) +
            QStringLiteral("] + ca; ctx->xer = (ctx->xer & ~(1u << 29)) | ((result < ctx->gpr[") + text(ra) +
            QStringLiteral("]) ? (1u << 29) : 0); ctx->gpr[") + text(rd) + QStringLiteral("] = result; }"));
    }
    if (mn.startsWith(QStringLiteral("subfe"))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ca = (ctx->xer >> 29) & 1; uint64_t a = ~ctx->gpr[") + text(ra) +
                QStringLiteral("], b = ctx->gpr[") + text(rb) +
                QStringLiteral(
                    "]; uint64_t s1 = a + b; uint64_t co = (s1 < a); uint64_t result = s1 + ca; co |= "
                    "(result < s1); ctx->xer = (ctx->xer & ~(1u << 29)) | ((uint32_t)co << 29); ctx->gpr[") +
                text(rd) + QStringLiteral("] = result; }"));
    }
    if (mn.startsWith(QStringLiteral("addc"))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t result = ctx->gpr[") + text(ra) + QStringLiteral("] + ctx->gpr[") +
                text(rb) + QStringLiteral("]; ctx->xer = (ctx->xer & ~(1u << 29)) | ((result < ctx->gpr[") +
                text(ra) + QStringLiteral("]) ? (1u << 29) : 0); ctx->gpr[") + text(rd) +
                QStringLiteral("] = result; }"));
    }
    if (mn.startsWith(QStringLiteral("subfc"))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (
            QStringLiteral("{ uint64_t result = ~ctx->gpr[") + text(ra) + QStringLiteral("] + ctx->gpr[") +
            text(rb) + QStringLiteral("] + 1; ctx->xer = (ctx->xer & ~(1u << 29)) | ((ctx->gpr[") + text(rb) +
            QStringLiteral("] >= ctx->gpr[") + text(ra) + QStringLiteral("]) ? (1u << 29) : 0); ctx->gpr[") +
            text(rd) + QStringLiteral("] = result; }"));
    }
    return {};
}

QString miscellaneous(const PpuInstruction &insn, const QStringList &ops) {
    const auto &mn = insn.mnemonic;
    if (mn == QStringLiteral("cror")) {
        const auto bt = ops.value(0).toInt();
        const auto ba = ops.value(1).toInt();
        const auto bb = ops.value(2).toInt();
        return (QStringLiteral("{ uint32_t a = (ctx->cr >> (31 - ") + text(ba) +
                QStringLiteral(")) & 1; uint32_t b = (ctx->cr >> (31 - ") + text(bb) +
                QStringLiteral(")) & 1; ctx->cr = (ctx->cr & ~(1u << (31 - ") + text(bt) +
                QStringLiteral("))) | ((a | b) << (31 - ") + text(bt) + QStringLiteral(")); }"));
    }
    if (mn == QStringLiteral("crand")) {
        const auto bt = ops.value(0).toInt();
        const auto ba = ops.value(1).toInt();
        const auto bb = ops.value(2).toInt();
        return (QStringLiteral("{ uint32_t a = (ctx->cr >> (31 - ") + text(ba) +
                QStringLiteral(")) & 1; uint32_t b = (ctx->cr >> (31 - ") + text(bb) +
                QStringLiteral(")) & 1; ctx->cr = (ctx->cr & ~(1u << (31 - ") + text(bt) +
                QStringLiteral("))) | ((a & b) << (31 - ") + text(bt) + QStringLiteral(")); }"));
    }
    if (mn == QStringLiteral("crnand")) {
        const auto bt = ops.value(0).toInt();
        const auto ba = ops.value(1).toInt();
        const auto bb = ops.value(2).toInt();
        return (QStringLiteral("{ uint32_t a = (ctx->cr >> (31 - ") + text(ba) +
                QStringLiteral(")) & 1; uint32_t b = (ctx->cr >> (31 - ") + text(bb) +
                QStringLiteral(")) & 1; ctx->cr = (ctx->cr & ~(1u << (31 - ") + text(bt) +
                QStringLiteral("))) | ((~(a & b) & 1) << (31 - ") + text(bt) + QStringLiteral(")); }"));
    }
    if (mn == QStringLiteral("crxor")) {
        const auto bt = ops.value(0).toInt();
        const auto ba = ops.value(1).toInt();
        const auto bb = ops.value(2).toInt();
        return (QStringLiteral("{ uint32_t a = (ctx->cr >> (31 - ") + text(ba) +
                QStringLiteral(")) & 1; uint32_t b = (ctx->cr >> (31 - ") + text(bb) +
                QStringLiteral(")) & 1; ctx->cr = (ctx->cr & ~(1u << (31 - ") + text(bt) +
                QStringLiteral("))) | ((a ^ b) << (31 - ") + text(bt) + QStringLiteral(")); }"));
    }
    if (mn == QStringLiteral("crnor")) {
        const auto bt = ops.value(0).toInt();
        const auto ba = ops.value(1).toInt();
        const auto bb = ops.value(2).toInt();
        return (QStringLiteral("{ uint32_t a = (ctx->cr >> (31 - ") + text(ba) +
                QStringLiteral(")) & 1; uint32_t b = (ctx->cr >> (31 - ") + text(bb) +
                QStringLiteral(")) & 1; ctx->cr = (ctx->cr & ~(1u << (31 - ") + text(bt) +
                QStringLiteral("))) | ((~(a | b) & 1) << (31 - ") + text(bt) + QStringLiteral(")); }"));
    }
    if (mn == QStringLiteral("creqv")) {
        const auto bt = ops.value(0).toInt();
        const auto ba = ops.value(1).toInt();
        const auto bb = ops.value(2).toInt();
        return (QStringLiteral("{ uint32_t a = (ctx->cr >> (31 - ") + text(ba) +
                QStringLiteral(")) & 1; uint32_t b = (ctx->cr >> (31 - ") + text(bb) +
                QStringLiteral(")) & 1; ctx->cr = (ctx->cr & ~(1u << (31 - ") + text(bt) +
                QStringLiteral("))) | ((~(a ^ b) & 1) << (31 - ") + text(bt) + QStringLiteral(")); }"));
    }
    if (mn == QStringLiteral("crorc")) {
        const auto bt = ops.value(0).toInt();
        const auto ba = ops.value(1).toInt();
        const auto bb = ops.value(2).toInt();
        return (QStringLiteral("{ uint32_t a = (ctx->cr >> (31 - ") + text(ba) +
                QStringLiteral(")) & 1; uint32_t b = (ctx->cr >> (31 - ") + text(bb) +
                QStringLiteral(")) & 1; ctx->cr = (ctx->cr & ~(1u << (31 - ") + text(bt) +
                QStringLiteral("))) | ((a | (~b & 1)) << (31 - ") + text(bt) + QStringLiteral(")); }"));
    }
    if (mn == QStringLiteral("crandc")) {
        const auto bt = ops.value(0).toInt();
        const auto ba = ops.value(1).toInt();
        const auto bb = ops.value(2).toInt();
        return (QStringLiteral("{ uint32_t a = (ctx->cr >> (31 - ") + text(ba) +
                QStringLiteral(")) & 1; uint32_t b = (ctx->cr >> (31 - ") + text(bb) +
                QStringLiteral(")) & 1; ctx->cr = (ctx->cr & ~(1u << (31 - ") + text(bt) +
                QStringLiteral("))) | ((a & (~b & 1)) << (31 - ") + text(bt) + QStringLiteral(")); }"));
    }
    if (mn == QStringLiteral("mcrf")) {
        auto bf = (ops.value(0).startsWith(QStringLiteral("cr")) ? ops.value(0).mid(2).toInt()
                                                                 : ops.value(0).toInt());
        auto bfa = (ops.value(1).startsWith(QStringLiteral("cr")) ? ops.value(1).mid(2).toInt()
                                                                  : ops.value(1).toInt());
        auto dst_shift = ((7 - bf) * 4);
        auto src_shift = ((7 - bfa) * 4);
        return (QStringLiteral("{ uint32_t field = (ctx->cr >> ") + text(src_shift) +
                QStringLiteral(") & 0xF; ctx->cr = (ctx->cr & ~(0xFu << ") + text(dst_shift) +
                QStringLiteral(")) | (field << ") + text(dst_shift) + QStringLiteral("); }"));
    }
    if (mn.startsWith(QStringLiteral("rlwnm"))) {
        auto ra = registerIndex(ops.value(0));
        auto rs = registerIndex(ops.value(1));
        auto rb = registerIndex(ops.value(2));
        auto mb = ops.value(3).toInt();
        auto me = ops.value(4).toInt();
        return (QStringLiteral("ctx->gpr[") + text(ra) + QStringLiteral("] = ppc_rlw64(ctx->gpr[") +
                text(rs) + QStringLiteral("], (int)(ctx->gpr[") + text(rb) + QStringLiteral("] & 31), ") +
                text(mb) + QStringLiteral(", ") + text(me) + QStringLiteral(", 0, 0);"));
    }
    if (stripSuffix(mn, QStringLiteral(".")) == QStringLiteral("mffs")) {
        auto frd = registerIndex(ops.value(0));
        return (QStringLiteral("{ uint64_t fpscr64 = ctx->fpscr; memcpy(&ctx->fpr[") + text(frd) +
                QStringLiteral("], &fpscr64, 8); }"));
    }
    auto raw = insn.raw;
    if (oneOf(stripSuffix(mn, QStringLiteral(".")), {QStringLiteral("mtfsb0"), QStringLiteral("mtfsb1")})) {
        return (QStringLiteral("ppu_mtfsb(ctx, ") + text(((raw >> 21) & 31)) + QStringLiteral(", ") +
                text((mn.startsWith(QStringLiteral("mtfsb1")) ? 1 : 0)) + QStringLiteral(");"));
    }
    if ((stripSuffix(mn, QStringLiteral(".")) == QStringLiteral("mtfsfi"))) {
        const auto bf = ((raw >> 23) & 7);
        const auto u = ((raw >> 12) & 0xF);
        return (QStringLiteral("ppu_mtfsf(ctx, 0x") + hex((0x80 >> bf), 2) + QStringLiteral("u, 0x") +
                hex(((quint32(u) << (28 - (4 * bf))) & 0xFFFFFFFFu), 8) + QStringLiteral("u);"));
    }
    if ((stripSuffix(mn, QStringLiteral(".")) == QStringLiteral("mtfsf"))) {
        const auto flm = ((raw >> 17) & 0xFF);
        const auto frb = ((raw >> 11) & 31);
        return (QStringLiteral("{ uint64_t _v; memcpy(&_v, &ctx->fpr[") + text(frb) +
                QStringLiteral("], 8); ppu_mtfsf(ctx, 0x") + hex(flm, 2) +
                QStringLiteral("u, (uint32_t)_v); }"));
    }
    if ((mn == QStringLiteral("mcrfs"))) {
        const auto bf = ((raw >> 23) & 7);
        const auto bfa = ((raw >> 18) & 7);
        auto sh = (28 - (4 * bf));
        return (QStringLiteral("{ uint32_t _f = ppu_mcrfs(ctx, ") + text(bfa) +
                QStringLiteral("); ctx->cr = (ctx->cr & ~(0xFu << ") + text(sh) +
                QStringLiteral(")) | (_f << ") + text(sh) + QStringLiteral("); }"));
    }
    if (mn == QStringLiteral("stdux")) {
        const auto rs = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ctx->gpr[") + text(ra) + QStringLiteral("] + ctx->gpr[") +
                text(rb) + QStringLiteral("]; vm_write64(ea, ctx->gpr[") + text(rs) +
                QStringLiteral("]); ctx->gpr[") + text(ra) + QStringLiteral("] = ea; }"));
    }
    if (mn == QStringLiteral("stwux")) {
        const auto rs = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ctx->gpr[") + text(ra) + QStringLiteral("] + ctx->gpr[") +
                text(rb) + QStringLiteral("]; vm_write32(ea, (uint32_t)ctx->gpr[") + text(rs) +
                QStringLiteral("]); ctx->gpr[") + text(ra) + QStringLiteral("] = ea; }"));
    }
    if (mn == QStringLiteral("ldux")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ctx->gpr[") + text(ra) + QStringLiteral("] + ctx->gpr[") +
                text(rb) + QStringLiteral("]; ctx->gpr[") + text(rd) +
                QStringLiteral("] = vm_read64(ea); ctx->gpr[") + text(ra) + QStringLiteral("] = ea; }"));
    }
    if (mn == QStringLiteral("lwzux")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ctx->gpr[") + text(ra) + QStringLiteral("] + ctx->gpr[") +
                text(rb) + QStringLiteral("]; ctx->gpr[") + text(rd) +
                QStringLiteral("] = vm_read32(ea); ctx->gpr[") + text(ra) + QStringLiteral("] = ea; }"));
    }
    if (mn == QStringLiteral("lwarx")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        auto ea =
            ((ra != QStringLiteral("0")) ? (QStringLiteral("ctx->gpr[") + text(ra) +
                                            QStringLiteral("] + ctx->gpr[") + text(rb) + QStringLiteral("]"))
                                         : (QStringLiteral("ctx->gpr[") + text(rb) + QStringLiteral("]")));
        return (QStringLiteral("{ uint64_t ea = ") + text(ea) + QStringLiteral("; ctx->gpr[") + text(rd) +
                QStringLiteral(
                    "] = vm_read32(ea); ctx->reserve_addr = (uint32_t)ea; ctx->reserve_value = ctx->gpr[") +
                text(rd) + QStringLiteral("]; }"));
    }
    if ((mn == QStringLiteral("stwcx")) || (mn == QStringLiteral("stwcx."))) {
        const auto rs = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        auto ea =
            ((ra != QStringLiteral("0")) ? (QStringLiteral("ctx->gpr[") + text(ra) +
                                            QStringLiteral("] + ctx->gpr[") + text(rb) + QStringLiteral("]"))
                                         : (QStringLiteral("ctx->gpr[") + text(rb) + QStringLiteral("]")));
        return (QStringLiteral("{ uint64_t ea = ") + text(ea) +
                QStringLiteral("; int _sc = (ctx->reserve_addr == (uint32_t)ea) && ppu_stwcx32(ea, "
                               "(uint32_t)ctx->reserve_value, (uint32_t)ctx->gpr[") +
                text(rs) +
                QStringLiteral("]); ctx->cr = (ctx->cr & ~(0xFu << 28)) | (_sc ? (2u << 28) : 0u) | "
                               "((ctx->xer >> 31) << 28); ctx->reserve_addr = 0; }"));
    }
    if (mn == QStringLiteral("ldarx")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        auto ea =
            ((ra != QStringLiteral("0")) ? (QStringLiteral("ctx->gpr[") + text(ra) +
                                            QStringLiteral("] + ctx->gpr[") + text(rb) + QStringLiteral("]"))
                                         : (QStringLiteral("ctx->gpr[") + text(rb) + QStringLiteral("]")));
        return (QStringLiteral("{ uint64_t ea = ") + text(ea) + QStringLiteral("; ctx->gpr[") + text(rd) +
                QStringLiteral(
                    "] = vm_read64(ea); ctx->reserve_addr = (uint32_t)ea; ctx->reserve_value = ctx->gpr[") +
                text(rd) + QStringLiteral("]; }"));
    }
    if ((mn == QStringLiteral("stdcx")) || (mn == QStringLiteral("stdcx."))) {
        const auto rs = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        auto ea =
            ((ra != QStringLiteral("0")) ? (QStringLiteral("ctx->gpr[") + text(ra) +
                                            QStringLiteral("] + ctx->gpr[") + text(rb) + QStringLiteral("]"))
                                         : (QStringLiteral("ctx->gpr[") + text(rb) + QStringLiteral("]")));
        return (QStringLiteral("{ uint64_t ea = ") + text(ea) +
                QStringLiteral("; int _sc = (ctx->reserve_addr == (uint32_t)ea) && ppu_stdcx64(ea, "
                               "ctx->reserve_value, ctx->gpr[") +
                text(rs) +
                QStringLiteral("]); ctx->cr = (ctx->cr & ~(0xFu << 28)) | (_sc ? (2u << 28) : 0u) | "
                               "((ctx->xer >> 31) << 28); ctx->reserve_addr = 0; }"));
    }
    if (oneOf(mn, {QStringLiteral("dst"), QStringLiteral("dstst"), QStringLiteral("dss")})) {
        return (QStringLiteral("/* ") + text(mn) + QStringLiteral(": data-stream hint */;"));
    }
    if (oneOf(mn, {QStringLiteral("stvlx"), QStringLiteral("stvlxl"), QStringLiteral("stvrx"),
                   QStringLiteral("stvrxl")})) {
        auto vs = (ops.value(0).startsWith(QStringLiteral("v")) ? QString::number(ops.value(0).mid(1).toInt())
                                                                : registerIndex(ops.value(0)));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        QString loop;
        if (mn.startsWith(QStringLiteral("stvl"))) {
            loop = QStringLiteral("for (uint32_t i = 0; i < 16u - sh; i++) m[sh + i] = s[i];");
        } else {
            loop = QStringLiteral("for (uint32_t i = 16u - sh; i < 16u; i++) m[i - (16u - sh)] = s[i];");
        }
        return (QStringLiteral("{ uint64_t ea = ") + text(indexedAddress(ra, rb)) +
                QStringLiteral("; uint32_t sh = (uint32_t)(ea & 0xF); uint8_t* m = vm_base + (uint32_t)(ea & "
                               "~0xFULL); const uint8_t* s = (const uint8_t*)&ctx->vr[") +
                text(vs) + QStringLiteral("]; ") + text(loop) + QStringLiteral(" }"));
    }
    if (mn == QStringLiteral("lwaux")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ctx->gpr[") + text(ra) + QStringLiteral("] + ctx->gpr[") +
                text(rb) + QStringLiteral("]; ctx->gpr[") + text(rd) +
                QStringLiteral("] = (int64_t)(int32_t)vm_read32(ea); ctx->gpr[") + text(ra) +
                QStringLiteral("] = ea; }"));
    }
    if (mn == QStringLiteral("stdbrx")) {
        const auto rs = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ") + text(indexedAddress(ra, rb)) +
                QStringLiteral("; uint64_t raw = ctx->gpr[") + text(rs) +
                QStringLiteral("]; VM_RAW_ST(uint64_t, ea, raw); }"));
    }
    if (oneOf(mn, {QStringLiteral("lswi"), QStringLiteral("lswx"), QStringLiteral("stswi"),
                   QStringLiteral("stswx")})) {
        const auto rt = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        QString ea, n, body;
        if (mn.endsWith(QStringLiteral("i"))) {
            const int count = ops.value(2).toInt();
            const int nb = count ? count : 32;
            ea = ((text(ra) == QStringLiteral("0"))
                      ? QStringLiteral("0")
                      : (QStringLiteral("ctx->gpr[") + text(ra) + QStringLiteral("]")));
            n = text(nb);
        } else {
            ea = indexedAddress(ra, registerIndex(ops.value(2)));
            n = QStringLiteral("(ctx->xer & 0x7Fu)");
        }
        if (mn.startsWith(QStringLiteral("l"))) {
            body = QStringLiteral(
                "for (uint32_t i = 0; i < n; i++) { uint32_t r = (rt + i / 4) & 31, k = i & 3; if (!k) "
                "ctx->gpr[r] = 0; ctx->gpr[r] |= (uint64_t)vm_read8(ea + i) << (24 - 8 * k); }");
        } else {
            body =
                QStringLiteral("for (uint32_t i = 0; i < n; i++) { uint32_t r = (rt + i / 4) & 31, k = i & "
                               "3; vm_write8(ea + i, (uint8_t)(ctx->gpr[r] >> (24 - 8 * k))); }");
        }
        return (QStringLiteral("{ uint64_t ea = ") + text(ea) + QStringLiteral("; uint32_t n = ") + text(n) +
                QStringLiteral(", rt = ") + text(rt) + QStringLiteral("; ") + text(body) +
                QStringLiteral(" }"));
    }
    if ((mn == QStringLiteral("tw")) || (mn == QStringLiteral("twi")) || (mn == QStringLiteral("td")) ||
        (mn == QStringLiteral("tdi"))) {
        return (QStringLiteral("/* trap: ") + text(mn) + QStringLiteral(" ") + text(insn.operands) +
                QStringLiteral(" — ignored */;"));
    }
    if (mn == QStringLiteral("lwbrx")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ") + text(indexedAddress(ra, rb)) +
                QStringLiteral("; uint32_t raw = VM_RAW_LD(uint32_t, ea); ctx->gpr[") + text(rd) +
                QStringLiteral("] = raw; }"));
    }
    if (mn == QStringLiteral("stwbrx")) {
        const auto rs = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ") + text(indexedAddress(ra, rb)) +
                QStringLiteral("; uint32_t raw = (uint32_t)ctx->gpr[") + text(rs) +
                QStringLiteral("]; VM_RAW_ST(uint32_t, ea, raw); }"));
    }
    if (mn == QStringLiteral("lhbrx")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ") + text(indexedAddress(ra, rb)) +
                QStringLiteral("; uint16_t raw = VM_RAW_LD(uint16_t, ea); ctx->gpr[") + text(rd) +
                QStringLiteral("] = raw; }"));
    }
    if (mn == QStringLiteral("ldbrx")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ") + text(indexedAddress(ra, rb)) +
                QStringLiteral("; uint64_t raw = VM_RAW_LD(uint64_t, ea); ctx->gpr[") + text(rd) +
                QStringLiteral("] = raw; }"));
    }
    if (mn == QStringLiteral("sthbrx")) {
        const auto rs = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ") + text(indexedAddress(ra, rb)) +
                QStringLiteral("; uint16_t raw = (uint16_t)ctx->gpr[") + text(rs) +
                QStringLiteral("]; VM_RAW_ST(uint16_t, ea, raw); }"));
    }
    if (mn == QStringLiteral("lwax")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ") + text(indexedAddress(ra, rb)) +
                QStringLiteral("; ctx->gpr[") + text(rd) +
                QStringLiteral("] = (int64_t)(int32_t)vm_read32(ea); }"));
    }
    if (mn == QStringLiteral("lhaux")) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("{ uint64_t ea = ctx->gpr[") + text(ra) + QStringLiteral("] + ctx->gpr[") +
                text(rb) + QStringLiteral("]; ctx->gpr[") + text(rd) +
                QStringLiteral("] = (int64_t)(int16_t)vm_read16(ea); ctx->gpr[") + text(ra) +
                QStringLiteral("] = ea; }"));
    }
    if (mn == QStringLiteral("mftb")) {
        auto rd = registerIndex(ops.value(0));
        return (QStringLiteral("ctx->gpr[") + text(rd) + QStringLiteral("] = ppu_timebase_now();"));
    }
    if (mn == QStringLiteral("mftbu")) {
        auto rd = registerIndex(ops.value(0));
        return (QStringLiteral("ctx->gpr[") + text(rd) + QStringLiteral("] = (ppu_timebase_now() >> 32);"));
    }
    if (mn == QStringLiteral("dcbz")) {
        auto ra = registerIndex(ops.value(0));
        auto rb = registerIndex(ops.value(1));
        return (QStringLiteral("{ uint64_t ea = (") + text(indexedAddress(ra, rb)) +
                QStringLiteral(") & ~0x7FULL; memset(vm_base + (uint32_t)ea, 0, 128); }"));
    }
    if (oneOf(mn, {QStringLiteral("dcbt"), QStringLiteral("dcbtst"), QStringLiteral("dcbf"),
                   QStringLiteral("dcbst"), QStringLiteral("dcba"), QStringLiteral("icbi")})) {
        return (QStringLiteral("/* ") + text(mn) + QStringLiteral(": cache hint, no-op */;"));
    }
    if (oneOf(mn, {QStringLiteral("sync"), QStringLiteral("ptesync")})) {
        return (QStringLiteral("PPU_FENCE(seq_cst);   /* ") + text(mn) +
                QStringLiteral(": full barrier, StoreLoad included */"));
    }
    if (mn == QStringLiteral("lwsync")) {
        return QStringLiteral("PPU_FENCE(acq_rel);   /* lwsync: everything but StoreLoad */");
    }
    if (mn == QStringLiteral("eieio")) {
        return QStringLiteral("PPU_FENCE(release);   /* eieio: orders stores ahead of it */");
    }
    if (mn == QStringLiteral("isync")) {
        return QStringLiteral("PPU_FENCE(acquire);   /* isync: discards speculation past it */");
    }
    if (mn.startsWith(QStringLiteral("addme"))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        return (
            QStringLiteral("{ uint64_t ca = (ctx->xer >> 29) & 1; uint64_t a = ctx->gpr[") + text(ra) +
            QStringLiteral(
                "], b = ~0ULL; uint64_t s1 = a + b; uint64_t co = (s1 < a); uint64_t result = s1 + ca; co |= "
                "(result < s1); ctx->xer = (ctx->xer & ~(1u << 29)) | ((uint32_t)co << 29); ctx->gpr[") +
            text(rd) + QStringLiteral("] = result; }"));
    }
    if (mn.startsWith(QStringLiteral("subfme"))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        return (
            QStringLiteral("{ uint64_t ca = (ctx->xer >> 29) & 1; uint64_t a = ~ctx->gpr[") + text(ra) +
            QStringLiteral(
                "], b = ~0ULL; uint64_t s1 = a + b; uint64_t co = (s1 < a); uint64_t result = s1 + ca; co |= "
                "(result < s1); ctx->xer = (ctx->xer & ~(1u << 29)) | ((uint32_t)co << 29); ctx->gpr[") +
            text(rd) + QStringLiteral("] = result; }"));
    }
    if (mn.startsWith(QStringLiteral("subfze"))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        return (QStringLiteral("{ uint64_t ca = (ctx->xer >> 29) & 1; uint64_t result = ~ctx->gpr[") +
                text(ra) +
                QStringLiteral("] + ca; ctx->xer = (ctx->xer & ~(1u << 29)) | ((ca && result == 0) ? (1u << "
                               "29) : 0); ctx->gpr[") +
                text(rd) + QStringLiteral("] = result; }"));
    }
    if (mn.startsWith(QStringLiteral("mulhd")) && !(mn.startsWith(QStringLiteral("mulhdu")))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + text(rd) +
                QStringLiteral("] = (uint64_t)ppc_mulhd((int64_t)ctx->gpr[") + text(ra) +
                QStringLiteral("], (int64_t)ctx->gpr[") + text(rb) + QStringLiteral("]);"));
    }
    if (mn.startsWith(QStringLiteral("mulhdu"))) {
        const auto rd = registerIndex(ops.value(0));
        const auto ra = registerIndex(ops.value(1));
        const auto rb = registerIndex(ops.value(2));
        return (QStringLiteral("ctx->gpr[") + text(rd) +
                QStringLiteral("] = (uint64_t)ppc_mulhdu(ctx->gpr[") + text(ra) +
                QStringLiteral("], ctx->gpr[") + text(rb) + QStringLiteral("]);"));
    }
    return {};
}

QString lowerScalarOp(const PpuInstruction &insn, const QStringList &operands, quint32 tocBase) {
    auto code = arithmetic(insn, operands);
    if (code.isEmpty())
        code = logicalAndShift(insn, operands);
    if (code.isEmpty())
        code = memory(insn, operands, tocBase);
    if (code.isEmpty())
        code = comparison(insn, operands);
    if (code.isEmpty())
        code = specialRegisters(insn, operands);
    if (code.isEmpty())
        code = floatingPoint(insn, operands);
    if (code.isEmpty())
        code = extendedArithmetic(insn, operands);
    if (code.isEmpty())
        code = miscellaneous(insn, operands);
    return code;
}

// OE needs the original operands when the destination aliases either source.
QString lowerOverflow(const PpuInstruction &insn, const QStringList &ops, quint32 tocBase, QString code) {
    if ((insn.raw >> 26) != 31)
        return code;
    static const QHash<int, QString> overflow{
        {266, QStringLiteral("(((_a ^ _r) & (_b ^ _r)) >> 63)")},
        {10, QStringLiteral("(((_a ^ _r) & (_b ^ _r)) >> 63)")},
        {138, QStringLiteral("(((_a ^ _r) & (_b ^ _r)) >> 63)")},
        {40, QStringLiteral("(((~_a ^ _r) & (_b ^ _r)) >> 63)")},
        {8, QStringLiteral("(((~_a ^ _r) & (_b ^ _r)) >> 63)")},
        {136, QStringLiteral("(((~_a ^ _r) & (_b ^ _r)) >> 63)")},
        {234, QStringLiteral("(((_a ^ _r) & ~_r) >> 63)")},
        {202, QStringLiteral("(((_a ^ _r) & _r) >> 63)")},
        {232, QStringLiteral("(((~_a ^ _r) & ~_r) >> 63)")},
        {200, QStringLiteral("(((~_a ^ _r) & _r) >> 63)")},
        {104, QStringLiteral("(_a == 0x8000000000000000ull)")},
        {235,
         QStringLiteral(
             "({ int64_t _p = (int64_t)(int32_t)_a * (int64_t)(int32_t)_b; _p != (int64_t)(int32_t)_p; })")},
        {233, QStringLiteral("({ int64_t _p; __builtin_mul_overflow((int64_t)_a, (int64_t)_b, &_p); })")},
        {491, QStringLiteral(
                  "((uint32_t)_b == 0 || ((uint32_t)_a == 0x80000000u && (uint32_t)_b == 0xFFFFFFFFu))")},
        {459, QStringLiteral("((uint32_t)_b == 0)")},
        {489, QStringLiteral("(_b == 0 || (_a == 0x8000000000000000ull && _b == ~0ull))")},
        {457, QStringLiteral("(_b == 0)")},
    };
    const auto formula = overflow.value((insn.raw >> 1) & 0x1FF);
    if (formula.isEmpty())
        return code;
    const bool enabled = (insn.raw >> 10) & 1;
    if (code.isEmpty()) {
        auto base = insn;
        if (base.mnemonic.endsWith('.'))
            base.mnemonic.chop(1);
        if (enabled && base.mnemonic.endsWith('o'))
            base.mnemonic.chop(1);
        if (base.mnemonic != insn.mnemonic)
            code = lowerScalarOp(base, ops, tocBase);
    }
    if (!enabled || code.isEmpty() || code.startsWith("/*"))
        return code;
    const auto rt = (insn.raw >> 21) & 31;
    const auto ra = (insn.raw >> 16) & 31;
    const auto rb = (insn.raw >> 11) & 31;
    return QStringLiteral("{ const uint64_t _a = ctx->gpr[%1], _b = ctx->gpr[%2]; (void)_b; "
                          "%3 { const uint64_t _r = ctx->gpr[%4]; (void)_r; "
                          "const uint32_t _ov = (uint32_t)(%5); "
                          "ctx->xer = (ctx->xer & ~0x40000000u) | (_ov << 30) | (_ov << 31); } }")
        .arg(QString::number(ra), QString::number(rb), code, QString::number(rt), formula);
}

// Integer and floating-point record forms update different CR fields.
QString applyRecord(const PpuInstruction &insn, const QStringList &ops, QString code) {
    const auto &mn = insn.mnemonic;
    if (code.isEmpty() || code.startsWith("/*"))
        return code;
    if (mn.endsWith('.') && !ops.isEmpty() && !mn.startsWith('f') && !mn.startsWith('v') &&
        !mn.startsWith("mt") && !mn.startsWith("mf") && mn != "stwcx." && mn != "stdcx.") {
        static const QRegularExpression destination("^r([0-9]+)$");
        const auto match = destination.match(ops.first());
        if (match.hasMatch()) {
            code += QStringLiteral(" { int64_t _r = (int64_t)ctx->gpr[%1]; "
                                   "uint32_t _c = (_r < 0) ? 8u : (_r > 0) ? 4u : 2u; "
                                   "_c |= (uint32_t)((ctx->xer >> 31) & 1u); "
                                   "ctx->cr = (ctx->cr & 0x0FFFFFFFu) | (_c << 28); }")
                        .arg(match.captured(1));
        }
    }
    if ((insn.raw & 1) && ((insn.raw >> 26) == 59 || (insn.raw >> 26) == 63))
        code += QStringLiteral(" ctx->cr = (ctx->cr & ~0x0F000000u) | ((ctx->fpscr >> 4) & 0x0F000000u);");
    return code;
}
} // namespace

QString lowerPpuScalar(const PpuInstruction &insn, quint32 tocBase) {
    QStringList operands;
    for (const auto &piece : insn.operands.split(',')) {
        const auto operand = piece.trimmed();
        if (!operand.isEmpty())
            operands.append(operand);
    }
    auto code = lowerScalarOp(insn, operands, tocBase);
    code = lowerOverflow(insn, operands, tocBase, std::move(code));
    return applyRecord(insn, operands, std::move(code));
}
} // namespace prepare
