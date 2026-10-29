#ifndef PPU_NO_INLINE_LOADS
#define PPU_NO_INLINE_LOADS
#endif
#include "../port/recompiled/ppu_recomp.h"
#include "../ps3recomp/runtime/memory/vm.h"
#include "../port/frontend_policy.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>

extern "C" {
uint8_t* vm_base=nullptr;
uint32_t ppu_vm_size=0,ppu_hle_inject_base=0;
vm_stack_alloc g_vm_stack_alloc{};
PPU_THREAD_LOCAL ppu_context* g_active_ctx=nullptr;
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*)=nullptr;
uint8_t vm_read8(uint64_t a){assert(a<0x1000000);return vm_base[a];}
uint32_t vm_read32(uint64_t a){assert(a+4<=0x1000000);return uint32_t(vm_base[a])<<24|uint32_t(vm_base[a+1])<<16|uint32_t(vm_base[a+2])<<8|vm_base[a+3];}
}
static void write32(uint32_t a,uint32_t n){for(int i=3;i>=0;--i){vm_base[a+i]=uint8_t(n);n>>=8;}}
static std::map<uint64_t,void(*)(ppu_context*)> hooks;
extern "C" void ppu_register_function(uint64_t pc,void(*fn)(ppu_context*)){hooks[pc]=fn;}
static int top=7,load_status=0,call_status=0,pushed=-1,calls=0;
void func_0046B8F8(ppu_context* c){pushed=int(c->gpr[4]);}
void func_0046B388(ppu_context* c){c->gpr[3]=top;}
void func_0046B3A0(ppu_context* c){top=int(c->gpr[4]);}
void func_0047CD10(ppu_context* c){
    assert(g_active_ctx==c);assert(strstr(reinterpret_cast<char*>(vm_base+c->gpr[4]),"ShouldAllowAccessToDLC"));
    assert(!strcmp(reinterpret_cast<char*>(vm_base+c->gpr[6]),"@buzz-port/hide-psn-store"));
    ++top;c->gpr[3]=load_status;
}
void func_0046C3F0(ppu_context* c){assert(top==8&&c->gpr[4]==0&&c->gpr[5]==0&&c->gpr[6]==0);++calls;if(!call_status)--top;c->gpr[3]=call_status;}
int main(){
    vm_base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x1000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(vm_base);
    g_vm_stack_alloc.next_addr=0xa00000;g_vm_stack_alloc.region_end=0xc00000;
    write32(0x833098,0x6e5980);write32(0x83f008,0x12b760);
    _putenv_s("BUZZ_OFFLINE","0");_putenv_s("BUZZ_HIDE_PSN_STORE","0");
    buzz_register_frontend_policy();assert(hooks.empty()&&!buzz_frontend_needs_include());
    _putenv_s("BUZZ_OFFLINE","1");_putenv_s("BUZZ_HIDE_PSN_STORE","1");
    buzz_register_frontend_policy();assert(hooks.count(0x12b760)&&buzz_frontend_needs_include());
    ppu_context c{};c.gpr[3]=0x10000;hooks.at(0x12b760)(&c);assert(c.gpr[3]==1&&pushed==0);
    write32(0x10000+12,0x20000);write32(0x10000+8,0x20008);write32(0x20000,0x30000);write32(0x20004,4);
    memcpy(vm_base+0x30010,"NaviSliderMenu/Menu_Callbacks",sizeof("NaviSliderMenu/Menu_Callbacks"));
    assert(buzz_frontend_target_include(0x10000));
    memcpy(vm_base+0x30010,"InitNetworkConnection",sizeof("InitNetworkConnection"));assert(!buzz_frontend_target_include(0x10000));
    for(int mode=0;mode<3;++mode){
        c={};for(unsigned r=0;r<32;++r)c.gpr[r]=r+1;c.lr=0x1234;const auto before=c;
        load_status=mode==1?3:0;call_status=mode==2?2:0;top=7;calls=0;
        ppu_context previous{};g_active_ctx=&previous;
        buzz_frontend_after_include(&c,0x10000);
        assert(top==7&&!memcmp(&before,&c,sizeof c)&&g_active_ctx==&previous&&calls==(mode==1?0:1));
    }
    hooks.clear();write32(0x83f008,0);buzz_register_frontend_policy();assert(hooks.empty()&&!buzz_frontend_needs_include());
    VirtualFree(vm_base,0,MEM_RELEASE);vm_base=nullptr;
    puts("PASS: frontend options/version guard, offline boolean ABI, store predicate, context/stack/error preservation");
}
