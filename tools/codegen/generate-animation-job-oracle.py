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
        probe = f'animation_original_probe(0x{pc:x},ctx);' if pc in (0x6950,0x7408,0x7490,0x7788,0x7790,0x85d0,0x8688,0x8d00,0x9608,0x9a10,0x9a18,0x3188,0x3810,0x4fb8,0x5730,0x4d60,0x4fb0,0x20) else ''
        if pc in (0x20,0x34,0x60,0x74,0x7c,0xbc,0xd4):
            body = body.replace('SPU_RET(ctx);','animation_original_probe(0x10020,ctx);SPU_RET(ctx);')
        if pc in (0x9a18,0x9cc8):
            body = body.replace('SPU_RET(ctx);','animation_original_probe(0x19a18,ctx);SPU_RET(ctx);')
        if pc == 0x6950:
            probe = 'if(animation_native_quaternion_override(ctx))return;' + probe
        if pc == 0x20:
            probe = 'if(animation_native_affine_override(ctx))return;' + probe
        if pc == 0x4d60:
            probe = 'if(animation_native_matrix_override(ctx))return;' + probe
        if pc == 0x9a18:
            probe = 'if(animation_native_vector_override(ctx))return;' + probe
        if pc == 0x7490:
            probe = 'if(animation_native_static_quaternion_override(ctx))return;' + probe
        output += f'\nstatic void {prefix}{pc:08X}(spu_context* ctx) {{{probe}{body}}}'
    output += '\nstatic void (*animation_original_lookup(unsigned pc))(spu_context*) { switch(pc) {\n'
    output += '\n'.join(f'case 0x{pc:x}:return {prefix}{pc:08X};' for pc in functions)
    output += '\ndefault:return nullptr;} }\n'
    out = ROOT / 'analysis/native-geometry/phase4/job-oracle-v9/reference.inc'
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        if out.read_text(encoding='utf-8') != output:
            raise SystemExit('Existing animation job oracle differs; preserve it and use a new version')
    else:
        out.write_text(output, encoding='utf-8')
    print('Pinned original animation job: 279 complete blocks; quaternion/FLOAT3 observation and replacement hooks')


if __name__ == '__main__':
    main()
