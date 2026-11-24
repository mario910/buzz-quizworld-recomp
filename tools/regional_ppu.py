"""Bind native Quiz World PPU kernels to reviewed instruction streams across disc layouts."""
from collections import defaultdict
from hashlib import sha256
from pathlib import Path
import json
import re
import runpy

from tools.studio_adapter import ANCHOR, studio_targets

ROOT = Path(__file__).resolve().parents[1]
kernels = runpy.run_path(str(ROOT / 'tools/codegen/generate-ppu-kernels.py'))
FUNCTION = re.compile(r'^void func_([0-9A-F]{8})\(ppu_context\* ctx\) \{\n.*?^\}', re.M | re.S)
NOP = '        /* nop */;\n'
LABEL = re.compile(r'^loc_([0-9A-F]{8}):', re.M)
TOC = re.compile(r'(vm_read32\(ctx->gpr\[2\] \+ )-(0x[0-9A-F]+)\)')
CALL = re.compile(r'ctx->lr = 0x([0-9A-F]{8}); func_([0-9A-F]{8})\(ctx\)')
# Ordinals in the reviewed control flow. Branches into a native span still
# reach the original lifted continuation, including external loop backedges.
LABEL_KEYS = {
    '002B7054': 3, '00283128': 2, '00288A48': 11, '002B6E48': 0,
    '002DDAA0': 0, '002DDBB8': 7, '00288160': 0, '002C02F0': 3,
    '002C0404': 8, '002C0384': 5, '002DD3B0': 0, '002DD7E4': 19,
}


def signature(body):
    labels = {pc: str(i) for i, pc in enumerate(LABEL.findall(body))}
    callees = {}
    body = re.sub(r'loc_([0-9A-F]{8})', lambda m: 'label_' + labels.get(m[1], 'external_' + m[1]), body)
    body = re.sub(r'func_[0-9A-F]{8}', lambda m: 'function_' + str(callees.setdefault(m[0], len(callees))), body)
    # A common displacement of the TOC is a link-time change. Preserve the
    # distance between every pair of slots and bind the actual base at emission.
    first_toc = TOC.search(body)
    if first_toc:
        origin = int(first_toc[2], 16)
        body = TOC.sub(lambda m: m[1] + f'toc[{origin - int(m[2], 16)}])', body)
    # Every remaining immediate except a guest return address is significant.
    body = re.sub(r'ctx->lr = 0x[0-9A-F]{8};', 'ctx->lr = return_site;', body)
    return sha256(body.replace(NOP, '').encode('utf-8', errors='surrogateescape')).hexdigest()


def bind_body(body, role, updated, searches):
    k = kernels
    if role == k['COMMAND']:
        loop, *targets = studio_targets(body, all_commands=True)
        start = body.index('\n', body.index(f'loc_{loop:08X}:')) + 1
        end = body.index('        switch ((uint32_t)ctx->ctr)', start)
        args = ', '.join(hex(pc) for pc in targets)
        return body[:start] + f'        buzz::ppu_scene_command(*ctx, {{{args}}});\n' + body[end:]
    if role in k['PERMUTE_COUNTS']:
        return k['adapt_permutations'](body, role)
    labels = LABEL.findall(body)
    calls = CALL.findall(body)
    toc = [offset for _, offset in TOC.findall(body)]
    if role == k['COLLECT'] and calls[0][1] not in searches:
        return body

    def bind(fragment):
        fragment = re.sub(r'loc_([0-9A-F]{8})', lambda m: 'loc_' + labels[LABEL_KEYS[m[1]]], fragment)
        if role == k['COLLECT']:
            return fragment.replace('objects(*ctx)', f'objects(*ctx, 0x{calls[0][0]})')
        if role == k['SCENE_JOB']:
            fragment = fragment.replace('0x00288B30;', '0x' + calls[5][0] + ';')
            late = 'true' if updated else 'false'
            return fragment.replace('scene_job(*ctx)', f'scene_job(*ctx, {toc[5]}, {late})')
        if role == k['STREAM']:
            if updated:
                fragment = fragment.replace('gpr[5] = ppc_rlw64(ctx->gpr[27]', 'gpr[12] = ppc_rlw64(ctx->gpr[26]')
            schedule = 'true' if updated else 'false'
            return fragment.replace('ppu_pack_stream_job(*ctx)', f'ppu_pack_stream_job<{schedule}>(*ctx, {toc[0]})')
        if role == k['OBJECT']:
            return fragment.replace('transform(*ctx)', f'transform(*ctx, {toc[2]})')
        return fragment

    if role == k['WEIGHTED'] and updated:
        # Seven consecutive rows in the update share a bias register. The
        # first store in the function precedes setup of the remaining weights.
        anchor = '(ctx->gpr[9]) & ~0xFULL; vm_raw_st16'
        start = body.index('\n', body.index(anchor)) + 1
        end = body.index('        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xD0);', start)
        return body[:start] + k['guarded_simd_block'](body[start:end], 'ppu_accumulate_weighted_rows<true>') + body[end:]
    return k['adapt_existing'](body, role, bind)


def generate_regional(source_dir, output, completion=None, ppu_columns="none"):
    columns = kernels['COLUMN_GROUPS'][ppu_columns]
    optional = (kernels['BASIS_SETUP'], kernels['BASIS_TAIL'], kernels['ROTATION'])
    profiles = json.loads(Path(__file__).with_name('regional_ppu_signatures.json').read_text())
    profiles = {role: shapes for role, shapes in profiles.items() if role not in optional or role in columns}
    lookup = {digest: (role, updated) for role, shapes in profiles.items()
              for lines, digest, updated in shapes}
    lengths = {lines for shapes in profiles.values() for lines, _, _ in shapes}
    matches = defaultdict(list)
    searches = set()
    for original in sorted(source_dir.glob('ppu_recomp_*.cpp')):
        source = completion if completion and completion.name == original.name else original
        for match in FUNCTION.finditer(source.read_text(encoding='utf-8', errors='surrogateescape')):
            body = match[0]
            if ANCHOR in body and studio_targets(body):
                role, updated = kernels['COMMAND'], False
            elif body.count('\n') - body.count(NOP) in lengths:
                shape = lookup.get(signature(body))
                if not shape:
                    continue
                role, updated = shape
            else:
                continue
            if role == kernels['SEARCH']:
                searches.add(match[1])
            matches[source].append((match.start(), match.end(), match[1], role, updated))
    output.mkdir(parents=True, exist_ok=True)
    result = []
    mapped = defaultdict(list)
    for source, sites in matches.items():
        text = source.read_text(encoding='utf-8', errors='surrogateescape')
        changed = False
        for start, end, pc, role, updated in reversed(sites):
            original = text[start:end]
            replacement = original if role in optional else bind_body(original, role, updated, searches)
            if role in columns:
                for lo, hi, kernel in reversed(list(kernels['column_spans'](replacement, role))):
                    block = kernels['guarded_simd_block'](replacement[lo:hi], kernel)
                    replacement = replacement[:lo] + block + replacement[hi:]
            if replacement != original:
                changed = True
                mapped[role].append(pc)
                print(f'Native PPU {role} -> {pc}')
                text = text[:start] + replacement + text[end:]
        if not changed:
            continue
        anchor = '#include "ppu_vmx.h"'
        if text.count(anchor) != 1:
            raise ValueError(f'Unexpected lifted preamble in {source}')
        includes = ('#include "native_ppu_records.h"\n#include "native_ppu_jobs.h"\n#include "native_ppu_scene.h"\n'
                    f'#if {kernels["MATRIX_ISA"]}\n#include "native_ppu_transform.h"\n#include "native_ppu_permute.h"\n#include "native_ppu_columns.h"\n#endif')
        text = text.replace(anchor, anchor + '\n' + includes)
        dest = output / source.name
        if not dest.exists() or dest.read_text(encoding='utf-8', errors='surrogateescape') != text:
            dest.write_text(text, encoding='utf-8', errors='surrogateescape')
        result.append(dest)
    expected = set(profiles) | {kernels['COMMAND']}
    missing = sorted(expected - mapped.keys())
    report = {"matched": {r: sorted(pcs) for r, pcs in sorted(mapped.items())}, "unmatched": missing}
    (output / 'mapping.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Matched {len(mapped)}/{len(expected)} native PPU kernels')
    if missing:
        print('Using lifted PPU code for unmatched kernels: ' + ', '.join(missing))
    return result, kernels['COMMAND'] in mapped
