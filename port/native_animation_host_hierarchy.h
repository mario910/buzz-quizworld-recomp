/* Dependency-aware animation hierarchy on host pose arrays. */
#pragma once
#include "native_animation_host_pose.h"
#include "native_animation_host_quaternions.h"

namespace buzz {
struct AnimationHostHierarchyPlan {
    unsigned joints=0,batches=0;
    uint16_t order[256]{},parent[256]{};
    uint8_t no_scale_inheritance[256]{},batch_size[256]{};
};
// Admit covered four-record schedule groups, canonicalize repeats and build batches
// whose parents are complete. No pose or published buffer is mutated here.
inline bool plan_animation_host_hierarchy(AnimationByteView stream,unsigned entries,unsigned joints,
    AnimationHostHierarchyPlan& plan)
{
    if(joints>256||(entries&3)||(joints&&entries<8)||entries>1024||uint64_t(entries)*4>stream.bytes)return false;
    uint32_t definition[256];uint16_t head[256],next[256],ready[256];
    for(unsigned i=0;i<256;++i){definition[i]=0xffffffffu;head[i]=next[i]=0xffff;}
    for(unsigned i=0;i<entries;++i) {
        const auto* p=stream.data+i*4;
        const uint32_t raw=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
        const unsigned child=raw>>16,parent=raw&0x7fff;
        if(child>=joints)return false;
        const uint32_t canonical=(raw&0x8000)|(parent>0x3fff?0x7fff:parent);
        if(definition[child]!=0xffffffffu&&definition[child]!=canonical)return false;
        definition[child]=canonical;
    }
    unsigned begin=0,end=0,produced=0;
    plan.joints=joints;plan.batches=0;
    for(unsigned i=0;i<joints;++i) {
        if(definition[i]==0xffffffffu)return false;
        const unsigned parent=definition[i]&0x7fff;
        if(parent!=0x7fff&&parent>=joints)return false;
        plan.parent[i]=parent==0x7fff?0xffff:parent;
        plan.no_scale_inheritance[i]=(definition[i]&0x8000)!=0;
        if(parent==0x7fff)ready[end++]=i;
    }
    for(unsigned i=joints;i--;)if(plan.parent[i]!=0xffff){next[i]=head[plan.parent[i]];head[plan.parent[i]]=i;}
    while(begin<end) {
        const unsigned count=end-begin<8?end-begin:8;
        plan.batch_size[plan.batches++]=count;
        for(unsigned lane=0;lane<count;++lane)plan.order[produced+lane]=ready[begin+lane];
        for(unsigned lane=0;lane<count;++lane)for(unsigned child=head[ready[begin+lane]];child!=0xffff;child=next[child])ready[end++]=child;
        produced+=count;begin+=count;
    }
    return produced==joints;
}
// Compose parent * local, with the original quaternion product and
// normalization order. Each vector lane belongs to an independent joint.
inline AnimationHostPose8 compose_animation_host_poses8(const AnimationHostPose8& parent,
    const AnimationHostPose8& local,v8 no_scale_inheritance)
{
    const v8 x=parent.rotation.component[0],y=parent.rotation.component[1],
        z=parent.rotation.component[2],w=parent.rotation.component[3];
    const v8 a=local.rotation.component[0],b=local.rotation.component[1],
        c=local.rotation.component[2],d=local.rotation.component[3];
    v8 q[4];
    q[0]=v8_fnms(z,b,v8_fma(y,c,v8_fma(x,d,v8_fm(w,a))));
    q[1]=v8_fma(z,a,v8_fma(y,d,v8_fnms(x,c,v8_fm(w,b))));
    q[2]=v8_fma(z,d,v8_fnms(y,a,v8_fma(x,b,v8_fm(w,c))));
    q[3]=v8_fnms(z,c,v8_fnms(y,b,v8_fnms(x,a,v8_fm(w,d))));
    v8 norm=v8_fm(q[0],q[0]);
    for(unsigned component=1;component<4;++component)norm=v8_fma(q[component],q[component],norm);
    const v8 estimate=v8_fi(norm,v8_frsqest(norm)),one=v8_set(0x3f800000);
    const v8 inverse=v8_fma(v8_fm(estimate,v8_set(0x3f000000)),
        v8_fnms(v8_fm(norm,estimate),estimate,one),estimate);
    AnimationHostPose8 result;
    v8 t[3];
    for(unsigned component=0;component<4;++component) {
        result.rotation.component[component]=v8_fm(q[component],inverse);
        result.scale.component[component]=v8_fm(local.scale.component[component],
            _mm256_blendv_epi8(parent.scale.component[component],one,no_scale_inheritance));
        if(component<3)t[component]=v8_fm(local.translation.component[component],parent.scale.component[component]);
    }
    // Quaternion-vector rotation: t + 2 * cross(q.xyz,
    // q.w*t + cross(q.xyz,t)), keeping every original fused rounding.
    const v8 vx=v8_fma(w,t[0],v8_fnms(z,t[1],v8_fm(y,t[2])));
    const v8 vy=v8_fma(w,t[1],v8_fnms(x,t[2],v8_fm(z,t[0])));
    const v8 vz=v8_fma(w,t[2],v8_fnms(y,t[0],v8_fm(x,t[1])));
    const v8 rotated[]={
        v8_fma(v8_set(0x40000000),v8_fnms(z,vy,v8_fm(y,vz)),t[0]),
        v8_fma(v8_set(0x40000000),v8_fnms(x,vz,v8_fm(z,vx)),t[1]),
        v8_fma(v8_set(0x40000000),v8_fnms(y,vx,v8_fm(x,vy)),t[2])};
    for(unsigned component=0;component<3;++component)
        result.translation.component[component]=animation_add8(rotated[component],parent.translation.component[component]);
    result.translation.component[3]=animation_add8(one,parent.translation.component[3]);
    return result;
}
struct alignas(32) AnimationHostPoseArrays { uint32_t component[12][256]{}; };
inline AnimationHostPose8 gather_animation_host_poses8(const AnimationHostPoseArrays& poses,
    const uint16_t* indices,unsigned count,const AnimationHostPose8* root=nullptr)
{
    alignas(32) uint32_t offsets[8]{},active[8]{},root_lane[8]{};
    bool roots=false;
    for(unsigned lane=0;lane<count;++lane) {
        if(indices[lane]!=0xffff){offsets[lane]=indices[lane];active[lane]=~0u;}
        else {root_lane[lane]=~0u;roots=true;}
    }
    const v8 index=_mm256_load_si256(reinterpret_cast<const v8*>(offsets));
    const v8 mask=_mm256_load_si256(reinterpret_cast<const v8*>(active));
    const v8 root_mask=_mm256_load_si256(reinterpret_cast<const v8*>(root_lane));
    AnimationHostPose8 result;
    for(unsigned component=0;component<12;++component) {
        // Neutral inactive quaternions avoid reciprocal-root repair on lanes
        // that have no joint; masked gathers never read their source words.
        v8 value=_mm256_mask_i32gather_epi32(v8_set(component==3||component>=8?0x3f800000:0),
            reinterpret_cast<const int*>(poses.component[component]),index,mask,4);
        if(roots) {
            const auto& source=component<4?root->rotation:component<8?root->translation:root->scale;
            uint32_t word;memcpy(&word,&source.component[component%4],4);
            value=_mm256_blendv_epi8(value,v8_set(word),root_mask);
        }
        auto& destination=component<4?result.rotation:component<8?result.translation:result.scale;
        destination.component[component%4]=value;
    }
    return result;
}
inline void scatter_animation_host_poses8(const AnimationHostPose8& values,AnimationHostPoseArrays& output,
    const uint16_t* indices,unsigned count)
{
    alignas(32) uint32_t words[12][8];
    for(unsigned component=0;component<4;++component) {
        _mm256_store_si256(reinterpret_cast<v8*>(words[component]),values.rotation.component[component]);
        _mm256_store_si256(reinterpret_cast<v8*>(words[4+component]),values.translation.component[component]);
        _mm256_store_si256(reinterpret_cast<v8*>(words[8+component]),values.scale.component[component]);
    }
    for(unsigned lane=0;lane<count;++lane)for(unsigned component=0;component<12;++component)
        output.component[component][indices[lane]]=words[component][lane];
}
inline void produce_animation_host_hierarchy(const AnimationHostHierarchyPlan& plan,
    const AnimationHostPoseArrays& local,const AnimationHostPose8& root,AnimationHostPoseArrays& world)
{
    unsigned first=0;
    for(unsigned batch=0;batch<plan.batches;++batch) {
        const unsigned count=plan.batch_size[batch];uint16_t parents[8];alignas(32) uint32_t flags[8]{};
        for(unsigned lane=0;lane<count;++lane) {
            const unsigned child=plan.order[first+lane];parents[lane]=plan.parent[child];
            flags[lane]=plan.no_scale_inheritance[child]?~0u:0;
        }
        const auto input=gather_animation_host_poses8(local,plan.order+first,count);
        const auto parent=gather_animation_host_poses8(world,parents,count,&root);
        const auto result=compose_animation_host_poses8(parent,input,_mm256_load_si256(reinterpret_cast<const v8*>(flags)));
        scatter_animation_host_poses8(result,world,plan.order+first,count);first+=count;
    }
}
} // namespace buzz
