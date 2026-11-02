/* Shared exact gathers/numerics for fixed12 packed-unit input formats4/5. */
#pragma once
#include "native_geometry_builtin_input.h"
namespace buzz {
struct GeometryBuiltinInputPosition {u128 block,a,b,c,d,ab,cd,bits;};
inline GeometryBuiltinInputPosition gather_geometry_builtin_input_position(u128 first,u128 second,u128 control,u128 halves,u128 pair)
{
    GeometryBuiltinInputPosition p;p.block=spu_shufb(first,second,control);
    p.a=spu_rotqmbyi(p.block,-2);p.b=spu_rotqmbyi(p.block,-4);p.c=spu_rotqmbyi(p.block,-6);p.d=spu_rotqmbyi(p.block,-10);
    p.ab=spu_selb(p.a,p.b,halves);p.cd=spu_selb(p.c,p.d,halves);p.bits=spu_selb(p.ab,p.cd,pair);return p;
}
struct GeometryBuiltinInputUnitRow {u128 base,source,control,value;};
inline GeometryBuiltinInputUnitRow compose_geometry_builtin_input_unit(u128 z,u128 xy,u128 table,u128 word,u128 absolute,u128 sign,u128 add)
{
    GeometryBuiltinInputUnitRow p;p.base=spu_selb(word,table,absolute);p.source=spu_selb(z,table,sign);
    p.control=spu_a(p.base,add);p.value=spu_shufb(p.source,xy,p.control);return p;
}
inline u128 fields_geometry_builtin_input_unit(u128 packed,u128 shifts,u128 base,u128 mask)
{return spu_selb(base,spu_rotm(packed,shifts),mask);}
inline u128 numeric_geometry_builtin_fixed_input(u128 packed,u128 mask,u128 offsets,u128 left,u128 right)
{
    const auto sum=spu_a(spu_and(packed,mask),offsets);
    return spu_fa(spu_csflt(spu_rotma(sum,right),155),spu_cuflt(spu_shl(sum,left),123));
}
}
