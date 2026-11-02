/* Complete 97F8/9BB0 format5: fixed12 XYZ and three packed unit vectors. */
#pragma once
#include "native_geometry_builtin_fixed_input.h"
namespace buzz {
struct GeometryBuiltin15Input {unsigned groups=0;};
inline bool read_geometry_builtin15_input(const spu_context& c,GeometryBuiltin15Input& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    unsigned groups=0;GeometryBuiltinInputMemory memory{c};
    if(!read_geometry_builtin_input_metadata(memory,geometry_builtin_input_metadata(c),5,groups,reason))return false;
    out={groups};return true;
}
inline void open_geometry_builtin15_input(spu_context& c,u128* r)
{
    const auto source=r[4],future=r[84],parameter=r[5],parameter_base=r[34],bytes=r[36],selector=r[80],table=r[83],lut=r[8];
    const u128 publication[]={r[35],r[37],r[61],r[39]};const unsigned managers[]={r[3]._u32[0],r[82]._u32[0]};
    const auto q0=spu_ls_read128(&c,source._u32[0]),q1=spu_ls_read128(&c,source._u32[0]+16),q2=spu_ls_read128(&c,source._u32[0]+32);
    const auto p0=spu_ls_read128(&c,parameter._u32[0]),p1=spu_ls_read128(&c,parameter_base._u32[0]+4),p2=spu_ls_read128(&c,parameter_base._u32[0]+8),p3=spu_ls_read128(&c,parameter_base._u32[0]+12);
    const auto iota=spu_ls_read128(&c,0xd980),packing=spu_ls_read128(&c,0xd8b0);
    auto spread=[&](u128 x){return spu_shufb(x,x,r[65]);};
    auto gather=[&](int offset){const auto x=spu_a(iota,spread(spu_il(offset)));return spu_shufb(x,x,packing);};
    const auto alignment=spu_a(iota,spread(spu_andbi(source,15))),first=spu_shufb(q0,q1,alignment),second=spu_shufb(q1,q2,alignment);
    const auto pos0=spu_selb(spu_shufb(iota,iota,iota),spu_ilh(0xc0c0),r[38]);
    const auto fifteen=spu_a(iota,spread(spu_il(15))),pos1=spu_selb(spu_shufb(fifteen,fifteen,iota),spu_ilh(0xc0c0),r[38]);
    const auto xyz0=gather_geometry_builtin_input_position(first,second,pos0,r[63],r[87]),xyz1=gather_geometry_builtin_input_position(first,second,pos1,r[63],r[87]);
    const auto six=gather(6),five=gather(5),nine=gather(9),eight=gather(8),twelve=gather(12),eleven=gather(11);
    const auto shifts=spu_ls_read128(&c,0xd1d0),mask=spu_ls_read128(&c,0xd1e0),base=spu_ls_read128(&c,0xd1f0),join=spu_ls_read128(&c,0xda20);
    const auto n0=fields_geometry_builtin_input_unit(spu_shufb(first,second,six),shifts,base,mask),n1=fields_geometry_builtin_input_unit(spu_shufb(second,second,five),shifts,base,mask);
    const auto t0=fields_geometry_builtin_input_unit(spu_shufb(first,second,nine),shifts,base,mask),t1=fields_geometry_builtin_input_unit(spu_shufb(second,second,eight),shifts,base,mask);
    const auto u0=fields_geometry_builtin_input_unit(spu_shufb(first,second,twelve),shifts,base,mask),u1=fields_geometry_builtin_input_unit(spu_shufb(second,second,eleven),shifts,base,mask);
    auto offsets=spu_shufb(spu_rotqby(p0,parameter_base),spu_il(0),spu_cwd(r[1],0));
    offsets=spu_shufb(spu_rotqby(p1,spu_ai(parameter_base,4)),offsets,spu_cwd(r[1],4));
    offsets=spu_shufb(spu_rotqby(p2,spu_ai(parameter_base,8)),offsets,spu_cwd(r[1],8));
    offsets=spu_shufb(spu_rotqby(p3,spu_ai(parameter_base,12)),offsets,spu_cwd(r[1],12));
    GeometryBuiltinInputMemory memory{c,&c};const auto a=reserve_geometry_builtin_input(memory,selector,1,managers[0]+0x160,table,publication[0]);
    const auto b=reserve_geometry_builtin_input(memory,selector,2,managers[0]+0x160,table,publication[1]);
    const auto d=reserve_geometry_builtin_input(memory,selector,3,managers[1]+0x160,table,publication[2]);
    const auto e=reserve_geometry_builtin_input(memory,selector,4,managers[1]+0x160,table,publication[3]);
    r[28]=d.leading;r[48]=lut;r[89]=iota;r[58]=mask;r[59]=shifts;r[60]=base;r[88]=join;r[81]=offsets;
    r[94]=spu_a(bytes,a.pointer);r[90]=spu_ai(r[94],16);r[85]=spu_a(bytes,b.pointer);r[84]=spu_ai(r[85],16);r[86]=spu_a(bytes,d.pointer);r[82]=spu_ai(r[86],16);r[79]=spu_a(bytes,e.pointer);r[80]=spu_ai(r[79],16);
    r[95]=spu_rotm(spu_il(-1),spu_ls_read128(&c,0xd280));r[92]=spu_ls_read128(&c,0xd2a0);r[91]=spu_ls_read128(&c,0xd290);r[93]=spu_ls_read128(&c,0xcd60);
    r[41]=spu_ls_read128(&c,0xd210);r[43]=spu_ls_read128(&c,0xd200);r[40]=spu_ls_read128(&c,0xd220);r[51]=spu_andbi(spu_fsmbi(0xfff0),8);r[67]=spu_ilhu(0x3f80);r[104]=spu_fsmbi(0xf);
    r[96]=pos0;r[97]=pos1;r[103]=six;r[102]=five;r[101]=nine;r[100]=eight;r[99]=twelve;r[98]=eleven;r[27]=xyz0.bits;r[11]=xyz1.bits;
    r[57]=n0;r[55]=n1;r[54]=t0;r[35]=t1;r[53]=u0;r[52]=u1;r[76]=spu_fma(r[72],spu_shufb(n0,n1,join),r[71]);r[75]=spu_fma(r[72],spu_shufb(t0,t1,join),r[71]);r[74]=spu_fma(r[72],spu_shufb(u0,u1,join),r[71]);
    r[47]=spu_fm(r[76],r[76]);r[46]=spu_fm(r[75],r[75]);r[44]=spu_fm(r[74],r[74]);const unsigned cleared[]={45,50,68,64,66,70,69,61,73,62,78,77};for(unsigned i:cleared)r[i]=spu_il(0);
    const auto next=spu_ai(future,30),next_align=spu_a(iota,spread(spu_andbi(next,15)));
    const auto next0=spu_ls_read128(&c,next._u32[0]),next1=spu_ls_read128(&c,next._u32[0]+16),next2=spu_ls_read128(&c,next._u32[0]+32);
    r[17]=spu_shufb(next0,next1,next_align);r[12]=spu_shufb(next1,next2,next_align);r[56]=spu_ai(future,60);
}
inline void step_geometry_builtin15_input(spu_context& c,u128* r)
{
    const auto position0=numeric_geometry_builtin_fixed_input(r[27],r[95],r[81],r[92],r[91]),position1=numeric_geometry_builtin_fixed_input(r[11],r[95],r[81],r[92],r[91]);
    const auto next0=gather_geometry_builtin_input_position(r[17],r[12],r[96],r[63],r[87]),next1=gather_geometry_builtin_input_position(r[17],r[12],r[97],r[63],r[87]);
    const auto packed_n0=spu_shufb(r[17],r[12],r[103]),packed_n1=spu_shufb(r[12],r[12],r[102]),packed_t0=spu_shufb(r[17],r[12],r[101]),packed_t1=spu_shufb(r[12],r[12],r[100]),packed_u0=spu_shufb(r[17],r[12],r[99]),packed_u1=spu_shufb(r[12],r[12],r[98]);
    const auto n0=fields_geometry_builtin_input_unit(packed_n0,r[59],r[60],r[58]),n1=fields_geometry_builtin_input_unit(packed_n1,r[59],r[60],r[58]);
    const auto t0=fields_geometry_builtin_input_unit(packed_t0,r[59],r[60],r[58]),t1=fields_geometry_builtin_input_unit(packed_t1,r[59],r[60],r[58]);
    const auto u0=fields_geometry_builtin_input_unit(packed_u0,r[59],r[60],r[58]),u1=fields_geometry_builtin_input_unit(packed_u1,r[59],r[60],r[58]);
    const auto nsum=spu_fa(r[47],spu_shlqbyi(r[47],4)),tsum=spu_fa(r[46],spu_shlqbyi(r[46],4)),usum=spu_fa(r[44],spu_shlqbyi(r[44],4));
    const auto nr=spu_fs(r[67],nsum),tr=spu_fs(r[67],tsum),ur=spu_fs(r[67],usum),ne=spu_frsqest(nr),te=spu_frsqest(tr),ue=spu_frsqest(ur);
    const auto nz=spu_fm(nr,spu_fi(nr,ne)),tz=spu_fm(tr,spu_fi(tr,te)),uz=spu_fm(ur,spu_fi(ur,ue));
    const auto zero=spu_il(0);const auto oldn0=compose_geometry_builtin_input_unit(r[64],r[45],r[69],r[43],r[41],r[40],zero),oldn1=compose_geometry_builtin_input_unit(r[64],r[45],r[61],r[43],r[41],r[40],r[51]);
    const auto oldt0=compose_geometry_builtin_input_unit(r[66],r[50],r[73],r[43],r[41],r[40],zero),oldt1=compose_geometry_builtin_input_unit(r[66],r[50],r[62],r[43],r[41],r[40],r[51]);
    const auto oldu0=compose_geometry_builtin_input_unit(r[70],r[68],r[78],r[43],r[41],r[40],zero),oldu1=compose_geometry_builtin_input_unit(r[70],r[68],r[77],r[43],r[41],r[40],r[51]);
    const auto nxy=spu_shufb(n0,n1,r[88]),txy=spu_shufb(t0,t1,r[88]),uxy=spu_shufb(u0,u1,r[88]),next_n=spu_fma(r[72],nxy,r[71]),next_t=spu_fma(r[72],txy,r[71]),next_u=spu_fma(r[72],uxy,r[71]);
    const auto old49=r[49],old42=r[42],cursor=r[56],oldn=r[76],oldt=r[75],oldu=r[74],selectors0=r[57],selectors1=r[55],selectors2=r[54],selectors3=r[35],selectors4=r[53],selectors5=r[52];
    spu_ls_write128(&c,r[94]._u32[0]+old42._u32[0],spu_selb(position0,r[93],r[104]));spu_ls_write128(&c,r[90]._u32[0]+old42._u32[0],spu_selb(position1,r[93],r[104]));
    spu_ls_write128(&c,r[85]._u32[0]+old49._u32[0],oldn0.value);spu_ls_write128(&c,r[84]._u32[0]+old49._u32[0],oldn1.value);
    const auto table_n0=spu_ls_read128(&c,r[48]._u32[0]+selectors0._u32[0]),table_n1=spu_ls_read128(&c,r[48]._u32[0]+selectors1._u32[0]);
    spu_ls_write128(&c,r[86]._u32[0]+old49._u32[0],oldt0.value);spu_ls_write128(&c,r[82]._u32[0]+old49._u32[0],oldt1.value);
    const auto table_t1=spu_ls_read128(&c,r[48]._u32[0]+selectors3._u32[0]),table_t0=spu_ls_read128(&c,r[48]._u32[0]+selectors2._u32[0]);
    spu_ls_write128(&c,r[79]._u32[0]+old49._u32[0],oldu0.value);spu_ls_write128(&c,r[80]._u32[0]+old49._u32[0],oldu1.value);
    const auto q1=spu_ls_read128(&c,cursor._u32[0]+16),q2=spu_ls_read128(&c,cursor._u32[0]+32),q0=spu_ls_read128(&c,cursor._u32[0]);
    const auto table_u0=spu_ls_read128(&c,r[48]._u32[0]+selectors4._u32[0]),table_u1=spu_ls_read128(&c,r[48]._u32[0]+selectors5._u32[0]);
    const auto bias=spu_andbi(cursor,15),broadcast=spu_shufb(bias,bias,r[65]),alignment=spu_a(r[89],broadcast);
    r[2]=bias;r[3]=next0.cd;r[4]=oldu1.control;r[5]=oldt0.value;r[6]=oldu0.source;r[7]=oldt1.source;r[8]=nxy;r[9]=cursor;r[10]=next1.c;r[11]=next1.bits;r[12]=spu_shufb(q1,q2,alignment);
    r[13]=tsum;r[14]=usum;r[15]=alignment;r[16]=old49;r[17]=spu_shufb(q0,q1,alignment);r[18]=ue;r[19]=packed_n1;r[20]=ne;r[21]=oldu0.base;r[22]=uxy;r[23]=packed_t0;r[24]=broadcast;r[25]=oldu1.source;r[26]=next1.a;r[27]=next0.bits;
    r[29]=q0;r[30]=next_u;r[31]=q1;r[32]=next1.b;r[33]=next0.b;r[34]=next_t;r[35]=t1;r[36]=spu_ai(old42,32);r[37]=next1.d;r[38]=next1.ab;r[39]=selectors5;
    r[44]=spu_fm(next_u,next_u);r[45]=oldn;r[46]=spu_fm(next_t,next_t);r[47]=spu_fm(next_n,next_n);r[49]=old42;r[50]=oldt;r[52]=u1;r[53]=u0;r[54]=t0;r[55]=n1;r[56]=spu_ai(cursor,30);r[57]=n0;
    r[61]=table_n1;r[62]=table_t1;r[64]=nz;r[66]=tz;r[68]=oldu;r[69]=table_n0;r[70]=uz;r[73]=table_t0;r[74]=next_u;r[75]=next_t;r[76]=next_n;r[77]=table_u1;r[78]=table_u0;r[83]=next_n;r[42]=r[36];
}
inline void close_geometry_builtin15_input(spu_context& c,u128* r)
{
    const auto n0=compose_geometry_builtin_input_unit(r[64],r[45],r[69],r[43],r[41],r[40],spu_il(0)),n1=compose_geometry_builtin_input_unit(r[64],r[45],r[61],r[43],r[41],r[40],r[51]);
    const auto t0=compose_geometry_builtin_input_unit(r[66],r[50],r[73],r[43],r[41],r[40],spu_il(0)),t1=compose_geometry_builtin_input_unit(r[66],r[50],r[62],r[43],r[41],r[40],r[51]);
    const auto u0=compose_geometry_builtin_input_unit(r[70],r[68],r[78],r[43],r[41],r[40],spu_il(0)),u1=compose_geometry_builtin_input_unit(r[70],r[68],r[77],r[43],r[41],r[40],r[51]);
    spu_ls_write128(&c,r[85]._u32[0]+r[49]._u32[0],n0.value);spu_ls_write128(&c,r[84]._u32[0]+r[49]._u32[0],n1.value);
    spu_ls_write128(&c,r[86]._u32[0]+r[49]._u32[0],t0.value);spu_ls_write128(&c,r[82]._u32[0]+r[49]._u32[0],t1.value);
    spu_ls_write128(&c,r[79]._u32[0]+r[49]._u32[0],u0.value);spu_ls_write128(&c,r[80]._u32[0]+r[49]._u32[0],u1.value);
    r[58]=n1.base;r[93]=n0.source;r[99]=n0.base;r[91]=n1.source;r[92]=n1.control;r[94]=n0.value;r[60]=t1.base;r[59]=t0.source;r[104]=n1.value;r[90]=t0.base;r[96]=t1.source;
    r[87]=u1.base;r[35]=t0.value;r[97]=t1.control;r[72]=u0.source;r[65]=u0.base;r[71]=t1.value;r[48]=u1.source;r[63]=u1.control;r[42]=u0.value;r[84]=u1.value;
}
inline void decode_geometry_builtin15_input(spu_context& c,const GeometryBuiltin15Input& p,buzz_geometry_job_fn finish)
{
    auto* r=c.gpr;open_geometry_builtin15_input(c,r);
    for(unsigned group=0;group<p.groups;++group)step_geometry_builtin15_input(c,r);
    close_geometry_builtin15_input(c,r);c.pc=0x9498;g_spu_trampoline_fn=finish;
}
}
