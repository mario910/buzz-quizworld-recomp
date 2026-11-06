#pragma once
/* A set may be bound by multiple draws only while every descriptor matches.
 * It stays immutable. The freshly allocated unused slot is never published;
 * normal pool/fence retirement still governs all set and resource lifetimes. */
static buzz_descriptor_contents buzz_uniform_set;
static uint64_t buzz_uniform_set_generation;
static int buzz_uniform_sets_enabled(void) {
    static int enabled=-1;
    if(enabled<0){const char* p=getenv("BUZZ_VK_UNIFORM_SETS");enabled=!(p && !strcmp(p,"0"));}
    return buzz_uniform_enabled() && enabled;
}
static VkDescriptorSet buzz_uniform_set_find(int split,const VkDescriptorBufferInfo* buffers,
    const VkDescriptorImageInfo* samplers,const VkDescriptorImageInfo* images) {
    const buzz_descriptor_contents* c=&buzz_uniform_set;
    if(!c->set || c->split!=split || buzz_uniform_set_generation!=buzz_uniform_generation ||
       buzz_uniform_generation_exhausted || __atomic_load_n(&buzz_descriptor_epoch_exhausted,__ATOMIC_RELAXED))return VK_NULL_HANDLE;
    for(unsigned i=0;i<3;i++)if(c->epochs[i]!=__atomic_load_n(&buzz_descriptor_resource_epochs[i],__ATOMIC_RELAXED))return VK_NULL_HANDLE;
    for(unsigned i=0;i<2;i++)if(c->buffers[i].buffer!=buffers[i].buffer ||
        c->buffers[i].offset!=buffers[i].offset || c->buffers[i].range!=buffers[i].range)return VK_NULL_HANDLE;
    for(unsigned i=0;i<16;i++)if(c->samplers[i]!=samplers[i].sampler || c->views[i]!=images[i].imageView ||
        c->layouts[i]!=images[i].imageLayout)return VK_NULL_HANDLE;
    return c->set;
}
static void buzz_uniform_set_remember(VkDescriptorSet set,int split,const VkDescriptorBufferInfo* buffers,
    const VkDescriptorImageInfo* samplers,const VkDescriptorImageInfo* images) {
    buzz_descriptor_contents* c=&buzz_uniform_set;
    c->set=set;c->split=split;memcpy(c->buffers,buffers,sizeof c->buffers);
    for(unsigned i=0;i<16;i++){c->samplers[i]=samplers[i].sampler;c->views[i]=images[i].imageView;c->layouts[i]=images[i].imageLayout;}
    for(unsigned i=0;i<3;i++)c->epochs[i]=__atomic_load_n(&buzz_descriptor_resource_epochs[i],__ATOMIC_RELAXED);
    buzz_uniform_set_generation=buzz_uniform_generation;
}
