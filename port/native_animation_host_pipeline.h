/* Host control tree -> hierarchy -> final published animation streams. */
#pragma once
#include "native_animation_job_input.h"
#include "native_animation_host_clip.h"
#include "native_animation_host_additive.h"
#include "native_animation_host_matrices.h"

namespace buzz {
struct AnimationHostControlPlan {
    unsigned joints=0,curves=0,nodes=0,clips=0;
    AnimationJobBlendNode node[2];
    uint8_t clamp[256]{};
};
struct AnimationHostPipelineWorkspace {
    AnimationHostClipPose leaf[3],node[2];
    AnimationHostPoseArrays world;
};
inline bool plan_animation_host_control(const AnimationJobInput& job,AnimationByteView skeleton,
    AnimationHostControlPlan& output)
{
    if(!job.joints||job.joints>256||job.curve_floats>256||!job.nodes||job.nodes>2||job.clips!=job.nodes+1)
        return false;
    AnimationHostControlPlan plan;
    plan.joints=job.joints;plan.curves=job.curve_floats;plan.nodes=job.nodes;plan.clips=job.clips;
    for(unsigned i=0;i<job.nodes;++i) {
        const auto n=job.node[i];
        if(n.operation>1||n.factor>0x3f800000||(n.operation==1&&n.factor!=0x3f800000))return false;
        const unsigned handles[]={n.left,n.right};
        for(unsigned h:handles) {
            const unsigned index=h&0x3fff;
            if((h&0xc000)==0x8000){if(index>=job.clips)return false;}
            else if((h&0xc000)==0x4000){if(index<=i||index>=job.nodes)return false;}
            else return false;
        }
        plan.node[i]=n;
    }
    if(job.curve_floats) {
        if(!skeleton.data||skeleton.bytes<48)return false;
        const uint64_t relative=animation_job_be32(skeleton.data+40),at=40+relative;
        const unsigned count=(job.curve_floats+3)&~3u;
        if(!relative||at+count>skeleton.bytes)return false;
        for(unsigned i=0;i<count;++i){const unsigned flag=skeleton.data[at+i];if(flag>1)return false;plan.clamp[i]=flag;}
    }
    output=plan;return true;
}
inline const AnimationHostClipPose& animation_control_input(unsigned handle,const AnimationHostPipelineWorkspace& workspace)
{
    return (handle&0xc000)==0x8000?workspace.leaf[handle&0x3fff]:workspace.node[handle&0x3fff];
}
inline const AnimationHostClipPose& produce_animation_host_control(const AnimationHostControlPlan& plan,
    AnimationHostPipelineWorkspace& workspace)
{
    for(unsigned n=plan.nodes;n--;) {
        const auto& node=plan.node[n];auto& output=workspace.node[n];
        const auto& a=animation_control_input(node.left,workspace);const auto& b=animation_control_input(node.right,workspace);
        for(unsigned first=0;first<plan.joints;first+=8) {
            const unsigned lanes=plan.joints-first<8?plan.joints-first:8;
            uint16_t ids[8];for(unsigned lane=0;lane<lanes;++lane)ids[lane]=first+lane;
            const auto x=gather_animation_host_poses8(a.pose,ids,lanes),y=gather_animation_host_poses8(b.pose,ids,lanes);
            const auto result=node.operation?add_animation_host_poses8(x,y):blend_animation_host_poses8(x,y,v8_set(node.factor));
            scatter_animation_host_poses8(result,output.pose,ids,lanes);
        }
        const unsigned count=(plan.curves+3)&~3u;
        for(unsigned first=0;first<count;first+=8) {
            const unsigned lanes=count-first<8?count-first:8;
            const v8 active=_mm256_cmpgt_epi32(v8_set(lanes),_mm256_setr_epi32(0,1,2,3,4,5,6,7));
            const v8 x=_mm256_maskload_epi32(reinterpret_cast<const int*>(a.curve+first),active);
            const v8 y=_mm256_maskload_epi32(reinterpret_cast<const int*>(b.curve+first),active);
            alignas(32) uint32_t flags[8]{};
            for(unsigned lane=0;lane<lanes;++lane)flags[lane]=plan.clamp[first+lane]?~0u:0;
            const v8 clamp=_mm256_load_si256(reinterpret_cast<const v8*>(flags));
            const v8 value=node.operation?add_animation_host_curves8(x,y,clamp):blend_animation_host_curves8(x,y,v8_set(node.factor),clamp);
            _mm256_maskstore_epi32(reinterpret_cast<int*>(output.curve+first),active,value);
        }
    }
    return workspace.node[0];
}
// Inputs have been admitted once by the job frontend. Arrays remain LE SoA
// across control/hierarchy/matrix stages; only final published streams are BE.
inline void produce_animation_host_outputs(const AnimationHostControlPlan& control,
    const AnimationHostHierarchyPlan& hierarchy,const AnimationHostPose8& root,
    const uint8_t* reference,AnimationHostPipelineWorkspace& workspace,
    uint8_t* reference_output,uint8_t* world_output,uint8_t* curve_output)
{
    const auto& local=produce_animation_host_control(control,workspace);
    produce_animation_host_hierarchy(hierarchy,local.pose,root,workspace.world);
    for(unsigned first=0;first<control.joints;first+=8) {
        const unsigned count=control.joints-first<8?control.joints-first:8;
        uint16_t ids[8];for(unsigned lane=0;lane<count;++lane)ids[lane]=first+lane;
        const auto matrix=make_animation_host_world_matrices8(gather_animation_host_poses8(workspace.world,ids,count));
        AnimationHostAffine8 affine;for(unsigned i=0;i<12;++i)affine.coefficient[i]=matrix.coefficient[i];
        const auto adjusted=compose_animation_host_affines8(affine,decode_animation_host_affines8(reference+first*48,48,count));
        emit_animation_host_affines8(adjusted,reference_output+first*48,count);
        emit_animation_host_world_matrices8(matrix,world_output+first*64,count);
    }
    for(unsigned i=0;i<((control.curves+3)&~3u);++i)for(unsigned byte=0;byte<4;++byte)
        curve_output[i*4+byte]=uint8_t(local.curve[i]>>(24-byte*8));
}
} // namespace buzz
