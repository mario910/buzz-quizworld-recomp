/* Image-41 output-conversion control. The shared formatter and stride getter
 * are explicit callees; this does not replace their data implementations.
 * Standalone kernel only: production routing/switch/root checks come later.
 * Caller owns synchronous callee admission; the reader checks control spans,
 * not formatter data ranges or arbitrary stride-callback side effects. */
#pragma once
#include "native_geometry_commands.h"

namespace buzz {
enum class GeometryConversionPath { existing,empty,descriptor };
struct GeometryConversionControl {};
struct GeometryConversionCallees {
    buzz_geometry_job_fn formatter,stride;
};
inline bool read_geometry_conversion_control(const spu_context& c,GeometryConversionControl& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable && (c.event_status&c.event_mask))return reject("pending interrupt");
    const unsigned raw=c.gpr[3]._u32[0],manager=raw&~15u,sp=c.gpr[1]._u32[0],frame=sp&~15u;
    if(raw>=SPU_LS_SIZE || manager<0xe680 || uint64_t(manager)+0x90>SPU_LS_SIZE)return reject("conversion manager span");
    if(sp>=SPU_LS_SIZE || frame<0xe680+96 || uint64_t(frame)+32>SPU_LS_SIZE)return reject("conversion frame span");
    if(geometry_output_overlap(manager,0x90,frame-96,128))return reject("conversion frame/manager alias");
    if(!(c.ls[manager]&16) && geometry_job_be16(c.ls+manager+8)){
        if(c.ls[manager+4]<4)return reject("builtin output data retained");
        if(geometry_job_be32(c.ls+manager+0x8c))return reject("custom conversion callback");
        if(!geometry_job_be32(c.ls+manager+0x74))return reject("missing output descriptor report");
    }
    out={};return true;
}
inline void open_geometry_conversion_frame(spu_context& c)
{
    auto& r=c.gpr;const auto stack=r[1];
    spu_ls_write128(&c,stack._u32[0]-32,r[81]);r[81]=r[3];
    spu_ls_write128(&c,stack._u32[0]-64,r[126]);r[6]=spu_ila(0x5500);r[126]=spu_link(0x5500);
    spu_ls_write128(&c,stack._u32[0]+16,r[0]);spu_ls_write128(&c,stack._u32[0]-16,r[80]);
    spu_ls_write128(&c,stack._u32[0]-48,r[82]);r[126]=spu_sf(r[6],r[126]);
    spu_ls_write128(&c,stack._u32[0]-96,stack);r[1]=spu_ai(stack,-96);
}
inline void restore_geometry_conversion_frame(spu_context& c)
{
    auto& r=c.gpr;r[1]=spu_ai(r[1],96);r[3]=r[82];
    // Callees may have written frame bytes: restore from the actual late LS.
    r[0]=spu_ls_read128(&c,r[1]._u32[0]+16);
    for(unsigned reg=80;reg<=82;++reg)r[reg]=spu_ls_read128(&c,r[1]._u32[0]-(reg-79)*16);
    r[126]=spu_ls_read128(&c,r[1]._u32[0]-64);SPU_RET(&c);
}
inline void geometry_conversion_cursor(spu_context* c)
{
    c->gpr[3]=spu_rotqbyi(spu_ls_read128(c,c->gpr[3]._u32[0]+0x50),8);SPU_RET(c);
}
inline void geometry_conversion_publish(spu_context* c)
{
    auto& r=c->gpr;r[6]=spu_ls_read128(c,r[3]._u32[0]+0x50);r[5]=spu_cwd(r[1],8);
    r[2]=spu_shufb(r[4],r[6],r[5]);spu_ls_write128(c,r[3]._u32[0]+0x50,r[2]);SPU_RET(c);
}
inline void prepare_geometry_conversion_dispatch(spu_context& c)
{
    auto& r=c.gpr;
    const auto table=spu_ila(0x5604),mask=spu_ila(65535);
    const auto config=spu_ls_read128(&c,r[81]._u32[0]),shuffle=spu_ls_read128(&c,0xd980),packing=spu_ls_read128(&c,0xdad0);
    const auto base=spu_a(table,r[126]),count=spu_and(r[10],mask),rounded=spu_ai(count,1),format=spu_rotqbyi(config,1);
    const auto selected=spu_andi(format,255),offset=spu_shli(selected,2),address=spu_a(offset,base);
    const auto targets=spu_ls_read128(&c,base._u32[0]+offset._u32[0]),target=spu_rotqby(targets,address),packed=spu_rotqbyi(packing,1);
    r[2]=spu_a(target,base);r[11]=selected;r[12]=spu_clgti(selected,3);r[13]=shuffle;r[14]=target;r[15]=base;r[16]=targets;r[17]=address;
    r[18]=packed;r[19]=offset;r[20]=rounded;r[21]=count;r[22]=format;r[23]=config;r[24]=mask;r[25]=table;
    r[27]=spu_xorbi(shuffle,32);r[28]=spu_ai(r[81],16);r[29]=spu_andi(rounded,-2);r[30]=spu_xorbi(shuffle,16);
    r[31]=spu_ls_read128(&c,0xcf40);r[32]=spu_ls_read128(&c,0xcf30);r[33]=spu_orbi(packed,4);
    r[35]=spu_iohl(spu_ilhu(0x46ff),0xff00);r[41]=spu_iohl(spu_ilhu(0x3f),0xf800);r[42]=spu_ilh(0x303);r[44]=packing;r[48]=spu_ilhu(0xbf00);
}
inline GeometryConversionPath execute_geometry_conversion_control(spu_context& c,const GeometryConversionControl&,const GeometryConversionCallees& f)
{
    auto& r=c.gpr;g_spu_trampoline_fn=nullptr;open_geometry_conversion_frame(c);
    r[5]=spu_ls_read128(&c,r[3]._u32[0]);r[4]=spu_rotqbyi(r[5],13);r[2]=spu_andi(r[4],16);
    if(r[2]._u32[0]){
        r[3]=spu_ls_read128(&c,r[3]._u32[0]+0x50);r[82]=spu_rotqbyi(r[3],4);
        restore_geometry_conversion_frame(c);return GeometryConversionPath::existing;
    }
    c.pc=0x5558;geometry_command_call(c,geometry_conversion_cursor,0x555c);
    r[9]=spu_ls_read128(&c,r[81]._u32[0]+0x50);r[82]=r[3];r[8]=spu_ls_read128(&c,r[81]._u32[0]);
    r[11]=spu_cwd(r[1],4);r[7]=spu_shufb(r[3],r[9],r[11]);r[10]=spu_rotqbyi(r[8],6);
    spu_ls_write128(&c,r[81]._u32[0]+0x50,r[7]);
    if(!r[10]._u16[0]){restore_geometry_conversion_frame(c);return GeometryConversionPath::empty;}
    prepare_geometry_conversion_dispatch(c);
    r[44]=spu_ls_read128(&c,r[81]._u32[0]+0x80);r[2]=spu_rotqbyi(r[44],12);
    r[33]=spu_ls_read128(&c,r[81]._u32[0]+0x70);r[3]=spu_rotqbyi(r[33],4);
    r[4]=r[3];r[3]=r[81];c.pc=0x56ac;geometry_command_call(c,f.formatter,0x56c4);
    c.pc=0x5648;r[3]=r[81];geometry_command_call(c,geometry_conversion_cursor,0x5650);
    r[12]=spu_ls_read128(&c,r[81]._u32[0]);r[80]=r[3];r[3]=r[81];r[38]=spu_rotqbyi(r[12],1);r[4]=spu_andi(r[38],255);
    geometry_command_call(c,f.stride,0x5670);
    // Count, manager and all stride lanes are observed after the callee.
    r[24]=spu_ls_read128(&c,r[81]._u32[0]);r[42]=r[3];r[3]=r[81];r[37]=spu_rotqbyi(r[24],6);
    r[45]=spu_mpyu(r[42],r[37]);r[46]=spu_mpyh(r[42],r[37]);r[27]=spu_a(r[46],r[45]);
    r[30]=spu_ai(r[27],15);r[81]=spu_andi(r[30],-16);r[4]=spu_a(r[80],r[81]);
    geometry_command_call(c,geometry_conversion_publish,0x56a8);restore_geometry_conversion_frame(c);return GeometryConversionPath::descriptor;
}
}
