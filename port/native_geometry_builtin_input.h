/* Shared builtin input metadata publication and exact CMP vector decoding.
 * The readonly overlay forecasts frame/metadata writes, never payload math. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
namespace buzz {
struct GeometryBuiltinInputMemory {
    const spu_context& context;spu_context* target=nullptr;bool allowed=true;unsigned size=0;
    unsigned addresses[40]{};u128 values[40]{};
    u128 read(unsigned address) const {
        address&=0x3fff0;for(unsigned i=size;i;--i)if(addresses[i-1]==address)return values[i-1];
        return spu_ls_read128(&context,address);
    }
    void store(unsigned address,u128 value){
        address&=0x3fff0;if(address<0xe680)allowed=false;
        if(target){spu_ls_write128(target,address,value);return;}
        for(unsigned i=0;i<size;++i)if(addresses[i]==address){values[i]=value;return;}
        if(size==40){allowed=false;return;}addresses[size]=address;values[size++]=value;
    }
};
struct GeometryBuiltinInputSlot {
    u128 ids,equal,gather,leading,index,offset,byte_mask,address,ids_after,table,publish_before,pointer,word_mask,publish_after;
};
inline GeometryBuiltinInputSlot reserve_geometry_builtin_input(GeometryBuiltinInputMemory& m,u128 selector,unsigned semantic,unsigned ids,u128 table,u128 publication,unsigned ids_store)
{
    GeometryBuiltinInputSlot s;s.ids=m.read(ids);s.equal=spu_ceqb(selector,s.ids);s.gather=spu_gbb(s.equal);s.leading=spu_clz(s.gather);
    s.index=spu_ai(s.leading,-16);s.offset=spu_shli(s.index,2);s.byte_mask=spu_cbd(s.index,0);s.address=spu_a(table,s.offset);
    s.ids_after=spu_shufb(spu_il(semantic),s.ids,s.byte_mask);m.store(ids_store,s.ids_after);
    s.table=m.read(table._u32[0]+s.offset._u32[0]);s.publish_before=m.read(publication._u32[0]);s.pointer=spu_rotqby(s.table,s.address);
    s.word_mask=spu_cwd(publication,0);s.publish_after=spu_shufb(s.pointer,s.publish_before,s.word_mask);m.store(publication._u32[0],s.publish_after);return s;
}
inline GeometryBuiltinInputSlot reserve_geometry_builtin_input(GeometryBuiltinInputMemory& m,u128 selector,unsigned semantic,unsigned ids,u128 table,u128 publication)
{return reserve_geometry_builtin_input(m,selector,semantic,ids,table,publication,ids);}
struct GeometryBuiltinInputMetadata {
    u128 manager,ids_manager,selector,table,publication[4],bytes,counter,delayed;
};
inline GeometryBuiltinInputMetadata geometry_builtin_input_metadata(const spu_context& c)
{return {c.gpr[3],c.gpr[82],c.gpr[80],c.gpr[83],{c.gpr[35],c.gpr[37],c.gpr[61],c.gpr[39]},c.gpr[36],c.gpr[42],c.gpr[49]};}
inline bool read_geometry_builtin_input_metadata(GeometryBuiltinInputMemory& memory,const GeometryBuiltinInputMetadata& m,unsigned format,unsigned& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(format<1 || format>5)return reject("unknown builtin input metadata family");
    const unsigned bytes=0u-m.counter._u32[0];if(!bytes || (bytes&31) || bytes>1048576)return reject("builtin input counter");
    const unsigned streams=(format==3 || format==5)?4:3,groups=bytes/32,offset=m.bytes._u32[0]+m.counter._u32[0];u128 pointers[4];
    for(unsigned stream=0;stream<streams;++stream){
        const unsigned ids=(stream==0 || (stream==1 && format!=1))?m.manager._u32[0]:m.ids_manager._u32[0];
        const unsigned destination=(stream==1 && format==2)?m.ids_manager._u32[0]:ids;
        pointers[stream]=reserve_geometry_builtin_input(memory,m.selector,stream+1,ids+0x160,m.table,m.publication[stream],destination+0x160).pointer;
    }
    if(!memory.allowed)return reject("builtin input protected metadata write");
    for(unsigned stream=0;stream<streams;++stream)for(unsigned group=0;group<groups;++group)for(unsigned row=0;row<2;++row)
        if(((pointers[stream]._u32[0]+offset+group*32+row*16)&0x3fff0)<0xe680)return reject("builtin input protected data write");
    if(format>=4)for(unsigned stream=1;stream<streams;++stream)for(unsigned row=0;row<2;++row)
        if(((pointers[stream]._u32[0]+m.bytes._u32[0]+m.delayed._u32[0]+row*16)&0x3fff0)<0xe680)return reject("builtin input protected padding write");
    out=groups;return true;
}
struct GeometryBuiltinInputCmp {u128 shifted,masked,converted,scaled;};
inline GeometryBuiltinInputCmp decode_geometry_builtin_input_cmp(u128 bits,u128 shifts,u128 mask,u128 scale,u128 bias)
{
    GeometryBuiltinInputCmp p;p.shifted=spu_shl(bits,shifts);p.masked=spu_and(p.shifted,mask);p.converted=spu_csflt(p.masked,124);p.scaled=spu_fma(p.converted,scale,bias);return p;
}
}
