/* Complete fixed-width index unpacker, 74F0 through the 7730/77F8 boundary.
 * Loop-carried host vectors retain both warmups and alias ordering; the final
 * native step materializes the entire SPU ABI without an original callback. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"

namespace buzz {
struct GeometryIndexUnpack { unsigned width=0, iterations=0; };
inline bool read_geometry_index_unpack(const spu_context& c, GeometryIndexUnpack& out, const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    const unsigned width=c.gpr[82]._u32[0], packed=c.gpr[97]._u32[0];
    if(!width || width>16 || packed>65535 || c.gpr[96]._u32[0]>65535)return reject("width/count/packed offset");
    GeometryIndexUnpack p{width,(packed+(width>8?10:8)*width)/(4*width)+1};
    // Tables and cached selectors are inputs too. Simulate every output
    // offset rather than assuming the usual two zero steps followed by -64.
    u128 offset=c.gpr[80], increment=spu_ls_read128(&c,0xd120);
    const unsigned bases[]={c.gpr[88]._u32[0],c.gpr[94]._u32[0],c.gpr[93]._u32[0],c.gpr[86]._u32[0]};
    for(unsigned i=0;i<p.iterations;++i){
        for(unsigned base:bases){const unsigned address=base+offset._u32[0];
            if(address>=SPU_LS_SIZE || (address&~15u)<0xe680)return reject("unpack output span");}
        offset=spu_a(offset,increment);increment=spu_shufb(increment,increment,c.gpr[99]);
    }
    out=p;return true;
}

inline void initialize_geometry_index_unpack(spu_context& c,bool high)
{
    auto& r=c.gpr;
    const u128 half_width=r[81],word_width=r[82],position_table=spu_ls_read128(&c,0xd110);
    const u128 sign_shift=spu_sfhi(half_width,1),sign=spu_ilh(0x8000);
    const u128 shift_mask=spu_rothma(sign,sign_shift);
    const u128 bit_step=spu_shli(word_width,2),byte_step=spu_sfi(bit_step,0);
    const u128 low_product=spu_mpy(half_width,position_table),high_product=spu_mpyhh(half_width,position_table);
    const u128 high_bits=spu_shli(high_product,16),positions=spu_or(high_bits,low_product);
    const u128 bit_offsets=spu_rotqmbyi(positions,-2),bytes=spu_rothmi(bit_offsets,-3),bits=spu_andhi(bit_offsets,7);
    r[39]=sign_shift;r[40]=position_table;r[38]=sign;r[14]=word_width;r[31]=spu_clgti(word_width,8);
    r[36]=shift_mask;r[35]=bit_step;r[37]=high_product;r[34]=low_product;r[25]=spu_roth(shift_mask,half_width);
    r[19]=byte_step;r[33]=high_bits;r[32]=positions;r[13]=bit_offsets;r[9]=bytes;r[3]=bits;
    if(!high){
        const u128 remaining=spu_sfhi(half_width,8),word_remaining=spu_sfhi(half_width,16);
        const u128 extra_byte=spu_cgth(bits,remaining),byte_carry=spu_andhi(extra_byte,1);
        const u128 selectors=spu_a(bytes,byte_carry),previous=spu_ahi(selectors,-1);
        const u128 positive=spu_cgthi(selectors,0),shifted=spu_shlqbyi(previous,1),combined=spu_and(positive,shifted);
        const u128 first=spu_or(selectors,combined),second=spu_a(first,r[90]),third=spu_a(second,r[90]);
        const u128 double_step=spu_a(byte_step,byte_step),source=spu_a(r[98],double_step);
        const u128 offset=spu_sf(double_step,r[97]),input=spu_a(source,offset),low=spu_andi(input,15);
        const u128 half_steps=spu_shlhi(word_remaining,2),word_steps=spu_shlhi(half_steps,8);
        const u128 shift=spu_sfh(bit_offsets,remaining),shift_bits=spu_andhi(shift,7),zero{};
        r[55]=remaining;r[58]=word_remaining;r[14]=spu_ls_read128(&c,0xd120);r[60]=extra_byte;
        r[45]=r[94];r[57]=double_step;r[44]=r[93];r[59]=byte_carry;r[43]=r[86];r[46]=half_steps;r[31]=r[88];
        r[51]=selectors;r[9]=zero;r[41]=source;r[56]=previous;r[26]=zero;r[53]=positive;r[30]=source;
        r[10]=offset;r[54]=shifted;r[48]=shift;r[15]=zero;r[52]=input;r[11]=offset;r[47]=word_steps;
        r[23]=zero;r[50]=combined;r[8]=zero;r[49]=low;r[16]=zero;r[29]=first;r[18]=zero;r[42]=shift_bits;
        r[13]=spu_shufb(low,low,r[85]);r[28]=second;r[17]=zero;r[27]=spu_or(word_steps,half_steps);
        r[22]=third;r[12]=spu_sfhi(shift_bits,0);r[24]=spu_a(third,r[90]);r[21]=spu_ai(source,16);r[20]=spu_ai(source,32);
        return;
    }
    const u128 remaining=spu_sfhi(half_width,16),lanes=spu_ls_read128(&c,0xd9e0);
    const u128 broadcast=spu_ilh(0x202),odd_lanes=spu_a(lanes,broadcast);
    const u128 word_shuffle=spu_ls_read128(&c,0xd8f0),half_shuffle=spu_ls_read128(&c,0xd8e0);
    const u128 extra=spu_cgth(bits,remaining),threshold=spu_shli(spu_ahi(extra,1),3),negative_threshold=spu_sfhi(threshold,0);
    const u128 shift_bits=spu_andhi(spu_ah(spu_ahi(half_width,-8),bit_offsets),7);
    const u128 low_bytes=spu_shufb(bytes,bytes,lanes),high_bytes=spu_shufb(bytes,bytes,odd_lanes),identity=spu_ila(0x10203);
    const u128 word_shifts=spu_shufb(negative_threshold,negative_threshold,word_shuffle);
    const u128 half_shifts=spu_shufb(negative_threshold,negative_threshold,half_shuffle);
    const u128 first=spu_a(high_bytes,identity),second=spu_a(low_bytes,identity);
    const u128 shifted_first=spu_rotm(first,word_shifts),shifted_second=spu_rotm(second,half_shifts);
    const u128 double_step=spu_a(byte_step,byte_step),source=spu_a(r[98],double_step);
    const u128 stride=spu_sfi(spu_a(word_width,word_width),0),before=spu_sf(double_step,r[97]),offset=spu_sf(stride,before);
    const u128 input=spu_a(source,offset),low=spu_andi(input,15);
    const u128 amount=spu_ai(spu_shufb(shift_bits,shift_bits,word_shuffle),-8);
    const u128 exact_byte=spu_ceqi(amount,-8),adjust=spu_andi(exact_byte,8);
    const u128 double_remaining=spu_ah(remaining,remaining),high_remaining=spu_shlhi(double_remaining,8),zero{};
    r[8]=remaining;r[21]=broadcast;r[12]=spu_ahi(extra,1);r[43]=r[86];r[45]=r[94];r[44]=r[93];
    r[11]=low_bytes;r[20]=odd_lanes;r[31]=r[88];r[3]=identity;r[86]=negative_threshold;r[13]=shift_bits;
    r[28]=spu_shufb(shift_bits,shift_bits,half_shuffle);r[6]=double_step;r[9]=spu_a(word_width,word_width);
    r[94]=word_shifts;r[93]=first;r[4]=half_shifts;r[7]=before;r[29]=zero;r[34]=stride;
    r[30]=spu_ls_read128(&c,0xd120);r[24]=zero;r[76]=source;r[22]=r[101];r[81]=amount;
    r[47]=zero;r[23]=offset;r[42]=source;r[77]=double_remaining;r[15]=zero;r[82]=second;
    r[17]=offset;r[33]=spu_rotmi(shifted_first,-8);r[46]=zero;r[14]=input;r[5]=zero;r[97]=exact_byte;
    r[48]=zero;r[78]=high_remaining;r[49]=zero;r[32]=shifted_second;r[19]=zero;r[2]=low;r[16]=zero;
    r[79]=adjust;r[38]=zero;r[41]=spu_or(high_remaining,double_remaining);r[26]=spu_shufb(low,low,r[85]);
    r[27]=spu_a(adjust,amount);r[37]=zero;r[40]=spu_a(r[33],r[90]);r[39]=spu_a(shifted_second,r[90]);
    r[36]=spu_ai(source,16);r[35]=spu_ai(source,32);
}

struct IndexUnpackLowState {
    u128 values[4],raw[3],align,bits,offset,output,increment;
};
template<bool Last> inline void step_geometry_index_unpack_low(spu_context& c,IndexUnpackLowState& s)
{
    auto& r=c.gpr;
    const u128 first=spu_shufb(s.raw[0],s.raw[1],s.align),second=spu_shufb(s.raw[1],s.raw[2],s.align);
    const u128 next_offset=spu_a(s.offset,r[19]),next_output=spu_a(s.output,s.increment);
    u128 raw[3];raw[2]=spu_ls_read128(&c,r[20]._u32[0]+s.offset._u32[0]);
    raw[0]=spu_ls_read128(&c,r[30]._u32[0]+s.offset._u32[0]);raw[1]=spu_ls_read128(&c,r[21]._u32[0]+s.offset._u32[0]);
    const u128 merged[]={spu_shufb(first,second,r[29]),spu_shufb(first,second,r[28]),
                         spu_shufb(first,second,r[22]),spu_shufb(first,second,r[24])};
    u128 output[4],values[4];for(unsigned i=0;i<4;++i){output[i]=spu_and(s.values[i],r[25]);values[i]=spu_rothm(merged[i],r[12]);}
    const u128 align=spu_a(s.bits,r[83]),next_bits=spu_ah(s.bits,r[27]);
    const u128 increment=spu_shufb(s.increment,s.increment,r[99]);
    const unsigned bases[]={31,45,44,43};
    for(unsigned i=0;i<4;++i)spu_ls_write128(&c,r[bases[i]]._u32[0]+s.output._u32[0],output[i]);
    if constexpr(Last){
        r[75]=output[0];r[69]=second;r[74]=output[1];r[68]=first;r[73]=output[2];r[72]=output[3];
        r[62]=next_output;r[66]=merged[3];r[64]=align;r[70]=merged[0];r[63]=next_bits;
        r[67]=merged[1];r[61]=spu_cgti(next_offset,-1);r[65]=merged[2];
        r[10]=r[11]=next_offset;r[23]=raw[2];r[26]=raw[0];r[15]=raw[1];
        r[17]=values[3];r[8]=values[0];r[16]=values[1];r[18]=values[2];
        r[14]=increment;r[9]=align;r[80]=next_output;r[13]=spu_andbi(next_bits,15);
    }
    for(unsigned i=0;i<4;++i)s.values[i]=values[i];for(unsigned i=0;i<3;++i)s.raw[i]=raw[i];
    s.offset=next_offset;s.output=next_output;s.increment=increment;s.align=align;s.bits=spu_andbi(next_bits,15);
}

struct IndexUnpackHighState {
    u128 raw[4],pair[8],bits,offset,output,increment;
};
template<bool Last> inline void step_geometry_index_unpack_high(spu_context& c,IndexUnpackHighState& s)
{
    auto& r=c.gpr;
    const u128 left0=spu_shl(s.pair[4],r[28]),right0=spu_rot(s.pair[5],r[27]);
    const u128 left1=spu_shl(s.pair[6],r[28]),right1=spu_rot(s.pair[7],r[27]);
    const u128 selected[]={spu_selb(s.pair[0],s.pair[1],r[22]),spu_selb(s.pair[2],s.pair[3],r[22]),
                           spu_selb(left0,right0,r[22]),spu_selb(left1,right1,r[22])};
    u128 output[4];for(unsigned i=0;i<4;++i)output[i]=spu_and(selected[i],r[25]);
    const u128 first=spu_shufb(s.raw[0],s.raw[0],r[32]),second=spu_shufb(s.raw[0],s.raw[0],r[33]);
    const u128 joined0=spu_shufb(s.raw[0],s.raw[3],r[39]),joined1=spu_shufb(s.raw[0],s.raw[3],r[40]);
    const u128 middle=spu_a(s.offset,r[34]),next_offset=spu_a(middle,r[34]);
    const u128 load56=spu_ls_read128(&c,r[36]._u32[0]+s.offset._u32[0]);
    const u128 load61=spu_ls_read128(&c,r[35]._u32[0]+s.offset._u32[0]);
    const u128 load10=spu_ls_read128(&c,r[42]._u32[0]+s.offset._u32[0]);
    const u128 load53=spu_ls_read128(&c,r[36]._u32[0]+middle._u32[0]);
    const u128 load19=spu_ls_read128(&c,r[42]._u32[0]+middle._u32[0]);
    const u128 load60=spu_ls_read128(&c,r[35]._u32[0]+middle._u32[0]);
    const u128 moved=spu_ah(s.bits,r[41]),low=spu_andbi(moved,15);
    const u128 align0=spu_a(s.bits,r[83]),align1=spu_a(low,r[83]),next_bits=spu_ah(low,r[41]);
    const u128 raw[]={spu_shufb(load10,load56,align0),spu_shufb(load19,load53,align1),
                      spu_shufb(load53,load60,align1),spu_shufb(load56,load61,align0)};
    const u128 pair[]={spu_shl(first,r[28]),spu_rot(second,r[27]),spu_shl(joined0,r[28]),spu_rot(joined1,r[27]),
                       spu_shufb(s.raw[1],s.raw[1],r[32]),spu_shufb(s.raw[1],s.raw[1],r[33]),
                       spu_shufb(s.raw[1],s.raw[2],r[39]),spu_shufb(s.raw[1],s.raw[2],r[40])};
    const u128 next_output=spu_a(s.output,s.increment),increment=spu_shufb(s.increment,s.increment,r[99]);
    const unsigned bases[]={44,43,31,45};
    for(unsigned i=0;i<4;++i)spu_ls_write128(&c,r[bases[i]]._u32[0]+s.output._u32[0],output[i]);
    if constexpr(Last){
        r[71]=left0;r[65]=first;r[72]=right0;r[18]=second;r[63]=middle;r[56]=load56;r[70]=selected[0];
        r[61]=load61;r[67]=left1;r[10]=load10;r[68]=right1;r[53]=load53;r[66]=selected[1];r[60]=load60;
        r[62]=selected[2];r[69]=output[0];r[64]=joined1;r[54]=low;r[58]=selected[3];r[59]=output[2];
        r[51]=pair[0];r[50]=pair[1];r[57]=align0;r[55]=s.output;r[52]=align1;r[98]=next_output;
        r[101]=next_bits;r[90]=spu_cgti(next_offset,-1);
        r[15]=raw[0];r[5]=raw[1];r[24]=raw[2];r[29]=raw[3];
        r[19]=pair[0];r[16]=pair[1];r[38]=pair[2];r[37]=pair[3];r[47]=pair[4];r[46]=pair[5];r[48]=pair[6];r[49]=pair[7];
        r[23]=r[17]=next_offset;r[80]=next_output;r[30]=increment;r[26]=spu_andbi(next_bits,15);
    }
    for(unsigned i=0;i<4;++i)s.raw[i]=raw[i];for(unsigned i=0;i<8;++i)s.pair[i]=pair[i];
    s.bits=spu_andbi(next_bits,15);s.offset=next_offset;s.output=next_output;s.increment=increment;
}

inline void unpack_geometry_indices(spu_context& c,const GeometryIndexUnpack& p,
                                    buzz_geometry_job_fn deltas,buzz_geometry_job_fn expansion)
{
    const bool high=p.width>8;initialize_geometry_index_unpack(c,high);auto& r=c.gpr;
    if(high){
        IndexUnpackHighState s{{r[15],r[5],r[24],r[29]},
            {r[19],r[16],r[38],r[37],r[47],r[46],r[48],r[49]},r[26],r[23],r[80],r[30]};
        for(unsigned i=1;i<p.iterations;++i)step_geometry_index_unpack_high<false>(c,s);
        step_geometry_index_unpack_high<true>(c,s);
    }else{
        IndexUnpackLowState s{{r[8],r[16],r[18],r[17]},{r[26],r[15],r[23]},r[9],r[13],r[10],r[80],r[14]};
        for(unsigned i=1;i<p.iterations;++i)step_geometry_index_unpack_low<false>(c,s);
        step_geometry_index_unpack_low<true>(c,s);
    }
    r[44]=spu_ilh(0x203);r[10]=spu_shufb(r[84],r[84],r[44]);
    c.pc=r[96]._u32[0]?0x7730:0x77f8;g_spu_trampoline_fn=r[96]._u32[0]?deltas:expansion;
}
}
