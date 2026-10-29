#include "../ps3recomp/libs/video/rsx_frame_boundary.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    unsigned values[]={0,1,0xffff,0x10000,0x10001,0xffffffff};
    for(unsigned i=0;i<6;++i){assert(rsx_stop_at_flip(1,values[i],0x10000));assert(rsx_stop_at_flip(0,values[i],0x10000)==(values[i]<0x10000));}
    for(unsigned mask=0;mask<256;++mask){
        assert(rsx_clear_is_ordered(1,0,mask));assert(rsx_clear_is_ordered(0,1,mask));
        assert(rsx_clear_is_ordered(0,0,mask)==!(mask&0xf0));
        assert(rsx_op_is_display_content(1,1,0,mask)==!!(mask&0xf0));
        assert(!rsx_op_is_display_content(1,1,1,mask));assert(!rsx_op_is_display_content(0,1,0,mask));
        assert(rsx_op_is_display_content(1,0,0,mask));
    }
    puts("PASS: FIFO flip boundaries independent of backlog; ordered display clears and clear-only frames");
}
