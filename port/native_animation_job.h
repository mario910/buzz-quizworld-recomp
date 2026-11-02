/* Recognized animation job: read-only preparation followed by ordered I/O.
 * No lifted blocks, register bank, SPU stack frames or scratch reconstruction. */
#pragma once
#include "native_animation_clip_time.h"
#include "native_animation_host_pipeline.h"

namespace buzz {
struct AnimationNativeJobPlan {
    AnimationJobInput job;
    AnimationClipTimeInput header[3];
    AnimationClipTimeSelection body[3];
};
struct AnimationNativeJobWorkspace {
    AnimationHostPipelineWorkspace pipeline;
    AnimationHostPoseArrays defaults;
    AnimationHostClipWorkspace clip;
    alignas(32) uint8_t body[3][16384+1548];
};
struct AnimationNativeJobOutputs {uint8_t* reference;uint8_t* world;uint8_t* curve;};

// Resolve exposes bounded, immutable guest input bytes without issuing DMA.
// All eligibility and computation finish before any channel/LS mutation.
template<class Resolve>
inline bool prepare_native_animation_job(const AnimationJobInput& job,const uint8_t* ls,size_t ls_bytes,bool interrupts_enabled,
    Resolve&& resolve,AnimationNativeJobPlan& output,AnimationNativeJobWorkspace& workspace,
    AnimationNativeJobOutputs destination,const char** reason=nullptr)
{
    auto reject=[&](const char* message){if(reason)*reason=message;return false;};
    if(interrupts_enabled)return reject("enabled interrupt entry requires whole-job fallback");
    if(!ls||ls_bytes!=0x40000||!job.joints||job.joints>256||job.curve_floats>256||
       job.publications<2||job.publications>3||job.dma_tag>31||job.nodes<1||job.nodes>2||job.clips!=job.nodes+1||
       uint64_t(job.descriptor)+256>ls_bytes)return reject("animation I/O job family");
    if(job.nodes==1) {
        if(job.node[0].operation||job.node[0].left!=0x8000||job.node[0].right!=0x8001)
            return reject("uncovered two-clip schedule");
    } else if(job.node[0].operation!=1||job.node[0].factor!=0x3f800000||
        job.node[0].left!=0x4001||job.node[0].right!=0x8002||job.node[1].operation||
        job.node[1].left!=0x8000||job.node[1].right!=0x8001)return reject("uncovered three-clip schedule");
    const unsigned worker=animation_job_be32(ls+0x1c8);
    if(worker>5||job.cache[0].bytes<(worker+1)*8||uint64_t(job.cache[0].lsa)+job.cache[0].bytes>ls_bytes)
        return reject("animation worker cache span");
    const auto* allocation=ls+job.cache[0].lsa+worker*8;
    if(animation_job_be32(allocation)||animation_job_be32(allocation+4))return reject("uncovered worker allocation override");
    constexpr unsigned pitch=8192+256;
    const uint64_t scratch_bytes=uint64_t(animation_job_be16(ls+job.descriptor+30))*16;
    const uint64_t pose_base=uint64_t(job.scratch_lsa)+3*pitch;
    const unsigned pose_bytes=((job.joints+4)&~3u)*48,curve_bytes=((job.curve_floats+4)&~3u)*4;
    const unsigned weight_bytes=(((job.joints+3)&~3u)+((job.curve_floats+3)&~3u)+15)&~15u;
    const unsigned pose_stride=pose_bytes+curve_bytes+weight_bytes+16;
    if(uint64_t(job.scratch_lsa)+scratch_bytes>ls_bytes||pose_base+3*pose_stride>uint64_t(job.scratch_lsa)+scratch_bytes)
        return reject("animation buffer allocation");
    if(!destination.reference||!destination.world||(job.curve_floats&&!destination.curve))return reject("animation output pointers");
    for(unsigned i=0;i<job.publications;++i) {
        const auto& out=job.output[i];const unsigned expected=i==0?job.joints*48:i==1?job.joints*64:((job.curve_floats+3)&~3u)*4;
        if(!out.ea||!expected||out.bytes!=expected||((out.lsa|out.ea|out.bytes)&15)||uint64_t(out.lsa)+out.bytes>job.scratch_lsa)
            return reject("animation publication span");
    }
    const auto& skeleton=job.cache[1];
    if(uint64_t(skeleton.lsa)+skeleton.bytes>ls_bytes)return reject("animation skeleton span");
    const AnimationByteView sk{ls+skeleton.lsa,skeleton.bytes};
    AnimationByteView defaults;AnimationHostControlPlan control;AnimationHostHierarchyPlan hierarchy;
    if(!read_animation_pose_defaults(sk,job.joints,defaults)||!decode_animation_pose_defaults(defaults,job.joints,workspace.defaults)||
       !plan_animation_host_control(job,sk,control)||uint64_t(job.hierarchy.lsa)+job.hierarchy.bytes>ls_bytes||
       !plan_animation_host_hierarchy({ls+job.hierarchy.lsa,job.hierarchy.bytes},job.hierarchy_entries,job.joints,hierarchy))
        return reject("animation defaults/control/hierarchy");
    AnimationNativeJobPlan plan;plan.job=job;
    for(unsigned i=0;i<job.clips;++i) {
        const auto& clip=job.clip[i];
        const auto h=resolve(clip.ea,clip.header_bytes);
        if(h.bytes!=clip.header_bytes||clip.header_bytes>8192)return reject("animation header input");
        plan.header[i]={h,clip.ea,job.scratch_lsa+(i==1?0:2*pitch),clip.time};
    }
    if(!select_animation_clip_times8(plan.header,job.clips,plan.body,reason))return false;
    const unsigned curves=job.curve_floats,capacity=(curves+3)&~3u;
    uint32_t padding[2][3]{};
    for(unsigned slot=0;slot<2;++slot) {
        const uint64_t at=pose_base+slot*pose_stride+pose_bytes+curves*4;
        if(at+(capacity-curves)*4>ls_bytes)return reject("curve buffer entry input");
        for(unsigned i=0;i<capacity-curves;++i)padding[slot][i]=animation_job_be32(ls+at+i*4);
    }
    for(unsigned i=0;i<job.clips;++i) {
        const auto& b=plan.body[i];const auto& h=plan.header[i];
        if(uint64_t(b.lsa)+b.bytes>uint64_t(h.lsa)+pitch||uint64_t(b.lsa)+b.bytes+1548>ls_bytes)
            return reject("clip arena span");
        const auto raw=resolve(b.ea,b.bytes);if(!raw.data||raw.bytes!=b.bytes)return reject("animation body input");
        memcpy(workspace.body[i],raw.data,b.bytes);
        memcpy(workspace.body[i]+b.bytes,ls+b.lsa+b.bytes,1548);
        // Published padding may retain bytes from a previous GET in this
        // arena. Keep only that input provenance, never an SPU scratch image.
        auto overlay=[&](uint32_t lsa,AnimationByteView bytes) {
            const uint64_t begin=uint64_t(b.lsa)+b.bytes,end=begin+1548;
            const uint64_t lo=begin>lsa?begin:lsa,hi=end<uint64_t(lsa)+bytes.bytes?end:uint64_t(lsa)+bytes.bytes;
            if(lo<hi)memcpy(workspace.body[i]+b.bytes+size_t(lo-begin),bytes.data+size_t(lo-lsa),size_t(hi-lo));
        };
        for(unsigned prior=0;prior<i;++prior) {
            overlay(plan.header[prior].lsa,plan.header[prior].header);
            overlay(plan.body[prior].lsa,{workspace.body[prior],plan.body[prior].bytes});
        }
        overlay(h.lsa,h.header);
        AnimationClipInput clip;
        if(!read_animation_clip_input(h.header,{workspace.body[i],b.bytes},{workspace.body[i]+b.bytes,1548},
            capacity,b.interior_frames,b.frame,b.fraction,clip,reason))return false;
        if(clip.joints!=job.joints||uint64_t(b.lsa)+b.bytes+clip.published_tail_bytes>pose_base)
            return reject("clip output/input ownership");
        const unsigned slot=i==0?0:1;
        if(!produce_animation_host_clip(clip,workspace.defaults,curves,padding[slot],capacity-curves,
            workspace.pipeline.leaf[i],workspace.clip))return reject("native clip sampling");
        for(unsigned n=0;n<capacity-curves;++n)padding[slot][n]=workspace.pipeline.leaf[i].curve[curves+n];
    }
    if(uint64_t(job.root_pose_lsa)+48>ls_bytes||uint64_t(job.input[2].lsa)+job.joints*48>ls_bytes)
        return reject("animation root/reference span");
    produce_animation_host_outputs(control,hierarchy,decode_animation_host_pose8(ls+job.root_pose_lsa,1),
        ls+job.input[2].lsa,workspace.pipeline,destination.reference,destination.world,destination.curve);
    output=plan;if(reason)*reason=nullptr;return true;
}

// Io supplies read/write(channel,value), set_interrupts(bool), ls() and finish().
// Waits on pose-buffer tags remain observable even when host arrays need no DMA.
template<class Io>
inline void execute_native_animation_job(const AnimationNativeJobPlan& plan,AnimationNativeJobOutputs data,Io& io)
{
    auto begin=[&](){const bool enabled=(io.read(SPU_RdMachStat)&1)!=0;io.set_interrupts(false);return enabled;};
    auto end=[&](bool enabled){if(enabled)io.set_interrupts(true);};
    auto wait=[&](unsigned tag){io.write(MFC_WrTagMask,1u<<tag);io.write(MFC_WrTagUpdate,2);(void)io.read(MFC_RdTagStat);};
    auto transfer=[&](uint32_t lsa,uint32_t ea,uint32_t bytes,unsigned tag,unsigned command) {
        io.write(MFC_LSA,lsa);io.write(MFC_EAH,0);io.write(MFC_EAL,ea);io.write(MFC_Size,bytes);io.write(MFC_TagID,tag);io.write(MFC_Cmd,command);
    };
    auto header=[&](unsigned i){const bool enabled=begin();const auto& h=plan.header[i];transfer(h.lsa,h.ea,unsigned(h.header.bytes),i==1?28:30,65);end(enabled);};
    auto body=[&](unsigned i){const bool enabled=begin();const unsigned tag=i==1?28:30;wait(tag);const auto& b=plan.body[i];transfer(b.lsa,b.ea,b.bytes,tag,65);end(enabled);};
    auto acquire=[&](unsigned tag){const bool enabled=begin();wait(tag);end(enabled);};
    auto settle_node=[&](){acquire(2);acquire(2);acquire(3);acquire(0);};
    header(0);header(1);body(0);body(1);
    acquire(2);acquire(30);wait(2);
    if(plan.job.clips==3)header(2);
    acquire(3);acquire(28);wait(3);
    if(plan.job.clips==3)body(2);
    settle_node();
    if(plan.job.clips==3){acquire(3);acquire(30);wait(3);settle_node();}
    acquire(2);acquire(3);
    const uint8_t* sources[]={data.reference,data.world,data.curve};
    for(unsigned i=0;i<plan.job.publications;++i) {
        const auto& out=plan.job.output[i];memcpy(io.ls()+out.lsa,sources[i],out.bytes);
        transfer(out.lsa,out.ea,out.bytes,plan.job.dma_tag,32);
    }
    io.finish();
}
} // namespace buzz
