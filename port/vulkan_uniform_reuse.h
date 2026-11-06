#pragma once
/* Identical constant blocks may share an immutable slice within ONE draw
 * batch. Track changes while updating the existing CPU shadows; never keep
 * a second payload copy or read back write-combined mapped memory.
 * Every flush invalidates the cache before a context or slot can be reused. */
#ifndef BUZZ_VK_UNIFORM_REUSE_DEFAULT
#define BUZZ_VK_UNIFORM_REUSE_DEFAULT 0
#endif
static struct {
    VkDeviceSize offset[2], size[2];
    VkBuffer buffer;
    unsigned context, valid[2];
} buzz_uniform_cache;
static uint64_t buzz_uniform_generation;
static int buzz_uniform_generation_exhausted;

static int buzz_uniform_enabled(void) {
    static int enabled=-1;
    if(enabled<0) {
        const char* option=getenv("BUZZ_VK_UNIFORM_REUSE");
        enabled=option && *option ? !strcmp(option,"1") : BUZZ_VK_UNIFORM_REUSE_DEFAULT;
        fprintf(stderr,"[RSX vulkan] immutable uniform reuse=%d\n",enabled);
    }
    return enabled;
}
static void buzz_uniform_reset(void) {
    buzz_uniform_cache.valid[0]=buzz_uniform_cache.valid[1]=0;
    if(++buzz_uniform_generation==0)buzz_uniform_generation_exhausted=1;
}
static void buzz_uniform_update(unsigned stage,void* destination,const void* data,size_t bytes) {
    if(s_batch.limit && buzz_uniform_enabled()) {
        if(!memcmp(destination,data,bytes))return;
        buzz_uniform_cache.valid[stage]=0;
    }
    memcpy(destination,data,bytes);
}
static void buzz_uniform_bind(int ring,VkDescriptorBufferInfo bindings[2]) {
    if(!ring || !buzz_uniform_enabled())return;
    if(buzz_uniform_cache.buffer!=s_batch.ring_constants.buf ||
       buzz_uniform_cache.context!=s_batch.active_context)buzz_uniform_reset();
    buzz_uniform_cache.buffer=s_batch.ring_constants.buf;
    buzz_uniform_cache.context=s_batch.active_context;
    const u8* sources[2]={s_e2.vs_shadow,s_e2.ps_shadow};
    for(unsigned stage=0;stage<2;++stage) {
        const size_t bytes=(size_t)bindings[stage].range;
        if(buzz_uniform_cache.valid[stage] && buzz_uniform_cache.size[stage]==bytes) {
            bindings[stage].offset=buzz_uniform_cache.offset[stage];
        } else {
            /* Only the new draw's unused slot is written. Older slots remain
             * immutable until the existing submission/fence lifecycle retires
             * them, even if the two stages reuse different earlier draws. */
            memcpy((u8*)s_batch.ring_constants.ptr+bindings[stage].offset,sources[stage],bytes);
            buzz_uniform_cache.offset[stage]=bindings[stage].offset;
            buzz_uniform_cache.size[stage]=bytes;
            buzz_uniform_cache.valid[stage]=1;
        }
    }
}
