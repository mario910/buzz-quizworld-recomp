/* Complete unnormalized one/two/three-stream skin direction kernels.
 * Cache the original pipeline inputs, gather every next group before any
 * store, then materialize each variant's complete final register state. */
#pragma once
#include "native_geometry_job.h"
#include "native_geometry_output.h"
#include "native_stagef_avx2.h"
namespace buzz {
struct GeometrySkinDirections {unsigned streams=0,count=0;bool aliases=false;};
inline bool read_geometry_skin_directions(const spu_context& c,unsigned streams,GeometrySkinDirections& output,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    GeometrySkinDirections p;p.streams=streams;p.count=c.gpr[84]._u32[0];
    if(streams<1 || streams>3 || !p.count || (p.count&3) || p.count>4096)return reject("stream/group count");
    auto range=[](uint64_t raw,uint64_t size){return raw<SPU_LS_SIZE && (raw&~15ull)>=0xe680 && (raw&~15ull)+size<=SPU_LS_SIZE;};
    const uint64_t first=c.gpr[33]._u32[0],primary=c.gpr[67]._u32[0],matrix=c.gpr[40]._u32[0];
    if(!range(first,128) || !range(matrix,p.count*48ull) ||
       (p.count>4 && !range(primary+64,p.count*16ull)))return reject("primary/matrix lookahead bounds");
    struct Span {uint32_t begin,size;unsigned stream;} writes[4];unsigned n=0;
    writes[n++]={uint32_t(first)&~15u,64,0};
    if(p.count>4)writes[n++]={uint32_t(primary+64)&~15u,(p.count-4)*16,0};
    for(unsigned stream=1;stream<streams;++stream){
        const uint32_t at=c.gpr[stream==1?71:93]._u32[0];
        if(!range(at,p.count*16ull))return reject("secondary stream bounds");
        writes[n++]={at&~15u,p.count*16,stream};
    }
    for(unsigned i=0;i<n;++i){
        p.aliases|=geometry_output_overlap(writes[i].begin,writes[i].size,uint32_t(matrix)&~15u,p.count*48);
        for(unsigned j=i+1;j<n;++j)p.aliases|=geometry_output_overlap(writes[i].begin,writes[i].size,writes[j].begin,writes[j].size);
    }
    output=p;return true;
}
inline void skin_direction_pair(v8 value,u128& lo,u128& hi)
{
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&lo),_mm256_castsi256_si128(value));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&hi),_mm256_extracti128_si256(value,1));
}
inline void transform_geometry_skin_directions(spu_context& c,const GeometrySkinDirections& p,buzz_geometry_job_fn tail)
{
    auto& r=c.gpr;const u128 count=r[84],first_pointer=r[33],primary=r[67],secondary=r[71],tertiary=r[93],matrix_pointer=r[40];
    const unsigned xregs[]={37,36,35,34},yregs[]={65,61,64,59},zregs[]={66,63,62,60};
    const unsigned mregs[4][3]={{58,57,56},{50,49,48},{47,46,45},{44,42,41}};
    u128 matrix[4][3],next_matrix[4][3],raw[3][4],y[3][4],z[3][4],next_raw[4],next_y[4],next_z[4];
    u128 fm[3][4],middle[3][4],result[3][4];
    for(unsigned v=0;v<4;++v){raw[0][v]=r[xregs[v]];y[0][v]=r[yregs[v]];z[0][v]=r[zregs[v]];for(unsigned row=0;row<3;++row)matrix[v][row]=r[mregs[v][row]];}
    auto get=[&](uint32_t at){return spu_ls_read128(&c,at);};
    u128 last_primary{},last_matrix{};
    for(unsigned group=0;group<p.count/4;++group){
        last_primary=group?spu_ai(primary,int(group*64)):first_pointer;
        last_matrix=spu_ai(matrix_pointer,int(group*192));
        for(unsigned stream=1;stream<p.streams;++stream){
            const u128 pointer=spu_ai(stream==1?secondary:tertiary,int(group*64));
            for(unsigned v=0;v<4;++v){raw[stream][v]=get(pointer._u32[0]+v*16);y[stream][v]=spu_shufb(raw[stream][v],r[38],r[43]);z[stream][v]=spu_shufb(raw[stream][v],r[38],r[39]);}
        }
        for(unsigned stream=0;stream<p.streams;++stream)for(unsigned v=0;v<4;v+=2){
            const v8 a=v8_fm(v8_pair(raw[stream][v],raw[stream][v+1]),v8_pair(matrix[v][0],matrix[v+1][0]));
            const v8 b=v8_fma(v8_pair(y[stream][v],y[stream][v+1]),v8_pair(matrix[v][1],matrix[v+1][1]),a);
            const v8 out=v8_fma(v8_pair(z[stream][v],z[stream][v+1]),v8_pair(matrix[v][2],matrix[v+1][2]),b);
            skin_direction_pair(a,fm[stream][v],fm[stream][v+1]);skin_direction_pair(b,middle[stream][v],middle[stream][v+1]);skin_direction_pair(out,result[stream][v],result[stream][v+1]);
        }
        // All original lookahead loads precede all stores, including aliases
        // between matrix scratch, streams, and a following group's input.
        for(unsigned v=0;v<4;++v){
            next_raw[v]=get(last_primary._u32[0]+64+v*16);
            next_y[v]=spu_shufb(next_raw[v],r[38],r[43]);next_z[v]=spu_shufb(next_raw[v],r[38],r[39]);
            for(unsigned row=0;row<3;++row)next_matrix[v][row]=get(last_matrix._u32[0]+v*48+row*16);
        }
        for(unsigned stream=0;stream<p.streams;++stream){
            const u128 pointer=stream==0?last_primary:spu_ai(stream==1?secondary:tertiary,int(group*64));
            for(unsigned v=0;v<4;++v)spu_ls_write128(&c,pointer._u32[0]+v*16,result[stream][v]);
        }
        if(group+1<p.count/4)for(unsigned v=0;v<4;++v){raw[0][v]=next_raw[v];y[0][v]=next_y[v];z[0][v]=next_z[v];for(unsigned row=0;row<3;++row)matrix[v][row]=next_matrix[v][row];}
    }
    const u128 remaining=spu_ai(count,-int(p.count)),primary_end=spu_ai(primary,int(p.count*16));
    const u128 secondary_end=spu_ai(secondary,int(p.count*16)),tertiary_end=spu_ai(tertiary,int(p.count*16));
    const u128 matrix_end=spu_ai(matrix_pointer,int(p.count*48));
    if(p.streams==1){
        r[18]=fm[0][0];r[6]=fm[0][1];r[11]=fm[0][2];r[2]=fm[0][3];
        r[25]=middle[0][0];r[27]=middle[0][1];r[26]=middle[0][2];r[30]=middle[0][3];
        r[13]=result[0][0];r[31]=result[0][1];r[28]=result[0][2];r[32]=result[0][3];
        r[8]=remaining;r[3]=matrix_end;r[68]=last_matrix;r[33]=last_primary;
    }else if(p.streams==2){
        r[18]=fm[0][3];r[13]=fm[0][2];r[12]=middle[0][3];r[26]=middle[0][0];r[28]=middle[0][2];r[68]=middle[0][1];
        r[14]=result[0][0];r[15]=result[0][1];r[19]=result[0][2];
        r[31]=raw[1][1];r[32]=raw[1][2];r[29]=raw[1][3];r[30]=raw[1][0];
        r[2]=fm[1][1];r[25]=fm[1][2];r[27]=fm[1][3];r[6]=fm[1][0];
        r[3]=y[1][1];r[10]=y[1][2];r[11]=y[1][0];r[4]=z[1][1];r[5]=z[1][0];r[7]=z[1][2];r[22]=z[1][3];
        r[23]=middle[1][3];r[9]=middle[1][1];r[17]=middle[1][2];r[8]=middle[1][0];r[16]=result[1][0];
        r[20]=remaining;r[33]=secondary_end;r[71]=secondary_end;
    }else{
        r[24]=primary_end;r[23]=result[0][1];r[18]=middle[0][2];r[17]=result[0][3];r[32]=result[0][0];r[29]=result[0][2];
        r[25]=middle[1][1];r[26]=z[2][0];r[22]=middle[1][3];r[19]=middle[1][0];r[4]=z[1][3];
        r[7]=result[1][0];r[8]=result[1][1];r[9]=result[1][2];r[21]=result[1][3];
        r[27]=middle[2][0];r[28]=middle[2][3];r[30]=middle[2][2];r[31]=z[2][1];r[15]=result[2][3];
        r[13]=y[2][0];r[14]=middle[2][1];r[16]=y[2][3];r[5]=z[2][3];r[10]=raw[2][0];r[11]=raw[2][1];r[12]=z[2][2];
        r[68]=remaining;r[20]=tertiary_end;r[33]=secondary_end;r[71]=secondary_end;r[93]=tertiary_end;
    }
    r[67]=primary_end;r[40]=matrix_end;
    for(unsigned v=0;v<4;++v){r[xregs[v]]=next_raw[v];r[yregs[v]]=next_y[v];r[zregs[v]]=next_z[v];for(unsigned row=0;row<3;++row)r[mregs[v][row]]=next_matrix[v][row];}
    c.pc=0xb868;g_spu_trampoline_fn=tail;
}
} // namespace buzz
