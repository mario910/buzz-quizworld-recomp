"""Pinned original image-40 job, with optional data-observation hooks.

This is a test oracle only. No native data path uses its register bank.
"""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = '1b45bc6b4a53d7d4647714475a72e183b9b4e7a79d4e032f2e8925ed728f60ba'


def main():
    # Keep the exact reviewed source pin, not a normalization of its code.
    raw = (ROOT / 'port/spu/buzz_job_abe1/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit('Unreviewed animation whole-job oracle source')
    functions = {int(pc, 16): body for pc, body in re.findall(
        r'(?ms)^void buzz_job_abe1_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}', raw.decode())}
    if len(functions) != 279 or 0x10 not in functions:
        raise SystemExit('Unreviewed animation whole-job graph')
    prefix = 'buzz_job_abe1_spu_func_'
    output = '\n'.join(f'static void {prefix}{pc:08X}(spu_context*);' for pc in functions)
    for pc, body in functions.items():
        probe = f'animation_original_probe(0x{pc:x},ctx);' if pc in (0x6950,0x7408) else ''
        if pc == 0x6950:
            probe = 'if(animation_native_quaternion_override(ctx))return;' + probe
        output += f'\nstatic void {prefix}{pc:08X}(spu_context* ctx) {{{probe}{body}}}'
    output += '\nstatic void (*animation_original_lookup(unsigned pc))(spu_context*) { switch(pc) {\n'
    output += '\n'.join(f'case 0x{pc:x}:return {prefix}{pc:08X};' for pc in functions)
    output += '\ndefault:return nullptr;} }\n'
    out = ROOT / 'analysis/native-geometry/phase4/job-oracle-v2/reference.inc'
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        if out.read_text(encoding='utf-8') != output:
            raise SystemExit('Existing animation job oracle differs; preserve it and use a new version')
    else:
        out.write_text(output, encoding='utf-8')
    print('Pinned original animation job: 279 complete blocks; observation hooks at 6950/7408')


if __name__ == '__main__':
    main()
