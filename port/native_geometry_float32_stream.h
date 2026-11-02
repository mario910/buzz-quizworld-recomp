/* Complete storage-2 float32 descriptor stream. Copy raw payload bits;
 * retain both cross-quad and circular within-quad paths and their distinct ABI. */
#pragma once
#include "native_geometry_output.h"
#include "native_stagef_avx2.h"

namespace buzz {
struct GeometryFloat32Column { uint32_t offset=0,groups=0; bool crosses=false; };
struct GeometryFloat32Stream {
    uint32_t columns=0,input_step=0,output_step=0,limit=0;
    GeometryFloat32Column column[16]{}; // bounded by admission; no per-call allocation
};
inline bool read_geometry_float32_stream(const spu_context& ctx,GeometryFloat32Stream& output,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(ctx.ls!=ctx.ls_store)return reject("foreign LS");
    GeometryFloat32Stream p;
    if(!ctx.gpr[121]._u32[0]){output=p;return true;}
    p.columns=ctx.gpr[107]._u32[0];p.input_step=ctx.gpr[83]._u32[0];
    p.output_step=ctx.gpr[81]._u32[0];p.limit=ctx.gpr[80]._u32[0];
    const uint32_t bytes=ctx.gpr[25]._u32[0],stride=ctx.gpr[106]._u32[0],attribute=ctx.gpr[42]._u32[0],stack=ctx.gpr[1]._u32[0];
    if(!bytes || bytes>16 || !p.columns || p.columns>16 || !stride || stride>255 || attribute>255 ||
        !p.input_step || (p.input_step&15) || !p.output_step || (p.output_step&15) ||
        p.limit>=SPU_LS_SIZE || !geometry_output_range(stack,0x80))return reject("shape/step/stack");
    for(unsigned byte=0;byte<16;++byte)
        if(ctx.gpr[112]._u8[byte^3]!=byte || ctx.gpr[108]._u8[byte]!=3)return reject("gather registers");
    uint64_t input_begin=SPU_LS_SIZE,input_end=0,output_begin=SPU_LS_SIZE,output_end=0;
    const uint32_t scratch=ctx.gpr[82]._u32[0];
    if(!geometry_output_range(scratch,16))return reject("scratch range");
    output_begin=scratch;output_end=uint64_t(scratch)+16;
    if(scratch!=stack+0x20 && scratch<stack+0x80 && stack<scratch+16)return reject("scratch/stack metadata alias");
    const unsigned bases[]={118,117,116,124};
    for(unsigned row=0;row<4;++row){
        const uint32_t base=ctx.gpr[bases[row]]._u32[0];
        if((base&15) || geometry_job_be32(ctx.ls+stack+0x70-row*16)!=uint64_t(base)+16 ||
            ctx.gpr[83]._u32[row]!=p.input_step || ctx.gpr[81]._u32[row]!=p.output_step ||
            ctx.gpr[80]._u32[row]!=p.limit)return reject("input pointers/steps/scratch");
    }
    for(unsigned column=0;column<p.columns;++column){
        GeometryFloat32Column c;c.offset=column*stride+attribute;c.crosses=(c.offset&15)+bytes>16;
        const uint64_t first=uint64_t(ctx.gpr[41]._u32[0])+column*16;
        if(first>INT32_MAX)return reject("preferred cursor");
        c.groups=first>p.limit?1:(p.limit-uint32_t(first))/p.output_step+2;
        if(c.groups>4096)return reject("group count");
        for(unsigned row=0;row<4;++row){
            const uint64_t begin=uint64_t(ctx.gpr[bases[row]]._u32[0])+c.offset;
            const uint64_t end=begin+uint64_t(c.groups)*p.input_step+32;
            if(begin<0xe680 || end>SPU_LS_SIZE)return reject("source lookahead bounds");
            input_begin=std::min(input_begin,begin&~15ull);input_end=std::max(input_end,end);
            const uint64_t destination=uint64_t(ctx.gpr[41]._u32[row])+column*16;
            const uint64_t last=destination+uint64_t(c.groups-1)*p.output_step;
            if(last>INT32_MAX)return reject("output cursor bounds");
            if(destination<=p.limit){
                const uint64_t physical=destination&~15ull,last_end=(std::min(last,uint64_t(p.limit))&~15ull)+16;
                output_begin=std::min(output_begin,physical);output_end=std::max(output_end,last_end);
                if(physical<stack+0x80 && stack<last_end)return reject("destination/stack metadata alias");
            }
        }
        p.column[column]=c;
    }
    if(output_begin<0xe680 || output_end>SPU_LS_SIZE ||
        (input_begin<output_end && output_begin<input_end))return reject("output/source bounds or alias");
    output=p;return true;
}
// Per-column setup remains anchored to group zero: its setup-only and
// path-specific registers are observable even after a different final path.
inline void geometry_float32_setup_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[2] = spu_ori(r[23], 0);
    r[78] = spu_shufb(r[24], r[24], r[26]);
    r[75] = spu_mpya(r[2], r[106], r[42]);
    r[72] = spu_a(r[78], r[41]);
    r[15] = spu_ori(r[72], 0);
    r[14] = spu_shlqbyi(r[72], 4);
    r[13] = spu_shlqbyi(r[72], 12);
    r[77] = spu_andi(r[75], 15);
    r[22] = spu_ls_read128(&ctx, r[37]._u32[0] + r[75]._u32[0]);
    r[18] = spu_a(r[75], r[83]);
    r[16] = spu_ls_read128(&ctx, r[36]._u32[0] + r[75]._u32[0]);
    r[76] = spu_a(r[77], r[25]);
    r[73] = spu_shufb(r[77], r[75], r[108]);
    r[21] = spu_ls_read128(&ctx, r[40]._u32[0] + r[75]._u32[0]);
    r[70] = spu_cgti(r[76], 16);
    r[8] = spu_ls_read128(&ctx, r[35]._u32[0] + r[75]._u32[0]);
    r[20] = spu_ls_read128(&ctx, r[39]._u32[0] + r[75]._u32[0]);
    r[71] = spu_a(r[112], r[73]);
    r[9] = spu_ls_read128(&ctx, r[34]._u32[0] + r[75]._u32[0]);
    r[19] = spu_ls_read128(&ctx, r[38]._u32[0] + r[75]._u32[0]);
    r[17] = spu_ls_read128(&ctx, r[33]._u32[0] + r[75]._u32[0]);
    r[12] = spu_shlqbyi(r[72], 8);
    r[11] = spu_shufb(r[71], r[71], r[112]);
}
inline void geometry_float32_cross_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[43] = spu_cgt(r[15], r[80]);
    r[32] = spu_shufb(r[16], r[21], r[11]);
    r[30] = spu_cgt(r[14], r[80]);
    r[29] = spu_shufb(r[8], r[20], r[11]);
    r[28] = spu_cgt(r[12], r[80]);
    r[21] = spu_shufb(r[9], r[19], r[11]);
    r[31] = spu_selb(r[15], r[82], r[43]);
    r[19] = spu_shufb(r[17], r[22], r[11]);
    r[22] = spu_selb(r[14], r[82], r[30]);
    r[17] = spu_cgt(r[13], r[80]);
    spu_ls_write128(&ctx, r[31]._u32[0] + 0x0, r[32]);
    r[20] = spu_selb(r[12], r[82], r[28]);
    spu_ls_write128(&ctx, r[22]._u32[0] + 0x0, r[29]);
    r[16] = spu_selb(r[13], r[82], r[17]);
    r[7] = spu_a(r[18], r[83]);
    spu_ls_write128(&ctx, r[20]._u32[0] + 0x0, r[21]);
    r[6] = spu_a(r[15], r[81]);
    spu_ls_write128(&ctx, r[16]._u32[0] + 0x0, r[19]);
    r[5] = spu_a(r[14], r[81]);
    r[22] = spu_ls_read128(&ctx, r[37]._u32[0] + r[18]._u32[0]);
    r[4] = spu_a(r[12], r[81]);
    r[21] = spu_ls_read128(&ctx, r[40]._u32[0] + r[18]._u32[0]);
    r[3] = spu_a(r[13], r[81]);
    r[16] = spu_ls_read128(&ctx, r[36]._u32[0] + r[18]._u32[0]);
    r[15] = spu_ori(r[6], 0);
    r[20] = spu_ls_read128(&ctx, r[39]._u32[0] + r[18]._u32[0]);
    r[14] = spu_ori(r[5], 0);
    r[8] = spu_ls_read128(&ctx, r[35]._u32[0] + r[18]._u32[0]);
    r[12] = spu_ori(r[4], 0);
    r[19] = spu_ls_read128(&ctx, r[38]._u32[0] + r[18]._u32[0]);
    r[13] = spu_ori(r[3], 0);
    r[9] = spu_ls_read128(&ctx, r[34]._u32[0] + r[18]._u32[0]);
    r[17] = spu_ls_read128(&ctx, r[33]._u32[0] + r[18]._u32[0]);
    r[18] = spu_ori(r[7], 0);
}
inline void geometry_float32_within_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[62] = spu_cgt(r[15], r[80]);
    r[59] = spu_cgt(r[14], r[80]);
    r[61] = spu_shufb(r[16], r[16], r[11]);
    r[55] = spu_cgt(r[12], r[80]);
    r[58] = spu_shufb(r[8], r[8], r[11]);
    r[60] = spu_selb(r[15], r[82], r[62]);
    r[54] = spu_shufb(r[9], r[9], r[11]);
    r[56] = spu_selb(r[14], r[82], r[59]);
    r[51] = spu_shufb(r[17], r[17], r[11]);
    r[52] = spu_cgt(r[13], r[80]);
    spu_ls_write128(&ctx, r[60]._u32[0] + 0x0, r[61]);
    r[53] = spu_selb(r[12], r[82], r[55]);
    spu_ls_write128(&ctx, r[56]._u32[0] + 0x0, r[58]);
    r[8] = spu_selb(r[13], r[82], r[52]);
    r[49] = spu_a(r[18], r[83]);
    spu_ls_write128(&ctx, r[53]._u32[0] + 0x0, r[54]);
    r[48] = spu_a(r[15], r[81]);
    spu_ls_write128(&ctx, r[8]._u32[0] + 0x0, r[51]);
    r[47] = spu_a(r[14], r[81]);
    r[17] = spu_ls_read128(&ctx, r[33]._u32[0] + r[18]._u32[0]);
    r[46] = spu_a(r[12], r[81]);
    r[8] = spu_ls_read128(&ctx, r[35]._u32[0] + r[18]._u32[0]);
    r[45] = spu_a(r[13], r[81]);
    r[16] = spu_ls_read128(&ctx, r[36]._u32[0] + r[18]._u32[0]);
    r[15] = spu_ori(r[48], 0);
    r[9] = spu_ls_read128(&ctx, r[34]._u32[0] + r[18]._u32[0]);
    r[14] = spu_ori(r[47], 0);
    r[12] = spu_shlqbyi(r[46], 0);
    r[18] = spu_ori(r[49], 0);
    r[13] = spu_shlqbyi(r[45], 0);
}
inline void decode_geometry_float32_stream(spu_context& ctx,const GeometryFloat32Stream& p)
{
    if(!p.columns)return;
    auto& r=ctx.gpr;
    r[36]=r[118];r[40]=geometry_output_quad(ctx,r[1]._u32[0]+0x70);
    r[35]=r[117];r[39]=geometry_output_quad(ctx,r[1]._u32[0]+0x60);
    r[34]=r[116];r[38]=geometry_output_quad(ctx,r[1]._u32[0]+0x50);
    r[33]=r[124];r[37]=geometry_output_quad(ctx,r[1]._u32[0]+0x40);
    r[23]=spu_il(0);r[24]=spu_fsmbi(0);r[26]=spu_ila(0x10203);
    const unsigned bases[]={36,35,34,33},next[]={40,39,38,37};
    for(unsigned column=0;column<p.columns;++column){
        const auto& c=p.column[column];const unsigned prefix=c.groups-1;
        const __m128i rotation=_mm_add_epi8(_mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15),_mm_set1_epi8(char(c.offset&15)));
        for(unsigned group=0;group<prefix;++group){
            const unsigned offset=c.offset+group*p.input_step;
            for(unsigned row=0;row<4;++row){
                const unsigned source=r[bases[row]]._u32[0];
                const __m128i bytes=c.crosses?_mm_loadu_si128(reinterpret_cast<const __m128i*>(ctx.ls+source+offset)):
                    _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(ctx.ls+source+(offset&~15u))),rotation);
                uint32_t at=r[41]._u32[row]+column*16+group*p.output_step;
                // Every shifted cursor is clipped against the same sentinel
                // vector: the preferred scratch word serves all four rows.
                if(at>p.limit)at=r[82]._u32[0];
                _mm_storeu_si128(reinterpret_cast<__m128i*>(ctx.ls+(at&~15u)),bytes);
            }
        }
        geometry_float32_setup_abi(ctx);
        u128 output_shift,input_shift;
        for(unsigned lane=0;lane<4;++lane){output_shift._u32[lane]=prefix*r[81]._u32[lane];input_shift._u32[lane]=prefix*r[83]._u32[lane];}
        for(unsigned cursor:{15u,14u,12u,13u})r[cursor]=spu_a(r[cursor],output_shift);
        r[18]=spu_a(r[18],input_shift);
        const unsigned offset=c.offset+prefix*p.input_step;
        const unsigned pending[]={16,8,9,17},pending_next[]={21,20,19,22};
        for(unsigned row=0;row<4;++row){
            r[pending[row]]=spu_ls_read128(&ctx,r[bases[row]]._u32[0]+offset);
            if(c.crosses)r[pending_next[row]]=spu_ls_read128(&ctx,r[next[row]]._u32[0]+offset);
        }
        if(c.crosses)geometry_float32_cross_final_abi(ctx);
        else geometry_float32_within_final_abi(ctx);
        r[23]=spu_ai(r[23],1);r[24]=spu_ai(r[24],16);r[11]=spu_clgt(r[107],r[23]);
    }
}
} // namespace buzz
