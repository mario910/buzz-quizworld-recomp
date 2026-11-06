#pragma once
/* Context-owned sets survive retirement, but are only rewritten after the
 * SDK's existing fence-before-reset boundary. Two layouts per ordinal draw
 * cover ordinary and cube/volume layouts without sharing live descriptors. */
#ifndef BUZZ_VK_DESCRIPTOR_BATCH_DEFAULT
#define BUZZ_VK_DESCRIPTOR_BATCH_DEFAULT 0
#endif
typedef struct {
    VkDescriptorPool pool;
    VkDescriptorSetLayout layouts[2];
    VkDescriptorSet* sets;
    buzz_descriptor_contents* contents;
    uint32_t capacity,cursor;
    int reset_required;
} buzz_descriptor_pool;
static buzz_descriptor_pool buzz_descriptor_pools[2];
static int buzz_descriptor_batch_enabled(void) {
    static int enabled=-1;
    if(enabled<0) {
        const char* value=getenv("BUZZ_VK_DESCRIPTOR_BATCH");
        enabled=value && *value ? !strcmp(value,"1") : BUZZ_VK_DESCRIPTOR_BATCH_DEFAULT;
        fprintf(stderr,"[RSX vulkan] recycled descriptor allocation=%d\n",enabled);
    }
    return enabled;
}
static buzz_descriptor_pool* buzz_descriptor_find(VkDescriptorPool pool) {
    if(pool)for(unsigned i=0;i<2;++i)if(buzz_descriptor_pools[i].pool==pool)return &buzz_descriptor_pools[i];
    return NULL;
}
static void buzz_descriptor_forget(VkDescriptorPool pool) {
    buzz_descriptor_pool* state=buzz_descriptor_find(pool);
    if(state){buzz_descriptor_pending=NULL;free(state->contents);free(state->sets);memset(state,0,sizeof *state);}
}
static VkResult buzz_descriptor_create(VkDevice device,const VkDescriptorPoolCreateInfo* info,
    const VkAllocationCallbacks* allocator,VkDescriptorPool* out) {
    if(!buzz_descriptor_batch_enabled())return pvkCreateDescriptorPool(device,info,allocator,out);
    /* Only the draw pool creation call is substituted. Both supported layouts
     * contain two UBOs, sixteen images and sixteen samplers. Bound storage to
     * two sets per draw slot, regardless of frame count. */
    if(info->poolSizeCount!=3 || info->maxSets>UINT32_MAX/2)return VK_ERROR_INITIALIZATION_FAILED;
    VkDescriptorPoolSize sizes[3];
    for(unsigned i=0;i<3;++i) {
        sizes[i]=info->pPoolSizes[i];
        if(sizes[i].descriptorCount>UINT32_MAX/2)return VK_ERROR_INITIALIZATION_FAILED;
        sizes[i].descriptorCount*=2;
    }
    VkDescriptorPoolCreateInfo expanded=*info;
    expanded.maxSets*=2;expanded.pPoolSizes=sizes;
    return pvkCreateDescriptorPool(device,&expanded,allocator,out);
}
static VkResult buzz_descriptor_reset(VkDevice device,VkDescriptorPool pool,VkDescriptorPoolResetFlags flags) {
    buzz_descriptor_pool* state=buzz_descriptor_find(pool);
    if(state && state->sets && !state->reset_required) {
        buzz_descriptor_pending=NULL;state->cursor=0;return VK_SUCCESS;
    }
    /* After allocation failure release retained resources at the same safe
     * retirement boundary, preserving the original low-memory retry path. */
    buzz_descriptor_forget(pool);
    return pvkResetDescriptorPool(device,pool,flags);
}
static void buzz_descriptor_destroy(VkDevice device,VkDescriptorPool pool,const VkAllocationCallbacks* allocator) {
    buzz_descriptor_forget(pool);pvkDestroyDescriptorPool(device,pool,allocator);
}
static VkResult buzz_descriptor_allocate(const VkDescriptorSetAllocateInfo* info,VkDescriptorSet* out) {
    buzz_descriptor_pending=NULL;
    if(!buzz_descriptor_batch_enabled())return pvkAllocateDescriptorSets(s_vk.device,info,out);
    unsigned context=0;
    if(info->descriptorPool==s_batch.context_pool[0] && info->descriptorPool)context=0;
    else if(info->descriptorPool==s_batch.context_pool[1] && info->descriptorPool)context=1;
    else return pvkAllocateDescriptorSets(s_vk.device,info,out);
    buzz_descriptor_pool* pool=&buzz_descriptor_pools[context];
    if(pool->pool!=info->descriptorPool) {
        free(pool->contents);free(pool->sets);memset(pool,0,sizeof *pool);
        pool->pool=info->descriptorPool;pool->capacity=s_batch.limit;
        pool->sets=(VkDescriptorSet*)calloc(pool->capacity,2*sizeof(VkDescriptorSet));
        if(pool->sets && buzz_descriptor_writes_enabled())
            pool->contents=(buzz_descriptor_contents*)calloc(pool->capacity,2*sizeof(buzz_descriptor_contents));
    }
    if(!pool->sets)return pvkAllocateDescriptorSets(s_vk.device,info,out);
    if(info->descriptorSetCount!=1 || info->pNext || pool->cursor>=pool->capacity) {
        *out=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_POOL_MEMORY;
    }
    unsigned kind=2;
    for(unsigned i=0;i<2;++i)if(pool->layouts[i]==info->pSetLayouts[0])kind=i;
    if(kind==2)for(unsigned i=0;i<2;++i)if(!pool->layouts[i]){kind=i;pool->layouts[i]=info->pSetLayouts[0];break;}
    if(kind==2){pool->reset_required=1;*out=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_POOL_MEMORY;}
    VkDescriptorSet* slot=&pool->sets[2*pool->cursor+kind];
    if(!*slot) {
        VkResult result=pvkAllocateDescriptorSets(s_vk.device,info,slot);
        if(result!=VK_SUCCESS){*slot=VK_NULL_HANDLE;*out=VK_NULL_HANDLE;pool->reset_required=1;return result;}
    }
    if(pool->contents){buzz_descriptor_pending=&pool->contents[2*pool->cursor+kind];buzz_descriptor_pending->set=*slot;}
    *out=*slot;pool->cursor++;return VK_SUCCESS;
}
#define pvkResetDescriptorPool(...) buzz_descriptor_reset(__VA_ARGS__)
#define pvkDestroyDescriptorPool(...) buzz_descriptor_destroy(__VA_ARGS__)
