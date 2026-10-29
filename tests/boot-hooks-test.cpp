// Standalone hook contract test; no game assets or lifted function bodies.
#include "../port/recompiled/ppu_recomp.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#undef vm_read8
#undef vm_read32
extern "C" {
uint8_t* vm_base=nullptr;
int vm_inline_ok=0;
uint32_t ppu_vm_size=0,ppu_hle_inject_base=0;
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*)=nullptr;
uint8_t vm_read8(uint64_t){assert(false);return 0;}
uint32_t vm_read32(uint64_t){assert(false);return 0;}
// boot_trace's timestamp is diagnostic only; no real host clock in this fixture.
unsigned long long ps3_qpc_us(void){return 1000000;}
}
static std::map<uint64_t,void(*)(ppu_context*)> hooks;
static int original_calls,pushed;
static void original(ppu_context* c){++original_calls;c->gpr[3]=77;}
extern "C" const func_entry function_table[]={
    {0x119F04,original,"fade"},{0x12E17C,original,"playing"},
    {0x12DFBC,original,"errored"},{0x11F074,original,"open"},{0x1381A8,original,"play"},
    {0x23D8A0,original,"include"},{0x16E96C,original,"menu"}};
extern "C" const uint64_t function_table_count=7;
extern "C" void ppu_register_function(uint64_t pc,void(*fn)(ppu_context*)){hooks[pc]=fn;}
void func_0046B8F8(ppu_context* c){pushed=(int)c->gpr[4];}
void buzz_register_boot_trace();
static bool frontend_enabled=false;
static int frontend_applied=0;
bool buzz_frontend_needs_include(){return frontend_enabled;}
bool buzz_frontend_target_include(uint32_t){return frontend_enabled;}
void buzz_frontend_after_include(ppu_context* c,uint32_t L){assert(c->gpr[3]==77&&L==1);++frontend_applied;}
static void environment(const char* name,const char* value) {
#ifdef _WIN32
    _putenv_s(name,value?value:"");
#else
    if(value)setenv(name,value,1);else unsetenv(name);
#endif
}
int main(){
    environment("BUZZ_BOOT_TRACE",nullptr);environment("BUZZ_NO_FMV",nullptr);
    buzz_register_boot_trace();assert(hooks.empty());
    environment("BUZZ_NO_FMV","1");buzz_register_boot_trace();
    assert(hooks.size()==5 && !hooks.count(0x119F04));
    ppu_context c{};c.gpr[3]=1;
    hooks.at(0x12DFBC)(&c);assert(c.gpr[3]==1 && pushed==1 && original_calls==0);
    c.gpr[3]=1;hooks.at(0x12E17C)(&c);assert(c.gpr[3]==1 && pushed==0 && original_calls==0);
    c.gpr[3]=1;hooks.at(0x11F074)(&c);assert(c.gpr[3]==0 && original_calls==0);
    c.gpr[3]=1;hooks.at(0x1381A8)(&c);assert(c.gpr[3]==0 && original_calls==0);
    c.gpr[3]=1;hooks.at(0x16E96C)(&c);assert(c.gpr[3]==77 && original_calls==1);
    for(uint64_t pc : {0x11F074ull,0x1381A8ull,0x12E17Cull,0x12DFBCull}) {
        const int before=original_calls;c.gpr[3]=1;hooks.at(pc)(&c);
        assert(c.gpr[3]==77 && original_calls==before+1);
    }
    puts("PASS: startup skip is released by first menu; question media open/play/queries pass through");
    original_calls=0;
    environment("BUZZ_NO_FMV",nullptr);environment("BUZZ_BOOT_TRACE","1");buzz_register_boot_trace();
    c.gpr[3]=1;hooks.at(0x119F04)(&c);assert(c.gpr[3]==77 && original_calls==1);
    c.gpr[3]=1;hooks.at(0x12DFBC)(&c);assert(c.gpr[3]==77 && original_calls==2);
    c.gpr[3]=1;hooks.at(0x11F074)(&c);assert(c.gpr[3]==77 && original_calls==3);
    c.gpr[3]=1;hooks.at(0x1381A8)(&c);assert(c.gpr[3]==77 && original_calls==4);
    puts("PASS: no-FMV omits open/play, error/playing results, unrelated and trace-only passthrough");
    environment("BUZZ_NO_FMV",nullptr);environment("BUZZ_BOOT_TRACE",nullptr);
    hooks.clear();frontend_enabled=true;buzz_register_boot_trace();
    assert(hooks.size()==1&&hooks.count(0x23D8A0));
    c.gpr[3]=1;hooks.at(0x23D8A0)(&c);assert(c.gpr[3]==77&&frontend_applied==1);
    puts("PASS: frontend IncludeScript hook works independently of no-FMV/boot tracing and runs after original");
}
