#include "spu_decode.h"
#include <algorithm>
#include <initializer_list>
#include <string_view>

namespace prepare {
namespace {
struct Op {
    int opcode;
    const char *name;
};
#include "spu_opcodes.inc"
template <size_t N> const char *lookup(const Op (&table)[N], int opcode) {
    const auto it =
        std::lower_bound(table, table + N, opcode, [](const Op &op, int value) { return op.opcode < value; });
    return it != table + N && it->opcode == opcode ? it->name : nullptr;
}
int signExtend(quint32 value, int width) {
    return int(value) - ((value & (1u << (width - 1))) ? (1 << width) : 0);
}
QString reg(int index) {
    return "$r" + QString::number(index);
}
QString hex(quint32 value, int width = 0) {
    return "0x" + QString::number(value, 16).toUpper().rightJustified(width, '0');
}
bool oneOf(std::string_view value, std::initializer_list<std::string_view> choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}
} // namespace
QJsonArray SpuInstruction::json() const {
    return {qint64(address), qint64(raw), mnemonic, operands};
}
SpuInstruction decodeSpu(quint32 raw, quint32 address) {
    const int op4 = raw >> 28, op7 = raw >> 25, op8 = raw >> 24, op9 = raw >> 23, op10 = raw >> 22,
              op11 = raw >> 21;
    const int rt = raw & 127, ra = (raw >> 7) & 127, rb = (raw >> 14) & 127, rc = (raw >> 21) & 127;
    const auto i10 = signExtend((raw >> 14) & 0x3ff, 10), i16 = signExtend((raw >> 7) & 0xffff, 16);
    const quint32 i18 = (raw >> 7) & 0x3ffff;
    auto result = [&](const char *mnemonic, const QString &operands = QString()) {
        return SpuInstruction{address, raw, QString::fromLatin1(mnemonic), operands};
    };
    auto rr = [&] { return reg(rt) + ", " + reg(ra); };
    auto rrr = [&] { return rr() + ", " + reg(rb); };
    auto immediate = [&](int value) { return rr() + ", " + QString::number(value); };
    auto target = [&](quint32 value) { return reg(rt) + ", " + hex(value); };
    // Decode precedence is part of the ISA adapter contract: e.g. RI16 before
    // RI10 and channel operations before the colliding shift prefixes.
    if (const auto op = lookup(RRR, op4))
        return result(op, reg(rc) + ", " + reg(ra) + ", " + reg(rb) + ", " + reg(rt));
    if (const auto op = lookup(RI18, op7)) {
        const std::string_view name(op);
        if (name == "hbra" || name == "hbrr") {
            const quint32 hint = address + signExtend(((i18 >> 16) & 3) * 128 + rt, 9) * 4;
            const quint32 branch =
                name == "hbra" ? (i18 & 0xffff) * 4 : (address + signExtend(i18, 18) * 4) & 0x3ffff;
            return result(op, hex(hint) + ", " + hex(branch));
        }
        return result(op, target(i18));
    }
    if (const auto op = lookup(RI16, op9)) {
        const std::string_view name(op);
        if (oneOf(name, {"lqa", "stqa"}))
            return result(op, target(((i16 & 0xffff) * 4) & 0x3fff0));
        if (oneOf(name, {"hbra", "hbrr"}))
            return result(op, hex(i16 & 0xffff) + ", " + reg(rt));
        if (name == "il")
            return result(op, reg(rt) + ", " + QString::number(i16));
        if (oneOf(name, {"brz", "brnz", "brhz", "brhnz", "brsl"}))
            return result(op, target((address + i16 * 4) & 0x3ffff));
        if (name == "br")
            return result(op, hex((address + i16 * 4) & 0x3ffff));
        if (name == "bra")
            return result(op, hex((i16 * 4) & 0x3ffff));
        if (name == "brasl")
            return result(op, target((i16 * 4) & 0x3ffff));
        if (oneOf(name, {"lqr", "stqr"}))
            return result(op, target((address + i16 * 4) & 0x3fff0));
        return result(op, target(i16 & 0xffff));
    }
    if (op11 == 0x00d || op11 == 0x10d || op11 == 0x00f) {
        const auto name = lookup(Channels, ra);
        const auto channel = name ? QString::fromLatin1(name) : "ch" + QString::number(ra);
        if (op11 == 0x10d)
            return result("wrch", channel + ", " + reg(rt));
        return result(op11 == 0x00d ? "rdch" : "rchcnt", reg(rt) + ", " + channel);
    }
    if (const auto op = lookup(RI10, op8)) {
        if (oneOf(op, {"lqd", "stqd"})) {
            const auto displacement = i10 * 16;
            return result(op, reg(rt) + ", " + (displacement < 0 ? "-" : "") + hex(qAbs(displacement)) + "(" +
                                  reg(ra) + ")");
        }
        return result(op, immediate(i10));
    }
    if (const auto op = lookup(RI8, op10))
        return result(op, immediate((raw >> 14) & 255));
    if (const auto op = lookup(RR, op11)) {
        const std::string_view name(op);
        if (name == "bi")
            return result(op, reg(ra));
        if (oneOf(name, {"bisl", "biz", "binz", "bihz", "bihnz"}))
            return result(op, rr());
        if (name == "hbr")
            return result(op,
                          hex(address + signExtend(((raw >> 14) & 3) * 128 + rt, 9) * 4) + ", " + reg(ra));
        if (name == "stop")
            return result(op, hex(raw & 0x3fff));
        if (oneOf(name, {"lnop", "nop", "sync", "dsync"}))
            return result(name == "sync" && (raw & 0x100000) ? "syncc" : op);
        if (oneOf(name, {"clz", "cntb", "gb", "gbh", "fscrrd", "orx", "fsm", "fsmb"}))
            return result(op, rr());
        return result(op, rrr());
    }
    if (const auto op = lookup(RI7, op11))
        return result(op, immediate(oneOf(op, {"cbd", "chd", "cwd", "cdd"}) ? rb : signExtend(rb, 7)));
    if (const auto op = lookup(Indexed, op11))
        return result(op, rrr());
    return result(".word", hex(raw, 8));
}
QVector<SpuInstruction> decodeSpuImage(const QByteArray &bytes, const Cancel &cancel) {
    const auto image = validateSpuImage(bytes);
    QVector<SpuInstruction> result;
    for (const auto &segment : image.loads)
        if (segment.flags & 1) {
            require(result.size() + segment.fileSize / 4 <= 65536,
                    "SPU executable segments exceed local-store capacity.");
            for (quint64 at = 0; at < segment.fileSize; at += 4) {
                if (at % 4096 == 0)
                    cancelled(cancel);
                result << decodeSpu(be32(bytes, segment.offset + at), quint32(segment.address + at));
            }
        }
    return result;
}
} // namespace prepare
