/* Complete shared output formatter. Metadata admission precedes all writes;
 * data callees retain their own switches and exact queued boundaries. */
#pragma once
#include "native_geometry_job.h"
#include "native_geometry_context.h"
#include "spu_helpers.h"
#include <algorithm>
#include <cstdint>
namespace buzz {
struct GeometryFormatter { unsigned fields=0,known=0,skipped=0,mask=0; };
struct GeometryFormatterCallees { buzz_geometry_job_fn data[7]{},finish=nullptr,skip=nullptr; };
struct GeometryFormatterMemory {
    spu_context& c;bool commit;unsigned cursor_address;bool has_cursor=false;u128 cursor{};
    u128 read(unsigned address) const {
        if(!commit && has_cursor && (address&0x3fff0)==cursor_address)return cursor;
        return spu_ls_read128(&c,address);
    }
    void store(unsigned address,u128 value) {
        if(commit)spu_ls_write128(&c,address,value);
        else if((address&0x3fff0)==cursor_address){cursor=value;has_cursor=true;}
    }
};
inline bool geometry_formatter_overlap(unsigned a,unsigned an,unsigned b,unsigned bn)
{ return uint64_t(a)<uint64_t(b)+bn && uint64_t(b)<uint64_t(a)+an; }
inline bool open_geometry_formatter(u128* r,GeometryFormatterMemory& m)
{
    const auto stack=r[1];r[64]=r[4];r[59]=r[3];
    const unsigned saved[]={126,80,81,82};const unsigned offsets[]={64,16,32,48};
    for(unsigned i=0;i<4;++i)m.store(stack._u32[0]-offsets[i],r[saved[i]]);
    r[5]=spu_ila(0x4190);r[126]=spu_sf(r[5],spu_link(0x4190));
    m.store(stack._u32[0]-128,stack);r[4]=m.read(r[3]._u32[0]);r[1]=spu_ai(stack,-128);
    r[3]=spu_rotqbyi(r[4],13);r[2]=spu_andi(r[3],16);
    if(r[2]._u32[0]){r[6]=m.read(r[59]._u32[0]+0x50);r[70]=spu_rotqbyi(r[6],4);return false;}
    r[11]=m.read(r[59]._u32[0]+0x50);r[8]=m.read(r[59]._u32[0]);r[10]=spu_cwd(r[1],4);
    r[70]=spu_rotqbyi(r[11],8);r[6]=spu_rotqbyi(r[8],6);r[7]=spu_shufb(r[70],r[11],r[10]);
    m.store(r[59]._u32[0]+0x50,r[7]);return r[6]._u16[0]!=0;
}
inline bool prepare_geometry_formatter(u128* r,GeometryFormatterMemory& m)
{
    r[23]=spu_ila(65535);r[79]=spu_ilhu(0x8080);r[22]=m.read(r[64]._u32[0]);
    r[20]=spu_and(r[6],r[23]);r[51]=m.read(0xdad0);r[16]=spu_ila(0x10203);r[63]=m.read(0xd980);
    r[17]=spu_ai(r[20],3);r[36]=m.read(0xcf20);r[19]=spu_ai(r[1],32);r[35]=m.read(0xcf30);
    r[69]=spu_ila(0x10203);r[34]=m.read(0xcf40);r[15]=spu_andi(r[17],-4);
    r[21]=spu_rotqbyi(r[22],14);r[78]=r[79];r[14]=spu_rotqbyi(r[51],1);r[68]=spu_il(-256);
    r[11]=spu_shufb(r[19],r[69],r[69]);r[40]=spu_iohl(spu_ilhu(0x46ff),0xff00);
    r[12]=spu_shufb(r[15],r[15],r[16]);r[18]=spu_andi(r[21],255);
    r[50]=spu_iohl(spu_ilhu(0x3f),0xf800);r[43]=spu_ilhu(0x3f00);
    r[55]=spu_shufb(r[18],r[18],r[16]);r[13]=spu_andi(r[18],1);r[5]=r[18];r[41]=r[43];
    r[79]=spu_iohl(r[79],0x8001);r[78]=spu_iohl(r[78],0x8004);r[46]=spu_orbi(r[14],4);
    r[49]=spu_orbi(r[63],16);r[68]=spu_andbi(r[68],-128);r[81]=spu_ilh(0x303);
    r[42]=spu_ilhu(0x437f);r[39]=spu_ilhu(0xbf00);r[38]=spu_ilhu(0x3800);r[37]=spu_ilh(0x8000);
    r[58]=spu_ilh(0x7bff);r[57]=spu_ilh(0x3800);r[56]=spu_ilh(0x4780);
    r[7]=spu_il(4);r[9]=spu_il(1024);r[8]=spu_il(6);r[77]=spu_il(16);r[60]=spu_il(16);
    r[10]=spu_il(256);r[6]=spu_il(512);r[3]=spu_il(768);
    if(!r[13]._u32[0]){
        r[4]=spu_il(0);
        do {
            r[5]=spu_rotmai(r[5],-1);r[4]=spu_ai(r[4],1);r[30]=spu_cgti(r[4],3);r[29]=spu_xori(r[5],1);
            r[28]=spu_nor(r[30],r[30]);r[27]=spu_and(r[28],r[29]);r[26]=spu_andi(r[27],1);
            r[25]=spu_ceqbi(r[26],0);r[24]=spu_xsbh(r[25]);
        }while(!r[24]._u16[0]);
        r[48]=spu_il(16);r[47]=spu_sfi(r[4],0);r[44]=spu_il(64);r[77]=spu_rotma(r[48],r[47]);
        r[45]=spu_sfi(r[4],0);r[7]=spu_sfi(r[4],4);r[33]=spu_rotma(r[44],r[45]);
        r[32]=spu_a(r[77],r[77]);r[60]=r[77];r[10]=spu_shli(r[77],4);r[31]=spu_a(r[32],r[77]);
        r[9]=spu_shli(r[33],4);r[3]=spu_shli(r[31],4);r[6]=spu_shli(r[77],5);r[8]=spu_sfi(r[4],6);
    }
    r[73]=r[3];r[5]=m.read(r[64]._u32[0]);r[3]=spu_ila(0x10203);r[82]=m.read(0xcf50);
    r[67]=r[70];r[71]=m.read(0xcf60);r[75]=r[10];r[72]=spu_shufb(r[7],r[7],r[3]);r[74]=r[6];
    r[65]=spu_shufb(r[8],r[8],r[3]);r[76]=spu_ai(r[67],16);r[62]=spu_shufb(r[67],r[67],r[69]);
    r[61]=spu_ai(r[64],8);r[8]=spu_rotqbyi(r[5],13);r[53]=spu_shl(r[82],r[72]);
    r[29]=spu_shufb(r[9],r[9],r[3]);r[54]=spu_shl(r[71],r[72]);r[31]=spu_shl(r[55],r[65]);
    r[66]=spu_ceqbi(r[8],0);r[30]=spu_sf(r[62],r[11]);r[52]=spu_xsbh(r[66]);r[80]=spu_ai(r[53],1);
    if(r[52]._u16[0])return false;
    r[2]=spu_ila(0x445c);r[72]=m.read(0xd9b0);r[82]=spu_shli(r[12],4);r[71]=m.read(0xd9c0);
    r[65]=spu_il(0);r[33]=m.read(0xd9a0);r[66]=spu_a(r[2],r[126]);return true;
}
inline unsigned prepare_geometry_formatter_field(u128* r,GeometryFormatterMemory& m)
{
    r[44]=r[61];r[32]=m.read(r[61]._u32[0]);r[23]=spu_ilhu(0xffff);r[26]=m.read(r[59]._u32[0]+0x160);
    r[45]=spu_ai(r[61],8);r[20]=spu_ila(65535);r[61]=r[45];r[27]=spu_rotqby(r[32],r[44]);
    r[25]=spu_shufb(r[27],r[81],r[81]);r[28]=spu_shufb(r[27],r[27],r[79]);
    r[48]=spu_shufb(r[27],r[27],r[78]);r[53]=spu_shufb(r[27],r[27],r[68]);r[22]=spu_ceqb(r[25],r[26]);
    r[21]=spu_ai(r[28],-1);r[24]=spu_sfi(r[48],0);r[18]=spu_gbb(r[22]);r[17]=spu_shli(r[21],2);
    r[19]=spu_rotm(r[23],r[24]);r[6]=spu_clgti(r[21],6);r[16]=spu_clz(r[18]);r[12]=spu_a(r[66],r[17]);
    r[14]=m.read(r[66]._u32[0]+r[17]._u32[0]);r[15]=spu_ai(r[16],-16);r[45]=spu_and(r[20],r[19]);
    r[13]=spu_shli(r[15],2);r[4]=spu_rotqby(r[14],r[12]);r[11]=spu_a(r[13],r[59]);
    r[10]=m.read(r[11]._u32[0]+16);r[4]=spu_a(r[4],r[66]);r[9]=spu_rotqby(r[10],r[11]);
    r[7]=spu_shufb(r[9],r[69],r[69]);r[52]=spu_a(r[80],r[7]);r[3]=spu_a(r[82],r[7]);
    return r[6]._u32[0]?0:r[28]._u32[0];
}
inline bool finish_geometry_formatter_field(u128* r,GeometryFormatterMemory& m,bool converted)
{
    if(converted){r[48]=m.read(r[64]._u32[0]);r[8]=spu_rotqbyi(r[48],13);}
    r[65]=spu_ai(r[65],1);r[52]=spu_andi(r[8],255);r[53]=spu_clgt(r[52],r[65]);return r[53]._u32[0]!=0;
}
inline void close_geometry_formatter(spu_context& c,u128* r,GeometryFormatterMemory& m)
{
    r[1]=spu_ai(r[1],128);r[3]=r[70];
    for(unsigned reg=80;reg<=82;++reg)r[reg]=m.read(r[1]._u32[0]-(reg-79)*16);
    r[126]=m.read(r[1]._u32[0]-64);if(r!=c.gpr)memcpy(c.gpr,r,sizeof(c.gpr));
    // The original queued frame tail is entered with its queue already popped.
    g_spu_trampoline_fn=nullptr;SPU_RET(&c);
}
// A parent may forecast a synchronous entry using host-local arguments and
// its pending cursor publication. Only metadata must avoid the parent's frame;
// data may still touch frame bytes and is observed by actual late reloads.
struct GeometryFormatterEntry {
    const u128* registers=nullptr;
    const u128* cursor=nullptr;
    unsigned metadata_exclusion=0,metadata_exclusion_bytes=0;
    unsigned protected_data=0,protected_data_bytes=0;
};
struct GeometryFormatterWriteBounds {
    unsigned manager,descriptor,descriptor_bytes,protected_data,protected_bytes;
};
// Prove the same physical quads as the exhaustive admission below. A regular
// column advances signed source cursors monotonically and destination quads
// without wrapping LS. Other layouts retain the exhaustive check.
inline bool prove_geometry_formatter_column(const u128& cursor,const u128& limit,
    const u128& route,const u128& cursor_step,const u128& route_step,const u128& dummy,
    unsigned base,unsigned second_base,bool cross,const GeometryFormatterWriteBounds& bounds,
    unsigned total,unsigned& groups)
{
    const unsigned step=cursor_step._u32[0];
    if(!step || step>0x7fffffff)return false;
    const int64_t first=cursor._s32[0],last=limit._s32[0];
    const uint64_t count=first>last?1:uint64_t(last-first)/step+2;
    if(count>65536 || uint64_t(total)+count>1048576)return false;
    auto safe=[&](uint64_t begin,uint64_t end){
        if(begin<0xe680 || end>SPU_LS_SIZE)return false;
        const unsigned bytes=unsigned(end-begin);
        return !geometry_formatter_overlap(unsigned(begin),bytes,bounds.manager,0x170) &&
            !geometry_formatter_overlap(unsigned(begin),bytes,bounds.descriptor,bounds.descriptor_bytes) &&
            (!bounds.protected_bytes || !geometry_formatter_overlap(unsigned(begin),bytes,bounds.protected_data,bounds.protected_bytes));
    };
    for(unsigned lane=0;lane<4;++lane){
        if(cursor_step._u32[lane]!=step || (route_step._u32[lane]&15))return false;
        const int64_t start=cursor._s32[lane],end=limit._s32[lane];
        if(start+int64_t(count-1)*step>INT32_MAX)return false;
        const uint64_t actual=start>end?0:std::min(count,uint64_t(end-start)/step+1);
        for(unsigned side=0;side<(cross?2u:1u);++side){
            const unsigned selected_base=side?second_base:base;
            if(actual){
                // Modulo2^32 and moduloLS commute; aligned steps preserve
                // the low nibble. Only a non-wrapping physical span is proven.
                const uint64_t begin=(uint64_t(selected_base)+route._u32[lane])&0x3fff0;
                const uint64_t finish=begin+(actual-1)*(route_step._u32[lane]&0x3ffff)+16;
                if(!safe(begin,finish))return false;
            }
            if(actual<count){
                const uint64_t at=(uint64_t(selected_base)+dummy._u32[lane])&0x3fff0;
                if(!safe(at,at+16))return false;
            }
        }
    }
    groups=unsigned(count);return true;
}
inline bool read_geometry_formatter(const spu_context& c,GeometryFormatter& out,const char** reason=nullptr,const GeometryFormatterEntry* entry=nullptr,bool column_spans=true)
{
    auto reject=[&](const char* text){if(reason)*reason=text;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("active interrupt context");
    const auto* arguments=entry && entry->registers?entry->registers:c.gpr;
    const unsigned manager=arguments[3]._u32[0]&~15u,sp=arguments[1]._u32[0],frame=sp&~15u;
    if(arguments[3]._u32[0]>=SPU_LS_SIZE || manager<0xe680 || uint64_t(manager)+0x170>SPU_LS_SIZE)return reject("formatter manager span");
    if(sp>=SPU_LS_SIZE || frame<0xe680+128)return reject("formatter frame span");
    if(geometry_formatter_overlap(manager,0x170,frame-128,128))return reject("formatter frame/manager alias");
    unsigned desc=0,desc_bytes=0;
    if(!(c.ls[manager]&16) && geometry_job_be16(c.ls+manager+8)){
        const unsigned raw=arguments[4]._u32[0];desc=raw&~15u;
        if(raw>=SPU_LS_SIZE || desc<0xe680)return reject("formatter descriptor span");
        const unsigned count=c.ls[desc];const uint64_t end=(uint64_t(raw)+8+count*8+15)&~uint64_t(15);
        if(end>SPU_LS_SIZE)return reject("formatter descriptor span");desc_bytes=unsigned(end-desc);
        if(geometry_formatter_overlap(desc,desc_bytes,manager,0x170) || geometry_formatter_overlap(desc,desc_bytes,frame-128,128))return reject("formatter metadata/frame alias");
    }
    if(entry && (geometry_formatter_overlap(manager,0x170,entry->metadata_exclusion,entry->metadata_exclusion_bytes) ||
        geometry_formatter_overlap(desc,desc_bytes,entry->metadata_exclusion,entry->metadata_exclusion_bytes)))return reject("formatter parent frame/metadata alias");
    u128 r[128];memcpy(r,arguments,sizeof r);GeometryFormatterMemory m{const_cast<spu_context&>(c),false,manager+0x50};
    if(entry && entry->cursor){m.cursor=*entry->cursor;m.has_cursor=true;}
    GeometryFormatter plan;
    if(!open_geometry_formatter(r,m) || !prepare_geometry_formatter(r,m)){out=plan;return true;}
    static constexpr unsigned roots[]={0x4478,0x46d0,0x4880,0x4b9c,0x4dd0,0x4fdc,0x52b4};unsigned total=0;
    const GeometryFormatterWriteBounds bounds{manager,desc,desc_bytes,entry?entry->protected_data:0,entry?entry->protected_data_bytes:0};
    for(;;){
        if(++plan.fields>255)return reject("formatter field budget");
        const unsigned storage=prepare_geometry_formatter_field(r,m);
        if(storage){
            if(r[4]._u32[0]!=roots[storage-1])return reject("formatter data target");
            ++plan.known;plan.mask|=1u<<(storage-1);
            if(r[77]._u32[0]){
                const unsigned columns=r[60]._u32[0]?r[60]._u32[0]:1;if(columns>256)return reject("formatter column budget");
                const auto limit=storage==2?r[3]:spu_a(spu_xshw(r[29]),r[3]);
                for(unsigned col=0;col<columns;++col){
                    auto source=spu_a(spu_il(col*16),r[52]);auto cursor=storage==2?source:spu_a(source,r[29]);
                    auto route=spu_mpya(r[55],spu_a(spu_il(col),r[54]),r[53]);
                    const bool cross=spu_cgti(spu_a(spu_andi(route,15),r[48]),16)._u32[0]!=0;unsigned groups=0;
                    if(column_spans && prove_geometry_formatter_column(cursor,limit,route,r[29],r[31],r[30],r[67]._u32[0],r[76]._u32[0],cross,bounds,total,groups)){
                        total+=groups;continue;
                    }
                    for(;;){
                        if(++groups>65536 || ++total>1048576)return reject("formatter iteration budget");
                        const auto after=spu_cgt(cursor,limit),chosen=spu_selb(route,r[30],after);
                        for(unsigned lane=0;lane<4;++lane)for(unsigned side=0;side<(cross?2u:1u);++side){
                            const unsigned address=(r[side?76:67]._u32[0]+chosen._u32[lane])&0x3fff0;
                            if(address<0xe680)return reject("formatter protected data write");
                            if(entry && entry->protected_data_bytes && geometry_formatter_overlap(address,16,entry->protected_data,entry->protected_data_bytes))return reject("formatter saved parent control pointer");
                            if(geometry_formatter_overlap(address,16,manager,0x170) || geometry_formatter_overlap(address,16,desc,desc_bytes))return reject("formatter data/metadata alias");
                        }
                        if(after._u32[0])break;cursor=spu_a(cursor,r[29]);route=spu_a(route,r[31]);
                    }
                }
            }
        }else ++plan.skipped;
        // No data effects touch control metadata. Only the live execution uses
        // the actual child's scratch values and actual ordered header reload.
        if(!finish_geometry_formatter_field(r,m,storage!=0))break;
    }
    out=plan;return true;
}
// The data callback consumes this live host bank. It must not fall back after
// mutation: read_geometry_formatter has already proved every child layout.
template<class Data>
inline void execute_geometry_formatter_registers(spu_context& c,const GeometryFormatter&,Data data)
{
    auto* r=c.gpr;GeometryFormatterMemory m{c,true,(c.gpr[3]._u32[0]&~15u)+0x50};
    if(open_geometry_formatter(r,m) && prepare_geometry_formatter(r,m)){
        do {
            const unsigned storage=prepare_geometry_formatter_field(r,m);
            bool converted=false;
            if(storage)converted=data(c,r,storage);
            if(!finish_geometry_formatter_field(r,m,converted))break;
        }while(true);
    }
    close_geometry_formatter(c,r,m);
}
inline void execute_geometry_formatter(spu_context& c,const GeometryFormatter& plan,const GeometryFormatterCallees& f)
{
    execute_geometry_formatter_registers(c,plan,[&](spu_context& ctx,u128* r,unsigned storage){
        ctx.pc=r[4]._u32[0];g_spu_trampoline_fn=nullptr;f.data[storage-1](&ctx);
        for(unsigned steps=0;!((ctx.pc==0x4610 && g_spu_trampoline_fn==f.finish)||(ctx.pc==0x4618 && g_spu_trampoline_fn==f.skip));++steps){
            if(!g_spu_trampoline_fn || g_spu_trampoline_fn==spu_indirect_branch || steps>=2048)std::abort();
            const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&ctx);
        }
        return ctx.pc==0x4610;
    });
}
}
