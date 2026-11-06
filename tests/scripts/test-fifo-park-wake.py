"""Exercise generated FIFO and SPURS notification adapters together."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True)
p.add_argument('--notify',type=Path,required=True);p.add_argument('--arm64',action='store_true')
a=p.parse_args();root=Path(__file__).resolve().parents[2]
extra=r'''
    HANDLE kick=(HANDLE)cellGcm_fifo_kick_event();
    vm_write32(ctrl,b+16);
    cellGcm_rsx_process_fifo();
    check(buzz_park_token!=0,"actual self-JUMP registers a watch");
    spurs_command_dma_written(IO_ADDR+b,4);
    check(WaitForSingleObject(kick,0)==WAIT_TIMEOUT,"rewriting the same park does not wake");
    spurs_command_dma_written(IO_ADDR+b+4,4);
    check(WaitForSingleObject(kick,0)==WAIT_TIMEOUT,"unrelated DMA does not wake");
    spurs_command_dma_written(0xfffffff0u,32);
    check(WaitForSingleObject(kick,0)==WAIT_TIMEOUT,"overflowing DMA range cannot alias the watch");
    put_probe(b,3);put_probe(b+8,4);
    spurs_command_dma_written(IO_ADDR+b,8);
    check(WaitForSingleObject(kick,0)==WAIT_OBJECT_0,"changed parked word wakes without a SPURS waiter");
    spurs_command_dma_written(IO_ADDR+b,8);
    check(WaitForSingleObject(kick,0)==WAIT_TIMEOUT,"one watch produces at most one wake");
    check(seen_n==3,"producer notification does not execute commands");
    cellGcm_rsx_process_fifo();
    check(seen_n==5 && s_fifo_getoff==b+16,"normal consumer executes patched work once in order");
    check(!buzz_park_token,"consumer clears the watch");
    buzz_gcm_watch_park(IO_ADDR+b,0x20000000u|b);
    check(WaitForSingleObject(kick,0)==WAIT_OBJECT_0,"publication before registration is rechecked");
    buzz_park_enabled=0;
    buzz_gcm_watch_park(IO_ADDR+b,0x20000000u|b);
    check(!buzz_park_token && WaitForSingleObject(kick,0)==WAIT_TIMEOUT,"runtime OFF preserves bounded retry");
    buzz_park_enabled=1;
    gw32(IO_ADDR+b,0x20000000u|b);
    buzz_gcm_watch_park(IO_ADDR+b,0x20000000u|b);
    s_gcm_context_ea=0;s_gcm_syscall_mode=0;
    cellGcm_rsx_process_fifo();
    put_probe(b,3);spurs_command_dma_written(IO_ADDR+b,4);
    check(!buzz_park_token && WaitForSingleObject(kick,0)==WAIT_TIMEOUT,"early return invalidates stale watch");
'''
with tempfile.TemporaryDirectory(prefix='buzz-park-wake-') as temp:
    out=Path(temp)
    fixture=(root/'ps3recomp/libs/video/tests/test_gcm_get_on_jump.c').read_text()
    fixture=fixture.replace('#include "../cellGcmSys.c"',f'#include "{a.source.resolve().as_posix()}"\n#include "{a.notify.resolve().as_posix()}"')
    fixture=fixture.replace('    free(vm_base);',extra+'\n    free(vm_base);')
    source=out/'test.c';source.write_text(fixture);binary=out/'test'
    subprocess.run([('aarch64-linux-gnu-' if a.arm64 else '')+'gcc','-std=gnu17','-O2','-UNDEBUG',
        '-ffunction-sections','-fdata-sections',*[f'-I{root/s}' for s in
        ['ps3recomp/include','ps3recomp/libs/video','ps3recomp/runtime/spu','port']],
        str(source),str(root/'ps3recomp/runtime/platform/win32_compat.c'),
        '-Wl,--gc-sections','-pthread','-lm','-o',str(binary)],check=True)
    subprocess.run([*(['qemu-aarch64','-L','/usr/aarch64-linux-gnu'] if a.arm64 else []),str(binary)],check=True,timeout=45)
