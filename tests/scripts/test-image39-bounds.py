#!/usr/bin/env python3
"""Test the production Image 39 bounds driver against unchanged pinned code.

Linux/WSL; --arm64 runs correctness under QEMU. Private Stage F k3 captures
are optional. No game installation or display is needed for synthetic tests.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--corpus', action='append', default=[], type=Path)
    parser.add_argument('--arm64', action='store_true')
    parser.add_argument('--bench', action='store_true')
    args = parser.parse_args()
    if args.arm64 and args.bench:
        parser.error('QEMU timing is not a hardware benchmark')
    root = Path(__file__).resolve().parents[2]
    cases = []
    for directory in args.corpus:
        captured = sorted(directory.resolve().glob('stagef_k3_*.bin'))
        if not captured:
            parser.error('No Image 39 Stage F captures in ' + str(directory))
        cases.extend(captured)
    prefix = 'aarch64-linux-gnu-' if args.arm64 else ''
    runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if args.arm64 else []
    flags = ['-O2', '-UNDEBUG', '-DSPU_LS_DEBUG_HOOKS=0', '-frounding-math',
             '-ffp-contract=off', '-ffunction-sections', '-fdata-sections']
    if not args.arm64:
        flags += ['-mavx2', '-mfma']
    includes = ['-I'+str(root/p) for p in ('ps3recomp/include', 'ps3recomp/runtime/spu',
                                          'ps3recomp/runtime/platform', 'port')]

    def run(command):
        subprocess.run([str(x) for x in command], cwd=root, check=True)

    with tempfile.TemporaryDirectory(prefix='buzz-image39-bounds-') as temp:
        out = Path(temp)
        run([sys.executable, root/'tools/codegen/image39_bounds.py', '--reference',
             args.reference.resolve(), '--output', out/'original.c'])
        run([prefix+'gcc', *flags, *includes, '-c', out/'original.c', '-o', out/'original.o'])
        run([prefix+'g++', '-std=gnu++17', *flags, *includes, root/'tests/image39-bounds-test.cpp',
             root/'port/native_image39_bounds.cpp', out/'original.o', '-Wl,--gc-sections',
             '-lm', '-o', out/'test'])
        run([*runner, out/'test', *(['--bench'] if args.bench else []), *cases])


if __name__ == '__main__':
    main()
