/* Complete format2 A6A8/A750/A860/A864/A868: XYZ+CMP+signed16x4,
 * 24-byte vertices -> three float4 streams, exact queued9498 frontier. */
#pragma once
#include "native_geometry_builtin_input.h"
namespace buzz {
struct GeometryBuiltin24Input {unsigned groups=0;};
struct GeometryBuiltinInputSnorm {u128 converted,scaled;};
inline GeometryBuiltinInputSnorm decode_geometry_builtin24_snorm_input(u128 bits,u128 scale,u128 bias)
{GeometryBuiltinInputSnorm p;p.converted=spu_csflt(bits,139);p.scaled=spu_fma(p.converted,scale,bias);return p;}
inline bool read_geometry_builtin24_input(const spu_context& c,GeometryBuiltin24Input& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    const unsigned bytes=0u-c.gpr[42]._u32[0];if(!bytes || (bytes&31) || bytes>1048576)return reject("builtin24 input counter");
    GeometryBuiltinInputMemory memory{c};const auto selector=c.gpr[80],table=c.gpr[83];
    const auto a=reserve_geometry_builtin_input(memory,selector,1,c.gpr[3]._u32[0]+0x160,table,c.gpr[35]);
    const auto b=reserve_geometry_builtin_input(memory,selector,2,c.gpr[3]._u32[0]+0x160,table,c.gpr[37],c.gpr[82]._u32[0]+0x160);
    const auto d=reserve_geometry_builtin_input(memory,selector,3,c.gpr[82]._u32[0]+0x160,table,c.gpr[61]);
    if(!memory.allowed)return reject("builtin24 input protected metadata write");
    const unsigned groups=bytes/32,offset=c.gpr[36]._u32[0]+c.gpr[42]._u32[0];const u128 pointers[]={a.pointer,b.pointer,d.pointer};
    for(auto pointer:pointers)for(unsigned group=0;group<groups;++group)for(unsigned row=0;row<2;++row)
        if(((pointer._u32[0]+offset+group*32+row*16)&0x3fff0)<0xe680)return reject("builtin24 input protected data write");
    out={groups};return true;
}
inline void open_geometry_builtin24_input(spu_context& c,u128* r)
{
    const auto input=r[4],iota=spu_ls_read128(&c,0xd980),packing=spu_ls_read128(&c,0xda80),halves=spu_ls_read128(&c,0xd910);
    const auto shifts=spu_ls_read128(&c,0xd1a0),mask=spu_ls_read128(&c,0xd190),selector=r[80],table=r[83],bytes=r[36];
    const u128 publication[]={r[35],r[37],r[61]};const unsigned managers[]={r[3]._u32[0],r[82]._u32[0]};
    const auto q0=spu_ls_read128(&c,input._u32[0]),q1=spu_ls_read128(&c,input._u32[0]+16),q2=spu_ls_read128(&c,input._u32[0]+32);
    const auto rep12=spu_shufb(spu_il(12),spu_il(12),r[65]),rep4=spu_shufb(spu_il(4),spu_il(4),r[65]),rep8=spu_shufb(spu_il(8),spu_il(8),r[65]);
    const auto twelve=spu_a(iota,rep12),four=spu_a(iota,rep4),eight=spu_a(iota,rep8);
    const auto cmp12=spu_shufb(twelve,twelve,packing),cmp4=spu_shufb(four,four,packing),half0=spu_shufb(iota,iota,halves),half8=spu_shufb(eight,eight,halves),raw8=spu_shufb(eight,eight,iota);
    const auto b0bits=spu_shufb(q0,q1,cmp12),b1bits=spu_shufb(q2,q2,cmp4),d0bits=spu_shufb(q1,q2,half0),d1bits=spu_shufb(q2,q2,half8);
    const auto b0=decode_geometry_builtin_input_cmp(b0bits,shifts,mask,r[28],r[27]),b1=decode_geometry_builtin_input_cmp(b1bits,shifts,mask,r[28],r[27]);
    const auto d0=decode_geometry_builtin24_snorm_input(d0bits,r[25],r[24]),d1=decode_geometry_builtin24_snorm_input(d1bits,r[25],r[24]);
    GeometryBuiltinInputMemory memory{c,&c};const auto a=reserve_geometry_builtin_input(memory,selector,1,managers[0]+0x160,table,publication[0]);
    const auto b=reserve_geometry_builtin_input(memory,selector,2,managers[0]+0x160,table,publication[1],managers[1]+0x160);
    const auto d=reserve_geometry_builtin_input(memory,selector,3,managers[1]+0x160,table,publication[2]);
    r[2]=b.index;r[3]=b.leading;r[4]=b1.masked;r[5]=b0.masked;r[6]=d1.converted;r[10]=shifts;r[11]=mask;
    r[23]=iota;r[26]=r[42];r[31]=spu_ls_read128(&c,0xcd60);r[32]=raw8;r[34]=half8;r[38]=cmp4;r[39]=cmp12;r[40]=half0;r[41]=spu_fsmbi(0xf);
    r[43]=a.pointer;r[44]=b1.shifted;r[45]=d1bits;r[46]=q1;r[47]=q2;r[48]=a.publish_after;r[49]=spu_il(2);r[51]=a.publish_before;r[52]=a.word_mask;r[53]=b0bits;r[54]=a.table;r[55]=a.address;r[56]=a.ids_after;
    r[57]=rep4;r[58]=spu_il(8);r[59]=spu_il(1);r[60]=a.ids;r[62]=rep12;r[63]=spu_il(4);r[64]=a.leading;r[65]=spu_ai(input,48);r[66]=spu_il(12);
    r[77]=d.publish_after;r[78]=d.publish_before;r[79]=d.pointer;r[81]=b1.converted;r[84]=a.offset;r[85]=d.word_mask;r[86]=d.table;r[87]=d.address;r[88]=d.offset;r[89]=d.ids_after;r[90]=spu_il(3);r[91]=d.ids;r[92]=d.byte_mask;r[93]=d.index;r[94]=d.leading;r[95]=d.gather;r[96]=d.equal;
    r[97]=b.publish_after;r[98]=spu_a(bytes,b.pointer);r[99]=b.publish_before;r[100]=b.pointer;r[101]=b.word_mask;r[102]=b.table;r[103]=b.address;r[104]=b.offset;
    r[33]=spu_a(bytes,a.pointer);r[30]=spu_ai(r[33],16);r[22]=r[98];r[29]=spu_ai(r[22],16);r[13]=spu_a(bytes,d.pointer);r[16]=spu_ai(r[13],16);
    r[21]=q0;r[20]=spu_shufb(q1,q2,raw8);r[19]=b0.scaled;r[18]=b1.scaled;r[8]=d0.scaled;r[7]=d1.scaled;
    r[14]=spu_ai(input,96);r[15]=spu_ls_read128(&c,r[65]._u32[0]);r[9]=spu_ls_read128(&c,r[65]._u32[0]+32);r[12]=spu_ls_read128(&c,r[65]._u32[0]+16);
}
inline void step_geometry_builtin24_input(spu_context& c,u128* r)
{
    r[74]=spu_selb(r[21],r[31],r[41]);r[72]=spu_shufb(r[15],r[12],r[39]);r[73]=spu_selb(r[20],r[31],r[41]);r[71]=spu_shufb(r[9],r[9],r[38]);r[70]=spu_ai(r[42],32);r[21]=r[15];
    r[36]=spu_shufb(r[12],r[9],r[40]);r[35]=spu_shufb(r[9],r[9],r[34]);
    const auto b0=decode_geometry_builtin_input_cmp(r[72],r[10],r[11],r[28],r[27]),b1=decode_geometry_builtin_input_cmp(r[71],r[10],r[11],r[28],r[27]);
    const auto d0=decode_geometry_builtin24_snorm_input(r[36],r[25],r[24]),d1=decode_geometry_builtin24_snorm_input(r[35],r[25],r[24]);
    const unsigned ends[]={33,30,22,29,13,16},values[]={74,73,19,18,8,7};
    for(unsigned i=0;i<6;++i)spu_ls_write128(&c,r[ends[i]]._u32[0]+r[26]._u32[0],r[values[i]]);
    r[69]=b0.shifted;r[68]=b1.shifted;r[42]=r[70];r[82]=d0.converted;r[67]=b0.masked;r[37]=b1.masked;r[61]=d1.converted;r[80]=b0.converted;r[20]=spu_shufb(r[12],r[9],r[32]);r[83]=b1.converted;
    // The next physical input span is loaded only after all six stores.
    r[17]=spu_ls_read128(&c,r[14]._u32[0]);r[26]=r[70];r[9]=spu_ls_read128(&c,r[14]._u32[0]+32);r[8]=d0.scaled;r[12]=spu_ls_read128(&c,r[14]._u32[0]+16);r[14]=spu_ai(r[14],48);
    r[7]=d1.scaled;r[19]=b0.scaled;r[18]=b1.scaled;
}
inline void decode_geometry_builtin24_input(spu_context& c,const GeometryBuiltin24Input& p,buzz_geometry_job_fn finish)
{
    u128 r[128];memcpy(r,c.gpr,sizeof r);open_geometry_builtin24_input(c,r);
    for(unsigned group=0;group<p.groups;++group){step_geometry_builtin24_input(c,r);if(group+1<p.groups)r[15]=r[17];}
    memcpy(c.gpr,r,sizeof r);c.pc=0x9498;g_spu_trampoline_fn=finish;
}
}
