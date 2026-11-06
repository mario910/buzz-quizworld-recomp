#include "ppu_decode.h"
#include <QHash>
#include <algorithm>
#include <array>
#include <initializer_list>

namespace prepare {
namespace {
int bits(quint32 word, int first, int last) {
    return int((word >> (31 - last)) & ((quint32(1) << (last - first + 1)) - 1));
}
bool bit(quint32 word, int pos) {
    return (word >> (31 - pos)) & 1;
}
int signExtend(int value, int width) {
    return (value ^ (1 << (width - 1))) - (1 << (width - 1));
}
QString hex32(quint32 value, int width = 0) {
    return QString::number(value, 16).toUpper().rightJustified(width, '0');
}
bool oneOf(int value, std::initializer_list<int> choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}

static const QHash<int, QString> SPR_NAMES{
    {1, QStringLiteral("XER")},     {8, QStringLiteral("LR")},       {9, QStringLiteral("CTR")},
    {18, QStringLiteral("DSISR")},  {19, QStringLiteral("DAR")},     {22, QStringLiteral("DEC")},
    {25, QStringLiteral("SDR1")},   {256, QStringLiteral("VRSAVE")}, {26, QStringLiteral("SRR0")},
    {27, QStringLiteral("SRR1")},   {268, QStringLiteral("TBL")},    {269, QStringLiteral("TBU")},
    {272, QStringLiteral("SPRG0")}, {273, QStringLiteral("SPRG1")},  {274, QStringLiteral("SPRG2")},
    {275, QStringLiteral("SPRG3")}, {287, QStringLiteral("PVR")},    {1008, QStringLiteral("HID0")},
    {1009, QStringLiteral("HID1")}, {1013, QStringLiteral("DABR")}};

QPair<QString, QString> branchCondition(int bo, int bi) {
    static const std::array<QString, 4> whenSet{QStringLiteral("lt"), QStringLiteral("gt"),
                                                QStringLiteral("eq"), QStringLiteral("so")};
    static const std::array<QString, 4> whenClear{QStringLiteral("ge"), QStringLiteral("le"),
                                                  QStringLiteral("ne"), QStringLiteral("ns")};
    const bool decrementCtr = !(bo & 0x04);
    const bool testCondition = !(bo & 0x10);
    QString suffix;
    if (decrementCtr)
        suffix = (bo & 0x02) ? QStringLiteral("dz") : QStringLiteral("dnz");
    if (!testCondition)
        return {suffix, {}};
    suffix += (bo & 0x08) ? whenSet[bi & 3] : whenClear[bi & 3];
    const int field = bi >> 2;
    const auto operand = field ? QStringLiteral("cr%1, ").arg(field) : QString{};
    return {suffix, operand};
}
} // namespace

PpuInstruction decodePpu(quint32 insn, quint32 addr) {
    const auto opcd = bits(insn, 0, 5);
    auto result = PpuInstruction{addr, insn, QStringLiteral("???"), {}, {}};
    // Branch I-form  (opcd 18)
    if (opcd == 18) {
        const auto li = bits(insn, 6, 29);
        const auto aa = bit(insn, 30);
        const auto lk = bit(insn, 31);
        const quint32 target = quint32(signExtend(li << 2, 26)) + (aa ? 0u : addr);
        auto mne = QStringLiteral("b");
        if (lk) {
            mne += QStringLiteral("l");
        }
        if (aa) {
            mne += QStringLiteral("a");
        }
        result.mnemonic = mne;
        result.operands = QStringLiteral("0x") + hex32(target);
        return result;
    }
    // Branch conditional B-form  (opcd 16)
    if (opcd == 16) {
        const auto bo = bits(insn, 6, 10);
        const auto bi = bits(insn, 11, 15);
        const auto bd = bits(insn, 16, 29);
        const auto aa = bit(insn, 30);
        const auto lk = bit(insn, 31);
        const quint32 target = quint32(signExtend(bd << 2, 16)) + (aa ? 0u : addr);
        auto [cond_suf, cr_str] = branchCondition(bo, bi);
        auto mne = QStringLiteral("b");
        if (!cond_suf.isEmpty()) {
            mne += cond_suf;
        } else {
            mne += QStringLiteral("c");
        }
        if (lk) {
            mne += QStringLiteral("l");
        }
        if (aa) {
            mne += QStringLiteral("a");
        }
        if (!cond_suf.isEmpty()) {
            result.mnemonic = mne;
            result.operands = cr_str + QStringLiteral("0x") + hex32(target);
        } else {
            result.mnemonic = mne;
            result.operands = QString::number(bo) + QStringLiteral(", ") + QString::number(bi) +
                              QStringLiteral(", 0x") + hex32(target);
        }
        return result;
    }
    // Syscall (opcd 17)
    if (opcd == 17) {
        if (bit(insn, 30)) {
            result.mnemonic = QStringLiteral("sc");
            return result;
        }
    }
    // Condition register XL-form (opcd 19)
    if (opcd == 19) {
        const auto xo = bits(insn, 21, 30);
        const auto bo = bits(insn, 6, 10);
        const auto bi = bits(insn, 11, 15);
        const auto lk = bit(insn, 31);
        if (xo == 16) {
            auto [cond_suf, cr_str] = branchCondition(bo, bi);
            if ((bo == 20) && (bi == 0)) {
                auto mne = QStringLiteral("blr");
                if (lk) {
                    mne += QStringLiteral("l");
                }
                result.mnemonic = mne;
            } else {
                auto mne = (!cond_suf.isEmpty() ? ((QStringLiteral("b") + cond_suf) + QStringLiteral("lr"))
                                                : QStringLiteral("bclr"));
                if (lk) {
                    mne += QStringLiteral("l");
                }
                result.mnemonic = mne;
                if (cond_suf.isEmpty()) {
                    result.operands = QString::number(bo) + QStringLiteral(", ") + QString::number(bi);
                } else {
                    if (!cr_str.isEmpty()) {
                        result.operands = cr_str.chopped(2);
                    }
                }
            }
            return result;
        }
        if (xo == 528) {
            auto [cond_suf, cr_str] = branchCondition(bo, bi);
            if ((bo == 20) && (bi == 0)) {
                auto mne = QStringLiteral("bctr");
                if (lk) {
                    mne += QStringLiteral("l");
                }
                result.mnemonic = mne;
            } else {
                auto mne = (!cond_suf.isEmpty() ? ((QStringLiteral("b") + cond_suf) + QStringLiteral("ctr"))
                                                : QStringLiteral("bcctr"));
                if (lk) {
                    mne += QStringLiteral("l");
                }
                result.mnemonic = mne;
                if (cond_suf.isEmpty()) {
                    result.operands = QString::number(bo) + QStringLiteral(", ") + QString::number(bi);
                } else {
                    if (!cr_str.isEmpty()) {
                        result.operands = cr_str.chopped(2);
                    }
                }
            }
            return result;
        }
        // CR logical operations
        auto bt = bits(insn, 6, 10);
        auto ba = bits(insn, 11, 15);
        auto bb = bits(insn, 16, 20);
        static const QHash<int, QString> cr_ops{
            {257, QStringLiteral("crand")},  {449, QStringLiteral("cror")},   {193, QStringLiteral("crxor")},
            {33, QStringLiteral("crnor")},   {225, QStringLiteral("crnand")}, {289, QStringLiteral("creqv")},
            {129, QStringLiteral("crandc")}, {417, QStringLiteral("crorc")},  {0, QStringLiteral("mcrf")}};
        if (cr_ops.contains(xo)) {
            result.mnemonic = cr_ops.value(xo);
            if (xo == 0) {
                result.operands = QStringLiteral("cr") + QString::number((bt >> 2)) + QStringLiteral(", cr") +
                                  QString::number((ba >> 2));
            } else {
                result.operands = QString::number(bt) + QStringLiteral(", ") + QString::number(ba) +
                                  QStringLiteral(", ") + QString::number(bb);
            }
            return result;
        }
        if (xo == 50) {
            result.mnemonic = QStringLiteral("rfi");
            return result;
        }
        if (xo == 150) {
            result.mnemonic = QStringLiteral("isync");
            return result;
        }
    }
    // Integer arithmetic / logical / load-store by major opcode
    const auto rd = bits(insn, 6, 10);
    const auto ra = bits(insn, 11, 15);
    auto si = signExtend(bits(insn, 16, 31), 16);
    auto ui = bits(insn, 16, 31);
    // addi (14), addis (15)
    if (opcd == 14) {
        if (ra == 0) {
            result.mnemonic = QStringLiteral("li");
            result.operands =
                QStringLiteral("r") + QString::number(rd) + QStringLiteral(", ") + QString::number(si);
        } else {
            result.mnemonic = QStringLiteral("addi");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", ") + QString::number(si);
        }
        return result;
    }
    if (opcd == 15) {
        if (ra == 0) {
            result.mnemonic = QStringLiteral("lis");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", 0x") + hex32(ui);
        } else {
            result.mnemonic = QStringLiteral("addis");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", 0x") + hex32(ui);
        }
        return result;
    }
    // ori (24), oris (25), xori (26), xoris (27), andi. (28), andis. (29)
    if (opcd == 24) {
        if ((rd == 0) && (ra == 0) && (ui == 0)) {
            result.mnemonic = QStringLiteral("nop");
        } else {
            result.mnemonic = QStringLiteral("ori");
            result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                              QString::number(rd) + QStringLiteral(", 0x") + hex32(ui);
        }
        return result;
    }
    if (opcd == 25) {
        result.mnemonic = QStringLiteral("oris");
        result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                          QString::number(rd) + QStringLiteral(", 0x") + hex32(ui);
        return result;
    }
    if (opcd == 26) {
        result.mnemonic = QStringLiteral("xori");
        result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                          QString::number(rd) + QStringLiteral(", 0x") + hex32(ui);
        return result;
    }
    if (opcd == 27) {
        result.mnemonic = QStringLiteral("xoris");
        result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                          QString::number(rd) + QStringLiteral(", 0x") + hex32(ui);
        return result;
    }
    if (opcd == 28) {
        result.mnemonic = QStringLiteral("andi.");
        result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                          QString::number(rd) + QStringLiteral(", 0x") + hex32(ui);
        return result;
    }
    if (opcd == 29) {
        result.mnemonic = QStringLiteral("andis.");
        result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                          QString::number(rd) + QStringLiteral(", 0x") + hex32(ui);
        return result;
    }
    // mulli (7)
    if (opcd == 7) {
        result.mnemonic = QStringLiteral("mulli");
        result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                          QString::number(ra) + QStringLiteral(", ") + QString::number(si);
        return result;
    }
    // subfic (8)
    if (opcd == 8) {
        result.mnemonic = QStringLiteral("subfic");
        result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                          QString::number(ra) + QStringLiteral(", ") + QString::number(si);
        return result;
    }
    // cmpli (10), cmpi (11)
    if (opcd == 10) {
        auto bf = bits(insn, 6, 8);
        auto l_bit = bit(insn, 10);
        result.mnemonic = (l_bit ? QStringLiteral("cmpldi") : QStringLiteral("cmplwi"));
        auto cr_str =
            (bf ? QStringLiteral("cr") + QString::number(bf) + QStringLiteral(", ") : QStringLiteral(""));
        result.operands =
            cr_str + QStringLiteral("r") + QString::number(ra) + QStringLiteral(", 0x") + hex32(ui);
        return result;
    }
    if (opcd == 11) {
        auto bf = bits(insn, 6, 8);
        auto l_bit = bit(insn, 10);
        result.mnemonic = (l_bit ? QStringLiteral("cmpdi") : QStringLiteral("cmpwi"));
        auto cr_str =
            (bf ? QStringLiteral("cr") + QString::number(bf) + QStringLiteral(", ") : QStringLiteral(""));
        result.operands =
            cr_str + QStringLiteral("r") + QString::number(ra) + QStringLiteral(", ") + QString::number(si);
        return result;
    }
    // addic (12), addic. (13)
    if (opcd == 12) {
        result.mnemonic = QStringLiteral("addic");
        result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                          QString::number(ra) + QStringLiteral(", ") + QString::number(si);
        return result;
    }
    if (opcd == 13) {
        result.mnemonic = QStringLiteral("addic.");
        result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                          QString::number(ra) + QStringLiteral(", ") + QString::number(si);
        return result;
    }
    // Load/store instructions
    static const QHash<int, QPair<QString, bool>> load_store{
        {32, {QStringLiteral("lwz"), true}},   {33, {QStringLiteral("lwzu"), true}},
        {34, {QStringLiteral("lbz"), true}},   {35, {QStringLiteral("lbzu"), true}},
        {36, {QStringLiteral("stw"), false}},  {37, {QStringLiteral("stwu"), false}},
        {38, {QStringLiteral("stb"), false}},  {39, {QStringLiteral("stbu"), false}},
        {40, {QStringLiteral("lhz"), true}},   {41, {QStringLiteral("lhzu"), true}},
        {42, {QStringLiteral("lha"), true}},   {43, {QStringLiteral("lhau"), true}},
        {44, {QStringLiteral("sth"), false}},  {45, {QStringLiteral("sthu"), false}},
        {46, {QStringLiteral("lmw"), true}},   {47, {QStringLiteral("stmw"), false}},
        {48, {QStringLiteral("lfs"), true}},   {49, {QStringLiteral("lfsu"), true}},
        {50, {QStringLiteral("lfd"), true}},   {51, {QStringLiteral("lfdu"), true}},
        {52, {QStringLiteral("stfs"), false}}, {53, {QStringLiteral("stfsu"), false}},
        {54, {QStringLiteral("stfd"), false}}, {55, {QStringLiteral("stfdu"), false}}};
    if (load_store.contains(opcd)) {
        auto [mne, is_load] = load_store.value(opcd);
        auto d = signExtend(bits(insn, 16, 31), 16);
        const auto disp = d < 0 ? QStringLiteral("-0x") + hex32(-d) : QStringLiteral("0x") + hex32(d);
        result.mnemonic = mne;
        result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", ") + disp +
                          QStringLiteral("(r") + QString::number(ra) + QStringLiteral(")");
        return result;
    }
    // DS-form loads/stores (opcd 58 = ld/ldu/lwa, opcd 62 = std/stdu)
    if (opcd == 58) {
        auto ds = signExtend((bits(insn, 16, 29) << 2), 16);
        auto sub = bits(insn, 30, 31);
        auto mne = (sub == 0 ? QStringLiteral("ld")
                             : (sub == 1 ? QStringLiteral("ldu")
                                         : (sub == 2 ? QStringLiteral("lwa") : QStringLiteral("ld?"))));
        const auto disp =
            ((ds < 0) ? QStringLiteral("-0x") + hex32((-ds)) : QStringLiteral("0x") + hex32(ds));
        result.mnemonic = mne;
        result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", ") + disp +
                          QStringLiteral("(r") + QString::number(ra) + QStringLiteral(")");
        return result;
    }
    if (opcd == 62) {
        auto ds = signExtend((bits(insn, 16, 29) << 2), 16);
        auto sub = bits(insn, 30, 31);
        auto mne =
            (sub == 0 ? QStringLiteral("std") : (sub == 1 ? QStringLiteral("stdu") : QStringLiteral("std?")));
        const auto disp =
            ((ds < 0) ? QStringLiteral("-0x") + hex32((-ds)) : QStringLiteral("0x") + hex32(ds));
        result.mnemonic = mne;
        result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", ") + disp +
                          QStringLiteral("(r") + QString::number(ra) + QStringLiteral(")");
        return result;
    }
    // Rotate / shift (opcd 21 = rlwinm, 23 = rlwnm, 20 = rlwimi)
    if (opcd == 21) {
        auto sh = bits(insn, 16, 20);
        auto mb = bits(insn, 21, 25);
        auto me = bits(insn, 26, 30);
        const auto rc = bit(insn, 31);
        auto mne = (QStringLiteral("rlwinm") + (rc ? QStringLiteral(".") : QStringLiteral("")));
        result.mnemonic = mne;
        result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                          QString::number(rd) + QStringLiteral(", ") + QString::number(sh) +
                          QStringLiteral(", ") + QString::number(mb) + QStringLiteral(", ") +
                          QString::number(me);
        return result;
    }
    if (opcd == 23) {
        const auto rb = bits(insn, 16, 20);
        auto mb = bits(insn, 21, 25);
        auto me = bits(insn, 26, 30);
        const auto rc = bit(insn, 31);
        auto mne = (QStringLiteral("rlwnm") + (rc ? QStringLiteral(".") : QStringLiteral("")));
        result.mnemonic = mne;
        result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                          QString::number(rd) + QStringLiteral(", r") + QString::number(rb) +
                          QStringLiteral(", ") + QString::number(mb) + QStringLiteral(", ") +
                          QString::number(me);
        return result;
    }
    if (opcd == 20) {
        auto sh = bits(insn, 16, 20);
        auto mb = bits(insn, 21, 25);
        auto me = bits(insn, 26, 30);
        const auto rc = bit(insn, 31);
        auto mne = (QStringLiteral("rlwimi") + (rc ? QStringLiteral(".") : QStringLiteral("")));
        result.mnemonic = mne;
        result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                          QString::number(rd) + QStringLiteral(", ") + QString::number(sh) +
                          QStringLiteral(", ") + QString::number(mb) + QStringLiteral(", ") +
                          QString::number(me);
        return result;
    }
    // 64-bit rotate (opcd 30 = rldic*/rldc*)
    if (opcd == 30) {
        const auto rc = bit(insn, 31);
        // The sub-opcode encoding is different for immediate vs register forms:
        // Bits 27-30 for immediate forms (rldicl, rldicr, rldic, rldimi)
        // Bits 27-29 for register forms (rldcl, rldcr) where bit 30 is part of sh
        auto sub4 = bits(insn, 27, 30);
        auto sub3 = bits(insn, 27, 29);
        // MD form: XO is bits 27-29 (3-bit), bit 30 is sh[5]
        // MDS form: XO is bits 27-30 (4-bit, values 8=rldcl, 9=rldcr)
        static const QHash<int, QString> md_ops{{0, QStringLiteral("rldicl")},
                                                {1, QStringLiteral("rldicr")},
                                                {2, QStringLiteral("rldic")},
                                                {3, QStringLiteral("rldimi")}};
        if (md_ops.contains(sub3)) {
            auto sh = (bits(insn, 16, 20) | (bit(insn, 30) << 5));
            auto mb = (bits(insn, 21, 25) | (bit(insn, 26) << 5));
            auto mne = (md_ops.value(sub3) + (rc ? QStringLiteral(".") : QStringLiteral("")));
            result.mnemonic = mne;
            result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                              QString::number(rd) + QStringLiteral(", ") + QString::number(sh) +
                              QStringLiteral(", ") + QString::number(mb);
        } else {
            if (sub4 == 8) {
                const auto rb = bits(insn, 16, 20);
                auto mb = (bits(insn, 21, 25) | (bit(insn, 26) << 5));
                auto mne = (QStringLiteral("rldcl") + (rc ? QStringLiteral(".") : QStringLiteral("")));
                result.mnemonic = mne;
                result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                                  QString::number(rd) + QStringLiteral(", r") + QString::number(rb) +
                                  QStringLiteral(", ") + QString::number(mb);
            } else {
                if (sub4 == 9) {
                    const auto rb = bits(insn, 16, 20);
                    auto me = (bits(insn, 21, 25) | (bit(insn, 26) << 5));
                    auto mne = (QStringLiteral("rldcr") + (rc ? QStringLiteral(".") : QStringLiteral("")));
                    result.mnemonic = mne;
                    result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                                      QString::number(rd) + QStringLiteral(", r") + QString::number(rb) +
                                      QStringLiteral(", ") + QString::number(me);
                } else {
                    auto sh = (bits(insn, 16, 20) | (bit(insn, 30) << 5));
                    auto mb = (bits(insn, 21, 25) | (bit(insn, 26) << 5));
                    result.mnemonic = QStringLiteral("rld??");
                    result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                                      QString::number(rd) + QStringLiteral(", ") + QString::number(sh) +
                                      QStringLiteral(", ") + QString::number(mb);
                }
            }
        }
        return result;
    }
    // Extended opcode 31 (X-form, XO-form, XFX-form)
    if (opcd == 31) {
        auto xo_full = bits(insn, 21, 30);
        auto xo_9 = bits(insn, 22, 30);
        const auto rb = bits(insn, 16, 20);
        const auto rc = bit(insn, 31);
        const auto oe = bit(insn, 21);
        // --- XO-form integer arithmetic (xo_9) ---
        // Instructions with only 2 register operands (rD, rA — no rB)
        static const QHash<int, QString> xo_arith_2op{{202, QStringLiteral("addze")},
                                                      {234, QStringLiteral("addme")},
                                                      {200, QStringLiteral("subfze")},
                                                      {232, QStringLiteral("subfme")},
                                                      {104, QStringLiteral("neg")}};
        // Instructions with 3 register operands (rD, rA, rB)
        static const QHash<int, QString> xo_arith_3op{
            {266, QStringLiteral("add")},   {10, QStringLiteral("addc")},   {138, QStringLiteral("adde")},
            {40, QStringLiteral("subf")},   {8, QStringLiteral("subfc")},   {136, QStringLiteral("subfe")},
            {235, QStringLiteral("mullw")}, {75, QStringLiteral("mulhw")},  {11, QStringLiteral("mulhwu")},
            {491, QStringLiteral("divw")},  {459, QStringLiteral("divwu")}, {233, QStringLiteral("mulld")},
            {73, QStringLiteral("mulhd")},  {9, QStringLiteral("mulhdu")},  {489, QStringLiteral("divd")},
            {457, QStringLiteral("divdu")}};
        if (xo_arith_2op.contains(xo_9)) {
            auto mne = xo_arith_2op.value(xo_9);
            if (oe) {
                mne += QStringLiteral("o");
            }
            if (rc) {
                mne += QStringLiteral(".");
            }
            result.mnemonic = mne;
            result.operands =
                QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") + QString::number(ra);
            return result;
        }
        if (xo_arith_3op.contains(xo_9)) {
            auto mne = xo_arith_3op.value(xo_9);
            if (oe) {
                mne += QStringLiteral("o");
            }
            if (rc) {
                mne += QStringLiteral(".");
            }
            result.mnemonic = mne;
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        // --- X-form logical ---
        static const QHash<int, QString> x_logical{
            {28, QStringLiteral("and")},    {444, QStringLiteral("or")},    {316, QStringLiteral("xor")},
            {476, QStringLiteral("nand")},  {124, QStringLiteral("nor")},   {284, QStringLiteral("eqv")},
            {60, QStringLiteral("andc")},   {412, QStringLiteral("orc")},   {954, QStringLiteral("extsb")},
            {922, QStringLiteral("extsh")}, {986, QStringLiteral("extsw")}, {26, QStringLiteral("cntlzw")},
            {58, QStringLiteral("cntlzd")}};
        if (x_logical.contains(xo_full)) {
            auto mne = x_logical.value(xo_full);
            if (rc) {
                mne += QStringLiteral(".");
            }
            result.mnemonic = mne;
            if (oneOf(xo_full, {954, 922, 986, 26, 58})) {
                result.operands =
                    QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") + QString::number(rd);
            } else {
                result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                                  QString::number(rd) + QStringLiteral(", r") + QString::number(rb);
            }
            return result;
        }
        // --- Shifts ---
        static const QHash<int, QString> x_shift{
            {24, QStringLiteral("slw")},    {536, QStringLiteral("srw")},   {792, QStringLiteral("sraw")},
            {824, QStringLiteral("srawi")}, {27, QStringLiteral("sld")},    {539, QStringLiteral("srd")},
            {794, QStringLiteral("srad")},  {826, QStringLiteral("sradi")}, {827, QStringLiteral("sradi")}};
        if (x_shift.contains(xo_full)) {
            auto mne = x_shift.value(xo_full);
            if (rc) {
                mne += QStringLiteral(".");
            }
            result.mnemonic = mne;
            if (oneOf(xo_full, {824, 826, 827})) {
                auto sh = (rb + ((xo_full == 827) ? 32 : 0));
                result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                                  QString::number(rd) + QStringLiteral(", ") + QString::number(sh);
            } else {
                result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                                  QString::number(rd) + QStringLiteral(", r") + QString::number(rb);
            }
            return result;
        }
        // --- Compare ---
        if (xo_full == 0) {
            auto bf = bits(insn, 6, 8);
            auto l_bit = bit(insn, 10);
            auto mne = (l_bit ? QStringLiteral("cmpd") : QStringLiteral("cmpw"));
            auto cr_str =
                (bf ? QStringLiteral("cr") + QString::number(bf) + QStringLiteral(", ") : QStringLiteral(""));
            result.mnemonic = mne;
            result.operands = cr_str + QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                              QString::number(rb);
            return result;
        }
        if (xo_full == 32) {
            auto bf = bits(insn, 6, 8);
            auto l_bit = bit(insn, 10);
            auto mne = (l_bit ? QStringLiteral("cmpld") : QStringLiteral("cmplw"));
            auto cr_str =
                (bf ? QStringLiteral("cr") + QString::number(bf) + QStringLiteral(", ") : QStringLiteral(""));
            result.mnemonic = mne;
            result.operands = cr_str + QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                              QString::number(rb);
            return result;
        }
        // --- Load/store indexed ---
        static const QHash<int, QPair<QString, bool>> x_ldst{
            {23, {QStringLiteral("lwzx"), false}},   {55, {QStringLiteral("lwzux"), false}},
            {87, {QStringLiteral("lbzx"), false}},   {119, {QStringLiteral("lbzux"), false}},
            {151, {QStringLiteral("stwx"), false}},  {183, {QStringLiteral("stwux"), false}},
            {215, {QStringLiteral("stbx"), false}},  {247, {QStringLiteral("stbux"), false}},
            {279, {QStringLiteral("lhzx"), false}},  {311, {QStringLiteral("lhzux"), false}},
            {343, {QStringLiteral("lhax"), false}},  {375, {QStringLiteral("lhaux"), false}},
            {407, {QStringLiteral("sthx"), false}},  {439, {QStringLiteral("sthux"), false}},
            {21, {QStringLiteral("ldx"), false}},    {53, {QStringLiteral("ldux"), false}},
            {149, {QStringLiteral("stdx"), false}},  {181, {QStringLiteral("stdux"), false}},
            {20, {QStringLiteral("lwarx"), false}},  {150, {QStringLiteral("stwcx."), true}},
            {84, {QStringLiteral("ldarx"), false}},  {214, {QStringLiteral("stdcx."), true}},
            {535, {QStringLiteral("lfsx"), false}},  {567, {QStringLiteral("lfsux"), false}},
            {599, {QStringLiteral("lfdx"), false}},  {631, {QStringLiteral("lfdux"), false}},
            {663, {QStringLiteral("stfsx"), false}}, {695, {QStringLiteral("stfsux"), false}},
            {727, {QStringLiteral("stfdx"), false}}, {759, {QStringLiteral("stfdux"), false}},
            {983, {QStringLiteral("stfiwx"), false}}};
        if (x_ldst.contains(xo_full)) {
            auto [mne, has_dot] = x_ldst.value(xo_full);
            result.mnemonic = mne;
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        // --- SPR operations ---
        if (xo_full == 339) {
            auto spr_raw = bits(insn, 11, 20);
            auto spr = (((spr_raw & 0x1F) << 5) | ((spr_raw >> 5) & 0x1F));
            auto spr_name = SPR_NAMES.value(spr, QStringLiteral("spr") + QString::number(spr));
            if (spr == 8) {
                result.mnemonic = QStringLiteral("mflr");
                result.operands = QStringLiteral("r") + QString::number(rd);
            } else {
                if (spr == 9) {
                    result.mnemonic = QStringLiteral("mfctr");
                    result.operands = QStringLiteral("r") + QString::number(rd);
                } else {
                    result.mnemonic = QStringLiteral("mfspr");
                    result.operands =
                        QStringLiteral("r") + QString::number(rd) + QStringLiteral(", ") + spr_name;
                }
            }
            return result;
        }
        if (xo_full == 467) {
            auto spr_raw = bits(insn, 11, 20);
            auto spr = (((spr_raw & 0x1F) << 5) | ((spr_raw >> 5) & 0x1F));
            auto spr_name = SPR_NAMES.value(spr, QStringLiteral("spr") + QString::number(spr));
            if (spr == 8) {
                result.mnemonic = QStringLiteral("mtlr");
                result.operands = QStringLiteral("r") + QString::number(rd);
            } else {
                if (spr == 9) {
                    result.mnemonic = QStringLiteral("mtctr");
                    result.operands = QStringLiteral("r") + QString::number(rd);
                } else {
                    result.mnemonic = QStringLiteral("mtspr");
                    result.operands = spr_name + QStringLiteral(", r") + QString::number(rd);
                }
            }
            return result;
        }
        // mfcr, mtcrf
        if (xo_full == 19) {
            result.mnemonic = QStringLiteral("mfcr");
            result.operands = QStringLiteral("r") + QString::number(rd);
            return result;
        }
        if (xo_full == 144) {
            auto crm = bits(insn, 12, 19);
            if (crm == 0xFF) {
                result.mnemonic = QStringLiteral("mtcr");
                result.operands = QStringLiteral("r") + QString::number(rd);
            } else {
                result.mnemonic = QStringLiteral("mtcrf");
                result.operands =
                    QStringLiteral("0x") + hex32(crm, 2) + QStringLiteral(", r") + QString::number(rd);
            }
            return result;
        }
        // --- Sync / cache / misc ---
        // XO 598 is three different barriers, told apart by the 2-bit L field
        // (bits 9..10): 0 is the heavyweight sync, 1 lwsync, 2 ptesync. Naming
        // all three "sync" cost nothing while the lifter treated them as
        // no-ops, but it does now that they lower to host fences: every
        // lightweight barrier a game emits would take a full one, and the
        // lifter's lwsync arm would be unreachable. objdump spells them apart
        // too, so this also keeps the disassembly audit honest.
        if (xo_full == 598) {
            result.mnemonic = (bits(insn, 9, 10) == 1 ? QStringLiteral("lwsync")
                                                      : (bits(insn, 9, 10) == 2 ? QStringLiteral("ptesync")
                                                                                : QStringLiteral("sync")));
            return result;
        }
        static const QHash<int, QString> misc_x{
            {854, QStringLiteral("eieio")}, {278, QStringLiteral("dcbt")}, {246, QStringLiteral("dcbtst")},
            {86, QStringLiteral("dcbf")},   {54, QStringLiteral("dcbst")}, {982, QStringLiteral("icbi")},
            {758, QStringLiteral("dcba")},  {1014, QStringLiteral("dcbz")}};
        if (misc_x.contains(xo_full)) {
            result.mnemonic = misc_x.value(xo_full);
            if (oneOf(xo_full, {278, 246, 86, 54, 982, 758, 1014})) {
                result.operands =
                    QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            }
            return result;
        }
        // twi trap
        if (xo_full == 4) {
            result.mnemonic = QStringLiteral("tw");
            result.operands = QString::number(rd) + QStringLiteral(", r") + QString::number(ra) +
                              QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        // --- Byte-reverse loads/stores ---
        if (xo_full == 534) {
            result.mnemonic = QStringLiteral("lwbrx");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 662) {
            result.mnemonic = QStringLiteral("stwbrx");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 790) {
            result.mnemonic = QStringLiteral("lhbrx");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 918) {
            result.mnemonic = QStringLiteral("sthbrx");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        // NOTE: XO 827 is sradi with shift >= 32 (handled in the x_shift block
        // above). The real lhaux is XO 375, decoded in the algebraic table below.
        // --- Load/store algebraic ---
        if (xo_full == 341) {
            result.mnemonic = QStringLiteral("lwax");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 373) {
            result.mnemonic = QStringLiteral("lwaux");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        // --- Move from/to timebase ---
        if (xo_full == 371) {
            auto tbr_raw = bits(insn, 11, 20);
            auto tbr = (((tbr_raw & 0x1F) << 5) | ((tbr_raw >> 5) & 0x1F));
            if (tbr == 268) {
                result.mnemonic = QStringLiteral("mftb");
            } else {
                if (tbr == 269) {
                    result.mnemonic = QStringLiteral("mftbu");
                } else {
                    result.mnemonic = QStringLiteral("mftb");
                }
            }
            result.operands = QStringLiteral("r") + QString::number(rd);
            return result;
        }
        // --- String word load/store ---
        if (xo_full == 597) {
            auto nb = rb;
            result.mnemonic = QStringLiteral("lswi");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", ") + QString::number(nb);
            return result;
        }
        if (xo_full == 725) {
            auto nb = rb;
            result.mnemonic = QStringLiteral("stswi");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", ") + QString::number(nb);
            return result;
        }
        // --- VMX scalar helpers ---
        if (xo_full == 6) {
            result.mnemonic = QStringLiteral("lvsl");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 38) {
            result.mnemonic = QStringLiteral("lvsr");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        // VMX vector load/store (opcode 31, X-form)
        if (xo_full == 103) {
            result.mnemonic = QStringLiteral("lvx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 231) {
            result.mnemonic = QStringLiteral("stvx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 7) {
            result.mnemonic = QStringLiteral("lvebx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 39) {
            result.mnemonic = QStringLiteral("lvehx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 71) {
            result.mnemonic = QStringLiteral("lvewx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 135) {
            result.mnemonic = QStringLiteral("stvebx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 167) {
            result.mnemonic = QStringLiteral("stvehx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 199) {
            result.mnemonic = QStringLiteral("stvewx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 359) {
            result.mnemonic = QStringLiteral("lvxl");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 487) {
            result.mnemonic = QStringLiteral("stvxl");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 6) {
            result.mnemonic = QStringLiteral("lvsl");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 38) {
            result.mnemonic = QStringLiteral("lvsr");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 519) {
            result.mnemonic = QStringLiteral("lvlx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 551) {
            result.mnemonic = QStringLiteral("lvrx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 775) {
            result.mnemonic = QStringLiteral("lvlxl");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 807) {
            result.mnemonic = QStringLiteral("lvrxl");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 660) {
            result.mnemonic = QStringLiteral("stdbrx");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 533) {
            result.mnemonic = QStringLiteral("lswx");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 661) {
            result.mnemonic = QStringLiteral("stswx");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 68) {
            result.mnemonic = QStringLiteral("td");
            result.operands = QString::number(rd) + QStringLiteral(", r") + QString::number(ra) +
                              QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (oneOf(xo_full, {342, 374})) {
            result.mnemonic = ((xo_full == 342) ? QStringLiteral("dst") : QStringLiteral("dstst"));
            result.operands = QStringLiteral("r") + QString::number(ra) + QStringLiteral(", r") +
                              QString::number(rb) + QStringLiteral(", ") + QString::number((rd & 3));
            return result;
        }
        if (xo_full == 822) {
            result.mnemonic = QStringLiteral("dss");
            result.operands = QString::number((rd & 3));
            return result;
        }
        if (xo_full == 532) {
            result.mnemonic = QStringLiteral("ldbrx");
            result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        // Cell unaligned vector STORES (were previously undecoded, so any
        // vector store using them fell through to the op31_x fallback and got
        // lifted as a TODO no-op -- a silent memory non-write, not just a
        // wrong value).
        if (xo_full == 647) {
            result.mnemonic = QStringLiteral("stvlx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 679) {
            result.mnemonic = QStringLiteral("stvrx");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 903) {
            result.mnemonic = QStringLiteral("stvlxl");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        if (xo_full == 935) {
            result.mnemonic = QStringLiteral("stvrxl");
            result.operands = QStringLiteral("v") + QString::number(rd) + QStringLiteral(", r") +
                              QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
            return result;
        }
        // Fall through – unknown ext opcode 31
        result.mnemonic = QStringLiteral("op31_x") + QString::number(xo_full);
        result.operands = QStringLiteral("r") + QString::number(rd) + QStringLiteral(", r") +
                          QString::number(ra) + QStringLiteral(", r") + QString::number(rb);
        return result;
    }
    // Extended opcode 63 (floating-point)
    if (opcd == 63) {
        auto xo_full = bits(insn, 21, 30);
        auto xo_5 = bits(insn, 26, 30);
        auto frd = rd;
        auto fra = ra;
        auto frb = bits(insn, 16, 20);
        auto frc = bits(insn, 21, 25);
        const auto rc = bit(insn, 31);
        static const QHash<int, QString> fp_a{
            {21, QStringLiteral("fadd")},   {20, QStringLiteral("fsub")},    {25, QStringLiteral("fmul")},
            {18, QStringLiteral("fdiv")},   {29, QStringLiteral("fmadd")},   {28, QStringLiteral("fmsub")},
            {31, QStringLiteral("fnmadd")}, {30, QStringLiteral("fnmsub")},  {23, QStringLiteral("fsel")},
            {22, QStringLiteral("fsqrt")},  {26, QStringLiteral("frsqrte")}, {24, QStringLiteral("fre")}};
        if (fp_a.contains(xo_5)) {
            auto mne = fp_a.value(xo_5);
            if (rc) {
                mne += QStringLiteral(".");
            }
            result.mnemonic = mne;
            if (xo_5 == 25) {
                result.operands = QStringLiteral("f") + QString::number(frd) + QStringLiteral(", f") +
                                  QString::number(fra) + QStringLiteral(", f") + QString::number(frc);
            } else {
                if (oneOf(xo_5, {29, 28, 31, 30})) {
                    result.operands = QStringLiteral("f") + QString::number(frd) + QStringLiteral(", f") +
                                      QString::number(fra) + QStringLiteral(", f") + QString::number(frc) +
                                      QStringLiteral(", f") + QString::number(frb);
                } else {
                    if (xo_5 == 23) {
                        result.operands = QStringLiteral("f") + QString::number(frd) + QStringLiteral(", f") +
                                          QString::number(fra) + QStringLiteral(", f") +
                                          QString::number(frc) + QStringLiteral(", f") + QString::number(frb);
                    } else {
                        if (oneOf(xo_5, {26, 24, 22})) {
                            result.operands = QStringLiteral("f") + QString::number(frd) +
                                              QStringLiteral(", f") + QString::number(frb);
                        } else {
                            result.operands = QStringLiteral("f") + QString::number(frd) +
                                              QStringLiteral(", f") + QString::number(fra) +
                                              QStringLiteral(", f") + QString::number(frb);
                        }
                    }
                }
            }
            return result;
        }
        static const QHash<int, QString> fp_x{
            {0, QStringLiteral("fcmpu")},    {32, QStringLiteral("fcmpo")},  {12, QStringLiteral("frsp")},
            {14, QStringLiteral("fctiw")},   {15, QStringLiteral("fctiwz")}, {814, QStringLiteral("fctid")},
            {815, QStringLiteral("fctidz")}, {846, QStringLiteral("fcfid")}, {40, QStringLiteral("fneg")},
            {72, QStringLiteral("fmr")},     {264, QStringLiteral("fabs")},  {136, QStringLiteral("fnabs")},
            {64, QStringLiteral("mcrfs")},   {583, QStringLiteral("mffs")},  {711, QStringLiteral("mtfsf")},
            {70, QStringLiteral("mtfsb0")},  {38, QStringLiteral("mtfsb1")}, {134, QStringLiteral("mtfsfi")}};
        if (fp_x.contains(xo_full)) {
            auto mne = fp_x.value(xo_full);
            if (rc) {
                mne += QStringLiteral(".");
            }
            result.mnemonic = mne;
            if (oneOf(xo_full, {0, 32})) {
                auto bf = bits(insn, 6, 8);
                result.operands = QStringLiteral("cr") + QString::number(bf) + QStringLiteral(", f") +
                                  QString::number(fra) + QStringLiteral(", f") + QString::number(frb);
            } else {
                if (oneOf(xo_full, {40, 72, 264, 136})) {
                    result.operands = QStringLiteral("f") + QString::number(frd) + QStringLiteral(", f") +
                                      QString::number(frb);
                } else {
                    if (xo_full == 583) {
                        result.operands = QStringLiteral("f") + QString::number(frd);
                    } else {
                        if (xo_full == 711) {
                            auto fm = bits(insn, 7, 14);
                            result.operands = QStringLiteral("0x") + hex32(fm, 2) + QStringLiteral(", f") +
                                              QString::number(frb);
                        } else {
                            result.operands = QStringLiteral("f") + QString::number(frd) +
                                              QStringLiteral(", f") + QString::number(frb);
                        }
                    }
                }
            }
            return result;
        }
    }
    // Extended opcode 59 (single-precision FP)
    if (opcd == 59) {
        auto xo_5 = bits(insn, 26, 30);
        auto frd = rd;
        auto fra = ra;
        auto frb = bits(insn, 16, 20);
        auto frc = bits(insn, 21, 25);
        const auto rc = bit(insn, 31);
        static const QHash<int, QString> fps{
            {21, QStringLiteral("fadds")},   {20, QStringLiteral("fsubs")},   {25, QStringLiteral("fmuls")},
            {18, QStringLiteral("fdivs")},   {29, QStringLiteral("fmadds")},  {28, QStringLiteral("fmsubs")},
            {31, QStringLiteral("fnmadds")}, {30, QStringLiteral("fnmsubs")}, {22, QStringLiteral("fsqrts")},
            {24, QStringLiteral("fres")},    {26, QStringLiteral("frsqrtes")}};
        if (fps.contains(xo_5)) {
            auto mne = fps.value(xo_5);
            if (rc) {
                mne += QStringLiteral(".");
            }
            result.mnemonic = mne;
            if (xo_5 == 25) {
                result.operands = QStringLiteral("f") + QString::number(frd) + QStringLiteral(", f") +
                                  QString::number(fra) + QStringLiteral(", f") + QString::number(frc);
            } else {
                if (oneOf(xo_5, {29, 28, 31, 30})) {
                    result.operands = QStringLiteral("f") + QString::number(frd) + QStringLiteral(", f") +
                                      QString::number(fra) + QStringLiteral(", f") + QString::number(frc) +
                                      QStringLiteral(", f") + QString::number(frb);
                } else {
                    if (oneOf(xo_5, {24, 26, 22})) {
                        result.operands = QStringLiteral("f") + QString::number(frd) + QStringLiteral(", f") +
                                          QString::number(frb);
                    } else {
                        result.operands = QStringLiteral("f") + QString::number(frd) + QStringLiteral(", f") +
                                          QString::number(fra) + QStringLiteral(", f") + QString::number(frb);
                    }
                }
            }
            return result;
        }
    }
    // VMX / AltiVec (opcd 4)
    if (opcd == 4) {
        auto vd = rd;
        auto va = ra;
        auto vb = bits(insn, 16, 20);
        auto vc = bits(insn, 21, 25);
        // AltiVec VX-form has an 11-bit XO at bits 21-31; extract the full 11 bits.
        // Bit 21 doubles as the Rc flag for compare (VC-form) instructions.
        auto xo_full = bits(insn, 21, 31);
        auto xo_6 = bits(insn, 26, 31);
        // VA-form (6-bit xo at bits 26-31). XOs corrected per kakaroto/ps3ida
        // PPCAltivec (VXA macro) — the 36-45 range was previously scrambled,
        // mislabeling vperm/vsel/vsldoi and the whole vmsum* family.
        static const QHash<int, QString> vmx_va{
            {32, QStringLiteral("vmhaddshs")}, {33, QStringLiteral("vmhraddshs")},
            {34, QStringLiteral("vmladduhm")}, {36, QStringLiteral("vmsumubm")},
            {37, QStringLiteral("vmsummbm")},  {38, QStringLiteral("vmsumuhm")},
            {39, QStringLiteral("vmsumuhs")},  {40, QStringLiteral("vmsumshm")},
            {41, QStringLiteral("vmsumshs")},  {42, QStringLiteral("vsel")},
            {43, QStringLiteral("vperm")},     {44, QStringLiteral("vsldoi")},
            {46, QStringLiteral("vmaddfp")},   {47, QStringLiteral("vnmsubfp")}};
        if (vmx_va.contains(xo_6)) {
            result.mnemonic = vmx_va.value(xo_6);
            if (xo_6 == 44) {
                result.operands = QStringLiteral("v") + QString::number(vd) + QStringLiteral(", v") +
                                  QString::number(va) + QStringLiteral(", v") + QString::number(vb) +
                                  QStringLiteral(", ") + QString::number(vc);
            } else {
                result.operands = QStringLiteral("v") + QString::number(vd) + QStringLiteral(", v") +
                                  QString::number(va) + QStringLiteral(", v") + QString::number(vb) +
                                  QStringLiteral(", v") + QString::number(vc);
            }
            return result;
        }
        // VX-form: xo_full is now 11 bits (bits 21-31).
        // For compare (VC-form) instructions, bit 21 is Rc; XO_10 is bits 22-31.
        auto vmx_cmp_rc = bit(insn, 21);
        static const QHash<int, QString> vmx_cmp{
            {6, QStringLiteral("vcmpequb")},   {70, QStringLiteral("vcmpequh")},
            {134, QStringLiteral("vcmpequw")}, {198, QStringLiteral("vcmpeqfp")},
            {966, QStringLiteral("vcmpbfp")},  {454, QStringLiteral("vcmpgefp")},
            {710, QStringLiteral("vcmpgtfp")}, {774, QStringLiteral("vcmpgtsb")},
            {838, QStringLiteral("vcmpgtsh")}, {902, QStringLiteral("vcmpgtsw")},
            {518, QStringLiteral("vcmpgtub")}, {582, QStringLiteral("vcmpgtuh")},
            {646, QStringLiteral("vcmpgtuw")}};
        // Strip Rc (bit 10 of the 11-bit xo_full) to get the base 10-bit XO
        auto base_cmp_xo = (xo_full & (~(1 << 10)));
        if (vmx_cmp.contains(base_cmp_xo)) {
            auto mne = vmx_cmp.value(base_cmp_xo);
            if (vmx_cmp_rc) {
                mne += QStringLiteral(".");
            }
            result.mnemonic = mne;
            result.operands = QStringLiteral("v") + QString::number(vd) + QStringLiteral(", v") +
                              QString::number(va) + QStringLiteral(", v") + QString::number(vb);
            return result;
        }
        // Non-compare VX instructions (full 11-bit xo, no Rc ambiguity)
        static const QHash<int, QString> vmx_vx{
            {10, QStringLiteral("vaddfp")},     {74, QStringLiteral("vsubfp")},
            {1034, QStringLiteral("vmaxfp")},   {1098, QStringLiteral("vminfp")},
            {266, QStringLiteral("vrefp")},     {330, QStringLiteral("vrsqrtefp")},
            {394, QStringLiteral("vexptefp")},  {458, QStringLiteral("vlogefp")},
            {522, QStringLiteral("vrfin")},     {586, QStringLiteral("vrfiz")},
            {650, QStringLiteral("vrfip")},     {714, QStringLiteral("vrfim")},
            {0, QStringLiteral("vaddubm")},     {64, QStringLiteral("vadduhm")},
            {128, QStringLiteral("vadduwm")},   {1024, QStringLiteral("vsububm")},
            {1088, QStringLiteral("vsubuhm")},  {1152, QStringLiteral("vsubuwm")},
            {512, QStringLiteral("vaddubs")},   {576, QStringLiteral("vadduhs")},
            {640, QStringLiteral("vadduws")},   {768, QStringLiteral("vaddsbs")},
            {832, QStringLiteral("vaddshs")},   {896, QStringLiteral("vaddsws")},
            {1536, QStringLiteral("vsububs")},  {1600, QStringLiteral("vsubuhs")},
            {1664, QStringLiteral("vsubuws")},  {1792, QStringLiteral("vsubsbs")},
            {1856, QStringLiteral("vsubshs")},  {1920, QStringLiteral("vsubsws")},
            {2, QStringLiteral("vmaxub")},      {66, QStringLiteral("vmaxuh")},
            {130, QStringLiteral("vmaxuw")},    {258, QStringLiteral("vmaxsb")},
            {322, QStringLiteral("vmaxsh")},    {386, QStringLiteral("vmaxsw")},
            {514, QStringLiteral("vminub")},    {578, QStringLiteral("vminuh")},
            {642, QStringLiteral("vminuw")},    {770, QStringLiteral("vminsb")},
            {834, QStringLiteral("vminsh")},    {898, QStringLiteral("vminsw")},
            {1028, QStringLiteral("vand")},     {1092, QStringLiteral("vandc")},
            {1156, QStringLiteral("vor")},      {1220, QStringLiteral("vxor")},
            {1284, QStringLiteral("vnor")},     {260, QStringLiteral("vslb")},
            {324, QStringLiteral("vslh")},      {388, QStringLiteral("vslw")},
            {516, QStringLiteral("vsrb")},      {580, QStringLiteral("vsrh")},
            {644, QStringLiteral("vsrw")},      {772, QStringLiteral("vsrab")},
            {836, QStringLiteral("vsrah")},     {900, QStringLiteral("vsraw")},
            {12, QStringLiteral("vmrghb")},     {76, QStringLiteral("vmrghh")},
            {140, QStringLiteral("vmrghw")},    {268, QStringLiteral("vmrglb")},
            {332, QStringLiteral("vmrglh")},    {396, QStringLiteral("vmrglw")},
            {14, QStringLiteral("vpkuhum")},    {78, QStringLiteral("vpkuwum")},
            {142, QStringLiteral("vpkuhus")},   {206, QStringLiteral("vpkuwus")},
            {270, QStringLiteral("vpkshus")},   {334, QStringLiteral("vpkswus")},
            {398, QStringLiteral("vpkshss")},   {462, QStringLiteral("vpkswss")},
            {526, QStringLiteral("vupkhsb")},   {590, QStringLiteral("vupkhsh")},
            {654, QStringLiteral("vupklsb")},   {718, QStringLiteral("vupklsh")},
            {4, QStringLiteral("vrlb")},        {68, QStringLiteral("vrlh")},
            {132, QStringLiteral("vrlw")},      {1026, QStringLiteral("vavgub")},
            {1090, QStringLiteral("vavguh")},   {1154, QStringLiteral("vavguw")},
            {1282, QStringLiteral("vavgsb")},   {1346, QStringLiteral("vavgsh")},
            {1410, QStringLiteral("vavgsw")},   {8, QStringLiteral("vmuloub")},
            {72, QStringLiteral("vmulouh")},    {264, QStringLiteral("vmulosb")},
            {328, QStringLiteral("vmulosh")},   {520, QStringLiteral("vmuleub")},
            {584, QStringLiteral("vmuleuh")},   {776, QStringLiteral("vmulesb")},
            {840, QStringLiteral("vmulesh")},   {1928, QStringLiteral("vsumsws")},
            {1672, QStringLiteral("vsum2sws")}, {1544, QStringLiteral("vsum4ubs")},
            {1800, QStringLiteral("vsum4sbs")}, {1608, QStringLiteral("vsum4shs")}};
        if (vmx_vx.contains(xo_full)) {
            result.mnemonic = vmx_vx.value(xo_full);
            result.operands = QStringLiteral("v") + QString::number(vd) + QStringLiteral(", v") +
                              QString::number(va) + QStringLiteral(", v") + QString::number(vb);
            return result;
        }
        // VX instructions whose vA field (bits 11-15) is an immediate, not a
        // register: splats (element index) and FP<->int converts (scale).
        // All emit "vD, vB, UIMM" so the lifter reads a bare integer at ops[2].
        static const QHash<int, QString> vmx_uimm{
            {524, QStringLiteral("vspltb")}, {588, QStringLiteral("vsplth")}, {652, QStringLiteral("vspltw")},
            {778, QStringLiteral("vcfux")},  {842, QStringLiteral("vcfsx")},  {906, QStringLiteral("vctuxs")},
            {970, QStringLiteral("vctsxs")}};
        if (vmx_uimm.contains(xo_full)) {
            result.mnemonic = vmx_uimm.value(xo_full);
            result.operands = QStringLiteral("v") + QString::number(vd) + QStringLiteral(", v") +
                              QString::number(vb) + QStringLiteral(", ") + QString::number(va);
            return result;
        }
        // vspltis* : vD, SIMM — the vA field (bits 11-15) is a 5-bit SIGNED
        // immediate (-16..15), NOT a register (unlike vspltb/h/w's UIMM index).
        static const QHash<int, QString> vmx_simm{{780, QStringLiteral("vspltisb")},
                                                  {844, QStringLiteral("vspltish")},
                                                  {908, QStringLiteral("vspltisw")}};
        if (vmx_simm.contains(xo_full)) {
            result.mnemonic = vmx_simm.value(xo_full);
            auto simm = ((va >= 16) ? (va - 32) : va);
            result.operands =
                QStringLiteral("v") + QString::number(vd) + QStringLiteral(", ") + QString::number(simm);
            return result;
        }
        // VSCR moves: mfvscr vD (only vD), mtvscr vB (only vB).
        if (xo_full == 1540) {
            result.mnemonic = QStringLiteral("mfvscr");
            result.operands = QStringLiteral("v") + QString::number(vd);
            return result;
        }
        if (xo_full == 1604) {
            result.mnemonic = QStringLiteral("mtvscr");
            result.operands = QStringLiteral("v") + QString::number(vb);
            return result;
        }
        // lvx / stvx (X-form under opcode 31 actually, but some are opcd 4)
        // Fallback for opcode 4
        result.mnemonic = QStringLiteral("vmx_x") + QString::number(xo_full);
        result.operands = QStringLiteral("v") + QString::number(vd) + QStringLiteral(", v") +
                          QString::number(va) + QStringLiteral(", v") + QString::number(vb);
        return result;
    }
    // lvx/stvx now handled inside the main opcode 31 block above
    // twi (3)
    if (opcd == 3) {
        auto to = rd;
        result.mnemonic = QStringLiteral("twi");
        result.operands = QString::number(to) + QStringLiteral(", r") + QString::number(ra) +
                          QStringLiteral(", ") + QString::number(si);
        return result;
    }
    // tdi (2)
    if (opcd == 2) {
        auto to = rd;
        result.mnemonic = QStringLiteral("tdi");
        result.operands = QString::number(to) + QStringLiteral(", r") + QString::number(ra) +
                          QStringLiteral(", ") + QString::number(si);
        return result;
    }
    // Unknown
    result.mnemonic = QStringLiteral(".word");
    result.operands = QStringLiteral("0x") + hex32(insn, 8);
    return result;
}
} // namespace prepare
