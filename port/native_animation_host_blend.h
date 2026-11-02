/* Pose blending for clips with implicit full joint weights. */
#pragma once
#include "native_animation_host_pose.h"
#include "native_animation_host_quaternions.h"

namespace buzz {
// The recognized corpus uses full implicit joint weights on both inputs.
// Explicit weight streams require whole-job fallback until genuinely covered.
inline AnimationHostPose8 blend_animation_host_poses8(const AnimationHostPose8& first,
    const AnimationHostPose8& last,v8 factor)
{
    // Full implicit weights have a fixed FI/FREST/refinement result under
    // SPU RTZ: 0x3f7fffff. Retain its rounding without repeating constant work.
    const v8 weight=v8_fm(factor,v8_set(0x3f7fffff));
    AnimationHostPose8 result;
    result.rotation=interpolate_animation_host_quaternions8(first.rotation,last.rotation,weight);
    for(unsigned component=0;component<4;++component) {
        const v8 a=first.translation.component[component],b=last.translation.component[component];
        result.translation.component[component]=v8_fnms(a,weight,v8_fma(b,weight,a));
        const v8 x=first.scale.component[component],y=last.scale.component[component];
        result.scale.component[component]=v8_fnms(x,weight,v8_fma(y,weight,x));
    }
    return result;
}
} // namespace buzz
