/* Image-41 clip-matrix preparation: 6188/65EC/65FC to queued 63E0.
 * Matrix chains stay in host vectors; the complete handoff ABI is materialized
 * once. The existing Stage F vertex loop executes after this boundary. */
#pragma once
#include "native_geometry_commands.h"
namespace buzz {
struct GeometryVertexPrepare { unsigned path=0; };
inline bool read_geometry_vertex_prepare(const spu_context& c,GeometryVertexPrepare& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable && (c.event_status&c.event_mask))return reject("pending interrupt");
    const unsigned manager=c.gpr[3]._u32[0];
    if(manager>=SPU_LS_SIZE || (manager&~15u)<0xe680 || uint64_t(manager&~15u)+0x150>SPU_LS_SIZE)
        return reject("vertex-preparation manager span");
    const unsigned format=c.ls[(manager+0x140)&0x3fff0];
    out.path=format<=2?0:format<=4?1:format==5?2:3;return true;
}
struct GeometryPreparedColumn { u128 xyz,value; };
inline GeometryPreparedColumn geometry_prepare_column(u128 v,const u128* matrix,const u128* selectors)
{
    u128 total=spu_fm(spu_shufb(v,v,selectors[0]),matrix[0]);
    total=spu_fma(spu_shufb(v,v,selectors[1]),matrix[1],total);
    total=spu_fma(spu_shufb(v,v,selectors[2]),matrix[2],total);
    return {total,spu_fma(spu_shufb(v,v,selectors[3]),matrix[3],total)};
}
inline void prepare_geometry_vertices(spu_context& c,const GeometryVertexPrepare& p,buzz_geometry_job_fn vertex)
{
    auto& r=c.gpr;const u128 manager=r[3];
    auto load=[&](unsigned offset){return spu_ls_read128(&c,manager._u32[0]+offset);};
    const u128 selectors[]={spu_ila(0x10203),spu_iohl(spu_ilhu(0x405),0x607),
        spu_iohl(spu_ilhu(0x809),0xa0b),spu_iohl(spu_ilhu(0xc0d),0xe0f)};
    const u128 config=load(0),cursor=spu_rotqbyi(load(0x50),12),counts=load(0xd0);
    const u128 a=load(0x130),b=load(0x120),origin=load(0x110);
    const u128 basis[]={load(0xf0),load(0x100),load(0xe0)};
    const u128 matrix[]={load(0xa0),load(0xb0),load(0xc0),spu_ls_read128(&c,0xcd60)};
    u128 projected[3];GeometryPreparedColumn transformed[3];
    for(unsigned axis=0;axis<3;++axis){
        // The source pairs the three basis columns with Y/Z/X coefficients.
        const auto selector=selectors[(axis+1)%3];
        const u128 product=spu_fm(origin,spu_shufb(a,a,selector));
        projected[axis]=spu_fma(spu_shufb(b,b,selector),basis[axis],product);
        transformed[axis]=geometry_prepare_column(projected[axis],matrix,selectors);
    }
    const auto translated=geometry_prepare_column(origin,matrix,selectors);
    const u128 flags=spu_andi(spu_rotqbyi(config,13),255),flag16=spu_andi(flags,16),direct=spu_ceqi(flag16,0);
    const u128 selected=spu_a(spu_shli(spu_andi(flags,15),2),manager);
    const u128 attribute=spu_rotqby(spu_ls_read128(&c,selected._u32[0]+16),selected);
    const u128 input=spu_selb(cursor,attribute,spu_fsm(direct));
    const u128 count=spu_and(spu_rotqbyi(config,6),spu_ila(0xffff));
    const u128 batches=spu_shli(spu_rotmai(spu_ai(count,3),-2),6),bytes=spu_ai(batches,192);
    const u128 scratch=spu_a(spu_shufb(bytes,bytes,selectors[0]),cursor);
    const u128 low=spu_shufb(counts,counts,selectors[2]),high=spu_shufb(counts,counts,selectors[3]);
    const u128 greater=spu_fcgt(low,high),minimum=spu_selb(low,high,greater),maximum=spu_selb(high,low,greater);
    const u128 sum=spu_ah(counts,spu_rotqmbyi(counts,-4));
    const u128 scale=spu_cuflt(spu_shufb(counts,sum,spu_ls_read128(&c,0xd8d0)),155);
    const u128 format=spu_rotqbyi(load(0x140),13);
    r[2]=cursor;r[3]=spu_ai(scratch,48);r[4]=input;r[5]=spu_xsbh(spu_clgtbi(format,2));
    r[6]=spu_il(p.path==1?1:p.path==2?2:0);r[7]=spu_ai(scratch,32);r[8]=spu_ai(scratch,16);
    r[9]=spu_ai(input,48);r[10]=spu_ai(input,32);r[11]=scratch;r[12]=spu_ai(input,16);
    r[13]=spu_ls_read128(&c,0xd960);r[14]=bytes;r[15]=maximum;r[16]=minimum;r[17]=attribute;
    r[18]=spu_fsm(direct);r[19]=selectors[2];r[20]=high;r[21]=scale;r[22]=direct;r[23]=greater;
    r[24]=transformed[1].xyz;r[25]=flag16;
    r[26]=spu_shufb(scale,maximum,spu_ls_read128(&c,0xda90));r[27]=spu_shufb(scale,minimum,r[13]);
    r[28]=transformed[0].value;r[29]=low;r[30]=spu_sfi(bytes,0);
    r[32]=format;r[33]=cursor;r[34]=spu_rotqbyi(spu_ls_read128(&c,0xdaa0),8);
    r[36]=transformed[2].value;r[37]=p.path?spu_xsbh(spu_clgtbi(format,4)):selectors[1];
    r[38]=selectors[0];r[39]=selectors[3];r[40]=translated.value;
    r[41]=spu_shufb(counts,sum,spu_ls_read128(&c,0xd8d0));r[42]=spu_ls_read128(&c,0xdaa0);
    r[43]=spu_shufb(projected[1],projected[1],selectors[3]);r[44]=matrix[3];
    r[45]=spu_shufb(projected[0],projected[0],selectors[3]);r[46]=selectors[0];
    r[47]=spu_shufb(projected[2],projected[2],selectors[3]);r[48]=transformed[2].xyz;r[49]=direct;
    r[50]=spu_il(64);r[51]=spu_ilhu(0x8000);
    const unsigned destinations[4][4]={{60,55,52,62},{69,67,66,68},{63,59,54,65},{61,57,53,64}};
    const u128 columns[]={transformed[1].value,translated.value,transformed[2].value,transformed[0].value};
    for(unsigned column=0;column<4;++column)for(unsigned lane=0;lane<4;++lane)
        r[destinations[column][lane]]=spu_shufb(columns[column],columns[column],selectors[lane]);
    r[56]=p.path?spu_clgtbi(format,4):translated.xyz;
    r[58]=p.path>=2?spu_xsbh(spu_ceqbi(format,5)):batches;
    r[70]=spu_il(0);r[71]=spu_ilhu(0x3f80);
    r[77]=spu_ls_read128(&c,0xd930);r[75]=spu_ls_read128(&c,0xd940);r[76]=spu_ls_read128(&c,0xda40);
    r[74]=spu_rotqbyi(r[77],8);r[73]=spu_rotqbyi(r[75],8);r[72]=spu_rotqbyi(r[76],8);
    r[78]=p.path>=2?spu_ceqbi(format,5):flags;r[79]=counts;
    c.pc=0x63e0;g_spu_trampoline_fn=vertex;
}
}
