/* Complete typed attribute payload loop with exact final pipeline ABI. */
#pragma once
#include "native_geometry_typed_stream.h"
#include "native_stagef_avx2.h"
namespace buzz {
using GeometryU8IntegerStream=GeometryTypedStream;
inline bool read_geometry_u8_integer_stream(const spu_context& ctx,GeometryU8IntegerStream& p,const char** reason=nullptr)
{
    const uint32_t table[]={0xff00ffff,0xff01ffff,0xff02ffff,0xff03ffff};
    return read_geometry_typed_stream(ctx,p,0xd8c0,table,true,reason);
}
inline u128 geometry_u8_integer_bits(const uint8_t* memory,unsigned source,unsigned offset,bool crosses)
{
    u128 v;
    for(unsigned lane=0;lane<4;++lane){
        const unsigned at=crosses?source+offset+lane:source+(offset&~15u)+((offset+lane)&15);
        v._u32[lane]=uint32_t(memory[at])<<16;
    }
    return v;
}

inline void geometry_u8_integer_entry_abi(spu_context& ctx)
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

inline void geometry_u8_integer_setup_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[18] = spu_ori(r[20], 0);
    r[54] = spu_ls_read128(&ctx, 0xD8C0);
    r[58] = spu_mpya(r[18], r[106], r[42]);
    r[60] = spu_shufb(r[19], r[19], r[21]);
    r[14] = spu_a(r[60], r[41]);
    r[59] = spu_andi(r[58], 15);
    r[18] = spu_ls_read128(&ctx, r[37]._u32[0] + r[58]._u32[0]);
    r[17] = spu_a(r[58], r[83]);
    r[7] = spu_ls_read128(&ctx, r[36]._u32[0] + r[58]._u32[0]);
    r[55] = spu_a(r[59], r[25]);
    r[56] = spu_shufb(r[59], r[58], r[108]);
    r[8] = spu_ls_read128(&ctx, r[40]._u32[0] + r[58]._u32[0]);
    r[52] = spu_cgti(r[55], 16);
    r[16] = spu_ls_read128(&ctx, r[35]._u32[0] + r[58]._u32[0]);
    r[11] = spu_ls_read128(&ctx, r[39]._u32[0] + r[58]._u32[0]);
    r[53] = spu_a(r[112], r[56]);
    r[9] = spu_ls_read128(&ctx, r[34]._u32[0] + r[58]._u32[0]);
    r[10] = spu_ls_read128(&ctx, r[38]._u32[0] + r[58]._u32[0]);
    r[15] = spu_ls_read128(&ctx, r[33]._u32[0] + r[58]._u32[0]);
    r[13] = spu_shufb(r[53], r[53], r[54]);
}

inline void geometry_u8_integer_cross_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[67] = spu_cgt(r[14], r[80]);
    r[70] = spu_shufb(r[7], r[8], r[13]);
    r[23] = spu_ori(r[17], 0);
    r[57] = spu_shufb(r[16], r[11], r[13]);
    r[71] = spu_a(r[17], r[83]);
    r[69] = spu_shufb(r[9], r[10], r[13]);
    r[66] = spu_a(r[14], r[81]);
    r[68] = spu_shufb(r[15], r[18], r[13]);
    r[65] = spu_cuflt(r[70], 139);
    r[17] = spu_shlqbyi(r[71], 0);
    r[63] = spu_cuflt(r[57], 139);
    r[15] = spu_selb(r[14], r[82], r[67]);
    r[14] = spu_shlqbyi(r[66], 0);
    r[24] = spu_cuflt(r[69], 139);
    r[16] = spu_cuflt(r[68], 139);
    r[26] = spu_rotqbyi(r[15], 4);
    r[11] = spu_rotqbyi(r[15], 8);
    r[8] = spu_rotqbyi(r[15], 12);
    spu_ls_write128(&ctx, r[15]._u32[0] + 0x0, r[65]);
    spu_ls_write128(&ctx, r[26]._u32[0] + 0x0, r[63]);
    spu_ls_write128(&ctx, r[11]._u32[0] + 0x0, r[24]);
    spu_ls_write128(&ctx, r[8]._u32[0] + 0x0, r[16]);
    r[18] = spu_ls_read128(&ctx, r[37]._u32[0] + r[23]._u32[0]);
    r[7] = spu_ls_read128(&ctx, r[36]._u32[0] + r[23]._u32[0]);
    r[8] = spu_ls_read128(&ctx, r[40]._u32[0] + r[23]._u32[0]);
    r[16] = spu_ls_read128(&ctx, r[35]._u32[0] + r[23]._u32[0]);
    r[11] = spu_ls_read128(&ctx, r[39]._u32[0] + r[23]._u32[0]);
    r[9] = spu_ls_read128(&ctx, r[34]._u32[0] + r[23]._u32[0]);
    r[10] = spu_ls_read128(&ctx, r[38]._u32[0] + r[23]._u32[0]);
    r[15] = spu_ls_read128(&ctx, r[33]._u32[0] + r[23]._u32[0]);
}

inline void geometry_u8_integer_within_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[27] = spu_cgt(r[14], r[80]);
    r[4] = spu_shufb(r[7], r[7], r[13]);
    r[73] = spu_ori(r[17], 0);
    r[74] = spu_selb(r[14], r[82], r[27]);
    r[28] = spu_shufb(r[16], r[16], r[13]);
    r[22] = spu_a(r[17], r[83]);
    r[5] = spu_shufb(r[9], r[9], r[13]);
    r[10] = spu_cuflt(r[4], 139);
    r[2] = spu_shufb(r[15], r[15], r[13]);
    r[3] = spu_a(r[14], r[81]);
    r[77] = spu_rotqbyi(r[74], 4);
    r[78] = spu_cuflt(r[28], 139);
    r[75] = spu_rotqbyi(r[74], 8);
    r[76] = spu_cuflt(r[5], 139);
    r[9] = spu_rotqbyi(r[74], 12);
    r[12] = spu_cuflt(r[2], 139);
    r[17] = spu_shlqbyi(r[22], 0);
    r[14] = spu_ori(r[3], 0);
    spu_ls_write128(&ctx, r[74]._u32[0] + 0x0, r[10]);
    spu_ls_write128(&ctx, r[77]._u32[0] + 0x0, r[78]);
    spu_ls_write128(&ctx, r[75]._u32[0] + 0x0, r[76]);
    spu_ls_write128(&ctx, r[9]._u32[0] + 0x0, r[12]);
    r[15] = spu_ls_read128(&ctx, r[33]._u32[0] + r[73]._u32[0]);
    r[7] = spu_ls_read128(&ctx, r[36]._u32[0] + r[73]._u32[0]);
    r[16] = spu_ls_read128(&ctx, r[35]._u32[0] + r[73]._u32[0]);
    r[9] = spu_ls_read128(&ctx, r[34]._u32[0] + r[73]._u32[0]);
}

inline void decode_geometry_u8_integer_stream(spu_context& ctx,const GeometryU8IntegerStream& p)
{
    if(!p.columns)return;geometry_u8_integer_entry_abi(ctx);auto& r=ctx.gpr;
    const unsigned bases[]={36,35,34,33};
    for(unsigned column=0;column<p.columns;++column){
        const auto& c=p.column[column];const unsigned prefix=c.groups-1;
        for(unsigned group=0;group<prefix;++group){
            u128 values[4];const unsigned offset=c.offset+group*p.input_step;
            for(unsigned row=0;row<4;++row){
                values[row]=spu_cuflt(geometry_u8_integer_bits(ctx.ls,r[bases[row]]._u32[0],offset,c.crosses),139);
            }
            for(unsigned row=0;row<4;++row){
                uint32_t at=r[41]._u32[row]+column*16+group*p.output_step;
                if(at>p.limit)at=r[82]._u32[row];spu_ls_write128(&ctx,at,values[row]);
            }
        }
        geometry_u8_integer_setup_abi(ctx);
        r[14]=spu_ai(r[14],prefix*p.output_step);r[17]=spu_ai(r[17],prefix*p.input_step);
        const unsigned current[]={7,16,9,15},following[]={8,11,10,18},next[]={40,39,38,37};
        const unsigned offset=c.offset+prefix*p.input_step;
        for(unsigned row=0;row<4;++row){
            r[current[row]]=spu_ls_read128(&ctx,r[bases[row]]._u32[0]+offset);
            if(c.crosses)r[following[row]]=spu_ls_read128(&ctx,r[next[row]]._u32[0]+offset);
        }
        if(c.crosses)geometry_u8_integer_cross_final_abi(ctx);else geometry_u8_integer_within_final_abi(ctx);
        r[20]=spu_ai(r[20],1);r[19]=spu_ai(r[19],16);r[7]=spu_clgt(r[107],r[20]);
    }
}
} // namespace buzz
