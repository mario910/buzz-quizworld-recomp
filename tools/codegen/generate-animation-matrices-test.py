"""Pinned original image-40 matrix producer, for published-buffer comparisons.

The native implementation does not use this register/LS representation.
"""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = '1b45bc6b4a53d7d4647714475a72e183b9b4e7a79d4e032f2e8925ed728f60ba'
PREFIX = 'buzz_job_abe1_spu_func_'


def main():
    raw = (ROOT / 'port/spu/buzz_job_abe1/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit('Unreviewed animation matrix oracle source')
    functions = {int(pc, 16): body for pc, body in re.findall(
        r'(?ms)^void buzz_job_abe1_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}', raw.decode())}
    pending, selected = [0x20], set()
    while pending:
        pc = pending.pop()
        if pc in selected:
            continue
        selected.add(pc)
        pending.extend(int(pc, 16) for pc in re.findall(PREFIX + r'([0-9A-F]+)', functions[pc]))
    if selected != {0x20, 0x34, 0x60, 0x74, 0x7c, 0xbc, 0xd4}:
        raise SystemExit('Unreviewed animation matrix graph')
    output = '\n'.join(f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected))
    output += '\n' + '\n'.join(f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}' for pc in sorted(selected))
    out = ROOT / 'analysis/native-geometry/phase4/matrices-v1'
    out.mkdir(parents=True, exist_ok=True)
    reference = out / 'reference.inc'
    if reference.exists():
        if reference.read_text(encoding='utf-8') != output:
            raise SystemExit('Existing animation oracle differs; preserve its evidence and use a new oracle version')
    else:
        reference.write_text(output, encoding='utf-8')
    print('Pinned original matrix producer: seven complete blocks; published bytes only')


if __name__ == '__main__':
    main()
