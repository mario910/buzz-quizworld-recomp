/* Image-41 output-conversion control. Explicit formatter/builtin data callees
 * keep their own exact contracts. Admission forecasts formatter metadata or
 * scalar builtin write footprints before any frame/cursor mutation. */
#pragma once
#include "native_geometry_commands.h"
#include "native_geometry_formatter.h"

namespace buzz {
enum class GeometryConversionPath { existing,empty,descriptor,builtin0,builtin1,builtin2,builtin3 };
struct GeometryConversionControl {};
struct GeometryConversionCallees {
    buzz_geometry_job_fn formatter,stride;
    const buzz_geometry_builtin_output_functions* builtins=nullptr;
};
inline bool read_geometry_conversion_control(const spu_context& c,GeometryConversionControl& out,const char** reason=nullptr,bool builtins=false)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    const unsigned raw=c.gpr[3]._u32[0],manager=raw&~15u,sp=c.gpr[1]._u32[0],frame=sp&~15u;
    if(raw>=SPU_LS_SIZE || manager<0xe680 || uint64_t(manager)+0x90>SPU_LS_SIZE)return reject("conversion manager span");
    if(sp>=SPU_LS_SIZE || frame<0xe680+96 || uint64_t(frame)+32>SPU_LS_SIZE)return reject("conversion frame span");
    if(geometry_output_overlap(manager,0x90,frame-96,128))return reject("conversion frame/manager alias");
    if(!(c.ls[manager]&16) && geometry_job_be16(c.ls+manager+8)){
        if(c.ls[manager+4]<4){if(!builtins)return reject("builtin output data retained");}
        else {
            if(geometry_job_be32(c.ls+manager+0x8c))return reject("custom conversion callback");
            if(!geometry_job_be32(c.ls+manager+0x74))return reject("missing output descriptor report");
        }
    }
    out={};return true;
}
struct GeometryConversionAdmission {
    GeometryConversionControl control;
    GeometryConversionPath path=GeometryConversionPath::existing;
    GeometryFormatter formatter;
};
inline bool read_geometry_conversion_native(const spu_context& c,GeometryConversionAdmission& out,const char** reason=nullptr,bool column_spans=true)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};GeometryConversionAdmission plan;
    if(!read_geometry_conversion_control(c,plan.control,reason,true))return false;
    const unsigned manager=c.gpr[3]._u32[0]&~15u;
    if(c.ls[manager]&16){out=plan;return true;}
    if(!geometry_job_be16(c.ls+manager+8)){plan.path=GeometryConversionPath::empty;out=plan;return true;}
    const unsigned format=c.ls[manager+4];
    if(format<4){
        // Only the selected table words determine control flow. Other bytes
        // remain observable through the original speculative vector loads.
        static constexpr unsigned dispatch[]={0xc4,0x824,0x15c,0x42c},stride[]={0x68,0x60,0x58,0x58};
        if(geometry_job_be32(c.ls+0x5604+format*4)!=dispatch[format] || geometry_job_be32(c.ls+0x4104+format*4)!=stride[format])return reject("builtin conversion target");
        const unsigned count=geometry_job_be16(c.ls+manager+8),groups=format?(count+1)/2:(count+3)/4;
        const unsigned cursor=geometry_job_be32(c.ls+manager+0x58),step=format==1?40:48,quads=format==1?4:3;
        // Scalar control/footprint only: no original clone, quantization or
        // speculative source evaluation is needed to decide eligibility.
        for(unsigned group=0;group<groups;++group)for(unsigned quad=0;quad<quads;++quad){
            const unsigned address=(cursor+group*step+quad*16)&0x3fff0;
            if(address<0xe680)return reject("builtin conversion protected padded write");
            if(geometry_output_overlap(address,16,manager,0x90))return reject("builtin conversion data/control metadata alias");
        }
        plan.path=GeometryConversionPath(unsigned(GeometryConversionPath::builtin0)+format);out=plan;return true;
    }
    if(geometry_job_be32(c.ls+manager+0x88))return reject("custom stride callback");
    plan.path=GeometryConversionPath::descriptor;
    // Outer frame writes and cursor publication cannot touch manager/descriptor
    // control metadata. Data/frame aliases stay allowed: execution reloads the
    // actual late frame. Formatter admission checks its own deeper 128-byte frame.
    u128 arguments[128];memcpy(arguments,c.gpr,sizeof arguments);
    arguments[1]=spu_ai(arguments[1],-96);arguments[0]=spu_link(0x56c4);
    arguments[4]=spu_rotqbyi(spu_ls_read128(&c,c.gpr[3]._u32[0]+0x70),4);
    const auto old=spu_ls_read128(&c,c.gpr[3]._u32[0]+0x50);
    const auto published=spu_shufb(spu_rotqbyi(old,8),old,spu_cwd(arguments[1],4));
    // Formatter restores r81 from this slot. Its caller immediately uses r81
    // to load callback/manager metadata again, so data must not replace that
    // pointer. Other data/frame aliases, including scratch and late outer
    // return values, remain allowed and are observed by actual reloads.
    const unsigned frame=c.gpr[1]._u32[0]&~15u;
    const GeometryFormatterEntry entry{arguments,&published,frame-96,128,frame-128,16};
    if(!read_geometry_formatter(c,plan.formatter,reason,&entry,column_spans))return false;
    out=plan;return true;
}
// Complete seven-block standard stride getter, including the speculative table
// and descriptor loads on paths that do not consume their low-lane results.
// Production admission proves the custom callback absent before outer mutation.
inline void geometry_conversion_stride(spu_context* c)
{
    auto& r=c->gpr;r[13]=spu_ila(0x40d0);r[12]=spu_link(0x40d0);
    r[10]=spu_shli(r[4],2);r[11]=spu_ila(0x4104);r[6]=spu_clgti(r[4],3);
    r[79]=spu_sf(r[13],r[12]);r[7]=spu_a(r[11],r[79]);r[5]=spu_a(r[10],r[7]);
    r[9]=spu_ls_read128(c,r[7]._u32[0]+r[10]._u32[0]);r[2]=spu_a(spu_rotqby(r[9],r[5]),r[7]);
    if(r[6]._u32[0]){
        r[14]=spu_ls_read128(c,r[3]._u32[0]+0x80);r[5]=spu_rotqbyi(r[14],8);
        if(r[5]._u32[0])std::abort();
        r[20]=spu_il(1);r[21]=spu_ls_read128(c,r[3]._u32[0]+0x70);r[3]=spu_il(0);
        r[19]=spu_rotqbyi(r[21],4);r[18]=spu_ai(r[19],14);r[17]=spu_ls_read128(c,r[19]._u32[0]+r[20]._u32[0]);
        r[15]=spu_ceqi(r[19],0);r[16]=spu_rotqby(r[17],r[18]);r[4]=spu_andi(r[16],255);r[3]=spu_selb(r[4],r[3],r[15]);
    }else r[3]=spu_il(r[4]._u32[0]==0?12:r[4]._u32[0]==1?20:24);
    SPU_RET(c);
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
    GeometryConversionPath path=GeometryConversionPath::descriptor;
    if(!r[12]._u32[0]){
        const unsigned format=r[11]._u32[0];path=GeometryConversionPath(unsigned(GeometryConversionPath::builtin0)+format);
        if(format>=4 || !f.builtins || !f.builtins->data[format] || !f.builtins->finish)std::abort();
        // This is a queued jump, not a synchronous callee: r0 and host depth
        // must remain unchanged. Consume only the proven shared data frontier.
        c.pc=r[2]._u32[0];g_spu_trampoline_fn=nullptr;f.builtins->data[format](&c);
        if(c.pc!=0x5648 || g_spu_trampoline_fn!=f.builtins->finish)std::abort();
        g_spu_trampoline_fn=nullptr;
    }else {
        r[44]=spu_ls_read128(&c,r[81]._u32[0]+0x80);r[2]=spu_rotqbyi(r[44],12);
        r[33]=spu_ls_read128(&c,r[81]._u32[0]+0x70);r[3]=spu_rotqbyi(r[33],4);
        r[4]=r[3];r[3]=r[81];c.pc=0x56ac;geometry_command_call(c,f.formatter,0x56c4);
    }
    c.pc=0x5648;r[3]=r[81];geometry_command_call(c,geometry_conversion_cursor,0x5650);
    r[12]=spu_ls_read128(&c,r[81]._u32[0]);r[80]=r[3];r[3]=r[81];r[38]=spu_rotqbyi(r[12],1);r[4]=spu_andi(r[38],255);
    geometry_command_call(c,f.stride,0x5670);
    // Count, manager and all stride lanes are observed after the callee.
    r[24]=spu_ls_read128(&c,r[81]._u32[0]);r[42]=r[3];r[3]=r[81];r[37]=spu_rotqbyi(r[24],6);
    r[45]=spu_mpyu(r[42],r[37]);r[46]=spu_mpyh(r[42],r[37]);r[27]=spu_a(r[46],r[45]);
    r[30]=spu_ai(r[27],15);r[81]=spu_andi(r[30],-16);r[4]=spu_a(r[80],r[81]);
    geometry_command_call(c,geometry_conversion_publish,0x56a8);restore_geometry_conversion_frame(c);return path;
}
}
