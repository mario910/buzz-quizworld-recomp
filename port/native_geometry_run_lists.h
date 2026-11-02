/* Complete 7118/7298 in-place run-list expansion. Host runs own the bulk
 * output; the last two pipeline steps materialize the exact observable ABI. */
#pragma once
#include "native_geometry_output.h"
namespace buzz {
struct GeometryRunList { uint32_t base=0, bytes=0, vertices=0; uint16_t first[2]{}; };
inline bool read_geometry_run_list(const spu_context& ctx,GeometryRunList& output,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(ctx.ls!=ctx.ls_store)return reject("foreign LS");
    GeometryRunList p;p.base=ctx.gpr[3]._u32[0];p.bytes=ctx.gpr[4]._u32[0];p.vertices=ctx.gpr[5]._u32[0];
    if(!p.bytes){output=p;return true;}
    if((p.base&15) || p.base<0xe680 || p.base>=SPU_LS_SIZE || (p.bytes&1) ||
       p.bytes>SPU_LS_SIZE-p.base || p.vertices>(SPU_LS_SIZE-p.base)/2 || p.bytes>p.vertices*2)
        return reject("encoded/output span");
    uint32_t total=0;
    for(unsigned i=0;i<p.bytes;i+=2){const uint16_t code=uint16_t(geometry_job_be16(ctx.ls+p.base+i));if(i<4)p.first[i/2]=code;total+=(code&31)+1;}
    if(total!=p.vertices)return reject("run lengths/output count");
    for(unsigned table=0;table<9;++table)for(unsigned lane=0;lane<8;++lane){
        const unsigned expected=table<4?(table*8+lane)*16:((table-4)*8+lane)*2+1;
        if(geometry_job_be16(ctx.ls+0xd080+table*16+lane*2)!=expected)return reject("unreviewed run tables");
    }
    for(unsigned i=0;i<16;++i)if(ctx.ls[0xd980+i]!=i)return reject("unreviewed identity mask");
    output=p;return true;
}
inline void geometry_run_list_setup(spu_context& c)
{
    auto& r=c.gpr;auto get=[&](uint32_t at){return spu_ls_read128(&c,at);};
    r[45]=spu_a(r[5],r[5]);r[69]=r[4];r[71]=spu_ilhu(0xffff);r[15]=r[3];r[70]=spu_andi(r[45],15);
    r[42]=get(0xd980);r[37]=spu_ai(r[45],-1);r[72]=spu_sfi(r[70],0);r[21]=r[15];r[67]=spu_ceqi(r[70],0);
    r[49]=get(r[3]._u32[0]+r[37]._u32[0]);r[68]=spu_rotma(r[71],r[72]);r[65]=spu_ai(r[69],-2);
    r[41]=spu_ilhu(0x8080);r[12]=spu_ai(r[65],-2);r[66]=spu_or(r[67],r[68]);r[41]=spu_ori(r[41],1);
    r[2]=spu_rotmi(r[20],-1);r[50]=spu_fsmb(r[66]);r[13]=spu_ai(r[12],-2);r[34]=spu_ai(r[15],16);r[33]=spu_ai(r[15],32);
    r[43]=spu_and(r[49],r[50]);r[20]=r[2];r[49]=spu_ilh(0x303);r[50]=spu_ilh(0x203);
    spu_ls_write128(&c,r[3]._u32[0]+r[37]._u32[0],r[43]);
    r[37]=spu_ila(0x10001);r[63]=get(r[3]._u32[0]+r[65]._u32[0]);r[43]=spu_xorbi(r[42],32);
    r[61]=get(r[3]._u32[0]+r[12]._u32[0]);r[31]=spu_ai(r[15],48);r[42]=get(0xd080);r[30]=spu_ai(r[15],64);
    r[60]=spu_rotqby(r[63],r[65]);r[57]=spu_rotqby(r[61],r[12]);r[59]=spu_andhi(r[60],31);
    r[39]=spu_andhi(r[57],-32);r[56]=spu_ahi(r[59],1);r[58]=spu_andhi(r[60],-32);r[44]=spu_rotqmbii(r[39],-1);
    r[40]=spu_andhi(r[57],31);r[47]=spu_shlqbii(r[56],1);r[38]=spu_rotqmbii(r[58],-1);r[48]=spu_ahi(r[40],1);
    r[39]=get(0xd0a0);r[8]=spu_shufb(r[44],r[15],r[37]);r[46]=spu_shufb(r[47],r[15],r[41]);
    r[10]=spu_shufb(r[47],r[15],r[37]);r[55]=spu_shufb(r[38],r[15],r[37]);r[19]=spu_ah(r[8],r[42]);
    r[16]=spu_shlqbii(r[48],1);r[36]=spu_sf(r[46],r[45]);r[40]=get(0xd090);r[14]=spu_ah(r[8],r[39]);
    r[38]=get(0xd0b0);r[52]=r[36];r[48]=get(0xd0c0);r[32]=spu_ah(r[55],r[42]);r[46]=get(0xd0d0);
    r[35]=spu_andi(r[52],15);r[47]=get(0xd0e0);r[4]=spu_ah(r[55],r[39]);r[45]=get(0xd0f0);
    r[29]=spu_ah(r[55],r[40]);r[44]=get(0xd100);r[28]=spu_ah(r[55],r[38]);
    r[54]=spu_shufb(r[35],r[15],r[50]);r[11]=spu_ah(r[8],r[38]);r[27]=spu_shufb(r[35],r[15],r[49]);
    r[17]=spu_ah(r[8],r[40]);r[53]=spu_shufb(r[16],r[15],r[41]);r[16]=spu_shufb(r[16],r[15],r[37]);
    r[51]=spu_sfh(r[54],r[48]);r[3]=spu_sf(r[27],r[43]);r[7]=spu_sfh(r[54],r[44]);r[9]=spu_sfh(r[54],r[46]);
    r[35]=spu_shufb(r[15],r[28],r[3]);r[5]=spu_sfh(r[54],r[47]);r[27]=spu_shufb(r[32],r[15],r[3]);
    r[6]=spu_sfh(r[54],r[45]);r[32]=spu_shufb(r[29],r[32],r[3]);r[25]=spu_clgth(r[7],r[10]);
    r[29]=spu_shufb(r[4],r[29],r[3]);r[18]=spu_sf(r[53],r[52]);r[28]=spu_shufb(r[28],r[4],r[3]);
    r[26]=spu_clgth(r[51],r[10]);r[24]=spu_clgth(r[9],r[10]);r[23]=spu_clgth(r[5],r[10]);r[22]=spu_clgth(r[6],r[10]);
}
inline void geometry_run_list_finish_step(spu_context& c)
{
    auto& r=c.gpr;auto get=[&](uint32_t at){return spu_ls_read128(&c,at);};
    auto put=[&](uint32_t at,u128 value){spu_ls_write128(&c,at,value);};
    r[56]=r[36];r[62]=get(r[21]._u32[0]+r[36]._u32[0]);r[55]=r[13];r[61]=get(r[34]._u32[0]+r[36]._u32[0]);
    r[4]=spu_ai(r[20],-1);r[59]=get(r[33]._u32[0]+r[36]._u32[0]);r[58]=get(r[31]._u32[0]+r[36]._u32[0]);
    r[20]=r[4];r[57]=get(r[30]._u32[0]+r[36]._u32[0]);r[36]=spu_andi(r[18],15);
    r[60]=spu_selb(r[27],r[62],r[26]);r[27]=spu_selb(r[32],r[61],r[24]);r[7]=spu_shufb(r[36],r[15],r[50]);
    r[32]=spu_selb(r[29],r[59],r[23]);put(r[21]._u32[0]+r[56]._u32[0],r[60]);r[29]=spu_selb(r[28],r[58],r[22]);
    put(r[34]._u32[0]+r[56]._u32[0],r[27]);r[3]=spu_selb(r[35],r[57],r[25]);put(r[33]._u32[0]+r[56]._u32[0],r[32]);
    r[10]=spu_sfh(r[7],r[44]);put(r[31]._u32[0]+r[56]._u32[0],r[29]);r[6]=spu_sfh(r[7],r[48]);
    put(r[30]._u32[0]+r[56]._u32[0],r[3]);r[8]=spu_sfh(r[7],r[46]);r[28]=get(r[21]._u32[0]+r[13]._u32[0]);
    r[9]=spu_sfh(r[7],r[47]);r[5]=spu_shufb(r[36],r[15],r[49]);r[12]=spu_sfh(r[7],r[45]);r[36]=r[18];
    r[25]=spu_clgth(r[10],r[16]);r[26]=spu_clgth(r[6],r[16]);r[78]=spu_sf(r[5],r[43]);r[24]=spu_clgth(r[8],r[16]);
    r[54]=spu_rotqby(r[28],r[55]);r[23]=spu_clgth(r[9],r[16]);r[32]=spu_shufb(r[17],r[19],r[78]);
    r[22]=spu_clgth(r[12],r[16]);r[29]=spu_shufb(r[14],r[17],r[78]);r[13]=spu_ai(r[13],-2);
    r[28]=spu_shufb(r[11],r[14],r[78]);r[53]=spu_andhi(r[54],31);r[35]=spu_shufb(r[15],r[11],r[78]);
    r[52]=spu_andhi(r[54],-32);r[27]=spu_shufb(r[19],r[15],r[78]);r[2]=spu_ahi(r[53],1);
    r[51]=spu_rotqmbii(r[52],-1);r[79]=spu_shlqbii(r[2],1);r[77]=spu_shufb(r[51],r[15],r[37]);
    r[76]=spu_shufb(r[79],r[15],r[41]);r[16]=spu_shufb(r[79],r[15],r[37]);r[74]=spu_ah(r[42],r[77]);
    r[75]=spu_sf(r[76],r[18]);r[19]=r[74];r[18]=r[75];r[11]=spu_ah(r[38],r[77]);r[17]=spu_ah(r[40],r[77]);r[14]=spu_ah(r[39],r[77]);
}
inline void expand_geometry_run_list(spu_context& c,const GeometryRunList& p)
{
    auto& r=c.gpr;const auto encoded=r[4],output_bytes=spu_a(r[5],r[5]);r[20]=r[4];
    if(p.bytes){
        geometry_run_list_setup(c);
        const unsigned bulk=(p.bytes/2)>2?unsigned((p.bytes/2)-2):0;
        uint32_t cursor=p.vertices*2;
        // Expanding backwards cannot overwrite an unread earlier code: each
        // remaining run occupies at least its two encoded bytes. The first
        // two codes are cached because their final ABI steps write in place.
        for(unsigned i=unsigned((p.bytes/2));i>2;--i){
            const uint16_t code=geometry_job_be16(c.ls+p.base+(i-1)*2);const unsigned length=(code&31)+1;cursor-=length*2;
            auto* out=c.ls+p.base+cursor;
            for(unsigned j=0;j<length;++j){const unsigned value=((code>>5)+j)*16;out[j*2]=uint8_t(value>>8);out[j*2+1]=uint8_t(value);}
        }
        if(bulk){
            const unsigned current_length=((p.first[1]&31)+1)*2,next_length=((p.first[0]&31)+1)*2;
            r[20]=spu_ai(spu_rotmi(encoded,-1),-int(bulk));r[13]=spu_ai(encoded,-6-int(bulk*2));
            r[36]=spu_ai(output_bytes,-int(p.vertices*2-cursor+current_length));r[18]=spu_ai(r[36],-int(next_length));
            const auto next=spu_ilh((p.first[0]>>5)*16);r[16]=spu_ilh(next_length);
            r[19]=spu_ah(next,r[42]);r[17]=spu_ah(next,r[40]);r[14]=spu_ah(next,r[39]);r[11]=spu_ah(next,r[38]);
            const auto start=spu_ilh((p.first[1]>>5)*16),a=spu_ah(start,r[42]),b=spu_ah(start,r[40]),d=spu_ah(start,r[39]),e=spu_ah(start,r[38]);
            const auto offset=spu_andi(r[36],15),selector=spu_sf(spu_shufb(offset,r[15],r[49]),r[43]);
            r[27]=spu_shufb(a,r[15],selector);r[32]=spu_shufb(b,a,selector);r[29]=spu_shufb(d,b,selector);
            r[28]=spu_shufb(e,d,selector);r[35]=spu_shufb(r[15],e,selector);
            const auto mod=spu_shufb(offset,r[15],r[50]),length=spu_ilh(current_length);
            r[26]=spu_clgth(spu_sfh(mod,r[48]),length);r[24]=spu_clgth(spu_sfh(mod,r[46]),length);
            r[23]=spu_clgth(spu_sfh(mod,r[47]),length);r[22]=spu_clgth(spu_sfh(mod,r[45]),length);r[25]=spu_clgth(spu_sfh(mod,r[44]),length);
        }
        for(unsigned i=0;i<(p.bytes/2)-bulk;++i)geometry_run_list_finish_step(c);
    }
    c.pc=r[0]._u32[0];g_spu_trampoline_fn=c.host_depth?nullptr:spu_indirect_branch;
}
} // namespace buzz
