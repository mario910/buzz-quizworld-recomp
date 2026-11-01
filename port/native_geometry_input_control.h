/* Complete standard 93F0 input control. Admission overlays actual frame
 * spills before metadata; execution reloads actual late saved registers. */
#pragma once
#include "native_geometry_attributes.h"
#include "native_geometry_builtin20_input.h"
#include "native_geometry_builtin24_input.h"
#include "native_geometry_builtin24_cmp_input.h"
#include "native_geometry_builtin12_input.h"
#include "native_geometry_builtin15_input.h"
namespace buzz {
struct GeometryInputControl {unsigned format=0,groups=0;bool empty=false;GeometryRawStream raw;};
inline void spill_geometry_input_frame(GeometryBuiltinInputMemory& memory,const u128* r)
{
    const unsigned sp=r[1]._u32[0];memory.store(sp-416,r[126]);memory.store(sp-48,r[82]);memory.store(sp-80,r[84]);memory.store(sp+16,r[0]);
    memory.store(sp-16,r[80]);memory.store(sp-32,r[81]);memory.store(sp-64,r[83]);
    for(unsigned reg=85;reg<=104;++reg)memory.store(sp-(reg-79)*16,r[reg]);
    memory.store(sp-448,r[1]);
}
inline bool read_geometry_input_control(const spu_context& c,GeometryInputControl& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    // Count is cached before the frame can overwrite its manager/config quad.
    const auto config=spu_ls_read128(&c,c.gpr[3]._u32[0]);const unsigned count=spu_rotqbyi(config,6)._u16[0];
    GeometryInputControl p;p.empty=!count;p.format=c.gpr[6]._u32[0]&255;
    GeometryBuiltinInputMemory memory{c};spill_geometry_input_frame(memory,c.gpr);
    if(!memory.allowed)return reject("input control protected frame write");
    if(p.empty){out=p;return true;}
    if(p.format>5)return reject("custom input dispatch retained");
    static constexpr unsigned targets[]={0x13c,0x1334,0x10e4,0xdcc,0x8ac,0x234};
    const auto slot=spu_ila(0x95c4+p.format*4),word=spu_rotqby(spu_ls_read128(&c,slot._u32[0]),slot);
    if(word._u32[0]!=targets[p.format])return reject("input control dispatch target");
    if(!p.format){if(!read_geometry_raw_stream(c,p.raw))return reject("raw input contract");out=p;return true;}
    const auto manager=c.gpr[3],bytes=spu_il(((count+1)&~1u)*16);
    const GeometryBuiltinInputMetadata metadata{manager,manager,spu_il(0),spu_ai(manager,16),{spu_ai(manager,92),spu_ai(manager,96),spu_ai(manager,100),spu_ai(manager,104)},bytes,spu_sfi(bytes,0),spu_il(0)};
    if(!read_geometry_builtin_input_metadata(memory,metadata,p.format,p.groups,reason))return false;
    out=p;return true;
}
inline void open_geometry_input_frame(spu_context& c)
{
    auto& r=c.gpr;const auto config=spu_ls_read128(&c,r[3]._u32[0]);GeometryBuiltinInputMemory memory{c,&c};spill_geometry_input_frame(memory,r);
    r[12]=r[8];r[2]=config;r[13]=r[7];r[9]=spu_ila(0x9408);r[126]=spu_sf(r[9],spu_link(0x9408));
    r[34]=r[5];r[82]=r[3];r[84]=r[4];r[8]=spu_rotqbyi(config,6);r[1]=spu_ai(r[1],-448);
}
inline void prepare_geometry_input_dispatch(spu_context& c)
{
    auto& r=c.gpr;r[10]=spu_andi(r[6],255);r[28]=spu_ls_read128(&c,0xd1b0);r[21]=spu_ila(0x95c4);r[27]=spu_ls_read128(&c,0xd1c0);
    r[18]=spu_shli(r[10],2);r[49]=spu_il(0);r[15]=spu_a(r[21],r[126]);r[38]=spu_fsmbi(0xf);r[11]=spu_ila(65535);r[87]=spu_fsmbi(0xff);r[65]=spu_ilh(0x303);r[63]=spu_fsmbi(0xf0f);
    r[17]=spu_a(r[18],r[15]);r[16]=spu_ls_read128(&c,r[15]._u32[0]+r[18]._u32[0]);r[11]=spu_and(r[8],r[11]);r[80]=spu_shufb(r[49],r[65],r[65]);
    r[25]=spu_ilhu(0x3800);r[20]=spu_ai(r[11],1);r[24]=spu_ilhu(0x3780);r[19]=spu_andi(r[20],-2);r[72]=spu_ilhu(0x4635);r[14]=spu_rotqby(r[16],r[17]);r[36]=spu_shli(r[19],4);r[71]=spu_ilhu(0xc635);
    r[6]=spu_ila(0xddf0);r[9]=spu_a(r[14],r[15]);r[7]=spu_clgti(r[10],5);r[25]=spu_ori(r[25],128);r[24]=spu_ori(r[24],128);r[72]=spu_iohl(r[72],0x2e7c);r[71]=spu_iohl(r[71],0x3150);
    r[42]=spu_sfi(r[36],0);r[8]=spu_a(r[6],r[126]);r[35]=spu_ai(r[3],92);r[37]=spu_ai(r[3],96);r[61]=spu_ai(r[3],100);r[39]=spu_ai(r[3],104);r[83]=spu_ai(r[3],16);
    c.pc=r[9]._u32[0];g_spu_trampoline_fn=spu_indirect_branch;
}
inline void finish_geometry_input_control(spu_context* c)
{
    auto& r=c->gpr;r[1]=spu_ai(r[1],448);r[0]=spu_ls_read128(c,r[1]._u32[0]+16);
    for(unsigned reg=80;reg<=104;++reg)r[reg]=spu_ls_read128(c,r[1]._u32[0]-(reg-79)*16);
    r[126]=spu_ls_read128(c,r[1]._u32[0]-416);SPU_RET(c);
}
inline void execute_geometry_input_control(spu_context& c,const GeometryInputControl& p)
{
    g_spu_trampoline_fn=nullptr;
    if(!p.empty && !p.format){decode_geometry_raw_stream(c,p.raw);SPU_RET(&c);}
    open_geometry_input_frame(c);
    if(!p.empty){
        prepare_geometry_input_dispatch(c);
        switch(p.format){
        case 1:decode_geometry_builtin20_input(c,{p.groups},finish_geometry_input_control);break;
        case 2:decode_geometry_builtin24_input(c,{p.groups},finish_geometry_input_control);break;
        case 3:decode_geometry_builtin24_cmp_input(c,{p.groups},finish_geometry_input_control);break;
        case 4:decode_geometry_builtin12_input(c,{p.groups},finish_geometry_input_control);break;
        case 5:decode_geometry_builtin15_input(c,{p.groups},finish_geometry_input_control);break;
        }
    }
    // A queued function would be cleared by the scheduler before the tail.
    g_spu_trampoline_fn=nullptr;finish_geometry_input_control(&c);
}
}
