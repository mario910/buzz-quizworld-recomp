/* Complete A8F8/AAF8/AB00 input format1: XYZ+2CMP -> three float4 streams.
 * Actual metadata publication and fresh post-store reads retain all aliases. */
#pragma once
#include "native_geometry_builtin_input.h"
namespace buzz {
struct GeometryBuiltin20Input {unsigned groups=0;};
inline bool read_geometry_builtin20_input(const spu_context& c,GeometryBuiltin20Input& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    unsigned groups=0;GeometryBuiltinInputMemory memory{c};
    if(!read_geometry_builtin_input_metadata(memory,geometry_builtin_input_metadata(c),1,groups,reason))return false;
    out={groups};return true;
}
inline void open_geometry_builtin20_input(spu_context& c,u128* r)
{
    const auto input=r[4],advance=spu_ai(input,40),iota=spu_ls_read128(&c,0xd980),packing=spu_ls_read128(&c,0xda80);
    const auto shifts=spu_ls_read128(&c,0xd1a0),mask=spu_ls_read128(&c,0xd190),selector=r[80],table=r[83],bytes=r[36];
    const u128 published[]={r[35],r[37],r[61]};const unsigned managers[]={r[3]._u32[0],r[82]._u32[0]};
    const auto q0=spu_ls_read128(&c,input._u32[0]),q1=spu_ls_read128(&c,input._u32[0]+16),q2=spu_ls_read128(&c,input._u32[0]+32),q3=spu_ls_read128(&c,input._u32[0]+48);
    const auto bias=spu_andbi(input,15),repeated=spu_shufb(bias,bias,r[65]),align=spu_a(iota,repeated);
    const auto four=spu_a(iota,spu_shufb(spu_il(4),spu_il(4),r[65])),twelve=spu_a(iota,spu_shufb(spu_il(12),spu_il(12),r[65]));
    const auto pack0=spu_shufb(iota,iota,packing),pack4=spu_shufb(four,four,packing),pack12=spu_shufb(twelve,twelve,packing);
    const auto aligned0=spu_shufb(q0,q1,align),aligned1=spu_shufb(q1,q2,align),aligned2=spu_shufb(q2,q3,align);
    const auto b0bits=spu_shufb(aligned0,aligned1,pack12),b1bits=spu_shufb(aligned2,aligned2,pack0),d0bits=spu_shufb(aligned1,aligned2,pack0),d1bits=spu_shufb(aligned2,aligned2,pack4);
    const auto b0=decode_geometry_builtin_input_cmp(b0bits,shifts,mask,r[28],r[27]),b1=decode_geometry_builtin_input_cmp(b1bits,shifts,mask,r[28],r[27]);
    const auto d0=decode_geometry_builtin_input_cmp(d0bits,shifts,mask,r[28],r[27]),d1=decode_geometry_builtin_input_cmp(d1bits,shifts,mask,r[28],r[27]);
    GeometryBuiltinInputMemory memory{c,&c};const auto a=reserve_geometry_builtin_input(memory,selector,1,managers[0]+0x160,table,published[0]);
    const auto b=reserve_geometry_builtin_input(memory,selector,2,managers[1]+0x160,table,published[1]);
    const auto d=reserve_geometry_builtin_input(memory,selector,3,managers[1]+0x160,table,published[2]);
    // Prefix survivors: preserve full vectors, including speculative scratch.
    r[3]=a.pointer;r[4]=advance;r[5]=align;r[6]=four;r[7]=d1.shifted;r[8]=q0;r[11]=aligned0;r[12]=mask;r[18]=a.publish_after;r[19]=shifts;r[21]=b1bits;
    r[25]=pack0;r[26]=r[42];r[33]=spu_ls_read128(&c,0xcd60);r[34]=spu_shufb(four,four,iota);r[39]=pack12;r[40]=iota;r[41]=pack4;r[43]=spu_fsmbi(0xf);
    r[44]=a.offset;r[45]=a.ids_after;r[46]=a.ids;r[47]=a.byte_mask;r[49]=b1.shifted;r[54]=spu_il(4);r[55]=d.publish_after;
    r[59]=d.table;r[60]=d.address;r[62]=d.offset;r[63]=d.ids_after;r[64]=spu_il(3);r[66]=d.ids;r[67]=d.byte_mask;r[68]=d.index;r[69]=d.leading;r[70]=d.gather;r[71]=d.equal;
    r[72]=b.publish_after;r[74]=b.publish_before;r[77]=b.table;r[78]=b.address;r[79]=shifts;r[81]=a.publish_before;
    r[85]=d1.converted;r[86]=b.offset;r[87]=b.ids_after;r[88]=d0.converted;r[89]=spu_il(2);r[90]=b.ids;r[91]=b.byte_mask;r[92]=b1.converted;r[93]=b0.converted;
    r[94]=b.index;r[95]=b.leading;r[96]=spu_shufb(spu_andbi(advance,15),spu_andbi(advance,15),r[65]);r[97]=b.gather;r[98]=d1.masked;r[99]=d0.masked;r[100]=b1.masked;r[101]=b.equal;r[102]=b0.masked;r[103]=d0.shifted;r[104]=aligned1;
    r[38]=spu_a(bytes,a.pointer);r[32]=spu_ai(r[38],16);r[20]=spu_a(bytes,b.pointer);r[31]=spu_ai(r[20],16);r[13]=spu_a(bytes,d.pointer);r[16]=spu_ai(r[13],16);
    r[30]=aligned0;r[29]=spu_shufb(aligned1,aligned2,r[34]);r[24]=b0.scaled;r[23]=b1.scaled;r[10]=d0.scaled;r[9]=d1.scaled;
    r[22]=spu_ai(r[84],80);r[51]=spu_a(iota,r[96]);r[50]=spu_ls_read128(&c,advance._u32[0]+32);r[48]=spu_ls_read128(&c,advance._u32[0]+16);r[53]=spu_ls_read128(&c,advance._u32[0]);r[52]=spu_ls_read128(&c,advance._u32[0]+48);
    r[15]=spu_shufb(r[48],r[50],r[51]);r[17]=spu_shufb(r[53],r[48],r[51]);r[14]=spu_shufb(r[50],r[52],r[51]);
}
inline void step_geometry_builtin20_input(spu_context& c,u128* r)
{
    r[76]=spu_selb(r[30],r[33],r[43]);r[73]=spu_shufb(r[15],r[14],r[25]);r[75]=spu_selb(r[29],r[33],r[43]);r[72]=spu_shufb(r[14],r[14],r[41]);
    r[71]=r[22];r[69]=spu_shufb(r[17],r[15],r[39]);r[30]=r[17];r[68]=spu_shufb(r[14],r[14],r[25]);
    const auto b0=decode_geometry_builtin_input_cmp(r[69],r[19],r[12],r[28],r[27]),b1=decode_geometry_builtin_input_cmp(r[68],r[19],r[12],r[28],r[27]);
    const auto d0=decode_geometry_builtin_input_cmp(r[73],r[19],r[12],r[28],r[27]),d1=decode_geometry_builtin_input_cmp(r[72],r[19],r[12],r[28],r[27]);
    const unsigned ends[]={38,32,20,31,13,16},values[]={76,75,24,23,10,9};
    for(unsigned i=0;i<6;++i)spu_ls_write128(&c,r[ends[i]]._u32[0]+r[26]._u32[0],r[values[i]]);
    r[67]=d0.shifted;r[66]=d1.shifted;r[64]=b0.shifted;r[63]=b1.shifted;r[59]=d0.masked;r[58]=d1.masked;r[62]=b0.masked;r[60]=b1.masked;
    // Every fresh source quad follows every data store, also on the last step.
    r[84]=spu_ls_read128(&c,r[22]._u32[0]+32);r[56]=b0.converted;r[83]=spu_ls_read128(&c,r[22]._u32[0]+16);r[35]=b1.converted;r[82]=d0.converted;
    r[36]=spu_ls_read128(&c,r[22]._u32[0]);r[61]=d1.converted;r[37]=spu_ls_read128(&c,r[22]._u32[0]+48);
    r[70]=spu_andbi(r[71],15);r[29]=spu_shufb(r[15],r[14],r[34]);r[26]=spu_ai(r[42],32);r[22]=spu_ai(r[22],40);r[57]=spu_shufb(r[70],r[70],r[65]);
    r[24]=b0.scaled;r[42]=r[26];r[23]=b1.scaled;r[10]=d0.scaled;r[80]=spu_a(r[40],r[57]);r[9]=d1.scaled;
    r[14]=spu_shufb(r[84],r[37],r[80]);r[2]=spu_shufb(r[36],r[83],r[80]);r[15]=spu_shufb(r[83],r[84],r[80]);
}
inline void decode_geometry_builtin20_input(spu_context& c,const GeometryBuiltin20Input& p,buzz_geometry_job_fn finish)
{
    u128 r[128];memcpy(r,c.gpr,sizeof r);open_geometry_builtin20_input(c,r);
    for(unsigned group=0;group<p.groups;++group){step_geometry_builtin20_input(c,r);if(group+1<p.groups)r[17]=r[2];}
    memcpy(c.gpr,r,sizeof r);c.pc=0x9498;g_spu_trampoline_fn=finish;
}
}
