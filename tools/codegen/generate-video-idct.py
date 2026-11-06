"""Generate a scoped native RTZ leaf for the EU 1.00 Sofdec transform.

The image also contains double precision code. Only the reviewed, straight-line
202B0 -> 20E14 block runs under the single-precision FP environment.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, default=Path(__file__).resolve().parents[2])
    root = parser.parse_args().workspace
    image = 'spu_0007_at_0080A080'
    source_dir = root / 'port/spu' / image
    raw = (source_dir / 'spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest() != 'b04054f7b32c5f046263f3cbe33048a1da3080b405d6edb9f7dc6b5302303cda':
        raise SystemExit('Video transform reference changed; re-audit before generation')
    source = raw.decode()
    name = image + '_spu_func_000202B0'
    marker = f'void {name}(spu_context* ctx) {{'
    assert source.count(marker) == 1
    start = source.index(marker)
    end = source.index('\n}', start) + 2
    body = source[start + len(marker):end - 2]
    if re.search(r'spu_(?:df\w*|fesd|frds|fscr\w*|rdch|wrch|rchcnt|stop|halt)\(|spu_drain_call|SPU_RET', body):
        raise SystemExit('Unscopable FP/channel/call operation in video leaf')
    if set(re.findall(r'g_spu_trampoline_fn = (\w+);', body)) != {image + '_spu_func_00020E14'}:
        raise SystemExit('Unexpected video leaf exit')
    includes = ''.join(line + '\n' for line in source.splitlines() if line.startswith('#include'))
    wrapper = f'''
extern void buzz_video_idct_native(spu_context*);
extern void buzz_video_idct_run(spu_context*, void (*)(spu_context*), void (*)(spu_context*));
void {name}(spu_context* ctx) {{
    buzz_video_idct_run(ctx, {name}_reference, buzz_video_idct_native);
}}
'''
    out = root / 'analysis/native-video-idct'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'spu_recomp.c').write_text(source[:start] + f'void {name}_reference(spu_context* ctx) {{' + body + '\n}\n' + wrapper + source[end:])
    native = '#if defined(__aarch64__)\n#include "spu_neon_rtz.h"\n#include "native_video_idct_neon.h"\n#endif\n'
    # This leaf has no stores, callbacks, branches or suspension points before
    # its one tail exit. Keep intermediate registers local; only the final bank
    # is observable. Unknown input/output forms fail generation, not silently
    # change the live-in set.
    if 'spu_ls_write' in body or body.count('return;') != 1:
        raise SystemExit('Video leaf is no longer a read-only straight-line block')
    register = r'ctx->gpr\[(\d+)\]'
    used = set(map(int, re.findall(register, body)))
    written, live = set(), set()
    for line in body.splitlines():
        assignment = re.match(r'\s*ctx->gpr\[(\d+)\] = (.*);$', line)
        reads = assignment.group(2) if assignment else line
        live.update(int(n) for n in re.findall(register, reads) if int(n) not in written)
        if assignment:
            written.add(int(assignment.group(1)))
        elif 'ctx->gpr[' in line:
            raise SystemExit('Unreviewed register access in video leaf')
    local = re.sub(register, lambda m: f'r{m.group(1)}', body)
    tail = '        { ctx->pc = 0x20E14;'
    assert local.count(tail) == 1
    local = local.replace(tail, ''.join(f'        ctx->gpr[{n}] = r{n};\n' for n in sorted(written)) + tail)
    declarations = ''.join(f'    u128 r{n}' + (f' = ctx->gpr[{n}]' if n in live else '') + ';\n' for n in sorted(used))
    # A caller may supply external LS storage. The source form preserves even
    # deliberately aliased test contexts; the optimized form uses owned LS.
    fallback = '    if (ctx->ls != ctx->ls_store) {\n' + body + '\n    }\n'
    (out / 'kernel.c').write_text(includes + native + 'void buzz_video_idct_native(spu_context* ctx) {\n' + fallback + declarations + local + '\n}\n')
    exact = ''.join(f'#define spu_{op} spu_{op}_ref\n' for op in ('fa','fs','fma','cflts'))
    (out / 'oracle.c').write_text(includes + exact + 'void buzz_video_idct_oracle(spu_context* ctx) {' + body + '\n}\n')
    shutil.copyfile(source_dir / 'spu_recomp.h', out / 'spu_recomp.h')
    print(out)

if __name__ == '__main__':
    main()
