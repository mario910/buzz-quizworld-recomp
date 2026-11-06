/* Complete storage-8 variable-bit fixed-point attribute decoder.
 * Host bit extraction owns all payload groups; only the final group recreates
 * the original pipeline ABI and its mandatory trailing prefetch/store. */
#pragma once
#include "native_geometry_output.h"
#include "native_stagef_avx2.h"
#include <array>

namespace buzz {
struct GeometryFixedColumn { uint32_t offset=0,groups=0; };
struct GeometryFixedStream {
    uint32_t columns=0,input_step=0,output_step=0,limit=0;
    std::array<unsigned,4> width{},fraction{};
    u128 offsets{};
    GeometryFixedColumn column[17]{}; // bounded by admission; no per-call allocation
};
inline bool read_geometry_fixed_stream(const spu_context& ctx,GeometryFixedStream& output,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(ctx.ls!=ctx.ls_store)return reject("foreign LS");
    GeometryFixedStream p;p.columns=ctx.gpr[107]._u32[0]+1;
    p.input_step=ctx.gpr[83]._u32[0];p.output_step=ctx.gpr[81]._u32[0];p.limit=ctx.gpr[80]._u32[0];
    const uint32_t stride=ctx.gpr[106]._u32[0],attribute=ctx.gpr[42]._u32[0],stack=ctx.gpr[1]._u32[0];
    if(ctx.gpr[107]._u32[0]>16 || !stride || stride>255 || attribute>255 ||
        !p.input_step || (p.input_step&15) || !p.output_step || (p.output_step&15) ||
        p.limit>=SPU_LS_SIZE || !geometry_output_range(stack,0x120))return reject("shape/step/stack");
    struct Table {unsigned at;uint32_t word[4];};
    static const Table tables[]={
        {0xd230,{0x80808001,0x80808003,0x80808005,0x80808007}},
        {0xd240,{0x80808000,0x80808002,0x80808004,0x80808006}},
        {0xd5d0,{0x03030303,0x03030303,0x03030303,0x03030303}},
        {0xd5e0,{0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f}},
        {0xd5f0,{0x00010203,0x00010203,0x00010203,0x00010203}}};
    for(const auto& t:tables){const auto actual=geometry_output_quad(ctx,t.at);
        if(memcmp(actual._u32,t.word,16))return reject("fixed gather tables");}
    const auto selector=geometry_output_quad(ctx,stack+0xe0);
    const auto metadata_pointer=spu_a(geometry_output_quad(ctx,stack+0x110),spu_shufb(ctx.gpr[84],ctx.gpr[84],selector));
    const uint32_t metadata=metadata_pointer._u32[0]&~15u;
    const uint32_t offsets=geometry_job_be32(ctx.ls+stack+0xd0)&~15u;
    if(!geometry_output_range(metadata,16) || !geometry_output_range(offsets,16))return reject("metadata/offset bounds");
    if(metadata==stack+0xd0)return reject("width metadata/update alias");
    const auto widths=spu_rotqby(geometry_output_quad(ctx,metadata),metadata_pointer);
    for(unsigned lane=0;lane<4;++lane){
        const unsigned integer=widths._u8[(lane*2)^3],fraction=widths._u8[(lane*2+1)^3];
        if(integer+fraction>32)return reject("component width");
        p.width[lane]=integer+fraction;p.fraction[lane]=fraction;
    }
    p.offsets=geometry_output_quad(ctx,offsets);
    auto overlaps_metadata=[&](uint64_t begin,uint64_t end){
        return (begin<metadata+16 && metadata<end) || (begin<offsets+16 && offsets<end);
    };
    uint64_t input_begin=SPU_LS_SIZE,input_end=0,output_begin=SPU_LS_SIZE,output_end=0;
    const unsigned bases[]={118,117,116,124};
    for(unsigned row=0;row<4;++row){
        const uint32_t base=ctx.gpr[bases[row]]._u32[0],scratch=ctx.gpr[82]._u32[row];
        if((base&15) || geometry_job_be32(ctx.ls+stack+0x70-row*16)!=uint64_t(base)+16 ||
            ctx.gpr[83]._u32[row]!=p.input_step || ctx.gpr[81]._u32[row]!=p.output_step ||
            ctx.gpr[80]._u32[row]!=p.limit || !geometry_output_range(scratch,16))return reject("input pointers/steps/scratch");
        output_begin=std::min(output_begin,uint64_t(scratch));output_end=std::max(output_end,uint64_t(scratch)+16);
        if(overlaps_metadata(scratch,uint64_t(scratch)+16))return reject("scratch/metadata alias");
        if(scratch!=stack+0x20 && scratch<stack+0x120 && stack<scratch+16)return reject("scratch/stack metadata alias");
    }
    for(unsigned column=0;column<p.columns;++column){
        GeometryFixedColumn c;c.offset=column*stride+attribute;
        const uint64_t first=uint64_t(ctx.gpr[41]._u32[0])+column*16;
        if(first>INT32_MAX)return reject("preferred cursor");
        c.groups=first>p.limit?1:(p.limit-uint32_t(first))/p.output_step+2;
        if(c.groups>4096)return reject("group count");
        for(unsigned row=0;row<4;++row){
            const uint64_t begin=uint64_t(ctx.gpr[bases[row]]._u32[0])+c.offset;
            const uint64_t end=begin+uint64_t(c.groups)*p.input_step+32;
            if(begin<0xe680 || end>SPU_LS_SIZE)return reject("source lookahead bounds");
            input_begin=std::min(input_begin,begin&~uint64_t{15});input_end=std::max(input_end,end);
            const uint64_t destination=uint64_t(ctx.gpr[41]._u32[row])+column*16;
            const uint64_t last=destination+uint64_t(c.groups-1)*p.output_step;
            if(last>INT32_MAX)return reject("output cursor bounds");
            if(destination<=p.limit){
                const uint64_t physical=destination&~uint64_t{15},last_end=(std::min(last,uint64_t(p.limit))&~uint64_t{15})+16;
                output_begin=std::min(output_begin,physical);output_end=std::max(output_end,last_end);
                if(overlaps_metadata(physical,last_end))return reject("destination/metadata alias");
                if(physical<stack+0x120 && stack<last_end)return reject("destination/stack metadata alias");
            }
        }
        p.column[column]=c;
    }
    if(output_begin<0xe680 || output_end>SPU_LS_SIZE ||
        (input_begin<output_end && output_begin<input_end) ||
        (input_begin<stack+0xe0 && stack+0xd0<input_end))return reject("output/source/metadata bounds or alias");
    output=p;return true;
}
inline u128 geometry_fixed_payload(const uint8_t* memory,const GeometryFixedStream& p)
{
    u128 integer,fraction;unsigned bit=0;
    for(unsigned lane=0;lane<4;++lane){
        uint32_t value=0;unsigned remaining=p.width[lane];
        while(remaining){
            const unsigned take=std::min(remaining,8-(bit&7));
            value=(value<<take)|((memory[bit/8]>>(8-(bit&7)-take))&((1u<<take)-1));
            remaining-=take;bit+=take;
        }
        value+=p.offsets._u32[lane];const unsigned f=p.fraction[lane];
        integer._s32[lane]=int32_t(value)>>(f==32?31:f);
        fraction._u32[lane]=f?value<<(32-f):0;
    }
    return spu_fa(spu_csflt(integer,155),spu_cuflt(fraction,123));
}

inline void geometry_fixed_entry_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[4] = spu_ori(r[41], 0);
    r[25] = spu_ls_read128(&ctx, r[1]._u32[0] + 0xE0);
    r[37] = spu_il(7);
    r[5] = spu_ls_read128(&ctx, r[1]._u32[0] + 0xD0);
    r[35] = spu_il(-1);
    r[6] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x110);
    r[3] = spu_ori(r[107], 0);
    r[43] = spu_ls_read128(&ctx, r[1]._u32[0] + 0xD0);
    r[7] = spu_ori(r[83], 0);
    r[32] = spu_ls_read128(&ctx, 0xD230);
    r[12] = spu_ori(r[82], 0);
    r[31] = spu_ls_read128(&ctx, 0xD240);
    r[13] = spu_ori(r[80], 0);
    r[18] = spu_shufb(r[84], r[84], r[25]);
    r[14] = spu_ai(r[5], 16);
    r[5] = spu_ori(r[106], 0);
    r[16] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x70);
    r[15] = spu_ori(r[118], 0);
    r[9] = spu_ls_read128(&ctx, r[43]._u32[0] + 0x0);
    r[17] = spu_a(r[6], r[18]);
    spu_ls_write128(&ctx, r[1]._u32[0] + 0xD0, r[14]);
    r[6] = spu_ori(r[42], 0);
    r[20] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x50);
    r[14] = spu_ori(r[81], 0);
    r[19] = spu_ls_read128(&ctx, r[17]._u32[0] + 0x0);
    r[21] = spu_ori(r[124], 0);
    r[18] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x60);
    r[22] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x40);
    r[42] = spu_rotqby(r[19], r[17]);
    r[17] = spu_ori(r[117], 0);
    r[19] = spu_shlqbyi(r[116], 0);
    r[40] = spu_shufb(r[42], r[42], r[32]);
    r[41] = spu_shufb(r[42], r[42], r[31]);
    r[11] = spu_sfi(r[40], 32);
    r[39] = spu_a(r[41], r[40]);
    r[10] = spu_sfi(r[40], 0);
    r[38] = spu_sfi(r[39], 32);
    r[26] = spu_sfi(r[38], 0);
    r[36] = spu_sfi(r[38], 7);
    r[8] = spu_rotm(r[35], r[26]);
    r[33] = spu_selb(r[36], r[26], r[37]);
    r[23] = spu_ori(r[33], 0);
    r[24] = spu_shlqbyi(r[33], 4);
    r[25] = spu_shlqbyi(r[33], 8);
    r[0] = spu_link(0x9110);
    r[60] = spu_il(0);
    r[27] = spu_ls_read128(&ctx, 0xD5E0);
    r[28] = spu_fsmbi(0xF0F);
    r[29] = spu_fsmbi(0xFF);
    r[30] = spu_ls_read128(&ctx, 0xD5F0);
    r[31] = spu_ls_read128(&ctx, 0xD5D0);
    r[2] = spu_ai(r[27], 0);

}

inline void geometry_fixed_setup_abi(spu_context& ctx,const GeometryFixedStream& p,unsigned column,unsigned group)
{
    auto& r=ctx.gpr;r[60]=spu_il(column);
    r[40] = spu_mpya(r[60], r[5], r[6]);
    r[40] = spu_ai(r[40],group*p.input_step);
    r[75] = spu_andi(r[40], 15);
    r[49] = spu_ls_read128(&ctx, r[22]._u32[0] + r[40]._u32[0]);
    r[48] = spu_ls_read128(&ctx, r[21]._u32[0] + r[40]._u32[0]);
    r[74] = spu_shufb(r[75], r[40], r[31]);
    r[47] = spu_ls_read128(&ctx, r[20]._u32[0] + r[40]._u32[0]);
    r[46] = spu_ls_read128(&ctx, r[19]._u32[0] + r[40]._u32[0]);
    r[45] = spu_ls_read128(&ctx, r[18]._u32[0] + r[40]._u32[0]);
    r[66] = spu_a(r[27], r[74]);
    r[44] = spu_ls_read128(&ctx, r[17]._u32[0] + r[40]._u32[0]);
    r[51] = spu_shli(r[60], 4);
    r[43] = spu_ls_read128(&ctx, r[16]._u32[0] + r[40]._u32[0]);
    r[59] = spu_shufb(r[66], r[66], r[2]);
    r[50] = spu_ls_read128(&ctx, r[15]._u32[0] + r[40]._u32[0]);
    r[38] = spu_shufb(r[51], r[51], r[30]);
    r[65] = spu_shufb(r[48], r[49], r[59]);
    r[67] = spu_shufb(r[46], r[47], r[59]);
    r[68] = spu_shufb(r[44], r[45], r[59]);
    r[69] = spu_shufb(r[50], r[43], r[59]);
    r[72] = spu_rotqmbybi(r[65], r[23]);
    r[71] = spu_rotqmbybi(r[67], r[23]);
    r[70] = spu_rotqmbybi(r[68], r[23]);
    r[73] = spu_rotqmbybi(r[69], r[23]);
    r[58] = spu_rotqmbi(r[72], r[23]);
    r[57] = spu_rotqmbi(r[71], r[23]);
    r[56] = spu_rotqmbi(r[70], r[23]);
    r[55] = spu_rotqmbi(r[73], r[23]);
    r[64] = spu_rotqmbybi(r[58], r[24]);
    r[63] = spu_rotqmbybi(r[57], r[24]);
    r[62] = spu_rotqmbybi(r[56], r[24]);
    r[61] = spu_rotqmbybi(r[55], r[24]);
    r[54] = spu_rotqmbi(r[64], r[24]);
    r[53] = spu_rotqmbi(r[63], r[24]);
    r[52] = spu_rotqmbi(r[62], r[24]);
    r[51] = spu_rotqmbi(r[61], r[24]);
    r[42] = spu_rotqmbybi(r[54], r[25]);
    r[41] = spu_rotqmbybi(r[53], r[25]);
    r[39] = spu_rotqmbybi(r[52], r[25]);
    r[37] = spu_rotqmbybi(r[51], r[25]);
    r[47] = spu_a(r[40], r[7]);
    r[49] = spu_rotqmbi(r[42], r[25]);
    r[45] = spu_a(r[38], r[4]);
    r[45] = spu_ai(r[45],group*p.output_step);
    r[48] = spu_rotqmbi(r[41], r[25]);
    r[50] = spu_cgt(r[3], r[60]);
    r[46] = spu_rotqmbi(r[39], r[25]);
    r[60] = spu_ai(r[60], 1);
    r[44] = spu_rotqmbi(r[37], r[25]);
    r[43] = spu_rotm(r[49], r[26]);
    r[42] = spu_rotm(r[48], r[26]);
    r[61] = spu_rotm(r[46], r[26]);
    r[41] = spu_rotm(r[44], r[26]);

}

inline void geometry_fixed_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[35] = spu_selb(r[55], r[51], r[28]);
    r[34] = spu_ls_read128(&ctx, r[15]._u32[0] + r[47]._u32[0]);
    r[51] = spu_selb(r[48], r[42], r[28]);
    r[36] = spu_ls_read128(&ctx, r[16]._u32[0] + r[47]._u32[0]);
    r[37] = spu_selb(r[44], r[41], r[28]);
    r[48] = spu_ls_read128(&ctx, r[20]._u32[0] + r[47]._u32[0]);
    r[39] = spu_selb(r[56], r[52], r[28]);
    r[44] = spu_ls_read128(&ctx, r[19]._u32[0] + r[47]._u32[0]);
    r[41] = spu_selb(r[46], r[61], r[28]);
    r[38] = spu_ls_read128(&ctx, r[17]._u32[0] + r[47]._u32[0]);
    r[46] = spu_selb(r[57], r[53], r[28]);
    r[40] = spu_ls_read128(&ctx, r[18]._u32[0] + r[47]._u32[0]);
    r[53] = spu_selb(r[58], r[54], r[28]);
    r[52] = spu_ls_read128(&ctx, r[21]._u32[0] + r[47]._u32[0]);
    r[54] = spu_selb(r[49], r[43], r[28]);
    r[55] = spu_ls_read128(&ctx, r[22]._u32[0] + r[47]._u32[0]);
    r[73] = spu_selb(r[46], r[51], r[29]);
    r[70] = spu_shufb(r[34], r[36], r[59]);
    r[74] = spu_selb(r[53], r[54], r[29]);
    r[75] = spu_shufb(r[44], r[48], r[59]);
    r[71] = spu_selb(r[39], r[41], r[29]);
    r[66] = spu_selb(r[35], r[37], r[29]);
    r[72] = spu_shufb(r[38], r[40], r[59]);
    r[33] = spu_and(r[74], r[8]);
    r[76] = spu_rotqmbybi(r[70], r[23]);
    r[35] = spu_and(r[73], r[8]);
    r[36] = spu_shufb(r[52], r[55], r[59]);
    r[78] = spu_and(r[71], r[8]);
    r[32] = spu_rotqmbybi(r[75], r[23]);
    r[77] = spu_and(r[66], r[8]);
    r[34] = spu_rotqmbybi(r[72], r[23]);
    r[63] = spu_a(r[33], r[9]);
    r[55] = spu_rotqmbi(r[76], r[23]);
    r[65] = spu_a(r[35], r[9]);
    r[79] = spu_rotqmbybi(r[36], r[23]);
    r[67] = spu_a(r[78], r[9]);
    r[57] = spu_rotqmbi(r[32], r[23]);
    r[69] = spu_a(r[77], r[9]);
    r[56] = spu_rotqmbi(r[34], r[23]);
    r[75] = spu_shl(r[67], r[11]);
    r[68] = spu_rotqmbybi(r[55], r[24]);
    r[78] = spu_rotma(r[63], r[10]);
    r[58] = spu_rotqmbi(r[79], r[23]);
    r[79] = spu_rotma(r[65], r[10]);
    r[62] = spu_rotqmbybi(r[57], r[24]);
    r[72] = spu_shl(r[63], r[11]);
    r[64] = spu_rotqmbybi(r[56], r[24]);
    r[33] = spu_rotma(r[67], r[10]);
    r[51] = spu_rotqmbi(r[68], r[24]);
    r[74] = spu_shl(r[65], r[11]);
    r[32] = spu_rotqmbybi(r[58], r[24]);
    r[61] = spu_rotma(r[69], r[10]);
    r[53] = spu_rotqmbi(r[62], r[24]);
    r[77] = spu_shl(r[69], r[11]);
    r[52] = spu_rotqmbi(r[64], r[24]);
    r[62] = spu_cuflt(r[72], 123);
    r[76] = spu_rotqmbybi(r[51], r[25]);
    r[64] = spu_cuflt(r[74], 123);
    r[54] = spu_rotqmbi(r[32], r[24]);
    r[65] = spu_csflt(r[79], 155);
    r[71] = spu_rotqmbybi(r[53], r[25]);
    r[68] = spu_cuflt(r[77], 123);
    r[73] = spu_rotqmbybi(r[52], r[25]);
    r[67] = spu_cuflt(r[75], 123);
    r[44] = spu_rotqmbi(r[76], r[25]);
    r[63] = spu_csflt(r[78], 155);
    r[70] = spu_rotqmbybi(r[54], r[25]);
    r[66] = spu_csflt(r[33], 155);
    r[48] = spu_rotqmbi(r[71], r[25]);
    r[69] = spu_csflt(r[61], 155);
    r[46] = spu_rotqmbi(r[73], r[25]);
    r[40] = spu_cgt(r[45], r[13]);
    r[35] = spu_fa(r[65], r[64]);
    r[49] = spu_rotqmbi(r[70], r[25]);
    r[39] = spu_selb(r[45], r[12], r[40]);
    r[42] = spu_rotm(r[48], r[26]);
    r[33] = spu_fa(r[63], r[62]);
    r[32] = spu_rotqbyi(r[39], 12);
    r[37] = spu_fa(r[66], r[67]);
    r[34] = spu_rotqbyi(r[39], 8);
    r[38] = spu_fa(r[69], r[68]);
    r[36] = spu_rotqbyi(r[39], 4);
    r[43] = spu_rotm(r[49], r[26]);
    r[41] = spu_rotm(r[44], r[26]);
    r[61] = spu_rotm(r[46], r[26]);
    spu_ls_write128(&ctx, r[34]._u32[0] + 0x0, r[35]);
    r[45] = spu_a(r[45], r[14]);
    spu_ls_write128(&ctx, r[32]._u32[0] + 0x0, r[33]);
    r[47] = spu_a(r[47], r[7]);
    spu_ls_write128(&ctx, r[36]._u32[0] + 0x0, r[37]);
    spu_ls_write128(&ctx, r[39]._u32[0] + 0x0, r[38]);
}

inline void decode_geometry_fixed_stream(spu_context& ctx,const GeometryFixedStream& p)
{
    geometry_fixed_entry_abi(ctx);auto& r=ctx.gpr;
    const unsigned bases[]={15,17,19,21},order[]={2,3,1,0};
    for(unsigned column=0;column<p.columns;++column){
        const auto& c=p.column[column];const bool last=column+1==p.columns;
        const unsigned prefix=c.groups-(last?1:0);
        for(unsigned group=0;group<prefix;++group){
            u128 values[4];
            for(unsigned row=0;row<4;++row)values[row]=geometry_fixed_payload(ctx.ls+r[bases[row]]._u32[0]+c.offset+group*p.input_step,p);
            for(unsigned row:order){
                uint32_t at=r[4]._u32[row]+column*16+group*p.output_step;
                if(at>p.limit)at=r[12]._u32[row];
                spu_ls_write128(&ctx,at,values[row]);
            }
        }
        if(last){geometry_fixed_setup_abi(ctx,p,column,prefix);geometry_fixed_final_abi(ctx);}
    }
}
} // namespace buzz
