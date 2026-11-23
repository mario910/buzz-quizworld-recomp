"""Localize SPU registers in hash-pinned 1.00 audio DSP blocks.

Owned LS permits keeping GPRs local across stores. The optional x86 path
pairs independent quads and the ARM64 path uses checked NEON arithmetic.
Both preserve guest memory dependencies and restore host FP state.
Generated sources stay private.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil

PIN = 'c90383fcb4712df518ac193de96f858fb30ff30b776d410059aa3a3e3aa82ee0'
ENTRIES = ('0002FF18', '000301D8')


# Entries in the pinned EU mixer image. None calls out while holding local registers.
MIXER_SIMD = {'00004A4C': None, '000047B4': '00004890', '0000AC1C': '0000ACA4',
              '00008400': '00008460', '00005524': '000055B8',
              '000053DC': '00004DA8', '0000A290': '0000A338', '0000A214': '0000A27C',
              '0000A504': '0000A590', '0000C65C': '0000C6C0',
              '000059C0': '00005BA4', '00008578': '0000899C'}


ATRAC_SIMD = {'00034770': '00034840', '00034840': '000348C4',
              '000344E0': ('00034400', '00034608'), '000363E0': '00036538',
              '00034B58': ('00035294', '00034ED8'), '00034930': '00034B58',
              '00034608': '00034770', '000342E8': '00034400',
              '00035600': ('00035460', '000357E8'),
              '000358B0': '0003405C', '00033DE8': '0003405C',
              '00034410': '000344E0', '000352D8': '000342D8', '000341E0': '000342D8',
              '00035030': ('00034ED8', '00035294'), '00034F38': '00035028'}


# These blocks contain only integer operations, except the final PCM conversion.
# The five copies occur in different paths of the decoder with distinct exits.
ATRAC_COPIES = {
    '00037AE8': (13, 4, 7, '00037BE0', '00037B0C'),
    '00036EB0': (5, 7, 8, '000377A8', '00036ED8'),
    '00037750': (5, 7, 8, '00036F40', '00037778'),
    '00037148': (15, 7, 8, '00037408', '00037170'),
    '00037590': (5, 4, 7, '00037078', '000375BC'),
}
ATRAC_SIMD.update({entry: row[3:] for entry, row in ATRAC_COPIES.items()})
ATRAC_SIMD.update({'000368B8': ('00036890', '00036950'), '00036370': '000363E0',
                   '0003614C': ('000360F8', '000353C8')})
MIXER_SIMD.update({'000055DC': None, '000057EC': '00005890'})
AUDIO_LOOP_SHUFFLES = {'000055DC': ((15, 4), (16, 4)), '000057EC': ((18, 4), (19, 4)),
                       '00036370': ((36, 4),), '000368B8': ((16, 4),)}


def optimize_sample_loop(entry, body):
    if entry in ATRAC_COPIES:
        count, src, dst, _, _ = ATRAC_COPIES[entry]
        finish = f'goto loc_{entry};'
        assert body.count(finish) == 1
        # If LS wraps, forward stores feed later loads, or the first iteration
        # underflows, keep the guest loop with its original access order.
        begin = (f'    if (audio_copy_quads(ctx, &r{count}, &r{src}, &r{dst}, &r2, &r3))'
                 ' goto copy_finished;\n')
        return begin + body.replace(finish, finish + '\ncopy_finished:')
    if entry == '0003614C':
        # r6 advances by one quad. The selected word and source word stay fixed.
        select = 'r5 = spu_cwx(r4, r7);'
        insert = 'spu_shufb(r11, r3, r5)'
        assert body.count(select) == body.count(insert) == 1
        setup = ('    r5 = spu_cwx(spu_a(r12, r6), r7);\n'
                 '    const __m128i mask = audio_sample_mask(r12._u32[0] + r6._u32[0] + r7._u32[0]);\n')
        return setup + body.replace(select, '').replace(insert, 'audio_insert_sample(r11, r3, mask)')
    setup = ''
    for reg, expected in AUDIO_LOOP_SHUFFLES[entry]:
        pattern = rf'spu_shufb\((r\d+), (r\d+), r{reg}\)'
        body, hits = re.subn(pattern, rf'audio_shuffle_samples(\1, \2, mask{reg})', body)
        if hits != expected:
            raise ValueError('Unexpected sample shuffle schedule')
        setup += f'    const audio_shuffle_mask mask{reg} = audio_prepare_shuffle(r{reg});\n'
    if entry == '000368B8':
        body, count = re.subn(r'spu_cflts\((r\d+), 157\)', r'audio_pcm_fixed16(\1)', body)
        assert count == 8
    return setup + body


def pair_independent_audio_quads(local, *, pair_fma=False):
    # Match independent quads in a straight-line part of a butterfly. Delay
    # the first result only while both its inputs and destination are untouched.
    operations = [('fs', 'audio_difference8'), ('fa', 'audio_sum8'), ('fm', 'audio_product8')]
    if pair_fma:
        operations.append(('fma', 'audio_fma8'))
    for op, helper in operations:
        operands = ', '.join([r'(r\d+)'] * (3 if op == 'fma' else 2))
        pattern = rf'        (r\d+) = spu_{op}\({operands}\);'
        while True:
            sites = list(re.finditer(pattern, local))
            for a, b in zip(sites, sites[1:]):
                gap = local[a.end():b.start()]
                if ('goto ' in gap or 'return;' in gap or 'loc_' in gap or 'audio_' in gap or
                        a[1] in b.groups() or re.search(rf'\b{a[1]}\b', gap) or
                        any(re.search(rf'\b{reg} =', gap) for reg in a.groups()[1:])):
                    continue
                args = ', '.join((*a.groups()[1:], *b.groups()[1:], '&' + a[1], '&' + b[1]))
                call = f'        {helper}({args});'
                local = local[:a.start()] + gap.lstrip('\n') + call + local[b.end():]
                break
            else:
                break
    if 'audio_' not in local:
        raise ValueError('Expected independent audio quads')
    return local


def fuse_atrac_signed_bytes(body, expected):
    # XSBH then XSHW sign-extend the low byte of each of the four words.
    # Retain intervening instructions and require the intermediate to be dead.
    pattern = re.compile(r'        (r\d+) = spu_xsbh\((r\d+)\);\n')
    fused = 0
    for match in reversed(list(pattern.finditer(body))):
        dest, source = match.groups()
        target = f'        {dest} = spu_xshw({dest});'
        end = body.find(target, match.end())
        if end < 0:
            continue
        gap = body[match.end():end]
        if re.search(rf'\b{dest}\b|\bgoto\b|\breturn\b|\bloc_', gap):
            continue
        body = (body[:match.start()] + f'        {dest} = audio_signed_bytes({source});\n'
                + gap + body[end + len(target):])
        fused += 1
    if fused != expected:
        raise ValueError(f'Unexpected signed-byte schedule: {fused} of {expected}')
    return body


def mixer_simd_body(entry, body):
    if entry == '00004A4C':
        # The caller admitted a word transpose. Interleave words first,
        # then pairs of words, preserving all architecturally visible results.
        for selector, unpack in [(33, 'unpacklo_epi32'), (31, 'unpackhi_epi32'),
                                 (29, 'unpacklo_epi64'), (28, 'unpackhi_epi64')]:
            pattern = rf'spu_shufb\((r\d+), (r\d+), r{selector}\)'
            body, count = re.subn(pattern, rf'rtz_store(_mm_{unpack}(rtz_load(\1), rtz_load(\2)))', body)
            if count != 8:
                raise ValueError('Unexpected audio transpose schedule')
        return body

    if entry in ('000053DC', '000059C0', '00008578', '0000C65C'):
        return pair_independent_audio_quads(body, pair_fma=entry == '00008578')

    operation, helper = {
        '000047B4': ('fm', 'audio_product8'), '00005524': ('fm', 'audio_product8'),
        '0000A504': ('fma', 'audio_mix8'), '0000A290': ('fma', 'audio_mix8'), '0000A214': ('fm', 'audio_product8'),
        '0000AC1C': ('fma', 'audio_mix8'), '00008400': ('csflt', 'audio_pcm8'),
    }[entry]
    pattern = rf'        (r\d+) = spu_{operation}\(([^)]+)\);'
    ops = list(re.finditer(pattern, body))
    if len(ops) != 8:
        raise ValueError('Expected eight sample quads')
    for a, b in reversed(list(zip(ops[::2], ops[1::2]))):
        left, right = a[2].split(', '), b[2].split(', ')
        # Delay the first result to the second operation. No intervening
        # instruction may observe that result or change any of its operands.
        between = body[a.end():b.start()]
        if (a[1] in right or a[1] == b[1] or re.search(rf'\b{a[1]}\b', between) or
                any(re.search(rf'\b{reg} =', between) for reg in left)):
            raise ValueError('Coupled audio sample operations')
        if operation == 'fma':
            if left[0] != right[0]:
                raise ValueError('Expected shared mixer gain')
            right = right[1:]
        if operation == 'csflt':
            if left[-1] != '124' or right[-1] != '124':
                raise ValueError('Expected signed PCM scale')
            left, right = left[:1], right[:1]
        args = ', '.join([*left, *right, '&' + a[1], '&' + b[1]])
        body = body[:a.start()] + between.lstrip('\n') + f'        {helper}({args});' + body[b.end():]
    return body


# Each pair is one integer-only buffer loop split by the lifter. Either PC
# can be entered independently, including on the last trip through the latch.
MIXER_BUFFER_LOOPS = (
    ('3E3C', '3E84'), ('3BE0', '3C2C'), ('AB50', 'AB74'),
    ('AFB4', 'AFD8'), ('6A24', '6A54'), ('7BA0', '7BE4'),
)


def fuse_mixer_buffer_loop(source, prefix, first, latch):
    pcs = (first, latch)
    symbols = {pc: f'{prefix}_spu_func_{pc:0>8}' for pc in pcs}
    functions = {pc: re.search(r'void ' + name + r'\(spu_context\* ctx\) \{(.*?)\n\}', source, re.S)
                 for pc, name in symbols.items()}
    bodies = [functions[pc][1] for pc in pcs]
    combined = ''.join(bodies)
    reg = r'ctx->gpr\[(\d+)\]'
    regs = sorted(set(map(int, re.findall(reg, combined))))
    written = sorted(set(map(int, re.findall(reg + ' = ', combined))))
    edge = re.compile(r'\{ ctx->pc = 0x([0-9A-F]+); g_spu_trampoline_fn = (\w+); return; \}')
    guard = '#if defined(SPU_DIRECT_HOPS) && defined(__clang__) && !SPU_LS_DEBUG_HOOKS'
    lines = [guard, '#pragma push_macro("return")', '#undef return',
             f'static void mixer_buffer_{first}(spu_context* ctx, int at_latch) {{']
    lines.extend(f'    u128 r{n} = ctx->gpr[{n}];' for n in regs)
    lines.append(f'    if (at_latch) goto block_{latch};')

    def transfer(match):
        pc, symbol = match.groups()
        if symbol != f'{prefix}_spu_func_{pc:0>8}':
            raise ValueError('Mixer loop PC and target differ')
        jump = ''
        if pc in pcs:
            # Exactly the same admission and step accounting as a direct tail
            # hop. Interrupts and step observers still see a complete context.
            jump = (f'if (spu_hop_direct_ok(ctx, {symbol})) {{ '
                    f'g_spu_trampoline_fn = 0; ++ctx->steps; goto block_{pc}; }} ')
        return f'{{ ctx->pc = 0x{pc}; {jump}g_spu_trampoline_fn = {symbol}; goto finish; }}'

    for pc, body in zip(pcs, bodies):
        edges = edge.findall(body)
        if (len(edges) != (1 if pc == first else 2) or
                edges[0][0] != (latch if pc == first else first)):
            raise ValueError('Unreviewed mixer loop edges')
        if re.search(r'ctx->(?!gpr\[)|SPU_RET|spu_(?:drain|rdch|wrch)|return;|goto ', edge.sub('', body)):
            raise ValueError('Unreviewed mixer loop operation')
        lines += [f'block_{pc}:', edge.sub(transfer, re.sub(reg, r'r\1', body)).strip('\n')]
    lines.append('finish:')
    lines.extend(f'    ctx->gpr[{n}] = r{n};' for n in written)
    lines += ['}', '#pragma pop_macro("return")', '#endif']
    for pc in pcs:
        match = functions[pc]
        fast = (f'\n{guard}\n    if (ctx->ls == ctx->ls_store && g_spu_direct_hops > 0) {{\n'
                f'        mixer_buffer_{first}(ctx, {int(pc == latch)});\n        return;\n    }}\n#endif')
        replacement = match[0].replace(match[1], fast + match[1], 1)
        if pc == first:
            replacement = '\n'.join(lines) + '\n' + replacement
        source = source.replace(match[0], replacement, 1)
    return source


def generate(root, directory, pin, prefix, entries, output, tails=None, x86_rtz=False):
    raw = (directory / 'spu_recomp.c').read_bytes()
    # Older MultiStream pins were recorded with Windows CRLF, while the
    # embedded mixer pin uses LF. Accept either representation of the exact
    # reviewed text, without accepting any instruction/content changes.
    normalized = raw.replace(b'\r\n', b'\n')
    if pin not in {hashlib.sha256(normalized).hexdigest(),
                   hashlib.sha256(normalized.replace(b'\n', b'\r\n')).hexdigest()}:
        raise SystemExit(f'Unreviewed {output} lift; audit before generation')
    source = normalized.decode()
    kernel = '#include "spu_context.h"\n#include "spu_helpers.h"\n'
    oracle = kernel
    baseline = kernel
    if x86_rtz:
        kernel = ('#if defined(SPU_HELPERS_H) && (!defined(SPU_NATIVE_RTZ) || !defined(SPU_RTZ_RESULT_GUARD))\n'
                  '#error \"Audio RTZ definitions must precede forced SPU helper includes\"\n#endif\n'
                  '#define SPU_NATIVE_RTZ 1\n#define SPU_RTZ_RESULT_GUARD 1\n'
                  '#include <immintrin.h>\n' + kernel)
    kernel += '#if defined(__aarch64__) && defined(BUZZ_AUDIO_COMPRESSOR_NEON)\n#include "spu_arm_fp_state.h"\n#include "spu_neon_rtz.h"\n#endif\n'
    for op in ('fa', 'fs', 'fm', 'fma', 'fms', 'fnms'):
        oracle += f'#define spu_{op} spu_{op}_ref\n'
    if x86_rtz:
        kernel += '#include "native_audio_mixer_x86.h"\n'
    for entry in entries:
        mixer_simd = x86_rtz and output == 'native-audio-mixer' and entry in MIXER_SIMD
        atrac_simd = x86_rtz and output == 'native-audio-atrac' and entry in ATRAC_SIMD
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
        sample_loop = entry in AUDIO_LOOP_SHUFFLES or entry in ATRAC_COPIES or entry == '0003614C'
        if sample_loop:
            local = optimize_sample_loop(entry, local)
        elif mixer_simd:
            local = mixer_simd_body(entry, local)
        elif atrac_simd and entry in ('00033DE8', '000358B0'):
            local = fuse_atrac_signed_bytes(local, 8 if entry == '00033DE8' else 16)
        elif atrac_simd and entry in ('00035030', '00034F38'):
            # Every CWD offset in these blocks is a multiple of sixteen.
            # The destination word selected by SHUFB is invariant even when
            # the second block advances its base by 128. Keep the CWD GPRs.
            base, count = ('r10', 7) if entry == '00035030' else ('r4', 8)
            selectors = re.findall(rf'(r\d+) = spu_cwd\({base}, (\d+)\);', local)
            if len(selectors) != count or any(int(offset) % 16 for _, offset in selectors):
                raise ValueError('Unexpected ATRAC word insertion')
            for selector, _ in selectors:
                pattern = rf'spu_shufb\((r\d+), (r\d+), {selector}\)'
                local, hits = re.subn(pattern, r'audio_insert_sample(\1, \2, sample_mask)', local)
                if hits != 1:
                    raise ValueError('Unexpected ATRAC insertion selector use')
            local = f'\n    const __m128i sample_mask = audio_sample_mask({base}._u32[0]);\n' + local
        elif x86_rtz and entry == '0002FCD8':
            local = pair_independent_audio_quads(local, pair_fma=True)
        # 0x35600 has a serial FP dependency chain. Its GPRs can still stay local.
        elif atrac_simd and entry != '00035600':
            local = pair_independent_audio_quads(local)
        flush = ''.join(f'        ctx->gpr[{n}] = r{n};\n' for n in sorted(written))
        flush += '#if defined(__aarch64__) && defined(BUZZ_AUDIO_COMPRESSOR_NEON)\n        buzz_arm_fp_restore(saved_fp);\n#endif\n'
        baseline_flush = flush
        transpose = mixer_simd and entry == '00004A4C'
        scoped_x86_fp = x86_rtz and not transpose and (not sample_loop or entry == '000368B8')
        if scoped_x86_fp:
            flush += '        _mm_setcsr(saved_mxcsr);\n'
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
        if transpose:
            kernel += f'    if (!audio_fft_transpose_masks(ctx)) {{ {reference}(ctx); return; }}\n'
        if scoped_x86_fp:
            # CFLTS is an integer result on SPU and never traps on special PCM
            # values. Suppress host conversion traps even for a caller with
            # unmasked exceptions; the per-exit flush restores its MXCSR.
            mask_exceptions = ' | _MM_MASK_MASK' if entry == '000368B8' else ''
            kernel += ('    const unsigned saved_mxcsr = _mm_getcsr();\n'
                       f'    _mm_setcsr((saved_mxcsr & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | 0x8040{mask_exceptions});\n')
        kernel += declarations + local + '\n}\n'
        oracle += f'void buzz_audio_{entry}_oracle(spu_context* ctx) {{' + body + '\n}\n'
        if x86_rtz:
            # Compare timing with the already-selected localized software-FP
            # kernel, not the slower original register-bank implementation.
            baseline_body = re.sub(register, lambda m: f'r{m[1]}', body)
            if tail_exits:
                for tail in tail_exits:
                    baseline_body = baseline_body.replace(tail, '{\n' + baseline_flush + tail[1:])
            else:
                baseline_body = baseline_body.replace('        SPU_RET(ctx);', baseline_flush + '        SPU_RET(ctx);')
            baseline += f'extern void {reference}(spu_context*);\n'
            baseline += f'void buzz_audio_{entry}_baseline(spu_context* ctx) {{\n'
            baseline += f'    if (SPU_LS_DEBUG_HOOKS || ctx->ls != ctx->ls_store) {{ {reference}(ctx); return; }}\n'
            baseline += declarations + baseline_body + '\n}\n'
        else:
            baseline += f'void buzz_audio_{entry}_baseline(spu_context* ctx) {{' + body + '\n}\n'
        wrapper = f'''
extern void {native}(spu_context*);
extern void buzz_audio_compressor_run(spu_context*, void (*)(spu_context*), void (*)(spu_context*), unsigned);
void {name}(spu_context* ctx) {{
    buzz_audio_compressor_run(ctx, {reference}, {native}, {int(entry,16)}u);
}}
'''
        reference_body = f'void {reference}(spu_context* ctx) {{' + body + '\n}\n'
        if mixer_simd or atrac_simd:
            # A direct hop belongs to the dispatch wrapper. The diagnostic
            # comparison must see exactly one fragment in both contexts.
            reference_body = ('#pragma push_macro("return")\n#undef return\n' +
                              reference_body + '#pragma pop_macro("return")\n')
            wrapper = wrapper.replace('\n}\n', '\n    return;\n}\n')
        source = source[:start] + reference_body + wrapper + source[end:]
    if x86_rtz and output == 'native-audio-mixer':
        for first, latch in MIXER_BUFFER_LOOPS:
            source = fuse_mixer_buffer_loop(source, prefix, first, latch)
        # Only the inner loop is pure. The caller retains its original
        # branches and nested job call after the butterfly completes.
        name = prefix + '_spu_func_00005048'
        marker = f'void {name}(spu_context* ctx) {{'
        start = source.index(marker) + len(marker)
        last = 'goto loc_00005048;'
        end = source.index(last, start) + len(last)
        loop = source[start:end]
        if 'spu_drain_call' in loop or loop.count('spu_ls_write128(') != 8:
            raise ValueError('Unexpected mixer butterfly loop')
        reference = name + '_reference'
        local = 'buzz_audio_00005048_local'
        declaration = f'extern void {local}(spu_context*);\n'
        ref_function = f'void {reference}(spu_context* ctx) {{{loop}\n}}\n'
        dispatch = f'\n    buzz_audio_compressor_run(ctx, {reference}, {local}, 0x5048u);'
        source = source[:start-len(marker)] + ref_function + declaration + marker + dispatch + source[end:]
        oracle += f'void buzz_audio_00005048_oracle(spu_context* ctx) {{{loop}\n}}\n'
        baseline += f'void buzz_audio_00005048_baseline(spu_context* ctx) {{{loop}\n}}\n'
        kernel += f'''extern void {reference}(spu_context*);
void {local}(spu_context* ctx) {{
    const unsigned rounds = ctx->gpr[22]._u32[0];
    if (!SPU_LS_DEBUG_HOOKS && ctx->ls == ctx->ls_store && rounds && rounds <= 1023) {{
        const unsigned saved = _mm_getcsr();
        _mm_setcsr((saved & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | 0x8040);
        audio_fft_butterfly_prefix(ctx, rounds);
        _mm_setcsr(saved);
    }}
    {reference}(ctx);
}}
'''
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
    parser.add_argument('--x86-rtz', action='store_true', help='Experimental checked FMA kernels with scoped MXCSR; requires -mfma -frounding-math')
    args = parser.parse_args()
    root = args.workspace
    generate(root, root / 'analysis/mstream-modules/compressor', PIN,
             'buzz_mstream_compressor', ENTRIES + (('0002FCD8',) if args.x86_rtz else ()), 'native-audio-compressor', x86_rtz=args.x86_rtz)
    if args.include_mixer:
        generate(root, root / 'port/spu/spu_0001_at_0071D300',
                 'f198ca88edd7d30c393aa95656e5cb29ced60dedf393f128388373125a882e36',
                 'spu_0001_at_0071D300', ('0000B078','0000C758','0000C8D4','00004E00','00004F24','0000513C','000051F8', *(MIXER_SIMD if args.x86_rtz else ())), 'native-audio-mixer',
                 {'0000B078':'0000B100','0000C758':'0000C7E0','0000C8D4':'0000C95C',
                  '00004E00':('00004DC8','00004EEC'),'00004F24':'00004FF0','0000513C':None,'000051F8':'00004DA8', **MIXER_SIMD}, x86_rtz=args.x86_rtz)
    if args.include_atrac:
        generate(root, root / 'analysis/mstream-modules/atrac',
                 '0b864e850022c72359df119cd5aaff459f4efc5ac6c3ad5435c54b1abaa5a78e',
                 'buzz_mstream_atrac', ('00036538','00036710', *(ATRAC_SIMD if args.x86_rtz else ())), 'native-audio-atrac',
                 {'00036538':'00036710','00036710':('000363E0','00036988','00036828'), **ATRAC_SIMD}, x86_rtz=args.x86_rtz)


if __name__ == '__main__':
    main()
