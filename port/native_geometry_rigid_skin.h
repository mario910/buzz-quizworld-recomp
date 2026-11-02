/* Complete B348 rigid data suffix/B3C0/B3C8: native pair pipeline.
 * Preserve cached next positions/indices before aliased stores. Host AVX2
 * handles prefix pairs; fixed final-pair materialization preserves all ABI. */
#pragma once
#include "native_geometry_job.h"
#include "native_geometry_output.h"
#include "native_stagef_avx2.h"
namespace buzz {
struct GeometryRigidSkin {unsigned count=0;bool aliases=false;};
inline bool read_geometry_rigid_skin(const spu_context& c,GeometryRigidSkin& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    GeometryRigidSkin p;p.count=c.gpr[84]._u32[0];
    if(!p.count || (p.count&1) || p.count>4096)return reject("positive even pair count");
    const unsigned regs[]={80,85,96,54,53,52};unsigned base[6];
    for(unsigned i=0;i<6;++i){const unsigned raw=c.gpr[regs[i]]._u32[0];
        if(raw>=SPU_LS_SIZE || (raw&~15u)<0xe680)return reject("data base bounds");base[i]=raw&~15u;}
    if(uint64_t(base[0])+(p.count+2ull)*16>SPU_LS_SIZE ||
       uint64_t(c.gpr[85]._u32[0])+p.count>=SPU_LS_SIZE ||
       uint64_t(base[2])+p.count*48ull>SPU_LS_SIZE)return reject("stream/lookahead bounds");
    // Aliases are supported, not a fallback. This category conservatively
    // includes any possible indexed palette quad, including wrapped reads.
    const unsigned write_at[]={base[0],base[2]},write_size[]={p.count*16,p.count*48};
    const unsigned index_bytes=((c.gpr[85]._u32[0]+p.count)&~15u)+16-base[1];
    p.aliases=geometry_output_overlap(base[0],p.count*16,base[2],p.count*48);
    for(unsigned w=0;w<2;++w){
        p.aliases|=geometry_output_overlap(write_at[w],write_size[w],base[1],index_bytes);
        for(unsigned row=3;row<6;++row){const unsigned length=256*48;
            const unsigned first=base[row]+length>SPU_LS_SIZE?SPU_LS_SIZE-base[row]:length;
            p.aliases|=geometry_output_overlap(write_at[w],write_size[w],base[row],first);
            if(first<length)p.aliases|=geometry_output_overlap(write_at[w],write_size[w],0,length-first);
        }
    }
    out=p;return true;
}
inline void transform_geometry_rigid_skin(spu_context& ctx,const GeometryRigidSkin& p,
                                         buzz_geometry_job_fn directions,buzz_geometry_job_fn normals)
{
    auto& r=ctx.gpr;const u128 position=r[80],indices=r[85],destination=r[96],count=r[84];
    r[27] = spu_ilhu(0x8000);
    r[44] = spu_ls_read128(&ctx, r[85]._u32[0] + 0x0);
    r[48] = spu_ori(r[84], 0);
    r[27] = spu_iohl(r[27], 0x8001);
    r[18] = spu_ls_read128(&ctx, r[80]._u32[0] + 0x0);
    r[41] = spu_ori(r[80], 0);
    r[17] = spu_ls_read128(&ctx, r[80]._u32[0] + 0x10);
    r[28] = spu_a(r[85], r[48]);
    r[20] = spu_shlqbyi(r[96], 0);
    r[21] = spu_ai(r[85], 2);
    r[19] = spu_shlqbyi(r[84], 0);
    r[46] = spu_rotqby(r[44], r[85]);
    r[44] = spu_ai(r[80], 32);
    r[26] = spu_shufb(r[18], r[38], r[39]);
    r[22] = spu_ori(r[44], 0);
    r[25] = spu_shufb(r[18], r[38], r[43]);
    r[45] = spu_shufb(r[46], r[46], r[27]);
    r[24] = spu_shufb(r[17], r[38], r[39]);
    r[23] = spu_shufb(r[17], r[38], r[43]);
    r[85] = spu_shli(r[45], 4);
    r[80] = spu_shli(r[45], 5);
    r[3] = spu_a(r[80], r[85]);
    r[2] = spu_roti(r[3], 16);
    u128 point[2]={r[18],r[17]},y[2]={r[25],r[23]},z[2]={r[26],r[24]};
    u128 offsets[2]={r[2],r[3]};
    for(unsigned v=0;v+2<p.count;v+=2){
        u128 rows[2][3],matrix[2][3],translation[2],next_point[2],next_y[2],next_z[2];
        const unsigned bases[]={54,53,52};
        for(unsigned vertex=0;vertex<2;++vertex)for(unsigned row=0;row<3;++row)
            rows[vertex][row]=spu_ls_read128(&ctx,r[bases[row]]._u32[0]+offsets[vertex]._u32[0]);
        // Original gathers every palette row and the next input group before
        // writing. This ordering also handles aliases with indices/positions.
        const u128 next_index_ptr=spu_ai(indices,int(v+2));
        const u128 next_index_raw=spu_ls_read128(&ctx,next_index_ptr._u32[0]);
        const u128 next_index_rot=spu_rotqby(next_index_raw,next_index_ptr);
        const u128 next_packed=spu_shufb(next_index_rot,next_index_rot,r[27]);
        const u128 next_offsets=spu_a(spu_shli(next_packed,4),spu_shli(next_packed,5));
        for(unsigned vertex=0;vertex<2;++vertex){
            matrix[vertex][0]=spu_selb(r[55],rows[vertex][0],r[51]);
            matrix[vertex][1]=spu_selb(r[51],rows[vertex][1],r[51]);
            matrix[vertex][2]=spu_selb(r[51],rows[vertex][2],r[51]);
            translation[vertex]=spu_shufb(spu_shufb(rows[vertex][1],rows[vertex][2],r[81]),rows[vertex][0],r[77]);
            next_point[vertex]=spu_ls_read128(&ctx,position._u32[0]+(v+2+vertex)*16);
            next_y[vertex]=spu_shufb(next_point[vertex],r[38],r[43]);
            next_z[vertex]=spu_shufb(next_point[vertex],r[38],r[39]);
        }
        v8 result=v8_fma(v8_pair(point[0],point[1]),v8_pair(matrix[0][0],matrix[1][0]),v8_pair(translation[0],translation[1]));
        result=v8_fma(v8_pair(y[0],y[1]),v8_pair(matrix[0][1],matrix[1][1]),result);
        result=v8_fma(v8_pair(z[0],z[1]),v8_pair(matrix[0][2],matrix[1][2]),result);
        for(unsigned vertex=0;vertex<2;++vertex)for(unsigned row=0;row<3;++row)
            spu_ls_write128(&ctx,destination._u32[0]+(v+vertex)*48+row*16,matrix[vertex][row]);
        u128 result0,result1;_mm_storeu_si128(reinterpret_cast<__m128i*>(&result0),_mm256_castsi256_si128(result));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&result1),_mm256_extracti128_si256(result,1));
        spu_ls_write128(&ctx,position._u32[0]+(v+1)*16,result1);spu_ls_write128(&ctx,position._u32[0]+v*16,result0);
        for(unsigned vertex=0;vertex<2;++vertex){point[vertex]=next_point[vertex];y[vertex]=next_y[vertex];z[vertex]=next_z[vertex];}
        offsets[0]=spu_roti(next_offsets,16);offsets[1]=next_offsets;
    }
    const unsigned last=p.count-2;
    r[41]=spu_ai(position,int(last*16));r[44]=spu_ai(r[41],32);r[22]=r[44];
    r[19]=spu_ai(count,-int(last));r[20]=spu_ai(destination,int(last*48));r[21]=spu_ai(indices,int(last+2));
    r[18]=point[0];r[17]=point[1];r[25]=y[0];r[23]=y[1];r[26]=z[0];r[24]=z[1];
    r[2]=offsets[0];r[3]=offsets[1];
    // Final pair exposes its arithmetic, raw lookahead and pointer lanes.
    r[30] = spu_ori(r[22], 0);
    r[35] = spu_ls_read128(&ctx, r[52]._u32[0] + r[2]._u32[0]);
    r[11] = spu_ai(r[22], 32);
    r[7] = spu_ls_read128(&ctx, r[52]._u32[0] + r[3]._u32[0]);
    r[65] = spu_ai(r[21], 2);
    r[15] = spu_ls_read128(&ctx, r[53]._u32[0] + r[3]._u32[0]);
    r[59] = spu_ai(r[20], 96);
    r[16] = spu_ls_read128(&ctx, r[53]._u32[0] + r[2]._u32[0]);
    r[19] = spu_ai(r[19], -2);
    r[34] = spu_ls_read128(&ctx, r[54]._u32[0] + r[2]._u32[0]);
    r[22] = spu_ori(r[11], 0);
    r[32] = spu_ls_read128(&ctx, r[54]._u32[0] + r[3]._u32[0]);
    r[62] = spu_selb(r[51], r[35], r[51]);
    r[6] = spu_ls_read128(&ctx, r[21]._u32[0] + 0x0);
    r[60] = spu_selb(r[51], r[7], r[51]);
    r[66] = spu_selb(r[51], r[15], r[51]);
    r[31] = spu_shufb(r[15], r[7], r[81]);
    r[5] = spu_selb(r[51], r[16], r[51]);
    r[33] = spu_shufb(r[16], r[35], r[81]);
    r[9] = spu_selb(r[55], r[34], r[51]);
    r[80] = spu_shlqbyi(r[44], 0);
    r[29] = spu_selb(r[55], r[32], r[51]);
    r[4] = spu_shufb(r[31], r[32], r[77]);
    r[3] = spu_shufb(r[33], r[34], r[77]);
    r[10] = spu_rotqby(r[6], r[21]);
    r[21] = spu_ori(r[65], 0);
    r[68] = spu_fma(r[17], r[29], r[4]);
    r[17] = spu_ls_read128(&ctx, r[30]._u32[0] + 0x10);
    r[2] = spu_fma(r[18], r[9], r[3]);
    r[18] = spu_ls_read128(&ctx, r[30]._u32[0] + 0x0);
    r[64] = spu_shufb(r[10], r[10], r[27]);
    spu_ls_write128(&ctx, r[20]._u32[0] + 0x50, r[60]);
    spu_ls_write128(&ctx, r[20]._u32[0] + 0x0, r[9]);
    spu_ls_write128(&ctx, r[20]._u32[0] + 0x10, r[5]);
    r[61] = spu_fma(r[23], r[66], r[68]);
    spu_ls_write128(&ctx, r[20]._u32[0] + 0x20, r[62]);
    r[63] = spu_fma(r[25], r[5], r[2]);
    spu_ls_write128(&ctx, r[20]._u32[0] + 0x30, r[29]);
    r[58] = spu_shli(r[64], 4);
    spu_ls_write128(&ctx, r[20]._u32[0] + 0x40, r[66]);
    r[57] = spu_shli(r[64], 5);
    r[25] = spu_shufb(r[18], r[38], r[43]);
    r[20] = spu_ori(r[59], 0);
    r[23] = spu_shufb(r[17], r[38], r[43]);
    r[49] = spu_fma(r[24], r[60], r[61]);
    r[24] = spu_shufb(r[17], r[38], r[39]);
    r[50] = spu_fma(r[26], r[62], r[63]);
    r[26] = spu_shufb(r[18], r[38], r[39]);
    r[56] = spu_a(r[57], r[58]);
    r[2] = spu_roti(r[56], 16);
    r[3] = spu_shlqbyi(r[56], 0);
    spu_ls_write128(&ctx, r[41]._u32[0] + 0x10, r[49]);
    spu_ls_write128(&ctx, r[41]._u32[0] + 0x0, r[50]);
    r[45]=spu_xsbh(r[102]);r[85]=r[28];
    if(r[45]._u16[0]){ctx.pc=0xb4c0;g_spu_trampoline_fn=directions;}
    else{ctx.pc=0xbb58;g_spu_trampoline_fn=normals;}
}
} // namespace buzz
