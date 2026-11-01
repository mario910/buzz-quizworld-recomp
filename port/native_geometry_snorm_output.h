/* Complete storage-1 SNORM16 output: FMA, signed16 fields, aliases and final ABI. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
namespace buzz {
struct GeometrySnormOutput { unsigned columns=0,groups=0,cross_columns=0; };
struct GeometrySnormOutputPack { u128 input[4],scaled[4],integer[4]; };
inline GeometrySnormOutputPack pack_geometry_snorm_output(const u128* r,const u128* input)
{
    GeometrySnormOutputPack p;
    for(unsigned i=0;i<4;++i){p.input[i]=input[i];p.scaled[i]=spu_fma(input[i],r[40],r[39]);p.integer[i]=spu_cflts(p.scaled[i],157);}
    return p;
}
inline bool read_geometry_snorm_output(const spu_context& c,GeometrySnormOutput& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    if(!c.gpr[77]._u32[0]){out={};return true;}
    const unsigned columns=c.gpr[60]._u32[0]?c.gpr[60]._u32[0]:1;
    if(columns>256)return reject("SNORM output column budget");
    const auto* r=c.gpr;const auto limit=spu_a(spu_xshw(r[29]),r[3]);unsigned total=0,cross_columns=0;
    for(unsigned col=0;col<columns;++col){
        auto cursor=spu_a(spu_a(spu_il(col*16),r[52]),r[29]);auto route=spu_mpya(r[55],spu_a(spu_il(col),r[54]),r[53]);
        const bool cross=spu_cgti(spu_a(spu_andi(route,15),r[48]),16)._u32[0]!=0;cross_columns+=cross;unsigned groups=0;
        for(;;){
            if(++groups>65536 || ++total>1048576)return reject("SNORM output iteration budget");
            const auto after=spu_cgt(cursor,limit),chosen=spu_selb(route,r[30],after);
            for(unsigned lane=0;lane<4;++lane){
                if(((r[67]._u32[0]+chosen._u32[lane])&0x3fff0)<0xe680)return reject("SNORM output protected first quad");
                if(cross && ((r[76]._u32[0]+chosen._u32[lane])&0x3fff0)<0xe680)return reject("SNORM output protected second quad");
            }
            if(after._u32[0])break;cursor=spu_a(cursor,r[29]);route=spu_a(route,r[31]);
        }
    }
    out={columns,total,cross_columns};return true;
}
inline void cache_geometry_snorm_output(u128* r,const GeometrySnormOutputPack& p)
{
    r[24]=p.integer[0];r[23]=p.integer[1];r[22]=p.integer[2];r[25]=p.integer[3];
}
inline bool prepare_geometry_snorm_output_column(spu_context& c,u128* r)
{
    const auto source=spu_a(spu_shufb(r[47],r[47],r[62]),r[52]),index=spu_a(spu_shufb(r[44],r[44],r[62]),r[54]);
    r[17]=spu_a(source,r[29]);r[20]=spu_mpya(r[55],index,r[53]);
    u128 input[4];const unsigned offsets[]={0,r[28]._u32[0],r[27]._u32[0],r[26]._u32[0]};
    for(unsigned i=0;i<4;++i)input[i]=spu_ls_read128(&c,source._u32[0]+offsets[i]);cache_geometry_snorm_output(r,pack_geometry_snorm_output(r,input));
    const auto low=spu_andi(r[20],15),right=spu_sfi(low,0),shifted=spu_rotqmby(r[72],right),bits=spu_rotm(r[45],right);
    r[16]=spu_selb(r[49],shifted,spu_fsmb(bits));const auto cross=spu_cgti(spu_a(low,r[48]),16);r[21]=cross;
    if(!cross._u32[0])return false;
    const auto left=spu_sfi(low,16);r[21]=spu_selb(r[49],spu_shlqby(r[72],left),spu_fsmb(spu_shl(r[45],left)));return true;
}
template<bool Last> inline void step_geometry_snorm_output(spu_context& c,u128* r,bool cross)
{
    const auto source=r[17],after=spu_cgt(source,r[32]),chosen=spu_selb(r[20],r[30],after),cursor=spu_a(source,r[29]),route=spu_a(r[20],r[31]);
    const u128 cached[]={r[24],r[23],r[22],r[25]};u128 old[4],second[4],value[4],tail[4];
    for(unsigned i=0;i<4;++i)old[i]=spu_ls_read128(&c,r[19]._u32[0]+chosen._u32[i]);
    if(cross)for(unsigned i=0;i<4;++i)second[i]=spu_ls_read128(&c,r[18]._u32[0]+chosen._u32[i]);
    for(unsigned i=0;i<4;++i)value[i]=spu_shufb(cached[i],old[i],r[16]);
    if(cross)for(unsigned i=0;i<4;++i)tail[i]=spu_shufb(cached[i],second[i],r[21]);
    for(unsigned i=0;i<4;++i)spu_ls_write128(&c,r[19]._u32[0]+chosen._u32[i],value[i]);
    if(cross)for(unsigned i=0;i<4;++i)spu_ls_write128(&c,r[18]._u32[0]+chosen._u32[i],tail[i]);
    // The original's final lookahead reads the modified LS after every store.
    u128 input[4];const unsigned offsets[]={0,r[28]._u32[0],r[27]._u32[0],r[26]._u32[0]};
    for(unsigned i=0;i<4;++i)input[i]=spu_ls_read128(&c,source._u32[0]+offsets[i]);
    const auto p=pack_geometry_snorm_output(r,input);cache_geometry_snorm_output(r,p);r[17]=cursor;r[20]=route;
    if constexpr(Last){
        r[2]=input[3];
        if(cross){
            r[3]=p.scaled[0];r[4]=input[1];r[5]=input[2];r[6]=input[0];r[7]=p.scaled[1];r[8]=tail[3];r[9]=p.scaled[2];
            r[10]=tail[2];r[11]=tail[1];r[12]=chosen;r[13]=tail[0];r[14]=p.scaled[3];r[15]=after;
        }else{
            r[3]=input[0];r[4]=spu_rotqbyi(chosen,12);r[5]=value[2];r[6]=spu_rotqbyi(chosen,4);r[7]=value[3];r[8]=value[0];r[9]=source;
            r[10]=p.scaled[3];r[11]=input[2];r[12]=p.scaled[0];r[13]=input[1];r[14]=p.scaled[1];r[15]=p.scaled[2];r[21]=after;
        }
    }
}
inline void emit_geometry_snorm_output(spu_context& c,const GeometrySnormOutput& p,buzz_geometry_job_fn finish,buzz_geometry_job_fn skip)
{
    if(!p.columns){c.pc=0x4618;g_spu_trampoline_fn=skip;return;}
    u128 r[128];memcpy(r,c.gpr,sizeof r);r[32]=spu_a(spu_xshw(r[29]),r[3]);r[28]=r[75];r[27]=r[74];r[26]=r[73];
    r[19]=r[67];r[18]=r[76];r[44]=spu_fsmbi(0);r[47]=spu_fsmbi(0);r[62]=spu_ila(0x10203);
    for(unsigned col=0;col<p.columns;++col){
        const bool cross=prepare_geometry_snorm_output_column(c,r);
        for(;;){const bool last=spu_cgt(r[17],r[32])._u32[0]!=0;
            if(last && col+1==p.columns)step_geometry_snorm_output<true>(c,r,cross);else step_geometry_snorm_output<false>(c,r,cross);if(last)break;}
        r[44]=spu_ai(r[44],1);r[47]=spu_ai(r[47],16);r[16]=spu_clgt(r[60],r[44]);
    }
    memcpy(c.gpr,r,sizeof r);c.pc=0x4610;g_spu_trampoline_fn=finish;
}
}
