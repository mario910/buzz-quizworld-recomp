#include "../ps3recomp/libs/video/rsx_pipeline_index.h"
#include <assert.h>
#include <stdio.h>
typedef struct {uint32_t code, vertex, blend, depth, other;} Key;
static unsigned bucket(Key k) {return rsx_pipeline_bucket(k.code,k.vertex,k.blend,k.depth);}
static int equal(Key a,Key b) {return !memcmp(&a,&b,sizeof a);}
int main(void) {
    rsx_pipeline_index index = {0};
    Key entries[256]; unsigned count=0,evictions=0,comparisons=0;
    uint32_t seed=7;
    for(unsigned q=0;q<100000;++q) {
        seed=seed*1664525u+1013904223u;
        unsigned id=(seed>>16)%600;
        Key key={id%17,id%11,id%3,id%5,id}; // deliberately colliding partial keys
        int linear=-1,fast=-1;
        for(unsigned i=0;i<count;++i) if(equal(entries[i],key)){linear=(int)i;break;}
        for(unsigned link=index.heads[bucket(key)];link;link=index.next[link-1]) {
            assert(link<=count); ++comparisons;
            if(equal(entries[link-1],key)){fast=(int)link-1;break;}
        }
        assert(fast==linear);
        if(linear<0) {
            if(count==256) {
                memmove(entries,entries+1,255*sizeof *entries);count=255;++evictions;
                rsx_pipeline_index_clear(&index);
                for(unsigned i=0;i<count;++i)rsx_pipeline_index_add(&index,bucket(entries[i]),i);
            }
            entries[count]=key;rsx_pipeline_index_add(&index,bucket(key),count++);
        }
    }
    assert(evictions>1000 && comparisons<100000*10);
    rsx_pipeline_index_clear(&index);
    /* Exercise all expanded slots, including slot+1 above 8 bits. */
    for(unsigned i=0;i<RSX_PSO_INDEX_SLOTS;++i)
        rsx_pipeline_index_add(&index,0,i);
    unsigned link=index.heads[0], seen=0;
    while(link) {
        assert(link==RSX_PSO_INDEX_SLOTS-seen);
        ++seen; link=index.next[link-1];
    }
    assert(seen==RSX_PSO_INDEX_SLOTS);
    rsx_pipeline_index_add(&index,0,RSX_PSO_INDEX_SLOTS);
    assert(index.heads[0]==RSX_PSO_INDEX_SLOTS);
    puts("PASS: 100000 indexed/linear comparisons, partial-key collisions and FIFO evictions");
    return 0;
}
