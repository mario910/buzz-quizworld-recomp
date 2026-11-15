"""Linux x86 image36 search checks with ASan/UBSan and the pinned runtime rings.

Requires the user's prepared EU 1.00 lift. No game-derived fixture is tracked.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workspace', type=Path, default=root)
parser.add_argument('--cc', default=os.environ.get('CC', 'clang'))
args = parser.parse_args()
workspace = args.workspace.resolve()
subprocess.run([sys.executable, str(root / 'tools/codegen/generate-image36-native.py'),
                '--workspace', str(workspace)], check=True)
generated = workspace / 'analysis/native-image36'
if not (generated / 'reference_search.c').is_file():
    parser.error('The search fixture requires the reviewed EU 1.00 lift')
spu = root / 'ps3recomp/runtime/spu'
flags = ['-std=gnu17', '-O1', '-g', '-mavx2', '-fwrapv', '-fno-strict-aliasing',
         '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
         '-ffunction-sections', '-fdata-sections', '-DSPU_LS_DEBUG_HOOKS=0',
         *['-I' + str(p) for p in (generated, root / 'port', spu, root / 'ps3recomp/include')]]
with tempfile.TemporaryDirectory(prefix='buzz-image36-search-') as temp:
    out = Path(temp)
    drain = out / 'drain.c'
    drain.write_text((spu / 'spu_drain.c').read_text() + '\n' +
                    (root / 'port/spu_pure_loop_runtime.inc').read_text() + '''
/* Fixture access to the same private ring the production helper updates. */
void search_test_history_reset(unsigned n) {
    t_recent_n=n;g_spu_pch_n=n^0x12345678u;
    for(unsigned i=0;i<32;i++)t_recent[i]=0x10000u+i;
    for(unsigned i=0;i<8;i++)g_spu_pch[i]=0x20000u+i;
}
void search_test_history_copy(uint32_t* ring,unsigned* n) {
    for(unsigned i=0;i<32;i++)ring[i]=t_recent[i];*n=t_recent_n;
}
void search_test_hook(spu_context* ctx) {
    t_recent[t_recent_n++&31u]=(uint32_t)ctx->pc;ctx->steps++;
}
''')
    # Unused full drain dispatch is discarded. The fixture supplies the one
    # residual ordinary hook; the actual budget, bulk update and rings remain.
    subprocess.run([args.cc, *flags, '-Dspu_drain_step_hooks=unused_sdk_step_hooks',
                    '-c', str(drain), '-o', str(out / 'drain.o')], check=True)
    binary = out / 'test'
    subprocess.run([args.cc, *flags, '-include', str(root / 'port/spu_shuffle.h'),
                    str(generated / 'reference_search.c'), str(root / 'tests/image36-search-test.c'),
                    str(out / 'drain.o'), '-Wl,--gc-sections', '-lm', '-o', str(binary)], check=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('SPU_', 'SPURS_', 'BUZZ_'))}
    env.update(BUZZ_IMAGE36_SEARCH='1', BUZZ_IMAGE36_SEARCH_STATS='1')
    subprocess.run([str(binary)], env=env, check=True)
    # Startup diagnostic gates are cached: each needs a fresh process.
    for key, value in [('BUZZ_IMAGE36_SEARCH', '0'), ('SPU_PCHIST', '1'),
                       ('BUZZ_SPU_PURE_LOOPS_OFF', '1'), ('SPU_NO_LS0_END', '1'),
                       ('SPU_TAILRET', '1'), ('SPU_DRAIN_FASTPATH', '0')]:
        subprocess.run([str(binary), 'disabled'], env=dict(env, **{key: value}), check=True)
        print(f'PASS startup fallback {key}={value}', flush=True)
