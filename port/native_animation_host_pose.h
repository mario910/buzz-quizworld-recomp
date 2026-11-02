/* Logical Q/T/S pose -> eight row-major host world matrices. */
#pragma once
#include "native_geometry_host_streams.h"

namespace buzz {
struct AnimationHostPose8 { GeometryHostSoA8 rotation,translation,scale; };
struct AnimationHostWorldMatrix8 { v8 coefficient[16]; };

// The frontend admits the active joints*48 source span; inactive lanes never
// read a following joint. All data becomes host little-endian SoA once.
inline AnimationHostPose8 decode_animation_host_pose8(const uint8_t* source,unsigned joints)
{
    return {decode_geometry_host_floats8(source,48,4,joints),
        decode_geometry_host_floats8(source+16,48,3,joints),
        decode_geometry_host_floats8(source+32,48,3,joints)};
}
inline AnimationHostWorldMatrix8 make_animation_host_world_matrices8(const AnimationHostPose8& pose)
{
    const v8 x=pose.rotation.component[0],y=pose.rotation.component[1],
        z=pose.rotation.component[2],w=pose.rotation.component[3];
    const v8 half=v8_set(0x3f000000),two=v8_set(0x40000000);
    const v8 xx=v8_fnms(x,x,half),yy=v8_fnms(y,y,half);
    const v8 xy=v8_fm(x,y),xz=v8_fm(x,z),yz=v8_fm(y,z);
    const v8 sx=v8_fm(pose.scale.component[0],two),sy=v8_fm(pose.scale.component[1],two),
        sz=v8_fm(pose.scale.component[2],two);
    AnimationHostWorldMatrix8 result;
    result.coefficient[0]=v8_fm(v8_fnms(z,z,yy),sx);
    result.coefficient[1]=v8_fm(v8_fnms(w,z,xy),sy);
    result.coefficient[2]=v8_fm(v8_fma(w,y,xz),sz);
    result.coefficient[4]=v8_fm(v8_fma(w,z,xy),sx);
    result.coefficient[5]=v8_fm(v8_fnms(z,z,xx),sy);
    result.coefficient[6]=v8_fm(v8_fnms(w,x,yz),sz);
    result.coefficient[8]=v8_fm(v8_fnms(w,y,xz),sx);
    result.coefficient[9]=v8_fm(v8_fma(w,x,yz),sy);
    result.coefficient[10]=v8_fm(v8_fnms(y,y,xx),sz);
    for(unsigned row=0;row<3;++row)result.coefficient[row*4+3]=pose.translation.component[row];
    for(unsigned column=0;column<3;++column)result.coefficient[12+column]=_mm256_setzero_si256();
    result.coefficient[15]=v8_set(0x3f800000);
    return result;
}
// Final published world stream, row-major BE 4x4. Each active word is stored
// once; the job frontend owns the admitted joints*64 destination span.
inline void emit_animation_host_world_matrices8(const AnimationHostWorldMatrix8& matrices,
    uint8_t* destination,unsigned joints)
{
    alignas(32) uint32_t encoded[16][8];
    for(unsigned coefficient=0;coefficient<16;++coefficient)
        _mm256_store_si256(reinterpret_cast<v8*>(encoded[coefficient]),v8_bswap(matrices.coefficient[coefficient]));
    for(unsigned joint=0;joint<joints;++joint)for(unsigned coefficient=0;coefficient<16;++coefficient)
        memcpy(destination+joint*64+coefficient*4,&encoded[coefficient][joint],4);
}
} // namespace buzz
