/* Complete storage-9 unit-vector decoder (9114 through 92A0).
 * Host columns/batches decode all data. The final two iterations per column
 * materializes the SPU pipeline ABI, including its two-group lookahead. */
#pragma once
#include "native_geometry_output.h"
#include "native_stagef_avx2.h"
#include <vector>

namespace buzz {
struct GeometryUnitColumn { uint32_t offset=0,groups=0; };
struct GeometryUnitStream {
    uint32_t columns=0,input_step=0,output_step=0,limit=0;
    GeometryUnitColumn column[16]{}; // columns<=16 is an admission bound; no per-call heap
};
inline u128 geometry_unit_row(unsigned selector)
{
    static const uint32_t order[3][3]={{0x00010203,0x10111213,0x14151617},
        {0x10111213,0x00010203,0x14151617},{0x10111213,0x14151617,0x00010203}};
    u128 row{};
    for(unsigned i=0;i<3;++i)row._u32[i]=order[selector>>2][i];
    if(!(selector&1)){row._u32[0]|=0x80000000;row._u32[2]|=0x80000000;}
    row._u32[3]=selector&2?0x3f800000:0xbf800000;return row;
}
inline bool read_geometry_unit_vectors(const spu_context& ctx,GeometryUnitStream& output,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(ctx.ls!=ctx.ls_store)return reject("foreign LS");
    GeometryUnitStream p;
    if(!ctx.gpr[121]._u32[0]){output=p;return true;}
    p.columns=ctx.gpr[107]._u32[0];p.input_step=ctx.gpr[83]._u32[0];
    p.output_step=ctx.gpr[81]._u32[0];p.limit=ctx.gpr[80]._u32[0];
    const uint32_t stride=ctx.gpr[106]._u32[0],attribute=ctx.gpr[42]._u32[0],stack=ctx.gpr[1]._u32[0];
    if(!p.columns || p.columns>16 || !stride || stride>255 || attribute>255 ||
        !p.input_step || (p.input_step&15) || !p.output_step || (p.output_step&15) ||
        p.limit>=SPU_LS_SIZE || !geometry_output_range(stack,0xd0))return reject("shape/step/stack");
    struct Constant { unsigned reg; uint32_t word[4]; };
    static const Constant constants[]={
        {87,{0xfffffffc,0xfffffffa,0xfffffffc,0}}, {85,{0,0x3f800000,0x3f800000,0}},
        {86,{0xf0,0x3ff,0x3ff,0}}, {89,{0,0,0,0x0c0d0e0f}},
        {88,{0x7fffffff,0x7fffffff,0x7fffffff,0}}, {97,{0x80000000,0x80000000,0x80000000,0xffffffff}},
        {109,{0x04050607,0x08090a0b,0x14151617,0x18191a1b}},
        {112,{0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f}},
        {108,{0x03030303,0x03030303,0x03030303,0x03030303}},
        {102,{0x46352e7c,0x46352e7c,0x46352e7c,0x46352e7c}},
        {101,{0xc6353150,0xc6353150,0xc6353150,0xc6353150}},
        {110,{0x3f800000,0x3f800000,0x3f800000,0x3f800000}},
        {96,{0x08080808,0x08080808,0x08080808,0}}};
    for(const auto& c:constants)if(memcmp(ctx.gpr[c.reg]._u32,c.word,16))return reject("math/gather constant registers");
    const u128 gather=geometry_output_quad(ctx,0xd8b0);
    const uint32_t expected[]={0x80800280,0x80800001,0x80800102,0x80808080};
    if(memcmp(gather._u32,expected,16))return reject("gather table");
    const uint32_t table=geometry_job_be32(ctx.ls+stack+0xc0)&0x3fff0;
    if(table<0xddf0 || table+256>0xe680)return reject("selector table address");
    for(unsigned i=0;i<12;++i){const auto row=geometry_unit_row(i),actual=geometry_output_quad(ctx,table+i*16);
        if(memcmp(&row,&actual,16))return reject("selector row data");}
    uint64_t input_begin=SPU_LS_SIZE,input_end=0,output_begin=SPU_LS_SIZE,output_end=0;
    const unsigned bases[]={118,117,116,124};
    for(unsigned row=0;row<4;++row){
        const uint32_t base=ctx.gpr[bases[row]]._u32[0],scratch=ctx.gpr[82]._u32[row];
        if((base&15) || geometry_job_be32(ctx.ls+stack+0x70-row*16)!=uint64_t(base)+16 ||
            ctx.gpr[83]._u32[row]!=p.input_step || ctx.gpr[81]._u32[row]!=p.output_step ||
            ctx.gpr[80]._u32[row]!=p.limit || !geometry_output_range(scratch,16))return reject("input pointers/steps/scratch");
        output_begin=std::min(output_begin,uint64_t(scratch)&~uint64_t{15});
        output_end=std::max(output_end,(uint64_t(scratch)&~uint64_t{15})+16);
        const uint32_t physical=scratch&~15u;
        if(physical!=stack+0x20 && physical<stack+0xd0 && stack<physical+16)return reject("scratch/stack metadata alias");
    }
    for(unsigned column=0;column<p.columns;++column){
        GeometryUnitColumn c;c.offset=column*stride+attribute;
        const uint64_t first=uint64_t(ctx.gpr[41]._u32[0])+column*16;
        if(first>INT32_MAX)return reject("preferred cursor");
        c.groups=first>p.limit?1:(p.limit-uint32_t(first))/p.output_step+2;
        if(c.groups>4096)return reject("group count");
        for(unsigned row=0;row<4;++row){
            const uint64_t begin=uint64_t(ctx.gpr[bases[row]]._u32[0])+c.offset;
            const uint64_t end=begin+uint64_t(c.groups+1)*p.input_step+32;
            if(begin<0xe680 || end>SPU_LS_SIZE)return reject("source lookahead bounds");
            input_begin=std::min(input_begin,begin&~uint64_t{15});input_end=std::max(input_end,end);
            // Host batches require actual unit-vector selectors. The final
            // two iterations and speculative reads retain all sixteen table
            // outcomes through exact native ABI math, including padded bytes.
            for(unsigned group=0;group+2<c.groups;++group)
                if((ctx.ls[begin+uint64_t(group)*p.input_step+2]&15)>=12)return reject("reserved selector");
            const uint64_t destination=uint64_t(ctx.gpr[41]._u32[row])+column*16;
            const uint64_t last=destination+uint64_t(c.groups-1)*p.output_step;
            if(last>INT32_MAX)return reject("output cursor bounds");
            if(destination<=p.limit){output_begin=std::min(output_begin,destination&~uint64_t{15});
                output_end=std::max(output_end,(std::min(last,uint64_t(p.limit))&~uint64_t{15})+16);
                if((destination&~uint64_t{15})<stack+0xd0 && stack<(std::min(last,uint64_t(p.limit))&~uint64_t{15})+16)
                    return reject("destination/stack metadata alias");}
        }
        p.column[column]=c;
    }
    if(output_begin<0xe680 || output_end>SPU_LS_SIZE ||
        (input_begin<output_end && output_begin<input_end))return reject("output/source/stack alias");
    output=p;return true;
}

inline void geometry_unit_values(const spu_context& ctx,unsigned offset,unsigned step,unsigned groups,u128* values)
{
    const unsigned bases[]={36,35,34,33};uint32_t packed[8]{};
    for(unsigned i=0;i<groups*4;++i){const auto* at=ctx.ls+ctx.gpr[bases[i%4]]._u32[0]+offset+(i/4)*step;
        packed[i]=(uint32_t(at[0])<<16)|(uint32_t(at[1])<<8)|at[2];}
    const v8 bits=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(packed)),one=v8_set(0x3f800000);
    v8 u=_mm256_or_si256(one,_mm256_and_si256(_mm256_srli_epi32(bits,14),v8_set(1023)));
    v8 v=_mm256_or_si256(one,_mm256_and_si256(_mm256_srli_epi32(bits,4),v8_set(1023)));
    u=v8_fma(u,v8_set(0x46352e7c),v8_set(0xc6353150));v=v8_fma(v,v8_set(0x46352e7c),v8_set(0xc6353150));
    const v8 uu=v8_fm(u,u),vv=v8_fm(v,v);
    const v8 sum=v8_finish(_mm256_add_ps(_mm256_castsi256_ps(uu),_mm256_castsi256_ps(vv)),uu,vv,uu,0);
    const v8 t=v8_fs(one,sum),dominant=v8_fm(t,v8_fi(t,v8_frsqest(t)));
    uint32_t us[8],vs[8],ds[8];_mm256_storeu_si256(reinterpret_cast<__m256i*>(us),u);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(vs),v);_mm256_storeu_si256(reinterpret_cast<__m256i*>(ds),dominant);
    for(unsigned i=0;i<groups*4;++i){const unsigned selector=packed[i]&15,axis=selector>>2;
        const uint32_t d=(ds[i]&0x7fffffff)|((selector^1)<<31);
        values[i]._u32[0]=axis==0?d:us[i];values[i]._u32[1]=axis==1?d:(axis==0?us[i]:vs[i]);
        values[i]._u32[2]=axis==2?d:vs[i];values[i]._u32[3]=selector&2?0x3f800000:0xbf800000;}
}

// Materialize the surviving software-pipeline ABI at a selected group.
// These helpers never schedule an original block or execute a prefix loop.
inline void geometry_unit_setup_abi(spu_context& ctx,const GeometryUnitStream& p,unsigned group)
{
    auto& r=ctx.gpr;
    r[47] = spu_ori(r[43], 0);
    r[46] = spu_shufb(r[44], r[44], r[45]);
    r[6] = spu_ls_read128(&ctx, 0xD8B0);
    r[3] = spu_mpya(r[47], r[106], r[42]);
    r[3] = spu_ai(r[3],group*p.input_step);
    r[31] = spu_a(r[46], r[41]);
    r[31] = spu_ai(r[31],group*p.output_step);
    r[29] = spu_ls_read128(&ctx, r[37]._u32[0] + r[3]._u32[0]);
    r[12] = spu_andi(r[3], 15);
    r[14] = spu_ls_read128(&ctx, r[40]._u32[0] + r[3]._u32[0]);
    r[4] = spu_a(r[3], r[83]);
    r[30] = spu_ls_read128(&ctx, r[34]._u32[0] + r[3]._u32[0]);
    r[5] = spu_shufb(r[12], r[3], r[108]);
    r[32] = spu_a(r[4], r[83]);
    r[17] = spu_ls_read128(&ctx, r[38]._u32[0] + r[3]._u32[0]);
    r[28] = spu_ls_read128(&ctx, r[33]._u32[0] + r[3]._u32[0]);
    r[7] = spu_ls_read128(&ctx, r[36]._u32[0] + r[3]._u32[0]);
    r[23] = spu_a(r[112], r[5]);
    r[18] = spu_ls_read128(&ctx, r[35]._u32[0] + r[3]._u32[0]);
    r[19] = spu_ls_read128(&ctx, r[39]._u32[0] + r[3]._u32[0]);
    r[23] = spu_shufb(r[23], r[23], r[6]);
    r[72] = spu_ls_read128(&ctx, r[37]._u32[0] + r[4]._u32[0]);
    r[74] = spu_ls_read128(&ctx, r[34]._u32[0] + r[4]._u32[0]);
    r[75] = spu_ls_read128(&ctx, r[38]._u32[0] + r[4]._u32[0]);
    r[10] = spu_shufb(r[7], r[14], r[23]);
    r[76] = spu_shufb(r[30], r[17], r[23]);
    r[73] = spu_shufb(r[28], r[29], r[23]);
    r[9] = spu_shufb(r[18], r[19], r[23]);
    r[70] = spu_rotm(r[10], r[87]);
    r[71] = spu_ls_read128(&ctx, r[33]._u32[0] + r[4]._u32[0]);
    r[67] = spu_rotm(r[76], r[87]);
    r[27] = spu_ls_read128(&ctx, r[40]._u32[0] + r[4]._u32[0]);
    r[65] = spu_rotm(r[73], r[87]);
    r[79] = spu_ls_read128(&ctx, r[36]._u32[0] + r[4]._u32[0]);
    r[57] = spu_rotm(r[9], r[87]);
    r[77] = spu_ls_read128(&ctx, r[35]._u32[0] + r[4]._u32[0]);
    r[24] = spu_selb(r[85], r[70], r[86]);
    r[78] = spu_ls_read128(&ctx, r[39]._u32[0] + r[4]._u32[0]);
    r[61] = spu_selb(r[85], r[67], r[86]);
    r[60] = spu_shufb(r[74], r[75], r[23]);
    r[8] = spu_selb(r[85], r[65], r[86]);
    r[59] = spu_shufb(r[71], r[72], r[23]);
    r[26] = spu_selb(r[85], r[57], r[86]);
    r[56] = spu_shufb(r[61], r[8], r[109]);
    r[54] = spu_rotm(r[60], r[87]);
    r[58] = spu_shufb(r[24], r[26], r[109]);
    r[52] = spu_rotm(r[59], r[87]);
    r[63] = spu_shufb(r[79], r[27], r[23]);
    r[62] = spu_shufb(r[77], r[78], r[23]);
    r[53] = spu_fma(r[56], r[102], r[101]);
    r[27] = spu_ls_read128(&ctx, r[25]._u32[0] + r[8]._u32[0]);
    r[55] = spu_fma(r[58], r[102], r[101]);
    r[30] = spu_ls_read128(&ctx, r[25]._u32[0] + r[24]._u32[0]);
    r[16] = spu_rotm(r[63], r[87]);
    r[29] = spu_ls_read128(&ctx, r[25]._u32[0] + r[26]._u32[0]);
    r[15] = spu_rotm(r[62], r[87]);
    r[28] = spu_ls_read128(&ctx, r[25]._u32[0] + r[61]._u32[0]);
    r[10] = spu_selb(r[85], r[54], r[86]);
    r[8] = spu_selb(r[85], r[52], r[86]);
    r[20] = spu_fm(r[53], r[53]);
    r[26] = spu_shlqbyi(r[53], 0);
    r[50] = spu_fm(r[55], r[55]);
    r[51] = spu_shufb(r[10], r[8], r[109]);
    r[17] = spu_selb(r[85], r[15], r[86]);
    r[24] = spu_shlqbyi(r[55], 0);
    r[16] = spu_selb(r[85], r[16], r[86]);
    r[15] = spu_selb(r[89], r[30], r[88]);
    r[21] = spu_fma(r[51], r[102], r[101]);
    r[13] = spu_shufb(r[16], r[17], r[109]);
    r[14] = spu_selb(r[89], r[28], r[88]);
    r[48] = spu_shlqbyi(r[20], 4);
    r[9] = spu_selb(r[89], r[27], r[88]);
    r[49] = spu_shlqbyi(r[50], 4);
    r[22] = spu_fma(r[13], r[102], r[101]);
    r[46] = spu_fa(r[48], r[20]);
    r[47] = spu_fa(r[49], r[50]);
    r[13] = spu_selb(r[89], r[29], r[88]);
    r[12] = spu_fs(r[110], r[46]);
    r[11] = spu_fs(r[110], r[47]);
    r[3] = spu_frsqest(r[12]);
    r[2] = spu_frsqest(r[11]);
    r[6] = spu_fi(r[12], r[3]);
    r[4] = spu_fi(r[11], r[2]);
}
inline void geometry_unit_final_abi(spu_context& ctx,bool final)
{
    auto& r=ctx.gpr;
    r[3] = spu_fm(r[11], r[4]);
    r[70] = spu_shlqbyi(r[10], 0);
    r[7] = spu_fm(r[12], r[6]);
    r[71] = spu_shlqbyi(r[32], 0);
    r[18] = spu_cgt(r[31], r[80]);
    r[69] = spu_shlqbyi(r[8], 0);
    r[19] = spu_a(r[13], r[96]);
    r[76] = spu_selb(r[31], r[82], r[18]);
    r[63] = spu_fm(r[22], r[22]);
    r[5] = spu_selb(r[3], r[30], r[97]);
    r[78] = spu_rotqbyi(r[76], 4);
    r[30] = spu_selb(r[3], r[29], r[97]);
    r[74] = spu_rotqbyi(r[76], 8);
    r[29] = spu_selb(r[7], r[28], r[97]);
    r[10] = spu_shufb(r[5], r[24], r[15]);
    r[28] = spu_selb(r[7], r[27], r[97]);
    r[79] = spu_shufb(r[30], r[24], r[19]);
    r[67] = spu_fm(r[21], r[21]);
    r[75] = spu_shufb(r[29], r[26], r[14]);
    r[9] = spu_a(r[9], r[96]);
    r[72] = spu_rotqbyi(r[76], 12);
    r[77] = spu_a(r[32], r[83]);
    spu_ls_write128(&ctx, r[76]._u32[0] + 0x0, r[10]);
    r[73] = spu_shufb(r[28], r[26], r[9]);
    r[26] = spu_a(r[31], r[81]);
    spu_ls_write128(&ctx, r[78]._u32[0] + 0x0, r[79]);
    r[32] = spu_ori(r[77], 0);
    spu_ls_write128(&ctx, r[74]._u32[0] + 0x0, r[75]);
    r[31] = spu_ori(r[26], 0);
    r[62] = spu_shlqbyi(r[63], 4);
    r[26] = spu_ori(r[21], 0);
    spu_ls_write128(&ctx, r[72]._u32[0] + 0x0, r[73]);
    r[66] = spu_shlqbyi(r[67], 4);
    r[24] = spu_ls_read128(&ctx, r[36]._u32[0] + r[71]._u32[0]);
    r[55] = spu_fa(r[62], r[63]);
    r[8] = spu_ls_read128(&ctx, r[35]._u32[0] + r[71]._u32[0]);
    r[61] = spu_ls_read128(&ctx, r[37]._u32[0] + r[71]._u32[0]);
    r[57] = spu_ls_read128(&ctx, r[40]._u32[0] + r[71]._u32[0]);
    r[68] = spu_ls_read128(&ctx, r[39]._u32[0] + r[71]._u32[0]);
    r[64] = spu_ls_read128(&ctx, r[34]._u32[0] + r[71]._u32[0]);
    r[65] = spu_ls_read128(&ctx, r[38]._u32[0] + r[71]._u32[0]);
    r[11] = spu_fs(r[110], r[55]);
    r[60] = spu_ls_read128(&ctx, r[33]._u32[0] + r[71]._u32[0]);
    r[30] = spu_ls_read128(&ctx, r[25]._u32[0] + r[16]._u32[0]);
    r[16] = spu_fa(r[66], r[67]);
    r[59] = spu_shufb(r[24], r[57], r[23]);
    r[24] = spu_ori(r[22], 0);
    r[58] = spu_shufb(r[8], r[68], r[23]);
    r[29] = spu_ls_read128(&ctx, r[25]._u32[0] + r[17]._u32[0]);
    r[56] = spu_shufb(r[64], r[65], r[23]);
    r[54] = spu_rotm(r[59], r[87]);
    r[15] = spu_shufb(r[60], r[61], r[23]);
    r[53] = spu_rotm(r[58], r[87]);
    r[50] = spu_frsqest(r[11]);
    r[12] = spu_fs(r[110], r[16]);
    r[28] = spu_ls_read128(&ctx, r[25]._u32[0] + r[70]._u32[0]);
    r[52] = spu_rotm(r[56], r[87]);
    r[27] = spu_ls_read128(&ctx, r[25]._u32[0] + r[69]._u32[0]);
    r[13] = spu_rotm(r[15], r[87]);
    r[16] = spu_selb(r[85], r[54], r[86]);
    r[17] = spu_selb(r[85], r[53], r[86]);
    r[10] = spu_selb(r[85], r[52], r[86]);
    r[8] = spu_selb(r[85], r[13], r[86]);
    r[51] = spu_frsqest(r[12]);
    r[4] = spu_fi(r[11], r[50]);
    r[49] = spu_shufb(r[16], r[17], r[109]);
    r[15] = spu_selb(r[89], r[30], r[88]);
    r[20] = spu_shufb(r[10], r[8], r[109]);
    r[13] = spu_selb(r[89], r[29], r[88]);
    r[6] = spu_fi(r[12], r[51]);
    r[14] = spu_selb(r[89], r[28], r[88]);
    r[22] = spu_fma(r[49], r[102], r[101]);
    r[21] = spu_fma(r[20], r[102], r[101]);
    r[9] = spu_selb(r[89], r[27], r[88]);
    if(final){r[43] = spu_ai(r[43], 1);r[44] = spu_ai(r[44], 16);r[17] = spu_clgt(r[107], r[43]);}
}
inline void decode_geometry_unit_vectors(spu_context& ctx,const GeometryUnitStream& p)
{
    if(!p.columns)return;
    auto& r=ctx.gpr;
    r[36]=r[118];r[40]=geometry_output_quad(ctx,r[1]._u32[0]+0x70);
    r[35]=r[117];r[39]=geometry_output_quad(ctx,r[1]._u32[0]+0x60);
    r[34]=r[116];r[38]=geometry_output_quad(ctx,r[1]._u32[0]+0x50);
    r[33]=r[124];r[37]=geometry_output_quad(ctx,r[1]._u32[0]+0x40);
    r[43]=spu_il(0);r[25]=geometry_output_quad(ctx,r[1]._u32[0]+0xc0);
    r[45]=spu_ila(0x10203);r[44]=spu_fsmbi(0);
    for(unsigned column=0;column<p.columns;++column){
        const auto& c=p.column[column];const unsigned prefix=c.groups>2?c.groups-2:0;
        for(unsigned group=0;group<prefix;group+=2){
            const unsigned groups=std::min(2u,prefix-group);u128 values[8];
            geometry_unit_values(ctx,c.offset+group*p.input_step,p.input_step,groups,values);
            for(unsigned i=0;i<groups*4;++i){
                const unsigned row=i%4;uint32_t at=r[41]._u32[row]+column*16+(group+i/4)*p.output_step;
                if(at>p.limit)at=r[82]._u32[row];
                spu_ls_write128(&ctx,at,values[i]);
            }
        }
        // Five setup-only registers belong to group zero even when the last
        // groups have different data. All other volatile state is final-group ABI.
        const unsigned protected_regs[]={2,23,46,47,48};u128 initial[5];
        if(prefix && column+1==p.columns){
            geometry_unit_setup_abi(ctx,p,0);
            for(unsigned i=0;i<5;++i)initial[i]=r[protected_regs[i]];
        }
        geometry_unit_setup_abi(ctx,p,prefix);
        for(unsigned group=prefix;group<c.groups;++group)geometry_unit_final_abi(ctx,group+1==c.groups);
        if(prefix && column+1==p.columns)
            for(unsigned i=0;i<5;++i)r[protected_regs[i]]=initial[i];
    }
}
} // namespace buzz
