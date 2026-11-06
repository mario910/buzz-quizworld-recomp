#include "renderer_packed_remap.h"
static buzz_packed_remap packed;
#define buzz_vertex_remap_build(r,v,n,u) buzz_packed_remap_build(&packed,r,v,n,u)
#define main remap_test_main
#include "renderer-vertex-remap-test.c"
#undef main
int main(int argc,char** argv) {
    const int result=remap_test_main(argc,argv);
    /* Alternate SDK/packed calls on the same allocation, cross the bounded
     * range and wrap the private generation; compare every output byte. */
    for(u32 step=0;step<12;++step) {
        for(u32 i=0;i<2049;++i) {
            input[i].vertex_id=step%3==0?i*4096u:i%173u;
            input[i].base_index=0xfffffffdu;
        }
        if(step==5)packed.generation=UINT_MAX;
        compare(2049);
        memcpy(actual,input,sizeof input);u32 unique;
        assert(rsx_vertex_remap_build(&b,actual,2049,&unique));
        compare(2049);
    }
    rsx_vertex_remap_destroy(&a);rsx_vertex_remap_destroy(&b);
    puts("PASS packed remap: fallback interleaving and generation wrap");
    return result;
}
