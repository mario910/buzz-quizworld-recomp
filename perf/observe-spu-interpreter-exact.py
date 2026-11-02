"""Cumulative exact executor counts from an explicitly bound diagnostic process.

Requires SPU_INTERP_EXACT=1 before startup. Suspends every target thread for a
coherent snapshot, then restores each thread's previous suspension count.
Never use in FPS/CPU benchmarks. No target memory writes, resets or sampling.
The legacy observer remains for older, immutable executables.
"""
import argparse
from contextlib import contextmanager
import ctypes as c
from ctypes import wintypes as w
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import time

spec = importlib.util.spec_from_file_location('exact_process_work', Path(__file__).with_name('measure-process-work.py'))
work = importlib.util.module_from_spec(spec)
spec.loader.exec_module(work)
CAPACITY = 4096
DESCRIPTOR = 0x5349455800011000
LAYOUT = {'descriptor': 8, 'enabled': 4, 'flags': 4,
          'unbucketed': 8, 'keys': CAPACITY * 8, 'steps': CAPACITY * 8}


def validate_binding(binding):
    symbols = binding['interpreter_symbols']
    if set(symbols) != set(LAYOUT):
        raise ValueError('Incomplete exact-counter binding')
    spans = []
    for name, size in LAYOUT.items():
        item = symbols[name]
        if (item['size'] != size or not isinstance(item['rva'], int) or item['rva'] < 0
                or item['rva'] + size > binding['image_size'] or item['rva'] % min(size, 8)):
            raise ValueError('Invalid exact-counter span: ' + name)
        spans.append((item['rva'], item['rva'] + size))
    spans.sort()
    if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
        raise ValueError('Exact-counter spans overlap')
    if set(binding['symbols']) != set(work.SYMBOLS) or len(set(binding['symbols'].values())) != 2:
        raise ValueError('Need both distinct renderer counters')
    for rva in binding['symbols'].values():
        if rva < 0 or rva + 8 > binding['image_size'] or rva % 8:
            raise ValueError('Invalid renderer span')
        if any(start < rva + 8 and rva < end for start, end in spans):
            raise ValueError('Renderer and interpreter spans overlap')


def bind(executable, link_map):
    result = work.make_binding(executable, link_map)
    text = Path(link_map).read_text(errors='replace')
    base = int(re.search(r'Preferred load address is\s+([0-9a-fA-F]+)', text)[1], 16)
    symbols = {}
    for name, size in LAYOUT.items():
        pattern = (r'^\s+[0-9a-fA-F]{4}:[0-9a-fA-F]+\s+g_spu_interp_exact_'
                   + re.escape(name) + r'\s+([0-9a-fA-F]{16})\s+.*$')
        hits = re.findall(pattern, text, re.M)
        if len(hits) != 1:
            raise ValueError('Need one exact interpreter symbol: ' + name)
        symbols[name] = dict(rva=int(hits[0], 16) - base, size=size)
    result['interpreter_symbols'] = symbols
    validate_binding(result)
    return result


def interpret(values):
    if set(values) != set(LAYOUT) or any(len(values[n]) != size for n, size in LAYOUT.items()):
        raise ValueError('Incomplete exact-counter snapshot')
    if struct.unpack('<Q', values['descriptor'])[0] != DESCRIPTOR:
        raise ValueError('Unknown exact-counter ABI')
    if struct.unpack('<i', values['enabled'])[0] != 1:
        raise ValueError('Exact counters are disabled or unarmed')
    if struct.unpack('<I', values['flags'])[0] or struct.unpack('<Q', values['unbucketed'])[0]:
        raise ValueError('Counter inventory full, overflowed or lost attribution')
    keys = struct.unpack('<4096Q', values['keys'])
    counts = struct.unpack('<4096Q', values['steps'])
    rows, seen, total = [], set(), 0
    for key, count in zip(keys, counts):
        if not key:
            if count:
                raise ValueError('Unclaimed bucket contains steps')
            continue
        if key > 1 << 40 or key in seen:
            raise ValueError('Invalid or duplicate image/page key')
        seen.add(key)
        if not count:
            # A writer can be suspended after claiming a new bucket and before
            # committing its first count. This does not imply instruction loss.
            continue
        raw = key - 1
        image = raw >> 8
        if image >= 1 << 31:
            image -= 1 << 32
        rows.append(dict(image=image, page=(raw & 255) << 10, steps=count))
        total += count
    if total >= 1 << 64:
        raise ValueError('Total instruction count overflow')
    rows.sort(key=lambda row: (row['image'], row['page']))
    return dict(total=total, buckets=rows, all_images_zero_proven=total == 0)


class WindowsThreads:
    """Suspend rights only; process/path identity was checked by NativeProcess."""
    def __init__(self, target):
        if target.pid == os.getpid():
            raise ValueError('Cannot suspend the observer itself')
        self.target = target
        self.k = target.k
        for name, result, args in (
            ('SuspendThread', w.DWORD, [w.HANDLE]),
            ('ResumeThread', w.DWORD, [w.HANDLE]),
            ('GetProcessIdOfThread', w.DWORD, [w.HANDLE]),
            ('GetExitCodeThread', w.BOOL, [w.HANDLE, c.POINTER(w.DWORD)]),
        ):
            fn = getattr(self.k, name)
            fn.restype, fn.argtypes = result, args

    def enumerate(self):
        class Entry(c.Structure):
            _fields_ = [(name, w.DWORD) for name in ('size', 'usage', 'tid', 'pid', 'base', 'delta', 'flags')]
        self.k.Thread32First.argtypes = self.k.Thread32Next.argtypes = [w.HANDLE, c.POINTER(Entry)]
        handle = self.k.CreateToolhelp32Snapshot(4, 0)
        if handle in (None, c.c_void_p(-1).value):
            raise c.WinError(c.get_last_error())
        result = set()
        try:
            entry = Entry()
            entry.size = c.sizeof(entry)
            ok = self.k.Thread32First(handle, c.byref(entry))
            while ok:
                if entry.pid == self.target.pid:
                    result.add(entry.tid)
                ok = self.k.Thread32Next(handle, c.byref(entry))
            if c.get_last_error() != 18:  # ERROR_NO_MORE_FILES
                raise c.WinError(c.get_last_error())
        finally:
            self.k.CloseHandle(handle)
        if not result:
            raise ValueError('No target threads remain')
        return result

    def suspend(self, tid):
        handle = self.k.OpenThread(0x802, False, tid)  # query limited + suspend/resume
        if not handle:
            raise c.WinError(c.get_last_error())
        try:
            if self.k.GetProcessIdOfThread(handle) != self.target.pid:
                raise ValueError('Thread identity changed during enumeration')
            previous = self.k.SuspendThread(handle)
            if previous == 0xffffffff:
                raise c.WinError(c.get_last_error())
            return (handle, previous)
        except BaseException:
            self.k.CloseHandle(handle)
            raise

    def active(self, item):
        code = w.DWORD()
        self.target.check(self.k.GetExitCodeThread(item[0], c.byref(code)))
        return code.value == 259

    def resume(self, item):
        handle, previous = item
        try:
            observed = self.k.ResumeThread(handle)
            if observed != previous + 1 and self.active(item):
                raise RuntimeError('Target suspension count changed externally')
        finally:
            self.k.CloseHandle(handle)


@contextmanager
def frozen_threads(api):
    """Discover to a fixed point; unwind every successful suspend on any error."""
    held = {}
    deadline = time.monotonic() + 2
    try:
        stable = 0
        while stable < 2:
            if time.monotonic() > deadline:
                raise ValueError('Target thread inventory did not stabilize')
            tids = api.enumerate()
            new = tids - held.keys()
            for tid in sorted(new):
                held[tid] = api.suspend(tid)
            if not set(held).issubset(tids) or not all(api.active(item) for item in held.values()):
                raise ValueError('A suspended thread exited or changed identity')
            stable = stable + 1 if not new else 0
        yield len(held)
    finally:
        failures = []
        for item in reversed(list(held.values())):
            try:
                api.resume(item)
            except BaseException as error:
                failures.append(error)
        if failures:
            raise RuntimeError('Could not restore all target thread suspensions') from failures[0]


def snapshot(target, binding):
    validate_binding(binding)
    before = target.now()
    with frozen_threads(WindowsThreads(target)) as thread_count:
        values = {}
        for name, item in binding['interpreter_symbols'].items():
            data = c.create_string_buffer(item['size'])
            got = c.c_size_t()
            target.check(target.k.ReadProcessMemory(target.process, target.base + item['rva'], data,
                                                    item['size'], c.byref(got)))
            if got.value != item['size']:
                raise ValueError('Partial exact-counter read')
            values[name] = data.raw
        frames = target.read_counter(binding, 'g_render_perf_serial')
        epoch = target.read_counter(binding, 'g_render_perf_epoch')
        result = interpret(values)
    return dict(**result, raw_hex={name: data.hex() for name, data in values.items()},
                suspended_threads=thread_count, frames=frames, renderer_epoch_ns=epoch,
                query_begin_ns=before, query_end_ns=target.now())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    b = commands.add_parser('bind')
    b.add_argument('--exe', type=Path, required=True)
    b.add_argument('--map', type=Path, required=True)
    b.add_argument('--output', type=Path, required=True)
    s = commands.add_parser('snapshot')
    s.add_argument('--pid', type=int, required=True)
    s.add_argument('--exe', type=Path, required=True)
    s.add_argument('--binding', type=Path, required=True)
    s.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Output already exists')
    if args.command == 'bind':
        work.write_new(args.output, bind(args.exe, args.map))
        return
    binding = json.loads(args.binding.read_text())
    validate_binding(binding)
    if work.sha256(args.exe) != binding['exe_sha256']:
        raise ValueError('Executable changed')
    target = work.NativeProcess(args.pid, args.exe)
    try:
        item = snapshot(target, binding)
        work.write_new(args.output, dict(status='complete', pid=args.pid,
            exe_sha256=binding['exe_sha256'], binding_sha256=work.sha256(args.binding),
            method='All target threads suspended to a stable inventory;fixed map-bound reads;each suspension restored;diagnostic only',
            snapshot=item))
    finally:
        target.close()


if __name__ == '__main__':
    main()
