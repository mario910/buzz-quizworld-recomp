/* Covered full-strength additive poses and scalar curve operations. */
#pragma once
#include "native_animation_host_blend.h"

namespace buzz {
// Covered operation-1 recipe: flags0, full joint weights, strength1.
inline AnimationHostPose8 add_animation_host_poses8(const AnimationHostPose8& first,
    const AnimationHostPose8& delta)
{
    const auto& a=first.rotation;const auto& b=delta.rotation;
    AnimationHostQuaternion8 product;
    const v8 ax=a.component[0],ay=a.component[1],az=a.component[2],aw=a.component[3];
    const v8 bx=b.component[0],by=b.component[1],bz=b.component[2],bw=b.component[3];
    product.component[0]=v8_fnms(by,az,v8_fma(bz,ay,v8_fma(bw,ax,v8_fm(bx,aw))));
    product.component[1]=v8_fma(bx,az,v8_fma(bw,ay,v8_fnms(bz,ax,v8_fm(by,aw))));
    product.component[2]=v8_fma(bw,az,v8_fnms(bx,ay,v8_fma(by,ax,v8_fm(bz,aw))));
    product.component[3]=v8_fnms(bz,az,v8_fnms(by,ay,v8_fnms(bx,ax,v8_fm(bw,aw))));
    const v8 one=v8_set(0x3f800000);
    AnimationHostPose8 result;result.rotation=interpolate_animation_host_quaternions8(a,product,one);
    for(unsigned component=0;component<4;++component) {
        const v8 t=first.translation.component[component];
        const v8 target=animation_add8(v8_fm(delta.translation.component[component],
            component==3?_mm256_setzero_si256():one),t);
        result.translation.component[component]=v8_fnms(t,one,v8_fma(target,one,t));
        const v8 s=first.scale.component[component],scaled=v8_fm(delta.scale.component[component],s);
        result.scale.component[component]=v8_fnms(s,one,v8_fma(scaled,one,s));
    }
    return result;
}
inline v8 clamp_animation_host_curves8(v8 value,v8 clamp)
{
    const v8 one=v8_set(0x3f800000),zero=_mm256_setzero_si256();
    v8 bounded=_mm256_blendv_epi8(value,one,v8_fcgt(value,one));
    bounded=_mm256_blendv_epi8(bounded,zero,v8_fcgt(zero,bounded));
    return _mm256_blendv_epi8(value,bounded,clamp);
}
inline v8 blend_animation_host_curves8(v8 first,v8 last,v8 factor,v8 clamp)
{
    const v8 weight=v8_fm(factor,v8_set(0x3f7fffff));
    return clamp_animation_host_curves8(v8_fnms(first,weight,v8_fma(last,weight,first)),clamp);
}
inline v8 add_animation_host_curves8(v8 first,v8 delta,v8 clamp)
{
    return clamp_animation_host_curves8(v8_fma(delta,v8_set(0x3f800000),first),clamp);
}
} // namespace buzz
