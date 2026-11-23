"""Reject late audio RTZ defines after the production forced helper include."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('workspace', type=Path)
args = parser.parse_args()
root = args.workspace.resolve()
base = [os.environ.get('CC', 'clang'), '-E', '-mavx2', '-mfma',
        '-I'+str(root/'port'), '-I'+str(root/'ps3recomp/include'), '-I'+str(root/'ps3recomp/runtime/spu'),
        '-include', str(root/'port/spu_shuffle.h')]
for name in ('compressor', 'mixer', 'atrac'):
    kernel = str(root/f'analysis/native-audio-{name}/kernel.c')
    bad = subprocess.run([*base, kernel], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    assert bad.returncode and 'Audio RTZ definitions must precede' in bad.stderr, bad.stderr
    good = subprocess.run([*base, '-DSPU_NATIVE_RTZ=1', '-DSPU_RTZ_RESULT_GUARD=1', kernel],
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    assert good.returncode == 0, good.stderr
print('PASS all three audio kernels: late definitions rejected, production compiler definitions accepted')
