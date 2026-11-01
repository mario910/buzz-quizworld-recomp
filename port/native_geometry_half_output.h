/* Complete storage-3 binary16 output, including both scatter arms, fresh
 * post-store prefetch and final scratch vectors. No lifted last iteration. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
namespace buzz {
struct GeometryHalfOutput { unsigned columns=0,groups=0,cross_columns=0; };
struct GeometryHalfOutputPack {
    u128 input[4],rotated[4],original[2],converted[2],magnitude[2],under_max[2],clamped[2],signed_bits[2],small[2],packed[2],high_half[2];
};
inline GeometryHalfOutputPack pack_geometry_half_output(const u128* r,const u128* input)
{
    GeometryHalfOutputPack p;
    for(unsigned i=0;i<4;++i){p.input[i]=input[i];p.rotated[i]=spu_rotqbii(spu_sf(r[38],input[i]),3);}
    for(unsigned pair=0;pair<2;++pair){
        p.original[pair]=spu_shufb(input[pair*2],input[pair*2+1],r[33]);
        p.converted[pair]=spu_shufb(p.rotated[pair*2],p.rotated[pair*2+1],r[33]);
        p.magnitude[pair]=spu_andc(p.original[pair],r[37]);p.under_max[pair]=spu_cgth(r[56],p.magnitude[pair]);
        p.clamped[pair]=spu_selb(r[58],p.converted[pair],p.under_max[pair]);
        p.signed_bits[pair]=spu_selb(p.clamped[pair],p.original[pair],r[37]);p.small[pair]=spu_cgth(r[57],p.magnitude[pair]);
        p.packed[pair]=spu_andc(p.signed_bits[pair],p.small[pair]);p.high_half[pair]=spu_rotqbyi(p.packed[pair],8);
    }
    return p;
}
inline bool read_geometry_half_output(const spu_context& c,GeometryHalfOutput& out,const char** reason=nullptr,const u128* registers=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    const auto* arguments=registers?registers:c.gpr;
    if(!arguments[77]._u32[0]){out={};return true;}
    const unsigned columns=arguments[60]._u32[0]?arguments[60]._u32[0]:1;
    if(columns>256)return reject("half output column budget");
    const auto* r=arguments;const auto limit=spu_a(spu_xshw(r[29]),r[3]);unsigned total=0,cross_columns=0;
    for(unsigned col=0;col<columns;++col){
        auto cursor=spu_a(spu_a(spu_il(col*16),r[52]),r[29]);
        auto route=spu_mpya(r[55],spu_a(spu_il(col),r[54]),r[53]);
        const bool cross=spu_cgti(spu_a(spu_andi(route,15),r[48]),16)._u32[0]!=0;cross_columns+=cross;unsigned groups=0;
        for(;;){
            if(++groups>65536 || ++total>1048576)return reject("half output iteration budget");
            const auto after=spu_cgt(cursor,limit),chosen=spu_selb(route,r[30],after);
            for(unsigned lane=0;lane<4;++lane){
                if(((r[67]._u32[0]+chosen._u32[lane])&0x3fff0)<0xe680)return reject("half output protected first quad");
                if(cross && ((r[76]._u32[0]+chosen._u32[lane])&0x3fff0)<0xe680)return reject("half output protected second quad");
            }
            if(after._u32[0])break;cursor=spu_a(cursor,r[29]);route=spu_a(route,r[31]);
        }
    }
    out={columns,total,cross_columns};return true;
}
inline void cache_geometry_half_output(u128* r,const GeometryHalfOutputPack& p)
{
    r[16]=p.packed[0];r[17]=p.packed[1];r[21]=p.high_half[0];r[20]=p.high_half[1];
}
inline bool prepare_geometry_half_output_column(spu_context& c,u128* r)
{
    const auto source=spu_a(spu_shufb(r[47],r[47],r[62]),r[52]);
    const auto index=spu_a(spu_shufb(r[44],r[44],r[62]),r[54]);
    r[22]=spu_a(source,r[29]);r[24]=spu_mpya(r[55],index,r[53]);
    u128 input[4];const unsigned offsets[]={0,r[28]._u32[0],r[27]._u32[0],r[26]._u32[0]};
    for(unsigned i=0;i<4;++i)input[i]=spu_ls_read128(&c,source._u32[0]+offsets[i]);
    cache_geometry_half_output(r,pack_geometry_half_output(r,input));
    const auto low=spu_andi(r[24],15),right=spu_sfi(low,0),bits=spu_rotm(r[45],right);
    const auto shifted=spu_rotqmby(r[63],right);r[18]=spu_selb(r[49],shifted,spu_fsmb(bits));
    const auto cross=spu_cgti(spu_a(low,r[48]),16);r[25]=cross;
    if(!cross._u32[0])return false;
    const auto left=spu_sfi(low,16);r[25]=spu_selb(r[49],spu_shlqby(r[63],left),spu_fsmb(spu_shl(r[45],left)));return true;
}
template<bool Last> inline void step_geometry_half_output(spu_context& c,u128* r,bool cross)
{
    const auto after=spu_cgt(r[22],r[32]),chosen=spu_selb(r[24],r[30],after),source=r[22];
    const auto cursor=spu_a(source,r[29]),route=spu_a(r[24],r[31]);
    const u128 packed[]={r[16],r[21],r[17],r[20]};u128 old[4],second[4];
    for(unsigned i=0;i<4;++i)old[i]=spu_ls_read128(&c,r[19]._u32[0]+chosen._u32[i]);
    if(cross)for(unsigned i=0;i<4;++i)second[i]=spu_ls_read128(&c,r[23]._u32[0]+chosen._u32[i]);
    u128 value[4],tail[4];for(unsigned i=0;i<4;++i)value[i]=spu_shufb(packed[i],old[i],r[18]);
    if(cross)for(unsigned i=0;i<4;++i)tail[i]=spu_shufb(packed[i],second[i],r[25]);
    for(unsigned i=0;i<4;++i)spu_ls_write128(&c,r[19]._u32[0]+chosen._u32[i],value[i]);
    if(cross)for(unsigned i=0;i<4;++i)spu_ls_write128(&c,r[23]._u32[0]+chosen._u32[i],tail[i]);
    // The prefetch MUST observe the just-written LS, even on the final group.
    u128 input[4];const unsigned offsets[]={0,r[28]._u32[0],r[27]._u32[0],r[26]._u32[0]};
    for(unsigned i=0;i<4;++i)input[i]=spu_ls_read128(&c,source._u32[0]+offsets[i]);
    const auto p=pack_geometry_half_output(r,input);cache_geometry_half_output(r,p);r[22]=cursor;r[24]=route;
    if constexpr(Last){
        r[9]=p.input[0];
        if(cross){
            r[2]=p.clamped[0];r[3]=p.clamped[1];r[4]=p.small[0];r[5]=p.magnitude[0];r[6]=p.under_max[0];r[7]=p.under_max[1];
            r[8]=p.small[1];r[10]=p.magnitude[1];r[11]=p.converted[1];r[12]=p.converted[0];r[13]=p.original[0];r[14]=p.original[1];r[15]=after;
        }else{
            r[2]=p.converted[0];r[3]=p.clamped[1];r[4]=p.original[0];r[5]=p.converted[1];r[6]=p.under_max[0];r[7]=p.under_max[1];
            r[8]=p.original[1];r[10]=p.magnitude[1];r[11]=p.magnitude[0];r[12]=p.rotated[2];r[13]=p.clamped[0];r[14]=p.signed_bits[0];r[15]=p.signed_bits[1];r[25]=after;
        }
    }
}
// Execute an admitted plan on 128 initialized host vectors. Only the supplied
// bank and LS are updated; PC and trampoline are left to the ABI caller.
// The bank must be disjoint from the context, including LS and c.gpr.
inline bool emit_geometry_half_output_registers(spu_context& c,u128* r,const GeometryHalfOutput& p)
{
    if(!p.columns)return false;
    r[32]=spu_a(spu_xshw(r[29]),r[3]);r[28]=r[75];r[27]=r[74];r[26]=r[73];
    r[19]=r[67];r[23]=r[76];r[44]=spu_fsmbi(0);r[62]=spu_ila(0x10203);r[47]=spu_fsmbi(0);
    for(unsigned col=0;col<p.columns;++col){
        const bool cross=prepare_geometry_half_output_column(c,r);
        for(;;){
            const bool last=spu_cgt(r[22],r[32])._u32[0]!=0;
            if(last && col+1==p.columns)step_geometry_half_output<true>(c,r,cross);else step_geometry_half_output<false>(c,r,cross);
            if(last)break;
        }
        r[44]=spu_ai(r[44],1);r[47]=spu_ai(r[47],16);r[18]=spu_clgt(r[60],r[44]);
    }
    return true;
}
inline void emit_geometry_half_output(spu_context& c,const GeometryHalfOutput& p,buzz_geometry_job_fn finish,buzz_geometry_job_fn skip)
{
    if(!p.columns){c.pc=0x4618;g_spu_trampoline_fn=skip;return;}
    u128 r[128];memcpy(r,c.gpr,sizeof r);
    emit_geometry_half_output_registers(c,r,p);
    memcpy(c.gpr,r,sizeof r);c.pc=0x4610;g_spu_trampoline_fn=finish;
}
}
