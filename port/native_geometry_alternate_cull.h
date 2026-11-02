/* Complete 69E8/6B18/6B10 alternate cull/compaction pipeline.
 * Host caches retain the source's prefetch stages. Every LS access preserves
 * wrap/floor semantics, including output/table/vertex feedback. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
namespace buzz {
struct GeometryAlternateCull { unsigned iterations=0; };
inline bool read_geometry_alternate_cull(const spu_context& c,GeometryAlternateCull& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable && (c.event_status&c.event_mask))return reject("pending interrupt");
    const unsigned n=c.gpr[15]._u32[0]+2;
    if(!n || n>8192)return reject("alternate cull iteration count");
    out.iterations=n;return true;
}
struct AlternateCullConstants {
    u128 vertices,pack_x,pack_y,pair_x,pair_y,pair_z,pair_w,merge,pair;
    u128 bounds_x,bounds_y,assemble_a,assemble_b,cross,pack_mask_a,pack_mask_b;
    u128 alignment,toggle,align_base,table_a,table_b,input_a,input_b,scratch,limit_a,limit_b;
    u128 threshold,selection,clip_limit,face_mask,input_step,tail_mask,normal_mask,orientation,full,empty_nibble,nibble_mask,twelve;
};
struct AlternateCullState {
    u128 delta[8]{},rows[6]{},equal[3]{},bounds[8]{},reject[6]{},previous[2]{},input[2];
    u128 pending,offset,output,advance,residue,negative,lookup,shift,kept,shuffle,cache[2]{};
};
template<bool Last> inline void step_geometry_alternate_cull(spu_context& c,const AlternateCullConstants& k,AlternateCullState& s)
{
    const u128 same=spu_and(spu_ceqh(s.equal[0],s.equal[1]),spu_ceqh(s.equal[0],s.equal[2]));
    const u128 edge_a=spu_shufb(s.delta[0],s.delta[1],k.assemble_a),edge_b=spu_shufb(s.delta[6],s.delta[7],k.assemble_b);
    const u128 edge_c=spu_shufb(s.delta[2],s.delta[3],k.assemble_a),edge_d=spu_shufb(s.delta[4],s.delta[5],k.assemble_b);
    const u128 pending=spu_ai(s.pending,-1),output=spu_a(s.output,s.advance);
    const u128 alignment_code=spu_ls_read128(&c,k.align_base._u32[0]+s.lookup._u32[0]);
    const u128 product_a=spu_fm(edge_a,edge_d),product_b=spu_fm(edge_c,edge_b);
    const u128 packed_a=spu_shufb(s.rows[0],s.rows[1],k.pair),packed_b=spu_shufb(s.rows[2],s.rows[3],k.pair),packed_c=spu_shufb(s.rows[4],s.rows[5],k.pair);
    const u128 choose_tail=spu_selb(k.normal_mask,k.tail_mask,spu_ceqi(pending,0));
    const u128 same_bits=spu_gb(spu_clgti(same,0)),zero_bits=spu_gb(spu_clgti(s.reject[2],0));
    const u128 combined_bits=spu_shufb(spu_shufb(s.reject[1],zero_bits,k.pack_mask_b),spu_shufb(s.reject[0],same_bits,k.pack_mask_a),k.pair);
    const u128 mask_bits=spu_orx(combined_bits),byte_mask=spu_shli(spu_or(s.shift,spu_or(choose_tail,mask_bits)),4);
    const u128 min_a=spu_selb(s.bounds[3],s.bounds[4],s.bounds[5]),max_a=spu_selb(s.bounds[4],s.bounds[3],s.bounds[5]);
    const u128 min_b=spu_selb(s.bounds[0],s.bounds[1],s.bounds[2]),max_b=spu_selb(s.bounds[1],s.bounds[0],s.bounds[2]);
    const u128 lower_a=spu_selb(min_a,s.bounds[7],spu_fcgt(min_a,s.bounds[7])),lower_b=spu_selb(min_b,s.bounds[6],spu_fcgt(min_b,s.bounds[6]));
    const u128 upper_a=spu_selb(s.bounds[7],max_a,spu_fcgt(max_a,s.bounds[7])),upper_b=spu_selb(s.bounds[6],max_b,spu_fcgt(max_b,s.bounds[6]));
    const u128 fixed_min_a=spu_cflts(lower_a,171),fixed_min_b=spu_cflts(lower_b,171);
    const u128 fixed_max_a=spu_cflts(upper_a,171),fixed_max_b=spu_cflts(upper_b,171);
    const u128 area=spu_shufb(spu_fs(product_a,spu_shlqbyi(product_a,4)),spu_fs(product_b,spu_shlqbyi(product_b,4)),k.cross);
    const u128 indices_a=spu_shufb(s.input[0],s.input[1],s.shuffle),indices_b=spu_shufb(s.input[1],s.input[1],s.shuffle);
    const u128 offsets_a=spu_roti(indices_a,4),offsets_b=spu_roti(indices_b,4);
    const u128 shifted_a4=spu_shlqbyi(offsets_a,4),shifted_a8=spu_shlqbyi(offsets_a,8),shifted_a12=spu_shlqbyi(offsets_a,12),shifted_b4=spu_shlqbyi(offsets_b,4);
    const u128 pre_output=spu_ls_read128(&c,output._u32[0]);
    const u128 table_b=spu_ls_read128(&c,k.table_b._u32[0]+byte_mask._u32[0]),table_a=spu_ls_read128(&c,k.table_a._u32[0]+byte_mask._u32[0]);
    const u128 output_mask=spu_selb(k.alignment,alignment_code,spu_rotqmby(k.full,s.negative));
    const u128 joined_a=spu_shufb(s.previous[1],s.previous[0],table_b),joined_b=spu_shufb(s.previous[0],s.previous[0],table_a);
    const u128 out_a=spu_shufb(pre_output,joined_a,output_mask),out_b=spu_shufb(joined_a,joined_b,alignment_code),out_c=spu_shufb(joined_b,joined_b,alignment_code);
    const u128 destination_b=spu_selb(output,k.scratch,spu_cgt(output,k.limit_a)),destination_c=spu_selb(output,k.scratch,spu_cgt(output,k.limit_b));
    const u128 input_offset=spu_a(s.offset,k.input_step);
    const u128 input_b=spu_ls_read128(&c,k.input_b._u32[0]+input_offset._u32[0]),input_a=spu_ls_read128(&c,k.input_a._u32[0]+input_offset._u32[0]);
    // Every lookup/prefetch above precedes all three stores. Vertex reads
    // below must observe the completed stores, including aliases and wrapping.
    spu_ls_write128(&c,output._u32[0],out_a);spu_ls_write128(&c,destination_b._u32[0]+16,out_b);spu_ls_write128(&c,destination_c._u32[0]+32,out_c);
    const u128 addresses[]={shifted_a4,offsets_b,spu_roti(offsets_a,16),spu_roti(shifted_a12,16),offsets_a,shifted_a12,
        spu_roti(shifted_a8,16),spu_roti(shifted_b4,16),shifted_b4,shifted_a8,spu_roti(shifted_a4,16),spu_roti(offsets_b,16)};
    u128 v[12];for(unsigned i=0;i<12;++i)v[i]=spu_ls_read128(&c,k.vertices._u32[0]+addresses[i]._u32[0]);
    const u128 fixed_pack=spu_selb(fixed_min_b,spu_shlqbyi(fixed_min_a,2),k.twelve);
    const u128 bounds_selector=spu_selb(k.empty_nibble,spu_andi(fixed_pack,15),k.nibble_mask);
    const u128 bound_x=spu_shufb(k.bounds_x,k.bounds_x,bounds_selector),bound_y=spu_shufb(k.bounds_y,k.bounds_y,bounds_selector);
    const u128 bounds_ok=spu_and(spu_cgt(bound_y,spu_sf(fixed_min_a,fixed_max_a)),spu_cgt(bound_x,spu_sf(fixed_min_b,fixed_max_b)));
    const u128 signed_area=spu_xor(area,k.orientation),front=spu_fcgt(signed_area,k.threshold),degenerate=spu_fcmeq(area,k.threshold);
    const u128 face=spu_and(spu_and(s.reject[4],front),spu_rotmai(spu_and(packed_c,spu_and(packed_b,packed_a)),-31));
    const u128 face_enabled=spu_and(face,k.face_mask),rejected=spu_or(bounds_ok,degenerate);
    const u128 x[]={spu_shufb(v[2],v[0],k.pair_x),spu_shufb(v[4],v[6],k.pair_x),spu_shufb(v[10],v[9],k.pair_x)};
    const u128 y[]={spu_shufb(v[3],v[1],k.pair_y),spu_shufb(v[5],v[7],k.pair_y),spu_shufb(v[11],v[8],k.pair_y)};
    const u128 z[]={spu_shufb(v[2],v[0],k.pair_z),spu_shufb(v[4],v[6],k.pair_z),spu_shufb(v[10],v[9],k.pair_z)};
    const u128 w[]={spu_shufb(v[3],v[1],k.pair_w),spu_shufb(v[5],v[7],k.pair_w),spu_shufb(v[11],v[8],k.pair_w)};
    const u128 flags[]={spu_or(x[0],y[0]),spu_or(x[1],y[1]),spu_or(x[2],y[2])};
    const u128 pairs[]={spu_shufb(z[0],w[0],k.merge),spu_shufb(z[1],w[1],k.merge),spu_shufb(z[2],w[2],k.merge)};
    const u128 compact[]={spu_selb(z[0],w[0],k.selection),spu_selb(z[1],w[1],k.selection),spu_selb(z[2],w[2],k.selection)};
    const u128 rows[]={spu_shufb(v[2],v[0],k.pack_x),spu_shufb(v[3],v[1],k.pack_y),spu_shufb(v[4],v[6],k.pack_x),
        spu_shufb(v[5],v[7],k.pack_y),spu_shufb(v[10],v[9],k.pack_x),spu_shufb(v[11],v[8],k.pack_y)};
    const u128 delta[]={spu_fs(v[4],v[2]),spu_fs(v[6],v[0]),spu_fs(v[5],v[3]),spu_fs(v[7],v[1]),spu_fs(v[10],v[2]),spu_fs(v[9],v[0]),spu_fs(v[11],v[3]),spu_fs(v[8],v[1])};
    const u128 byte_counts=spu_sfi(spu_cntb(byte_mask),4),advance=spu_mpyui(byte_counts,6),residual_sum=spu_a(s.residue,advance),residue=spu_andi(residual_sum,15);
    const u128 kept=spu_a(s.kept,byte_counts),lookup=spu_shli(residue,4),negative=spu_sfi(residue,0);
    const u128 flags_or=spu_or(spu_or(flags[0],flags[1]),flags[2]),flags_and=spu_and(flags[0],flags[1]),old_flags=spu_and(s.reject[3],s.reject[5]);
    const u128 clip=spu_clgt(k.clip_limit,flags_or),compare_a=spu_fcgt(pairs[0],pairs[1]),compare_b=spu_fcgt(compact[0],compact[1]);
    const u128 shift=spu_shlqbyi(s.shift,4),shuffle=spu_xor(s.shuffle,k.toggle),gb_rejected=spu_gb(rejected),gb_face=spu_gb(face_enabled);
    if constexpr(Last){auto& r=c.gpr;
        r[2]=residual_sum;r[3]=flags_or;r[4]=signed_area;r[5]=negative;r[6]=v[11];r[7]=delta[4];r[8]=flags[1];r[9]=byte_counts;r[10]=delta[5];
        r[11]=compare_a;r[12]=gb_face;r[13]=delta[3];r[14]=compact[1];r[15]=gb_rejected;r[16]=w[2];r[17]=delta[0];r[18]=face_enabled;r[19]=delta[1];r[20]=v[4];r[21]=lookup;
        r[22]=s.cache[0];r[23]=delta[2];r[24]=kept;r[25]=s.cache[1];r[26]=flags[0];r[27]=delta[7];r[28]=delta[6];r[29]=old_flags;r[30]=w[1];r[31]=compare_b;r[32]=w[0];r[33]=compact[0];r[34]=compact[2];
        r[35]=pending;r[37]=output;r[38]=s.cache[0];r[39]=input_b;r[40]=pending;r[41]=residue;r[42]=pairs[1];r[43]=advance;r[44]=pairs[0];r[46]=pairs[2];r[48]=input_offset;r[49]=shift;r[50]=kept;
        r[55]=packed_a;r[58]=shuffle;r[59]=indices_b;r[60]=packed_c;r[63]=indices_a;r[65]=flags[2];r[66]=packed_b;
        r[72]=rows[5];r[73]=rows[3];r[74]=rows[2];r[75]=rows[4];r[76]=rows[1];r[77]=rows[0];r[79]=input_a;r[86]=flags_and;r[87]=clip;r[92]=s.cache[1];
    }
    for(unsigned i=0;i<8;++i)s.delta[i]=delta[i];for(unsigned i=0;i<6;++i)s.rows[i]=rows[i];
    s.equal[0]=packed_a;s.equal[1]=packed_c;s.equal[2]=packed_b;
    const u128 bounds[]={compact[0],compact[1],compare_b,pairs[0],pairs[1],compare_a,compact[2],pairs[2]};
    for(unsigned i=0;i<8;++i)s.bounds[i]=bounds[i];
    s.reject[0]=gb_rejected;s.reject[1]=gb_face;s.reject[2]=old_flags;s.reject[3]=flags_and;s.reject[4]=clip;s.reject[5]=flags[2];
    s.previous[0]=s.cache[0];s.previous[1]=s.cache[1];s.input[0]=input_a;s.input[1]=input_b;
    s.pending=pending;s.offset=input_offset;s.output=output;s.advance=advance;s.residue=residue;s.negative=negative;s.lookup=lookup;s.shift=shift;s.kept=kept;s.shuffle=shuffle;
    if constexpr(!Last){s.cache[0]=indices_b;s.cache[1]=indices_a;}
}
inline void cull_geometry_alternate(spu_context& c,const GeometryAlternateCull& p,buzz_geometry_job_fn finish)
{
    auto& r=c.gpr;auto load=[&](unsigned at){return spu_ls_read128(&c,at);};
    const u128 configuration=load(r[81]._u32[0]),mode=spu_rothmi(spu_andi(spu_rotqbyi(configuration,3),255),-4);
    const u128 pick=spu_or(spu_ceqi(mode,0),spu_ceqi(mode,2)),flip=spu_and(spu_shufb(pick,pick,r[12]),spu_ilhu(0x8000));
    const AlternateCullConstants k{r[10],r[51],r[45],r[52],r[47],r[78],r[56],r[54],r[53],load(0xd000),load(0xd010),load(0xd050),load(0xd060),load(0xd070),r[94],r[100],
        load(0xd980),load(0xcff0),r[3],r[7],r[8],r[13],r[9],r[57],r[64],r[91],r[70],spu_fsmbi(0xff),r[80],r[82],r[83],r[84],r[85],spu_xor(r[11],flip),r[97],spu_ilh(0x8080),spu_il(255),spu_il(12)};
    AlternateCullState s{};s.input[0]=load(r[13]._u32[0]+r[48]._u32[0]);s.input[1]=load(r[9]._u32[0]+r[48]._u32[0]);
    s.pending=spu_ai(r[15],2);s.offset=r[48];s.output=r[37];s.advance=r[43];s.residue=r[41];s.negative=r[5];s.lookup=r[21];s.shift=spu_andbi(spu_fsmbi(0x1100),15);s.kept=r[50];s.shuffle=k.alignment;
    // These setup-only vectors survive every data iteration.
    r[36]=k.vertices;r[61]=k.input_b;r[62]=k.input_a;r[67]=k.align_base;r[68]=k.table_a;r[69]=k.table_b;r[71]=k.selection;
    r[88]=k.bounds_x;r[89]=k.bounds_y;r[90]=k.orientation;r[93]=k.alignment;r[95]=k.assemble_b;r[96]=k.assemble_a;r[98]=k.toggle;r[99]=k.empty_nibble;r[101]=k.nibble_mask;r[102]=k.twelve;r[103]=k.cross;
    for(unsigned i=1;i<p.iterations;++i)step_geometry_alternate_cull<false>(c,k,s);
    step_geometry_alternate_cull<true>(c,k,s);c.pc=0x6958;g_spu_trampoline_fn=finish;
}
}
