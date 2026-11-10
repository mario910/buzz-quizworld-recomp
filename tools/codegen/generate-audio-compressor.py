"""Localize SPU registers in hash-pinned 1.00 audio DSP blocks.

Instruction order is preserved. Optional ARM64 checked NEON arithmetic is
scoped to these leaves, with exact exceptional-lane repair and restored host
FP state. No whole-image FP mode or approximate reciprocal. Only owned LS
permits keeping GPRs local across stores. Generated sources stay private.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil

PIN = 'c90383fcb4712df518ac193de96f858fb30ff30b776d410059aa3a3e3aa82ee0'
ENTRIES = ('0002FF18', '000301D8')


def generate(root, directory, pin, prefix, entries, output, tails=None):
    raw = (directory / 'spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest() != pin:
        raise SystemExit(f'Unreviewed {output} lift; audit before generation')
    source = raw.decode()
    kernel = '#include "spu_context.h"\n#include "spu_helpers.h"\n'
    oracle = kernel
    baseline = kernel
    kernel += '#if defined(__aarch64__) && defined(BUZZ_AUDIO_COMPRESSOR_NEON)\n#include "spu_arm_fp_state.h"\n#include "spu_neon_rtz.h"\n#endif\n'
    for op in ('fa', 'fs', 'fm', 'fma', 'fms', 'fnms'):
        oracle += f'#define spu_{op} spu_{op}_ref\n'
    for entry in entries:
        name = f'{prefix}_spu_func_{entry}'
        marker = f'void {name}(spu_context* ctx) {{'
        if source.count(marker) != 1:
            raise SystemExit('Expected one reviewed leaf')
        start = source.index(marker)
        end = source.index('\n}', start) + 2
        body = source[start + len(marker):end - 2]
        tail_exits = []
        checked = body
        if tails and tails.get(entry):
            targets = tails[entry] if isinstance(tails[entry], tuple) else (tails[entry],)
            for target_pc in targets:
                target = f'{prefix}_spu_func_{target_pc}'
                tail = f'{{ ctx->pc = 0x{int(target_pc,16):X}; g_spu_trampoline_fn = {target}; return; }}'
                if body.count(tail) != 1:
                    raise SystemExit('Expected one reviewed tail exit')
                tail_exits.append(tail)
                checked = checked.replace(tail, '')
                declaration = f'extern void {target}(spu_context*);\n'
                kernel += declaration
                oracle += declaration
                baseline += declaration
        if re.search(r'spu_(?:df\w*|fesd|frds|fscr\w*|rdch|wrch|rchcnt|stop|halt)\(|spu_drain_call|g_spu_trampoline_fn', checked):
            raise SystemExit('Unreviewed FP mode, channel, call or tail exit')
        if checked.count('SPU_RET(ctx);') != (0 if tail_exits else 1) or 'return;' in checked:
            raise SystemExit('Expected one return after the local loop')
        # Only the GPR bank, LS operations and SPU_RET may touch the context.
        if re.search(r'ctx->(?!gpr\[)', checked):
            raise SystemExit('Unreviewed context access')
        register = r'ctx->gpr\[(\d+)\]'
        used = set(map(int, re.findall(register, body)))
        written = set(map(int, re.findall(register + r' = ', body)))
        local = re.sub(register, lambda m: f'r{m[1]}', body)
        flush = ''.join(f'        ctx->gpr[{n}] = r{n};\n' for n in sorted(written))
        flush += '#if defined(__aarch64__) && defined(BUZZ_AUDIO_COMPRESSOR_NEON)\n        buzz_arm_fp_restore(saved_fp);\n#endif\n'
        if tail_exits:
            for tail in tail_exits:
                local = local.replace(tail, '{\n' + flush + tail[1:])
        else:
            local = local.replace('        SPU_RET(ctx);', flush + '        SPU_RET(ctx);')
        declarations = ''.join(f'    u128 r{n} = ctx->gpr[{n}];\n' for n in sorted(used))
        reference = name + '_reference'
        native = 'buzz_audio_' + entry + '_local'
        kernel += f'extern void {reference}(spu_context*);\nvoid {native}(spu_context* ctx) {{\n'
        kernel += f'    if (SPU_LS_DEBUG_HOOKS || ctx->ls != ctx->ls_store) {{ {reference}(ctx); return; }}\n'
        kernel += '#if defined(__aarch64__) && defined(BUZZ_AUDIO_COMPRESSOR_NEON)\n    const buzz_arm_fp_state saved_fp = buzz_arm_fp_save();\n    buzz_arm_fp_enter_rtz(saved_fp);\n#endif\n'
        kernel += declarations + local + '\n}\n'
        oracle += f'void buzz_audio_{entry}_oracle(spu_context* ctx) {{' + body + '\n}\n'
        baseline += f'void buzz_audio_{entry}_baseline(spu_context* ctx) {{' + body + '\n}\n'
        wrapper = f'''
extern void {native}(spu_context*);
extern void buzz_audio_compressor_run(spu_context*, void (*)(spu_context*), void (*)(spu_context*), unsigned);
void {name}(spu_context* ctx) {{
    buzz_audio_compressor_run(ctx, {reference}, {native}, {int(entry,16)}u);
}}
'''
        source = source[:start] + f'void {reference}(spu_context* ctx) {{' + body + '\n}\n' + wrapper + source[end:]
    out = root / 'analysis' / output
    out.mkdir(parents=True, exist_ok=True)
    for name, content in [('spu_recomp.c', source), ('kernel.c', kernel), ('oracle.c', oracle), ('baseline.c', baseline)]:
        (out / name).write_text(content, encoding='utf-8', newline='\n')
    shutil.copyfile(directory / 'spu_recomp.h', out / 'spu_recomp.h')
    print(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--include-mixer', action='store_true')
    parser.add_argument('--include-atrac', action='store_true',
                        help='Generate two audited single-precision ATRAC filter blocks')
    args = parser.parse_args()
    root = args.workspace
    generate(root, root / 'analysis/mstream-modules/compressor', PIN,
             'buzz_mstream_compressor', ENTRIES, 'native-audio-compressor')
    if args.include_mixer:
        generate(root, root / 'port/spu/spu_0001_at_0071D300',
                 'f198ca88edd7d30c393aa95656e5cb29ced60dedf393f128388373125a882e36',
                 'spu_0001_at_0071D300', ('0000B078','0000C758','0000C8D4','00004E00','00004F24','0000513C','000051F8'), 'native-audio-mixer',
                 {'0000B078':'0000B100','0000C758':'0000C7E0','0000C8D4':'0000C95C',
                  '00004E00':('00004DC8','00004EEC'),'00004F24':'00004FF0','0000513C':None,'000051F8':'00004DA8'})
    if args.include_atrac:
        generate(root, root / 'analysis/mstream-modules/atrac',
                 '0b864e850022c72359df119cd5aaff459f4efc5ac6c3ad5435c54b1abaa5a78e',
                 'buzz_mstream_atrac', ('00036538','00036710'), 'native-audio-atrac',
                 {'00036538':'00036710','00036710':('000363E0','00036988','00036828')})


if __name__ == '__main__':
    main()
