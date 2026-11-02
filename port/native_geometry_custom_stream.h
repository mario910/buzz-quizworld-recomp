/* Complete 93F0 custom-descriptor dispatch through its 7DF8 tail handoff.
 * Descriptor iteration/data retain their independent native switches. */
#pragma once
#include "native_geometry_attributes.h"
namespace buzz {
struct GeometryCustomStream { uint32_t context=0,stack=0,vertices=0,format=0; };
inline bool read_geometry_custom_stream(const spu_context& ctx,GeometryCustomStream& output)
{
    GeometryCustomStream p;p.context=ctx.gpr[3]._u32[0];p.stack=ctx.gpr[1]._u32[0];p.format=ctx.gpr[6]._u32[0]&255;
    if(ctx.ls!=ctx.ls_store || p.context<0xe680 || !geometry_output_range(p.context,0x170) ||
       (p.stack&15) || p.stack<0xe840 || p.stack>SPU_LS_SIZE-32 ||
       geometry_output_overlap(p.context,0x170,p.stack-448,480))return false;
    p.vertices=geometry_job_be16(ctx.ls+p.context+8);
    if(!p.vertices || p.format<6 || ctx.gpr[8]._u32[0] || !ctx.gpr[7]._u32[0])return false;
    output=p;return true;
}
inline void dispatch_geometry_custom_stream(spu_context& ctx,const GeometryCustomStream& p,buzz_geometry_job_fn descriptors)
{
    auto& r=ctx.gpr;auto get=[&](uint32_t at){return spu_ls_read128(&ctx,at);};
    auto put=[&](uint32_t at,u128 value){spu_ls_write128(&ctx,at,value);};
    // All spills survive in LS even though the following descriptor frame can
    // overwrite them later. Preserve the complete boundary before that tail.
    r[12]=r[8];r[2]=get(p.context);r[13]=r[7];put(p.stack-0x1a0,r[126]);
    r[9]=spu_ila(0x9408);r[126]=spu_link(0x9408);
    for(unsigned reg=80;reg<=104;++reg)put(p.stack-(reg-79)*16,r[reg]);
    put(p.stack+16,r[0]);put(p.stack-448,r[1]);
    r[34]=r[5];r[82]=r[3];r[84]=r[4];r[126]=spu_sf(r[9],r[126]);r[8]=spu_rotqbyi(r[2],6);r[1]=spu_ai(r[1],-448);
    r[10]=spu_andi(r[6],255);r[28]=get(0xd1b0);r[21]=spu_ila(0x95c4);r[27]=get(0xd1c0);
    r[18]=spu_shli(r[10],2);r[49]=spu_fsmbi(0);r[15]=spu_a(r[21],r[126]);r[38]=spu_fsmbi(0xf);
    r[11]=spu_ila(65535);r[87]=spu_fsmbi(0xff);r[65]=spu_ilh(0x303);r[63]=spu_fsmbi(0xf0f);
    r[17]=spu_a(r[18],r[15]);r[16]=get(r[15]._u32[0]+r[18]._u32[0]);r[11]=spu_and(r[8],r[11]);
    r[80]=spu_shufb(r[49],r[65],r[65]);r[25]=spu_ilhu(0x3800);r[20]=spu_ai(r[11],1);r[24]=spu_ilhu(0x3780);
    r[19]=spu_andi(r[20],-2);r[72]=spu_ilhu(0x4635);r[14]=spu_rotqby(r[16],r[17]);r[36]=spu_shli(r[19],4);r[71]=spu_ilhu(0xc635);
    r[6]=spu_ila(0xddf0);r[9]=spu_a(r[14],r[15]);r[7]=spu_clgti(r[10],5);
    r[25]=spu_ori(r[25],128);r[24]=spu_ori(r[24],128);r[72]=spu_iohl(r[72],0x2e7c);r[71]=spu_iohl(r[71],0x3150);
    r[42]=spu_sfi(r[36],0);r[8]=spu_a(r[6],r[126]);r[35]=spu_ai(r[3],92);r[37]=spu_ai(r[3],96);
    r[61]=spu_ai(r[3],100);r[39]=spu_ai(r[3],104);r[83]=spu_ai(r[3],16);
    r[1]=spu_ai(r[1],448);r[6]=r[13];r[7]=r[11];r[8]=spu_il(0);
    r[126]=get(p.stack-0x1a0);r[9]=spu_il(0);r[0]=get(p.stack+16);
    for(unsigned reg=80;reg<=104;++reg)r[reg]=get(p.stack-(reg-79)*16);
    ctx.pc=0x7df8;g_spu_trampoline_fn=descriptors;
}
} // namespace buzz
