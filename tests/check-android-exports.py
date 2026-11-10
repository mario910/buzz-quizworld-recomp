"""Audit libmain.so's complete public JNI/SDL surface after Android linking."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('elf', type=Path)
parser.add_argument('--readelf', default='llvm-readelf')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
expected = {'SDL_main'}
for name in ['android_controls.cpp', 'android_diagnostics.cpp']:
    expected.update(re.findall(r'\bJava_org_buzzrecomp_game_\w+', (root / 'port' / name).read_text()))
result = subprocess.run([args.readelf, '--dyn-symbols', '--wide', str(args.elf)],
                        capture_output=True, text=True, check=True)
exported = set()
for line in result.stdout.splitlines():
    fields = line.split()
    if len(fields) >= 8 and fields[0].endswith(':') and fields[4] in ('GLOBAL', 'WEAK') and fields[6] != 'UND':
        exported.add(fields[7])
if exported != expected:
    raise SystemExit(f'Android export mismatch: missing={sorted(expected-exported)} extra={sorted(exported-expected)}')
print(f'PASS Android exports: {len(exported)} defined JNI/SDL symbols; no internal API leakage')
