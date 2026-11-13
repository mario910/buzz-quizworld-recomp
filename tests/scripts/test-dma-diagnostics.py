"""Compare real pinned/generated DMA transfers and list completion paths."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--header', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
sdk = root / 'ps3recomp/runtime/spu'
fixture = (sdk / 'tests/test_mfc_resume_elements.c').read_text()
checks = r'''
    spu_context* probe = calloc(1,sizeof(*probe));
    check(probe != NULL,"allocate transfer probe");
    spu_context_init(probe,0);
    unsigned cases=0;
    for(unsigned size=16;size<=16384;size*=2) {
        for(unsigned offset=0;offset<16;offset++) {
            uint32_t lsa=0x1000+offset, ea=0x10000+offset;
            memset(probe->ls+lsa,0xC7,size);
            const uint8_t poison[4]={0xC7,0x08,0xC7,0x08};
            memcpy(probe->ls+lsa+size-4,poison,4);
            memset(memory+ea,0,size);
            check(mfc_do_transfer(probe,lsa,ea,size,MFC_PUT_CMD)==0,"PUT result");
            check(!memcmp(memory+ea,probe->ls+lsa,size),"complete poison PUT bytes");
            memset(probe->ls+lsa,0,size);
            check(mfc_do_transfer(probe,lsa,ea,size,MFC_GET_CMD)==0,"GET result");
            check(!memcmp(memory+ea,probe->ls+lsa,size),"complete poison GET bytes");
            cases++;
        }
    }
    free(probe);
    printf("PASS direct GET/PUT cases=%u\n",cases);
'''
fixture = fixture.replace('    if(strcmp(argv[1],"concurrent")==0){', checks+'\n    if(strcmp(argv[1],"concurrent")==0){')
with tempfile.TemporaryDirectory(prefix='buzz-dma-diagnostics-') as temp:
    directory = Path(temp)
    bins = {}
    for name,header in [('sdk',sdk/'spu_dma.h'),('candidate',a.header.resolve())]:
        source=directory/(name+'.c'); exe=directory/name
        source.write_text(fixture.replace('#include "../spu_dma.h"',f'#include "{header}"'))
        subprocess.run(['gcc','-O2','-std=gnu17','-UNDEBUG','-ffunction-sections','-fdata-sections',
            '-I'+str(sdk),'-I'+str(root/'ps3recomp/include'),str(source),
            str(root/'ps3recomp/runtime/platform/win32_compat.c'),'-pthread','-lm','-Wl,--gc-sections','-o',str(exe)],check=True)
        bins[name]=exe
    for mode in ['empty','transfer','repark','concurrent']:
        outputs=[]
        for name,enabled in [('sdk','0'),('candidate','0'),('candidate','1')]:
            env=os.environ.copy();env['BUZZ_DMA_POISON_SCAN']=enabled
            result=subprocess.run([str(bins[name]),mode],env=env,capture_output=True,text=True,check=True)
            assert ('[POISON-DMA]' in result.stderr)==(name=='sdk' or enabled=='1')
            outputs.append(result.stdout)
        assert outputs[0]==outputs[1]==outputs[2]
        print('PASS',mode,'SDK, diagnostic OFF and ON: identical transfer checks; expected logging',flush=True)
