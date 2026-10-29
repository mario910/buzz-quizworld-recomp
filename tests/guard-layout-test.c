#include "../ps3recomp/libs/spurs/cellSpurs.h"
#include "../ps3recomp/runtime/ppu/ppu_memory.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static uint8_t ram[0x40000];
uint8_t* vm_base=ram;
uint32_t ppu_vm_size=sizeof ram;
void ppu_resv_break_store(uint64_t ea){(void)ea;}
void ps3_ww_report_inline(uint32_t a,uint64_t v,int w){(void)a;(void)v;(void)w;}
uint32_t g_ww_lo=0xFFFFFFFFu,g_ww_hi,g_barrier_sync_watch;
int g_resv_store_active;
int spu_coh_is_reserved(uint32_t a){(void)a;return 0;}
void spu_coh_notify_write(uint32_t a){(void)a;}
void spu_lockline_lock(void){}
void spu_lockline_unlock(void){}
extern s32 cellSpursJobGuardInitialize(u64,u64,u32,u32,u32);
int main(void){
    setenv("SPURS_JOB_KERNEL_MIRROR","1",1);
    assert(cellSpursJobGuardInitialize(0x10000,0x20000,3,2,1)==0);
    assert(vm_read32(0x20000)==3 && vm_read32(0x20004)==3);
    assert(vm_read32(0x20008)==0x10000);
    assert(vm_read32(0x20010)==2 && vm_read32(0x20020)==1);
    assert(vm_read32(0x20030)==0);
    puts("PASS: native guard count/reset/owner/request/auto-reset/waiting layout");
    return 0;
}
