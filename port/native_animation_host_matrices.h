/* Eight independent affine matrices in host SoA. Inputs and results have no
 * SPU registers, LS scratch or call-frame representation. */
#pragma once
#include "native_geometry_host_streams.h"

namespace buzz {
struct AnimationHostAffine8 {
    v8 coefficient[12]; // Row-major 3x4; lane i is joint i.
};

inline AnimationHostAffine8 decode_animation_host_affines8(const uint8_t* source,
    unsigned stride, unsigned joints)
{
    AnimationHostAffine8 result;
    for (unsigned row = 0; row < 3; ++row) {
        const auto values = decode_geometry_host_floats8(source + row * 16, stride, 4, joints);
        for (unsigned column = 0; column < 4; ++column)
            result.coefficient[row * 4 + column] = values.component[column];
    }
    return result;
}

// world * reference, with the implicit affine row (0,0,0,1). Preserve the
// published SPU multiply/FMA order, including the final translation term.
inline AnimationHostAffine8 compose_animation_host_affines8(
    const AnimationHostAffine8& world, const AnimationHostAffine8& reference)
{
    AnimationHostAffine8 result;
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            v8 value = v8_fm(reference.coefficient[column], world.coefficient[row * 4]);
            value = v8_fma(reference.coefficient[4 + column], world.coefficient[row * 4 + 1], value);
            value = v8_fma(reference.coefficient[8 + column], world.coefficient[row * 4 + 2], value);
            result.coefficient[row * 4 + column] = v8_fma(
                v8_set(column == 3 ? 0x3f800000 : 0), world.coefficient[row * 4 + 3], value);
        }
    }
    return result;
}

// The job owns admission of joints<=8 and the dense joints*48 output span.
// Write every final-format word once; the tail does not publish inactive lanes.
inline void emit_animation_host_affines8(const AnimationHostAffine8& matrices,
    uint8_t* destination, unsigned joints)
{
    alignas(32) uint32_t encoded[12][8];
    for (unsigned coefficient = 0; coefficient < 12; ++coefficient)
        _mm256_store_si256(reinterpret_cast<v8*>(encoded[coefficient]),
            v8_bswap(matrices.coefficient[coefficient]));
    for (unsigned joint = 0; joint < joints; ++joint)
        for (unsigned coefficient = 0; coefficient < 12; ++coefficient)
            memcpy(destination + joint * 48 + coefficient * 4, &encoded[coefficient][joint], 4);
}
} // namespace buzz
