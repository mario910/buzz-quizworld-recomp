#pragma once
#include "native_geometry_host_data.h"

namespace buzz {
#if BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__)
// Both SPU estimate tables satisfy base >= step for every fraction. With
// a 19-bit coordinate, FI therefore cannot borrow from the seed exponent.
// The caller handles zero-exponent results when its seed permits them.
inline v8 geometry_interpolate_estimate(v8 input, v8 seed)
{
    const auto interpolate = [](uint32x4_t word, uint32x4_t estimate) {
        const auto base = vshlq_n_u32(vandq_u32(estimate, vdupq_n_u32(0x007ffc00)), 9);
        const auto delta = vmlsq_u32(base, vandq_u32(estimate, vdupq_n_u32(1023)),
                                     vandq_u32(word, vdupq_n_u32(0x7ffff)));
        return vsriq_n_u32(estimate, delta, 9);
    };
    return {interpolate(input.lo, seed.lo), interpolate(input.hi, seed.hi)};
}

inline v8 geometry_inverse_sqrt_estimate(v8 input)
{
    // FRSQEST has a nonzero result exponent for every input exponent.
    return geometry_interpolate_estimate(input, v8_frsqest(input));
}
#endif

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
    return _mm256_andnot_si256(_mm256_cmpeq_epi32(bits,v8_set(0x80000000)),bits);
}
inline v8 geometry_bounded_mul(v8 a,v8 b)
{ return geometry_bounded_finish(_mm256_mul_ps(_mm256_castsi256_ps(a),_mm256_castsi256_ps(b))); }
inline v8 geometry_bounded_fma(v8 a,v8 b,v8 c)
{ return geometry_bounded_finish(_mm256_fmadd_ps(_mm256_castsi256_ps(a),_mm256_castsi256_ps(b),_mm256_castsi256_ps(c))); }

// Matrix admission belongs to the caller and is shared by every block in a
// job. Input rejection leaves value unchanged for the exact generic path.
inline bool transform_geometry_bounded_position(GeometryHostSoA8& value,const v8* matrix,v8 mask)
{
#if BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__)
    uint32x4_t magnitude = vdupq_n_u32(0);
    for (auto component : value.component) {
        const v8 bits = _mm256_and_si256(component,v8_set(0x7fffffff));
        magnitude = vmaxq_u32(magnitude,vmaxq_u32(bits.lo,bits.hi));
    }
    if (vmaxvq_u32(magnitude)>0x4e800000u) return false;
#else
    v8 outside=_mm256_setzero_si256();
    for (const auto component:value.component)
        outside=_mm256_or_si256(outside,_mm256_cmpgt_epi32(
            _mm256_and_si256(component,v8_set(0x7fffffff)),v8_set(0x4e800000)));
    if (_mm256_movemask_ps(_mm256_castsi256_ps(outside))) return false;
#endif
    const v8 x=value.component[0],y=value.component[1],z=value.component[2],w=value.component[3];
#if BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__)
    // The bounded dot products consume intermediate zeros only as FMA addends.
    // Either zero sign yields the same nonzero result. Canonicalize each final
    // component before it can be observed or used by another operation.
    auto mul=[](v8 p,v8 q) {
        return _mm256_castps_si256(_mm256_mul_ps(_mm256_castsi256_ps(p),_mm256_castsi256_ps(q)));
    };
    auto madd=[](v8 p,v8 q,v8 r) {
        return _mm256_castps_si256(_mm256_fmadd_ps(
            _mm256_castsi256_ps(p),_mm256_castsi256_ps(q),_mm256_castsi256_ps(r)));
    };
#else
    auto mul=geometry_bounded_mul;
    auto madd=geometry_bounded_fma;
#endif
    v8 a=mul(x,matrix[0]);a=madd(y,matrix[1],a);
    a=madd(z,matrix[2],a);a=madd(w,matrix[3],a);
    v8 b=mul(y,matrix[5]);b=madd(z,matrix[6],b);
    b=madd(w,matrix[7],b);b=madd(x,matrix[4],b);
    v8 d=mul(z,matrix[10]);d=madd(w,matrix[11],d);
    d=madd(x,matrix[8],d);d=madd(y,matrix[9],d);
#if BUZZ_GEOMETRY_MOBILE_KERNELS && defined(__aarch64__)
    a=geometry_bounded_finish(_mm256_castsi256_ps(a));
    b=geometry_bounded_finish(_mm256_castsi256_ps(b));
    d=geometry_bounded_finish(_mm256_castsi256_ps(d));
#endif
    value.component[0]=_mm256_and_si256(a,mask);
    value.component[1]=_mm256_and_si256(b,mask);
    value.component[2]=_mm256_and_si256(d,mask);
    return true;
}
}
