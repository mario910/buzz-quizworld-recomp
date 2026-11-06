#pragma once
/* This indexes the existing pipeline table, never guest memory or shader
 * contents. A hit revalidates the full SDK key and live table index. Collisions
 * fall back to the original first-match scan; table resets need no epoch. */
static u32 buzz_pipeline_slots[2048];
static int buzz_pipeline_lookup_enabled=-1,buzz_pipeline_lookup_profile;
static uint64_t buzz_pipeline_calls,buzz_pipeline_hits,buzz_pipeline_steps;
static inline u32 buzz_pipeline_bucket(u64 key) {
    key^=key>>33;key*=UINT64_C(0xff51afd7ed558ccd);key^=key>>33;
    return (u32)key&2047u;
}
static u32 buzz_pipeline_find(u64 key) {
    if(buzz_pipeline_lookup_enabled<0) {
        const char* e=getenv("BUZZ_PIPELINE_LOOKUP");
        buzz_pipeline_lookup_enabled=!(e && !strcmp(e,"0"));
        e=getenv("BUZZ_PIPELINE_LOOKUP_PROFILE");
        buzz_pipeline_lookup_profile=e && !strcmp(e,"1");
        fprintf(stderr,"[rsx engine] pipeline lookup index=%d\n",buzz_pipeline_lookup_enabled);
    }
    const u32 bucket=buzz_pipeline_bucket(key);
    u32 index=buzz_pipeline_slots[bucket]-1u;
    int hit=buzz_pipeline_lookup_enabled && index<g.n_pipelines && g.pipelines[index].key==key;
    u32 steps=0;
    if(!hit) {
        for(index=0;index<g.n_pipelines;++index) {
            ++steps;
            if(g.pipelines[index].key==key)break;
        }
        if(buzz_pipeline_lookup_enabled && index<g.n_pipelines)buzz_pipeline_slots[bucket]=index+1u;
    }
    if(buzz_pipeline_lookup_profile) {
        ++buzz_pipeline_calls;buzz_pipeline_hits+=hit;buzz_pipeline_steps+=steps;
        if(!(buzz_pipeline_calls&65535u))
            fprintf(stderr,"[pipeline lookup] calls=%llu hits=%llu scanned=%llu entries=%u\n",
                (unsigned long long)buzz_pipeline_calls,(unsigned long long)buzz_pipeline_hits,
                (unsigned long long)buzz_pipeline_steps,g.n_pipelines);
    }
    return index;
}
