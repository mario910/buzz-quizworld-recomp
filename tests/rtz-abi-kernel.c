/* Compile twice with RTZ_ABI_NAME and (for control) SPU_RTZ_LEGACY_FIX_ABI.
 * Both use the same validity predicates and exact integer fallback. */
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#ifndef RTZ_ABI_NAME
#error Define a distinct external name for each ABI variant
#endif
void RTZ_ABI_NAME(u128* dst,const u128* src,unsigned count)
{
    for(unsigned i=0;i<count;++i){
        u128 a=src[i*3],b=src[i*3+1],c=src[i*3+2];
        u128 x=spu_fma(a,b,c),y=spu_fm(a,b);
        dst[i]=spu_fa(x,y);
    }
}
