"""Exercise the actual generated automatic-poll policy, including env priority."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True)
p.add_argument('--automatic-us',type=int,required=True);p.add_argument('--arm64',action='store_true')
a=p.parse_args();r=Path(__file__).resolve().parents[2]
fixture=r'''
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
static long test_cpus;
static long test_sysconf(int ignored) {(void)ignored;return test_cpus;}
#define sysconf test_sysconf
#include "SOURCE"
#undef sysconf
int main(int argc,char**argv) {
    if(argc!=3)return 2;
    test_cpus=atol(argv[1]);LARGE_INTEGER f;QueryPerformanceFrequency(&f);
    const uint64_t expected=(uint64_t)atol(argv[2])*f.QuadPart/1000000;
    if(spin_ticks()!=expected){fputs("Wrong automatic/explicit polling policy\n",stderr);return 1;}
    return 0;
}
'''.replace('SOURCE',a.source.resolve().as_posix())
with tempfile.TemporaryDirectory(prefix='buzz-pool-spin-') as temp:
    src=Path(temp)/'policy.c';src.write_text(fixture);exe=Path(temp)/'policy'
    subprocess.run([('aarch64-linux-gnu-' if a.arm64 else '')+'gcc','-O2','-std=gnu17',
        '-ffunction-sections','-fdata-sections','-I'+str(r/'ps3recomp/include'),
        '-I'+str(r/'ps3recomp/runtime/spu'),str(src),str(r/'ps3recomp/runtime/platform/win32_compat.c'),
        '-Wl,--gc-sections','-pthread','-lm','-o',str(exe)],check=True)
    for cpus in (1,8,11,12,24):
        for setting in (None,'','0','20','1000'):
            env=dict(os.environ);env.pop('SPURS_POOL_SPIN_US',None)
            if setting is not None:env['SPURS_POOL_SPIN_US']=setting
            expected=int(setting) if setting else (a.automatic_us if cpus>=12 else 0)
            subprocess.run([*(['qemu-aarch64','-L','/usr/aarch64-linux-gnu'] if a.arm64 else []),
                            str(exe),str(cpus),str(expected)],env=env,check=True,timeout=10)
print('PASS 25 CPU-count, automatic/default and explicit-override policy cases')
