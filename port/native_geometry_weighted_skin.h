/* Complete weighted position/matrix kernel, B880/B934/B940. AVX2 handles
 * the prefix; the last pair materializes every original pipeline register
 * directly. No original setup, data iteration or final pair is called. */
#pragma once
#include "native_geometry_output.h"
#include "native_geometry_job.h"
#include "native_stagef_avx2.h"

namespace buzz {
struct GeometryWeightedSkin {
    uint32_t count=0,position=0,weights=0,destination=0,rows[3]{};
    bool palette_alias=false;
};
inline bool read_geometry_weighted_skin(const spu_context& ctx,GeometryWeightedSkin& output,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(ctx.ls!=ctx.ls_store)return reject("foreign LS");
    GeometryWeightedSkin p;p.count=ctx.gpr[84]._u32[0];
    if(!p.count || (p.count&1) || p.count>4096)return reject("positive even pair count");
    const struct Constant {unsigned reg;uint32_t word[4];} constants[]={
        {38,{0x00010203,0x00010203,0x00010203,0x00010203}},
        {39,{0x08090a0b,0x00010203,0x04050607,0x0c0d0e0f}},
        {43,{0x04050607,0x08090a0b,0x00010203,0x0c0d0e0f}},
        {51,{0xffffffff,0xffffffff,0xffffffff,0}}, {55,{0x3f800000,0x3f800000,0x3f800000,0x3f800000}},
        {77,{0x1c1d1e1f,0x04050607,0x08090a0b,0x80808080}},
        {81,{0x80808080,0x0c0d0e0f,0x1c1d1e1f,0x80808080}},
        {87,{0x0c0d0e0f,0x0c0d0e0f,0x0c0d0e0f,0x0c0d0e0f}},
        {88,{0x08090a0b,0x08090a0b,0x08090a0b,0x08090a0b}},
        {89,{0x04050607,0x04050607,0x04050607,0x04050607}},
        {90,{0x3b808080,0x3b808080,0x3b808080,0x3b808080}},
        {94,{0x80808008,0x8080800a,0x8080800c,0x8080800e}},
        {95,{0x80808000,0x80808002,0x80808004,0x80808006}}
    };
    for(const auto& c:constants)if(memcmp(ctx.gpr[c.reg]._u32,c.word,16))return reject("matrix/gather/scale registers");
    const unsigned registers[]={80,85,96,54,53,52};uint32_t base[6];
    for(unsigned i=0;i<6;++i){
        const uint32_t raw=ctx.gpr[registers[i]]._u32[0];
        if(raw>=SPU_LS_SIZE || (raw&~15u)<0xe680)return reject("data base bounds");
        base[i]=raw&~15u;
    }
    p.position=base[0];p.weights=base[1];p.destination=base[2];
    if(uint64_t(p.position)+(p.count+2ull)*16>SPU_LS_SIZE ||
       uint64_t(p.weights)+(p.count+2ull)*8>SPU_LS_SIZE ||
       uint64_t(p.destination)+p.count*48ull>SPU_LS_SIZE)return reject("stream/lookahead bounds");
    bool used[256]{};
    // All four indices are read even for zero weights. Lookahead coefficients
    // survive in registers but do not cause another palette read.
    for(unsigned v=0;v<p.count;++v)for(unsigned j=0;j<4;++j)
        used[ctx.ls[p.weights+v*8+j*2+1]]=true;
    if(geometry_output_overlap(p.position,p.count*16,p.weights,(p.count+2)*8) ||
       geometry_output_overlap(p.destination,p.count*48,p.weights,(p.count+2)*8) ||
       geometry_output_overlap(p.destination,p.count*48,p.position,(p.count+2)*16))return reject("written stream/input alias");
    for(unsigned row=0;row<3;++row){
        p.rows[row]=base[row+3];
        // Indexed palette quads can alias earlier output: each pair reads
        // all rows before writing, and the next pair sees those writes.
        for(unsigned index=0;index<256;++index)if(used[index]){
            const uint64_t at=uint64_t(p.rows[row])+index*48;
            if(at+16>SPU_LS_SIZE)return reject("palette read bounds");
            p.palette_alias|=geometry_output_overlap(p.position,p.count*16,uint32_t(at),16) ||
                geometry_output_overlap(p.destination,p.count*48,uint32_t(at),16);
        }
    }
    output=p;return true;
}

inline void weighted_skin_prefix(spu_context& ctx,const GeometryWeightedSkin& p)
{
    const v8 xyz=_mm256_setr_epi32(-1,-1,-1,0,-1,-1,-1,0);
    for(unsigned vertex=0;vertex+2<p.count;vertex+=2){
        const uint8_t* skin=ctx.ls+p.weights+vertex*8;
        const v8 quantized=_mm256_setr_epi32(skin[0],skin[2],skin[4],skin[6],skin[8],skin[10],skin[12],skin[14]);
        const v8 coefficients=v8_fm(_mm256_castps_si256(_mm256_cvtepi32_ps(quantized)),v8_bcast(ctx.gpr[90]));
        v8 factors[4],raw[3],matrix[3];
        for(unsigned j=0;j<4;++j)factors[j]=_mm256_permutevar8x32_epi32(coefficients,_mm256_setr_epi32(j,j,j,j,j+4,j+4,j+4,j+4));
        for(unsigned row=0;row<3;++row){
            raw[row]=v8_fm(factors[0],v8_ld2(ctx.ls,p.rows[row]+skin[1]*48,p.rows[row]+skin[9]*48));
            for(unsigned j=1;j<4;++j)raw[row]=v8_fma(factors[j],v8_ld2(ctx.ls,p.rows[row]+skin[j*2+1]*48,p.rows[row]+skin[j*2+9]*48),raw[row]);
            matrix[row]=_mm256_and_si256(raw[row],xyz);
            if(!row)matrix[row]=_mm256_or_si256(matrix[row],_mm256_setr_epi32(0,0,0,0x3f800000,0,0,0,0x3f800000));
        }
        // The original gathers all six rows before its first store. Loading
        // and storing one row at a time corrupts aliased palette inputs.
        for(unsigned row=0;row<3;++row)
            v8_st2(ctx.ls,p.destination+vertex*48+row*16,p.destination+(vertex+1)*48+row*16,matrix[row]);
        v8 translation=_mm256_blend_epi32(_mm256_shuffle_epi32(raw[0],255),_mm256_shuffle_epi32(raw[1],255),0x22);
        translation=_mm256_and_si256(_mm256_blend_epi32(translation,_mm256_shuffle_epi32(raw[2],255),0x44),xyz);
        const v8 point=v8_ld2(ctx.ls,p.position+vertex*16,p.position+(vertex+1)*16);
        v8 result=v8_fma(point,matrix[0],translation);
        result=v8_fma(_mm256_shuffle_epi32(point,_MM_SHUFFLE(3,0,2,1)),matrix[1],result);
        result=v8_fma(_mm256_shuffle_epi32(point,_MM_SHUFFLE(3,1,0,2)),matrix[2],result);
        v8_st2(ctx.ls,p.position+vertex*16,p.position+(vertex+1)*16,result);
    }
}

inline void transform_geometry_weighted_skin(spu_context& ctx,const GeometryWeightedSkin& p,
                                            buzz_geometry_job_fn directions,buzz_geometry_job_fn normals)
{
    auto& r=ctx.gpr;
    const u128 input_position=r[80],input_weights=r[85],input_destination=r[96],input_count=r[84];
    weighted_skin_prefix(ctx,p);
    const unsigned last=p.count-2;
    const u128 position=spu_ai(input_position,int(last*16));
    const u128 weights=spu_ai(input_weights,int(last*8));
    const u128 destination=spu_ai(input_destination,int(last*48));
    const auto get=[&](uint32_t at){return spu_ls_read128(&ctx,at);};
    const u128 skin=get(weights._u32[0]);
    const u128 point[2]={get(position._u32[0]),get(position._u32[0]+16)};
    const u128 coefficient[2]={spu_fm(spu_cuflt(spu_shufb(skin,r[38],r[95]),155),r[90]),
                               spu_fm(spu_cuflt(spu_shufb(skin,r[38],r[94]),155),r[90])};
    const unsigned selectors[]={38,89,88,87};
    u128 stage[2][3][4],raw[2][3],matrix[2][3],translation[2],first[2],second[2],result[2],partial[2];
    for(unsigned vertex=0;vertex<2;++vertex){
        for(unsigned row=0;row<3;++row){
            for(unsigned j=0;j<4;++j){
                const auto factor=spu_shufb(coefficient[vertex],r[38],r[selectors[j]]);
                const unsigned index=ctx.ls[p.weights+(last+vertex)*8+j*2+1];
                const auto value=get(p.rows[row]+index*48);
                stage[vertex][row][j]=j?spu_fma(factor,value,stage[vertex][row][j-1]):spu_fm(factor,value);
            }
            raw[vertex][row]=stage[vertex][row][3];
            matrix[vertex][row]=spu_selb(row?r[51]:r[55],raw[vertex][row],r[51]);
        }
        partial[vertex]=spu_shufb(raw[vertex][1],raw[vertex][2],r[81]);
        translation[vertex]=spu_shufb(partial[vertex],raw[vertex][0],r[77]);
        first[vertex]=spu_fma(point[vertex],matrix[vertex][0],translation[vertex]);
        second[vertex]=spu_fma(spu_shufb(point[vertex],r[38],r[43]),matrix[vertex][1],first[vertex]);
        result[vertex]=spu_fma(spu_shufb(point[vertex],r[38],r[39]),matrix[vertex][2],second[vertex]);
    }
    const auto next_skin=get(weights._u32[0]+16);
    const auto next0=spu_cuflt(spu_shufb(next_skin,r[38],r[95]),155);
    const auto next1=spu_cuflt(spu_shufb(next_skin,r[38],r[94]),155);
    const auto next_factor0=spu_fm(next0,r[90]),next_factor1=spu_fm(next1,r[90]);
    const auto indices=spu_andhi(next_skin,255);
    // These values are the original final pair's live and scratch ABI, not
    // just its output matrices. Upper pointer/count lanes remain arithmetic.
    r[2]=matrix[0][2];r[3]=first[1];r[5]=matrix[1][1];
    r[10]=spu_shufb(next_skin,r[38],r[94]);r[14]=matrix[1][0];r[15]=first[0];r[16]=matrix[0][0];
    r[17]=raw[0][1];r[18]=partial[1];r[19]=raw[1][1];r[20]=stage[0][0][2];r[21]=next_factor0;
    r[22]=raw[0][0];r[23]=stage[0][2][2];r[24]=stage[1][2][2];
    r[25]=spu_ai(position,64);r[26]=raw[1][0];r[27]=partial[0];r[28]=next1;r[30]=second[0];
    const unsigned first_regs[]={35,36,37,42},second_regs[]={32,33,34,40};
    for(unsigned j=0;j<4;++j){
        r[first_regs[j]]=spu_shufb(next_factor0,r[38],r[selectors[j]]);
        r[second_regs[j]]=spu_shufb(next_factor1,r[38],r[selectors[j]]);
    }
    r[41]=spu_ai(destination,96);r[44]=spu_ai(position,32);r[46]=get(position._u32[0]+32);
    r[47]=r[41];r[48]=spu_ai(input_count,-int(p.count));r[49]=r[25];r[50]=spu_ai(weights,32);
    r[56]=spu_shufb(get(position._u32[0]+48),r[38],r[39]);
    r[57]=spu_shufb(get(position._u32[0]+48),r[38],r[43]);
    r[58]=spu_shufb(r[46],r[38],r[39]);r[59]=spu_shufb(r[46],r[38],r[43]);
    r[60]=spu_a(input_weights,spu_shli(input_count,3));r[61]=result[1];r[62]=result[0];r[63]=next0;
    r[64]=spu_shli(indices,5);r[65]=spu_shli(indices,4);r[66]=matrix[1][2];r[68]=second[1];
    r[6]=spu_a(r[64],r[65]);r[11]=spu_shlqbyi(r[6],4);r[8]=spu_shlqbyi(r[6],8);r[7]=spu_shlqbyi(r[6],12);
    r[13]=spu_roti(r[6],16);r[12]=spu_roti(r[11],16);r[4]=spu_roti(r[8],16);r[9]=spu_roti(r[7],16);
    r[80]=r[44];r[85]=r[60];r[45]=spu_xsbh(r[102]);
    for(unsigned vertex=0;vertex<2;++vertex){
        for(unsigned row=0;row<3;++row)spu_ls_write128(&ctx,destination._u32[0]+vertex*48+row*16,matrix[vertex][row]);
        spu_ls_write128(&ctx,position._u32[0]+vertex*16,result[vertex]);
    }
    if(r[45]._u16[0]){ctx.pc=0xb4c0;g_spu_trampoline_fn=directions;}
    else{ctx.pc=0xbb58;g_spu_trampoline_fn=normals;}
}
} // namespace buzz
