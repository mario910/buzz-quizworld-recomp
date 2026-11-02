/* Complete BB58/BB68 cofactor normal transform and normalization.
 * Host groups cache all matrix/normal inputs before stores; AVX2 pairs use
 * SPU RTZ arithmetic, followed by exact four-lane reciprocal-root/FI steps.
 * The final group materializes every original register and queued boundary. */
#pragma once
#include "native_geometry_job.h"
#include "native_geometry_output.h"
#include "native_stagef_avx2.h"
namespace buzz {
struct GeometrySkinCofactor {unsigned count=0;bool aliases=false;};
inline bool read_geometry_skin_cofactor(const spu_context& c,GeometrySkinCofactor& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    GeometrySkinCofactor p;p.count=c.gpr[84]._u32[0];
    if(!p.count || (p.count&3) || p.count>4096)return reject("positive group-of-four count");
    const unsigned normal=c.gpr[83]._u32[0],matrix=c.gpr[96]._u32[0];
    if(normal>=SPU_LS_SIZE || matrix>=SPU_LS_SIZE || (normal&~15u)<0xe680 || (matrix&~15u)<0xe680)return reject("physical data bases");
    if(uint64_t(normal&~15u)+p.count*16>SPU_LS_SIZE)return reject("normal writes");
    const unsigned begin=matrix&~15u,length=p.count*48,first=begin+length>SPU_LS_SIZE?SPU_LS_SIZE-begin:length;
    p.aliases=geometry_output_overlap(normal&~15u,p.count*16,begin,first) ||
        (first<length && geometry_output_overlap(normal&~15u,p.count*16,0,length-first));
    out=p;return true;
}
inline v8 skin_cofactor_fms(v8 a,v8 b,v8 c)
{
    return v8_fma(a,b,_mm256_xor_si256(c,v8_set(0x80000000u)));
}
inline void skin_cofactor_pair(v8 value,u128& a,u128& b)
{
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&a),_mm256_castsi256_si128(value));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&b),_mm256_extracti128_si256(value,1));
}
inline void transform_geometry_skin_cofactor(spu_context& ctx,const GeometrySkinCofactor& p,buzz_geometry_job_fn tail)
{
    auto& r=ctx.gpr;const u128 normal=r[83],matrix=r[96],count=r[84];
    for(unsigned v=0;v+4<p.count;v+=4){
        u128 rows[4][3],point[4],y[4][3],z[4][3],ny[4],nz[4],result[4],scale[4];
        for(unsigned i=0;i<4;++i){
            for(unsigned row=0;row<3;++row){
                rows[i][row]=spu_ls_read128(&ctx,matrix._u32[0]+(v+i)*48+row*16);
                y[i][row]=spu_shufb(rows[i][row],rows[i][row],r[43]);
                z[i][row]=spu_shufb(rows[i][row],rows[i][row],r[39]);
            }
            point[i]=spu_ls_read128(&ctx,normal._u32[0]+(v+i)*16);
            ny[i]=spu_shufb(point[i],r[38],r[43]);nz[i]=spu_shufb(point[i],r[38],r[39]);
        }
        for(unsigned i=0;i<4;i+=2){
            const v8 ay=v8_pair(y[i][0],y[i+1][0]),az=v8_pair(z[i][0],z[i+1][0]);
            const v8 by=v8_pair(y[i][1],y[i+1][1]),bz=v8_pair(z[i][1],z[i+1][1]);
            const v8 cy=v8_pair(y[i][2],y[i+1][2]),cz=v8_pair(z[i][2],z[i+1][2]);
            const v8 a=skin_cofactor_fms(ay,az,v8_fm(cz,by));
            const v8 b=skin_cofactor_fms(bz,by,v8_fm(cy,az));
            const v8 c=skin_cofactor_fms(cy,cz,v8_fm(bz,ay));
            v8 value=v8_fm(a,v8_pair(point[i],point[i+1]));
            value=v8_fma(b,v8_pair(ny[i],ny[i+1]),value);
            value=v8_fma(c,v8_pair(nz[i],nz[i+1]),value);
            skin_cofactor_pair(value,result[i],result[i+1]);
        }
        const u128 ab=spu_shufb(result[0],result[1],r[72]);
        const u128 ac=spu_selb(result[0],result[2],r[78]),bd=spu_selb(result[1],result[3],r[78]);
        const u128 cd=spu_shufb(result[2],result[3],r[79]),middle=spu_shufb(ac,bd,r[91]);
        const u128 zz=spu_shufb(ab,cd,r[73]),yy=spu_selb(middle,cd,r[70]),xx=spu_selb(ab,middle,r[70]);
        u128 length=spu_fm(yy,yy);length=spu_fma(xx,xx,length);length=spu_fma(zz,zz,length);
        const u128 inverse=spu_fi(length,spu_frsqest(length));const unsigned shuffles[]={92,76,75,74};
        for(unsigned i=0;i<4;++i)scale[i]=spu_shufb(inverse,r[55],r[shuffles[i]]);
        for(unsigned i=0;i<4;i+=2)skin_cofactor_pair(v8_fm(v8_pair(result[i],result[i+1]),v8_pair(scale[i],scale[i+1])),result[i],result[i+1]);
        // All current group reads precede every store, including aliases.
        for(unsigned i=0;i<4;++i)spu_ls_write128(&ctx,normal._u32[0]+(v+i)*16,result[i]);
    }
    const unsigned last=p.count-4;
    r[41]=spu_ai(count,-int(last));r[40]=spu_ai(matrix,int(last*48));r[83]=spu_ai(normal,int(last*16));
    r[47] = spu_ai(r[40], 192);
    r[62] = spu_ls_read128(&ctx, r[40]._u32[0] + 0xB0);
    r[4] = spu_ai(r[83], 64);
    r[14] = spu_ls_read128(&ctx, r[40]._u32[0] + 0x10);
    r[46] = spu_ori(r[83], 0);
    r[44] = spu_ls_read128(&ctx, r[40]._u32[0] + 0x20);
    r[41] = spu_ai(r[41], -4);
    r[24] = spu_ls_read128(&ctx, r[40]._u32[0] + 0x40);
    r[23] = spu_ls_read128(&ctx, r[40]._u32[0] + 0x50);
    r[11] = spu_ls_read128(&ctx, r[40]._u32[0] + 0x70);
    r[7] = spu_ls_read128(&ctx, r[40]._u32[0] + 0x80);
    r[6] = spu_ls_read128(&ctx, r[40]._u32[0] + 0xA0);
    r[10] = spu_ls_read128(&ctx, r[40]._u32[0] + 0x0);
    r[17] = spu_ls_read128(&ctx, r[40]._u32[0] + 0x30);
    r[5] = spu_ls_read128(&ctx, r[40]._u32[0] + 0x60);
    r[21] = spu_ls_read128(&ctx, r[40]._u32[0] + 0x90);
    r[40] = spu_ori(r[47], 0);
    r[59] = spu_shufb(r[44], r[44], r[39]);
    r[57] = spu_shufb(r[23], r[23], r[39]);
    r[25] = spu_shufb(r[14], r[14], r[43]);
    r[47] = spu_shufb(r[62], r[62], r[39]);
    r[30] = spu_shufb(r[24], r[24], r[43]);
    r[64] = spu_shufb(r[11], r[11], r[43]);
    r[22] = spu_fm(r[59], r[25]);
    r[35] = spu_shufb(r[6], r[6], r[43]);
    r[56] = spu_shufb(r[7], r[7], r[39]);
    r[20] = spu_fm(r[57], r[30]);
    r[45] = spu_shufb(r[10], r[10], r[39]);
    r[15] = spu_shufb(r[17], r[17], r[39]);
    r[18] = spu_fm(r[47], r[35]);
    r[16] = spu_shufb(r[5], r[5], r[39]);
    r[19] = spu_fm(r[56], r[64]);
    r[61] = spu_shufb(r[21], r[21], r[39]);
    r[3] = spu_shufb(r[10], r[10], r[43]);
    r[68] = spu_shufb(r[17], r[17], r[43]);
    r[63] = spu_shufb(r[5], r[5], r[43]);
    r[9] = spu_shufb(r[44], r[44], r[43]);
    r[33] = spu_fms(r[3], r[45], r[22]);
    r[8] = spu_shufb(r[23], r[23], r[43]);
    r[32] = spu_fms(r[68], r[15], r[20]);
    r[48] = spu_shufb(r[7], r[7], r[43]);
    r[50] = spu_fms(r[63], r[16], r[19]);
    r[13] = spu_shufb(r[21], r[21], r[43]);
    r[2] = spu_fm(r[9], r[45]);
    r[10] = spu_shufb(r[62], r[62], r[43]);
    r[26] = spu_fm(r[8], r[15]);
    r[34] = spu_ls_read128(&ctx, r[83]._u32[0] + 0x0);
    r[65] = spu_fm(r[48], r[16]);
    r[29] = spu_ls_read128(&ctx, r[83]._u32[0] + 0x20);
    r[42] = spu_fms(r[13], r[61], r[18]);
    r[60] = spu_ls_read128(&ctx, r[83]._u32[0] + 0x30);
    r[36] = spu_fm(r[10], r[61]);
    r[28] = spu_ls_read128(&ctx, r[83]._u32[0] + 0x10);
    r[83] = spu_ori(r[4], 0);
    r[27] = spu_shufb(r[14], r[14], r[39]);
    r[66] = spu_shufb(r[24], r[24], r[39]);
    r[17] = spu_fm(r[33], r[34]);
    r[37] = spu_shufb(r[11], r[11], r[39]);
    r[19] = spu_fm(r[50], r[29]);
    r[31] = spu_shufb(r[6], r[6], r[39]);
    r[23] = spu_fms(r[27], r[25], r[2]);
    r[44] = spu_shufb(r[34], r[38], r[43]);
    r[58] = spu_fm(r[27], r[3]);
    r[62] = spu_shufb(r[29], r[38], r[43]);
    r[21] = spu_fms(r[66], r[30], r[26]);
    r[15] = spu_shufb(r[60], r[38], r[43]);
    r[24] = spu_fms(r[37], r[64], r[65]);
    r[45] = spu_shufb(r[28], r[38], r[43]);
    r[49] = spu_fm(r[37], r[63]);
    r[2] = spu_shufb(r[34], r[38], r[39]);
    r[61] = spu_fms(r[31], r[35], r[36]);
    r[16] = spu_shufb(r[29], r[38], r[39]);
    r[12] = spu_fm(r[66], r[68]);
    r[5] = spu_shufb(r[60], r[38], r[39]);
    r[18] = spu_fm(r[42], r[60]);
    r[3] = spu_shufb(r[28], r[38], r[39]);
    r[4] = spu_fm(r[31], r[13]);
    r[20] = spu_fm(r[32], r[28]);
    r[25] = spu_fms(r[9], r[59], r[58]);
    r[7] = spu_fms(r[48], r[56], r[49]);
    r[27] = spu_fms(r[8], r[57], r[12]);
    r[14] = spu_fma(r[23], r[44], r[17]);
    r[30] = spu_fms(r[10], r[47], r[4]);
    r[22] = spu_fma(r[21], r[45], r[20]);
    r[6] = spu_fma(r[24], r[62], r[19]);
    r[26] = spu_fma(r[61], r[15], r[18]);
    r[42] = spu_fma(r[25], r[2], r[14]);
    r[58] = spu_fma(r[27], r[3], r[22]);
    r[56] = spu_fma(r[7], r[16], r[6]);
    r[50] = spu_fma(r[30], r[5], r[26]);
    r[63] = spu_shufb(r[42], r[58], r[72]);
    r[66] = spu_selb(r[42], r[56], r[78]);
    r[68] = spu_selb(r[58], r[50], r[78]);
    r[65] = spu_shufb(r[56], r[50], r[79]);
    r[64] = spu_shufb(r[66], r[68], r[91]);
    r[31] = spu_shufb(r[63], r[65], r[73]);
    r[11] = spu_selb(r[64], r[65], r[70]);
    r[36] = spu_selb(r[63], r[64], r[70]);
    r[37] = spu_fm(r[11], r[11]);
    r[35] = spu_fma(r[36], r[36], r[37]);
    r[33] = spu_fma(r[31], r[31], r[35]);
    r[34] = spu_frsqest(r[33]);
    r[32] = spu_fi(r[33], r[34]);
    r[29] = spu_shufb(r[32], r[55], r[74]);
    r[60] = spu_shufb(r[32], r[55], r[92]);
    r[59] = spu_shufb(r[32], r[55], r[76]);
    r[57] = spu_shufb(r[32], r[55], r[75]);
    r[49] = spu_fm(r[50], r[29]);
    r[48] = spu_fm(r[42], r[60]);
    r[47] = spu_fm(r[58], r[59]);
    r[44] = spu_fm(r[56], r[57]);
    spu_ls_write128(&ctx, r[46]._u32[0] + 0x30, r[49]);
    spu_ls_write128(&ctx, r[46]._u32[0] + 0x0, r[48]);
    spu_ls_write128(&ctx, r[46]._u32[0] + 0x10, r[47]);
    spu_ls_write128(&ctx, r[46]._u32[0] + 0x20, r[44]);
    ctx.pc=0xb4c0;g_spu_trampoline_fn=tail;
}
} // namespace buzz
