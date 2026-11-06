"""Exercise a CMake-generated ready adapter with real SDK FIFO fixtures."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--source',required=True,type=Path)
parser.add_argument('--arm64',action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
source=args.source.resolve()
assert 'int buzz_gcm_fifo_ready(void)' in source.read_text()
prefix='aarch64-linux-gnu-' if args.arm64 else ''
runner=['qemu-aarch64','-L','/usr/aarch64-linux-gnu'] if args.arm64 else []
extra=r'''
    check(!buzz_gcm_fifo_ready(), "caught-up FIFO without new PUT sleeps");
    vm_write32(ctrl,b+8);
    check(buzz_gcm_fifo_ready(), "new PUT after caught-up pass is noticed before sleeping");
    AcquireSRWLockExclusive(&s_gcm_fifo_lock);
    check(!buzz_gcm_fifo_ready(), "contended consumer lock keeps bounded wait");
    ReleaseSRWLockExclusive(&s_gcm_fifo_lock);
    cellGcm_rsx_process_fifo();
    check(!buzz_gcm_fifo_ready(), "new work that hits self-JUMP cannot trigger polling");
    vm_write32(ctrl,b+16);
    check(!buzz_gcm_fifo_ready(), "changing PUT alone does not unpark the FIFO");
    put_probe(b,3);put_probe(b+8,4);
    cellGcm_rsx_process_fifo();
    check(seen_n==5 && !buzz_gcm_fifo_ready(), "normal timeout retry drains patched work");
    put_probe(b+16,5);vm_write32(ctrl,b+24);
    check(buzz_gcm_fifo_ready(), "later publication is eligible after recovery");
    cellGcm_rsx_process_fifo();
    check(seen_n==6 && !buzz_gcm_fifo_ready(), "new work executes once and stops at PUT");
    setenv("GCM_SEMA_ACQUIRE","1",1);
    gw32(IO_ADDR+b+24,(1u<<18)|0x64u);gw32(IO_ADDR+b+28,0x120);
    gw32(IO_ADDR+b+32,(1u<<18)|0x68u);gw32(IO_ADDR+b+36,0x2468);
    put_probe(b+40,6);vm_write32(ctrl,b+48);
    cellGcm_rsx_process_fifo();
    check(seen_n==6 && s_fifo_getoff==b+32 && !buzz_gcm_fifo_ready(), "semaphore acquire preserves blocking and wait");
    put_probe(b+48,7);vm_write32(ctrl,b+56);
    check(!buzz_gcm_fifo_ready(), "publishing behind an acquire cannot bypass it");
    vm_write32(GCM_LABEL_GUEST_BASE+0x120,0x2468);
    cellGcm_rsx_process_fifo();
    check(seen_n==8 && !buzz_gcm_fifo_ready(), "normal retry observes label release and executes in order");
    s_gcm_context_ea=0;s_gcm_syscall_mode=0;
    cellGcm_rsx_process_fifo();vm_write32(ctrl,b+64);
    check(!buzz_gcm_fifo_ready(), "early-return invalid context clears stale readiness");
'''
with tempfile.TemporaryDirectory(prefix='buzz-fifo-ready-') as temp:
    out=Path(temp)
    for name in ['test_gcm_get_on_jump.c','test_gcm_flip_stream.c','test_gcm_get_on_release.c']:
        text=(root/'ps3recomp/libs/video/tests'/name).read_text().replace('#include "../cellGcmSys.c"',f'#include "{source.as_posix()}"')
        if name=='test_gcm_get_on_jump.c':text=text.replace('    free(vm_base);',extra+'\n    free(vm_base);')
        if name=='test_gcm_flip_stream.c':
            anchor='    check((p & 1) && !(p & 2), "a written, unflushed flip does not complete");'
            assert anchor in text
            text=text.replace(anchor,anchor+'\n    check(!buzz_gcm_fifo_ready(), "an unflushed flip does not make the consumer ready");')
            anchor='    vm_write32(GCM_CONTROL_GUEST_ADDR + 0, gcm_ea2io(cur2) + 20);'
            assert anchor in text
            text=text.replace(anchor,anchor+'\n    check(buzz_gcm_fifo_ready(), "fresh PUT after a flip is noticed without completing it early");')
        fixture=out/name;fixture.write_text(text)
        binary=out/name.removesuffix('.c')
        subprocess.run([prefix+'gcc','-std=gnu17','-O2','-UNDEBUG','-ffunction-sections','-fdata-sections',
            '-I'+str(root/'ps3recomp/include'),'-I'+str(root/'ps3recomp/libs/video'),'-I'+str(root/'port'),
            str(fixture),'-Wl,--gc-sections','-pthread','-lm','-o',str(binary)],check=True)
        subprocess.run([*runner,str(binary)],check=True,timeout=45)
print('PASS actual FIFO readiness, parked-jump recovery, contention, flips and release ordering')
