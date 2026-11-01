/* Image-41 6608 enclosing transform control, exact frames and cull setup.
 * Vertex/cull/footer children retain independent switches and contracts. */
#pragma once
#include "native_geometry_transform_finish.h"
namespace buzz {
enum class TransformControlPath { bypass_skip,bypass_publish,idle,one,two,three,alternate };
struct GeometryTransformControl { bool active=false; };
inline bool read_geometry_transform_control(const spu_context& c,GeometryTransformControl& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable && (c.event_status&c.event_mask))return reject("pending interrupt");
    const unsigned raw=c.gpr[3]._u32[0],manager=raw&~15u,sp=c.gpr[1]._u32[0],frame=sp&~15u;
    if(raw>=SPU_LS_SIZE || manager<0xe680 || uint64_t(manager)+0x190>SPU_LS_SIZE)return reject("transform manager span");
    if(sp>=SPU_LS_SIZE || frame<0xe680+480 || uint64_t(frame)+32>SPU_LS_SIZE)return reject("transform frame span");
    if(geometry_output_overlap(manager,0x190,frame-480,512))return reject("transform frame/manager alias");
    GeometryTransformControl p;p.active=c.gpr[4]._u32[0]!=0;
    if(p.active && !(c.ls[manager]&0x90))return reject("transform reporting frontier");
    out=p;return true;
}
inline void restore_geometry_transform_control(spu_context& c)
{
    auto& r=c.gpr;r[1]=spu_ai(r[1],480);r[3]=r[80];r[0]=spu_ls_read128(&c,r[1]._u32[0]+16);
    for(unsigned reg=80;reg<=103;++reg)r[reg]=spu_ls_read128(&c,r[1]._u32[0]-(reg-79)*16);
    r[126]=spu_ls_read128(&c,r[1]._u32[0]-0x190);SPU_RET(&c);
}
inline void open_geometry_transform_frame(spu_context& c)
{
    auto& r=c.gpr;const u128 stack=r[1],argument=r[4],manager=r[3];
    spu_ls_write128(&c,stack._u32[0]-0x190,r[126]);r[2]=spu_ila(0x6618);r[126]=spu_link(0x6618);
    spu_ls_write128(&c,stack._u32[0]-16,r[80]);r[80]=argument;
    spu_ls_write128(&c,stack._u32[0]-32,r[81]);r[81]=manager;
    spu_ls_write128(&c,stack._u32[0]+16,r[0]);r[126]=spu_sf(r[2],r[126]);
    for(unsigned reg=82;reg<=103;++reg)spu_ls_write128(&c,stack._u32[0]-(reg-79)*16,r[reg]);
    spu_ls_write128(&c,stack._u32[0]-480,stack);r[1]=spu_ai(stack,-480);
}
inline void prepare_geometry_cull_control(spu_context& c)
{
    auto& r=c.gpr;const u128 zero=spu_il(0),ones=spu_fsmbi(0xffff),sign=spu_ilhu(0x8000),selector=spu_ila(0x10203),mask=spu_ila(65535);
    const u128 manager=r[81],mode=r[80],enabled=spu_xsbh(r[82]);
    auto load=[&](unsigned offset){return spu_ls_read128(&c,manager._u32[0]+offset);};
    const u128 counts=load(0x180),configuration=load(0),format=load(0x140),indices=load(0x50);
    const u128 count_bytes=spu_rotqbyi(counts,14),count=spu_and(count_bytes,mask);
    const u128 low_multiplier=spu_iohl(spu_ilhu(0xaaab),0xaaaa),high_multiplier=spu_iohl(spu_ilhu(0xaaaa),0xaaab);
    const u128 low=spu_mpyu(count,low_multiplier),middle=spu_mpyu(count,high_multiplier),high=spu_mpyhhu(count,high_multiplier);
    const u128 shifted=spu_shli(low,16),carry=spu_addx(high,spu_rotmi(low,-16),spu_cg(middle,shifted));
    const u128 sum=spu_a(middle,shifted),quotient_high=spu_addx(carry,zero,spu_cg(sum,zero)),quotient=spu_rotmi(quotient_high,-1);
    const u128 triangles=spu_and(quotient,mask),remainder=spu_andi(quotient,3),rounded=spu_ai(triangles,3);
    const u128 flags=spu_rotqbyi(configuration,13),format_flags=spu_rotqbyi(format,13),selector_offset=spu_shli(spu_andi(flags,15),2);
    const u128 selected=spu_a(selector_offset,manager),selected_quad=spu_ls_read128(&c,selected._u32[0]+16);
    const u128 selected_word=spu_rotqby(selected_quad,selected),original_word=spu_rotqbyi(indices,12);
    const u128 enabled_mask=spu_fsm(spu_ceqhi(enabled,0)),is_five=spu_ceqbi(format_flags,5),five_extended=spu_xsbh(is_five);
    const u128 not_five=spu_ceqhi(five_extended,0),not_five_mask=spu_fsm(not_five),alternate_bit=spu_andc(sign,not_five_mask);
    const u128 selection=spu_or(alternate_bit,mode),groups=spu_rotmi(rounded,-2);
    const u128 index_bytes=spu_a(spu_a(triangles,triangles),triangles),aligned=spu_andi(spu_ai(spu_a(index_bytes,index_bytes),15),-16);
    const u128 input=spu_shufb(indices,indices,selector),aligned_word=spu_shufb(aligned,selector,selector),output=spu_a(input,aligned_word);
    r[5]=zero;r[6]=selection;r[10]=spu_selb(selected_word,original_word,enabled_mask);
    r[13]=indices;r[15]=spu_shufb(groups,groups,selector);r[17]=spu_ceqi(selection,3);r[18]=spu_ls_read128(&c,0xdaa0);
    r[19]=spu_ila(0xdcf0);r[20]=spu_ila(0xdbf0);r[21]=zero;r[22]=zero;r[23]=spu_ceqi(remainder,0);
    r[24]=spu_rotma(spu_il(15),spu_sfi(remainder,0));r[25]=enabled_mask;r[26]=original_word;r[27]=selected_word;r[28]=groups;r[29]=rounded;
    r[30]=remainder;r[31]=output;r[32]=spu_ls_read128(&c,0xcfe0);r[33]=spu_sfi(remainder,0);r[34]=spu_il(15);r[35]=aligned_word;
    r[36]=triangles;r[37]=input;r[38]=quotient;r[39]=aligned;r[40]=selector;r[41]=zero;r[42]=alternate_bit;r[43]=zero;
    r[44]=spu_ceqhi(enabled,0);r[45]=spu_rotqbyi(spu_ls_read128(&c,0xd040),8);r[46]=index_bytes;
    r[47]=spu_rotqbyi(spu_ls_read128(&c,0xd030),8);r[48]=zero;r[49]=selected;r[50]=zero;r[51]=spu_ls_read128(&c,0xd040);r[52]=spu_ls_read128(&c,0xd030);
    r[53]=spu_rotqbyi(r[18],8);r[54]=spu_xorbi(r[18],16);r[55]=sign;r[56]=spu_rotqbyi(spu_ls_read128(&c,0xd930),8);
    r[57]=spu_shufb(spu_ai(r[1],32),selector,selector);r[58]=not_five_mask;r[59]=mask;r[60]=not_five;r[61]=five_extended;r[62]=carry;r[63]=zero;
    r[64]=spu_a(output,r[32]);r[65]=spu_andi(flags,15);r[66]=sum;r[67]=high;r[68]=spu_rotmi(low,-16);r[69]=middle;r[70]=zero;r[71]=is_five;
    r[72]=flags;r[73]=format_flags;r[74]=low;r[75]=count;r[76]=high_multiplier;r[77]=low_multiplier;r[78]=spu_ls_read128(&c,0xd930);
    r[80]=sign;r[82]=zero;r[83]=spu_il(24);r[84]=spu_selb(r[24],zero,r[23]);r[85]=zero;r[86]=configuration;r[87]=count_bytes;r[88]=format;r[89]=counts;r[90]=indices;
    r[91]=spu_shlqbyi(r[64],4);r[94]=spu_rotqbyi(spu_ls_read128(&c,0xd020),8);r[97]=ones;r[100]=spu_ls_read128(&c,0xd020);
    r[3]=spu_a(r[19],r[126]);r[7]=spu_a(r[20],r[126]);r[8]=spu_a(spu_ila(0xdaf0),r[126]);r[9]=spu_ai(indices,16);r[11]=sign;r[12]=spu_ilh(0x303);
}
inline void run_geometry_transform_cull(spu_context& c,buzz_geometry_job_fn root,buzz_geometry_job_fn finish)
{
    g_spu_trampoline_fn=nullptr;root(&c);
    for(unsigned steps=0;!(c.pc==0x6958 && g_spu_trampoline_fn==finish);++steps){
        if(!g_spu_trampoline_fn || steps>=1000000)std::abort();
        switch(c.pc){case 0x6ee0:case 0x6ee8:case 0x6b10:case 0x6b18:break;
            default:std::fprintf(stderr,"[geometry-job/transform-control] unexpected cull continuation %X\n",c.pc);std::abort();}
        const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&c);
    }
    g_spu_trampoline_fn=nullptr;
}
inline TransformControlPath execute_geometry_transform_control(spu_context& c,const GeometryTransformControl& p,const buzz_geometry_transform_functions& f)
{
    auto& r=c.gpr;g_spu_trampoline_fn=nullptr;open_geometry_transform_frame(c);
    if(!p.active){
        r[6]=spu_ls_read128(&c,r[3]._u32[0]);r[5]=spu_rotqbyi(r[6],12);r[4]=spu_ceqi(r[5],-1);
        const bool publish=!r[4]._u32[0];
        if(publish){r[4]=spu_ls_read128(&c,r[3]._u32[0]+0x50);geometry_command_call(c,publish_geometry_cull_end,0x66b8);}
        r[14]=spu_ila(65535);r[16]=spu_ls_read128(&c,r[81]._u32[0]+0x180);r[15]=spu_rotqbyi(r[16],14);r[80]=spu_and(r[15],r[14]);
        restore_geometry_transform_control(c);return publish?TransformControlPath::bypass_publish:TransformControlPath::bypass_skip;
    }
    r[10]=spu_ls_read128(&c,r[3]._u32[0]);r[4]=spu_rotqbyi(r[10],13);r[9]=spu_andi(r[4],255);
    r[8]=spu_rotmi(r[9],-4);r[7]=spu_andi(r[8],1);r[82]=spu_ceqbi(r[7],0);r[3]=spu_xsbh(r[82]);
    if(r[3]._u16[0]){r[12]=spu_cgtbi(r[4],-1);r[11]=spu_xsbh(r[12]);}
    c.pc=0x6790;r[3]=r[81];geometry_command_call(c,f.vertex,0x6798);prepare_geometry_cull_control(c);
    auto path=TransformControlPath::idle;buzz_geometry_job_fn cull=nullptr;
    if(r[17]._u32[0]){r[82]=spu_il(-1);path=TransformControlPath::three;cull=f.cull;}
    else{
        r[92]=spu_clgti(r[6],3);
        if(r[92]._u32[0]){
            const u128 sign=spu_ilhu(0x8000);r[96]=sign;r[95]=spu_ori(sign,2);r[93]=spu_ceq(r[6],r[95]);
            if(r[93]._u32[0]){r[11]=spu_il(0);r[82]=spu_il(-1);cull=f.alternate;}
            else{
                r[101]=sign;r[99]=spu_ori(sign,3);r[98]=spu_ceq(r[6],r[99]);
                if(r[98]._u32[0]){r[82]=spu_il(-1);cull=f.alternate;}
                else{r[2]=sign;r[103]=spu_ori(sign,1);r[102]=spu_ceq(r[6],r[103]);if(r[102]._u32[0])cull=f.alternate;}
            }
            if(cull)path=TransformControlPath::alternate;
        }else{
            r[78]=spu_ceqi(r[6],1);
            if(r[78]._u32[0]){path=TransformControlPath::one;cull=f.cull;}
            else{r[82]=spu_ceqi(r[6],2);if(r[82]._u32[0]){r[11]=spu_il(0);r[82]=spu_il(-1);path=TransformControlPath::two;cull=f.cull;}}
        }
    }
    c.pc=cull?(path==TransformControlPath::alternate?0x69e8:0x6e08):0x6958;
    if(cull)run_geometry_transform_cull(c,cull,f.finish);
    g_spu_trampoline_fn=nullptr;f.finish(&c);
    // An independently disabled footer retains its original queued 66C8.
    if(g_spu_trampoline_fn==f.restore && c.pc==0x66c8){g_spu_trampoline_fn=nullptr;f.restore(&c);}
    return path;
}
}
