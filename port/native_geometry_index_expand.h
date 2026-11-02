/* Complete 7838/78A8 flag expansion with exact native final ABI and capacity
 * continuation. The earlier phase-2 prefix and its switch remain independent. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
#include "native_geometry_shuffle.h"
namespace buzz {
struct GeometryIndexExpand { unsigned iterations=0; };
inline bool read_geometry_index_expand(const spu_context& c,GeometryIndexExpand& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    GeometryIndexExpand p{(c.gpr[82]._u32[0]>>4)+1};
    const unsigned output=c.gpr[92]._u32[0]+c.gpr[80]._u32[0];
    const uint64_t bytes=uint64_t(p.iterations>1?p.iterations-1:1)*32;
    if(output>=SPU_LS_SIZE || (output&~15u)<0xe680 || (output&~15u)+bytes>SPU_LS_SIZE)
        return reject("expansion output span");
    out=p;return true;
}
struct IndexExpandState {
    u128 input,first_count,first_skip,second_skip,picked_count,picked_control;
    u128 output,counter,sequential,flag_offset,input_step,output_step,mask;
};
struct IndexExpandConstants {
    BuzzGeometryShuffle broadcast,pick1,pick0,count,mask,alignment;
    u128 align_base,popcount_mask,selection_base,control_bias;
    unsigned flags;
};
template<bool Last> inline void step_geometry_index_expand(spu_context& c,const IndexExpandConstants& k,IndexExpandState& s)
{
    const u128 flag_quad=spu_ls_read128(&c,k.flags+s.flag_offset._u32[0]);
    const u128 twice_count=spu_a(s.first_count,s.first_count),extra_input=spu_a(twice_count,s.input);
    const u128 next1=spu_ls_read128(&c,s.input._u32[0]+16),next0=spu_ls_read128(&c,s.input._u32[0]);
    const u128 extra1=spu_ls_read128(&c,extra_input._u32[0]+16),extra0=spu_ls_read128(&c,extra_input._u32[0]);
    const u128 picked=spu_selb(s.picked_control,s.picked_count,s.mask),twice_picked=spu_a(picked,picked);
    const u128 low0=spu_andi(s.input,15),low1=spu_andi(extra_input,15);
    const u128 offset0=buzz_geometry_shuffle_apply(low0,low0,k.alignment),offset1=buzz_geometry_shuffle_apply(low1,low1,k.alignment);
    const u128 align0=spu_a(k.align_base,offset0),align1=spu_a(k.align_base,offset1);
    const u128 pick1=buzz_geometry_shuffle_apply(twice_picked,twice_picked,k.pick1),pick0=buzz_geometry_shuffle_apply(twice_picked,twice_picked,k.pick0);
    const u128 control1=spu_ahi(pick1,1),control0=spu_ahi(pick0,1);
    const u128 initial=spu_shufb(next0,next1,align0),extra=spu_shufb(extra0,extra1,align1);
    const u128 code=spu_rotqby(flag_quad,s.flag_offset),counts=spu_cntb(code),low_counts=spu_andi(counts,15);
    const u128 code_control=buzz_geometry_shuffle_apply(code,code,k.broadcast),selected=spu_and(code_control,k.popcount_mask);
    const u128 picked_count=spu_cntb(selected),control_delta=spu_sf(picked_count,k.selection_base);
    const u128 first_skip=buzz_geometry_shuffle_apply(s.first_skip,s.first_skip,k.broadcast);
    const u128 second_skip=buzz_geometry_shuffle_apply(s.second_skip,s.second_skip,k.broadcast);
    const u128 carry=spu_a(s.sequential,first_skip),sequential=spu_a(carry,second_skip);
    const u128 output0=spu_shufb(initial,s.sequential,control0),output1=spu_shufb(extra,carry,control1);
    const u128 first_count=buzz_geometry_shuffle_apply(counts,counts,k.count),mask_bits=spu_fsmb(code);
    const u128 mask=buzz_geometry_shuffle_apply(mask_bits,mask_bits,k.mask),picked_control=spu_ah(control_delta,k.control_bias);
    const u128 input=spu_a(extra_input,s.input_step),output=spu_a(s.output,s.output_step);
    const u128 flag_offset=spu_ai(s.flag_offset,2),counter=spu_ai(s.counter,-1),input_step=spu_a(low_counts,low_counts);
    const u128 skip0=spu_sfi(first_count,8),skip1=spu_sfi(low_counts,8);
    // Preserve the original high-quad-first order, including overlapping
    // explicit input, flag data, manager metadata and output workspaces.
    spu_ls_write128(&c,s.output._u32[0]+16,output1);spu_ls_write128(&c,s.output._u32[0],output0);
    if constexpr(Last){auto& r=c.gpr;
        r[65]=twice_count;r[63]=flag_quad;r[62]=low0;r[53]=next1;r[8]=extra_input;r[52]=next0;r[60]=picked;
        r[57]=offset0;r[58]=low1;r[38]=extra1;r[80]=twice_picked;r[37]=extra0;r[5]=input;r[51]=code;r[54]=align0;
        r[55]=offset1;r[81]=output;r[56]=first_skip;r[2]=flag_offset;r[101]=pick1;r[41]=counts;r[50]=align1;
        r[98]=pick0;r[93]=carry;r[12]=initial;r[13]=control1;r[90]=selected;r[42]=control0;r[94]=second_skip;
        r[88]=low_counts;r[34]=output0;r[14]=control_delta;
        r[3]=input;r[6]=skip0;r[7]=first_count;r[9]=skip1;r[10]=picked_count;r[11]=picked_control;
        r[17]=output;r[18]=counter;r[19]=sequential;r[21]=flag_offset;r[22]=input_step;r[23]=spu_il(32);r[24]=mask;
    }
    s.input=input;s.first_count=first_count;s.first_skip=skip0;s.second_skip=skip1;s.picked_count=picked_count;s.picked_control=picked_control;
    s.output=output;s.counter=counter;s.sequential=sequential;s.flag_offset=flag_offset;s.input_step=input_step;s.output_step=spu_il(32);s.mask=mask;
}
inline void expand_geometry_indices(spu_context& c,const GeometryIndexExpand& p,buzz_geometry_job_fn commands,buzz_geometry_job_fn capacity_error)
{
    auto& r=c.gpr;const u128 blocks=spu_rotmi(r[82],-4),output=spu_a(r[92],r[80]),zero{};
    r[4]=blocks;r[33]=r[80];r[30]=spu_ls_read128(&c,0xd950);r[10]=zero;r[32]=spu_ls_read128(&c,0xd9d0);
    r[82]=output;r[31]=spu_ls_read128(&c,0xd8a0);r[26]=r[81];r[19]=spu_ls_read128(&c,0xd130);
    r[17]=output;r[27]=spu_ls_read128(&c,0xd150);r[84]=output;r[28]=spu_ls_read128(&c,0xd140);r[18]=spu_ai(blocks,1);
    r[3]=r[88];r[11]=r[24]=r[6]=r[22]=r[7]=r[9]=r[23]=zero;r[21]=spu_il(6);r[20]=spu_ilh(0x203);
    r[25]=spu_orhi(r[30],257);r[29]=spu_ilh(0x808);
    const IndexExpandConstants k{buzz_geometry_shuffle_prepare(r[20]),buzz_geometry_shuffle_prepare(r[25]),buzz_geometry_shuffle_prepare(r[30]),
        buzz_geometry_shuffle_prepare(r[31]),buzz_geometry_shuffle_prepare(r[32]),buzz_geometry_shuffle_prepare(r[85]),r[83],r[28],r[27],r[29],r[26]._u32[0]};
    IndexExpandState s{r[3],r[7],r[6],r[9],r[10],r[11],r[17],r[18],r[19],r[21],r[22],r[23],r[24]};
    for(unsigned i=1;i<p.iterations;++i)step_geometry_index_expand<false>(c,k,s);
    step_geometry_index_expand<true>(c,k,s);
    // This capacity read is deliberately AFTER all output writes: manager
    // aliases must affect the same signed comparison and continuation.
    const u128 manager=spu_ls_read128(&c,r[95]._u32[0]+0x170);
    r[68]=r[87];r[30]=manager;r[69]=r[102];r[82]=r[81];r[23]=spu_a(r[81],r[68]);r[67]=spu_a(r[92],r[69]);
    r[29]=spu_ai(r[23],15);r[80]=spu_ai(r[67],-16);r[32]=spu_andi(r[29],-16);r[26]=spu_rotqbyi(manager,12);r[102]=spu_cgt(r[32],r[26]);
    const bool error=r[102]._u32[0]!=0;c.pc=error?0x7dcc:0x79a8;g_spu_trampoline_fn=error?capacity_error:commands;
}
}
