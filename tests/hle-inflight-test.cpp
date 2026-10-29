#include "../ps3recomp/runtime/ppu/hle_inflight.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <cstdint>
int main()
{
    HleInflightSlots<64> slots;
    assert(reinterpret_cast<uintptr_t>(&slots.slots[0])%64==0);
    assert(reinterpret_cast<char*>(&slots.slots[1])-reinterpret_cast<char*>(&slots.slots[0])==64);
    const char* outer="outer",*inner="inner";
    assert(slots.get(3)==nullptr && slots.get(64)==nullptr);
    slots.set(3,outer);
    const char* previous=slots.get(3);slots.set(3,inner);
    assert(slots.get(3)==inner);slots.set(3,previous);
    assert(slots.get(3)==outer);slots.set(3,nullptr);
    slots.set(64,inner);assert(slots.get(63)==nullptr);
    auto poll=[&](unsigned tid,const char* name){
        for(unsigned n=0;n<1000000;++n){slots.set(tid,name);assert(slots.get(tid)==name);slots.set(tid,nullptr);}
    };
    std::thread a(poll,3,outer),b(poll,4,inner);a.join();b.join();
    assert(slots.get(3)==nullptr && slots.get(4)==nullptr);
    puts("PASS: cache-line isolation, bounds, nested restore, 2M concurrent in-flight updates");
}
