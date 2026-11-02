/* Complete builtin format2: raw XYZ + CMP semantic2 + SNORM16x4 semantic3.
 * Local vectors replace all three blocks, including final lookahead and ABI.
 * Every pair stores +32,+16,+0 before fresh input reads; no lifted tail. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
namespace buzz {
struct GeometryBuiltin24Output {unsigned vertices=0,groups=0;};
struct GeometryBuiltin24CmpPair {u128 scaled[2],integer[2],shifted[2],join33,join44,packed,rotated;};
struct GeometryBuiltin24SnormPair {u128 scaled[2],integer[2];};
struct GeometryBuiltin24Lookup {u128 leading,address,quad,pointer;};
inline GeometryBuiltin24Lookup geometry_builtin24_stream(const spu_context& c,const u128* r,u128 ids,unsigned semantic)
{
    GeometryBuiltin24Lookup p;const auto selector=spu_shufb(spu_il(semantic),spu_il(0),r[42]);
    p.leading=spu_clz(spu_gbb(spu_ceqb(selector,ids)));const auto offset=spu_shli(spu_ai(p.leading,-16),2);
    p.address=spu_a(offset,r[28]);p.quad=spu_ls_read128(&c,offset._u32[0]+r[28]._u32[0]);p.pointer=spu_rotqby(p.quad,p.address);return p;
}
inline GeometryBuiltin24CmpPair pack_geometry_builtin24_cmp_pair(const u128* r,u128 first,u128 second)
{
    GeometryBuiltin24CmpPair p;const u128 input[]={first,second};
    for(unsigned i=0;i<2;++i){p.scaled[i]=spu_fma(r[32],input[i],r[31]);p.integer[i]=spu_cflts(p.scaled[i],142);p.shifted[i]=spu_rotm(p.integer[i],r[18]);}
    p.join33=spu_shufb(p.shifted[0],p.shifted[1],r[33]);p.join44=spu_shufb(p.shifted[0],p.shifted[1],r[44]);p.packed=spu_selb(p.join44,p.join33,r[41]);p.rotated=spu_rotqbyi(p.packed,4);return p;
}
inline GeometryBuiltin24SnormPair pack_geometry_builtin24_snorm_pair(const u128* r,u128 first,u128 second)
{
    GeometryBuiltin24SnormPair p;const u128 input[]={first,second};
    for(unsigned i=0;i<2;++i){p.scaled[i]=spu_fma(r[35],input[i],r[48]);p.integer[i]=spu_cflts(p.scaled[i],157);}return p;
}
inline bool read_geometry_builtin24_output(const spu_context& c,GeometryBuiltin24Output& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    const unsigned vertices=c.gpr[29]._u32[0];if(!vertices || (vertices&1) || vertices>65536)return reject("builtin24 rounded count");
    const unsigned groups=vertices/2;
    for(unsigned group=0;group<groups;++group)for(unsigned offset=0;offset<48;offset+=16)
        if(((c.gpr[82]._u32[0]+group*48+offset)&0x3fff0)<0xe680)return reject("builtin24 protected padded write");
    out={vertices,groups};return true;
}
inline void open_geometry_builtin24(spu_context& c,u128* r)
{
    const auto ids=spu_ls_read128(&c,r[81]._u32[0]+0x160);
    const auto a=geometry_builtin24_stream(c,r,ids,2),b=geometry_builtin24_stream(c,r,ids,3),raw=geometry_builtin24_stream(c,r,ids,1);
    const auto bytes=spu_shli(spu_a(spu_a(r[29],r[29]),r[29]),3),negative=spu_sfi(bytes,0),end=spu_a(r[82],bytes);
    const auto mask=spu_ls_read128(&c,0xd9b0),raw_shuffle=r[27],other=r[30];
    r[18]=spu_ls_read128(&c,0xcf20);r[51]=spu_selb(other,spu_shlqbyi(raw_shuffle,8),spu_fsmbi(0xf000));
    r[50]=spu_selb(other,spu_rotqmbyi(raw_shuffle,-4),spu_fsmbi(0xf00));r[56]=spu_fsmbi(0xff);r[52]=spu_fsmbi(0xfff0);
    r[57]=spu_rotqmbyi(mask,-8);r[23]=spu_selb(other,spu_rotqmbyi(raw_shuffle,-8),r[56]);
    r[49]=spu_selb(other,mask,spu_fsmbi(0xff00));r[20]=spu_selb(other,spu_rotqmbyi(raw_shuffle,-12),spu_fsmbi(0xf));r[21]=spu_selb(other,r[57],r[56]);
    u128 input[3][3][2];const u128 streams[]={a.pointer,b.pointer,raw.pointer};
    for(unsigned stream=0;stream<3;++stream)for(unsigned pair=0;pair<3;++pair)for(unsigned row=0;row<2;++row)
        input[stream][pair][row]=spu_ls_read128(&c,streams[stream]._u32[0]+pair*32+row*16);
    const auto a0=pack_geometry_builtin24_cmp_pair(r,input[0][0][0],input[0][0][1]),a1=pack_geometry_builtin24_cmp_pair(r,input[0][1][0],input[0][1][1]);
    const auto b0=pack_geometry_builtin24_snorm_pair(r,input[1][0][0],input[1][0][1]),b1=pack_geometry_builtin24_snorm_pair(r,input[1][1][0],input[1][1][1]);
    // Prefix-only values remain visible even after every pipeline stage runs.
    r[4]=spu_and(input[2][0][0],r[52]);r[5]=a0.rotated;r[6]=raw.quad;r[7]=a.pointer;r[24]=spu_ai(a.pointer,64);r[25]=a1.shifted[0];r[27]=bytes;
    r[53]=raw.leading;r[54]=spu_shufb(a0.rotated,spu_shufb(input[2][0][1],spu_il(0),r[51]),r[50]);r[55]=spu_ai(raw.pointer,32);
    r[71]=raw.address;r[72]=b1.scaled[0];r[73]=spu_ai(raw.pointer,64);r[74]=b0.integer[0];r[75]=spu_shufb(input[2][0][1],spu_il(0),r[23]);r[76]=a1.join44;r[77]=a1.join33;
    r[79]=spu_ai(a.pointer,32);r[80]=spu_ai(b.pointer,32);
    r[45]=spu_ai(end,16);r[46]=spu_ai(end,32);r[47]=end;r[11]=negative;r[12]=negative;
    r[28]=spu_shufb(a0.packed,r[4],r[20]);r[17]=spu_shufb(b0.integer[0],r[75],r[49]);r[22]=spu_shufb(b0.integer[1],r[54],r[21]);
    r[10]=a1.packed;r[26]=a1.rotated;r[13]=b1.integer[0];r[14]=b1.integer[1];
    r[40]=input[0][2][0];r[39]=input[0][2][1];r[37]=input[1][2][0];r[36]=input[1][2][1];r[38]=input[2][1][0];r[29]=input[2][1][1];r[43]=input[2][2][0];r[42]=input[2][2][1];
    r[16]=spu_ai(a.pointer,96);r[15]=spu_ai(b.pointer,96);r[19]=spu_ai(raw.pointer,96);
}
inline void step_geometry_builtin24(spu_context& c,u128* r)
{
    const auto a=pack_geometry_builtin24_cmp_pair(r,r[40],r[39]);const auto b=pack_geometry_builtin24_snorm_pair(r,r[37],r[36]);
    r[69]=a.scaled[0];r[68]=a.scaled[1];r[67]=spu_shufb(r[29],r[22],r[51]);r[66]=b.scaled[0];
    spu_ls_write128(&c,r[46]._u32[0]+r[12]._u32[0],r[22]);r[64]=b.scaled[1];spu_ls_write128(&c,r[45]._u32[0]+r[12]._u32[0],r[17]);
    r[8]=spu_selb(r[28],r[38],r[52]);r[9]=spu_shufb(r[29],r[17],r[23]);r[3]=spu_ai(r[16],32);r[63]=spu_shufb(r[26],r[67],r[50]);r[61]=a.integer[0];
    spu_ls_write128(&c,r[47]._u32[0]+r[12]._u32[0],r[28]);r[60]=a.integer[1];
    // All new input loads follow all three stores, including the final stage.
    r[34]=spu_ls_read128(&c,r[19]._u32[0]+16);r[65]=b.integer[0];r[30]=spu_ls_read128(&c,r[19]._u32[0]);r[62]=b.integer[1];
    r[39]=spu_ls_read128(&c,r[16]._u32[0]+16);r[29]=spu_ai(r[19],32);r[40]=spu_ls_read128(&c,r[16]._u32[0]);r[12]=spu_ai(r[11],48);
    r[37]=spu_ls_read128(&c,r[15]._u32[0]);r[2]=spu_ai(r[15],32);r[36]=spu_ls_read128(&c,r[15]._u32[0]+16);r[38]=a.shifted[0];
    r[28]=spu_shufb(r[10],r[8],r[20]);r[59]=a.shifted[1];r[17]=spu_shufb(r[13],r[9],r[49]);r[19]=r[29];r[22]=spu_shufb(r[14],r[63],r[21]);
    r[29]=r[42];r[11]=r[12];r[13]=b.integer[0];r[14]=b.integer[1];r[16]=r[3];r[58]=a.join33;r[15]=r[2];r[78]=a.join44;r[38]=r[43];r[10]=a.packed;r[26]=a.rotated;
}
inline void emit_geometry_builtin24_output(spu_context& c,const GeometryBuiltin24Output& plan,buzz_geometry_job_fn finish)
{
    auto* r=c.gpr;open_geometry_builtin24(c,r);
    for(unsigned group=0;group<plan.groups;++group){step_geometry_builtin24(c,r);if(group+1<plan.groups){r[43]=r[30];r[42]=r[34];}}
    c.pc=0x5648;g_spu_trampoline_fn=finish;
}
}
