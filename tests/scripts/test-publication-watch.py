"""Check that native C/C++ publication observers share one registration map."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arm64', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    prefix = 'aarch64-linux-gnu-' if args.arm64 else ''
    includes = ['-I'+str(root/'port'), '-I'+str(root/'ps3recomp/include')]
    with tempfile.TemporaryDirectory(prefix='buzz-publication-watch-') as temp:
        out = Path(temp)
        subprocess.run([prefix+'gcc', '-O2', *includes, '-c', str(root/'tests/publication-watch-c.c'), '-o', str(out/'c.o')], check=True)
        subprocess.run([prefix+'g++', '-std=c++17', '-O2', '-UNDEBUG', '-pthread', *includes,
                        str(root/'tests/publication-watch-test.cpp'), str(out/'c.o'), '-o', str(out/'test')], check=True)
        runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if args.arm64 else []
        subprocess.run([*runner, str(out/'test')], check=True)


if __name__ == '__main__':
    main()
