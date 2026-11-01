/* Complete storage-2 output: columns, both masked scatter arms and final ABI.
 * Raw float bits stay raw. Loads precede ordered stores even for aliases.
 * Host-local vectors replace all five original blocks, including the last
 * iteration; 4610/4618 are explicit formatter continuations. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
namespace buzz {
struct GeometryFloatOutput { unsigned columns=0,groups=0,cross_columns=0; };
inline void open_geometry_float_output(u128* r)
{
    r[28]=r[75];r[27]=r[74];r[26]=r[73];r[19]=r[67];r[22]=r[76];r[25]=r[3];
    r[32]=spu_il(0);r[44]=spu_fsmbi(0);r[47]=spu_ila(0x10203);
}
inline bool prepare_geometry_float_output_column(u128* r)
{
    const auto column=spu_shufb(r[32],r[32],r[47]),offset=spu_shufb(r[44],r[44],r[47]);
    const auto index=spu_a(column,r[54]),source=spu_a(offset,r[52]),route=spu_mpya(r[55],index,r[53]);
    const auto low=spu_andi(route,15),right=spu_sfi(low,0),extent=spu_a(low,r[48]);
    const auto bits=spu_rotm(r[45],right),shifted=spu_rotqmby(r[63],right),cross=spu_cgti(extent,16),mask=spu_fsmb(bits);
    r[13]=column;r[11]=offset;r[10]=index;r[21]=source;r[23]=route;r[5]=low;r[7]=right;r[3]=extent;
    r[2]=bits;r[6]=shifted;r[62]=cross;r[8]=mask;r[20]=spu_selb(r[49],shifted,mask);
    if(!cross._u32[0])return false;
    r[15]=spu_sfi(low,16);r[12]=spu_shl(r[45],r[15]);r[14]=spu_shlqby(r[63],r[15]);
    r[4]=spu_fsmb(r[12]);r[24]=spu_selb(r[49],r[14],r[4]);return true;
}
inline bool read_geometry_float_output(const spu_context& c,GeometryFloatOutput& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    // The original drainer polls between blocks. A new interrupt must never
    // arrive in an admitted region that has fused those poll boundaries.
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    if(!c.gpr[77]._u32[0]){out={};return true;}
    const unsigned columns=c.gpr[60]._u32[0]?c.gpr[60]._u32[0]:1;
    if(columns>256)return reject("float output column budget");
    u128 r[128];memcpy(r,c.gpr,sizeof r);open_geometry_float_output(r);unsigned total=0,cross_columns=0;
    for(unsigned col=0;col<columns;++col){
        const bool cross=prepare_geometry_float_output_column(r);cross_columns+=cross;unsigned groups=0;
        for(;;){
            if(++groups>65536 || ++total>1048576)return reject("float output iteration budget");
            const auto after=spu_cgt(r[21],r[25]),chosen=spu_selb(r[23],r[30],after);
            for(unsigned lane=0;lane<4;++lane){
                const unsigned physical=(r[19]._u32[0]+chosen._u32[lane])&0x3fff0;
                if(physical<0xe680)return reject("float output protected first quad");
                if(cross && ((r[22]._u32[0]+chosen._u32[lane])&0x3fff0)<0xe680)return reject("float output protected second quad");
            }
            if(after._u32[0])break;
            r[21]=spu_a(r[21],r[29]);r[23]=spu_a(r[23],r[31]);
        }
        r[32]=spu_ai(r[32],1);r[44]=spu_ai(r[44],16);
    }
    out={columns,total,cross_columns};return true;
}
inline void step_geometry_float_output(spu_context& c,u128* r,bool cross)
{
    const auto after=spu_cgt(r[21],r[25]),chosen=spu_selb(r[23],r[30],after);
    const auto next_source=spu_a(r[21],r[29]),next_route=spu_a(r[23],r[31]);
    const auto lane1=spu_rotqbyi(chosen,4),lane2=spu_rotqbyi(chosen,8),lane3=spu_rotqbyi(chosen,12);
    // All source and destination quads are fetched before any store. Two
    // logical fields may share a physical quad: never merge their stores.
    u128 input[4],old[4],second[4];const unsigned offsets[]={0,r[28]._u32[0],r[27]._u32[0],r[26]._u32[0]};
    for(unsigned lane=0;lane<4;++lane)input[lane]=spu_ls_read128(&c,r[21]._u32[0]+offsets[lane]);
    for(unsigned lane=0;lane<4;++lane)old[lane]=spu_ls_read128(&c,r[19]._u32[0]+chosen._u32[lane]);
    if(cross)for(unsigned lane=0;lane<4;++lane)second[lane]=spu_ls_read128(&c,r[22]._u32[0]+chosen._u32[lane]);
    u128 value[4];for(unsigned lane=0;lane<4;++lane)value[lane]=spu_shufb(input[lane],old[lane],r[20]);
    for(unsigned lane=0;lane<4;++lane)spu_ls_write128(&c,r[19]._u32[0]+chosen._u32[lane],value[lane]);
    if(cross){
        u128 tail[4];for(unsigned lane=0;lane<4;++lane)tail[lane]=spu_shufb(input[lane],second[lane],r[24]);
        for(unsigned lane=0;lane<4;++lane)spu_ls_write128(&c,r[22]._u32[0]+chosen._u32[lane],tail[lane]);
        r[9]=chosen;r[10]=input[0];r[6]=lane1;r[11]=input[1];r[62]=lane3;r[12]=input[2];r[2]=lane2;r[13]=input[3];
        r[18]=after;r[7]=tail[1];r[16]=second[0];r[4]=value[2];r[5]=tail[0];r[17]=tail[3];r[3]=value[3];r[15]=second[2];r[8]=tail[2];r[14]=second[3];
    }else{
        r[14]=lane3;r[13]=value[3];r[7]=input[0];r[2]=chosen;r[24]=after;r[18]=next_route;r[62]=input[1];
        r[10]=lane1;r[15]=lane2;r[9]=old[0];r[6]=old[1];r[5]=input[2];r[4]=old[2];r[17]=input[3];r[3]=old[3];r[16]=value[0];r[8]=value[1];r[11]=value[2];
    }
    r[23]=next_route;r[21]=next_source;
}
// Execute an admitted plan on 128 initialized host vectors. Only the supplied
// bank and LS are updated; PC and trampoline are left to the ABI caller.
// The bank must be disjoint from LS, as in the existing local-bank emitter.
inline bool emit_geometry_float_output_registers(spu_context& c,u128* r,const GeometryFloatOutput& p)
{
    if(!p.columns)return false;
    open_geometry_float_output(r);
    for(unsigned col=0;col<p.columns;++col){
        const bool cross=prepare_geometry_float_output_column(r);
        for(;;){const bool last=spu_cgt(r[21],r[25])._u32[0]!=0;step_geometry_float_output(c,r,cross);if(last)break;}
        r[32]=spu_ai(r[32],1);r[44]=spu_ai(r[44],16);r[20]=spu_clgt(r[60],r[32]);
    }
    return true;
}
inline void emit_geometry_float_output(spu_context& c,const GeometryFloatOutput& p,buzz_geometry_job_fn finish,buzz_geometry_job_fn skip)
{
    if(!p.columns){c.pc=0x4618;g_spu_trampoline_fn=skip;return;}
    u128 r[128];memcpy(r,c.gpr,sizeof r);
    emit_geometry_float_output_registers(c,r,p);
    memcpy(c.gpr,r,sizeof r);c.pc=0x4610;g_spu_trampoline_fn=finish;
}
}
