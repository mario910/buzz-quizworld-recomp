/* Private, bounded RSX reference stream replay. No game asset dependency. */
#include "renderer_vertex_remap.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
typedef struct {u32 count;rsx_vertex_ref* refs;} Draw;
static Draw draws[10000];
static rsx_vertex_ref a[65536],b[65536];
static volatile u64 checksum;
int main(int argc,char** argv) {
    assert(argc==2 || (argc==3 && !strcmp(argv[2],"--check-only")));
    FILE* file=fopen(argv[1],"rb");assert(file);
    u32 count=0,n=0;u64 refs=0;
    while(fread(&count,4,1,file)==1) {
        assert(n<10000 && count<=65536);
        draws[n].count=count;draws[n].refs=(rsx_vertex_ref*)malloc((size_t)count*sizeof(*a));
        assert(draws[n].refs && fread(draws[n].refs,sizeof(*a),count,file)==count);
        refs+=count;++n;
    }
    assert(feof(file)&&n==10000);fclose(file);
    rsx_vertex_remap reference={0},optimized={0};
    for(u32 i=0;i<n;++i) {
        const Draw d=draws[i];memcpy(a,d.refs,(size_t)d.count*sizeof(*a));memcpy(b,a,(size_t)d.count*sizeof(*b));
        u32 na,nb;
        assert(rsx_vertex_remap_build(&reference,a,d.count,&na));
        assert(buzz_vertex_remap_build(&optimized,b,d.count,&nb));
        assert(na==nb && !memcmp(a,b,(size_t)d.count*sizeof(*a)));
        assert(!memcmp(reference.occurrence_to_unique,optimized.occurrence_to_unique,(size_t)d.count*4));
    }
    printf("PASS corpus draws=%u refs=%llu\n",n,(unsigned long long)refs);
    for(int round=0;argc==2 && round<4;++round) {
        const int enabled=(round==1||round==2);
        const clock_t start=clock();
        for(u32 repeat=0;repeat<20;++repeat)for(u32 i=0;i<n;++i) {
            const Draw d=draws[i];memcpy(a,d.refs,(size_t)d.count*sizeof(*a));u32 unique;
            const int ok=enabled?buzz_vertex_remap_build(&optimized,a,d.count,&unique)
                :rsx_vertex_remap_build(&reference,a,d.count,&unique);
            assert(ok);checksum+=unique;
        }
        printf("bench enabled=%d ms=%.3f\n",enabled,1000.0*(clock()-start)/CLOCKS_PER_SEC);
    }
    rsx_vertex_remap_destroy(&reference);rsx_vertex_remap_destroy(&optimized);
    for(u32 i=0;i<n;++i)free(draws[i].refs);
    return 0;
}
