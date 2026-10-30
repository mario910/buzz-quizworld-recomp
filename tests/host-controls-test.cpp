#include "ps3emu/host_controls.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
void check(bool b){if(!b)throw std::runtime_error("Host controls regression");}
int main(){try {
    check(ps3_host_get_volume()==100);
    check(ps3_host_get_frame_limit()==PS3_HOST_LEGACY_FRAME_LIMIT&&ps3_host_get_fullscreen()==-1);
    check(!ps3_host_set_volume(101)&&ps3_host_get_volume()==100);
    std::array<float,8> values={0.125f,-.125f,.5f,-.5f,1,-1,0,-0.0f};
    auto original=values;float gain=1;
    ps3_host_apply_volume(values.data(),4,&gain);check(std::memcmp(values.data(),original.data(),sizeof(values))==0);
    check(ps3_host_set_volume(50));values.fill(1);ps3_host_apply_volume(values.data(),4,&gain);
    check(values[0]==.875f&&values[2]==.75f&&values[4]==.625f&&values[6]==.5f&&gain==.5f);
    check(ps3_host_set_volume(0));values.fill(1);ps3_host_apply_volume(values.data(),4,&gain);check(values[6]==0&&gain==0);
    values.fill(1);ps3_host_apply_volume(values.data(),4,&gain);for(float v:values)check(v==0);
    check(ps3_host_set_volume(100));ps3_host_apply_volume(values.data(),0,&gain);check(gain==0);
    ps3_host_apply_volume(nullptr,4,&gain);ps3_host_apply_volume(values.data(),4,nullptr);
    check(!ps3_host_set_frame_limit(60));check(ps3_host_set_frame_limit(30)&&ps3_host_get_frame_limit()==30);
    check(ps3_host_set_frame_limit(0)&&ps3_host_get_frame_limit()==0);
    ps3_host_set_fullscreen(1);check(ps3_host_get_fullscreen()==1);ps3_host_set_fullscreen(0);check(!ps3_host_get_fullscreen());
    ps3_host_pacer p{};
    constexpr uint64_t step=1000000000ull/30;
    check(ps3_host_frame_deadline(&p,1000,30)==1000);
    check(ps3_host_frame_deadline(&p,2000,30)==1000+step);
    check(ps3_host_frame_deadline(&p,1000+step+100,30)==1000+2*step);
    check(ps3_host_frame_deadline(&p,1000+100*step,30)==1000+100*step); // stall: no burst debt
    check(ps3_host_frame_deadline(&p,8000000000ull,0)==8000000000ull);
    check(ps3_host_frame_deadline(&p,8000000001ull,30)==8000000001ull);
    std::thread writer([]{for(unsigned i=0;i<10000;++i)ps3_host_set_volume(i%101);});
    for(unsigned i=0;i<10000;++i)check(ps3_host_get_volume()<=100);writer.join();
    std::cout<<"PASS host volume ramp, atomic controls and non-catching-up frame deadlines\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
