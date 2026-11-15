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
    size_t prefix[2];   /* bytes of the block the cached slot really holds */
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
/* The engine's vertex block is 512 transform constants plus two epilogue
 * vectors (8224 bytes), compared and copied up to three times per draw. Only
 * the slots the bound vertex program can read (rsx_dsp_constants_extent, set
 * per draw by the engine; also capped by the highest slot the title ever
 * wrote) and the epilogue take part. Bytes between the prefix and the
 * epilogue are unspecified in the shadow and in ring slots; no shader of a
 * draw that uses them reads there. A cached slot records how much it holds
 * and is reused only by draws that need no more. Returns the prefix length,
 * or `bytes` for any other block (fragment constants, unbatched UBO,
 * RSX_CONST_EXTENT=0). */
extern u32 rsx_dsp_constants_extent(void);
static size_t buzz_uniform_prefix(unsigned stage,size_t bytes) {
    if(stage==0 && s_batch.limit && bytes==VK_VPCONST_BYTES) {
        const size_t used=(size_t)rsx_dsp_constants_extent()*16u;
        if(used<bytes-32u)return used;
    }
    return bytes;
}
static void buzz_uniform_update(unsigned stage,void* destination,const void* data,size_t bytes) {
    const size_t prefix=buzz_uniform_prefix(stage,bytes);
    const u8* tail_source=(const u8*)data+bytes-32u;
    u8* tail_destination=(u8*)destination+bytes-32u;
    if(s_batch.limit && buzz_uniform_enabled()) {
        if(!memcmp(destination,data,prefix) && (prefix==bytes || !memcmp(tail_destination,tail_source,32u))) {
            /* Unchanged where this draw looks, but a slot written for a
             * narrower program lacks the rest of this prefix. */
            if(buzz_uniform_cache.valid[stage] && buzz_uniform_cache.prefix[stage]<prefix)buzz_uniform_cache.valid[stage]=0;
            return;
        }
        buzz_uniform_cache.valid[stage]=0;
    }
    memcpy(destination,data,prefix);
    if(prefix!=bytes)memcpy(tail_destination,tail_source,32u);
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
            const size_t prefix=buzz_uniform_prefix(stage,bytes);
            u8* slot=(u8*)s_batch.ring_constants.ptr+bindings[stage].offset;
            memcpy(slot,sources[stage],prefix);
            if(prefix!=bytes)memcpy(slot+bytes-32u,sources[stage]+bytes-32u,32u);
            buzz_uniform_cache.offset[stage]=bindings[stage].offset;
            buzz_uniform_cache.size[stage]=bytes;
            buzz_uniform_cache.prefix[stage]=prefix;
            buzz_uniform_cache.valid[stage]=1;
        }
    }
}
