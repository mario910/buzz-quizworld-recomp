/* Recognized image-40 job metadata -> bounded semantic input/publication plan.
 * Read-only admission; no SPU registers, ABI frames or output mutation. */
#pragma once
#include <cstdint>
#include <cstddef>

namespace buzz {
struct AnimationJobSpan { uint32_t lsa=0,bytes=0,ea=0; };
struct AnimationJobBlendNode { uint16_t operation=0,left=0,right=0;uint32_t factor=0; };
struct AnimationJobClip { uint32_t ea=0,time=0,binding_ea=0;uint16_t header_bytes=0,flags=0; };
struct AnimationJobInput {
    uint32_t context=0,descriptor=0,root_pose_lsa=0,scratch_lsa=0,dma_tag=0;
    unsigned joints=0,curve_floats=0,hierarchy_entries=0,nodes=0,clips=0,publications=0;
    AnimationJobSpan input[4],cache[2],hierarchy,output[3];
    AnimationJobBlendNode node[2];AnimationJobClip clip[3];
};
inline uint32_t animation_job_be32(const uint8_t* p)
{ return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
inline unsigned animation_job_be16(const uint8_t* p){return unsigned(p[0])<<8|p[1];}
inline bool read_animation_job_input(const uint8_t* ls,size_t bytes,uint32_t context,uint32_t descriptor,
    AnimationJobInput& output,const char** reason=nullptr)
{
    auto reject=[&](const char* message){if(reason)*reason=message;return false;};
    auto span=[&](uint32_t at,uint64_t n){return uint64_t(at)+n<=bytes;};
    if(bytes!=0x40000||!span(context,48)||!span(descriptor,256))return reject("job context/descriptor span");
    const auto* c=ls+context;const auto* d=ls+descriptor;
    if((animation_job_be32(c+20)>>28)!=2||animation_job_be16(c+24)!=4||animation_job_be16(c+26)!=2||
       animation_job_be16(d+10)!=32||animation_job_be32(d+36)!=16)return reject("job256 four-input/two-cache family");
    if(animation_job_be32(d+248))return reject("uncovered driver flags");
    AnimationJobInput plan;plan.context=context;plan.descriptor=descriptor;
    plan.root_pose_lsa=descriptor+112;plan.scratch_lsa=animation_job_be32(c+32);plan.dma_tag=animation_job_be32(c+36);
    if(plan.dma_tag>31)return reject("DMA tag");
    uint32_t cursor=animation_job_be32(c);
    for(unsigned i=0;i<4;++i) {
        const uint32_t size=animation_job_be32(d+48+i*8),ea=animation_job_be32(d+52+i*8);
        if(size&~0x7fffu||(size&&!ea)||ea&15||!span(cursor,size))return reject("input DMA span/alignment/flags");
        plan.input[i]={cursor,size,ea};cursor+=(size+15)&~15u;
    }
    if(cursor-animation_job_be32(c)>animation_job_be32(d+20))return reject("input allocation");
    for(unsigned i=0;i<2;++i) {
        const uint32_t size=animation_job_be32(d+80+i*8),ea=animation_job_be32(d+84+i*8),at=animation_job_be32(c+4+i*4);
        if(size&~0x7fffu||(size&&!ea)||ea&15||!span(at,size))return reject("cache DMA span/alignment/flags");
        plan.cache[i]={at,size,ea};
    }
    const auto* user=d+96;
    plan.nodes=animation_job_be16(user+64);plan.clips=animation_job_be16(user+66);
    if(plan.nodes<1||plan.nodes>2||plan.clips!=plan.nodes+1||plan.input[0].bytes!=plan.nodes*16||
       plan.input[1].bytes!=plan.clips*16||plan.input[3].bytes)return reject("observed control/clip family");
    unsigned seen_nodes=1,seen_clips=0;
    for(unsigned i=0;i<plan.nodes;++i) {
        const auto* p=ls+plan.input[0].lsa+i*16;auto& node=plan.node[i];
        node={uint16_t(animation_job_be16(p)),uint16_t(animation_job_be16(p+2)),uint16_t(animation_job_be16(p+4)),animation_job_be32(p+8)};
        if(node.operation>1||node.factor>0x3f800000u||animation_job_be16(p+6)||animation_job_be32(p+12))return reject("control operation/factor/padding");
        const unsigned handles[]={node.left,node.right};
        for(const unsigned handle:handles) {
            const unsigned index=handle&0x3fff;
            if((handle&0xc000)==0x8000) {if(index>=plan.clips||seen_clips&(1u<<index))return reject("clip handle");seen_clips|=1u<<index;}
            else if((handle&0xc000)==0x4000) {if(index<=i||index>=plan.nodes||seen_nodes&(1u<<index))return reject("node dependency");seen_nodes|=1u<<index;}
            else return reject("control handle kind");
        }
    }
    if(seen_nodes!=(1u<<plan.nodes)-1||seen_clips!=(1u<<plan.clips)-1)return reject("control tree coverage");
    for(unsigned i=0;i<plan.clips;++i) {
        const auto* p=ls+plan.input[1].lsa+i*16;auto& clip=plan.clip[i];
        clip={animation_job_be32(p),animation_job_be32(p+8),animation_job_be32(p+12),uint16_t(animation_job_be16(p+4)),uint16_t(animation_job_be16(p+6))};
        if(!clip.ea||(clip.ea&15)||!clip.header_bytes||(clip.header_bytes&15)||clip.flags)return reject("clip header recipe");
    }
    const auto& skeleton=plan.cache[1];
    if(skeleton.bytes<48||animation_job_be32(ls+skeleton.lsa)!=0x45533031||
       animation_job_be32(ls+skeleton.lsa+8)!=skeleton.bytes)return reject("ES01 skeleton header");
    const auto* sk=ls+skeleton.lsa;plan.joints=animation_job_be16(sk+12);plan.curve_floats=animation_job_be16(sk+14);
    plan.hierarchy_entries=animation_job_be16(sk+16)*4;
    if(!plan.joints||plan.joints>256||plan.input[2].bytes!=plan.joints*48||plan.hierarchy_entries<8||
       plan.hierarchy_entries>1024||48+plan.hierarchy_entries*4>skeleton.bytes)return reject("skeleton record counts");
    plan.hierarchy={skeleton.lsa+48,plan.hierarchy_entries*4,skeleton.ea+48};
    const uint32_t world=animation_job_be32(c+28),rounded=(plan.joints+3)&~3u;
    const uint32_t reference=world+rounded*64,curves=reference+rounded*48+48,curve_bytes=(plan.curve_floats*4+15)&~15u;
    const uint32_t world_ea=animation_job_be32(user+4),reference_ea=animation_job_be32(user+68),curve_ea=animation_job_be32(user+76);
    if(animation_job_be32(user)||animation_job_be32(user+72)||!world_ea||!reference_ea||
       (world_ea|reference_ea|curve_ea)&15)return reject("publication addresses");
    if((world&15)||!span(world,uint64_t(rounded)*112+48+curve_bytes)||uint64_t(curves)+curve_bytes>plan.scratch_lsa||
       uint64_t(rounded)*112+48+curve_bytes>animation_job_be32(d+24))return reject("output allocation");
    plan.output[0]={reference,plan.joints*48,reference_ea};plan.output[1]={world,plan.joints*64,world_ea};
    plan.publications=2;
    if(curve_ea){if(!curve_bytes||curve_bytes>16384)return reject("curve publication size");plan.output[2]={curves,curve_bytes,curve_ea};++plan.publications;}
    output=plan;if(reason)*reason=nullptr;return true;
}
} // namespace buzz
