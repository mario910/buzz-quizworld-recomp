#pragma once
/* Only retained, retired draw sets participate. This stores references, not
 * buffer/image contents; updating pixels or mapped constants needs no rewrite. */
#ifndef BUZZ_VK_DESCRIPTOR_WRITES_DEFAULT
#define BUZZ_VK_DESCRIPTOR_WRITES_DEFAULT 0
#endif
typedef struct {
    VkDescriptorBufferInfo buffers[2];
    VkImageView views[16];
    VkImageLayout layouts[16];
    VkSampler samplers[16];
    VkDescriptorSet set;
    uint64_t epochs[3]; /* buffer, image view, sampler */
    int split;
} buzz_descriptor_contents;
static buzz_descriptor_contents* buzz_descriptor_pending;
static uint64_t buzz_descriptor_resource_epochs[3]={1,1,1};
static int buzz_descriptor_epoch_exhausted;
static int buzz_descriptor_writes_enabled(void) {
    static int enabled=-1;
    if(enabled<0) {
        const char* value=getenv("BUZZ_VK_DESCRIPTOR_WRITES");
        enabled=value && *value ? !strcmp(value,"1") : BUZZ_VK_DESCRIPTOR_WRITES_DEFAULT;
        fprintf(stderr,"[RSX vulkan] cached descriptor writes=%d\n",enabled);
    }
    return enabled;
}
static void buzz_descriptor_invalidate_resources(unsigned kind) {
    /* Destruction can also come from presentation cleanup. Atomic epochs avoid
     * racing that thread; all actual set allocation/update stays on RSX. */
    if(__atomic_fetch_add(&buzz_descriptor_resource_epochs[kind],1,__ATOMIC_RELAXED)==UINT64_MAX)
        __atomic_store_n(&buzz_descriptor_epoch_exhausted,1,__ATOMIC_RELAXED);
}
static void buzz_descriptor_destroy_view(VkDevice device,VkImageView view,const VkAllocationCallbacks* allocator) {
    if(view)buzz_descriptor_invalidate_resources(1);
    pvkDestroyImageView(device,view,allocator);
}
static void buzz_descriptor_destroy_sampler(VkDevice device,VkSampler sampler,const VkAllocationCallbacks* allocator) {
    if(sampler)buzz_descriptor_invalidate_resources(2);
    pvkDestroySampler(device,sampler,allocator);
}
static void buzz_descriptor_destroy_buffer(VkDevice device,VkBuffer buffer,const VkAllocationCallbacks* allocator) {
    if(buffer)buzz_descriptor_invalidate_resources(0);
    pvkDestroyBuffer(device,buffer,allocator);
}
static void buzz_descriptor_write_profile(uint32_t writes) {
    static int enabled=-1;
    static uint64_t calls,written;
    if(enabled<0){const char* value=getenv("BUZZ_VK_DESCRIPTOR_WRITE_PROFILE");enabled=value && !strcmp(value,"1");}
    if(!enabled)return;
    calls++;written+=writes;
    if(calls%8192==0)
        fprintf(stderr,"[RSX vulkan] descriptor-writes calls=%llu requested=%llu written=%llu buffers_epoch=%llu images_epoch=%llu samplers_epoch=%llu\n",
            (unsigned long long)calls,(unsigned long long)(calls*34),(unsigned long long)written,
            (unsigned long long)__atomic_load_n(&buzz_descriptor_resource_epochs[0],__ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&buzz_descriptor_resource_epochs[1],__ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&buzz_descriptor_resource_epochs[2],__ATOMIC_RELAXED));
}
/* This is the pinned vk_eng_write_descriptors payload: two UBO writes, one
 * array of 16 samplers, then either an image array or 16 split image writes.
 * Keep the original descriptors and order for every write that is needed. */
static void buzz_descriptor_write(VkDevice device,VkDescriptorSet set,int split,
    const VkDescriptorBufferInfo* buffers,const VkDescriptorImageInfo* samplers,
    const VkDescriptorImageInfo* images,uint32_t count,const VkWriteDescriptorSet* writes) {
    buzz_descriptor_contents* cache=buzz_descriptor_pending;
    buzz_descriptor_pending=NULL; /* One update per freshly selected draw slot. */
    if(!cache || cache->set!=set || count!=(split?19u:4u) ||
       __atomic_load_n(&buzz_descriptor_epoch_exhausted,__ATOMIC_RELAXED)) {
        if(cache)memset(cache->epochs,0,sizeof cache->epochs);
        pvkUpdateDescriptorSets(device,count,writes,0,NULL);
        buzz_descriptor_write_profile(34);return;
    }
    uint64_t epochs[3];int fresh[3];
    for(unsigned i=0;i<3;i++) {
        epochs[i]=__atomic_load_n(&buzz_descriptor_resource_epochs[i],__ATOMIC_RELAXED);
        fresh[i]=!cache->epochs[i] || cache->epochs[i]!=epochs[i] || cache->split!=split;
    }
    VkWriteDescriptorSet changed[34];uint32_t used=0,descriptors=0;
    for(unsigned i=0;i<2;i++) {
        if(fresh[0] || cache->buffers[i].buffer!=buffers[i].buffer ||
           cache->buffers[i].offset!=buffers[i].offset || cache->buffers[i].range!=buffers[i].range) {
            changed[used++]=writes[i];descriptors++;
            cache->buffers[i]=buffers[i];
        }
    }
    unsigned sampler_changed[16],image_changed[16];
    for(unsigned i=0;i<16;i++) {
        sampler_changed[i]=fresh[2] || cache->samplers[i]!=samplers[i].sampler;
        cache->samplers[i]=samplers[i].sampler;
    }
    /* Coalesce adjacent changes, keeping untouched array elements intact.
     * One changed texture must not cause fifteen dummy bindings to be written. */
    for(unsigned i=0;i<16;) {
        if(!sampler_changed[i]){i++;continue;}
        const unsigned first=i++;
        while(i<16 && sampler_changed[i])i++;
        changed[used]=writes[2];changed[used].dstArrayElement=first;
        changed[used].descriptorCount=i-first;changed[used].pImageInfo=samplers+first;
        used++;descriptors+=i-first;
    }
    for(unsigned i=0;i<16;i++) {
        image_changed[i]=fresh[1] || cache->views[i]!=images[i].imageView || cache->layouts[i]!=images[i].imageLayout;
        if(image_changed[i]) {
            if(split){changed[used++]=writes[3+i];descriptors++;}
        }
        cache->views[i]=images[i].imageView;cache->layouts[i]=images[i].imageLayout;
    }
    if(!split)for(unsigned i=0;i<16;) {
        if(!image_changed[i]){i++;continue;}
        const unsigned first=i++;
        while(i<16 && image_changed[i])i++;
        changed[used]=writes[3];changed[used].dstArrayElement=first;
        changed[used].descriptorCount=i-first;changed[used].pImageInfo=images+first;
        used++;descriptors+=i-first;
    }
    memcpy(cache->epochs,epochs,sizeof epochs);cache->split=split;
    if(used)pvkUpdateDescriptorSets(device,used,changed,0,NULL);
    buzz_descriptor_write_profile(descriptors);
}
#define pvkDestroyImageView(...) buzz_descriptor_destroy_view(__VA_ARGS__)
#define pvkDestroySampler(...) buzz_descriptor_destroy_sampler(__VA_ARGS__)
#define pvkDestroyBuffer(...) buzz_descriptor_destroy_buffer(__VA_ARGS__)
