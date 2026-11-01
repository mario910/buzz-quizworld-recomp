/* Complete 9E70/A170 format4: fixed12 XYZ and two packed unit vectors. */
#pragma once
#include "native_geometry_builtin_fixed_input.h"
namespace buzz {
struct GeometryBuiltin12Input {unsigned groups=0;};
inline bool read_geometry_builtin12_input(const spu_context& c,GeometryBuiltin12Input& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    unsigned groups=0;GeometryBuiltinInputMemory memory{c};
    if(!read_geometry_builtin_input_metadata(memory,geometry_builtin_input_metadata(c),4,groups,reason))return false;
    out={groups};return true;
}
inline void open_geometry_builtin12_input(spu_context& c,u128* r)
{
    const auto source=r[4],future=r[84],parameter=r[5],parameter_base=r[34],bytes=r[36],selector=r[80],table=r[83];
    const u128 publication[]={r[35],r[37],r[61]};const unsigned managers[]={r[3]._u32[0],r[82]._u32[0]};
    const auto q0=spu_ls_read128(&c,source._u32[0]),q1=spu_ls_read128(&c,source._u32[0]+16),q2=spu_ls_read128(&c,source._u32[0]+32);
    const auto p0=spu_ls_read128(&c,parameter._u32[0]),p1=spu_ls_read128(&c,parameter._u32[0]+4),p2=spu_ls_read128(&c,parameter._u32[0]+8),p3=spu_ls_read128(&c,parameter._u32[0]+12);
    const auto iota=spu_ls_read128(&c,0xd980),packing=spu_ls_read128(&c,0xd8b0);
    auto spread=[&](u128 x){return spu_shufb(x,x,r[65]);};
    auto gather=[&](int offset){const auto x=spu_a(iota,spread(spu_il(offset)));return spu_shufb(x,x,packing);};
    const auto alignment=spu_a(iota,spread(spu_andbi(source,15))),first=spu_shufb(q0,q1,alignment),second=spu_shufb(q1,q2,alignment);
    const auto pos0=spu_selb(spu_shufb(iota,iota,iota),spu_ilh(0xc0c0),r[38]);
    const auto twelve=spu_a(iota,spread(spu_il(12))),pos1=spu_selb(spu_shufb(twelve,twelve,iota),spu_ilh(0xc0c0),r[38]);
    const auto xyz0=gather_geometry_builtin_input_position(first,second,pos0,r[63],r[87]),xyz1=gather_geometry_builtin_input_position(first,second,pos1,r[63],r[87]);
    const auto six=gather(6),two=gather(2),nine=gather(9),five=gather(5);
    const auto shifts=spu_ls_read128(&c,0xd1d0),mask=spu_ls_read128(&c,0xd1e0),base=spu_ls_read128(&c,0xd1f0),join=spu_ls_read128(&c,0xda20);
    const auto n0=fields_geometry_builtin_input_unit(spu_shufb(first,second,six),shifts,base,mask),n1=fields_geometry_builtin_input_unit(spu_shufb(second,second,two),shifts,base,mask);
    const auto t0=fields_geometry_builtin_input_unit(spu_shufb(first,second,nine),shifts,base,mask),t1=fields_geometry_builtin_input_unit(spu_shufb(second,second,five),shifts,base,mask);
    auto offsets=spu_shufb(spu_rotqby(p0,parameter),spu_il(0),spu_cwd(r[1],0));
    offsets=spu_shufb(spu_rotqby(p1,spu_ai(parameter_base,4)),offsets,spu_cwd(r[1],4));
    offsets=spu_shufb(spu_rotqby(p2,spu_ai(parameter_base,8)),offsets,spu_cwd(r[1],8));
    offsets=spu_shufb(spu_rotqby(p3,spu_ai(parameter_base,12)),offsets,spu_cwd(r[1],12));
    GeometryBuiltinInputMemory memory{c,&c};const auto a=reserve_geometry_builtin_input(memory,selector,1,managers[0]+0x160,table,publication[0]);
    const auto b=reserve_geometry_builtin_input(memory,selector,2,managers[0]+0x160,table,publication[1]);
    const auto d=reserve_geometry_builtin_input(memory,selector,3,managers[1]+0x160,table,publication[2]);
    r[23]=d.publish_after;r[48]=r[8];r[67]=iota;r[50]=mask;r[51]=shifts;r[52]=base;r[74]=join;r[59]=offsets;
    r[73]=spu_a(bytes,a.pointer);r[62]=spu_ai(r[73],16);r[60]=spu_a(bytes,b.pointer);r[66]=spu_ai(r[60],16);r[80]=spu_a(bytes,d.pointer);r[64]=spu_ai(r[80],16);
    r[75]=spu_rotm(spu_il(-1),spu_ls_read128(&c,0xd280));r[68]=spu_ls_read128(&c,0xd2a0);r[69]=spu_ls_read128(&c,0xd290);r[70]=spu_ls_read128(&c,0xcd60);
    r[35]=spu_ls_read128(&c,0xd210);r[36]=spu_ls_read128(&c,0xd200);r[37]=spu_ls_read128(&c,0xd220);r[58]=spu_andbi(spu_fsmbi(0xfff0),8);r[61]=spu_ilhu(0x3f80);r[79]=spu_fsmbi(0xf);
    r[81]=pos0;r[76]=pos1;r[86]=six;r[78]=two;r[77]=nine;r[85]=five;r[16]=xyz0.bits;r[9]=xyz1.bits;
    r[45]=n0;r[44]=n1;r[43]=t0;r[40]=t1;r[57]=spu_fma(r[72],spu_shufb(n0,n1,join),r[71]);r[56]=spu_fma(r[72],spu_shufb(t0,t1,join),r[71]);
    r[38]=spu_fm(r[57],r[57]);r[34]=spu_fm(r[56],r[56]);const unsigned cleared[]={17,33,39,46,47,53,54,55};for(unsigned i:cleared)r[i]=spu_il(0);
    const auto next=spu_ai(future,24),next_align=spu_a(iota,spread(spu_andbi(next,15)));
    const auto next0=spu_ls_read128(&c,next._u32[0]),next1=spu_ls_read128(&c,next._u32[0]+16),next2=spu_ls_read128(&c,next._u32[0]+32);
    r[10]=spu_shufb(next0,next1,next_align);r[7]=spu_shufb(next1,next2,next_align);r[41]=spu_ai(future,48);
}
inline void step_geometry_builtin12_input(spu_context& c,u128* r)
{
    const auto position0=numeric_geometry_builtin_fixed_input(r[16],r[75],r[59],r[68],r[69]),position1=numeric_geometry_builtin_fixed_input(r[9],r[75],r[59],r[68],r[69]);
    const auto next0=gather_geometry_builtin_input_position(r[10],r[7],r[81],r[63],r[87]),next1=gather_geometry_builtin_input_position(r[10],r[7],r[76],r[63],r[87]);
    const auto packed_n0=spu_shufb(r[10],r[7],r[86]),packed_n1=spu_shufb(r[7],r[7],r[78]),packed_t0=spu_shufb(r[10],r[7],r[77]),packed_t1=spu_shufb(r[7],r[7],r[85]);
    const auto n0=fields_geometry_builtin_input_unit(packed_n0,r[51],r[52],r[50]),n1=fields_geometry_builtin_input_unit(packed_n1,r[51],r[52],r[50]);
    const auto t0=fields_geometry_builtin_input_unit(packed_t0,r[51],r[52],r[50]),t1=fields_geometry_builtin_input_unit(packed_t1,r[51],r[52],r[50]);
    const auto nsum=spu_fa(r[38],spu_shlqbyi(r[38],4)),tsum=spu_fa(r[34],spu_shlqbyi(r[34],4));
    const auto nr=spu_fs(r[61],nsum),tr=spu_fs(r[61],tsum);
    const auto nz=spu_fm(nr,spu_fi(nr,spu_frsqest(nr))),tz=spu_fm(tr,spu_fi(tr,spu_frsqest(tr)));
    const auto zero=spu_il(0);const auto oldn0=compose_geometry_builtin_input_unit(r[17],r[33],r[53],r[36],r[35],r[37],zero),oldn1=compose_geometry_builtin_input_unit(r[17],r[33],r[39],r[36],r[35],r[37],r[58]);
    const auto oldt0=compose_geometry_builtin_input_unit(r[47],r[46],r[54],r[36],r[35],r[37],zero),oldt1=compose_geometry_builtin_input_unit(r[47],r[46],r[55],r[36],r[35],r[37],r[58]);
    const auto nxy=spu_shufb(n0,n1,r[74]),txy=spu_shufb(t0,t1,r[74]),next_n=spu_fma(r[72],nxy,r[71]),next_t=spu_fma(r[72],txy,r[71]);
    const auto old49=r[49],old42=r[42],cursor=r[41],oldn=r[57],oldt=r[56],selectors0=r[45],selectors1=r[44],selectors2=r[43],selectors3=r[40];
    const auto out0=spu_selb(position0,r[70],r[79]),out1=spu_selb(position1,r[70],r[79]);
    spu_ls_write128(&c,r[73]._u32[0]+old42._u32[0],out0);spu_ls_write128(&c,r[62]._u32[0]+old42._u32[0],out1);
    spu_ls_write128(&c,r[60]._u32[0]+old49._u32[0],oldn0.value);spu_ls_write128(&c,r[66]._u32[0]+old49._u32[0],oldn1.value);
    const auto table_n0=spu_ls_read128(&c,r[48]._u32[0]+selectors0._u32[0]),table_n1=spu_ls_read128(&c,r[48]._u32[0]+selectors1._u32[0]);
    spu_ls_write128(&c,r[80]._u32[0]+old49._u32[0],oldt0.value);spu_ls_write128(&c,r[64]._u32[0]+old49._u32[0],oldt1.value);
    const auto q0=spu_ls_read128(&c,cursor._u32[0]),q1=spu_ls_read128(&c,cursor._u32[0]+16),q2=spu_ls_read128(&c,cursor._u32[0]+32);
    const auto table_t0=spu_ls_read128(&c,r[48]._u32[0]+selectors2._u32[0]),table_t1=spu_ls_read128(&c,r[48]._u32[0]+selectors3._u32[0]);
    const auto bias=spu_andbi(cursor,15),alignment=spu_a(r[67],spu_shufb(bias,bias,r[65]));
    r[2]=oldt1.control;r[3]=oldt1.value;r[4]=out1;r[5]=bias;r[6]=cursor;r[7]=spu_shufb(q1,q2,alignment);r[8]=next1.block;r[9]=next1.bits;r[10]=spu_shufb(q0,q1,alignment);r[11]=next0.block;
    r[12]=tr;r[13]=nr;r[14]=txy;r[15]=nxy;r[16]=next0.bits;r[17]=nz;r[18]=packed_t1;r[19]=packed_t0;r[20]=old49;r[21]=next1.ab;r[22]=position1;r[24]=old42;r[25]=oldn0.source;r[26]=next0.b;r[27]=next0.d;r[28]=next0.c;
    r[29]=selectors0;r[30]=selectors1;r[31]=selectors2;r[32]=position0;r[33]=oldn;r[34]=spu_fm(next_t,next_t);r[38]=spu_fm(next_n,next_n);r[39]=table_n1;r[40]=t1;r[41]=spu_ai(cursor,24);r[42]=spu_ai(old42,32);r[43]=t0;r[44]=n1;r[45]=n0;r[46]=oldt;r[47]=tz;r[49]=old42;
    r[53]=table_n0;r[54]=table_t0;r[55]=table_t1;r[56]=next_t;r[57]=next_n;r[82]=next_n;r[83]=next_t;r[84]=tsum;r[88]=next0.a;r[89]=selectors3;r[90]=oldn0.value;r[91]=oldt1.base;r[92]=oldt0.source;r[93]=oldt0.base;r[94]=oldn1.base;r[95]=r[42];r[96]=next0.cd;r[97]=next0.ab;r[98]=next1.c;r[99]=next1.d;r[100]=next1.a;r[101]=next1.b;r[102]=q1;r[103]=q2;r[104]=alignment;
}
inline void close_geometry_builtin12_input(spu_context& c,u128* r)
{
    const auto n0=compose_geometry_builtin_input_unit(r[17],r[33],r[53],r[36],r[35],r[37],spu_il(0)),n1=compose_geometry_builtin_input_unit(r[17],r[33],r[39],r[36],r[35],r[37],r[58]);
    const auto t0=compose_geometry_builtin_input_unit(r[47],r[46],r[54],r[36],r[35],r[37],spu_il(0)),t1=compose_geometry_builtin_input_unit(r[47],r[46],r[55],r[36],r[35],r[37],r[58]);
    spu_ls_write128(&c,r[60]._u32[0]+r[24]._u32[0],n0.value);spu_ls_write128(&c,r[66]._u32[0]+r[24]._u32[0],n1.value);
    spu_ls_write128(&c,r[80]._u32[0]+r[24]._u32[0],t0.value);spu_ls_write128(&c,r[64]._u32[0]+r[24]._u32[0],t1.value);
    r[79]=n1.base;r[62]=n0.source;r[81]=n0.base;r[57]=n1.source;r[65]=t1.base;r[56]=n0.value;r[76]=n1.control;r[87]=t0.source;r[61]=t0.base;r[74]=n1.value;r[71]=t1.source;r[72]=t1.control;r[63]=t0.value;r[48]=t1.value;
}
inline void decode_geometry_builtin12_input(spu_context& c,const GeometryBuiltin12Input& p,buzz_geometry_job_fn finish)
{
    u128 r[128];memcpy(r,c.gpr,sizeof r);open_geometry_builtin12_input(c,r);
    for(unsigned group=0;group<p.groups;++group)step_geometry_builtin12_input(c,r);
    close_geometry_builtin12_input(c,r);memcpy(c.gpr,r,sizeof r);c.pc=0x9498;g_spu_trampoline_fn=finish;
}
}
