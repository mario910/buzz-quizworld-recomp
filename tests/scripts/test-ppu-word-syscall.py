"""Exercise the actual generated POSIX syscall and post-DMA adapters together."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--timer',type=Path,required=True)
p.add_argument('--notify',type=Path,required=True);p.add_argument('--arm64',action='store_true')
a=p.parse_args();r=Path(__file__).resolve().parents[2]
fixture=r'''
#include "ppu_word_wait.c"
#include <time.h>
#include <stdio.h>
static unsigned ordinary_sleeps;
static int observed_nanosleep(const struct timespec* a, struct timespec* b) {
    ++ordinary_sleeps; return nanosleep(a,b);
}
unsigned long long ps3_qpc_us(void) {return 0;}
void ppu_log_host_chain(const char* label) {(void)label;}
uint8_t* vm_base;
void (*g_spu_put_notify)(uint32_t,uint32_t);
#define nanosleep observed_nanosleep
#include "TIMER_SOURCE"
#undef nanosleep
#include "NOTIFY_SOURCE"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"check %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static DWORD WINAPI producer(void* ignored) {
    (void)ignored;Sleep(1);
    __atomic_store_n((uint32_t*)(vm_base+0x101A0),0,__ATOMIC_RELEASE);
    g_spu_put_notify(0x101A0,4);return 0;
}
int main(int argc,char**argv) {
    const int off=argc>1;
    vm_base=calloc(1,0x20000);CHECK(vm_base);
    ppu_context c={0};c.gpr[3]=2;c.gpr[30]=0x10000;c.lr=0x288BE0;
    sys_timer_register_usleep_wait(0x288BE0,30,0x1A0);
    g_spu_put_notify=spurs_command_dma_written;
    CHECK(sys_timer_usleep(&c)==CELL_OK);CHECK(ordinary_sleeps==(unsigned)off);
    if(!off) {
        /* Missing notifier, wrong call site and invalid alignment must use
         * the original syscall rather than silently waiting without a wake. */
        g_spu_put_notify=NULL;CHECK(sys_timer_usleep(&c)==CELL_OK);CHECK(ordinary_sleeps==1);
        g_spu_put_notify=spurs_command_dma_written;c.lr=0xDEADBEEF;
        CHECK(sys_timer_usleep(&c)==CELL_OK);CHECK(ordinary_sleeps==2);
        c.lr=0x288BE0;c.gpr[30]=0x10001;
        CHECK(sys_timer_usleep(&c)==CELL_OK);CHECK(ordinary_sleeps==3);
        c.gpr[30]=0x10000;*(uint32_t*)(vm_base+0x101A0)=1;
        HANDLE h=CreateThread(NULL,0,producer,NULL,0,NULL);CHECK(h);
        while(__atomic_load_n((uint32_t*)(vm_base+0x101A0),__ATOMIC_ACQUIRE))
            CHECK(sys_timer_usleep(&c)==CELL_OK);
        CHECK(WaitForSingleObject(h,5000)==WAIT_OBJECT_0);CloseHandle(h);
        CHECK(ordinary_sleeps==3 && !buzz_ppu_wait_lines);
    }
    free(vm_base);puts("PASS actual generated syscall admission/OFF/notifier/DMA path");return 0;
}
'''.replace('TIMER_SOURCE',a.timer.resolve().as_posix()).replace('NOTIFY_SOURCE',a.notify.resolve().as_posix())
with tempfile.TemporaryDirectory(prefix='buzz-ppu-syscall-') as temp:
    source=Path(temp)/'test.c';source.write_text(fixture);binary=Path(temp)/'test'
    subprocess.run([('aarch64-linux-gnu-' if a.arm64 else '')+'gcc','-std=gnu17','-O2',
        '-ffunction-sections','-fdata-sections',*[f'-I{r/x}' for x in
        ('port','ps3recomp/include','ps3recomp/runtime/syscalls','ps3recomp/runtime/spu')],str(source),
        str(r/'ps3recomp/runtime/platform/win32_compat.c'),'-Wl,--gc-sections','-pthread','-lm','-o',str(binary)],check=True)
    for mode in ('1','0',None):
        env=dict(os.environ)
        if mode is None:env.pop('PS3_USLEEP_WAIT_SITES',None)
        else:env['PS3_USLEEP_WAIT_SITES']=mode
        subprocess.run([*(['qemu-aarch64','-L','/usr/aarch64-linux-gnu'] if a.arm64 else []),str(binary),
                        *(['off'] if mode=='0' else [])],env=env,check=True,timeout=30)
