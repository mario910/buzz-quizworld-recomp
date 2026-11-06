#include "spu_lift.h"
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace prepare {
namespace {
QString hex(int value, int width = 0) {
    return QString::number(quint32(value), 16).toUpper().rightJustified(width, '0');
}
QStringList operandsOf(const QString &operands) {
    QStringList parts;
    for (const auto &part : operands.split(',')) {
        if (!part.trimmed().isEmpty())
            parts.append(part.trimmed());
    }
    return parts;
}
QString registerIndex(const QString &token) {
    return token.startsWith("$r") ? token.mid(2) : token;
}
QString g(const QString &index) {
    return "ctx->gpr[" + index + "]";
}
QString g(int index) {
    return g(QString::number(index));
}
QString immediateOperand(const QString &token) {
    if (token.startsWith("0x") || token.startsWith("-0x"))
        return token;
    bool ok = false;
    const int value = registerIndex(token).toInt(&ok);
    return ok ? QString::number(value) : QStringLiteral("0");
}
QString channelOperand(const QString &token) {
    return token.startsWith("ch") ? token.mid(2) : token;
}
QPair<QString, QString> displacementBase(const QString &token) {
    static const QRegularExpression pattern(R"(^(-?0x[0-9A-Fa-f]+|-?\d+)\(\$r(\d+)\))");
    const auto match = pattern.match(token);
    return {match.captured(1), match.captured(2)};
}
bool oneOf(const QString &value, std::initializer_list<QString> choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}
const QSet<QString> _COND_BR{"brz", "brnz", "brhz", "brhnz"};
const QSet<int> LV2_RESUMING_STOPS{0x100, 0x110, 0x111};
const QSet<QString> noRtWrite{
#include "spu_trace_exclusions.inc"
};
const QSet<QString> terminators{"br", "bra", "bi", "iret", "stop", "stopd"};
int branchTarget(const SpuInstruction &insn) {
    if (!_COND_BR.contains(insn.mnemonic) && !oneOf(insn.mnemonic, {"br", "brsl", "bra", "brasl"}))
        return -1;
    const auto token = insn.operands.section(',', -1).trimmed();
    bool ok = false;
    const int value = token.toInt(&ok, 16);
    return ok && token.startsWith("0x") ? value : -1;
}
QString branchCondition(const QString &mnemonic, const QString &reg) {
    const QString slot = mnemonic.contains('h') ? "._u16[0]" : "._u32[0]";
    return g(reg) + slot + (mnemonic.endsWith("nz") ? " != 0" : " == 0");
}
struct Emitter {
    QString prefix;
    QSet<int> computedReturns, linkReturns, callTargets, branchTargets;
    QHash<QString, int> unsupported;
    QString name(int addr) const { return prefix + "spu_func_" + hex(addr, 8); }
    QString unconditionalBranch(int target, const SpuBound &func) {
        if (target < 0)
            return "/* TODO spu: unresolved branch */;";
        if (func.start <= target && target < func.end)
            return "goto loc_" + hex(target, 8) + ";";
        branchTargets.insert(target);
        return "{ ctx->pc = 0x" + hex(target) + "; g_spu_trampoline_fn = " + name(target) + "; return; }";
    }
    QString lower(const SpuInstruction &insn, const SpuBound &func);
};

// Instruction lowering ported from the MIT-licensed PS3Recomp SDK.
QString Emitter::lower(const SpuInstruction &insn, const SpuBound &func) {
    const auto &mn = insn.mnemonic;
    const auto ops = operandsOf(insn.operands);
    const int addr = int(insn.address);
    auto rt = [&] { return registerIndex(ops[0]); };
    auto ra = [&] { return registerIndex(ops[1]); };
    auto rb = [&] { return registerIndex(ops[2]); };
    if (oneOf(mn, {QStringLiteral("nop"), QStringLiteral("lnop")})) {
        return QStringLiteral("/* nop */;");
    }
    if (oneOf(mn, {QStringLiteral("sync"), QStringLiteral("dsync"), QStringLiteral("syncc")})) {
        return QStringLiteral("spu_arch_fence();");
    }
    if (mn == QStringLiteral("fscrwr")) {
        return QStringLiteral("/* fscrwr: FP status/control write (no-op in recomp) */;");
    }
    if (mn == QStringLiteral("stop")) {
        auto code = int(insn.raw & 0x3fff);
        if (LV2_RESUMING_STOPS.contains(code)) {
            return QStringLiteral("ctx->stop_code = 0x") + hex(code) +
                   QStringLiteral("u; ctx->status = SPU_STATUS_STOPPED_BY_STOP; spu_stop(ctx); if "
                                  "(ctx->status != SPU_STATUS_RUNNING) return;");
        }
        return QStringLiteral("ctx->stop_code = 0x") + hex(code) +
               QStringLiteral("u; ctx->status = SPU_STATUS_STOPPED_BY_STOP; spu_stop(ctx); return;");
    }
    if (mn == QStringLiteral("il")) {
        return g(rt()) + QStringLiteral(" = spu_il(") + immediateOperand(ops[1]) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("ila")) {
        return g(rt()) + QStringLiteral(" = spu_ila(") + immediateOperand(ops[1]) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("ilh")) {
        return g(rt()) + QStringLiteral(" = spu_ilh(") + immediateOperand(ops[1]) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("ilhu")) {
        return g(rt()) + QStringLiteral(" = spu_ilhu(") + immediateOperand(ops[1]) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("iohl")) {
        return g(rt()) + QStringLiteral(" = spu_iohl(") + g(rt()) + QStringLiteral(", ") +
               immediateOperand(ops[1]) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("fsmbi")) {
        return g(rt()) + QStringLiteral(" = spu_fsmbi(") + immediateOperand(ops[1]) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("addx")) {
        return g(rt()) + QStringLiteral(" = spu_addx(") + g(ra()) + QStringLiteral(", ") + g(rb()) +
               QStringLiteral(", ") + g(rt()) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("bgx")) {
        return g(rt()) + QStringLiteral(" = spu_bgx(") + g(ra()) + QStringLiteral(", ") + g(rb()) +
               QStringLiteral(", ") + g(rt()) + QStringLiteral(");");
    }
    if (oneOf(mn, {QStringLiteral("hgti"), QStringLiteral("hlgti"), QStringLiteral("heqi"),
                   QStringLiteral("hgt"), QStringLiteral("hlgt"), QStringLiteral("heq")})) {
        auto pw = [&](const QString &i) { return g(i) + QStringLiteral("._u32[0]"); };
        QString cond;
        if (mn == "heqi")
            cond = QStringLiteral("(int32_t)") + pw(ra()) + QStringLiteral(" == (int32_t)(") +
                   immediateOperand(ops[2]) + QStringLiteral(")");
        else if (mn == "hgti")
            cond = QStringLiteral("(int32_t)") + pw(ra()) + QStringLiteral(" > (int32_t)(") +
                   immediateOperand(ops[2]) + QStringLiteral(")");
        else if (mn == "hlgti")
            cond =
                pw(ra()) + QStringLiteral(" > (uint32_t)(") + immediateOperand(ops[2]) + QStringLiteral(")");
        else if (mn == "heq")
            cond = pw(ra()) + QStringLiteral(" == ") + pw(rb());
        else if (mn == "hgt")
            cond = QStringLiteral("(int32_t)") + pw(ra()) + QStringLiteral(" > (int32_t)") + pw(rb());
        else
            cond = pw(ra()) + QStringLiteral(" > ") + pw(rb());
        return QStringLiteral("if (") + cond +
               QStringLiteral(
                   ") { extern void spu_halt(spu_context*); fprintf(stderr, \"[spu] HALT-ASSERT ") +
               mn + QStringLiteral(" pc=0x") + hex(addr, 5) +
               QStringLiteral(" img=%d\\n\", ctx->image_id); ctx->status = SPU_STATUS_STOPPED_BY_HALT; "
                              "spu_halt(ctx); return; }");
    }
    if (mn == QStringLiteral("stopd")) {
        return QStringLiteral(
            "ctx->stop_code = 0x3FFFu; ctx->status = SPU_STATUS_STOPPED_BY_STOP; spu_stop(ctx); return;");
    }
    static const QHash<QString, QString> rr_bin{
        {QStringLiteral("a"), QStringLiteral("spu_a")},
        {QStringLiteral("sf"), QStringLiteral("spu_sf")},
        {QStringLiteral("ah"), QStringLiteral("spu_ah")},
        {QStringLiteral("sfh"), QStringLiteral("spu_sfh")},
        {QStringLiteral("and"), QStringLiteral("spu_and")},
        {QStringLiteral("or"), QStringLiteral("spu_or")},
        {QStringLiteral("xor"), QStringLiteral("spu_xor")},
        {QStringLiteral("nand"), QStringLiteral("spu_nand")},
        {QStringLiteral("nor"), QStringLiteral("spu_nor")},
        {QStringLiteral("andc"), QStringLiteral("spu_andc")},
        {QStringLiteral("orc"), QStringLiteral("spu_orc")},
        {QStringLiteral("ceq"), QStringLiteral("spu_ceq")},
        {QStringLiteral("ceqh"), QStringLiteral("spu_ceqh")},
        {QStringLiteral("ceqb"), QStringLiteral("spu_ceqb")},
        {QStringLiteral("cgt"), QStringLiteral("spu_cgt")},
        {QStringLiteral("cgth"), QStringLiteral("spu_cgth")},
        {QStringLiteral("cgtb"), QStringLiteral("spu_cgtb")},
        {QStringLiteral("clgt"), QStringLiteral("spu_clgt")},
        {QStringLiteral("clgth"), QStringLiteral("spu_clgth")},
        {QStringLiteral("clgtb"), QStringLiteral("spu_clgtb")},
        {QStringLiteral("mpy"), QStringLiteral("spu_mpy")},
        {QStringLiteral("mpyu"), QStringLiteral("spu_mpyu")},
        {QStringLiteral("mpyh"), QStringLiteral("spu_mpyh")},
        {QStringLiteral("mpyhh"), QStringLiteral("spu_mpyhh")},
        {QStringLiteral("mpys"), QStringLiteral("spu_mpys")},
        {QStringLiteral("fa"), QStringLiteral("spu_fa")},
        {QStringLiteral("fs"), QStringLiteral("spu_fs")},
        {QStringLiteral("fm"), QStringLiteral("spu_fm")},
        {QStringLiteral("fi"), QStringLiteral("spu_fi")},
        {QStringLiteral("fceq"), QStringLiteral("spu_fceq")},
        {QStringLiteral("fcgt"), QStringLiteral("spu_fcgt")},
        {QStringLiteral("fcmeq"), QStringLiteral("spu_fcmeq")},
        {QStringLiteral("fcmgt"), QStringLiteral("spu_fcmgt")},
        {QStringLiteral("dfa"), QStringLiteral("spu_dfa")},
        {QStringLiteral("dfs"), QStringLiteral("spu_dfs")},
        {QStringLiteral("dfm"), QStringLiteral("spu_dfm")},
        {QStringLiteral("dfceq"), QStringLiteral("spu_dfceq")},
        {QStringLiteral("dfcmeq"), QStringLiteral("spu_dfcmeq")},
        {QStringLiteral("dfcgt"), QStringLiteral("spu_dfcgt")},
        {QStringLiteral("dfcmgt"), QStringLiteral("spu_dfcmgt")},
        {QStringLiteral("mpyhhu"), QStringLiteral("spu_mpyhhu")},
        {QStringLiteral("eqv"), QStringLiteral("spu_eqv")},
        {QStringLiteral("absdb"), QStringLiteral("spu_absdb")},
        {QStringLiteral("avgb"), QStringLiteral("spu_avgb")},
        {QStringLiteral("cg"), QStringLiteral("spu_cg")},
        {QStringLiteral("bg"), QStringLiteral("spu_bg")},
        {QStringLiteral("sumb"), QStringLiteral("spu_sumb")},
        {QStringLiteral("shl"), QStringLiteral("spu_shl")},
        {QStringLiteral("shlh"), QStringLiteral("spu_shlh")},
        {QStringLiteral("rot"), QStringLiteral("spu_rot")},
        {QStringLiteral("roth"), QStringLiteral("spu_roth")},
        {QStringLiteral("shlqbi"), QStringLiteral("spu_shlqbi")},
        {QStringLiteral("rotqbi"), QStringLiteral("spu_rotqbi")},
        {QStringLiteral("shlqby"), QStringLiteral("spu_shlqby")},
        {QStringLiteral("rotqby"), QStringLiteral("spu_rotqby")},
        {QStringLiteral("shlqbybi"), QStringLiteral("spu_shlqbybi")},
        {QStringLiteral("rotqbybi"), QStringLiteral("spu_rotqbybi")},
        {QStringLiteral("rotm"), QStringLiteral("spu_rotm")},
        {QStringLiteral("rotma"), QStringLiteral("spu_rotma")},
        {QStringLiteral("rothm"), QStringLiteral("spu_rothm")},
        {QStringLiteral("rotmah"), QStringLiteral("spu_rothma")},
        {QStringLiteral("rotqmbi"), QStringLiteral("spu_rotqmbi")},
        {QStringLiteral("rotqmby"), QStringLiteral("spu_rotqmby")},
        {QStringLiteral("rotqmbybi"), QStringLiteral("spu_rotqmbybi")},
        {QStringLiteral("cbx"), QStringLiteral("spu_cbx")},
        {QStringLiteral("chx"), QStringLiteral("spu_chx")},
        {QStringLiteral("cwx"), QStringLiteral("spu_cwx")},
        {QStringLiteral("cdx"), QStringLiteral("spu_cdx")},
    };
    if (rr_bin.contains(mn)) {
        return g(rt()) + QStringLiteral(" = ") + rr_bin[mn] + QStringLiteral("(") + g(ra()) +
               QStringLiteral(", ") + g(rb()) + QStringLiteral(");");
    }
    static const QHash<QString, QString> rr_un{
        {QStringLiteral("clz"), QStringLiteral("spu_clz")},
        {QStringLiteral("cntb"), QStringLiteral("spu_cntb")},
        {QStringLiteral("fscrrd"), QStringLiteral("spu_fscrrd")},
        {QStringLiteral("gb"), QStringLiteral("spu_gb")},
        {QStringLiteral("gbh"), QStringLiteral("spu_gbh")},
        {QStringLiteral("gbb"), QStringLiteral("spu_gbb")},
        {QStringLiteral("fesd"), QStringLiteral("spu_fesd")},
        {QStringLiteral("frds"), QStringLiteral("spu_frds")},
        {QStringLiteral("frsqest"), QStringLiteral("spu_frsqest")},
        {QStringLiteral("frest"), QStringLiteral("spu_frest")},
        {QStringLiteral("xsbh"), QStringLiteral("spu_xsbh")},
        {QStringLiteral("xshw"), QStringLiteral("spu_xshw")},
        {QStringLiteral("xswd"), QStringLiteral("spu_xswd")},
        {QStringLiteral("orx"), QStringLiteral("spu_orx")},
        {QStringLiteral("fsm"), QStringLiteral("spu_fsm")},
        {QStringLiteral("fsmh"), QStringLiteral("spu_fsmh")},
        {QStringLiteral("fsmb"), QStringLiteral("spu_fsmb")},
        {QStringLiteral("mfspr"), QStringLiteral("spu_mfspr")},
    };
    if (rr_un.contains(mn)) {
        return g(rt()) + QStringLiteral(" = ") + rr_un[mn] + QStringLiteral("(") + g(ra()) +
               QStringLiteral(");");
    }
    static const QHash<QString, QString> ri_imm{
        {QStringLiteral("ai"), QStringLiteral("spu_ai")},
        {QStringLiteral("ahi"), QStringLiteral("spu_ahi")},
        {QStringLiteral("sfi"), QStringLiteral("spu_sfi")},
        {QStringLiteral("sfhi"), QStringLiteral("spu_sfhi")},
        {QStringLiteral("andi"), QStringLiteral("spu_andi")},
        {QStringLiteral("ori"), QStringLiteral("spu_ori")},
        {QStringLiteral("xori"), QStringLiteral("spu_xori")},
        {QStringLiteral("ceqi"), QStringLiteral("spu_ceqi")},
        {QStringLiteral("cgti"), QStringLiteral("spu_cgti")},
        {QStringLiteral("clgti"), QStringLiteral("spu_clgti")},
        {QStringLiteral("ceqbi"), QStringLiteral("spu_ceqbi")},
        {QStringLiteral("ceqhi"), QStringLiteral("spu_ceqhi")},
        {QStringLiteral("clgtbi"), QStringLiteral("spu_clgtbi")},
        {QStringLiteral("clgthi"), QStringLiteral("spu_clgthi")},
        {QStringLiteral("cgthi"), QStringLiteral("spu_cgthi")},
        {QStringLiteral("cgtbi"), QStringLiteral("spu_cgtbi")},
        {QStringLiteral("mpyi"), QStringLiteral("spu_mpyi")},
        {QStringLiteral("mpyui"), QStringLiteral("spu_mpyui")},
        {QStringLiteral("andhi"), QStringLiteral("spu_andhi")},
        {QStringLiteral("andbi"), QStringLiteral("spu_andbi")},
        {QStringLiteral("orhi"), QStringLiteral("spu_orhi")},
        {QStringLiteral("orbi"), QStringLiteral("spu_orbi")},
        {QStringLiteral("xorhi"), QStringLiteral("spu_xorhi")},
        {QStringLiteral("xorbi"), QStringLiteral("spu_xorbi")},
        {QStringLiteral("dftsv"), QStringLiteral("spu_dftsv")},
        {QStringLiteral("cflts"), QStringLiteral("spu_cflts")},
        {QStringLiteral("cfltu"), QStringLiteral("spu_cfltu")},
        {QStringLiteral("csflt"), QStringLiteral("spu_csflt")},
        {QStringLiteral("cuflt"), QStringLiteral("spu_cuflt")},
    };
    if (ri_imm.contains(mn)) {
        return g(rt()) + QStringLiteral(" = ") + ri_imm[mn] + QStringLiteral("(") + g(ra()) +
               QStringLiteral(", ") + immediateOperand(ops[2]) + QStringLiteral(");");
    }
    static const QHash<QString, QString> sh_imm{
        {QStringLiteral("shli"), QStringLiteral("spu_shli")},
        {QStringLiteral("shlhi"), QStringLiteral("spu_shlhi")},
        {QStringLiteral("roti"), QStringLiteral("spu_roti")},
        {QStringLiteral("rothi"), QStringLiteral("spu_rothi")},
        {QStringLiteral("rotmi"), QStringLiteral("spu_rotmi")},
        {QStringLiteral("rotmai"), QStringLiteral("spu_rotmai")},
        {QStringLiteral("rotmhi"), QStringLiteral("spu_rotmhi")},
        {QStringLiteral("rotmahi"), QStringLiteral("spu_rotmahi")},
        {QStringLiteral("shlqbyi"), QStringLiteral("spu_shlqbyi")},
        {QStringLiteral("rotqbyi"), QStringLiteral("spu_rotqbyi")},
        {QStringLiteral("shlqbii"), QStringLiteral("spu_shlqbii")},
        {QStringLiteral("rotqbii"), QStringLiteral("spu_rotqbii")},
        {QStringLiteral("rothmi"), QStringLiteral("spu_rothmi")},
        {QStringLiteral("rotqmbii"), QStringLiteral("spu_rotqmbii")},
        {QStringLiteral("rotqmbyi"), QStringLiteral("spu_rotqmbyi")},
        {QStringLiteral("cbd"), QStringLiteral("spu_cbd")},
        {QStringLiteral("chd"), QStringLiteral("spu_chd")},
        {QStringLiteral("cwd"), QStringLiteral("spu_cwd")},
        {QStringLiteral("cdd"), QStringLiteral("spu_cdd")},
    };
    if (sh_imm.contains(mn)) {
        auto tok = ops[2];
        if (tok.startsWith("$r")) {
            tok = tok.mid(2);
        } else {
            tok = immediateOperand(tok);
        }
        return g(rt()) + QStringLiteral(" = ") + sh_imm[mn] + QStringLiteral("(") + g(ra()) +
               QStringLiteral(", ") + tok + QStringLiteral(");");
    }
    if (mn == QStringLiteral("selb")) {
        auto rc = registerIndex(ops[3]);
        return g(rt()) + QStringLiteral(" = spu_selb(") + g(ra()) + QStringLiteral(", ") + g(rb()) +
               QStringLiteral(", ") + g(rc) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("shufb")) {
        auto rc = registerIndex(ops[3]);
        return g(rt()) + QStringLiteral(" = spu_shufb(") + g(ra()) + QStringLiteral(", ") + g(rb()) +
               QStringLiteral(", ") + g(rc) + QStringLiteral(");");
    }
    if (oneOf(mn, {QStringLiteral("fma"), QStringLiteral("fms"), QStringLiteral("fnms"),
                   QStringLiteral("mpya")})) {
        auto rc = registerIndex(ops[3]);
        return g(rt()) + QStringLiteral(" = spu_") + mn + QStringLiteral("(") + g(ra()) +
               QStringLiteral(", ") + g(rb()) + QStringLiteral(", ") + g(rc) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("sfx")) {
        return g(rt()) + QStringLiteral(" = spu_sfx(") + g(ra()) + QStringLiteral(", ") + g(rb()) +
               QStringLiteral(", ") + g(rt()) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("cgx")) {
        return g(rt()) + QStringLiteral(" = spu_cgx(") + g(ra()) + QStringLiteral(", ") + g(rb()) +
               QStringLiteral(", ") + g(rt()) + QStringLiteral(");");
    }
    if (oneOf(mn, {QStringLiteral("dfma"), QStringLiteral("dfms"), QStringLiteral("dfnms"),
                   QStringLiteral("dfnma")})) {
        return g(rt()) + QStringLiteral(" = spu_") + mn + QStringLiteral("(") + g(ra()) +
               QStringLiteral(", ") + g(rb()) + QStringLiteral(", ") + g(rt()) + QStringLiteral(");");
    }
    if (oneOf(mn, {QStringLiteral("mpyhha"), QStringLiteral("mpyhhau")})) {
        return g(rt()) + QStringLiteral(" = spu_") + mn + QStringLiteral("(") + g(ra()) +
               QStringLiteral(", ") + g(rb()) + QStringLiteral(", ") + g(rt()) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("lqd")) {
        const auto [disp, base] = displacementBase(ops[1]);
        if (!disp.isEmpty()) {
            return g(rt()) + QStringLiteral(" = spu_ls_read128(ctx, ") + g(base) +
                   QStringLiteral("._u32[0] + ") + disp + QStringLiteral(");");
        }
    }
    if (mn == QStringLiteral("stqd")) {
        const auto [disp, base] = displacementBase(ops[1]);
        if (!disp.isEmpty()) {
            return QStringLiteral("spu_ls_write128(ctx, ") + g(base) + QStringLiteral("._u32[0] + ") + disp +
                   QStringLiteral(", ") + g(rt()) + QStringLiteral(");");
        }
    }
    if (oneOf(mn, {QStringLiteral("lqa"), QStringLiteral("lqr")})) {
        return g(rt()) + QStringLiteral(" = spu_ls_read128(ctx, ") + immediateOperand(ops[1]) +
               QStringLiteral(");");
    }
    if (oneOf(mn, {QStringLiteral("stqa"), QStringLiteral("stqr")})) {
        return QStringLiteral("spu_ls_write128(ctx, ") + immediateOperand(ops[1]) + QStringLiteral(", ") +
               g(rt()) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("lqx")) {
        return g(rt()) + QStringLiteral(" = spu_ls_read128(ctx, ") + g(ra()) + QStringLiteral("._u32[0] + ") +
               g(rb()) + QStringLiteral("._u32[0]);");
    }
    if (mn == QStringLiteral("stqx")) {
        return QStringLiteral("spu_ls_write128(ctx, ") + g(ra()) + QStringLiteral("._u32[0] + ") + g(rb()) +
               QStringLiteral("._u32[0], ") + g(rt()) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("rdch")) {
        return g(rt()) + QStringLiteral(" = spu_rdch(ctx, ") + channelOperand(ops[1]) + QStringLiteral(");");
    }
    if (mn == QStringLiteral("rchcnt")) {
        return g(rt()) + QStringLiteral(" = spu_pref_u32(spu_rchcnt(ctx, ") + channelOperand(ops[1]) +
               QStringLiteral("));");
    }
    if (mn == QStringLiteral("wrch")) {
        return QStringLiteral("spu_wrch(ctx, ") + channelOperand(ops[0]) + QStringLiteral(", ") +
               g(registerIndex(ops[1])) + QStringLiteral(");");
    }
    if (oneOf(mn, {QStringLiteral("br"), QStringLiteral("bra")})) {
        auto tgt = branchTarget(insn);
        if (tgt == addr) {
            return QStringLiteral("ctx->status = SPU_STATUS_STOPPED_BY_HALT; return;");
        }
        return unconditionalBranch(tgt, func);
    }
    if (oneOf(mn, {QStringLiteral("brsl"), QStringLiteral("brasl")})) {
        auto link_rt = (insn.raw & 127);
        auto tgt = branchTarget(insn);
        auto link = g(link_rt) + QStringLiteral(" = spu_link(0x") + hex(((addr + 4) & 0x3fffc), 0) +
                    QStringLiteral(");");
        if (tgt == addr) {
            return link + QStringLiteral(" ctx->status = SPU_STATUS_STOPPED_BY_HALT; return;");
        }
        if (tgt == (addr + 4)) {
            return link + QStringLiteral(" ") + unconditionalBranch((addr + 4), func);
        }
        if (tgt != -1) {
            callTargets.insert(tgt);
            return link + QStringLiteral(" { int32_t _si = (int32_t)ctx->image_id; ctx->host_depth++; ") +
                   prefix + QStringLiteral("spu_func_") + hex(tgt, 8) +
                   QStringLiteral("(ctx); spu_drain_call(ctx, 0x") + hex((addr + 4), 0) +
                   QStringLiteral("); ctx->host_depth--; spu_img_restore(ctx, _si); }");
        }
        return link + QStringLiteral(" /* TODO spu: brsl unresolved target */;");
    }
    if (_COND_BR.contains(mn)) {
        auto tgt = branchTarget(insn);
        auto cond = branchCondition(mn, registerIndex(ops[0]));
        if (tgt == -1) {
            return QStringLiteral("/* TODO spu: ") + mn + QStringLiteral(" ") + insn.operands +
                   QStringLiteral(" */ spu_unsupported(0x") + hex(insn.address, 0) + QStringLiteral(", \"") +
                   mn + QStringLiteral("\");");
        }
        if (func.start <= tgt && tgt < func.end) {
            return QStringLiteral("if (") + cond + QStringLiteral(") goto loc_") + hex(tgt, 8) +
                   QStringLiteral(";");
        }
        branchTargets.insert(tgt);
        return QStringLiteral("if (") + cond + QStringLiteral(") { ctx->pc = 0x") + hex(tgt, 0) +
               QStringLiteral("; g_spu_trampoline_fn = ") + prefix + QStringLiteral("spu_func_") +
               hex(tgt, 8) + QStringLiteral("; return; }");
    }
    auto interruptChange =
        ((insn.raw & 0x40000)
             ? QStringLiteral("ctx->int_enable = 1; ")
             : ((insn.raw & 0x80000) ? QStringLiteral("ctx->int_enable = 0; ") : QStringLiteral("")));
    if (oneOf(mn, {QStringLiteral("bi")})) {
        auto tgt_reg = registerIndex(ops[0]);
        if (((tgt_reg == QStringLiteral("0")) && (!computedReturns.contains(addr))) ||
            (linkReturns.contains(addr))) {
            if (tgt_reg != QStringLiteral("0")) {
                return interruptChange + QStringLiteral("SPU_RET_REG(ctx, ") + tgt_reg + QStringLiteral(");");
            }
            return interruptChange + QStringLiteral("SPU_RET(ctx);");
        }
        return interruptChange + QStringLiteral("ctx->pc = ") + g(tgt_reg) +
               QStringLiteral("._u32[0]; g_spu_trampoline_fn = spu_indirect_branch; return;");
    }
    if (mn == QStringLiteral("iret")) {
        return interruptChange +
               QStringLiteral("ctx->pc = ctx->srr0; g_spu_trampoline_fn = spu_indirect_branch; return;");
    }
    if (oneOf(mn, {QStringLiteral("bisl")})) {
        auto link_rt = (insn.raw & 127);
        auto tgt_reg = registerIndex(ops.last());
        return QStringLiteral("{ uint32_t _tg = ") + g(tgt_reg) + QStringLiteral("._u32[0]; ") + g(link_rt) +
               QStringLiteral(" = spu_link(0x") + hex(((addr + 4) & 0x3fffc), 0) +
               QStringLiteral("); int32_t _si = (int32_t)ctx->image_id; ") + interruptChange +
               QStringLiteral(
                   "ctx->pc = _tg; ctx->host_depth++; spu_indirect_branch(ctx); spu_drain_call(ctx, 0x") +
               hex((addr + 4), 0) + QStringLiteral("); ctx->host_depth--; spu_img_restore(ctx, _si); }");
    }
    if (oneOf(mn, {QStringLiteral("bisled")})) {
        auto link_rt = (insn.raw & 127);
        auto tgt_reg = ((insn.raw >> 7) & 127);
        return QStringLiteral("{ uint32_t _tg = ") + g(tgt_reg) + QStringLiteral("._u32[0]; ") + g(link_rt) +
               QStringLiteral(" = spu_link(0x") + hex(((addr + 4) & 0x3fffc), 0) +
               QStringLiteral("); if ((ctx->event_status & ctx->event_mask) != 0) { ") + interruptChange +
               QStringLiteral("ctx->pc = _tg; g_spu_trampoline_fn = spu_indirect_branch; return; } }");
    }
    if (oneOf(mn, {QStringLiteral("biz"), QStringLiteral("binz"), QStringLiteral("bihz"),
                   QStringLiteral("bihnz")})) {
        auto cond = branchCondition(mn.mid(1), registerIndex(ops[0]));
        auto tgt_reg = registerIndex(ops[1]);
        if (((tgt_reg == QStringLiteral("0")) && (!computedReturns.contains(addr))) ||
            (linkReturns.contains(addr))) {
            if (tgt_reg != QStringLiteral("0")) {
                return QStringLiteral("if (") + cond + QStringLiteral(") { ") + interruptChange +
                       QStringLiteral("SPU_RET_REG(ctx, ") + tgt_reg + QStringLiteral("); }");
            }
            return QStringLiteral("if (") + cond + QStringLiteral(") { ") + interruptChange +
                   QStringLiteral("SPU_RET(ctx); }");
        }
        return QStringLiteral("if (") + cond + QStringLiteral(") { ") + interruptChange +
               QStringLiteral("ctx->pc = ") + g(tgt_reg) +
               QStringLiteral("._u32[0]; g_spu_trampoline_fn = spu_indirect_branch; return; }");
    }
    if (oneOf(mn, {QStringLiteral("hbr"), QStringLiteral("hbra"), QStringLiteral("hbrr")})) {
        return QStringLiteral("/* branch hint (ignored) */;");
    }
    if (mn == QStringLiteral("mtspr")) {
        return QStringLiteral("/* mtspr: SPR write ignored */;");
    }
    ++unsupported[mn];
    return QStringLiteral("/* TODO spu: ") + mn + QStringLiteral(" ") + insn.operands +
           QStringLiteral(" */ spu_unsupported(0x") + hex(insn.address, 0) + QStringLiteral(", \"") + mn +
           QStringLiteral("\");");
}

int indirectRegister(const SpuInstruction &insn) {
    if (oneOf(insn.mnemonic, {"bi", "biz", "binz", "bihz", "bihnz"}))
        return (insn.raw >> 7) & 127;
    return -1;
}
bool writes(const SpuInstruction &insn, int reg) {
    const bool four = oneOf(insn.mnemonic, {"selb", "shufb", "mpya", "fnms", "fma", "fms"});
    return !noRtWrite.contains(insn.mnemonic) && int((insn.raw >> (four ? 21 : 0)) & 127) == reg;
}
bool identityMove(const SpuInstruction &insn) {
    return oneOf(insn.mnemonic, {"ai", "ori"}) && ((insn.raw >> 14) & 1023) == 0 &&
           (insn.raw & 127) == ((insn.raw >> 7) & 127);
}
void classifyReturns(Emitter &emitter, QVector<SpuInstruction> code, const QVector<SpuBound> &bounds,
                     const Cancel &cancel) {
    std::sort(code.begin(), code.end(), [](const auto &a, const auto &b) { return a.address < b.address; });
    QSet<int> linkRegisters;
    QMap<int, QVector<qsizetype>> predecessors;
    for (qsizetype i = 0; i < code.size(); ++i) {
        const auto &insn = code[i];
        if (oneOf(insn.mnemonic, {"brsl", "brasl", "bisl", "bisled"}))
            linkRegisters.insert(insn.raw & 127);
        if (oneOf(insn.mnemonic, {"br", "bra"}))
            predecessors[branchTarget(insn)].append(i);
    }
    linkRegisters.remove(0);
    for (const auto &bound : bounds) {
        cancelled(cancel);
        const auto begin =
            std::lower_bound(code.cbegin(), code.cend(), quint32(bound.start),
                             [](const auto &insn, quint32 address) { return insn.address < address; });
        const auto first = begin - code.cbegin();
        for (qsizetype i = first; i < code.size() && code[i].address < quint32(bound.end); ++i) {
            const auto &insn = code[i];
            const int reg = indirectRegister(insn);
            if (reg == 0) {
                bool identity = false;
                bool decided = false;
                for (auto j = i - 1; j >= first && !terminators.contains(code[j].mnemonic); --j) {
                    if (!writes(code[j], 0))
                        continue;
                    if (identityMove(code[j])) {
                        identity = true;
                        continue;
                    }
                    if (oneOf(code[j].mnemonic, {"lqa", "lqr", "il", "ila"}))
                        emitter.computedReturns.insert(insn.address);
                    decided = true;
                    break;
                }
                if (!decided && identity) {
                    for (auto j = i - 1; j >= 0 && j >= i - 12; --j) {
                        if (!writes(code[j], 0) || identityMove(code[j]))
                            continue;
                        if (oneOf(code[j].mnemonic, {"il", "ila"}))
                            emitter.computedReturns.insert(insn.address);
                        break;
                    }
                }
                continue;
            }
            if (!linkRegisters.contains(reg))
                continue;
            bool written = false;
            int blockStart = bound.start;
            for (auto j = i - 1; j >= 0; --j) {
                if (terminators.contains(code[j].mnemonic)) {
                    blockStart = code[j + 1].address;
                    break;
                }
                if (writes(code[j], reg) && !identityMove(code[j])) {
                    written = true;
                    break;
                }
            }
            for (auto pred = predecessors.lowerBound(blockStart);
                 !written && pred != predecessors.end() && pred.key() <= int(insn.address); ++pred) {
                for (auto source : pred.value()) {
                    for (auto j = source - 1;
                         j >= 0 && j > source - 24 && !terminators.contains(code[j].mnemonic); --j) {
                        if (writes(code[j], reg) && !identityMove(code[j])) {
                            written = true;
                            break;
                        }
                    }
                    if (written)
                        break;
                }
            }
            if (!written)
                emitter.linkReturns.insert(insn.address);
        }
    }
}
QList<int> sorted(const QSet<int> &values) {
    auto result = values.values();
    std::sort(result.begin(), result.end());
    return result;
}
QJsonArray addresses(const QSet<int> &values) {
    QJsonArray result;
    for (int value : sorted(values))
        result.append(value);
    return result;
}
constexpr const char *preamble =
#include "spu_header.inc"
    ;
} // namespace

QVector<SpuInstruction> decodeSpuText(const QByteArray &elf, const Cancel &cancel) {
    const auto image = validateSpuImage(elf);
    quint64 start = 0x40000, end = 0;
    for (const auto &segment : image.loads) {
        if (!(segment.flags & 1))
            continue;
        start = std::min(start, segment.address);
        end = std::max(end, segment.address + segment.fileSize);
    }
    require(start < end && end <= 0x40000, "SPU ELF has no executable text.");
    QByteArray text(end - start, '\0');
    for (const auto &segment : image.loads) {
        if (segment.flags & 1)
            text.replace(segment.address - start, segment.fileSize,
                         part(elf, segment.offset, segment.fileSize));
    }
    QVector<SpuInstruction> instructions;
    for (quint64 pos = 0; pos < quint64(text.size()); pos += 4) {
        if (pos % 4096 == 0)
            cancelled(cancel);
        instructions.append(decodeSpu(be32(text, pos), start + pos));
    }
    return instructions;
}

namespace {
QSet<int> branchSeeds(const QVector<SpuInstruction> &code, bool raw, const Cancel &cancel) {
    require(!code.isEmpty(), "No decoded SPU instructions.");
    const int start = code.first().address;
    const int end = code.last().address + 4;
    const auto valid = [=](qint64 address) { return address >= start && address < end && address % 4 == 0; };
    QSet<int> seeds;
    const auto add = [&](qint64 address) {
        if (valid(address))
            seeds.insert(int(address));
    };
    QSet<int> tables;
    QHash<int, quint32> highHalves;
    const auto immediateLoad = [](const SpuInstruction &i) { return oneOf(i.mnemonic, {"il", "ila"}); };
    const auto immediate = [](const SpuInstruction &i) {
        return i.mnemonic == "ila" ? int((i.raw >> 7) & 0x3ffff) : int(qint16((i.raw >> 7) & 0xffff));
    };
    for (qsizetype i = 0; i < code.size(); ++i) {
        if (i % 1024 == 0)
            cancelled(cancel);
        const auto &insn = code[i];
        const int destination = insn.raw & 127;
        add(branchTarget(insn));
        if (immediateLoad(insn)) {
            const int value = immediate(insn);
            if ((!raw && value) || destination == 0)
                add(value);
            if (value && valid(value))
                tables.insert(value);
        } else if (!raw && insn.mnemonic == "ilhu") {
            highHalves.insert(destination, ((insn.raw >> 7) & 0xffff) << 16);
        } else if (insn.mnemonic == "iohl" && highHalves.contains(destination)) {
            const auto address = highHalves.take(destination) | ((insn.raw >> 7) & 0xffff);
            if (address)
                add(address);
        }
        int reg = indirectRegister(insn);
        if (insn.mnemonic == "bisl")
            reg = (insn.raw >> 7) & 127;
        if (insn.mnemonic == "bisled")
            reg = (insn.raw >> 14) & 127; // Last operand in the SDK decoder.
        if (reg >= 0) {
            for (auto j = i - 1; j >= 0 && j >= i - 8; --j) {
                if (immediateLoad(code[j]) && int(code[j].raw & 127) == reg) {
                    add(immediate(code[j]));
                    break;
                }
            }
        }
        if (raw || insn.mnemonic != "bi" || reg <= 0)
            continue;
        // PC getter plus an offset can enter the middle of an unrolled loop.
        int displacement = 0;
        for (auto j = i - 1; j >= 0 && j >= i - 12; --j) {
            const auto &previous = code[j];
            if (int(previous.raw & 127) != reg)
                continue;
            if (previous.mnemonic == "ai" && int((previous.raw >> 7) & 127) == reg) {
                const int value = (previous.raw >> 14) & 1023;
                displacement += value >= 512 ? value - 1024 : value;
            } else if (oneOf(previous.mnemonic, {"brsl", "brasl"})) {
                const int next = previous.address + 4;
                if (branchTarget(previous) == next && displacement > 0) {
                    for (int offset = 0; offset <= 64; offset += 4)
                        add(next + displacement + offset);
                }
                break;
            }
        }
    }
    for (int table : tables) {
        QSet<int> entries;
        for (int pos = table; pos < end && pos < table + 128; pos += 4) {
            const auto word = code[(pos - start) / 4].raw;
            const auto relative = (table + word) & 0x3ffff;
            if (word % 4 || (!valid(word) && !valid(relative)))
                break;
            if (valid(word))
                entries.insert(word);
            if (valid(relative))
                entries.insert(relative);
        }
        if (entries.size() >= 2)
            seeds |= entries;
    }
    return seeds;
}
} // namespace

QVector<SpuBound> findRawSpuBounds(const QVector<SpuInstruction> &code, int entry, const QSet<int> &extra,
                                   const Cancel &cancel) {
    auto seeds = branchSeeds(code, true, cancel);
    const int end = code.last().address + 4;
    seeds.insert(entry);
    seeds |= extra;
    const auto starts = sorted(seeds);
    QVector<SpuBound> bounds;
    for (qsizetype i = 0; i < starts.size(); ++i) {
        if (starts[i] >= entry)
            bounds.append({starts[i], i + 1 < starts.size() ? starts[i + 1] : end});
    }
    return bounds;
}

namespace {
void addPointerSeeds(QSet<int> &seeds, QByteArrayView bytes, int start, int end) {
    const auto valid = [=](qint64 address) { return address >= start && address < end && address % 4 == 0; };
    QVector<int> run;
    const auto flush = [&] {
        if (run.size() >= 3 && QSet<int>(run.cbegin(), run.cend()).size() >= 2) {
            for (int address : run)
                seeds.insert(address);
        }
        run.clear();
    };
    for (qsizetype pos = 0; pos + 4 <= bytes.size(); pos += 4) {
        const auto address = qFromBigEndian<quint32>(bytes.data() + pos);
        if (valid(address)) {
            run.append(address);
            if (run.size() == 1024)
                flush();
        } else {
            flush();
        }
    }
    flush();
}

QVector<SpuBound> boundSpuSeeds(const QVector<SpuInstruction> &code, QSet<int> seeds,
                                const QHash<int, int> &symbolEnds) {
    const int start = code.first().address;
    const int end = code.last().address + 4;
    if (seeds.isEmpty())
        seeds.insert(start);
    const auto starts = sorted(seeds);
    QVector<SpuBound> bounds;
    for (qsizetype i = 0; i < starts.size(); ++i) {
        const int address = starts[i];
        int limit = symbolEnds.value(address);
        if (!limit) {
            limit = i + 1 < starts.size() ? starts[i + 1] : end;
            for (int pos = address; pos < limit; pos += 4) {
                const auto &insn = code[(pos - start) / 4];
                const bool returns = insn.mnemonic == "bi" && indirectRegister(insn) == 0;
                const bool resumes =
                    insn.mnemonic == "stop" && LV2_RESUMING_STOPS.contains(insn.raw & 0x3fff);
                if (returns || (insn.mnemonic != "bi" && terminators.contains(insn.mnemonic) && !resumes)) {
                    limit = pos + 4;
                    break;
                }
            }
        }
        if (!bounds.isEmpty() && address < bounds.last().end)
            bounds.last().end = std::max(bounds.last().end, limit);
        else
            bounds.append({address, limit});
    }
    return bounds;
}
} // namespace

QVector<SpuBound> findSpuBounds(const QByteArray &elf, const QVector<SpuInstruction> &code,
                                const Cancel &cancel) {
    const auto image = validateSpuImage(elf);
    require(!code.isEmpty(), "No decoded SPU instructions.");
    const int start = code.first().address;
    const int end = code.last().address + 4;
    const auto valid = [=](qint64 address) { return address >= start && address < end && address % 4 == 0; };
    auto seeds = branchSeeds(code, false, cancel);
    const auto add = [&](qint64 address) {
        if (valid(address))
            seeds.insert(int(address));
    };
    add(image.entry);
    QHash<int, int> symbolEnds;
    const auto sectionOffset = be32(elf, 32);
    for (int section = 0; section < be16(elf, 48); ++section) {
        const auto header = sectionOffset + quint64(section) * 40;
        if (be32(elf, header + 4) != 2) // SHT_SYMTAB
            continue;
        const auto offset = be32(elf, header + 16);
        const auto size = be32(elf, header + 20);
        for (quint64 pos = offset; pos < quint64(offset) + size; pos += 16) {
            if ((quint8(elf.at(pos + 12)) & 15) != 2) // STT_FUNC
                continue;
            const auto address = be32(elf, pos + 4);
            const auto length = be32(elf, pos + 8);
            if (!valid(address))
                continue;
            add(address);
            if (length) {
                require(length <= quint32(0x40000 - address), "SPU symbol extends past local store.");
                symbolEnds.insert(address, address + length);
            }
        }
    }
    for (const auto &segment : image.loads) {
        cancelled(cancel);
        const auto align = (4 - segment.address % 4) % 4;
        if (segment.fileSize > align) {
            const QByteArrayView bytes(elf.constData() + segment.offset + align, segment.fileSize - align);
            addPointerSeeds(seeds, bytes, start, end);
        }
    }
    return boundSpuSeeds(code, std::move(seeds), symbolEnds);
}

QVector<SpuBound> findSpuModuleBounds(const QByteArray &text, const QVector<SpuInstruction> &code,
                                      const QVector<int> &entries, const Cancel &cancel) {
    auto seeds = branchSeeds(code, false, cancel);
    const int start = code.first().address;
    const int end = code.last().address + 4;
    seeds.insert(start);
    addPointerSeeds(seeds, text, start, end);
    auto bounds = boundSpuSeeds(code, std::move(seeds), {});
    for (const int entry : entries) {
        require(entry >= start && entry < end && entry % 4 == 0, "Invalid SPU module entry.");
        const auto next =
            std::lower_bound(bounds.begin(), bounds.end(), entry,
                             [](const SpuBound &bound, int address) { return bound.start < address; });
        if (next != bounds.end() && next->start == entry)
            continue;
        const int limit = next == bounds.end() ? end : next->start;
        if (next != bounds.begin() && (next - 1)->end > entry) {
            const int previousEnd = (next - 1)->end;
            (next - 1)->end = entry;
            bounds.insert(next, {entry, previousEnd});
        } else {
            bounds.insert(next, {entry, limit});
        }
    }
    return bounds;
}

SpuLiftResult liftSpu(const QVector<SpuInstruction> &instructions, const QVector<SpuBound> &bounds,
                      const SpuLiftOptions &options, const Cancel &cancel) {
    static const QRegularExpression identifier("^[A-Za-z_][A-Za-z_0-9]*$");
    require(options.prefix.isEmpty() || identifier.match(options.prefix).hasMatch(),
            "Invalid SPU symbol prefix.");
    Emitter emitter;
    emitter.prefix = options.prefix;
    if (!emitter.prefix.isEmpty() && !emitter.prefix.endsWith('_'))
        emitter.prefix += '_';
    QHash<int, qsizetype> index;
    for (qsizetype i = 0; i < instructions.size(); ++i) {
        const auto addr = instructions[i].address;
        require(addr < 0x40000 && addr % 4 == 0 && !index.contains(int(addr)),
                "Invalid or overlapping SPU code.");
        index.insert(int(addr), i);
    }
    QSet<int> starts;
    for (const auto &bound : bounds) {
        require(bound.start >= 0 && bound.start < bound.end && bound.end <= 0x40000 && bound.start % 4 == 0 &&
                    bound.end % 4 == 0 && !starts.contains(bound.start),
                "Invalid or duplicate SPU function bound.");
        starts.insert(bound.start);
    }
    classifyReturns(emitter, instructions, bounds, cancel);
    emitter.computedReturns |= options.forceIndirect;
    emitter.linkReturns -= options.forceIndirect;
    QStringList header{QString::fromLatin1(preamble), ""};
    QStringList source{"/* Auto-generated by BuzzPrepare -- do not edit by hand. */\n"
                       "#include \"spu_recomp.h\"\n#include \"../../runtime/spu/spu_helpers.h\"\n",
                       ""};
    QJsonArray functions;
    const QSet<QString> idleOps{"nop", "lnop", "br", "bra"};
    for (const auto &bound : bounds) {
        cancelled(cancel);
        functions.append(QJsonArray{bound.start, bound.end});
        const auto name = emitter.name(bound.start);
        header.append("void " + name + "(spu_context* ctx);");
        source.append("void " + name + "(spu_context* ctx) {");
        QStringList body;
        QSet<int> internal;
        const SpuInstruction *last = nullptr;
        bool idle = true;
        for (int addr = bound.start; addr < bound.end; addr += 4) {
            if (!index.contains(addr))
                continue;
            const int target = branchTarget(instructions[index.value(addr)]);
            if (target >= bound.start && target < bound.end)
                internal.insert(target);
        }
        for (int addr = bound.start; addr < bound.end; addr += 4) {
            if (!index.contains(addr))
                continue;
            const auto &insn = instructions[index.value(addr)];
            if (internal.contains(addr))
                body.append("loc_" + hex(addr, 8) + ":");
            const auto code = emitter.lower(insn, bound);
            if (options.trace)
                body.append("        spu_trace_pc(ctx, 0x" + hex(addr) + ");");
            body.append("        " + code);
            if (options.trace && !noRtWrite.contains(insn.mnemonic))
                body.append("        spu_trace_rt(ctx, " + QString::number(insn.raw & 127) + ");");
            idle = idle && idleOps.contains(insn.mnemonic);
            last = &insn;
        }
        if (last && starts.contains(bound.end) &&
            (!terminators.contains(last->mnemonic) ||
             (last->mnemonic == "stop" && LV2_RESUMING_STOPS.contains(last->raw & 0x3fff)))) {
            body.append("        { ctx->pc = 0x" + hex(bound.end) +
                        "; g_spu_trampoline_fn = " + emitter.name(bound.end) + "; return; }");
        }
        if (last && idle && oneOf(last->mnemonic, {"br", "bra"})) {
            const int target = branchTarget(*last);
            if (target >= bound.start && target < bound.end)
                body = {"        /* infinite no-op self-loop (SPU halt idiom) -> abort */",
                        "        spu_halt(ctx); return;"};
        }
        source.append(body);
        source.append("}");
        source.append("");
    }
    const auto external = sorted((emitter.callTargets | emitter.branchTargets) - starts);
    if (!external.isEmpty()) {
        source.append({"/* Indirect-dispatch stubs for cross-function targets the",
                       " * function detector did not seed (typically conditional",
                       " * branches that escape a function's range). */"});
        for (int target : external) {
            const auto name = emitter.name(target);
            header.append("void " + name + "(spu_context* ctx); /* external */");
            source.append("void " + name + "(spu_context* ctx) {");
            source.append("    ctx->pc = 0x" + hex(target) +
                          "u; g_spu_trampoline_fn = spu_indirect_branch; return;");
            source.append("}");
        }
        source.append("");
    }
    const auto registration = emitter.prefix + "spu_recomp_register";
    header.append({"", "/* Runtime glue (runtime/spu/spu_channels.c) */",
                   "void spu_register_function(uint32_t addr, void (*fn)(spu_context*));",
                   "/* Call once at init to register this image's functions for",
                   " * indirect-branch dispatch (spu_indirect_branch). */",
                   "void " + registration + "(void);", ""});
    source.append(
        {"/* Function table */",
         "typedef struct { uint32_t addr; void (*func)(spu_context*); const char* name; } spu_func_entry;",
         "static const spu_func_entry spu_function_table[] = {"});
    for (const auto &bound : bounds) {
        const auto name = emitter.name(bound.start);
        source.append("    { 0x" + hex(bound.start, 8) + "u, " + name + ", \"" + name + "\" },");
    }
    source.append({"    { 0, NULL, NULL }", "};", "", "void " + registration + "(void) {",
                   "    for (const spu_func_entry* e = spu_function_table; e->func; ++e)",
                   "        spu_register_function(e->addr, e->func);", "}", ""});
    QJsonObject unsupported;
    for (auto it = emitter.unsupported.cbegin(); it != emitter.unsupported.cend(); ++it)
        unsupported[it.key()] = it.value();
    return {header.join('\n').toUtf8(),
            source.join('\n').toUtf8(),
            {{"functions", functions},
             {"unsupported", unsupported},
             {"call_targets", addresses(emitter.callTargets)},
             {"branch_targets", addresses(emitter.branchTargets)}}};
}
} // namespace prepare
