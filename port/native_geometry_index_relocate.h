/* Complete 79A8/79C8 command relocation and triangle-entry ABI. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
#include "native_geometry_shuffle.h"
namespace buzz {
struct GeometryIndexRelocate { unsigned iterations=0; };
inline bool read_geometry_index_relocate(const spu_context& c,GeometryIndexRelocate& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    const uint64_t groups=(uint64_t(c.gpr[87]._u32[0])+15)/16;
    const uint64_t bytes=(groups?groups:1)*16;
    const unsigned output=c.gpr[81]._u32[0];
    if(output>=SPU_LS_SIZE || (output&~15u)<0xe680 || (output&~15u)+bytes>SPU_LS_SIZE)
        return reject("relocation output span");
    out.iterations=unsigned(groups+1);return true;
}
inline void relocate_geometry_index_commands(spu_context& c,const GeometryIndexRelocate& p,buzz_geometry_job_fn triangles)
{
    auto& r=c.gpr;
    const u128 zero{},step=spu_il(16),low=spu_andi(r[80],15);
    const u128 offset=spu_shufb(low,low,r[85]),control=spu_a(offset,r[83]);
    const auto shuffle=buzz_geometry_shuffle_prepare(control);
    u128 input=r[80],output=r[81],next0=zero,next1=zero,stored=zero;
    // The zero warmup store and repeated first destination are intentional.
    // Each store precedes BOTH next reads, preserving overlap feedback.
    for(unsigned i=0;i<p.iterations;++i){
        input=spu_a(input,step);
        stored=i?buzz_geometry_shuffle_apply(next0,next1,shuffle):zero;
        spu_ls_write128(&c,output._u32[0],stored);
        if(i)output=spu_a(output,step);
        next1=spu_ls_read128(&c,input._u32[0]+16);
        next0=spu_ls_read128(&c,input._u32[0]);
    }
    r[17]=input;r[16]=stored;r[43]=low;r[80]=input;r[73]=output;
    r[72]=spu_clgt(r[87],spu_il(int((p.iterations-1)*16)));
    r[46]=offset;r[6]=output;r[9]=next1;
    // Reconstruct the full triangle-entry ABI after every relocation write.
    r[45]=spu_rotmi(r[103],-3);r[21]=zero;r[18]=r[92];r[92]=spu_ila(0xd690);
    r[35]=spu_ls_read128(&c,0xd160);r[87]=spu_ila(0xd790);r[34]=spu_ls_read128(&c,0xd170);
    r[20]=r[84];r[33]=spu_ls_read128(&c,0xd180);
    r[3]=r[7]=r[19]=r[10]=r[8]=r[12]=r[32]=zero;r[22]=spu_ai(r[45],1);r[28]=spu_il(-2);
    r[26]=r[25]=r[30]=r[29]=r[23]=r[5]=zero;r[38]=spu_ila(0xaaaa);r[37]=spu_il(3840);r[36]=spu_ila(0xf000);
    r[24]=spu_a(r[92],r[126]);r[27]=spu_a(r[87],r[126]);r[31]=spu_ai(r[82],2);
    c.pc=0x7a90;g_spu_trampoline_fn=triangles;
}
}
