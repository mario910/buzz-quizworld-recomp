/* FLOAT1 curve keys, projected onto final published channel owners. */
#pragma once
#include "native_animation_host_quaternions.h"

namespace buzz {
struct AnimationHostCurveKeyPlan {
    unsigned tracks=0,destinations=0;
    uint16_t destination[256]{};
    uint8_t publish[256]{};
};
// Four-slot storage groups may map repeated tail entries to a published
// alignment word. Only the last owner contributes to that channel's result.
inline bool plan_animation_host_curve_keys(AnimationByteView ids,unsigned logical_tracks,
    unsigned destinations,AnimationHostCurveKeyPlan& plan)
{
    if(logical_tracks>256||destinations>256)return false;
    const unsigned tracks=(logical_tracks+3)&~3u;
    if(uint64_t(tracks)*2>ids.bytes)return false;
    plan.tracks=tracks;plan.destinations=destinations;
    bool seen[256]{};
    for(unsigned i=tracks;i--;) {
        const unsigned index=unsigned(ids.data[i*2])<<8|ids.data[i*2+1];
        if(index>=destinations)return false;
        plan.destination[i]=index;plan.publish[i]=!seen[index];seen[index]=true;
    }
    return true;
}
inline bool sample_animation_host_curve_keys(const AnimationKeyTracks& recipe,
    const AnimationHostCurveKeyPlan& plan,v8* output)
{
    if(recipe.tracks!=plan.tracks||!animation_track_recipe_valid(recipe,4))return false;
    unsigned key_prefix=0;
    for(unsigned first=0;first<recipe.tracks;first+=8) {
        AnimationTrackSelection8 selection;
        select_animation_host_tracks8(recipe,first,key_prefix,selection);
        alignas(32) uint32_t before[8]{},after[8]{};
        for(unsigned lane=0;lane<8&&first+lane<recipe.tracks;++lane) {
            if(!plan.publish[first+lane])continue;
            const AnimationByteView streams[]={selection.before_stream[lane],selection.after_stream[lane]};
            const unsigned indices[]={selection.before_index[lane],selection.after_index[lane]};
            for(unsigned endpoint=0;endpoint<2;++endpoint) {
                const uint64_t at=uint64_t(indices[endpoint])*4;
                if(at+4>streams[endpoint].bytes)return false;
                const auto* p=streams[endpoint].data+at;
                (endpoint?after:before)[lane]=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
            }
        }
        const v8 a=_mm256_load_si256(reinterpret_cast<const v8*>(before)),b=_mm256_load_si256(reinterpret_cast<const v8*>(after));
        output[first/8]=v8_fnms(a,selection.weight,v8_fma(selection.weight,b,a));
    }
    return true;
}
} // namespace buzz
