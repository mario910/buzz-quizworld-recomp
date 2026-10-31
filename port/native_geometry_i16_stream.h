/* Complete typed attribute payload loop with exact final pipeline ABI. */
#pragma once
#include "native_geometry_typed_stream.h"
#include "native_stagef_avx2.h"
namespace buzz {
using GeometryI16Stream=GeometryTypedStream;
inline bool read_geometry_i16_stream(const spu_context& ctx,GeometryI16Stream& p,const char** reason=nullptr)
{
    const uint32_t table[]={0x1ffff,0x203ffff,0x405ffff,0x607ffff};
    return read_geometry_typed_stream(ctx,p,0xd910,table,true,reason);
}

inline void geometry_i16_entry_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[36] = spu_ori(r[118], 0);
    r[40] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x70);
    r[35] = spu_ori(r[117], 0);
    r[39] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x60);
    r[34] = spu_ori(r[116], 0);
    r[38] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x50);
    r[33] = spu_ori(r[124], 0);
    r[37] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x40);
    r[20] = spu_il(0);
    r[19] = spu_fsmbi(0x0);
    r[21] = spu_ila(0x10203);
}

inline void geometry_i16_setup_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[68] = spu_ori(r[20], 0);
    r[26] = spu_ls_read128(&ctx, 0xD910);
    r[65] = spu_mpya(r[68], r[106], r[42]);
    r[67] = spu_shufb(r[19], r[19], r[21]);
    r[14] = spu_a(r[67], r[41]);
    r[66] = spu_andi(r[65], 15);
    r[18] = spu_ls_read128(&ctx, r[37]._u32[0] + r[65]._u32[0]);
    r[17] = spu_a(r[65], r[83]);
    r[7] = spu_ls_read128(&ctx, r[36]._u32[0] + r[65]._u32[0]);
    r[63] = spu_a(r[66], r[25]);
    r[64] = spu_shufb(r[66], r[65], r[108]);
    r[8] = spu_ls_read128(&ctx, r[40]._u32[0] + r[65]._u32[0]);
    r[23] = spu_cgti(r[63], 16);
    r[16] = spu_ls_read128(&ctx, r[35]._u32[0] + r[65]._u32[0]);
    r[11] = spu_ls_read128(&ctx, r[39]._u32[0] + r[65]._u32[0]);
    r[24] = spu_a(r[112], r[64]);
    r[9] = spu_ls_read128(&ctx, r[34]._u32[0] + r[65]._u32[0]);
    r[10] = spu_ls_read128(&ctx, r[38]._u32[0] + r[65]._u32[0]);
    r[15] = spu_ls_read128(&ctx, r[33]._u32[0] + r[65]._u32[0]);
    r[13] = spu_shufb(r[24], r[24], r[26]);
}

inline void geometry_i16_cross_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[77] = spu_cgt(r[14], r[80]);
    r[3] = spu_shufb(r[7], r[8], r[13]);
    r[69] = spu_ori(r[17], 0);
    r[79] = spu_shufb(r[16], r[11], r[13]);
    r[71] = spu_selb(r[14], r[82], r[77]);
    r[10] = spu_shufb(r[9], r[10], r[13]);
    r[2] = spu_a(r[17], r[83]);
    r[78] = spu_shufb(r[15], r[18], r[13]);
    r[75] = spu_csflt(r[3], 139);
    r[12] = spu_rotqbyi(r[71], 4);
    r[74] = spu_csflt(r[79], 139);
    r[72] = spu_rotqbyi(r[71], 8);
    r[73] = spu_csflt(r[10], 139);
    r[70] = spu_rotqbyi(r[71], 12);
    r[9] = spu_csflt(r[78], 139);
    r[17] = spu_shlqbyi(r[2], 0);
    r[76] = spu_a(r[14], r[81]);
    r[14] = spu_ori(r[76], 0);
    spu_ls_write128(&ctx, r[71]._u32[0] + 0x0, r[75]);
    spu_ls_write128(&ctx, r[12]._u32[0] + 0x0, r[74]);
    spu_ls_write128(&ctx, r[72]._u32[0] + 0x0, r[73]);
    spu_ls_write128(&ctx, r[70]._u32[0] + 0x0, r[9]);
    r[18] = spu_ls_read128(&ctx, r[37]._u32[0] + r[69]._u32[0]);
    r[7] = spu_ls_read128(&ctx, r[36]._u32[0] + r[69]._u32[0]);
    r[8] = spu_ls_read128(&ctx, r[40]._u32[0] + r[69]._u32[0]);
    r[16] = spu_ls_read128(&ctx, r[35]._u32[0] + r[69]._u32[0]);
    r[11] = spu_ls_read128(&ctx, r[39]._u32[0] + r[69]._u32[0]);
    r[9] = spu_ls_read128(&ctx, r[34]._u32[0] + r[69]._u32[0]);
    r[10] = spu_ls_read128(&ctx, r[38]._u32[0] + r[69]._u32[0]);
    r[15] = spu_ls_read128(&ctx, r[33]._u32[0] + r[69]._u32[0]);
}

inline void geometry_i16_within_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[43] = spu_cgt(r[14], r[80]);
    r[47] = spu_shufb(r[7], r[7], r[13]);
    r[28] = spu_ori(r[17], 0);
    r[5] = spu_selb(r[14], r[82], r[43]);
    r[45] = spu_shufb(r[16], r[16], r[13]);
    r[48] = spu_a(r[17], r[83]);
    r[46] = spu_shufb(r[9], r[9], r[13]);
    r[31] = spu_csflt(r[47], 139);
    r[44] = spu_shufb(r[15], r[15], r[13]);
    r[32] = spu_a(r[14], r[81]);
    r[29] = spu_rotqbyi(r[5], 4);
    r[6] = spu_csflt(r[45], 139);
    r[7] = spu_rotqbyi(r[5], 8);
    r[22] = spu_csflt(r[46], 139);
    r[4] = spu_rotqbyi(r[5], 12);
    r[16] = spu_csflt(r[44], 139);
    r[17] = spu_shlqbyi(r[48], 0);
    r[14] = spu_ori(r[32], 0);
    spu_ls_write128(&ctx, r[5]._u32[0] + 0x0, r[31]);
    spu_ls_write128(&ctx, r[29]._u32[0] + 0x0, r[6]);
    spu_ls_write128(&ctx, r[7]._u32[0] + 0x0, r[22]);
    spu_ls_write128(&ctx, r[4]._u32[0] + 0x0, r[16]);
    r[15] = spu_ls_read128(&ctx, r[33]._u32[0] + r[28]._u32[0]);
    r[7] = spu_ls_read128(&ctx, r[36]._u32[0] + r[28]._u32[0]);
    r[16] = spu_ls_read128(&ctx, r[35]._u32[0] + r[28]._u32[0]);
    r[9] = spu_ls_read128(&ctx, r[34]._u32[0] + r[28]._u32[0]);
}

inline void decode_geometry_i16_stream(spu_context& ctx,const GeometryI16Stream& p)
{
    if(!p.columns)return;geometry_i16_entry_abi(ctx);auto& r=ctx.gpr;
    const unsigned bases[]={36,35,34,33};
    for(unsigned column=0;column<p.columns;++column){
        const auto& c=p.column[column];const unsigned prefix=c.groups-1;
        for(unsigned group=0;group<prefix;++group){
            u128 values[4];const unsigned offset=c.offset+group*p.input_step;
            for(unsigned row=0;row<4;++row){
                values[row]=spu_csflt(geometry_typed_halfwords(ctx.ls,r[bases[row]]._u32[0],offset,c.crosses),139);
            }
            for(unsigned row=0;row<4;++row){
                uint32_t at=r[41]._u32[row]+column*16+group*p.output_step;
                if(at>p.limit)at=r[82]._u32[row];spu_ls_write128(&ctx,at,values[row]);
            }
        }
        geometry_i16_setup_abi(ctx);
        r[14]=spu_ai(r[14],prefix*p.output_step);r[17]=spu_ai(r[17],prefix*p.input_step);
        const unsigned current[]={7,16,9,15},following[]={8,11,10,18},next[]={40,39,38,37};
        const unsigned offset=c.offset+prefix*p.input_step;
        for(unsigned row=0;row<4;++row){
            r[current[row]]=spu_ls_read128(&ctx,r[bases[row]]._u32[0]+offset);
            if(c.crosses)r[following[row]]=spu_ls_read128(&ctx,r[next[row]]._u32[0]+offset);
        }
        if(c.crosses)geometry_i16_cross_final_abi(ctx);else geometry_i16_within_final_abi(ctx);
        r[20]=spu_ai(r[20],1);r[19]=spu_ai(r[19],16);r[13]=spu_clgt(r[107],r[20]);
    }
}
} // namespace buzz
