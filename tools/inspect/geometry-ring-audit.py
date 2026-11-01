"""Audit the pinned image-41 allocator graph and an original-only replay.

Reports stay private. Original coverage is not native acceptance. This narrow
reader understands this pinned lift, including its unconditional indirect exits;
it does not infer that the C text following such an exit can execute.
"""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = '5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
PREFIX = 'buzz_job_6eba_spu_func_'
REPORTERS = {0x298, 0xcc70}
BLOCKS = {
    0x37c0, 0x3800, 0x38d0, 0x3930, 0x3940, 0x3948, 0x39e8,
    0x3a18, 0x3a24, 0x3a98, 0x3aec, 0x3afc, 0x3b18, 0x3b28,
    0x3b58, 0x3b88, 0x3ba0, 0x3bdc, 0x3c78, 0x3cc4, 0x3cd0,
    0x3ce8, 0x3cec, 0x3cf8, 0x3d00, 0x3d18, 0x3d34, 0x3d54,
    0x3dd0, 0x3e04, 0x3e68, 0x3f10, 0x3f40, 0x3fa4, 0x400c,
    0x4064, 0x4078, 0x40ac, 0x40b4,
}
COMPUTED = {0x3940: 0x3948, 0x3cec: 0x3cf4}
TRACE_PINS = {
    'port/geometry_trace.h': '6b9cef66d42817354c766a35e37f9683d65c03a70180d1086eb4d1b18f8f5a0c',
    'tools/codegen/generate-geometry-trace.py': '580f9441a7f74d1858fc87a972fad2ea494cde13626a61f77e225190e05c0f93',
    'analysis/native-geometry/trace-game.exe': 'ab3881e6ca1cad094fc308932791b5567d8a17a370c43aa02e952d5f6e3b3704',
}


def reachable_text(body):
    lines = []
    for line in body.splitlines():
        lines.append(line)
        # All conditional exits in this source start with "if". Drained calls
        # do not return; labels and their intra-block gotos must also survive.
        if not line.lstrip().startswith('if ') and (
                line.strip() == 'SPU_RET(ctx);' or
                re.search(r'g_spu_trampoline_fn = \w+; return;\s*(?:\})?\s*$', line)):
            break
    return '\n'.join(lines)


def source_graph(raw):
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise ValueError('Unreviewed ring lift')
    source = raw.decode()
    functions = {int(pc, 16): body for pc, body in re.findall(
        r'(?ms)^void ' + PREFIX + r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}', source)}
    # The two helper links have preferred word only; AI still changes all four
    # words. Do not substitute the unreachable lexical continuation for 3CF4.
    for predecessor, link in ((0x3930, 0x3940), (0x3ce8, 0x3cec)):
        if f'ctx->gpr[2] = spu_link(0x{link:X})' not in functions[predecessor]:
            raise ValueError('Computed allocator link changed')
    for pc, target in COMPUTED.items():
        body = functions[pc]
        if ('ctx->gpr[2] = spu_ai(ctx->gpr[2], 8);' not in body or
                f'ctx->int_enable = {int(pc == 0x3cec)};' not in body or
                'ctx->pc = ctx->gpr[2]._u32[0];' not in body):
            raise ValueError('Computed allocator branch changed')
        if target != pc + 8:
            raise ValueError('Computed target model changed')
    registered = {int(pc, 16) for pc in re.findall(r'\{ 0x([0-9A-F]+)u, ' + PREFIX, source)}
    if 0x3948 not in registered or 0x3cf4 in registered or 0x3cf4 in functions:
        raise ValueError('Allocator indirect registry boundary changed')
    seen, todo, edges, dead, io, loops = set(), [0x37c0], {}, {}, {}, {}
    while todo:
        pc = todo.pop()
        if pc in seen or pc in REPORTERS:
            continue
        if pc not in functions:
            raise ValueError(f'Unknown ring target {pc:X}')
        body = functions[pc]
        live = reachable_text(body)
        targets = {int(x, 16) for x in re.findall(PREFIX + r'([0-9A-F]+)', live)}
        if pc in COMPUTED and COMPUTED[pc] in functions:
            targets.add(COMPUTED[pc])
        local = {int(x, 16) for x in re.findall(r'\bgoto loc_([0-9A-F]+);', live)}
        if local:
            if local != {pc}:
                raise ValueError('Unknown allocator intra-block loop')
            loops[f'{pc:X}'] = [f'{x:X}' for x in sorted(local)]
        seen.add(pc)
        todo.extend(targets - seen)
        edges[f'{pc:X}'] = [f'{x:X}' for x in sorted(targets)]
        removed = {int(x, 16) for x in re.findall(PREFIX + r'([0-9A-F]+)', body)} - targets
        if removed:
            dead[f'{pc:X}'] = [f'{x:X}' for x in sorted(removed)]
        operations = re.findall(r'\bspu_(wrch|rdch|rchcnt)\(ctx, (\w+)', live)
        if operations:
            io[f'{pc:X}'] = [f'{kind}:{channel}' for kind, channel in operations]
    if seen != BLOCKS:
        raise ValueError('Allocator graph changed')
    if '3B28' in edges['3CEC'] or dead.get('3CEC') != ['3B28']:
        raise ValueError('Interrupt helper dead tail incorrectly retained')
    return dict(source_sha256=PIN, blocks=len(seen), edges=edges,
                dead_lexical_targets=dead, channel_sites=io, intra_block_loops=loops,
                computed_targets={'3940': '3948 (registered)', '3CEC': '3CF4 (not registered)'},
                reporting_frontiers={'3DD0': ['CC70', '298'], '4078': ['CC70', '298']},
                unresolved_boundary='3CF4 requires resolver/interpreter audit before native acceptance')


def original_trace(directory):
    def rows(name):
        with (directory / name).open(newline='') as stream:
            return list(csv.DictReader(stream))
    entries = {int(r['pc'], 16): int(r['entries']) for r in rows('all.entries.csv')}
    edges = {(int(r['source'], 16), int(r['target'], 16)): int(r['count'])
             for r in rows('all.edges.csv')}
    n = entries.get(0x37c0, 0)
    if entries.get(0x10) != 19508 or n != 7201 or entries.get(0x38d0) != n:
        raise ValueError('Original replay/root coverage changed')
    for edge in ((0x3800, 0x3930), (0x3940, 0x3948), (0x3b18, 0x3b28)):
        if edges.get(edge) != n:
            raise ValueError('Observed allocator route changed')
    commands = [r for r in rows('all.commands.csv') if int(r['pc'], 16) in BLOCKS]
    dma = [r for r in rows('all.dma.csv') if int(r['pc'], 16) in BLOCKS]
    if commands != dma:
        raise ValueError('Allocator command/transfer shape or order differs')
    shapes = collections.Counter((int(r['pc'], 16), int(r['cmd'], 16), int(r['size'])) for r in dma)
    if shapes != {(0x3948, 0xd0, 128): n, (0x3afc, 0xb0, 128): n, (0x3e04, 0x40, 4): 20}:
        raise ValueError('Observed allocator I/O sites changed')
    if entries.get(0x3e04) != 20 or entries.get(0x3cec, 0):
        raise ValueError('Refresh/interrupt restore coverage changed')
    # Each allocator instance has one GETLLAR before its matching PUTLLUC;
    # intervening GPU reads belong to that same line, not another reservation.
    active, completed = None, 0
    for row in dma:
        pc = int(row['pc'], 16)
        if pc == 0x3948:
            if active is not None:
                raise ValueError('Overlapping allocator reservations in serial trace')
            active = row
        elif pc == 0x3afc:
            if active is None or any(row[k] != active[k] for k in ('job', 'lsa', 'ea', 'size')):
                raise ValueError('Allocator publication does not match reservation')
            active, completed = None, completed + 1
        elif active is None or row['job'] != active['job']:
            raise ValueError('GPU refresh outside allocator reservation')
    if active is not None or completed != n:
        raise ValueError('Incomplete allocator reservation')
    if '[replay] 19508 exact, 0 with concurrent input changes, 0 FAILED (58195 transfers)' not in (directory / 'replay.log').read_text():
        raise ValueError('Original whole-job replay did not pass')
    return dict(jobs=19508, whole_job_transfers=58195, allocator_calls=n,
                allocator_transfers=len(dma), gpu_cursor_refreshes=20,
                paired_lane_reservations=completed,
                entries={f'{pc:X}': entries.get(pc, 0) for pc in sorted(BLOCKS)},
                cold_blocks=[f'{pc:X}' for pc in sorted(BLOCKS) if not entries.get(pc, 0)],
                native_acceptance=False,
                limitation='Original block/transfer trace only; no complete architectural snapshots, native checks, live calls or speed claim')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Choose a fresh private verdict path')
    if not args.output.resolve().is_relative_to(ROOT / 'analysis'):
        raise ValueError('Keep game-derived reports in private analysis/')
    for name, pin in TRACE_PINS.items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != pin:
            raise ValueError(f'Unreviewed original tracer dependency: {name}')
    result = source_graph((ROOT / 'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes())
    result['original_trace'] = original_trace(args.directory)
    paths = [args.directory / name for name in ('all.entries.csv', 'all.edges.csv', 'all.commands.csv', 'all.dma.csv', 'replay.log')]
    paths += [Path(__file__), ROOT / 'port/geometry_trace.h', ROOT / 'tools/codegen/generate-geometry-trace.py', ROOT / 'analysis/native-geometry/trace-game.exe']
    result['bindings'] = {str(p.resolve().relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS ring audit:39 blocks;19508 original exact jobs;7201 allocator calls;14422 ordered ring transfers;20 refreshes;native gates OPEN')


if __name__ == '__main__':
    main()
