/* Complete 7A90 triangle stitching and frame restoration through queued 2738. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
#include "native_geometry_shuffle.h"
namespace buzz {
struct GeometryIndexTriangles { unsigned iterations=0; };
inline bool read_geometry_index_triangles(const spu_context& c,GeometryIndexTriangles& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    const unsigned n=c.gpr[22]._u32[0],first=c.gpr[18]._u32[0];
    if(!n)return reject("zero triangle iteration count");
    if(first>=SPU_LS_SIZE || (first&~15u)<0xe680 || uint64_t(first&~15u)+48>SPU_LS_SIZE)
        return reject("first triangle output span");
    if(n>1){
        const unsigned next=first+c.gpr[5]._u32[0];
        if(next>=SPU_LS_SIZE || (next&~15u)<0xe680 || uint64_t(next&~15u)+uint64_t(n-1)*48>SPU_LS_SIZE)
            return reject("remaining triangle output span");
    }
    out.iterations=n;return true;
}
struct IndexTriangleLookup { u128 raw;BuzzGeometryShuffle mask; };
struct IndexTriangleTables {
    IndexTriangleLookup entries[32];unsigned tags[32],ready=0;
    inline IndexTriangleLookup read(spu_context& c,unsigned address)
    {
        const unsigned physical=address&(SPU_LS_SIZE-16),slot=(physical>>4)&31,bit=1u<<slot;
        // Only protected low-LS quads are immutable. High tables can alias
        // output and must be reread, including the post-store LUT reads.
        if(physical>=0xe680){const u128 raw=spu_ls_read128(&c,address);return {raw,buzz_geometry_shuffle_prepare(raw)};}
        if(!(ready&bit)||tags[slot]!=physical){
            const u128 raw=spu_ls_read128(&c,address);entries[slot]={raw,buzz_geometry_shuffle_prepare(raw)};
            tags[slot]=physical;ready|=bit;
        }
        return entries[slot];
    }
};
struct IndexTriangleConstants {
    BuzzGeometryShuffle pack0,pack1,pack2,alignment;
    u128 high_mask,middle_mask,population_mask,alignment_base;
    unsigned table_a,table_b,codes;
};
struct IndexTriangleState {
    u128 q0,q1,q2,q3,previous,input,add,population,output,step,bits,remaining;
    u128 index0,index1,index2,control,control19,control29;
    BuzzGeometryShuffle select19,select29;
};
template<bool Last> inline void step_geometry_index_triangles(spu_context& c,const IndexTriangleConstants& k,
                                                             IndexTriangleTables& tables,IndexTriangleState& s)
{
    const u128 old_index2=s.index2;
    const auto table76=tables.read(c,k.table_a+s.index1._u32[0]);
    const u128 code_quad=spu_ls_read128(&c,k.codes+s.bits._u32[0]);
    const u128 output_next=spu_a(s.output,s.step),six_population=spu_mpyi(s.population,6);
    const auto aligned=buzz_geometry_shuffle_prepare(s.control);
    const u128 join7=buzz_geometry_shuffle_apply(s.q1,s.q2,aligned),join13=buzz_geometry_shuffle_apply(s.q2,s.q3,aligned);
    const u128 join90=buzz_geometry_shuffle_apply(s.q0,s.q1,aligned),bits_next=spu_ai(s.bits,2),remaining=spu_ai(s.remaining,-1);
    const auto table14=tables.read(c,k.table_b+s.index0._u32[0]),table9=tables.read(c,k.table_a+s.index0._u32[0]);
    const u128 join93=buzz_geometry_shuffle_apply(join7,join13,s.select19),join5=buzz_geometry_shuffle_apply(join90,join7,s.select19);
    const u128 join88=buzz_geometry_shuffle_apply(s.previous,join90,s.select29),advance=spu_a(six_population,s.add);
    const u128 join11=buzz_geometry_shuffle_apply(join13,join13,s.select19),code=spu_rotqby(code_quad,s.bits);
    const u128 input_next=spu_a(advance,s.input);
    const auto table82=tables.read(c,k.table_b+s.index2._u32[0]),table78=tables.read(c,k.table_a+s.index2._u32[0]);
    const u128 low=spu_andi(input_next,15),join91=buzz_geometry_shuffle_apply(join88,join5,table9.mask),twice_code=spu_a(code,code);
    const u128 join77=buzz_geometry_shuffle_apply(join5,join93,table14.mask),high=spu_and(code,k.high_mask);
    const u128 join97=buzz_geometry_shuffle_apply(join93,join11,table14.mask),population_bits=spu_and(code,twice_code);
    const u128 q3=spu_ls_read128(&c,input_next._u32[0]+48),high_index=spu_rotmi(high,-8);
    const u128 output0=buzz_geometry_shuffle_apply(join88,join91,k.pack0),masked_population=spu_and(population_bits,k.population_mask);
    const u128 join74=buzz_geometry_shuffle_apply(join91,join77,table78.mask),low_nibble=spu_andi(code,15);
    const u128 join89=buzz_geometry_shuffle_apply(join77,join97,table82.mask),byte_counts=spu_cntb(masked_population);
    const u128 q0=spu_ls_read128(&c,input_next._u32[0]),middle=spu_and(code,k.middle_mask);
    const u128 q1=spu_ls_read128(&c,input_next._u32[0]+16),index1=spu_shli(low_nibble,4);
    const u128 output1=buzz_geometry_shuffle_apply(join91,join74,k.pack1),index0=spu_rotmi(middle,-4);
    const u128 previous=buzz_geometry_shuffle_apply(join74,join89,table76.mask),high_population=spu_rotmi(byte_counts,-8);
    const u128 q2=spu_ls_read128(&c,input_next._u32[0]+32),low_population=spu_andi(byte_counts,255);
    spu_ls_write128(&c,s.output._u32[0],output0);spu_ls_write128(&c,s.output._u32[0]+16,output1);
    const u128 output2=buzz_geometry_shuffle_apply(join74,previous,k.pack2),population=spu_a(high_population,low_population);
    const u128 offset=buzz_geometry_shuffle_apply(low,low,k.alignment),index2=spu_andi(code,240),skip=spu_sfi(population,8);
    spu_ls_write128(&c,s.output._u32[0]+32,output2);
    const u128 add=spu_a(skip,skip);
    const auto table19=tables.read(c,k.table_b+high_index._u32[0]);
    const u128 control=spu_a(offset,k.alignment_base);
    const auto table29=tables.read(c,k.table_a+high_index._u32[0]);
    if constexpr(Last){auto& r=c.gpr;
        // Registers 80..103 are restored from the final LS frame below.
        r[51]=old_index2;r[76]=table76.raw;r[18]=output_next;r[8]=index2;r[7]=index1;r[42]=six_population;r[13]=join13;
        r[22]=remaining;r[14]=table14.raw;r[9]=table9.raw;r[5]=spu_il(48);r[11]=join11;r[28]=bits_next;r[78]=table78.raw;
        r[2]=low;r[41]=twice_code;r[77]=join77;r[6]=high;r[19]=table19.raw;r[30]=q3;r[40]=output0;
        r[74]=join74;r[75]=low_nibble;r[79]=byte_counts;r[32]=q0;r[26]=q1;r[49]=output1;r[3]=index0;
        r[23]=previous;r[25]=q2;r[39]=low_population;r[20]=input_next;r[47]=output2;r[12]=population;r[48]=skip;
        r[21]=add;r[10]=control;r[29]=table29.raw;
    }
    s.q0=q0;s.q1=q1;s.q2=q2;s.q3=q3;s.previous=previous;s.input=input_next;s.add=add;s.population=population;
    s.output=output_next;s.step=spu_il(48);s.bits=bits_next;s.remaining=remaining;s.index0=index0;s.index1=index1;s.index2=index2;
    s.control=control;s.control19=table19.raw;s.control29=table29.raw;s.select19=table19.mask;s.select29=table29.mask;
}
inline void stitch_geometry_index_triangles(spu_context& c,const GeometryIndexTriangles& p,buzz_geometry_job_fn publish)
{
    auto& r=c.gpr;
    const IndexTriangleConstants k{buzz_geometry_shuffle_prepare(r[35]),buzz_geometry_shuffle_prepare(r[34]),buzz_geometry_shuffle_prepare(r[33]),
        buzz_geometry_shuffle_prepare(r[85]),r[36],r[37],r[38],r[83],r[24]._u32[0],r[27]._u32[0],r[31]._u32[0]};
    IndexTriangleState s{r[32],r[26],r[25],r[30],r[23],r[20],r[21],r[12],r[18],r[5],r[28],r[22],
        r[3],r[7],r[8],r[10],r[19],r[29],buzz_geometry_shuffle_prepare(r[19]),buzz_geometry_shuffle_prepare(r[29])};
    IndexTriangleTables tables;
    for(unsigned i=1;i<p.iterations;++i)step_geometry_index_triangles<false>(c,k,tables,s);
    step_geometry_index_triangles<true>(c,k,tables,s);
    // Frame/output aliases are observed AFTER the final stores. Copy manager
    // and aligned publication pointer before restoring their nonvolatile GPRs.
    r[1]=spu_ai(r[1],432);const u128 aligned=spu_ai(r[18],15);r[3]=r[95];
    r[126]=spu_ls_read128(&c,r[1]._u32[0]-0x190);r[4]=spu_andi(aligned,-16);r[0]=spu_ls_read128(&c,r[1]._u32[0]+16);
    for(unsigned reg=80;reg<=103;++reg)r[reg]=spu_ls_read128(&c,r[1]._u32[0]-(reg-79)*16);
    c.pc=0x2738;g_spu_trampoline_fn=publish;
}
}
