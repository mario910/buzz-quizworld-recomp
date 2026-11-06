#pragma once
/* Only consecutive engine draws inside the same live render pass participate.
 * Pipeline changes conservatively resend every dynamic state and descriptor.
 * No cache survives command/context reuse, a flush or an intervening recorder. */
#ifndef BUZZ_VK_DRAW_STATE_DEFAULT
#define BUZZ_VK_DRAW_STATE_DEFAULT 0
#endif
static struct {
    VkCommandBuffer command;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSet set;
    VkViewport viewport;
    VkRect2D scissor;
    float depth_min,depth_max;
    uint64_t generation;
    unsigned draw,valid,active,stencil;
} buzz_draw_state;
static int buzz_draw_state_enabled(void) {
    static int enabled=-1;
    if(enabled<0){const char* p=getenv("BUZZ_VK_DRAW_STATE");enabled=p && *p ? !strcmp(p,"1") : BUZZ_VK_DRAW_STATE_DEFAULT;}
    return enabled;
}
static int buzz_draw_same_float(float a,float b) {
    uint32_t x,y;memcpy(&x,&a,sizeof x);memcpy(&y,&b,sizeof y);return x==y;
}
static void buzz_draw_state_begin(int reuse_pass,VkPipeline pipeline) {
    buzz_draw_state.active=buzz_draw_state_enabled();
    if(!buzz_draw_state.active){pvkCmdBindPipeline(s_vk.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);return;}
    const int same=reuse_pass && !buzz_uniform_generation_exhausted &&
        buzz_draw_state.command==s_vk.cmd && buzz_draw_state.generation==buzz_uniform_generation &&
        buzz_draw_state.draw+1==s_batch.draws && buzz_draw_state.pipeline==pipeline;
    if(!same){buzz_draw_state.valid=0;pvkCmdBindPipeline(s_vk.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);}
    buzz_draw_state.command=s_vk.cmd;buzz_draw_state.generation=buzz_uniform_generation;
    buzz_draw_state.draw=s_batch.draws;buzz_draw_state.pipeline=pipeline;
}
static void buzz_draw_viewport(const VkViewport* p) {
    const VkViewport* old=&buzz_draw_state.viewport;
    if(!buzz_draw_state.active || !(buzz_draw_state.valid&1) ||
       !buzz_draw_same_float(old->x,p->x) || !buzz_draw_same_float(old->y,p->y) ||
       !buzz_draw_same_float(old->width,p->width) || !buzz_draw_same_float(old->height,p->height) ||
       !buzz_draw_same_float(old->minDepth,p->minDepth) || !buzz_draw_same_float(old->maxDepth,p->maxDepth))
        pvkCmdSetViewport(s_vk.cmd,0,1,p);
    if(buzz_draw_state.active){buzz_draw_state.viewport=*p;buzz_draw_state.valid|=1;}
}
static void buzz_draw_scissor(const VkRect2D* p) {
    const VkRect2D* old=&buzz_draw_state.scissor;
    if(!buzz_draw_state.active || !(buzz_draw_state.valid&2) || old->offset.x!=p->offset.x ||
       old->offset.y!=p->offset.y || old->extent.width!=p->extent.width || old->extent.height!=p->extent.height)
        pvkCmdSetScissor(s_vk.cmd,0,1,p);
    if(buzz_draw_state.active){buzz_draw_state.scissor=*p;buzz_draw_state.valid|=2;}
}
static void buzz_draw_stencil(uint32_t reference) {
    if(!buzz_draw_state.active || !(buzz_draw_state.valid&4) || buzz_draw_state.stencil!=reference)
        pvkCmdSetStencilReference(s_vk.cmd,VK_STENCIL_FACE_FRONT_AND_BACK,reference);
    if(buzz_draw_state.active){buzz_draw_state.stencil=reference;buzz_draw_state.valid|=4;}
}
static void buzz_draw_depth(float low,float high) {
    if(!buzz_draw_state.active || !(buzz_draw_state.valid&8) || !buzz_draw_same_float(buzz_draw_state.depth_min,low) ||
       !buzz_draw_same_float(buzz_draw_state.depth_max,high))pvkCmdSetDepthBounds(s_vk.cmd,low,high);
    if(buzz_draw_state.active){buzz_draw_state.depth_min=low;buzz_draw_state.depth_max=high;buzz_draw_state.valid|=8;}
}
static void buzz_draw_descriptor(VkPipelineLayout layout,VkDescriptorSet set) {
    if(!buzz_draw_state.active || !(buzz_draw_state.valid&16) || buzz_draw_state.layout!=layout || buzz_draw_state.set!=set)
        pvkCmdBindDescriptorSets(s_vk.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);
    if(buzz_draw_state.active){buzz_draw_state.layout=layout;buzz_draw_state.set=set;buzz_draw_state.valid|=16;}
}
