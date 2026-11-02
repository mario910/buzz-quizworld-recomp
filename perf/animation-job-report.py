"""Validate one same-EXE image-40 whole-job 0/1/1/0 FPS or CPU series."""
import argparse
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import statistics


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


frames = load('animation_frames', 'geometry-ab-report.py')
work = load('animation_work', 'measure-process-work.py')
readback = load('animation_readback', 'frame-readback.py')
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()


def comparison(values):
    if any(value <= 0 for value in values):
        raise ValueError('Nonpositive measurement')
    baseline = statistics.mean(values[i] for i in (0, 3))
    native = statistics.mean(values[i] for i in (1, 2))
    return dict(baseline=baseline, native=native, change_percent=100*(native/baseline-1),
                pair_changes_percent=[100*(values[b]/values[a]-1) for a, b in ((0, 1), (3, 2))])


def report(progress, directory, qa_path):
    events = [json.loads(line) for line in progress.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    if len(events) != 8 or [event['state'] for event in events] != ['start', 'done']*4:
        raise ValueError('Need four complete sequential runs')
    runs = [event['run'] for event in events[1::2]]
    if runs != [event['run'] for event in events[::2]] or [run['arm'] for run in runs] != [0, 1, 1, 0] or [run['index'] for run in runs] != [1, 2, 3, 4]:
        raise ValueError('Changed metadata or invalid reversed order')
    common = ('scene', 'metric', 'renderer', 'window_seconds', 'exe_sha256', 'map_sha256',
              'manifest_sha256', 'save_sha256', 'source_pins', 'round_intro_seconds',
              'content_capture_ms', 'content_shot_at', 'cpu_binding_sha256', 'geometry_job')
    for field in common:
        if any(run[field] != runs[0][field] for run in runs):
            raise ValueError('Series differs in '+field)
    scene, metric = runs[0]['scene'], runs[0]['metric']
    if scene not in ('Menu', 'Round') or metric not in ('Fps', 'Cpu') or runs[0]['renderer'] != 'vulkan' or runs[0]['geometry_job'] != 1:
        raise ValueError('Wrong scene, metric, renderer or geometry baseline')
    window = {'Menu': [40, 98], 'Round': [200, 270]}[scene]
    if runs[0]['window_seconds'] != window or bool(runs[0]['cpu_binding_sha256']) != (metric == 'Cpu'):
        raise ValueError('Wrong window or CPU attachment')
    expected = dict(BUZZ_ANIMATION_JOB=0, BUZZ_GEOMETRY_JOB=1, BUZZ_GEOMETRY_JOB_HOST_PIPELINE=1,
                    BUZZ_GEOMETRY_JOB_PUBLICATION=1, BUZZ_GEOMETRY_JOB_COMMANDS=1,
                    BUZZ_GEOMETRY_JOB_LEGACY=0, BUZZ_GEOMETRY_JOB_AUDIT=0,
                    BUZZ_GEOMETRY_DEBUG_STAGES=0, BUZZ_GEOMETRY=1, BUZZ_GEOMETRY_KERNELS=827,
                    BUZZ_STAGEF=1, BUZZ_IMAGE40_LOCALS=0, SPU_INTERP_EXACT=0,
                    SPU_INTERP_STATS=0, SPU_INTERP_ENTRIES=0)
    qa = json.loads(qa_path.read_text()) if qa_path else {}
    summaries, methods = [], set()
    for event, run in zip(events[1::2], runs):
        tag, arm = run['tag'], run['arm']
        expected['BUZZ_ANIMATION_JOB'] = arm
        if run['settings'] != expected or run['animation_job'] != arm:
            raise ValueError('Runtime settings differ')
        csv_path, log = directory/f'vk-scene-{tag}.csv', directory/f'vk-scene-{tag}.stderr.log'
        if sha(csv_path) != event['csv_sha256'] or sha(log) != event['log_sha256']:
            raise ValueError('Completed-run artifact changed')
        text = log.read_text(encoding='utf-8-sig', errors='replace')
        for marker in ('window present mode mailbox', 'two-context asynchronous draw batches enabled',
                       'profile=host-groups publication=1 commands=1', 'host pipeline=1 '):
            if marker not in text:
                raise ValueError('Missing runtime marker '+marker)
        if (bool(arm) != ('[animation-job] mode=1 ' in text) or
                re.search(r'\[null-call\]|\] DIFF|MISMATCH\b|SPU HALT|ACCESS_VIOLATION|animation-job/native-outer', text)):
            raise ValueError('Wrong animation mode, checker overhead or game error')
        coverage = max(map(int, re.findall(r'^\[animation-job/host-pipeline\] native (\d+) \(owner count\)$', text, re.M)), default=0)
        if (arm and coverage < 256) or (not arm and coverage):
            raise ValueError('Actual native coverage differs from selected arm')
        phases = [json.loads(line) for line in (progress.parent/f'{tag}.phases.jsonl').read_text(encoding='utf-8-sig').splitlines()]
        if not phases or phases[-1]['phase'] != 'end':
            raise ValueError('Scene did not finish')
        if scene == 'Round':
            if run['round_intro_seconds'] != 50 or run['content_capture_ms'] != 190000 or run['content_shot_at'] != 195:
                raise ValueError('Wrong fixed round navigation/readback timing')
            captures = re.findall(r'one-shot frame dump complete frame=(\d+) elapsed_ms=([0-9.]+) continuous_readback=(\d+)', text)
            if (text.count('one-shot frame dump armed delay_ms=190000 continuous_readback=0') != 1 or
                    len(captures) != 1 or captures[0][2] != '0' or not 190000 <= float(captures[0][1]) < 195000):
                raise ValueError('Readback overlaps measurement or has wrong mode')
            with csv_path.open(newline='') as stream:
                captured = [row for row in csv.DictReader(stream) if int(row['frame']) == int(captures[0][0])]
            checks = [phase for phase in phases if phase['phase'] == 'content-shot']
            if len(captured) != 1 or not 180000 <= float(captured[0]['elapsed_ms']) < 195000 or len(checks) != 1 or checks[0]['elapsed_ms'] >= 200000:
                raise ValueError('Readback frame or conversion overlaps measurement')
            ppm, png = directory/f'vk-scene-{tag}-content.ppm', directory/f'vk-scene-{tag}-content.png'
            width, height, rgb = readback.read_ppm(ppm)
            if (width, height) != (1280, 720) or not any(rgb) or png.read_bytes() != readback.png_bytes(width, height, rgb):
                raise ValueError('Invalid native frame conversion')
            if not qa.get(tag, {}).get('inspected') or qa[tag]['sha256'] != sha(png) or qa[tag]['ppm_sha256'] != sha(ppm):
                raise ValueError('Native round frame not accepted')
        item = dict(tag=tag, arm=arm, native_lower_bound=coverage, frame_times=frames.summarize(csv_path, *window))
        if metric == 'Cpu':
            path = progress.parent/f'{tag}.cpu.json'
            if sha(path) != event['cpu_sha256']:
                raise ValueError('CPU artifact changed')
            cpu = json.loads(path.read_text())
            if cpu['status'] != 'complete' or cpu['exe_sha256'] != run['exe_sha256'] or cpu['binding_sha256'] != run['cpu_binding_sha256'] or cpu['window_seconds'] != window:
                raise ValueError('CPU binding/window mismatch')
            if len(cpu['snapshots']) != 2 or any(abs(s['elapsed_ns']/1e9-t) > .05 for s, t in zip(cpu['snapshots'], window)):
                raise ValueError('CPU snapshot boundary missed')
            item['cpu'] = work.summarize(*cpu['snapshots'])
            methods.add(cpu['method'])
        summaries.append(item)
    result = dict(scene=scene, metric=metric, option='BUZZ_ANIMATION_JOB', geometry_job=1,
                  exe_sha256=runs[0]['exe_sha256'], map_sha256=runs[0]['map_sha256'],
                  progress_sha256=sha(progress), runs=summaries,
                  fps=comparison([item['frame_times']['fps'] for item in summaries]),
                  limitation='One reversed series; image-41 host pipeline retained in both arms. FPS/CPU series separate; correctness and live native coverage qualified separately.')
    if metric == 'Cpu':
        if len(methods) != 1:
            raise ValueError('CPU measurement methods differ')
        result['cpu_method'] = methods.pop()
        for owner in ('pool', 'process'):
            result[owner] = {key: comparison([item['cpu'][owner][key] for item in summaries])
                             for key in ('cycles_per_frame', 'cpu_ms_per_frame')}
    if scene == 'Round':
        result['scene_qa_sha256'] = sha(qa_path)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('progress', type=Path)
    parser.add_argument('--csv-directory', type=Path, default=Path('analysis'))
    parser.add_argument('--scene-qa', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = report(args.progress, args.csv_directory, args.scene_qa)
    with args.output.open('x', encoding='utf8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(result['scene'], result['metric'], 'FPS', result['fps'])
    for owner in ('pool', 'process'):
        if owner in result:
            print(owner, result[owner])
