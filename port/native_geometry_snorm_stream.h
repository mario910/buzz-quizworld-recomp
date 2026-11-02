/* Complete storage-1 SNORM16 attribute stream and full pipeline ABI.
 * Group zero always gathers across quads; later within-quad groups wrap.
 * Preserve the zero low halfword from the two-stage shuffle and exact SPU CSFLT/FMA semantics. */
#pragma once
#include "native_geometry_typed_stream.h"
#include "native_stagef_avx2.h"

namespace buzz {
using GeometrySnormStream=GeometryTypedStream;
inline bool read_geometry_snorm_stream(const spu_context& ctx,GeometrySnormStream& p,const char** reason=nullptr)
{
    const uint32_t table[]={0x0001ffff,0x0203ffff,0x0405ffff,0x0607ffff};
    return read_geometry_typed_stream(ctx,p,0xd910,table,true,reason);
}
inline void geometry_snorm_setup_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[23] = spu_ori(r[17], 0);
    r[56] = spu_ls_read128(&ctx, 0xD910);
    r[60] = spu_mpya(r[23], r[106], r[42]);
    r[62] = spu_shufb(r[18], r[18], r[19]);
    r[15] = spu_a(r[62], r[41]);
    r[61] = spu_andi(r[60], 15);
    r[21] = spu_ls_read128(&ctx, r[37]._u32[0] + r[60]._u32[0]);
    r[16] = spu_a(r[60], r[83]);
    r[53] = spu_ls_read128(&ctx, r[36]._u32[0] + r[60]._u32[0]);
    r[58] = spu_a(r[61], r[25]);
    r[59] = spu_shufb(r[61], r[60], r[108]);
    r[54] = spu_ls_read128(&ctx, r[40]._u32[0] + r[60]._u32[0]);
    r[20] = spu_cgti(r[58], 16);
    r[52] = spu_ls_read128(&ctx, r[35]._u32[0] + r[60]._u32[0]);
    r[8] = spu_ls_read128(&ctx, r[39]._u32[0] + r[60]._u32[0]);
    r[55] = spu_a(r[112], r[59]);
    r[50] = spu_ls_read128(&ctx, r[34]._u32[0] + r[60]._u32[0]);
    r[51] = spu_ls_read128(&ctx, r[38]._u32[0] + r[60]._u32[0]);
    r[49] = spu_ls_read128(&ctx, r[33]._u32[0] + r[60]._u32[0]);
    r[14] = spu_shufb(r[55], r[55], r[56]);
    r[12] = spu_shufb(r[53], r[54], r[14]);
    r[8] = spu_shufb(r[52], r[8], r[14]);
    r[2] = spu_shufb(r[50], r[51], r[14]);
    r[13] = spu_shufb(r[49], r[21], r[14]);
}

inline void geometry_snorm_cross_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[27] = spu_csflt(r[12], 139);
    r[69] = spu_shlqbyi(r[16], 0);
    r[3] = spu_csflt(r[8], 139);
    r[10] = spu_csflt(r[2], 139);
    r[79] = spu_csflt(r[13], 139);
    r[78] = spu_cgt(r[15], r[80]);
    r[2] = spu_a(r[16], r[83]);
    r[71] = spu_selb(r[15], r[82], r[78]);
    r[77] = spu_fma(r[27], r[93], r[92]);
    r[16] = spu_shlqbyi(r[2], 0);
    r[74] = spu_fma(r[3], r[93], r[92]);
    r[12] = spu_rotqbyi(r[71], 4);
    r[73] = spu_fma(r[10], r[93], r[92]);
    r[72] = spu_rotqbyi(r[71], 8);
    r[9] = spu_fma(r[79], r[93], r[92]);
    r[70] = spu_rotqbyi(r[71], 12);
    r[75] = spu_a(r[15], r[81]);
    r[15] = spu_ori(r[75], 0);
    spu_ls_write128(&ctx, r[71]._u32[0] + 0x0, r[77]);
    spu_ls_write128(&ctx, r[12]._u32[0] + 0x0, r[74]);
    spu_ls_write128(&ctx, r[72]._u32[0] + 0x0, r[73]);
    spu_ls_write128(&ctx, r[70]._u32[0] + 0x0, r[9]);
    r[63] = spu_ls_read128(&ctx, r[37]._u32[0] + r[69]._u32[0]);
    r[68] = spu_ls_read128(&ctx, r[36]._u32[0] + r[69]._u32[0]);
    r[57] = spu_ls_read128(&ctx, r[40]._u32[0] + r[69]._u32[0]);
    r[66] = spu_ls_read128(&ctx, r[35]._u32[0] + r[69]._u32[0]);
    r[67] = spu_ls_read128(&ctx, r[39]._u32[0] + r[69]._u32[0]);
    r[65] = spu_ls_read128(&ctx, r[34]._u32[0] + r[69]._u32[0]);
    r[64] = spu_ls_read128(&ctx, r[38]._u32[0] + r[69]._u32[0]);
    r[26] = spu_ls_read128(&ctx, r[33]._u32[0] + r[69]._u32[0]);
    r[12] = spu_shufb(r[68], r[57], r[14]);
    r[8] = spu_shufb(r[66], r[67], r[14]);
    r[2] = spu_shufb(r[65], r[64], r[14]);
    r[13] = spu_shufb(r[26], r[63], r[14]);
}

inline void geometry_snorm_within_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[50] = spu_csflt(r[12], 139);
    r[49] = spu_csflt(r[8], 139);
    r[29] = spu_shlqbyi(r[16], 0);
    r[21] = spu_csflt(r[2], 139);
    r[20] = spu_csflt(r[13], 139);
    r[13] = spu_cgt(r[15], r[80]);
    r[51] = spu_a(r[16], r[83]);
    r[31] = spu_selb(r[15], r[82], r[13]);
    r[28] = spu_shlqbyi(r[13], 0);
    r[48] = spu_fma(r[50], r[93], r[92]);
    r[16] = spu_shlqbyi(r[51], 0);
    r[45] = spu_fma(r[49], r[93], r[92]);
    r[44] = spu_rotqbyi(r[31], 4);
    r[43] = spu_fma(r[21], r[93], r[92]);
    r[32] = spu_rotqbyi(r[31], 8);
    r[30] = spu_fma(r[20], r[93], r[92]);
    r[6] = spu_rotqbyi(r[31], 12);
    r[46] = spu_a(r[15], r[81]);
    r[15] = spu_ori(r[46], 0);
    spu_ls_write128(&ctx, r[31]._u32[0] + 0x0, r[48]);
    spu_ls_write128(&ctx, r[44]._u32[0] + 0x0, r[45]);
    spu_ls_write128(&ctx, r[32]._u32[0] + 0x0, r[43]);
    spu_ls_write128(&ctx, r[6]._u32[0] + 0x0, r[30]);
    r[22] = spu_ls_read128(&ctx, r[33]._u32[0] + r[29]._u32[0]);
    r[7] = spu_ls_read128(&ctx, r[36]._u32[0] + r[29]._u32[0]);
    r[4] = spu_ls_read128(&ctx, r[35]._u32[0] + r[29]._u32[0]);
    r[5] = spu_ls_read128(&ctx, r[34]._u32[0] + r[29]._u32[0]);
    r[13] = spu_shufb(r[22], r[22], r[14]);
    r[12] = spu_shufb(r[7], r[7], r[14]);
    r[8] = spu_shufb(r[4], r[4], r[14]);
    r[2] = spu_shufb(r[5], r[5], r[14]);
}

inline void decode_geometry_snorm_stream(spu_context& ctx,const GeometrySnormStream& p)
{
    if(!p.columns)return;
    auto& r=ctx.gpr;
    r[36]=r[118];r[40]=geometry_output_quad(ctx,r[1]._u32[0]+0x70);
    r[35]=r[117];r[39]=geometry_output_quad(ctx,r[1]._u32[0]+0x60);
    r[34]=r[116];r[38]=geometry_output_quad(ctx,r[1]._u32[0]+0x50);
    r[33]=r[124];r[37]=geometry_output_quad(ctx,r[1]._u32[0]+0x40);
    r[17]=spu_il(0);r[18]=spu_fsmbi(0);r[19]=spu_ila(0x10203);
    const unsigned bases[]={36,35,34,33},pending[]={12,8,2,13};
    for(unsigned column=0;column<p.columns;++column){
        const auto& c=p.column[column];const unsigned prefix=c.groups-1;
        for(unsigned group=0;group<prefix;++group){
            u128 values[4];const unsigned offset=c.offset+group*p.input_step;
            for(unsigned row=0;row<4;++row){
                const auto bits=geometry_typed_halfwords(ctx.ls,r[bases[row]]._u32[0],offset,c.crosses || !group);
                values[row]=spu_fma(spu_csflt(bits,139),r[93],r[92]);
            }
            for(unsigned row=0;row<4;++row){
                uint32_t at=r[41]._u32[row]+column*16+group*p.output_step;
                if(at>p.limit)at=r[82]._u32[row];
                spu_ls_write128(&ctx,at,values[row]);
            }
        }
        // Setup-only registers stay anchored to group zero. Each branch leaves
        // a different set of volatile registers, including earlier columns.
        geometry_snorm_setup_abi(ctx);
        r[15]=spu_ai(r[15],prefix*p.output_step);r[16]=spu_ai(r[16],prefix*p.input_step);
        const unsigned offset=c.offset+prefix*p.input_step;
        for(unsigned row=0;row<4;++row)
            r[pending[row]]=geometry_typed_halfwords(ctx.ls,r[bases[row]]._u32[0],offset,c.crosses || !prefix);
        if(c.crosses)geometry_snorm_cross_final_abi(ctx);else geometry_snorm_within_final_abi(ctx);
        r[17]=spu_ai(r[17],1);r[18]=spu_ai(r[18],16);r[14]=spu_clgt(r[107],r[17]);
    }
}
} // namespace buzz
