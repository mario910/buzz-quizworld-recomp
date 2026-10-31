/* Complete storage-3 halfword stream decoder, including its pipeline ABI.
 * Preserve the table shuffle's low output bits; hardware IEEE half conversion
 * does not reproduce this stream contract. No original final-column callback. */
#pragma once
#include "native_geometry_output.h"
#include "native_stagef_avx2.h"
#include <vector>

namespace buzz {
struct GeometryHalfColumn { uint32_t offset=0,groups=0; };
struct GeometryHalfStream {
    uint32_t columns=0,input_step=0,output_step=0,limit=0;
    std::vector<GeometryHalfColumn> column;
};
inline bool read_geometry_half_stream(const spu_context& ctx,GeometryHalfStream& output,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(ctx.ls!=ctx.ls_store)return reject("foreign LS");
    GeometryHalfStream p;p.columns=ctx.gpr[107]._u32[0];p.input_step=ctx.gpr[83]._u32[0];
    p.output_step=ctx.gpr[81]._u32[0];p.limit=ctx.gpr[80]._u32[0];
    const uint32_t stride=ctx.gpr[106]._u32[0],attribute=ctx.gpr[42]._u32[0],stack=ctx.gpr[1]._u32[0];
    if(!p.columns || p.columns>16 || !stride || stride>255 || attribute>255 ||
        !p.input_step || (p.input_step&15) || !p.output_step || (p.output_step&15) ||
        p.limit>=SPU_LS_SIZE || !geometry_output_range(stack,0x80))return reject("shape/step/stack");
    struct Table { unsigned at; uint32_t word[4]; };
    static const Table tables[]={
        {0xd5d0,{0x03030303,0x03030303,0x03030303,0x03030303}},
        {0xd5e0,{0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f}},
        {0xd5f0,{0x00010203,0x00010203,0x00010203,0x00010203}},
        {0xd600,{0x00018080,0x02038080,0x04058080,0x06078080}}};
    for(const auto& t:tables){const auto actual=geometry_output_quad(ctx,t.at);
        if(memcmp(actual._u32,t.word,16))return reject("half gather tables");}
    uint64_t input_begin=SPU_LS_SIZE,input_end=0,output_begin=SPU_LS_SIZE,output_end=0;
    const unsigned bases[]={118,117,116,124};
    for(unsigned row=0;row<4;++row){
        const uint32_t base=ctx.gpr[bases[row]]._u32[0],scratch=ctx.gpr[82]._u32[row];
        if((base&15) || geometry_job_be32(ctx.ls+stack+0x70-row*16)!=uint64_t(base)+16 ||
            ctx.gpr[83]._u32[row]!=p.input_step || ctx.gpr[81]._u32[row]!=p.output_step ||
            ctx.gpr[80]._u32[row]!=p.limit || !geometry_output_range(scratch,16))return reject("input pointers/steps/scratch");
        output_begin=std::min(output_begin,uint64_t(scratch));output_end=std::max(output_end,uint64_t(scratch)+16);
        if(scratch!=stack+0x20 && scratch<stack+0x80 && stack<scratch+16)return reject("scratch/stack metadata alias");
    }
    for(unsigned column=0;column<p.columns;++column){
        GeometryHalfColumn c;c.offset=column*stride+attribute;
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
        p.column.push_back(c);
    }
    if(output_begin<0xe680 || output_end>SPU_LS_SIZE ||
        (input_begin<output_end && output_begin<input_end))return reject("output/source bounds or alias");
    output=p;return true;
}
inline v8 geometry_half_pair(const uint8_t* memory,unsigned first,unsigned second)
{
    v8 input=_mm256_set_m128i(_mm_loadu_si128(reinterpret_cast<const __m128i*>(memory+second)),
                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(memory+first)));
    const __m128i expand=_mm_setr_epi8(-128,-128,1,0,-128,-128,3,2,-128,-128,5,4,-128,-128,7,6);
    input=_mm256_shuffle_epi8(input,_mm256_broadcastsi128_si256(expand));
    const unsigned a=memory[first&~15u]*0x101,b=memory[second&~15u]*0x101;
    input=_mm256_or_si256(input,_mm256_setr_epi32(a,a,a,a,b,b,b,b));
    return _mm256_add_epi32(_mm256_andnot_si256(v8_set(0x70000000),_mm256_srai_epi32(input,3)),
        _mm256_and_si256(_mm256_cmpgt_epi32(_mm256_and_si256(input,v8_set(0x7fffffff)),v8_set(0)),v8_set(0x38000000)));
}

// Only the last group materializes the pipeline's volatile state. Count
// updates are deferred until the group completes; no data op reads them.
inline void geometry_half_setup_abi(spu_context& ctx,const GeometryHalfStream& p,unsigned group)
{
    auto& r=ctx.gpr;
    r[27] = spu_mpya(r[44], r[5], r[6]);
    r[27] = spu_ai(r[27],group*p.input_step);
    r[35] = spu_andi(r[27], 15);
    r[30] = spu_ls_read128(&ctx, r[14]._u32[0] + r[27]._u32[0]);
    r[31] = spu_ls_read128(&ctx, r[13]._u32[0] + r[27]._u32[0]);
    r[34] = spu_shufb(r[35], r[27], r[22]);
    r[28] = spu_ls_read128(&ctx, r[12]._u32[0] + r[27]._u32[0]);
    r[29] = spu_ls_read128(&ctx, r[11]._u32[0] + r[27]._u32[0]);
    r[25] = spu_ls_read128(&ctx, r[10]._u32[0] + r[27]._u32[0]);
    r[33] = spu_a(r[21], r[34]);
    r[26] = spu_ls_read128(&ctx, r[9]._u32[0] + r[27]._u32[0]);
    r[32] = spu_shli(r[44], 4);
    r[78] = spu_ls_read128(&ctx, r[8]._u32[0] + r[27]._u32[0]);
    r[43] = spu_shufb(r[33], r[33], r[23]);
    r[79] = spu_ls_read128(&ctx, r[7]._u32[0] + r[27]._u32[0]);
    r[24] = spu_shufb(r[32], r[32], r[2]);
    r[77] = spu_shufb(r[31], r[30], r[43]);
    r[76] = spu_shufb(r[29], r[28], r[43]);
    r[42] = spu_a(r[27], r[20]);
    r[75] = spu_shufb(r[26], r[25], r[43]);
    r[41] = spu_a(r[24], r[4]);
    r[41] = spu_ai(r[41],group*p.output_step);
    r[74] = spu_shufb(r[79], r[78], r[43]);
    r[66] = spu_andc(r[77], r[45]);
    r[67] = spu_andc(r[76], r[45]);
    r[68] = spu_andc(r[75], r[45]);
    r[69] = spu_andc(r[74], r[45]);
    r[73] = spu_rotmai(r[77], -3);
    r[72] = spu_rotmai(r[76], -3);
    r[71] = spu_rotmai(r[75], -3);
    r[70] = spu_rotmai(r[74], -3);
    r[40] = spu_andc(r[73], r[15]);
    r[39] = spu_andc(r[72], r[15]);
    r[38] = spu_andc(r[71], r[15]);
    r[37] = spu_andc(r[70], r[15]);
}
inline void geometry_half_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[36] = spu_cgt(r[41], r[17]);
    r[35] = spu_ls_read128(&ctx, r[7]._u32[0] + r[42]._u32[0]);
    r[63] = spu_cgti(r[69], 0);
    r[34] = spu_ls_read128(&ctx, r[8]._u32[0] + r[42]._u32[0]);
    r[33] = spu_selb(r[41], r[18], r[36]);
    r[32] = spu_ls_read128(&ctx, r[9]._u32[0] + r[42]._u32[0]);
    r[59] = spu_cgti(r[68], 0);
    r[31] = spu_ls_read128(&ctx, r[10]._u32[0] + r[42]._u32[0]);
    r[47] = spu_cgti(r[67], 0);
    r[30] = spu_ls_read128(&ctx, r[11]._u32[0] + r[42]._u32[0]);
    r[29] = spu_cgti(r[66], 0);
    r[28] = spu_ls_read128(&ctx, r[12]._u32[0] + r[42]._u32[0]);
    r[27] = spu_and(r[63], r[16]);
    r[26] = spu_ls_read128(&ctx, r[13]._u32[0] + r[42]._u32[0]);
    r[25] = spu_and(r[59], r[16]);
    r[46] = spu_ls_read128(&ctx, r[14]._u32[0] + r[42]._u32[0]);
    r[24] = spu_and(r[47], r[16]);
    r[48] = spu_shufb(r[35], r[34], r[43]);
    r[49] = spu_and(r[29], r[16]);
    r[50] = spu_shufb(r[32], r[31], r[43]);
    r[52] = spu_a(r[37], r[27]);
    r[51] = spu_rotqbyi(r[33], 4);
    r[53] = spu_a(r[38], r[25]);
    r[54] = spu_shufb(r[30], r[28], r[43]);
    r[58] = spu_rotmai(r[48], -3);
    r[57] = spu_rotqbyi(r[33], 8);
    r[55] = spu_rotmai(r[50], -3);
    r[56] = spu_shufb(r[26], r[46], r[43]);
    r[62] = spu_a(r[39], r[24]);
    r[61] = spu_rotqbyi(r[33], 12);
    r[60] = spu_rotmai(r[54], -3);
    spu_ls_write128(&ctx, r[51]._u32[0] + 0x0, r[53]);
    r[65] = spu_a(r[40], r[49]);
    spu_ls_write128(&ctx, r[57]._u32[0] + 0x0, r[62]);
    r[64] = spu_rotmai(r[56], -3);
    spu_ls_write128(&ctx, r[33]._u32[0] + 0x0, r[52]);
    r[37] = spu_andc(r[58], r[15]);
    spu_ls_write128(&ctx, r[61]._u32[0] + 0x0, r[65]);
    r[38] = spu_andc(r[55], r[15]);
    r[39] = spu_andc(r[60], r[15]);
    r[40] = spu_andc(r[64], r[15]);
    r[69] = spu_andc(r[48], r[45]);
    r[68] = spu_andc(r[50], r[45]);
    r[67] = spu_andc(r[54], r[45]);
    r[66] = spu_andc(r[56], r[45]);
    r[41] = spu_a(r[41], r[19]);
    r[42] = spu_a(r[42], r[20]);
}
inline void decode_geometry_half_stream(spu_context& ctx,const GeometryHalfStream& p)
{
    auto& r=ctx.gpr;
    r[3]=r[107];r[4]=r[41];r[6]=r[42];r[5]=r[106];
    r[7]=r[118];r[8]=geometry_output_quad(ctx,r[1]._u32[0]+0x70);
    r[9]=r[117];r[10]=geometry_output_quad(ctx,r[1]._u32[0]+0x60);
    r[11]=r[116];r[12]=geometry_output_quad(ctx,r[1]._u32[0]+0x50);
    r[13]=r[124];r[14]=geometry_output_quad(ctx,r[1]._u32[0]+0x40);
    r[17]=r[80];r[15]=spu_ilhu(0x7000);r[18]=r[82];r[16]=spu_ilhu(0x3800);
    r[19]=r[81];r[20]=r[83];r[0]=spu_link(0x83c8);
    r[45]=spu_ilhu(0x8000);r[2]=geometry_output_quad(ctx,0xd5f0);r[44]=spu_il(0);
    r[21]=geometry_output_quad(ctx,0xd5e0);r[22]=geometry_output_quad(ctx,0xd5d0);r[23]=geometry_output_quad(ctx,0xd600);
    const unsigned order[]={1,2,0,3};
    for(unsigned column=0;column<p.columns;++column){
        const auto& c=p.column[column];const unsigned prefix=c.groups-1;
        for(unsigned group=0;group<prefix;++group){
            const unsigned index=c.offset+group*p.input_step;u128 values[4];
            for(unsigned pair=0;pair<2;++pair){
                const v8 decoded=geometry_half_pair(ctx.ls,r[7+pair*4]._u32[0]+index,r[9+pair*4]._u32[0]+index);
                const __m128i a=_mm256_castsi256_si128(decoded),b=_mm256_extracti128_si256(decoded,1);
                memcpy(&values[pair*2],&a,16);memcpy(&values[pair*2+1],&b,16);
            }
            for(unsigned row:order){
                uint32_t at=r[4]._u32[row]+column*16+group*p.output_step;
                if(at>p.limit)at=r[18]._u32[row];spu_ls_write128(&ctx,at,values[row]);
            }
        }
        // Setup-only r70..r79 describe group zero, not the final group.
        u128 initial[10];
        if(prefix && column+1==p.columns){geometry_half_setup_abi(ctx,p,0);
            for(unsigned i=0;i<10;++i)initial[i]=r[70+i];}
        geometry_half_setup_abi(ctx,p,prefix);geometry_half_final_abi(ctx);
        if(prefix && column+1==p.columns)for(unsigned i=0;i<10;++i)r[70+i]=initial[i];
        r[3]=spu_ai(r[3],-1);r[44]=spu_ai(r[44],1);
    }
}
} // namespace buzz
