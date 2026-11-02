"""Exercise actual Windows suspension/map-bound reads on a controlled fixture."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import time

path = Path(__file__).resolve().parents[1] / 'perf/observe-spu-interpreter-exact.py'
spec = importlib.util.spec_from_file_location('exact_process_observer', path)
observer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(observer)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    output = args.output_root / ('process-observer-' + str(time.time_ns()))
    output.mkdir()
    binding = observer.bind(args.exe, args.map)
    observer.work.write_new(output / 'binding.json', binding)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('SPU_', 'YDKJ_'))}
    env.update(SPU_INTERP_EXACT='1', SPU_INTERP_STATS='0', SPU_INTERP_ENTRIES='0')
    lines = queue.Queue()
    with (output / 'stderr.log').open('wb') as stderr:
        process = subprocess.Popen([str(args.exe.resolve()), 'hold'], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=stderr, env=env)
        target = None
        all_lines = []
        def read_stdout():
            for line in process.stdout:
                lines.put(line.decode(errors='replace').strip())
            lines.put(None)
        reader = threading.Thread(target=read_stdout, daemon=True)
        reader.start()
        def wait_line(prefix):
            deadline = time.monotonic() + 15
            while True:
                line = lines.get(timeout=max(.001, deadline - time.monotonic()))
                if line is None:
                    raise ValueError('Fixture exited before ' + prefix)
                all_lines.append(line)
                if line.startswith(prefix):
                    return
                if time.monotonic() > deadline:
                    raise ValueError('Fixture did not reach ' + prefix)
        try:
            wait_line('READY ')
            target = observer.work.NativeProcess(process.pid, args.exe)
            zero = observer.snapshot(target, binding)
            assert zero['total'] == 0 and zero['all_images_zero_proven'] and zero['frames'] == 0
            process.stdin.write(b'r')
            process.stdin.flush()
            # Includes live snapshots while the four recorder threads are being
            # created/running/exiting. These are diagnostics, never timings.
            active = []
            deadline = time.monotonic() + 5
            while True:
                item = observer.snapshot(target, binding)
                active.append(item)
                if 0 < item['total'] < 800000:
                    assert item['suspended_threads'] >= 5
                    break
                if item['total'] == 800000 or time.monotonic() >= deadline:
                    raise AssertionError('No snapshot captured the active recorder threads')
                time.sleep(.002)
            wait_line('DONE')
            positive = observer.snapshot(target, binding)
            expected = [dict(image=-7, page=0x3f800, steps=400000)]
            expected += [dict(image=100+i, page=0x1000, steps=100000) for i in range(4)]
            assert positive['total'] == 800000 and positive['buckets'] == expected and positive['frames'] == 1
            totals = [s['total'] for s in [zero, *active, positive]]
            assert totals == sorted(totals)
            # A bad ABI must raise and still resume the process: its following
            # stdin command and orderly exit prove that it was not left frozen.
            bad = dict(binding, interpreter_symbols={k: dict(v) for k, v in binding['interpreter_symbols'].items()})
            bad['interpreter_symbols']['descriptor']['rva'] = binding['symbols']['g_render_perf_epoch']
            # Preserve disjoint spans while reading a known wrong descriptor.
            bad['symbols'] = dict(g_render_perf_epoch=0, g_render_perf_serial=8)
            try:
                observer.snapshot(target, bad)
            except ValueError as error:
                assert 'ABI' in str(error), error
            else:
                raise AssertionError('Corrupt ABI was accepted')
            process.stdin.write(b'q')
            process.stdin.flush()
            assert process.wait(timeout=10) == 0
            result = dict(status='pass', binding_sha256=observer.work.sha256(output / 'binding.json'),
                          snapshots=[zero, *active, positive],
                          checks='explicit zero;actual concurrent recording;exact five-bucket 800000 total;monotonic snapshots;ABI failure cleanup;orderly exit')
            observer.work.write_new(output / 'result.json', result)
            print('PASS actual Windows process: coherent zero/positive snapshots, exact 800000 steps, suspension cleanup; ' + str(output))
        finally:
            if target is not None:
                target.close()
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            reader.join(timeout=2)
            process.stdin.close()
            process.stdout.close()
            (output / 'stdout.log').write_text('\n'.join(all_lines) + '\n')


if __name__ == '__main__':
    main()
