// Real title USB adapter with a tiny synthetic guest/driver. No game assets.
#ifndef PPU_NO_INLINE_LOADS
#define PPU_NO_INLINE_LOADS
#endif
#include "../port/recompiled/ppu_recomp.h"
#include "../ps3recomp/runtime/memory/vm.h"
#include "../port/buzzer_input.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <thread>
#include <chrono>
extern "C" {
uint8_t* vm_base=nullptr;
uint32_t ppu_vm_size=0,ppu_hle_inject_base=0;
vm_stack_alloc g_vm_stack_alloc{};
PPU_THREAD_LOCAL ppu_context* g_active_ctx=nullptr;
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*)=nullptr;
uint8_t vm_read8(uint64_t a){assert(a<0x1000000);return vm_base[a];}
uint16_t vm_read16(uint64_t a){return uint16_t(vm_read8(a))<<8|vm_read8(a+1);}
uint32_t vm_read32(uint64_t a){return uint32_t(vm_read16(a))<<16|vm_read16(a+2);}
}
static void write32(uint32_t a,uint32_t n){for(int i=3;i>=0;--i){vm_base[a+i]=uint8_t(n);n>>=8;}}
static std::map<uint32_t,void(*)(ppu_context*)> hooks;
extern "C" void ps3_hle_register_ctx(uint32_t nid,const char*,void(*fn)(ppu_context*)){hooks[nid]=fn;}
extern "C" void rsx_d3d12_set_key_handler(void(*fn)(uint32_t,int)){assert(fn==buzz_input_key);}
void buzz_input_init(){}
unsigned buzz_input_players(){return 8;}
void buzz_input_key(uint32_t,int){}
uint64_t buzz_input_poll(uint32_t){return buzz_input::bits(0,1)|buzz_input::bits(7,16);}
static std::atomic<unsigned> completions{0};
static uint64_t callback_status,callback_size;
static void completed(ppu_context* c){callback_status=c->gpr[3];callback_size=c->gpr[4];completions.fetch_add(1);}
extern "C" const func_entry function_table[]={{0x123456,completed,"synthetic callback"}};
extern "C" const uint64_t function_table_count=1;
void buzz_register_virtual_buzzer();
static uint32_t call(uint32_t nid,uint32_t a=0,uint32_t b=0,uint32_t d=0,uint32_t e=0,uint32_t f=0){
    ppu_context c{};c.gpr[3]=a;c.gpr[4]=b;c.gpr[5]=d;c.gpr[6]=e;c.gpr[7]=f;hooks.at(nid)(&c);return uint32_t(c.gpr[3]);
}
static void await(unsigned previous){
    for(unsigned i=0;i<1000&&completions.load()==previous;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(completions.load()==previous+1&&callback_status==0);
}
int main(){
    _putenv_s("BUZZ_NO_VIRTUAL_BUZZER","");_putenv_s("BUZZ_SCRIPT","");
    vm_base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x1000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(vm_base);
    g_vm_stack_alloc.next_addr=0xa00000;g_vm_stack_alloc.region_end=0xe00000;
    buzz_register_virtual_buzzer();
    const auto d1=call(0x2fb08e1e,1,0,1),d2=call(0x2fb08e1e,2,0,1);
    assert(d1&&d2==d1+0x100&&!memcmp(vm_base+d1,vm_base+d2,52));
    assert(call(0x2fb08e1e,3,0,1)==0);
    call(0x63bfdb97,1,111);call(0x63bfdb97,2,222);
    assert(call(0x5de3af36,1)==111&&call(0x5de3af36,2)==222);
    assert(call(0x63bfdb97,3,999)==0x8011000a);
    assert(call(0xdb819e03,1,0x40000)==0&&vm_base[0x40000]==1);
    assert(call(0xdb819e03,2,0x40010)==0&&vm_base[0x40010]==2);
    const auto pipe1=call(0x254289ac,1,0),pipe2=call(0x254289ac,2,0);
    assert(pipe1&&pipe2&&pipe1!=pipe2);
    write32(0x50000,0x123456);write32(0x50004,0);
    assert(call(0xd0e766fe)==0);
    unsigned count=completions.load();assert(call(0x0f411262,pipe1,0x60000,5,0x50000)==0);await(count);
    assert(callback_size==5&&vm_base[0x60002]==1&&(vm_base[0x60004]&15)==0);
    count=completions.load();assert(call(0x0f411262,pipe2,0x60010,5,0x50000)==0);await(count);
    assert(callback_size==5&&vm_base[0x60012]==0&&(vm_base[0x60014]&15)==8);
    const uint8_t request[]={0x80,6,3,3,0,0,0,64};memcpy(vm_base+0x70000,request,8);
    count=completions.load();assert(call(0x97cf128e,pipe2,0x70000,0x71000,0x50000)==0);await(count);
    assert(callback_size==18&&vm_base[0x71000]==18&&vm_base[0x71010]=='2');
    assert(call(0x9763e962,pipe2)==0);
    assert(call(0x0f411262,pipe2,0x60010,5,0x50000)==0x8011000d);
    count=completions.load();assert(call(0x0f411262,pipe1,0x60000,5,0x50000)==0);await(count);
    call(0x35f22ac3);
    puts("PASS: two USB devices, descriptors/location/private state/pipe isolation, P1/P8 reports, serial and close");fflush(stdout);
    // Production owns a detached worker for process lifetime. Don't run static
    // destructors underneath it in this synthetic standalone process.
    ExitProcess(0);
}
