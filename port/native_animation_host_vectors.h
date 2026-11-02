/* Raw FLOAT3 animation keys -> eight host vectors. Shared temporal selection,
 * bounded streams, no SPU registers, ABI frames or speculative key reads. */
#pragma once
#include "native_animation_host_quaternions.h"

namespace buzz {
using AnimationHostVector8 = GeometryHostSoA8;
using AnimationVectorTracks = AnimationKeyTracks;
inline bool sample_animation_host_vectors(const AnimationVectorTracks& recipe,AnimationHostVector8* output)
{
    if(!animation_track_recipe_valid(recipe,12))return false;
    unsigned key_prefix=0;
    for(unsigned first=0;first<recipe.tracks;first+=8) {
        AnimationTrackSelection8 selection;
        select_animation_host_tracks8(recipe,first,key_prefix,selection);
        alignas(32) uint32_t before[3][8]{},after[3][8]{};
        for(unsigned lane=0;lane<8&&first+lane<recipe.tracks;++lane) {
            const AnimationByteView streams[]={selection.before_stream[lane],selection.after_stream[lane]};
            const unsigned indices[]={selection.before_index[lane],selection.after_index[lane]};
            for(unsigned endpoint=0;endpoint<2;++endpoint) {
                const uint64_t at=uint64_t(indices[endpoint])*12;
                if(at+12>streams[endpoint].bytes)return false;
                for(unsigned component=0;component<3;++component) {
                    const auto* p=streams[endpoint].data+at+component*4;
                    (endpoint?after:before)[component][lane]=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
                }
            }
        }
        for(unsigned component=0;component<3;++component) {
            const v8 a=_mm256_load_si256(reinterpret_cast<const v8*>(before[component]));
            const v8 b=_mm256_load_si256(reinterpret_cast<const v8*>(after[component]));
            // Preserve both SPU fused roundings: (a + t*b) - a*t.
            output[first/8].component[component]=v8_fnms(a,selection.weight,v8_fma(selection.weight,b,a));
        }
        output[first/8].component[3]=v8_set(0x3f800000);
    }
    return true;
}
} // namespace buzz
