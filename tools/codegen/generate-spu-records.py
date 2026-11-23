"""Generate the EU 1.00 radix and record kernels in a private build directory."""
import argparse
import hashlib
from pathlib import Path
import re

# Each tuple identifies a reviewed SPU function and the complete original body.
BLOCKS = {
    'buzz_job_2b4b': {
        '00000198': 'e48902677f563017565d48c0cba2cb51b49417d519a2ffe9022390bcdc778218',
        '00000250': 'f9c11fb90b9fd3b3009c42d4faa33ea42861ff093b5a590193a8ce7caa1c7b4d',
        '000002D8': '61ca12ce739eace3dfc6bf068e0bfa6753393dac6e5b4af06e09b9835ff1ea68',
    },
    'buzz_job_6874': {
        '000008C8': 'ab3fbe0ab06e696feb22b6c8510fd3657e8c3c6f30edf4229944147e6c798676',
        '00000998': 'a3ad632d2df775ada5d9d5201b41772dbc4366a54f97714a7713c127ae3810fe',
        '00000B28': 'edc27f1d90c6d8b32e7893ffe200b853fac468d404b2b1a3c9427e6143448067',
    },
}
# The result of each rotated load and the original aligned register both
# remain architectural outputs. Record which original load supplies each rotation.
ROTATED = {
    '000008C8': (12, 10, 89),
    '00000B28': (51,),
}
# Record buffers in the common layout are quadword aligned. The other
# layouts execute the original body; only these proven shifts become constants.
ALIGNED = {
    '000008C8': ((80, 90), {12: 4, 89: 0}),
    '00000B28': ((7,), {51: 12}),
}
REG = r'ctx->gpr\[(\d+)\]'


def span(body, pc):
    if pc not in ROTATED and pc != '00000998':
        start = body.index('loc_' + pc + ':')
        end = body.index('\n', body.index('goto loc_' + pc, start))
        if pc == '00000250':
            end = body.index('\n', body.index('ctx->gpr[65] = spu_il(16);', end))
        return start, end
    if pc == '000008C8':
        # The preceding channel read remains at the original boundary.
        start = body.index('        ctx->gpr[15] = spu_ai')
    elif pc == '00000998':
        start = body.index('        ctx->gpr[60] = spu_il(28)')
    else:
        start = body.index('        ctx->gpr[53] = spu_il(28)')
    end = body.index('        { ctx->pc' if pc == '00000998' else '        if (', start)
    return start, end


def native(region, pc):
    if pc == '00000998':
        # The pinned prologue computes these five CWD/CWX selectors before the
        # conditional jump. With both bases aligned each inserts one fixed word.
        insertions = {19: 0, 15: 0, 18: 1, 20: 2, 21: 3}
        count = 0
        for mask, word in insertions.items():
            pattern = rf'spu_shufb\((ctx->gpr\[\d+\]), (ctx->gpr\[\d+\]), ctx->gpr\[{mask}\]\)'
            region, matches = re.subn(pattern, rf'record_insert_word(\1, \2, {word})', region)
            count += matches
        if count != 7:
            raise ValueError('Unexpected number of record word inserts')
        return region + '\n'
    regs = sorted(set(map(int, re.findall(REG, region))))
    written = sorted(set(map(int, re.findall(REG + r' = ', region))))
    local = re.sub(REG, lambda m: 'r' + m[1], region).strip('\n')
    pre = []
    if pc in ROTATED:
        for reg in ROTATED[pc]:
            pattern = rf'r{reg} = spu_ls_read128\(ctx, (.+?)\);'
            local, count = re.subn(pattern, rf'const __m128i raw{reg} = record_quad(ctx, \1);\n        r{reg} = record_vector(_mm_shuffle_epi8(raw{reg}, record_word_order()));', local)
            if count != 1:
                raise ValueError('Expected one record load')
            shift = str(ALIGNED[pc][1][reg]) if reg in ALIGNED[pc][1] else r'\1._u32[0]'
            local, count = re.subn(rf'spu_rotqby\(r{reg}, (r\d+)\)', rf'record_rotated_quad(raw{reg}, {shift})', local)
            if count != 1:
                raise ValueError('Expected one corresponding rotation')
    else:
        local = local.replace('loc_' + pc + ':', '        do {')
        local = local.replace('if (r14._u32[0] != 0) goto loc_' + pc + ';', '} while (r14._u32[0] != 0);')
        if pc == '00000250':
            # The final assignment to r65 kills this recurrence before any use.
            local = local.replace('        r65 = spu_shufb(r65, r65, r65);\n', '')
        for mask in {'00000198': (46, 47, 48, 49), '000002D8': (9,)}.get(pc, ()):
            pre.append(f'        const RadixSelector select{mask} = radix_selector(r{mask});')
            local, count = re.subn(rf'spu_shufb\((r\d+), \1, r{mask}\)', rf'radix_select(\1, select{mask})', local)
            if count != 1:
                raise ValueError('Unexpected radix digit selector')
    decl = [f'        u128 r{n} = ctx->gpr[{n}];' for n in regs]
    flush = [f'        ctx->gpr[{n}] = r{n};' for n in written]
    return '\n'.join([*decl, *pre, local, *flush, ''])


def function(src, name):
    return re.search(r'^void ' + name + r'\(spu_context\* ctx\) \{\n.*?^\}', src, re.M | re.S)


def generate(src, image):
    for pc, digest in BLOCKS[image].items():
        name = image + '_spu_func_' + pc
        m = function(src, name)
        if not m or hashlib.sha256(m[0].encode()).hexdigest() != digest:
            raise ValueError(f'Unrecognized EU 1.00 function {name}')
        a, b = span(m[0], pc)
        original = m[0][a:b]
        guard = 'ctx->ls == ctx->ls_store'
        if pc in ALIGNED:
            bases = ' | '.join(f'ctx->gpr[{n}]._u32[0]' for n in ALIGNED[pc][0])
            guard += f' && (({bases}) & 15u) == 0'
        isa = '__SSSE3__'
        if pc == '00000998':
            isa = '__SSE4_1__'
            guard += ' && ((ctx->gpr[1]._u32[0] | (ctx->gpr[10]._u32[0] + ctx->gpr[9]._u32[0])) & 15u) == 0'
        code = (f'#if defined({isa}) && !SPU_LS_DEBUG_HOOKS\n'
                f'    if ({guard}) {{\n' + native(original, pc) +
                '    } else\n#endif\n    {\n' + original + '\n    }\n')
        src = src[:m.start()+a] + code + src[m.start()+b:]
    anchor = '#include "spu_helpers.h"'
    assert src.count(anchor) == 1
    return src.replace(anchor, anchor + '\n#if defined(__SSSE3__) && !SPU_LS_DEBUG_HOOKS\n#pragma push_macro("return")\n#undef return\n#include "native_spu_records.h"\n#pragma pop_macro("return")\n#endif')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    for image in BLOCKS:
        src = args.inputs / image
        out = args.output / image
        out.mkdir(parents=True, exist_ok=True)
        for name, text in [('spu_recomp.c', generate((src/'spu_recomp.c').read_text(), image)),
                           ('spu_recomp.h', (src/'spu_recomp.h').read_text())]:
            f = out / name
            if not f.exists() or f.read_text() != text:
                f.write_text(text)


if __name__ == '__main__':
    main()
