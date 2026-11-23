"""Compare the six EU SPU record regions with their original register and LS results."""
import argparse
from pathlib import Path
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GEN = runpy.run_path(str(ROOT / "tools/codegen/generate-spu-records.py"))


def regions(workspace):
    functions = ['#include "spu_helpers.h"', '#include "native_spu_records.h"']
    names = []
    for image, entries in GEN['BLOCKS'].items():
        original = (workspace/'port/spu'/image/'spu_recomp.c').read_text()
        optimized = GEN['generate'](original, image)
        for pc in entries:
            name = image + '_spu_func_' + pc
            before = GEN['function'](original, name)[0]
            after = GEN['function'](optimized, name)[0]
            a, b = GEN['span'](before, pc)
            prefix, suffix = before[:a], before[b:]
            assert after.startswith(prefix) and after.endswith(suffix)
            for label, body in [('old', before[a:b]), ('new', after[len(prefix):-len(suffix)])]:
                functions.append(f'static void {label}_{pc}(spu_context* ctx) {{\n{body}\n}}')
            names.append(pc)
    functions.append('typedef void (*Region)(spu_context*);')
    functions.append('static const Region pairs[][2] = {' +
                     ','.join('{old_' + n + ',new_' + n + '}' for n in names) + '};')
    return '\n'.join(functions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as t:
        src = Path(t)/'test.c'
        exe = Path(t)/'test'
        src.write_text(regions(args.workspace) + (ROOT/'tests/spu-record-regions-test.c.in').read_text())
        subprocess.run(['clang', '-O2', '-march=znver2', '-DSPU_LS_DEBUG_HOOKS=0', '-DSPU_LS_FAST=1',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I', ROOT/'port', '-I', args.workspace/'ps3recomp/runtime/spu',
                        '-I', args.workspace/'ps3recomp/include', src, '-lm', '-o', exe], check=True)
        subprocess.run([exe], check=True)


if __name__ == '__main__':
    main()
