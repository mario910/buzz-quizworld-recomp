/* One raw animation clip -> job-owned host Q/T/S and curve arrays. */
#pragma once
#include "native_animation_clip_input.h"
#include "native_animation_host_vectors.h"
#include "native_animation_host_hierarchy.h"

namespace buzz {
struct AnimationHostClipPose {
    AnimationHostPoseArrays pose;
    alignas(32) uint32_t curve[256];
};
struct AnimationHostClipWorkspace {
    GeometryHostSoA8 values[32];
    v8 scalars[32];
};
inline bool read_animation_pose_defaults(AnimationByteView skeleton,unsigned joints,AnimationByteView& output)
{
    if(!skeleton.data||skeleton.bytes<48||!joints||joints>256)return false;
    const auto* p=skeleton.data;
    auto be32=[](const uint8_t* v){return uint32_t(v[0])<<24|uint32_t(v[1])<<16|uint32_t(v[2])<<8|v[3];};
    if(be32(p)!=0x45533031||be32(p+8)!=skeleton.bytes||(unsigned(p[12])<<8|p[13])!=joints)return false;
    const uint64_t relative=be32(p+20),at=20+relative;
    if(!relative||at+uint64_t(joints)*48>skeleton.bytes)return false;
    output={p+at,size_t(joints)*48};return true;
}
inline void scatter_animation_clip_values8(const GeometryHostSoA8& value,AnimationHostPoseArrays& output,
    unsigned component,const uint8_t* ids,unsigned count)
{
    alignas(32) uint32_t words[4][8];
    for(unsigned i=0;i<4;++i)_mm256_store_si256(reinterpret_cast<v8*>(words[i]),value.component[i]);
    for(unsigned lane=0;lane<count;++lane) {
        const unsigned id=unsigned(ids[lane*2])<<8|ids[lane*2+1];
        for(unsigned i=0;i<4;++i)output.component[component+i][id]=words[i][lane];
    }
}
inline bool decode_animation_pose_defaults(AnimationByteView defaults,unsigned joints,AnimationHostPoseArrays& output)
{
    if(joints>256||defaults.bytes<uint64_t(joints)*48||(joints&&!defaults.data))return false;
    for(unsigned first=0;first<joints;first+=8) {
        const unsigned count=joints-first<8?joints-first:8;
        const auto pose=decode_animation_host_pose8(defaults.data+first*48,count);
        uint16_t ids[8];for(unsigned lane=0;lane<count;++lane)ids[lane]=first+lane;
        scatter_animation_host_poses8(pose,output,ids,count);
    }
    return true;
}
// The clip plan has already admitted its IDs and raw stream spans. Rejection
// discards host arrays before any published buffer is changed.
inline bool produce_animation_host_clip(const AnimationClipInput& clip,const AnimationHostPoseArrays& defaults,
    unsigned curves,const uint32_t* curve_padding,unsigned padding_words,AnimationHostClipPose& output,AnimationHostClipWorkspace& workspace)
{
    if(!clip.joints||clip.joints>256||curves>256)return false;
    const unsigned capacity=(curves+3)&~3u;
    if(padding_words<capacity-curves||((capacity-curves)&&!curve_padding))return false;
    for(unsigned component=0;component<12;++component)
        memcpy(output.pose.component[component],defaults.component[component],clip.joints*4);
    for(unsigned i=0;i<curves;++i)output.curve[i]=0;
    for(unsigned i=curves;i<capacity;++i) {
        output.curve[i]=curve_padding[i-curves];
    }
    if(!decode_animation_host_static_quaternions(clip.static_values[0],clip.static_count[0],workspace.values))return false;
    for(unsigned first=0;first<clip.static_count[0];first+=8)
        scatter_animation_clip_values8(workspace.values[first/8],output.pose,0,clip.static_ids[0].data+first*2,
            clip.static_count[0]-first<8?clip.static_count[0]-first:8);
    if(!sample_animation_host_quaternions(clip.animated[0],workspace.values))return false;
    for(unsigned first=0;first<clip.animated_count[0];first+=8)
        scatter_animation_clip_values8(workspace.values[first/8],output.pose,0,clip.animated_ids[0].data+first*2,
            clip.animated_count[0]-first<8?clip.animated_count[0]-first:8);
    for(unsigned kind=1;kind<=2;++kind) {
        for(unsigned first=0;first<clip.static_count[kind];first+=8) {
            const unsigned count=clip.static_count[kind]-first<8?clip.static_count[kind]-first:8;
            const auto value=decode_geometry_host_positions8(clip.static_values[kind].data+first*12,count);
            scatter_animation_clip_values8(value,output.pose,kind*4,clip.static_ids[kind].data+first*2,count);
        }
        if(!sample_animation_host_vectors(clip.animated[kind],workspace.values))return false;
        for(unsigned first=0;first<clip.animated_count[kind];first+=8)
            scatter_animation_clip_values8(workspace.values[first/8],output.pose,kind*4,clip.animated_ids[kind].data+first*2,
                clip.animated_count[kind]-first<8?clip.animated_count[kind]-first:8);
    }
    for(unsigned i=0;i<clip.static_count[3];++i) {
        const auto* id=clip.static_ids[3].data+i*2;const auto* p=clip.static_values[3].data+i*4;
        output.curve[unsigned(id[0])<<8|id[1]]=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
    }
    AnimationHostCurveKeyPlan ownership;
    if(!plan_animation_host_curve_keys(clip.animated_ids[3],clip.animated_count[3],capacity,ownership))return false;
    auto recipe=clip.animated[3];recipe.tracks=ownership.tracks;
    if(!sample_animation_host_curve_keys(recipe,ownership,workspace.scalars))return false;
    for(unsigned first=0;first<ownership.tracks;first+=8) {
        alignas(32) uint32_t words[8];_mm256_store_si256(reinterpret_cast<v8*>(words),workspace.scalars[first/8]);
        for(unsigned lane=0;lane<8&&first+lane<ownership.tracks;++lane)if(ownership.publish[first+lane])
            output.curve[ownership.destination[first+lane]]=words[lane];
    }
    return true;
}
} // namespace buzz
