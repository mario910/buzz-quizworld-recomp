/* Complete 9F8 skin/rigid controller. AF18 and the final 2C00 are queued
 * boundaries; their data work is accounted for by their own implementations. */
#pragma once
#include "native_geometry_commands.h"

namespace buzz {
struct GeometrySkinDispatch { uint32_t stack=0; bool weighted=false; };
inline bool read_geometry_skin_dispatch(const spu_context& ctx,GeometrySkinDispatch& output)
{
    GeometrySkinDispatch p;p.stack=ctx.gpr[1]._u32[0];
    const uint32_t manager=ctx.gpr[3]._u32[0];
    if(ctx.ls!=ctx.ls_store || manager<0xe680 || !geometry_output_range(manager,0x170) ||
       (p.stack&15) || p.stack<0xe840 || p.stack>SPU_LS_SIZE-32 ||
       geometry_output_overlap(manager,0x170,p.stack-160,192))return false;
    // BRHZ observes the sign-extended preferred BYTE, not the entire word.
    p.weighted=(ctx.gpr[9]._u32[0]&255)==0;output=p;return true;
}

inline void dispatch_geometry_skin(spu_context& ctx,const GeometrySkinDispatch& p,
                                   const buzz_geometry_skin_dispatch_functions& f)
{
    auto& r=ctx.gpr;
    auto get=[&](uint32_t at){return spu_ls_read128(&ctx,at);};
    auto put=[&](uint32_t at,u128 value){spu_ls_write128(&ctx,at,value);};
    for(unsigned reg=80;reg<=87;++reg)put(p.stack-(reg-79)*16,r[reg]);
    put(p.stack+16,r[0]);put(p.stack-160,r[1]);
    r[11]=spu_ceqbi(r[9],0);r[2]=r[6];r[9]=spu_xsbh(r[11]);
    r[6]=r[5];r[5]=r[2];r[84]=r[7];r[87]=spu_ila(0xffff);
    r[86]=r[8];r[85]=r[10];r[81]=r[3];r[1]=spu_ai(r[1],-160);
    auto restore=[&]{
        r[0]=get(r[1]._u32[0]+0xb0);r[1]=spu_ai(r[1],160);
        for(unsigned reg=80;reg<=87;++reg)r[reg]=get(r[1]._u32[0]-(reg-79)*16);
    };
    if(p.weighted){restore();ctx.pc=0xaf18;g_spu_trampoline_fn=f.weighted;return;}

    ctx.pc=0xa84;
    geometry_command_call(ctx,f.position_pointer,0xa88);r[80]=r[3];r[3]=r[81];
    geometry_command_call(ctx,f.normal_pointer,0xa98);r[82]=r[3];r[3]=r[81];
    geometry_command_call(ctx,f.tangent_pointer,0xaa4);r[83]=r[3];
    r[13]=get(r[81]._u32[0]);r[4]=r[80];r[5]=r[80];r[6]=r[84];
    r[12]=spu_rotqbyi(r[13],6);r[3]=spu_and(r[87],r[12]);
    geometry_command_call(ctx,f.transform,0xad0);
    r[4]=spu_ceqi(r[82],0);r[5]=r[82];r[10]=spu_xori(r[85],1);r[6]=r[86];
    r[8]=spu_nor(r[4],r[4]);r[4]=r[82];r[80]=spu_andi(r[10],255);
    r[7]=spu_sfi(r[8],0);r[3]=spu_and(r[7],r[80]);
    if(r[3]._u32[0]){
        ctx.pc=0xb9c;r[15]=get(r[81]._u32[0]);r[14]=spu_rotqbyi(r[15],6);r[3]=spu_and(r[87],r[14]);
        geometry_command_call(ctx,f.transform,0xbb0);r[3]=r[82];
        r[6]=get(r[81]._u32[0]);r[5]=spu_rotqbyi(r[6],6);r[4]=spu_and(r[87],r[5]);
        geometry_command_call(ctx,f.normalize,0xbc8);
    }
    r[19]=spu_ceqi(r[83],0);r[4]=r[83];r[6]=r[86];r[5]=r[83];
    r[18]=spu_nor(r[19],r[19]);r[17]=spu_sfi(r[18],0);r[16]=spu_and(r[17],r[80]);
    if(!r[16]._u32[0]){restore();SPU_RET(&ctx);}

    r[23]=get(r[81]._u32[0]);r[22]=spu_rotqbyi(r[23],6);r[3]=spu_and(r[87],r[22]);
    ctx.pc=0xb54;geometry_command_call(ctx,f.transform,0xb58);r[3]=r[83];
    r[21]=get(r[81]._u32[0]);r[0]=get(r[1]._u32[0]+0xb0);r[1]=spu_ai(r[1],160);
    r[80]=get(r[1]._u32[0]-0x10);r[81]=get(r[1]._u32[0]-0x20);r[20]=spu_rotqbyi(r[21],6);
    r[82]=get(r[1]._u32[0]-0x30);r[83]=get(r[1]._u32[0]-0x40);r[84]=get(r[1]._u32[0]-0x50);
    r[4]=spu_and(r[87],r[20]);
    for(unsigned reg=85;reg<=87;++reg)r[reg]=get(r[1]._u32[0]-(reg-79)*16);
    ctx.pc=0x2c00;g_spu_trampoline_fn=f.normalize;
}
} // namespace buzz
