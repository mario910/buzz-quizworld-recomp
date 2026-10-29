#include "../ps3recomp/libs/video/rsx_zeta_layout.h"
#include "../ps3recomp/libs/video/rsx_texture_layout.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    unsigned aa[]={0,3,4,5};
    for(unsigned i=0;i<4;++i){unsigned f=0x148|(aa[i]<<12);
        assert(rsx_zeta_supported(f));assert(rsx_zeta_scale_x(f)==(i?2:1));assert(rsx_zeta_scale_y(f)==(i>1?2:1));}
    assert(!rsx_zeta_supported(0x128));assert(!rsx_zeta_supported(0x2148));
    u8 remap[4];rsx_texture_component_remap(0xAA93,0x85,remap);
    for(unsigned z=0;z<0x1000000u;z+=997){
        unsigned s=z&255;uint32_t p=rsx_zeta_rgba8(z,s);unsigned char* b=(unsigned char*)&p;
        assert(b[remap[1]]==(z>>16));assert(b[remap[2]]==((z>>8)&255));
        assert(b[remap[3]]==(z&255));assert(b[remap[0]]==s);
    }
    assert(rsx_zeta_rgba8(0x123456,0x78)==0x12785634u);
    puts("PASS: Z24S8 guest AA footprints, BE byte packing and AA93 component remap");
}
