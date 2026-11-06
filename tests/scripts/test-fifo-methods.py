"""Compare the actual generated FIFO with the pinned SDK, including callbacks."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',required=True,type=Path)
p.add_argument('--arm64',action='store_true');p.add_argument('--sanitize',action='store_true')
a=p.parse_args();root=Path(__file__).resolve().parents[2]
sdk=root/'ps3recomp/libs/video/cellGcmSys.c'
assert 'buzz_fifo_packet_eligible' in a.source.read_text()
extra=r'''
static uint64_t digest=1469598103934665603ull, calls;
static u32 mutation_ea;
static void record(u32 kind,u32 method,u32 data) {
    const u32 words[]={kind,method,data,vm_read32(GCM_CONTROL_GUEST_ADDR+4)};
    for(unsigned k=0;k<4;++k){digest^=words[k];digest*=1099511628211ull;}
    ++calls;
}
'''
cases=r'''
    /* Exhaust every aligned canonical method in both packet modes. Crossing
     * packets, GET_REPORT, low methods and other subchannels take the SDK path. */
    unsigned cases=0;
    const u32 counts[]={1,2,3,7,32,33,127,2047};
    for(u32 type=0;type<=2;type+=2)for(u32 m=0x200;m<0x2000;m+=4) {
        const u32 count=counts[(m>>2)%8],start=0x1000,end=start+4+count*4;
        gw32(IO_ADDR+start,(type<<29)|(count<<18)|m);
        for(u32 i=0;i<count;++i)gw32(IO_ADDR+start+4+i*4,(i*17+m)&0xffff);
        s_fifo_getoff=start;vm_write32(ctrl+4,start);vm_write32(ctrl,end);
        cellGcm_rsx_process_fifo();record(9,s_fifo_getoff,vm_read32(ctrl+4));++cases;
    }
    for(u32 sub=0;sub<8;++sub)for(u32 type=0;type<=2;type+=2) {
        const u32 start=0x5000,count=3;
        gw32(IO_ADDR+start,(type<<29)|(count<<18)|(sub<<13)|0x1efc);
        for(u32 i=0;i<count;++i)gw32(IO_ADDR+start+4+i*4,3+i);
        s_fifo_getoff=start;vm_write32(ctrl+4,start);vm_write32(ctrl,start+16);
        cellGcm_rsx_process_fifo();record(9,s_fifo_getoff,vm_read32(ctrl+4));++cases;
    }
    /* Callback changes the current argument. The second consumer must read
     * the changed word, not a snapshot taken before the first callback. */
    mutation_ea=IO_ADDR+0x6004;
    gw32(IO_ADDR+0x6000,(1u<<18)|0x210);gw32(mutation_ea,0x12345678);
    s_fifo_getoff=0x6000;vm_write32(ctrl+4,0x6000);vm_write32(ctrl,0x6008);
    cellGcm_rsx_process_fifo();record(10,0,vm_read32(mutation_ea));mutation_ea=0;++cases;
    /* A mapped header with an unmapped packet suffix must consume exactly
     * the mapped prefix, then advance GET just as the SDK does. */
    const u32 start=IO_SIZE-12;
    gw32(IO_ADDR+start,(32u<<18)|0x1f00);
    gw32(IO_ADDR+start+4,0xabc);gw32(IO_ADDR+start+8,0xdef);
    s_fifo_getoff=start;vm_write32(ctrl+4,start);vm_write32(ctrl,start+132);
    cellGcm_rsx_process_fifo();record(11,s_fifo_getoff,vm_read32(ctrl+4));++cases;
    printf("RESULT cases=%u calls=%llu hash=%016llx\n",cases,
        (unsigned long long)calls,(unsigned long long)digest);
'''
fixture=(root/'ps3recomp/libs/video/tests/test_gcm_get_on_jump.c').read_text()
fixture=fixture.replace('int  rsx_process_method(',extra+'\nint  rsx_process_method(')
fixture=fixture.replace('    (void)state;','    (void)state;record(2,method,data);')
fixture=fixture.replace('int  rsx_draw_engine_enabled(void) { return 0; }','int  rsx_draw_engine_enabled(void) { return !getenv("TEST_ENGINE_OFF"); }')
fixture=fixture.replace('void rsx_draw_engine_method(u32 method, u32 arg) { (void)method; (void)arg; }',
    'void rsx_draw_engine_method(u32 method,u32 arg) {record(1,method,arg);if(mutation_ea)vm_write32(mutation_ea,arg^0xa5a5a5a5u);}')
fixture=fixture.replace('int  rsx_live_draw_enabled(void) { return 0; }','int  rsx_live_draw_enabled(void) { return getenv("TEST_LIVE")!=NULL; }')
fixture=fixture.replace('void rsx_live_draw_method(u32 method, u32 arg) { (void)method; (void)arg; }','void rsx_live_draw_method(u32 method,u32 arg) {record(3,method,arg);}')
fixture=fixture.replace('    free(vm_base);',cases+'\n    free(vm_base);')
runner=['qemu-aarch64','-L','/usr/aarch64-linux-gnu'] if a.arm64 else []
compiler='aarch64-linux-gnu-gcc' if a.arm64 else 'gcc'
with tempfile.TemporaryDirectory(prefix='buzz-fifo-methods-') as temp:
    d=Path(temp);bins={}
    for name,source in [('sdk',sdk),('candidate',a.source.resolve())]:
        c=d/f'{name}.c';exe=d/name
        c.write_text(fixture.replace('#include "../cellGcmSys.c"',f'#include "{source.as_posix()}"'))
        flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if a.sanitize else []
        subprocess.run([compiler,'-std=gnu17','-O2','-UNDEBUG','-ffunction-sections','-fdata-sections',*flags,
            *['-I'+str(root/x) for x in ['ps3recomp/include','ps3recomp/libs/video','port']],
            str(c),'-Wl,--gc-sections','-pthread','-lm','-o',str(exe)],check=True)
        bins[name]=exe
    for options in [{},{'BUZZ_RSX_METHOD_FASTPATH':'0'},{'TEST_ENGINE_OFF':'1'},{'TEST_LIVE':'1'},
                    *[{key:'0'} for key in ['GCM_SUBCH7','GCM_OBJDBG','GCM_SUBCH_GATE','GCM_VAL_WATCH','GCM_METHOD_HIST','RSX_LIVE_FEED_DBG']]]:
        outputs=[]
        for name in ['sdk','candidate']:
            env=os.environ.copy();env.update(options)
            result=subprocess.run([*runner,str(bins[name])],env=env,text=True,capture_output=True,timeout=90)
            if result.returncode:raise RuntimeError(result.stdout+result.stderr)
            outputs.append([line for line in result.stdout.splitlines() if line.startswith('RESULT ')])
            assert outputs[-1],result.stdout
            if name=='candidate':
                assert f'compact FIFO methods={0 if options else 1}' in result.stderr,result.stderr[-1000:]
        assert outputs[0]==outputs[1],(options,outputs)
        print('PASS',options,outputs[0][0],flush=True)
