/* Complete builtin output format1 (20 bytes/vertex). Two CMP attributes and
 * raw XYZ use a staged pair pipeline. Actual ordered stores, fresh post-store
 * lookahead and every last scratch vector remain exact at queued 5648. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
namespace buzz {
struct GeometryBuiltin20Output {unsigned vertices=0,groups=0;};
struct GeometryBuiltin20Pair {
    u128 scaled[2],integer[2],shifted[2],join33,join44,packed,rotated;
};
inline GeometryBuiltin20Pair pack_geometry_builtin20_pair(const u128* r,u128 first,u128 second)
{
    GeometryBuiltin20Pair p;const u128 input[]={first,second};
    for(unsigned i=0;i<2;++i){p.scaled[i]=spu_fma(r[32],input[i],r[31]);p.integer[i]=spu_cflts(p.scaled[i],142);p.shifted[i]=spu_rotm(p.integer[i],r[17]);}
    p.join33=spu_shufb(p.shifted[0],p.shifted[1],r[33]);p.join44=spu_shufb(p.shifted[0],p.shifted[1],r[44]);
    p.packed=spu_selb(p.join44,p.join33,r[41]);p.rotated=spu_rotqbyi(p.packed,4);return p;
}
inline u128 geometry_builtin20_stream(const spu_context& c,const u128* r,u128 ids,unsigned semantic)
{
    const auto selector=spu_shufb(spu_il(semantic),spu_il(0),r[42]);
    const auto index=spu_ai(spu_clz(spu_gbb(spu_ceqb(selector,ids))),-16),offset=spu_shli(index,2),address=spu_a(offset,r[28]);
    return spu_rotqby(spu_ls_read128(&c,offset._u32[0]+r[28]._u32[0]),address);
}
inline bool read_geometry_builtin20_output(const spu_context& c,GeometryBuiltin20Output& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    const unsigned vertices=c.gpr[29]._u32[0];
    // The enclosing controller rounds to an even count. Zero underflows the
    // original do-loop; odd counts can never reach zero in steps of 40 bytes.
    if(!vertices || (vertices&1) || vertices>65536)return reject("builtin20 rounded count");
    const unsigned groups=vertices/2;
    for(unsigned group=0;group<groups;++group)for(unsigned offset=0;offset<64;offset+=16)
        if(((c.gpr[82]._u32[0]+group*40+offset)&0x3fff0)<0xe680)return reject("builtin20 protected padded write");
    out={vertices,groups};return true;
}
inline void open_geometry_builtin20(spu_context& c,u128* r)
{
    const auto ids=spu_ls_read128(&c,r[81]._u32[0]+0x160);
    const auto a=geometry_builtin20_stream(c,r,ids,2),b=geometry_builtin20_stream(c,r,ids,3),raw=geometry_builtin20_stream(c,r,ids,1);
    const auto bytes=spu_a(spu_shli(r[29],4),spu_shli(r[29],2)),negative=spu_sfi(bytes,0),end=spu_a(r[82],bytes);
    r[17]=spu_ls_read128(&c,0xcf20);r[79]=spu_rotqmbyi(r[27],-4);r[78]=spu_rotqmbyi(r[27],-12);
    r[53]=spu_selb(r[30],r[79],spu_fsmbi(0xfff));r[51]=spu_selb(r[30],r[79],spu_fsmbi(0xf00));r[50]=spu_selb(r[30],r[78],spu_fsmbi(0xf));
    r[52]=spu_fsmbi(0xf000);r[54]=r[52];r[55]=spu_fsmbi(0xfff0);r[74]=spu_il(0);
    u128 inputs[3][3][2];const u128 streams[]={a,b,raw};
    for(unsigned stream=0;stream<3;++stream)for(unsigned pair=0;pair<3;++pair)for(unsigned row=0;row<2;++row)
        inputs[stream][pair][row]=spu_ls_read128(&c,streams[stream]._u32[0]+pair*32+row*16);
    const auto a0=pack_geometry_builtin20_pair(r,inputs[0][0][0],inputs[0][0][1]),a1=pack_geometry_builtin20_pair(r,inputs[0][1][0],inputs[0][1][1]);
    const auto b0=pack_geometry_builtin20_pair(r,inputs[1][0][0],inputs[1][0][1]),b1=pack_geometry_builtin20_pair(r,inputs[1][1][0],inputs[1][1][1]);
    // Values that survive the initial packing into the final guest ABI.
    r[2]=a1.shifted[1];r[4]=b0.join33;r[12]=b1.integer[1];r[60]=b1.join33;r[61]=a1.join44;r[62]=a1.join33;
    r[70]=a0.rotated;r[72]=spu_ai(raw,32);r[73]=inputs[2][0][1];r[75]=inputs[2][0][0];
    r[76]=b1.shifted[0];r[77]=b1.shifted[1];r[80]=a1.shifted[0];
    r[43]=spu_ai(end,48);r[45]=spu_ai(end,16);r[46]=spu_ai(end,40);r[47]=spu_ai(end,32);r[48]=end;r[49]=end;
    r[25]=a1.packed;r[20]=b1.packed;r[14]=a1.rotated;r[36]=b1.rotated;
    r[21]=spu_shufb(a0.packed,spu_and(inputs[2][0][0],r[55]),r[50]);
    r[18]=spu_shufb(b0.rotated,spu_and(r[52],a0.rotated),r[51]);
    r[19]=spu_selb(spu_shufb(inputs[2][0][1],r[74],r[53]),b0.packed,r[52]);
    r[34]=spu_ls_read128(&c,end._u32[0]+negative._u32[0]);r[16]=negative;r[26]=negative;
    r[23]=spu_ai(a,96);r[22]=spu_ai(b,96);r[24]=spu_ai(raw,96);
    r[29]=inputs[0][2][0];r[35]=inputs[0][2][1];r[28]=inputs[1][2][0];r[30]=inputs[1][2][1];
    r[38]=inputs[2][1][0];r[37]=inputs[2][1][1];r[40]=inputs[2][2][0];r[39]=inputs[2][2][1];
}
inline void step_geometry_builtin20(spu_context& c,u128* r)
{
    const auto a=pack_geometry_builtin20_pair(r,r[29],r[35]),b=pack_geometry_builtin20_pair(r,r[28],r[30]);
    r[69]=a.scaled[0];r[15]=a.scaled[1];r[10]=spu_shufb(r[37],r[19],r[53]);r[67]=b.scaled[0];r[66]=b.scaled[1];
    r[71]=spu_a(r[48],r[26]);r[56]=spu_selb(r[21],r[38],r[55]);r[68]=spu_andbi(r[71],15);
    r[65]=a.integer[0];r[8]=a.integer[1];r[7]=spu_shufb(r[68],r[68],r[42]);r[64]=b.integer[0];
    r[35]=spu_rotqby(r[34],r[68]);r[63]=b.integer[1];r[38]=spu_selb(r[18],r[14],r[52]);r[34]=spu_sf(r[7],r[27]);r[5]=spu_ai(r[22],32);
    r[57]=a.shifted[0];r[9]=spu_shufb(r[18],r[19],r[34]);r[58]=a.shifted[1];r[29]=spu_shufb(r[19],r[21],r[34]);
    r[59]=b.shifted[0];r[28]=spu_shufb(r[21],r[35],r[34]);r[6]=b.shifted[1];r[30]=spu_shufb(r[18],r[18],r[34]);r[21]=spu_ai(r[23],32);
    spu_ls_write128(&c,r[47]._u32[0]+r[16]._u32[0],r[9]);
    r[19]=spu_selb(r[10],r[20],r[54]);r[11]=a.join33;r[18]=spu_ai(r[24],32);r[13]=a.join44;r[37]=b.join33;r[3]=b.join44;
    spu_ls_write128(&c,r[45]._u32[0]+r[16]._u32[0],r[29]);spu_ls_write128(&c,r[49]._u32[0]+r[16]._u32[0],r[28]);spu_ls_write128(&c,r[43]._u32[0]+r[16]._u32[0],r[30]);
    r[20]=b.packed;
    // These loads follow ALL four stores. Future input or old destination
    // bytes can be changed by this iteration, including its final iteration.
    r[34]=spu_ls_read128(&c,r[46]._u32[0]+r[16]._u32[0]);r[16]=spu_ai(r[26],40);
    r[35]=spu_ls_read128(&c,r[23]._u32[0]+16);r[37]=r[39];r[10]=spu_ls_read128(&c,r[24]._u32[0]+16);r[26]=r[16];
    r[30]=spu_ls_read128(&c,r[22]._u32[0]+16);r[9]=spu_ls_read128(&c,r[24]._u32[0]);r[24]=r[18];
    r[29]=spu_ls_read128(&c,r[23]._u32[0]);r[23]=r[21];r[28]=spu_ls_read128(&c,r[22]._u32[0]);r[22]=r[5];
    r[21]=spu_shufb(r[25],r[56],r[50]);r[25]=a.packed;r[18]=spu_shufb(r[36],r[38],r[51]);r[38]=r[40];r[36]=b.rotated;r[14]=a.rotated;
}
inline void emit_geometry_builtin20_output(spu_context& c,const GeometryBuiltin20Output& plan,buzz_geometry_job_fn finish)
{
    u128 r[128];memcpy(r,c.gpr,sizeof r);open_geometry_builtin20(c,r);
    for(unsigned group=0;group<plan.groups;++group){step_geometry_builtin20(c,r);if(group+1<plan.groups){r[40]=r[9];r[39]=r[10];}}
    memcpy(c.gpr,r,sizeof r);c.pc=0x5648;g_spu_trampoline_fn=finish;
}
}
