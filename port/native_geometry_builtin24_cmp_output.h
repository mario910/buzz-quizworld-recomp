/* Complete builtin format3: raw XYZ + three CMP 11/11/10 attributes.
 * All three original blocks, speculative reads and final ABI are native. */
#pragma once
#include "native_geometry_builtin24_output.h"
namespace buzz {
using GeometryBuiltin24CmpOutput=GeometryBuiltin24Output;
inline bool read_geometry_builtin24_cmp_output(const spu_context& c,GeometryBuiltin24CmpOutput& out,const char** reason=nullptr)
{return read_geometry_builtin24_output(c,out,reason);}
inline GeometryBuiltin24CmpPair pack_geometry_builtin24_cmp3_pair(const u128* r,u128 first,u128 second)
{
    GeometryBuiltin24CmpPair p;const u128 input[]={first,second};
    for(unsigned i=0;i<2;++i){p.scaled[i]=spu_fma(r[32],input[i],r[31]);p.integer[i]=spu_cflts(p.scaled[i],142);p.shifted[i]=spu_rotm(p.integer[i],r[21]);}
    p.join33=spu_shufb(p.shifted[0],p.shifted[1],r[33]);p.join44=spu_shufb(p.shifted[0],p.shifted[1],r[44]);p.packed=spu_selb(p.join44,p.join33,r[41]);p.rotated=spu_rotqbyi(p.packed,4);return p;
}
inline void open_geometry_builtin24_cmp(spu_context& c,u128* r)
{
    const auto ids=spu_ls_read128(&c,r[81]._u32[0]+0x160);
    const auto a=geometry_builtin24_stream(c,r,ids,2),b=geometry_builtin24_stream(c,r,ids,3),d=geometry_builtin24_stream(c,r,ids,4),raw=geometry_builtin24_stream(c,r,ids,1);
    const auto bytes=spu_shli(spu_a(spu_a(r[29],r[29]),r[29]),3),negative=spu_sfi(bytes,0),end=spu_a(r[82],bytes);
    const auto shuffle=r[27],other=r[30];r[21]=spu_ls_read128(&c,0xcf20);
    r[53]=spu_fsmbi(0xf000);r[54]=spu_fsmbi(0xfff0);
    r[52]=spu_selb(other,spu_shlqbyi(shuffle,8),r[53]);r[34]=spu_selb(other,spu_rotqmbyi(shuffle,-4),spu_fsmbi(0xf00));
    r[50]=spu_selb(other,spu_rotqmbyi(shuffle,-8),spu_fsmbi(0xf0));r[51]=spu_selb(other,spu_rotqmbyi(shuffle,-8),spu_fsmbi(0xff));
    r[24]=spu_selb(other,spu_rotqmbyi(shuffle,-12),spu_fsmbi(0xf));
    u128 input[4][3][2];const u128 streams[]={a.pointer,b.pointer,d.pointer,raw.pointer};
    for(unsigned stream=0;stream<4;++stream)for(unsigned pair=0;pair<3;++pair)for(unsigned row=0;row<2;++row)
        input[stream][pair][row]=spu_ls_read128(&c,streams[stream]._u32[0]+pair*32+row*16);
    const auto a0=pack_geometry_builtin24_cmp3_pair(r,input[0][0][0],input[0][0][1]),a1=pack_geometry_builtin24_cmp3_pair(r,input[0][1][0],input[0][1][1]);
    const auto b0=pack_geometry_builtin24_cmp3_pair(r,input[1][0][0],input[1][0][1]),b1=pack_geometry_builtin24_cmp3_pair(r,input[1][1][0],input[1][1][1]);
    const auto d0=pack_geometry_builtin24_cmp3_pair(r,input[2][0][0],input[2][0][1]),d1=pack_geometry_builtin24_cmp3_pair(r,input[2][1][0],input[2][1][1]);
    r[6]=raw.quad;r[7]=b0.integer[0];r[9]=a0.join44;r[76]=a1.join33;r[79]=spu_and(input[3][0][0],r[54]);r[80]=b1.scaled[0];
    r[30]=spu_shufb(a0.packed,spu_and(input[3][0][0],r[54]),r[24]);
    const auto low=spu_selb(spu_shufb(input[3][0][1],spu_il(0),r[51]),b0.packed,r[53]);
    r[29]=spu_shufb(d0.packed,low,r[34]);
    const auto high=spu_shufb(b0.rotated,spu_shufb(a0.rotated,spu_shufb(input[3][0][1],spu_il(0),r[52]),r[34]),r[50]);
    r[28]=spu_shufb(d0.rotated,high,r[24]);
    r[19]=a1.packed;r[25]=a1.rotated;r[17]=b1.packed;r[23]=b1.rotated;r[18]=d1.packed;r[35]=d1.rotated;
    r[43]=input[0][2][0];r[42]=input[0][2][1];r[39]=input[1][2][0];r[40]=input[1][2][1];r[38]=input[2][2][0];r[37]=input[2][2][1];
    r[36]=input[3][1][0];r[16]=input[3][1][1];r[46]=input[3][2][0];r[45]=input[3][2][1];
    r[22]=spu_ai(raw.pointer,96);r[20]=spu_ai(a.pointer,96);r[12]=spu_ai(b.pointer,96);r[14]=spu_ai(d.pointer,96);
    r[49]=end;r[48]=spu_ai(end,32);r[47]=spu_ai(end,16);r[15]=negative;r[13]=negative;
}
inline void step_geometry_builtin24_cmp(spu_context& c,u128* r)
{
    const auto a=pack_geometry_builtin24_cmp3_pair(r,r[43],r[42]),b=pack_geometry_builtin24_cmp3_pair(r,r[39],r[40]),d=pack_geometry_builtin24_cmp3_pair(r,r[38],r[37]);
    r[75]=a.scaled[0];spu_ls_write128(&c,r[48]._u32[0]+r[13]._u32[0],r[28]);
    r[74]=a.scaled[1];spu_ls_write128(&c,r[47]._u32[0]+r[13]._u32[0],r[29]);
    r[64]=b.scaled[0];spu_ls_write128(&c,r[49]._u32[0]+r[13]._u32[0],r[30]);
    r[78]=b.scaled[1];r[73]=d.scaled[0];r[72]=spu_shufb(r[16],r[28],r[52]);r[8]=d.scaled[1];r[10]=spu_shufb(r[16],r[29],r[51]);
    r[13]=a.integer[0];r[71]=a.integer[1];r[70]=b.integer[0];r[68]=b.integer[1];r[5]=d.integer[0];r[67]=d.integer[1];
    // Fresh future pairs must observe all three writes, even on the final step.
    r[27]=spu_ls_read128(&c,r[22]._u32[0]+16);r[26]=spu_ls_read128(&c,r[22]._u32[0]);r[2]=spu_shufb(r[25],r[72],r[34]);
    r[43]=spu_ls_read128(&c,r[20]._u32[0]);r[42]=spu_ls_read128(&c,r[20]._u32[0]+16);r[39]=spu_ls_read128(&c,r[12]._u32[0]);
    r[69]=spu_selb(r[30],r[36],r[54]);r[28]=spu_shufb(r[23],r[2],r[50]);r[3]=a.shifted[0];
    r[40]=spu_ls_read128(&c,r[12]._u32[0]+16);r[66]=a.shifted[1];r[38]=spu_ls_read128(&c,r[14]._u32[0]);r[4]=b.shifted[0];r[37]=spu_ls_read128(&c,r[14]._u32[0]+16);r[63]=b.shifted[1];
    r[30]=spu_shufb(r[19],r[69],r[24]);r[11]=d.shifted[0];r[28]=spu_shufb(r[35],r[28],r[24]);r[61]=d.shifted[1];
    r[77]=a.join33;r[29]=spu_selb(r[10],r[17],r[53]);r[60]=a.join44;r[65]=spu_ai(r[15],48);r[16]=b.join44;r[62]=spu_ai(r[22],32);
    r[59]=b.join33;r[57]=spu_ai(r[20],32);r[36]=d.join44;r[19]=a.packed;r[58]=d.join33;r[56]=spu_ai(r[12],32);
    r[29]=spu_shufb(r[18],r[29],r[34]);r[17]=b.packed;r[25]=a.rotated;r[55]=spu_ai(r[14],32);r[22]=r[62];r[18]=d.packed;r[23]=b.rotated;
    r[20]=r[57];r[12]=r[56];r[14]=r[55];r[35]=d.rotated;r[15]=r[65];r[13]=r[65];r[16]=r[45];r[36]=r[46];
}
inline void emit_geometry_builtin24_cmp_output(spu_context& c,const GeometryBuiltin24CmpOutput& plan,buzz_geometry_job_fn finish)
{
    u128 r[128];memcpy(r,c.gpr,sizeof r);open_geometry_builtin24_cmp(c,r);
    for(unsigned group=0;group<plan.groups;++group){step_geometry_builtin24_cmp(c,r);if(group+1<plan.groups){r[46]=r[26];r[45]=r[27];}}
    memcpy(c.gpr,r,sizeof r);c.pc=0x5648;g_spu_trampoline_fn=finish;
}
}
