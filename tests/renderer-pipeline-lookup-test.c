#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;typedef uint64_t u64;
static struct {struct {u64 key;} pipelines[8192];u32 n_pipelines;} g;
#include "renderer_pipeline_lookup.h"
static u32 oracle(u64 key) {
    for(u32 i=0;i<g.n_pipelines;++i)if(g.pipelines[i].key==key)return i;
    return g.n_pipelines;
}
static u64 random_state=1;
static u64 random64(void) {
    random_state^=random_state<<13;random_state^=random_state>>7;random_state^=random_state<<17;
    return random_state;
}
int main(void) {
    assert(buzz_pipeline_find(0)==0);
    g.pipelines[0].key=0;g.n_pipelines=1;
    assert(buzz_pipeline_find(0)==0);assert(buzz_pipeline_find(0)==0);
    /* Force a bucket collision, then alternate both valid keys and misses. */
    u64 collision=1;
    while(buzz_pipeline_bucket(collision)!=buzz_pipeline_bucket(0))++collision;
    g.pipelines[1].key=collision;g.n_pipelines=2;
    for(int i=0;i<1000;++i){assert(buzz_pipeline_find(0)==0);assert(buzz_pipeline_find(collision)==1);assert(buzz_pipeline_find(UINT64_MAX)==2);}
    /* The SDK inserts unique full keys. Reset/repopulate its live table without
     * resetting the optimization: stale cached indices must be revalidated. */
    for(unsigned epoch=0;epoch<32;++epoch) {
        g.n_pipelines=0;assert(buzz_pipeline_find(collision)==0);
        const u32 count=epoch==31?8192:1+(random64()%2048);
        for(u32 i=0;i<count;++i) {
            g.pipelines[i].key=((u64)epoch<<48)|((u64)i*0x10001u);
            ++g.n_pipelines;
            assert(buzz_pipeline_find(g.pipelines[i].key)==i);
        }
        for(u32 i=0;i<50000;++i) {
            u64 key=(i%5)?g.pipelines[random64()%count].key:random64();
            assert(buzz_pipeline_find(key)==oracle(key));
        }
    }
    puts("PASS pipeline lookup: 1.6M mixed queries, collisions, empty/reset/repopulated/full tables");
}
