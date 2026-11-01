/* Complete 6958/66C8 cull-result publication and 480-byte frame return. */
#pragma once
#include "native_geometry_commands.h"
namespace buzz {
struct GeometryTransformFinish { bool publish=false; };
inline bool read_geometry_transform_finish(const spu_context& c,GeometryTransformFinish& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable && (c.event_status&c.event_mask))return reject("pending interrupt");
    const unsigned manager=c.gpr[81]._u32[0];
    if(manager>=SPU_LS_SIZE || (manager&~15u)<0xe680 || uint64_t(manager&~15u)+0x190>SPU_LS_SIZE)
        return reject("cull-result manager span");
    out.publish=!(c.ls[(manager&~15u)]&16);return true;
}
inline void publish_geometry_cull_end(spu_context* c)
{
    auto& r=c->gpr;r[6]=spu_ls_read128(c,r[3]._u32[0]+0x50);r[5]=spu_cwd(r[1],8);
    r[2]=spu_shufb(r[4],r[6],r[5]);spu_ls_write128(c,r[3]._u32[0]+0x50,r[2]);SPU_RET(c);
}
inline void finish_geometry_transform(spu_context& c,const GeometryTransformFinish& p)
{
    auto& r=c.gpr;g_spu_trampoline_fn=nullptr;
    const u128 twice_kept=spu_a(r[50],r[50]),kept_indices=spu_a(twice_kept,r[50]);
    const u128 manager=r[81],configuration=spu_ls_read128(&c,manager._u32[0]);
    const u128 old_counts=spu_ls_read128(&c,manager._u32[0]+0x180),count_selector=spu_chd(r[1],0);
    const u128 flags=spu_rotqbyi(configuration,13),counts=spu_shufb(kept_indices,old_counts,count_selector);
    r[64]=twice_kept;r[83]=configuration;r[80]=kept_indices;r[85]=old_counts;r[84]=count_selector;
    r[97]=flags;r[52]=counts;r[51]=spu_andi(flags,16);
    spu_ls_write128(&c,manager._u32[0]+0x180,counts);
    if(p.publish){
        r[48]=spu_a(kept_indices,kept_indices);r[50]=spu_ls_read128(&c,manager._u32[0]+0x50);r[3]=manager;
        r[36]=spu_a(r[48],r[50]);r[81]=spu_ai(r[36],15);r[4]=spu_andi(r[81],-16);
        geometry_command_call(c,publish_geometry_cull_end,0x69a8);
    }
    // Metadata can overlap return/nonvolatile spills. Restore from the LS
    // AFTER both publications, preserving every upper word and wrapped read.
    r[1]=spu_ai(r[1],480);r[3]=r[80];r[0]=spu_ls_read128(&c,r[1]._u32[0]+16);
    for(unsigned reg=80;reg<=103;++reg)r[reg]=spu_ls_read128(&c,r[1]._u32[0]-(reg-79)*16);
    r[126]=spu_ls_read128(&c,r[1]._u32[0]-0x190);SPU_RET(&c);
}
}
