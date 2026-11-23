"""Compare both entry PCs of each optimized mixer loop with the original lift."""
import argparse
from pathlib import Path
import re
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GEN = runpy.run_path(str(ROOT / 'tools/codegen/generate-audio-compressor.py'))
IMAGE = 'spu_0001_at_0071D300'
PREFIX = IMAGE + '_spu_func_0000'


def emit(workspace):
    original = (workspace / 'port/spu' / IMAGE / 'spu_recomp.c').read_text()
    source = ['#include "spu_helpers.h"']
    cases = []
    for first, latch in GEN['MIXER_BUFFER_LOOPS']:
        pcs = (first, latch)
        pair = '\n'.join(re.search(r'^void ' + PREFIX + pc +
                                   r'\(spu_context\* ctx\) \{.*?^\}', original, re.S | re.M)[0]
                         for pc in pcs)
        targets = sorted(set(re.findall(r'g_spu_trampoline_fn = ' + PREFIX + r'(\w+)', pair)))
        for variant, code in [('before', pair), ('after', GEN['fuse_mixer_buffer_loop'](pair, IMAGE, first, latch))]:
            names = {pc: variant + '_' + pc for pc in pcs}
            for pc in targets:
                if pc not in names:
                    names[pc] = 'exit_' + pc
                    if variant == 'before':
                        source.append(f'static void exit_{pc}(spu_context* ctx) {{ (void)ctx; }}')
            for pc in pcs:
                source.append(f'void {names[pc]}(spu_context*);')
            for pc, name in names.items():
                code = code.replace(PREFIX + pc, name)
            source.append(code)
        for pc in pcs:
            cases.append(f'{{before_{pc}, after_{pc}, 0x{pc}}}')
    source.extend(['#undef return', 'typedef void (*Block)(spu_context*);',
                   'static const struct { Block before, after; unsigned pc; } blocks[] = {',
                   ',\n'.join(cases), '};'])
    return '\n'.join(source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'loops.c'
        exe = src.with_suffix('')
        src.write_text(emit(args.workspace) + '\n' + (ROOT / 'tests/audio-buffer-loops.c.in').read_text())
        subprocess.run(['clang', '-std=gnu17', '-O2', '-march=znver2', '-DSPU_DIRECT_HOPS=1',
                        '-DSPU_LS_DEBUG_HOOKS=0', '-DSPU_LS_FAST=1', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-I', ROOT / 'port', '-I', args.workspace / 'ps3recomp/runtime/spu',
                        '-I', args.workspace / 'ps3recomp/include', src, '-o', exe, '-lm'], check=True)
        subprocess.run([exe], check=True)


if __name__ == '__main__':
    main()
