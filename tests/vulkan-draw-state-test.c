#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HANDLE(type,n) ((type)(uintptr_t)(n))
static struct {VkCommandBuffer cmd;} s_vk;
static struct {unsigned draws;} s_batch;
static uint64_t buzz_uniform_generation=1;
static int buzz_uniform_generation_exhausted;
static unsigned calls[6];
static VkViewport actual_vp;
static VkRect2D actual_sc;
static float actual_lo,actual_hi;
static uint32_t actual_ref;
static VkDescriptorSet actual_set;
static VkPipeline actual_pipe;
static VkPipelineLayout actual_layout;
static void pvkCmdBindPipeline(VkCommandBuffer cmd,VkPipelineBindPoint point,VkPipeline pipe){assert(cmd==s_vk.cmd);calls[0]++;actual_pipe=pipe;}
static void pvkCmdSetViewport(VkCommandBuffer cmd,uint32_t first,uint32_t n,const VkViewport* vp){assert(!first&&n==1);calls[1]++;actual_vp=*vp;}
static void pvkCmdSetScissor(VkCommandBuffer cmd,uint32_t first,uint32_t n,const VkRect2D* sc){assert(!first&&n==1);calls[2]++;actual_sc=*sc;}
static void pvkCmdSetStencilReference(VkCommandBuffer cmd,VkStencilFaceFlags flags,uint32_t ref){assert(flags==VK_STENCIL_FACE_FRONT_AND_BACK);calls[3]++;actual_ref=ref;}
static void pvkCmdSetDepthBounds(VkCommandBuffer cmd,float lo,float hi){calls[4]++;actual_lo=lo;actual_hi=hi;}
static void pvkCmdBindDescriptorSets(VkCommandBuffer cmd,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,uint32_t n,const VkDescriptorSet* set,uint32_t offsets,const uint32_t* dynamic){assert(!first&&n==1&&!offsets&&!dynamic);calls[5]++;actual_layout=layout;actual_set=*set;}
#define BUZZ_VK_DRAW_STATE_DEFAULT 1
#include "vulkan_draw_state.h"
static VkViewport vp={0,0,128,64,0,1};
static VkRect2D sc={{0,0},{128,64}};
static VkPipeline pipe;
static VkPipelineLayout layout;
static VkDescriptorSet set;
static uint32_t reference=1;
static float low=0,high=1;
static void draw(int reuse) {
    buzz_draw_state_begin(reuse,pipe);buzz_draw_viewport(&vp);buzz_draw_scissor(&sc);
    buzz_draw_stencil(reference);buzz_draw_depth(low,high);buzz_draw_descriptor(layout,set);
    assert(actual_pipe==pipe && actual_layout==layout && actual_set==set && actual_ref==reference);
    assert(!memcmp(&actual_vp,&vp,sizeof vp)&&!memcmp(&actual_sc,&sc,sizeof sc));
    assert(!memcmp(&actual_lo,&low,sizeof low)&&!memcmp(&actual_hi,&high,sizeof high));
    s_batch.draws++;
}
int main(void) {
    s_vk.cmd=HANDLE(VkCommandBuffer,1);pipe=HANDLE(VkPipeline,2);
    layout=HANDLE(VkPipelineLayout,3);set=HANDLE(VkDescriptorSet,4);
    draw(0);for(unsigned i=0;i<1000;i++)draw(1);
    for(unsigned i=0;i<6;i++)assert(calls[i]==1);
    float* fields[]={&vp.x,&vp.y,&vp.width,&vp.height,&vp.minDepth,&vp.maxDepth};
    for(unsigned i=0;i<6;i++){unsigned before=calls[1];*fields[i]+=0.125f;draw(1);assert(calls[1]==before+1);}
    unsigned before=calls[2];sc.offset.x++;draw(1);sc.offset.y++;draw(1);sc.extent.width--;draw(1);sc.extent.height--;draw(1);assert(calls[2]==before+4);
    before=calls[4];low=0.25f;draw(1);high=0.75f;draw(1);assert(calls[4]==before+2);
    before=calls[3];reference++;draw(1);assert(calls[3]==before+1);
    before=calls[5];set=HANDLE(VkDescriptorSet,5);draw(1);layout=HANDLE(VkPipelineLayout,6);draw(1);assert(calls[5]==before+2);
    for(unsigned why=0;why<6;why++) {
        unsigned old[6];memcpy(old,calls,sizeof old);
        if(why==0)pipe=HANDLE(VkPipeline,7);
        if(why==1)s_vk.cmd=HANDLE(VkCommandBuffer,8);
        if(why==2)buzz_uniform_generation++;
        if(why==3)s_batch.draws++;
        if(why==5)buzz_uniform_generation_exhausted=1;
        draw(why!=4);
        for(unsigned i=0;i<6;i++)assert(calls[i]==old[i]+1);
    }
    buzz_uniform_generation_exhausted=0;
    // Exact bit identity: signed zero and NaN payloads are not collapsed.
    const uint32_t patterns[]={0,0x80000000u,0x7fc01234u,0x7fc05678u};
    for(unsigned i=0;i<4;i++){before=calls[1];memcpy(&vp.x,&patterns[i],4);draw(1);assert(calls[1]==before+1);}
    puts("PASS Vulkan draw state: repeated commands omitted, every field, pipeline/layout/command/batch/ordinal invalidation, exact float bits");
}
