/* Complete A390/A5D0/A5D8 format3: XYZ+3CMP -> four float4 streams. */
#pragma once
#include "native_geometry_builtin_input.h"
namespace buzz {
struct GeometryBuiltin24CmpInput {unsigned groups=0;};
inline bool read_geometry_builtin24_cmp_input(const spu_context& c,GeometryBuiltin24CmpInput& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    unsigned groups=0;GeometryBuiltinInputMemory memory{c};
    if(!read_geometry_builtin_input_metadata(memory,geometry_builtin_input_metadata(c),3,groups,reason))return false;
    out={groups};return true;
}
inline void open_geometry_builtin24_cmp_input(spu_context& c,u128* r)
{
    const auto input=r[4],iota=spu_ls_read128(&c,0xd980),packing=spu_ls_read128(&c,0xda80),shifts=spu_ls_read128(&c,0xd1a0),mask=spu_ls_read128(&c,0xd190);
    const auto selector=r[80],table=r[83],bytes=r[36];const u128 publication[]={r[35],r[37],r[61],r[39]};const unsigned managers[]={r[3]._u32[0],r[82]._u32[0]};
    const auto q0=spu_ls_read128(&c,input._u32[0]),q1=spu_ls_read128(&c,input._u32[0]+16),q2=spu_ls_read128(&c,input._u32[0]+32);
    const auto four=spu_a(iota,spu_shufb(spu_il(4),spu_il(4),r[65])),eight=spu_a(iota,spu_shufb(spu_il(8),spu_il(8),r[65])),twelve=spu_a(iota,spu_shufb(spu_il(12),spu_il(12),r[65]));
    const auto pack0=spu_shufb(iota,iota,packing),pack4=spu_shufb(four,four,packing),pack8=spu_shufb(eight,eight,packing),pack12=spu_shufb(twelve,twelve,packing),raw8=spu_shufb(eight,eight,iota);
    const auto b0bits=spu_shufb(q0,q1,pack12),b1bits=spu_shufb(q2,q2,pack4),d0bits=spu_shufb(q1,q2,pack0),d1bits=spu_shufb(q2,q2,pack8),e0bits=spu_shufb(q1,q2,pack4),e1bits=spu_shufb(q2,q2,pack12);
    const auto b0=decode_geometry_builtin_input_cmp(b0bits,shifts,mask,r[28],r[27]),b1=decode_geometry_builtin_input_cmp(b1bits,shifts,mask,r[28],r[27]);
    const auto d0=decode_geometry_builtin_input_cmp(d0bits,shifts,mask,r[28],r[27]),d1=decode_geometry_builtin_input_cmp(d1bits,shifts,mask,r[28],r[27]);
    const auto e0=decode_geometry_builtin_input_cmp(e0bits,shifts,mask,r[28],r[27]),e1=decode_geometry_builtin_input_cmp(e1bits,shifts,mask,r[28],r[27]);
    GeometryBuiltinInputMemory memory{c,&c};const auto a=reserve_geometry_builtin_input(memory,selector,1,managers[0]+0x160,table,publication[0]);
    const auto b=reserve_geometry_builtin_input(memory,selector,2,managers[0]+0x160,table,publication[1]);
    const auto d=reserve_geometry_builtin_input(memory,selector,3,managers[1]+0x160,table,publication[2]);
    const auto e=reserve_geometry_builtin_input(memory,selector,4,managers[1]+0x160,table,publication[3]);
    r[4]=b1.converted;r[7]=e1bits;r[8]=q2;r[12]=mask;r[13]=shifts;r[25]=e.word_mask;r[26]=r[42];r[30]=pack12;r[31]=pack4;r[37]=spu_ls_read128(&c,0xcd60);r[41]=raw8;r[43]=pack8;r[44]=pack0;r[45]=spu_fsmbi(0xf);
    r[47]=b.word_mask;r[48]=d0.converted;r[50]=b.offset;r[51]=b.ids_after;r[52]=b0.converted;r[53]=spu_il(2);r[54]=b.ids;r[55]=b.byte_mask;r[56]=b.leading;r[57]=b.gather;r[58]=d1.masked;r[59]=b1.masked;r[60]=b0.masked;
    r[62]=b.equal;r[63]=e1.shifted;r[64]=e0.shifted;r[65]=e.publish_before;r[66]=b0.shifted;r[67]=d1.shifted;r[68]=b1.shifted;r[69]=b0bits;r[70]=a.publish_after;r[71]=d1bits;r[72]=b1bits;r[73]=a.publish_before;r[74]=a.word_mask;r[75]=spu_ai(input,48);r[76]=e.publish_after;
    r[77]=e.pointer;r[78]=e.table;r[79]=e.address;r[84]=b.index;r[85]=e.offset;r[86]=e.ids_after;r[87]=spu_il(4);r[88]=e.ids;r[89]=e.byte_mask;r[90]=e.index;r[91]=e.leading;r[92]=e.gather;r[93]=e.equal;
    r[40]=spu_a(bytes,a.pointer);r[35]=spu_ai(r[40],16);r[38]=spu_a(bytes,b.pointer);r[34]=spu_ai(r[38],16);r[23]=spu_a(bytes,d.pointer);r[24]=spu_ai(r[23],16);r[16]=spu_a(bytes,e.pointer);r[20]=spu_ai(r[16],16);
    r[22]=q0;r[33]=spu_shufb(q1,q2,raw8);r[32]=b0.scaled;r[21]=b1.scaled;r[10]=d0.scaled;r[19]=d1.scaled;r[9]=e0.scaled;r[18]=e1.scaled;
    r[29]=spu_ai(input,96);r[17]=spu_ls_read128(&c,r[75]._u32[0]);r[11]=spu_ls_read128(&c,r[75]._u32[0]+32);r[14]=spu_ls_read128(&c,r[75]._u32[0]+16);
}
inline void step_geometry_builtin24_cmp_input(spu_context& c,u128* r)
{
    r[6]=spu_selb(r[22],r[37],r[45]);r[5]=spu_shufb(r[14],r[11],r[31]);r[2]=spu_selb(r[33],r[37],r[45]);r[15]=spu_shufb(r[11],r[11],r[30]);r[81]=spu_shufb(r[14],r[11],r[44]);r[3]=spu_shufb(r[17],r[14],r[30]);r[46]=spu_shufb(r[11],r[11],r[31]);r[49]=spu_shufb(r[11],r[11],r[43]);
    const auto b0=decode_geometry_builtin_input_cmp(r[3],r[13],r[12],r[28],r[27]),b1=decode_geometry_builtin_input_cmp(r[46],r[13],r[12],r[28],r[27]);
    const auto d0=decode_geometry_builtin_input_cmp(r[81],r[13],r[12],r[28],r[27]),d1=decode_geometry_builtin_input_cmp(r[49],r[13],r[12],r[28],r[27]);
    const auto e0=decode_geometry_builtin_input_cmp(r[5],r[13],r[12],r[28],r[27]),e1=decode_geometry_builtin_input_cmp(r[15],r[13],r[12],r[28],r[27]);
    const unsigned ends[]={40,35,38,34,23,24,16,20},values[]={6,2,32,21,10,19,9,18};
    for(unsigned i=0;i<8;++i)spu_ls_write128(&c,r[ends[i]]._u32[0]+r[26]._u32[0],r[values[i]]);
    r[100]=e0.shifted;r[99]=e1.shifted;r[102]=d0.shifted;r[104]=b0.shifted;r[103]=b1.shifted;r[101]=d1.shifted;r[96]=d0.masked;r[98]=b0.masked;r[97]=b1.masked;r[95]=d1.masked;r[94]=e0.masked;r[36]=e1.masked;
    r[33]=spu_shufb(r[14],r[11],r[41]);r[80]=b0.converted;r[15]=spu_ls_read128(&c,r[29]._u32[0]);r[22]=e1.converted;r[14]=spu_ls_read128(&c,r[29]._u32[0]+16);r[83]=b1.converted;r[11]=spu_ls_read128(&c,r[29]._u32[0]+32);r[82]=d0.converted;r[61]=d1.converted;r[39]=e0.converted;
    r[26]=spu_ai(r[42],32);r[32]=b0.scaled;r[18]=e1.scaled;r[42]=r[26];r[21]=b1.scaled;r[22]=r[17];r[10]=d0.scaled;r[19]=d1.scaled;r[9]=e0.scaled;r[29]=spu_ai(r[29],48);
}
inline void decode_geometry_builtin24_cmp_input(spu_context& c,const GeometryBuiltin24CmpInput& p,buzz_geometry_job_fn finish)
{
    u128 r[128];memcpy(r,c.gpr,sizeof r);open_geometry_builtin24_cmp_input(c,r);
    for(unsigned group=0;group<p.groups;++group){step_geometry_builtin24_cmp_input(c,r);if(group+1<p.groups)r[17]=r[15];}
    memcpy(c.gpr,r,sizeof r);c.pc=0x9498;g_spu_trampoline_fn=finish;
}
}
