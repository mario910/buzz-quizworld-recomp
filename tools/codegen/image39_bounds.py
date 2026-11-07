"""Extract an unchanged, hash-pinned Image 39 bounds oracle for offline tests.

The generated original code and private captures stay outside Git. This tool
does not generate the native implementation or share its arithmetic recipe.
"""
import argparse
import hashlib
from pathlib import Path
import re

EU100_SHA256 = 'c8e0e95c527ac062d019f5385b27fdafba20d08306e32f4977fbdf313ea79611'


def extract_reference(path):
    raw = Path(path).read_bytes()
    if hashlib.sha256(raw).hexdigest() != EU100_SHA256:
        raise ValueError('Unreviewed image39 bounds oracle source')
    source = raw.decode('utf-8')
    names = ['buzz_job_c0bc_spu_func_000005C0', 'buzz_job_c0bc_spu_func_00000750']
    functions = []
    for name in names:
        matches = list(re.finditer(r'(?m)^void ' + name + r'\(spu_context\* ctx\) \{', source))
        if len(matches) != 1:
            raise ValueError('Missing or ambiguous bounds oracle function: ' + name)
        start = matches[0].start()
        end = source.index('\n}', matches[0].end()) + 2
        functions.append(source[start:end])
    return ('#include "spu_helpers.h"\nvoid spu_stop(spu_context*);\n'
            + '\n'.join('void ' + name + '(spu_context*);' for name in names) + '\n'
            + '\n'.join(functions) + '''
void bounds_original(spu_context* ctx) {
    const unsigned ret = ctx->gpr[0]._u32[0];
    buzz_job_c0bc_spu_func_000005C0(ctx);
    while (g_spu_trampoline_fn && ctx->pc != ret) {
        void (*next)(spu_context*) = g_spu_trampoline_fn;
        g_spu_trampoline_fn = 0;
        next(ctx);
    }
    g_spu_trampoline_fn = 0;
    ctx->pc = ret;
}
''')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.write_text(extract_reference(args.reference), encoding='utf-8', newline='\n')
