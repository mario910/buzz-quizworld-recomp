"""Pinned EU 1.00 descriptor-request hook and standalone retained oracle."""
import argparse
import hashlib
from pathlib import Path
import re

EU100_SHA256 = 'c8e0e95c527ac062d019f5385b27fdafba20d08306e32f4977fbdf313ea79611'
PREFIX = 'buzz_job_c0bc_spu_func_0000'
REQUEST_PCS = ('0A90', '0AD0', '0B30', '0B40', '3A10', '3AB0', '3B40', '3B70')


def wrap_request(source, original):
    if hashlib.sha256(original).hexdigest() != EU100_SHA256:
        return source  # Other reviewed arithmetic profiles keep their full lift.
    name = PREFIX + '3A10'
    entry = f'void {name}(spu_context* ctx) {{'
    if source.count(entry) != 1:
        raise ValueError('Expected one retained EU100 descriptor-request entry')
    wrapper = f'''#define BUZZ_IMAGE39_REQUEST_PROFILE 100
#include "native_image39_request.h"
static void {name}_reference(spu_context* ctx);
void {name}(spu_context* ctx) {{
    if (!buzz_image39_native_request(ctx)) {name}_reference(ctx);
}}
static void {name}_reference(spu_context* ctx) {{'''
    return source.replace(entry, wrapper)


def oracle(original):
    if hashlib.sha256(original).hexdigest() != EU100_SHA256:
        raise ValueError('Unreviewed image39 descriptor-request oracle')
    source = original.decode('utf-8').replace('\r\n', '\n')
    declarations, bodies = [], []
    for pc in REQUEST_PCS:
        name = PREFIX + pc
        start = re.search(r'^void ' + name + r'\(spu_context\* ctx\) \{', source, re.M)
        if not start:
            raise ValueError('Missing retained request block ' + pc)
        body = source[start.end():source.index('\n}', start.end())]
        if 'SPU_RET(ctx);' in body:
            body = body[:body.index('SPU_RET(ctx);') + len('SPU_RET(ctx);')]
        declarations.append(f'static void {name}(spu_context* ctx);\n')
        bodies.append(f'static void {name}(spu_context* ctx) {{' + body + '\n}\n')
    return ''.join(declarations + bodies)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(oracle(args.reference.read_bytes()), encoding='utf-8')
