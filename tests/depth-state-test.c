#include "../ps3recomp/libs/video/rsx_depth_state.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    unsigned seen[32]={0};
    for(unsigned test=0;test<2;++test)for(unsigned write=0;write<2;++write)
        for(unsigned fn=0;fn<8;++fn){
            rsx_state st={0};st.depth_test_enable=test;st.depth_mask=write;st.depth_func=0x200+fn;
            unsigned key=rsx_depth_key(&st);assert(key<32 && !seen[key]++);
            assert((key&1)==test && ((key>>1)&1)==write);
            assert(rsx_depth_comparison(key)==fn+1);
        }
    puts("PASS: 32 distinct depth PSO keys and all RSX/D3D compare mappings");
}
