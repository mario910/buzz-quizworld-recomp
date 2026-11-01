/* Complete built-in output format 0: four float4 rows -> 48 packed bytes.
 * Every group prefetches its successor before ordered stores, including the
 * last group. Raw bits and all scratch vectors remain exact at 5648. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
namespace buzz {
struct GeometryBuiltinFloat3Output { unsigned vertices=0,groups=0; };
inline bool read_geometry_builtin_float3_output(const spu_context& c,GeometryBuiltinFloat3Output& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    const unsigned vertices=c.gpr[10]._u32[0]&65535u,groups=(vertices+3)/4;
    // The original do-loop underflows its counter for a zero-count direct
    // entry. The enclosing controller normally skips that region entirely.
    if(!groups)return reject("zero-count builtin root");
    for(unsigned group=0;group<groups;++group)for(unsigned offset=0;offset<48;offset+=16)
        if(((c.gpr[3]._u32[0]+group*48+offset)&0x3fff0)<0xe680)return reject("builtin float3 protected write");
    out={vertices,groups};return true;
}
inline void pack_geometry_builtin_float3(u128* r)
{
    r[13]=spu_shufb(r[26],r[27],r[16]);r[12]=spu_shufb(r[27],r[29],r[14]);r[8]=spu_shufb(r[28],r[26],r[15]);
}
inline void emit_geometry_builtin_float3_output(spu_context& c,const GeometryBuiltinFloat3Output& plan,buzz_geometry_job_fn finish)
{
    auto* r=c.gpr;
    r[35]=spu_ila(65535);r[34]=spu_ls_read128(&c,r[81]._u32[0]+0x50);r[11]=r[3];r[32]=spu_and(r[10],r[35]);
    r[15]=spu_ls_read128(&c,0xd970);r[16]=spu_ls_read128(&c,0xd9f0);r[30]=spu_ai(r[32],3);r[14]=spu_ls_read128(&c,0xda50);
    r[10]=spu_rotmi(r[30],-2);r[31]=spu_rotqbyi(r[34],12);r[9]=spu_ai(r[31],64);
    r[26]=spu_ls_read128(&c,r[31]._u32[0]+16);r[27]=spu_ls_read128(&c,r[31]._u32[0]+32);
    r[29]=spu_ls_read128(&c,r[31]._u32[0]+48);r[28]=spu_ls_read128(&c,r[31]._u32[0]);pack_geometry_builtin_float3(r);
    for(unsigned group=0;group<plan.groups;++group){
        r[41]=spu_ai(r[9],64);r[38]=spu_ls_read128(&c,r[9]._u32[0]+16);r[36]=spu_ai(r[11],48);
        r[39]=spu_ls_read128(&c,r[9]._u32[0]+32);r[10]=spu_ai(r[10],-1);
        r[40]=spu_ls_read128(&c,r[9]._u32[0]+48);r[37]=spu_ls_read128(&c,r[9]._u32[0]);r[9]=r[41];
        spu_ls_write128(&c,r[11]._u32[0]+32,r[12]);spu_ls_write128(&c,r[11]._u32[0],r[8]);spu_ls_write128(&c,r[11]._u32[0]+16,r[13]);
        r[11]=r[36];r[13]=spu_shufb(r[38],r[39],r[16]);r[12]=spu_shufb(r[39],r[40],r[14]);r[8]=spu_shufb(r[37],r[38],r[15]);
    }
    c.pc=0x5648;g_spu_trampoline_fn=finish;
}
}
