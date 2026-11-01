/* Shared builtin input metadata publication and exact CMP vector decoding.
 * The readonly overlay forecasts metadata only, never payload arithmetic. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
namespace buzz {
struct GeometryBuiltinInputMemory {
    const spu_context& context;spu_context* target=nullptr;bool allowed=true;unsigned size=0;
    unsigned addresses[8]{};u128 values[8]{};
    u128 read(unsigned address) const {
        address&=0x3fff0;for(unsigned i=size;i;--i)if(addresses[i-1]==address)return values[i-1];
        return spu_ls_read128(&context,address);
    }
    void store(unsigned address,u128 value){
        address&=0x3fff0;if(address<0xe680)allowed=false;
        if(target){spu_ls_write128(target,address,value);return;}
        for(unsigned i=0;i<size;++i)if(addresses[i]==address){values[i]=value;return;}
        if(size==8){allowed=false;return;}addresses[size]=address;values[size++]=value;
    }
};
struct GeometryBuiltinInputSlot {
    u128 ids,equal,gather,leading,index,offset,byte_mask,address,ids_after,table,publish_before,pointer,word_mask,publish_after;
};
inline GeometryBuiltinInputSlot reserve_geometry_builtin_input(GeometryBuiltinInputMemory& m,u128 selector,unsigned semantic,unsigned ids,u128 table,u128 publication)
{
    GeometryBuiltinInputSlot s;s.ids=m.read(ids);s.equal=spu_ceqb(selector,s.ids);s.gather=spu_gbb(s.equal);s.leading=spu_clz(s.gather);
    s.index=spu_ai(s.leading,-16);s.offset=spu_shli(s.index,2);s.byte_mask=spu_cbd(s.index,0);s.address=spu_a(table,s.offset);
    s.ids_after=spu_shufb(spu_il(semantic),s.ids,s.byte_mask);m.store(ids,s.ids_after);
    s.table=m.read(table._u32[0]+s.offset._u32[0]);s.publish_before=m.read(publication._u32[0]);s.pointer=spu_rotqby(s.table,s.address);
    s.word_mask=spu_cwd(publication,0);s.publish_after=spu_shufb(s.pointer,s.publish_before,s.word_mask);m.store(publication._u32[0],s.publish_after);return s;
}
struct GeometryBuiltinInputCmp {u128 shifted,masked,converted,scaled;};
inline GeometryBuiltinInputCmp decode_geometry_builtin_input_cmp(u128 bits,u128 shifts,u128 mask,u128 scale,u128 bias)
{
    GeometryBuiltinInputCmp p;p.shifted=spu_shl(bits,shifts);p.masked=spu_and(p.shifted,mask);p.converted=spu_csflt(p.masked,124);p.scaled=spu_fma(p.converted,scale,bias);return p;
}
}
