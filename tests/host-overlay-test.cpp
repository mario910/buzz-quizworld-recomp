#include "ps3emu/host_overlay.h"
#include <iostream>
#include <stdexcept>

namespace {
unsigned messages=0,draws=0,shutdowns=0;
void check(bool ok){if(!ok)throw std::runtime_error("Overlay hook regression");}
int message(void* window,unsigned m,uintptr_t w,intptr_t l){
    check(window==nullptr&&m==42&&w==123&&l==-7);++messages;return 1;
}
void draw(const ps3_host_overlay_frame* frame){check(frame&&frame->width==1280&&frame->height==720);++draws;}
void shutdown(){++shutdowns;}
}
int main(){try {
    check(!ps3_host_overlay_enabled());
    check(ps3_host_overlay_message(nullptr,0,0,0)==0);
    ps3_host_overlay_draw(nullptr);ps3_host_overlay_shutdown();
    const ps3_host_overlay_hooks hooks{message,draw,shutdown};ps3_host_set_overlay(&hooks);
    check(ps3_host_overlay_enabled());check(ps3_host_overlay_message(nullptr,42,123,-7)==1);
    ps3_host_overlay_frame frame{};frame.width=1280;frame.height=720;
    ps3_host_overlay_draw(&frame);ps3_host_overlay_shutdown();
    check(messages==1&&draws==1&&shutdowns==1);
    ps3_host_set_overlay(nullptr);check(!ps3_host_overlay_enabled());
    check(ps3_host_overlay_message(nullptr,0,0,0)==0);
    ps3_host_overlay_draw(nullptr);ps3_host_overlay_shutdown();
    check(messages==1&&draws==1&&shutdowns==1);
    std::cout<<"PASS optional host overlay registration, dispatch and teardown\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
