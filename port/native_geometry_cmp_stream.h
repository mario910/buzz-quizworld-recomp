/* Complete typed attribute payload loop with exact final pipeline ABI. */
#pragma once
#include "native_geometry_typed_stream.h"
#include "native_stagef_avx2.h"
namespace buzz {
using GeometryCmpStream=GeometryTypedStream;
inline bool read_geometry_cmp_stream(const spu_context& ctx,GeometryCmpStream& p,const char** reason=nullptr)
{
    const uint32_t table[]={0x203ffff,0x102ffff,0x1ffff,0xffffffff};
    return read_geometry_typed_stream(ctx,p,0xda80,table,false,reason);
}
inline u128 geometry_cmp_bits(const uint8_t* memory,unsigned source,unsigned offset)
{
    const uint8_t* at=memory+source+offset;u128 v;
    v._u32[0]=(uint32_t(at[2])<<24)|(uint32_t(at[3])<<16);
    v._u32[1]=(uint32_t(at[1])<<24)|(uint32_t(at[2])<<16);
    v._u32[2]=(uint32_t(at[0])<<24)|(uint32_t(at[1])<<16);v._u32[3]=0;
    return v;
}

inline void geometry_cmp_entry_abi(spu_context& ctx)
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
    r[18] = spu_il(0);
    r[17] = spu_fsmbi(0x0);
    r[19] = spu_ila(0x10203);
}

inline void geometry_cmp_setup_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[59] = spu_ori(r[18], 0);
    r[52] = spu_ls_read128(&ctx, 0xDA80);
    r[54] = spu_mpya(r[59], r[106], r[42]);
    r[56] = spu_shufb(r[17], r[17], r[19]);
    r[15] = spu_a(r[56], r[41]);
    r[55] = spu_andi(r[54], 15);
    r[47] = spu_ls_read128(&ctx, r[37]._u32[0] + r[54]._u32[0]);
    r[16] = spu_a(r[54], r[83]);
    r[49] = spu_ls_read128(&ctx, r[36]._u32[0] + r[54]._u32[0]);
    r[53] = spu_shufb(r[55], r[54], r[108]);
    r[50] = spu_ls_read128(&ctx, r[40]._u32[0] + r[54]._u32[0]);
    r[20] = spu_ls_read128(&ctx, r[35]._u32[0] + r[54]._u32[0]);
    r[21] = spu_ls_read128(&ctx, r[39]._u32[0] + r[54]._u32[0]);
    r[51] = spu_a(r[112], r[53]);
    r[48] = spu_ls_read128(&ctx, r[34]._u32[0] + r[54]._u32[0]);
    r[13] = spu_ls_read128(&ctx, r[38]._u32[0] + r[54]._u32[0]);
    r[46] = spu_ls_read128(&ctx, r[33]._u32[0] + r[54]._u32[0]);
    r[14] = spu_shufb(r[51], r[51], r[52]);
    r[45] = spu_shufb(r[49], r[50], r[14]);
    r[44] = spu_shufb(r[20], r[21], r[14]);
    r[43] = spu_shufb(r[48], r[13], r[14]);
    r[25] = spu_shufb(r[46], r[47], r[14]);
    r[2] = spu_shl(r[45], r[90]);
    r[3] = spu_shl(r[44], r[90]);
    r[8] = spu_shl(r[43], r[90]);
    r[5] = spu_shl(r[25], r[90]);
}

inline void geometry_cmp_cross_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[4] = spu_and(r[2], r[100]);
    r[57] = spu_shlqbyi(r[16], 0);
    r[30] = spu_and(r[3], r[100]);
    r[29] = spu_and(r[8], r[100]);
    r[27] = spu_csflt(r[4], 124);
    r[22] = spu_and(r[5], r[100]);
    r[10] = spu_csflt(r[30], 124);
    r[79] = spu_csflt(r[29], 124);
    r[9] = spu_csflt(r[22], 124);
    r[78] = spu_cgt(r[15], r[80]);
    r[28] = spu_a(r[16], r[83]);
    r[77] = spu_fma(r[27], r[99], r[98]);
    r[71] = spu_selb(r[15], r[82], r[78]);
    r[16] = spu_shlqbyi(r[28], 0);
    r[75] = spu_fma(r[10], r[99], r[98]);
    r[73] = spu_fma(r[79], r[99], r[98]);
    r[74] = spu_rotqbyi(r[71], 4);
    r[70] = spu_fma(r[9], r[99], r[98]);
    r[72] = spu_rotqbyi(r[71], 8);
    r[76] = spu_a(r[15], r[81]);
    r[69] = spu_rotqbyi(r[71], 12);
    spu_ls_write128(&ctx, r[71]._u32[0] + 0x0, r[77]);
    r[15] = spu_ori(r[76], 0);
    spu_ls_write128(&ctx, r[74]._u32[0] + 0x0, r[75]);
    spu_ls_write128(&ctx, r[72]._u32[0] + 0x0, r[73]);
    spu_ls_write128(&ctx, r[69]._u32[0] + 0x0, r[70]);
    r[24] = spu_ls_read128(&ctx, r[37]._u32[0] + r[57]._u32[0]);
    r[67] = spu_ls_read128(&ctx, r[36]._u32[0] + r[57]._u32[0]);
    r[68] = spu_ls_read128(&ctx, r[40]._u32[0] + r[57]._u32[0]);
    r[65] = spu_ls_read128(&ctx, r[35]._u32[0] + r[57]._u32[0]);
    r[66] = spu_ls_read128(&ctx, r[39]._u32[0] + r[57]._u32[0]);
    r[26] = spu_ls_read128(&ctx, r[34]._u32[0] + r[57]._u32[0]);
    r[63] = spu_ls_read128(&ctx, r[38]._u32[0] + r[57]._u32[0]);
    r[11] = spu_ls_read128(&ctx, r[33]._u32[0] + r[57]._u32[0]);
    r[64] = spu_shufb(r[67], r[68], r[14]);
    r[23] = spu_shufb(r[65], r[66], r[14]);
    r[2] = spu_shl(r[64], r[90]);
    r[62] = spu_shufb(r[26], r[63], r[14]);
    r[61] = spu_shufb(r[11], r[24], r[14]);
    r[3] = spu_shl(r[23], r[90]);
    r[8] = spu_shl(r[62], r[90]);
    r[5] = spu_shl(r[61], r[90]);
    r[18] = spu_ai(r[18], 1);
    r[17] = spu_ai(r[17], 16);
    r[7] = spu_clgt(r[107], r[18]);
}

inline void decode_geometry_cmp_stream(spu_context& ctx,const GeometryCmpStream& p)
{
    if(!p.columns)return;geometry_cmp_entry_abi(ctx);auto& r=ctx.gpr;
    const unsigned bases[]={36,35,34,33};
    for(unsigned column=0;column<p.columns;++column){
        const auto& c=p.column[column];const unsigned prefix=c.groups-1;
        for(unsigned group=0;group<prefix;++group){
            u128 values[4];const unsigned offset=c.offset+group*p.input_step;
            for(unsigned row=0;row<4;++row){
                const auto bits=geometry_cmp_bits(ctx.ls,r[bases[row]]._u32[0],offset);
                const auto packed=spu_and(spu_shl(bits,r[90]),r[100]);
                values[row]=spu_fma(spu_csflt(packed,124),r[99],r[98]);
            }
            for(unsigned row=0;row<4;++row){
                uint32_t at=r[41]._u32[row]+column*16+group*p.output_step;
                if(at>p.limit)at=r[82]._u32[row];spu_ls_write128(&ctx,at,values[row]);
            }
        }
        geometry_cmp_setup_abi(ctx);
        r[15]=spu_ai(r[15],prefix*p.output_step);r[16]=spu_ai(r[16],prefix*p.input_step);
        const unsigned pending[]={2,3,8,5};const unsigned offset=c.offset+prefix*p.input_step;
        for(unsigned row=0;row<4;++row)r[pending[row]]=spu_shl(geometry_cmp_bits(ctx.ls,r[bases[row]]._u32[0],offset),r[90]);
        geometry_cmp_cross_final_abi(ctx);
    }
}
} // namespace buzz
