"""Run the display-independent surface-format regression with Vulkan headers."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='buzz-surface-formats-') as directory:
    executable = Path(directory) / 'surface-formats-test'
    command = [args.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror']
    if args.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    command += [str(root / 'tests/vulkan-surface-formats-test.c'), '-o', str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
