#!/usr/bin/env python3
"""Offline Linux/WSL checks; ARM64 uses QEMU for correctness, never FPS.

Run from a prepared workspace. --engine-source is buzz_rsx_draw_engine.c
emitted by an actual CMake build, so this tests the production adapter.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arm64', action='store_true')
    parser.add_argument('--bench', action='store_true')
    parser.add_argument('--engine-source', required=True, type=Path)
    parser.add_argument('--corpus', type=Path, help='Complete private image41 capture directory')
    parser.add_argument('--remap-corpus', type=Path, help='Private stream of 10000 RSX remap inputs')
    parser.add_argument('--zlib', type=Path, help='ARM64 Linux libz.a, required with --arm64 --corpus')
    parser.add_argument('--image39-request-corpus', type=Path, help='Private B39REQ01 descriptor-request captures')
    parser.add_argument('--image39-source', type=Path, help='Original pinned EU100 C0BC translation unit')
    args = parser.parse_args()
    if bool(args.image39_request_corpus) != bool(args.image39_source):
        parser.error('--image39-request-corpus and --image39-source must be supplied together')
    if args.arm64 and args.bench:
        parser.error('QEMU timing is not a hardware benchmark')
    if args.arm64 and args.corpus and not args.zlib:
        parser.error('--arm64 --corpus requires --zlib pointing to an ARM64 Linux libz.a')
    root = Path(__file__).resolve().parents[2]
    engine = args.engine_source.resolve()
    if not engine.is_file() or any(name not in engine.read_text() for name in
                                  ('buzz_vp_analyze_cached', 'buzz_renderer_fp_hash')):
        parser.error('--engine-source must be the CMake-generated cached renderer')
    prefix = 'aarch64-linux-gnu-' if args.arm64 else ''
    runner = ['qemu-aarch64', '-L', '/usr/aarch64-linux-gnu'] if args.arm64 else []
    includes = ['-Ips3recomp/include', '-Ips3recomp/runtime/spu', '-Ips3recomp/libs/video', '-Iport']

    def run(command):
        subprocess.run([str(x) for x in command], cwd=root, check=True)

    with tempfile.TemporaryDirectory(prefix='buzz-offline-cpu-') as temp:
        out = Path(temp)
        run([prefix+'gcc', '-O2', '-std=gnu17', '-ffunction-sections', '-fdata-sections', *includes,
             'tests/renderer-texture-snapshot-test.c', 'ps3recomp/libs/video/rsx_dispatch.c',
             '-Wl,--gc-sections', '-lm', '-pthread', '-o', out/'texture-snapshot'])
        run([*runner, out/'texture-snapshot'])
        run([prefix+'gcc', '-O2', *includes, '-c', 'ps3recomp/libs/video/rsx_vp_decompiler.c', '-o', out/'vp.o'])
        run([prefix+'g++', '-O2', '-std=gnu++17', *includes, 'tests/vertex-program-cache-test.cpp', out/'vp.o', '-o', out/'cache'])
        run([*runner, out/'cache', *(['--bench'] if args.bench else [])])
        run([prefix+'gcc', '-O2', '-ffunction-sections', '-fdata-sections', *includes,
             '-c', 'ps3recomp/libs/video/rsx_fp_decompiler.c', '-o', out/'fp.o'])
        for refinements in ('0', '1'):
            run([prefix+'g++', '-O2', '-std=gnu++17', *includes,
                 '-DBUZZ_RENDERER_CPU_REFINEMENTS='+refinements, 'tests/fragment-program-cache-test.cpp',
                 out/'fp.o', '-Wl,--gc-sections', '-lm', '-o', out/'fragment-cache'])
            run([*runner, out/'fragment-cache'])
        run([prefix+'g++', '-O2', '-std=gnu++17', *includes, 'tests/renderer-program-data-test.cpp',
             out/'fp.o', '-Wl,--gc-sections', '-lm', '-o', out/'program-data'])
        run([*runner, out/'program-data'])
        fp = ['-frounding-math', '-ffp-contract=off', '-D_Thread_local=thread_local']
        if not args.arm64:
            fp += ['-mavx2', '-mfma', '-DSPU_NATIVE_RTZ=1', '-DSPU_RTZ_RESULT_GUARD=1']
        run([prefix+'g++', '-O2', '-std=gnu++17', *fp, *includes,
             'tests/stagef-image39-return-test.cpp', '-o', out/'stagef-return'])
        run([*runner, out/'stagef-return'])
        if args.image39_request_corpus:
            run([sys.executable, 'tools/codegen/image39_request.py', '--reference', args.image39_source.resolve(),
                 '--output', out/'image39_request_oracle.inc'])
            run([prefix+'g++', '-O2', '-std=gnu++17', *fp, *includes, '-I'+str(out),
                 'tests/image39-descriptor-request-test.cpp', '-o', out/'image39-request'])
            run([*runner, out/'image39-request', args.image39_request_corpus.resolve()])
        for source, name in [('geometry-bounded-position-test.cpp', 'position'),
                             ('geometry-object-directions-test.cpp', 'directions'),
                             ('geometry-host-streams-test.cpp', 'streams'),
                             ('geometry-deferred-clip-test.cpp', 'deferred-clip'),
                             ('geometry-reciprocal-estimate-test.cpp', 'reciprocal')]:
            run([prefix+'g++', '-O2', '-std=gnu++17', *fp, *includes, 'tests/'+source, '-o', out/name])
            run([*runner, out/name, *(['--bench'] if args.bench and name == 'position' else [])])
        sources = ['tests/test_rsx_draw_engine.c', 'rsx_dispatch.c', 'rsx_vertex_compact.c',
                   'rsx_texture_layout.c', 'rsx_vp_decompiler.c', 'rsx_fp_decompiler.c', 'rsx_texture_pack.c']
        for release in ([], ['-DNDEBUG']):
            run([prefix+'gcc', '-O2', '-std=gnu17', *release, *includes,
                 'tests/renderer-state-packets-test.c', 'ps3recomp/libs/video/rsx_dispatch.c',
                 'ps3recomp/libs/video/rsx_commands.c', '-o', out/'state-packets'])
            run([*runner, out/'state-packets'])
        run([prefix+'gcc', '-O2', '-std=gnu17', '-ffunction-sections', '-fdata-sections', *includes,
             'tests/renderer-vertex-remap-test.c', 'ps3recomp/libs/video/rsx_vertex_compact.c',
             'ps3recomp/libs/video/rsx_dispatch.c', '-Wl,--gc-sections', '-lm', '-o', out/'remap'])
        run([*runner, out/'remap', *(['--bench'] if args.bench else [])])
        run([prefix+'gcc', '-O2', '-ffunction-sections', '-fdata-sections', *includes,
             'tests/renderer-index-fusion-test.c', 'ps3recomp/libs/video/rsx_vertex_compact.c',
             'ps3recomp/libs/video/rsx_dispatch.c', '-Wl,--gc-sections', '-lm', '-o', out/'index-fusion'])
        run([*runner, out/'index-fusion'])
        run([prefix+'gcc', '-O2', '-ffunction-sections', '-fdata-sections', *includes,
             'tests/renderer-index-packed-fusion-test.c', 'ps3recomp/libs/video/rsx_vertex_compact.c',
             'ps3recomp/libs/video/rsx_dispatch.c', '-Wl,--gc-sections', '-lm', '-o', out/'index-packed-fusion'])
        run([*runner, out/'index-packed-fusion'])
        run([prefix+'gcc', '-O2', '-std=gnu17', '-ffunction-sections', '-fdata-sections', *includes,
             'tests/renderer-vertex-plan-test.c', 'ps3recomp/libs/video/rsx_vertex_compact.c',
             'ps3recomp/libs/video/rsx_dispatch.c', '-Wl,--gc-sections', '-lm', '-o', out/'vertex-plan'])
        run([*runner, out/'vertex-plan', *(['--bench'] if args.bench else [])])
        run([prefix+'gcc', '-O2', '-std=gnu17', '-frounding-math', '-ffp-contract=off', *includes,
             'tests/renderer-vertex-fetch-test.c', 'ps3recomp/libs/video/rsx_vertex_compact.c',
             'ps3recomp/libs/video/rsx_dispatch.c', '-lm', '-o', out/'vertex-fetch'])
        run([*runner, out/'vertex-fetch', *(['--bench'] if args.bench else [])])
        if args.remap_corpus:
            run([prefix+'gcc', '-O2', '-std=gnu17', '-ffunction-sections', '-fdata-sections', *includes,
                 'tests/renderer-remap-corpus.c', 'ps3recomp/libs/video/rsx_vertex_compact.c',
                 'ps3recomp/libs/video/rsx_dispatch.c', '-Wl,--gc-sections', '-lm', '-o', out/'remap-corpus'])
            run([*runner, out/'remap-corpus', args.remap_corpus.resolve(), *([] if args.bench else ['--check-only'])])
        run([prefix+'gcc', '-O2', '-std=gnu17', '-ffunction-sections', '-fdata-sections', *includes,
             engine, *['ps3recomp/libs/video/'+x for x in sources], 'ps3recomp/libs/system/host_controls.c',
             '-Wl,--gc-sections', '-lm', '-pthread', '-o', out/'renderer'])
        for enabled in ('0', '1'):
            for remap in ('0', '1'):
                run(['env', 'PS3RECOMP_ENG_VERTEX_BULK=1', 'PS3RECOMP_ENG_PROGRAM_PAIR_ONCE=1',
                     'BUZZ_VERTEX_PLAN_ACTIVE_ONLY='+remap,
                     'BUZZ_VERTEX_REMAP_SIMD='+remap, 'BUZZ_FP_HASH_CACHE='+enabled,
                     'BUZZ_VERTEX_FETCH=1', 'BUZZ_TEXTURE_SNAPSHOT=1', *runner, out/'renderer'])
        for label, source in [('reference', root/'ps3recomp/libs/video/rsx_draw_engine.c'), ('optimized', engine)]:
            run([prefix+'gcc', '-O2', '-std=gnu17', '-ffunction-sections', '-fdata-sections', *includes,
                 source, 'tests/renderer-program-trace.c',
                 *['ps3recomp/libs/video/'+x for x in sources[1:]], 'ps3recomp/libs/system/host_controls.c',
                 '-Wl,--gc-sections', '-lm', '-pthread', '-o', out/label])
            for enabled in (('1',) if label == 'reference' else ('0', '1')):
                for remap in (('0',) if label == 'reference' else ('0', '1')):
                    for fusion in (('0',) if label == 'reference' else ('0', '1')):
                        run(['env', 'PS3RECOMP_ENG_VERTEX_BULK=1', 'PS3RECOMP_ENG_PROGRAM_PAIR_ONCE=1',
                             'BUZZ_VERTEX_PLAN_ACTIVE_ONLY='+remap, 'BUZZ_INDEX_FUSION='+fusion,
                             'BUZZ_VERTEX_REMAP_SIMD='+remap, 'BUZZ_FP_HASH_CACHE='+enabled,
                             'BUZZ_VERTEX_FETCH=1', 'BUZZ_TEXTURE_SNAPSHOT=1', *runner,
                             out/label, out/(label+enabled+remap+fusion+'.trace')])
        if any((out/'reference100.trace').read_bytes() != (out/('optimized'+enabled+remap+fusion+'.trace')).read_bytes()
               for enabled in ('0', '1') for remap in ('0', '1') for fusion in ('0', '1')):
            raise RuntimeError('Renderer output differs from the unmodified SDK')
        print('PASS renderer differential trace: shader sources, layouts, constants, vertices and texture bindings')
        if args.arm64:
            run([prefix+'g++', '-O2', 'tests/geometry-neon-vectors-test.cpp', '-o', out/'vectors'])
            run([*runner, out/'vectors'])
        if args.corpus:
            zlib = [args.zlib.resolve()] if args.arm64 else ['-lz']
            for source, name in [('geometry-cull-rejection-test.cpp', 'cull-rejection'),
                                 ('geometry-coarse-clip-test.cpp', 'coarse-clip'),
                                 ('geometry-host-vertex-job-test.cpp', 'composed-geometry'),
                                 ('geometry-zero-vertex-padding-test.cpp', 'zero-vertex-padding'),
                                 ('geometry-zero-padding-mode-test.cpp', 'zero-padding-mode'),
                                 ('geometry-lazy-attributes-test.cpp', 'lazy-attributes'),
                                 ('geometry-index-cache-test.cpp', 'index-cache')]:
                run([prefix+'g++', '-O2', '-std=gnu++17', *fp, *includes, '-Ithird_party/zlib',
                     'tests/'+source, *zlib, '-o', out/name])
                run([*runner, out/name, args.corpus.resolve()])
            if args.bench:
                run([prefix+'g++', '-O2', '-std=gnu++17', *fp, *includes, '-Ithird_party/zlib',
                     'tests/geometry-projection-cull-bench.cpp', *zlib, '-o', out/'captured-bench'])
                run([out/'captured-bench', args.corpus.resolve()])


if __name__ == '__main__':
    main()
