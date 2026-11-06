"""Constructor-order and override checks for the optional SPURS wait profile.

Run outside performance measurements. Windows requires a VS developer shell.
ARM64 uses QEMU only for correctness, never for performance measurement.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--arm64', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    windows = sys.platform == 'win32'
    runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if args.arm64 else []
    sources = [root/'tests/runtime-spurs-defaults-test.cpp', root/'port/runtime_defaults.cpp']
    for name, profile, standalone, spin in [
            ('disabled', 0, False, 50), ('automatic', 1, False, -1),
            ('sleep', 1, False, 0), ('short', 1, False, 50),
            ('long', 1, False, 1000), ('standalone', 0, True, 50)]:
        case = out/name
        case.mkdir()
        exe = case/('test.exe' if windows else 'test')
        defines = [f'BUZZ_RUNTIME_DEFAULTS={profile}', f'BUZZ_SPURS_IDLE_SPIN_US={spin}']
        if standalone:
            defines.append('BUZZ_STANDALONE=1')
        command = ([args.compiler, '/nologo', '/std:c++17', '/O2',
                    *['/D'+x for x in defines], *map(str, sources),
                    '/Fo'+str(case)+os.sep, '/Fe'+str(exe)] if windows else
                   [args.compiler, '-std=c++17', '-O2', *['-D'+x for x in defines],
                    *map(str, sources), '-o', str(exe)])
        subprocess.run(command, check=True)
        default = str(spin) if (profile or standalone) and spin >= 0 else '<unset>'
        for pool, chain in [(None, None), ('0', None), (None, '125'), ('0', '0'), ('', '50')]:
            env = dict(os.environ)
            for key, value in [('SPURS_POOL_SPIN_US', pool), ('SPURS_JTS_SPIN_US', chain)]:
                if value is None:
                    env.pop(key, None)
                else:
                    env[key] = value
            subprocess.run([*runner, str(exe), pool if pool is not None else default,
                            chain if chain is not None else default], env=env, check=True)
    config = out/'config'
    config.mkdir()
    (config/'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(wait_options NONE)\n'
        f'include("{(root/"cmake/BuzzSpursWaits.cmake").as_posix()}")\n')
    for number, value in enumerate(['-1', '0', '50', '1000', '-2', '1001', '01', 'abc', '5.0', '']):
        result = subprocess.run([args.cmake, '-S', str(config), '-B', str(out/f'cmake-{number}'),
                                 '-DBUZZ_SPURS_IDLE_SPIN_US='+value], capture_output=True, text=True)
        (out/f'cmake-{number}.log').write_text(result.stdout+result.stderr)
        if (result.returncode == 0) != (number < 4):
            raise RuntimeError(f'Unexpected CMake validation for {value!r}: {result.stderr}')
    print('PASS six build profiles, 30 constructor/override cases, ten CMake boundary cases')


if __name__ == '__main__':
    main()
