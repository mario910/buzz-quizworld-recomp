#!/usr/bin/env python3
"""Check native Image 39 bounds in complete recorded jobs, without game boot.

Requires a compiled native-bounds EXE and private Image 39 .bjc.gz captures.
Each corpus runs OFF, paired, native and volatile-register poison modes.
Input divergences are preserved and reported separately; this is not FPS.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_log(log, mode):
    summaries = re.findall(r'\[replay\] (\d+) exact, (\d+) with concurrent input changes, '
                           r'(\d+) FAILED \((\d+) transfers\)', log)
    if len(summaries) != 1 or int(summaries[0][2]):
        raise ValueError('Missing, ambiguous or failed whole-job summary')
    reports = re.findall(r'\[stagef\] img39 05D0: native (\d+), fallback (\d+), '
                         r'compared (\d+), mismatched (\d+)', log)
    if not reports:
        raise ValueError('Missing Image 39 execution counters')
    native, fallback, compared, mismatched = map(int, reports[-1])
    if mismatched or 'MISMATCH' in log or '[function-check]' in log:
        raise ValueError('Wrong output or incompatible full-register diagnostics')
    poison = re.findall(r'\[stagef\] poisoned img39 05D0: calls (\d+)', log)
    poisoned = int(poison[-1]) if poison else 0
    expected_enabled = int(mode != 'off')
    if f'[image39-bounds] enabled={expected_enabled} profile=eu100 ' not in log:
        raise ValueError('Native bounds driver missing or wrong runtime mode')
    if '[image39-fp]' in log and '[image39-fp] mode=0' not in log:
        raise ValueError('Whole-image FP must remain at the reference mode')
    expected_stagef = {'off': 1, 'paired': 2, 'native': 1, 'poison': 4}[mode]
    if f'[stagef] mode={expected_stagef} ' not in log:
        raise ValueError('Wrong Stage F mode')
    if mode == 'off' and (native or compared or poisoned or not fallback):
        raise ValueError('OFF control did not exercise the retained bounds path')
    if mode == 'paired' and (native or poisoned or not compared):
        raise ValueError('Paired mode did not compare any native bounds calls')
    if mode == 'native' and (compared or poisoned or not native):
        raise ValueError('Native mode did not execute any native bounds calls')
    if mode == 'poison' and (native or compared or fallback or not poisoned):
        raise ValueError('Poison coverage missing; rebuild with poison counters')
    return {
        'exact_jobs': int(summaries[0][0]), 'input_divergent_jobs': int(summaries[0][1]),
        'failed_jobs': int(summaries[0][2]), 'ordered_transfers': int(summaries[0][3]),
        'native': native, 'fallback': fallback, 'compared': compared, 'poisoned': poisoned,
        'input_divergences': {Path(path).name: int(count) for path, count in re.findall(
            r'\[replay\] (.+): (\d+) reads differ from the captured input', log)},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--exe-sha256', help='Expected frozen executable identity')
    parser.add_argument('--corpus', required=True, action='append', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--timeout', type=int, default=180, help='Seconds per child process')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    exe = args.exe.resolve(strict=True)
    binary_hash = sha256(exe)
    if args.exe_sha256 and binary_hash != args.exe_sha256.lower():
        parser.error('Executable differs from the frozen reference')
    map_file = exe.with_suffix('.map')
    map_hash = sha256(map_file) if map_file.is_file() else None
    corpora = []
    for path in args.corpus:
        directory = path.resolve(strict=True)
        files = sorted(directory.glob('*.bjc.gz'))
        if not files:
            parser.error('No whole-job captures in ' + str(directory))
        for file in files:
            with gzip.open(file, 'rb') as stream:
                header = stream.read(24)
            if len(header) != 24 or struct.unpack('<6I', header)[:3] != (0x434a5a42, 1, 39):
                parser.error('Not a supported Image 39 capture: ' + str(file))
            if struct.unpack('<6I', header)[5] != 262144:
                parser.error('Wrong LS size: ' + str(file))
        corpora.append((directory, {str(p): sha256(p) for p in files}))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    result = {
        'scope': 'Offline complete Image39 ordered I/O/exit replay; no game boot or FPS',
        'executable': str(exe), 'exe_sha256': binary_hash, 'map_sha256': map_hash, 'corpora': [],
    }
    for index, (corpus, bindings) in enumerate(corpora):
        case = {'directory': str(corpus), 'files': bindings, 'modes': {}}
        result['corpora'].append(case)
        for mode in ('off', 'paired', 'native', 'poison'):
            directory = output / f'{index}-{mode}'
            directory.mkdir()
            env = os.environ.copy()
            for name in ('BUZZ_FUNCTION_CHECK', 'BUZZ_REPLAY_STEPS', 'SPU_PCHIST', 'SPU_CHHIST',
                         'BUZZ_STAGEF_POISON_KEEP', 'BUZZ_STAGEF_DUMP', 'BUZZ_STAGEF_PROBE',
                         'BUZZ_STAGEF_CULLTEST', 'BUZZ_JOB_CORPUS'):
                env.pop(name, None)
            selected = dict(
                BUZZ_REPLAY_CORPUS=str(corpus), BUZZ_REPLAY_CONTRACT='1', BUZZ_REPLAY_REPEAT='1',
                BUZZ_STAGEF=str({'off': 1, 'paired': 2, 'native': 1, 'poison': 4}[mode]),
                BUZZ_STAGEF_KERNELS='8', BUZZ_IMAGE39_BOUNDS='0' if mode == 'off' else '1',
                BUZZ_IMAGE39_REQUEST='1',
                SPURS_NATIVE_IMAGE39_FP='0', BUZZ_SPU_JOB_RETURN='1', BUZZ_GEOMETRY_CHANNEL_SETUP='1',
                BUZZ_GEOMETRY_JOB='1', BUZZ_ANIMATION_JOB='1')
            env.update(selected)
            if sha256(exe) != binary_hash or any(sha256(Path(p)) != digest for p, digest in bindings.items()):
                raise ValueError('Frozen binary or corpus changed')
            with (directory/'stdout.log').open('wb') as stdout, (directory/'stderr.log').open('wb') as stderr:
                completed = subprocess.run([str(exe), 'offline-corpus-replay-only'], cwd=directory,
                    env=env, stdout=stdout, stderr=stderr, timeout=args.timeout,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            log = (directory/'stderr.log').read_text(errors='replace')
            report = check_log(log, mode)
            report.update(exit_code=completed.returncode, environment=selected)
            case['modes'][mode] = report
            (output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
            if completed.returncode or report['exact_jobs'] + report['input_divergent_jobs'] != len(bindings):
                raise ValueError('Replay process failed or did not check every captured job')
            baseline = case['modes']['off']
            for field in ('exact_jobs', 'input_divergent_jobs', 'ordered_transfers', 'input_divergences'):
                if report[field] != baseline[field]:
                    raise ValueError('Native mode changed the replay cohort: ' + field)
            if report['native'] + report['fallback'] + report['compared'] + report['poisoned'] != baseline['fallback']:
                raise ValueError('Bounds invocation coverage changed')
            print(f"PASS {corpus.name}/{mode}: {report['exact_jobs']} exact, "
                  f"{report['input_divergent_jobs']} input-divergent, "
                  f"{report['ordered_transfers']} ordered transfers", flush=True)
        if sha256(exe) != binary_hash or (map_hash and sha256(map_file) != map_hash):
            raise ValueError('Frozen binary/map changed')
        if any(sha256(Path(p)) != digest for p, digest in bindings.items()):
            raise ValueError('Frozen corpus changed')
    result['status'] = 'pass'
    (output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS complete bounds ABI replay; native gameplay and FPS remain unqualified')


if __name__ == '__main__':
    main()
