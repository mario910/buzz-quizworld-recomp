"""Compare all entry LS/GPR bytes through real SDK staging, not a mock loop."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--job-source',type=Path,required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
sdk=root/'ps3recomp';spu=sdk/'runtime/spu'
assert 'buzz_spurs_clear_regions' in a.job_source.read_text()
sources=['spu_channels.c','spu_drain.c','spu_lockstep.c','spu_coherency.c','spu_workload.c',
         'spurs_policy.c','spurs_job_ls.c','spu_tsp_weak.c','spu_vm_pagemap.c',
         'spurs_policy_blob_weak.c','spu_interp.c']
env={k:v for k,v in os.environ.items() if not k.startswith(('SPU_','SPURS_','BUZZ_'))}
env.update(SPURS_JOB_LS_EXCHANGE='1',SPURS_JOB_LS_PUBLISH='mbox',SPURS_JOB_CACHE_ENV='1')
with tempfile.TemporaryDirectory(prefix='buzz-staging-') as temp:
    out=Path(temp); expected={}
    for name,source in [('sdk',spu/'spurs_job.c'),('candidate',a.job_source.resolve())]:
        exe=out/name
        subprocess.run([os.environ.get('CC','cc'),'-std=gnu17','-O2','-UNDEBUG',
                        *['-I'+str(x) for x in [sdk/'include',spu,sdk/'runtime/platform',root/'port']],
                        str(root/'tests/spurs-input-clear-test.c'),str(source),
                        *[str(spu/n) for n in sources],str(sdk/'runtime/platform/win32_compat.c'),
                        str(sdk/'libs/system/host_controls.c'),'-pthread','-lm','-o',str(exe)],check=True)
        for zero in ['0','1','2']:
            for enabled in (['0'] if name=='sdk' else ['0','1']):
                result=subprocess.run([str(exe)],env=dict(env,SPURS_JOB_LS_ZERO=zero,BUZZ_SPURS_INPUT_CLEAR=enabled),
                                      text=True,capture_output=True,check=True,timeout=90)
                if name=='sdk':expected[zero]=result.stdout
                elif result.stdout!=expected[zero]:raise RuntimeError(f'Entry-state mismatch: zero={zero}, enabled={enabled}')
                print(f'{name} zero={zero} enabled={enabled}: {result.stdout.strip()}',flush=True)
    print('PASS original SDK vs adapter OFF/ON: 4096 jobs in each of 9 runs; full entry LS/GPR hashes match.')
