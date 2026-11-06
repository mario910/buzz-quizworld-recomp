#pragma once
#include "native_geometry_host_data.h"

namespace buzz {
inline bool geometry_object_matrix_bounded(const uint32_t* coefficients)
{
    for (unsigned i=0;i<12;++i)
        if ((coefficients[i]&0x7fffffff)>0x4e800000u) return false; // |m| <= 2^30
    return true;
}

// Only called after proving all operands <= 2^30 in magnitude. Each row has
// four products, hence all intermediate magnitudes <= 2^62, far below SPU's
// extended range. Preserve the original mul/FMA order, RTZ/FZ and +zero rule;
// no reassociation, approximate reciprocal or skipped zero multiplication.
inline v8 geometry_bounded_finish(__m256 value)
{
    const v8 bits=_mm256_castps_si256(value);
    return _mm256_andnot_si256(_mm256_cmpeq_epi32(_mm256_slli_epi32(bits,1),
                                                _mm256_setzero_si256()),bits);
}
inline v8 geometry_bounded_mul(v8 a,v8 b)
{ return geometry_bounded_finish(_mm256_mul_ps(_mm256_castsi256_ps(a),_mm256_castsi256_ps(b))); }
inline v8 geometry_bounded_fma(v8 a,v8 b,v8 c)
{ return geometry_bounded_finish(_mm256_fmadd_ps(_mm256_castsi256_ps(a),_mm256_castsi256_ps(b),_mm256_castsi256_ps(c))); }

// Matrix admission belongs to the caller and is shared by every block in a
// job. Input rejection leaves value unchanged for the exact generic path.
inline bool transform_geometry_bounded_position(GeometryHostSoA8& value,const v8* matrix,v8 mask)
{
    v8 outside=_mm256_setzero_si256();
    for (const auto component:value.component)
        outside=_mm256_or_si256(outside,_mm256_cmpgt_epi32(
            _mm256_and_si256(component,v8_set(0x7fffffff)),v8_set(0x4e800000)));
    if (_mm256_movemask_ps(_mm256_castsi256_ps(outside))) return false;
    const v8 x=value.component[0],y=value.component[1],z=value.component[2],w=value.component[3];
    v8 a=geometry_bounded_mul(x,matrix[0]);a=geometry_bounded_fma(y,matrix[1],a);
    a=geometry_bounded_fma(z,matrix[2],a);a=geometry_bounded_fma(w,matrix[3],a);
    v8 b=geometry_bounded_mul(y,matrix[5]);b=geometry_bounded_fma(z,matrix[6],b);
    b=geometry_bounded_fma(w,matrix[7],b);b=geometry_bounded_fma(x,matrix[4],b);
    v8 d=geometry_bounded_mul(z,matrix[10]);d=geometry_bounded_fma(w,matrix[11],d);
    d=geometry_bounded_fma(x,matrix[8],d);d=geometry_bounded_fma(y,matrix[9],d);
    value.component[0]=_mm256_and_si256(a,mask);
    value.component[1]=_mm256_and_si256(b,mask);
    value.component[2]=_mm256_and_si256(d,mask);
    return true;
}
}
