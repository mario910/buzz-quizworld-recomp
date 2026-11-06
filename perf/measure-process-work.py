"""Read-only, non-suspending CPU counters for an exact EXE/map-bound game.

Reads only the renderer epoch/frame counters from the mapped EXE. No debugger,
thread suspension, target writes or clock-frequency conversion. Run a separate
matched series; never retrofit it into an already running FPS experiment.
Windows cycle API semantics:
https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import time

SYMBOLS = ("g_render_perf_epoch", "g_render_perf_serial")
RSX_THREAD = "RSX worker (FIFO + renderer)"


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def make_binding(executable, link_map):
    raw = Path(executable).read_bytes()
    if len(raw) < 64 or raw[:2] != b"MZ":
        raise ValueError("Not a PE executable")
    pe = struct.unpack_from("<I", raw, 0x3c)[0]
    if pe + 88 > len(raw) or raw[pe:pe+4] != b"PE\0\0" or struct.unpack_from("<H", raw, pe+24)[0] != 0x20b:
        raise ValueError("Need the reviewed 64-bit PE build")
    image_base = struct.unpack_from("<Q", raw, pe+24+24)[0]
    image_size = struct.unpack_from("<I", raw, pe+24+56)[0]
    text = Path(link_map).read_text(errors="replace")
    preferred = re.search(r"Preferred load address is\s+([0-9a-fA-F]+)", text)
    if not preferred or int(preferred[1], 16) != image_base:
        raise ValueError("Map preferred address differs from executable")
    symbols = {}
    for name in SYMBOLS:
        hits = re.findall(r"^\s+[0-9a-fA-F]{4}:[0-9a-fA-F]+\s+" + re.escape(name) +
                          r"\s+([0-9a-fA-F]{16})\s+.*$", text, re.M)
        if len(hits) != 1:
            raise ValueError(f"Need one exact map symbol: {name}")
        rva = int(hits[0], 16) - image_base
        if rva < 0 or rva + 8 > image_size or rva & 7:
            raise ValueError(f"Invalid counter RVA: {name}")
        symbols[name] = rva
    if len(set(symbols.values())) != len(SYMBOLS):
        raise ValueError("Renderer counters must not alias")
    return dict(executable=str(Path(executable).resolve()), exe_sha256=hashlib.sha256(raw).hexdigest(),
                map_sha256=sha256(link_map), image_size=image_size, symbols=symbols)


def pool_key(thread):
    return thread["tid"], thread["created_100ns"]


def summarize(start, end, expected_lanes=4, include_rsx=False, all_threads=False):
    if end["elapsed_ns"] <= start["elapsed_ns"]:
        raise ValueError("CPU window does not advance")
    lower = end["frames_before"] - start["frames_after"]
    upper = end["frames_after"] - start["frames_before"]
    if lower <= 0 or upper < lower:
        raise ValueError("No reliable completed-frame denominator")
    frames = (lower + upper)/2
    if (upper - lower)/frames > .002:
        raise ValueError("Counter-query frame uncertainty exceeds 0.2%")

    def delta(a, b):
        if a.get("created_100ns") != b.get("created_100ns"):
            raise ValueError("CPU counter identity mismatch")
        values = {field: b[field] - a[field] for field in ("cycles", "cpu_ns")}
        if any(value < 0 for value in values.values()):
            raise ValueError("CPU counter reset or identity mismatch")
        return dict(**values, cycles_per_frame=values["cycles"]/frames,
                    cpu_ms_per_frame=values["cpu_ns"]/1e6/frames)

    def lanes(snapshot):
        selected = [t for t in snapshot["threads"] if re.fullmatch(r"SPURS pool lane \d+", t["name"])]
        names = {t["name"] for t in selected}
        if names != {f"SPURS pool lane {i}" for i in range(1, expected_lanes+1)} or len(selected) != expected_lanes:
            raise ValueError("Missing or duplicate named pool lanes")
        if any(t.get("exited_100ns", 0) for t in selected):
            raise ValueError("A named pool lane exited inside the CPU window")
        return {pool_key(t): t for t in selected}

    before, after = lanes(start), lanes(end)
    if before.keys() != after.keys():
        raise ValueError("Pool thread identities changed inside CPU window")
    lane_results = [dict(name=before[key]["name"], tid=key[0], **delta(before[key], after[key])) for key in before]
    pooled = {field: sum(t[field] for t in lane_results) for field in ("cycles", "cpu_ns")}
    pooled.update(cycles_per_frame=pooled["cycles"]/frames, cpu_ms_per_frame=pooled["cpu_ns"]/1e6/frames)
    extra = {}
    if include_rsx:
        rsx_before = [t for t in start["threads"] if t["name"] == RSX_THREAD]
        rsx_after = [t for t in end["threads"] if t["name"] == RSX_THREAD]
        if len(rsx_before) != 1 or len(rsx_after) != 1:
            raise ValueError("Need one persistent named RSX thread")
        if pool_key(rsx_before[0]) != pool_key(rsx_after[0]) or any(
                t.get("exited_100ns", 0) for t in rsx_before + rsx_after):
            raise ValueError("RSX thread identity changed or exited inside CPU window")
        extra["rsx"] = delta(rsx_before[0], rsx_after[0])
    if all_threads:
        indexed = []
        for snapshot in (start, end):
            entries = {pool_key(t): t for t in snapshot["threads"]}
            if len(entries) != len(snapshot["threads"]):
                raise ValueError("Duplicate captured thread identity")
            indexed.append(entries)
        captured = []
        for key in indexed[0].keys() & indexed[1].keys():
            a, b = indexed[0][key], indexed[1][key]
            if a.get("exited_100ns", 0) or b.get("exited_100ns", 0):
                continue
            captured.append(dict(tid=key[0], created_100ns=key[1], name=b["name"], **delta(a, b)))
        captured.sort(key=lambda t: t["cpu_ns"], reverse=True)
        total = delta(start["process"], end["process"])
        extra["captured_threads"] = captured
        extra["uncaptured"] = {field: total[field] - sum(t[field] for t in captured)
                               for field in ("cpu_ns", "cycles")}
        extra["thread_coverage"] = (
            "Persistent handles discovered before the window only. Late-created or exited threads "
            "are excluded; process totals remain authoritative. Endpoint queries are not atomic.")
    return dict(**extra, frames=frames, frames_lower=lower, frames_upper=upper,
                elapsed_seconds=(end["elapsed_ns"]-start["elapsed_ns"])/1e9,
                process=delta(start["process"], end["process"]), pool=pooled, lanes=lane_results)


class NativeProcess:
    """Query only this explicit PID/path; retain thread handles for stable identity."""
    def __init__(self, pid, expected_executable):
        if sys.platform != "win32":
            raise RuntimeError("Windows CPU counters are required")
        self.k = c.WinDLL("kernel32", use_last_error=True)
        self.handles = {}
        self.pid = pid
        self.process = None
        signatures = {
            "OpenProcess": (w.HANDLE, [w.DWORD, w.BOOL, w.DWORD]),
            "OpenThread": (w.HANDLE, [w.DWORD, w.BOOL, w.DWORD]),
            "CloseHandle": (w.BOOL, [w.HANDLE]),
            "CreateToolhelp32Snapshot": (w.HANDLE, [w.DWORD, w.DWORD]),
            "QueryFullProcessImageNameW": (w.BOOL, [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]),
            "QueryProcessCycleTime": (w.BOOL, [w.HANDLE, c.POINTER(c.c_ulonglong)]),
            "QueryThreadCycleTime": (w.BOOL, [w.HANDLE, c.POINTER(c.c_ulonglong)]),
            "GetProcessTimes": (w.BOOL, [w.HANDLE] + [c.POINTER(w.FILETIME)]*4),
            "GetThreadTimes": (w.BOOL, [w.HANDLE] + [c.POINTER(w.FILETIME)]*4),
            "GetThreadDescription": (c.c_long, [w.HANDLE, c.POINTER(c.c_void_p)]),
            "LocalFree": (c.c_void_p, [c.c_void_p]),
            "GetExitCodeProcess": (w.BOOL, [w.HANDLE, c.POINTER(w.DWORD)]),
            "QueryPerformanceCounter": (w.BOOL, [c.POINTER(c.c_longlong)]),
            "QueryPerformanceFrequency": (w.BOOL, [c.POINTER(c.c_longlong)]),
            "ReadProcessMemory": (w.BOOL, [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]),
            "K32EnumProcessModules": (w.BOOL, [w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)]),
        }
        for name, (result, args) in signatures.items():
            function = getattr(self.k, name); function.restype = result; function.argtypes = args
        self.process = self.k.OpenProcess(0x410, False, pid)
        if not self.process:
            raise c.WinError(c.get_last_error())
        try:
            name = c.create_unicode_buffer(32768); length = w.DWORD(len(name))
            self.check(self.k.QueryFullProcessImageNameW(self.process, 0, name, c.byref(length)))
            if Path(name.value).resolve() != Path(expected_executable).resolve():
                raise ValueError("PID is not the explicit expected executable")
            module = c.c_void_p(); needed = w.DWORD()
            self.check(self.k.K32EnumProcessModules(self.process, c.byref(module), c.sizeof(module), c.byref(needed)))
            self.base = module.value
            frequency = c.c_longlong(); self.check(self.k.QueryPerformanceFrequency(c.byref(frequency)))
            self.frequency = frequency.value
        except Exception:
            self.close(); raise

    @staticmethod
    def check(ok):
        if not ok:
            raise c.WinError(c.get_last_error())

    def now(self):
        counter = c.c_longlong(); self.check(self.k.QueryPerformanceCounter(c.byref(counter)))
        # Same double conversion as ps3_ft_now, not CPU cycles -> time.
        return int(float(counter.value)*1e9/float(self.frequency))

    def alive(self):
        code = w.DWORD(); self.check(self.k.GetExitCodeProcess(self.process, c.byref(code)))
        return code.value == 259

    def read_counter(self, binding, symbol):
        if symbol not in SYMBOLS:
            raise ValueError("Only the two reviewed renderer counters may be read")
        rva = binding["symbols"][symbol]
        if rva < 0 or rva + 8 > binding["image_size"]:
            raise ValueError("Counter lies outside bound image")
        value = c.c_ulonglong(); got = c.c_size_t()
        self.check(self.k.ReadProcessMemory(self.process, self.base+rva, c.byref(value), 8, c.byref(got)))
        if got.value != 8:
            raise ValueError("Partial renderer counter read")
        return value.value

    def cpu(self, handle, thread=False):
        created, exited, kernel, user = (w.FILETIME() for _ in range(4))
        self.check(getattr(self.k, "GetThreadTimes" if thread else "GetProcessTimes")(
            handle, c.byref(created), c.byref(exited), c.byref(kernel), c.byref(user)))
        ticks = lambda f: (f.dwHighDateTime << 32) | f.dwLowDateTime
        cycles = c.c_ulonglong()
        self.check(getattr(self.k, "QueryThreadCycleTime" if thread else "QueryProcessCycleTime")(handle, c.byref(cycles)))
        return dict(created_100ns=ticks(created), exited_100ns=ticks(exited),
                    cycles=cycles.value, cpu_ns=(ticks(kernel)+ticks(user))*100)

    def thread_name(self, handle):
        pointer = c.c_void_p()
        if self.k.GetThreadDescription(handle, c.byref(pointer)) < 0:
            return ""
        try:
            return c.wstring_at(pointer) if pointer.value else ""
        finally:
            if pointer.value:
                self.k.LocalFree(pointer)

    def discover(self):
        class Entry(c.Structure):
            _fields_ = [(name, w.DWORD) for name in ("size", "usage", "tid", "pid", "base", "delta", "flags")]
        self.k.Thread32First.argtypes = self.k.Thread32Next.argtypes = [w.HANDLE, c.POINTER(Entry)]
        snapshot = self.k.CreateToolhelp32Snapshot(4, 0)
        if snapshot in (None, c.c_void_p(-1).value):
            raise c.WinError(c.get_last_error())
        try:
            entry = Entry(); entry.size = c.sizeof(entry)
            ok = self.k.Thread32First(snapshot, c.byref(entry))
            while ok:
                if entry.pid == self.pid and entry.tid not in self.handles:
                    handle = self.k.OpenThread(0x800, False, entry.tid)
                    if handle:
                        self.handles[entry.tid] = handle
                ok = self.k.Thread32Next(snapshot, c.byref(entry))
        finally:
            self.k.CloseHandle(snapshot)

    def snapshot(self, binding, epoch):
        # Discovery enumerates system threads and can exceed a frame. Retain
        # named handles before the window, never on a counter boundary.
        if not self.handles:
            raise ValueError("Discover pool handles before the CPU window")
        before = self.now(); frames_before = self.read_counter(binding, SYMBOLS[1])
        process = self.cpu(self.process); threads = []
        for tid, handle in self.handles.items():
            try:
                threads.append(dict(tid=tid, name=self.thread_name(handle), **self.cpu(handle, True)))
            except OSError:
                # Missing pool lanes are rejected by summarize; process total
                # remains authoritative for work outside the persistent pool.
                continue
        frames_after = self.read_counter(binding, SYMBOLS[1]); after = self.now()
        if frames_after < frames_before or after < before:
            raise ValueError("Counter snapshot is not monotonic")
        return dict(elapsed_ns=(before+after)//2-epoch, query_span_ns=after-before,
                    frames_before=frames_before, frames_after=frames_after, process=process, threads=threads)

    def discover_pool(self, include_rsx=False):
        self.discover()
        for tid, handle in list(self.handles.items()):
            name = self.thread_name(handle)
            if not re.fullmatch(r"SPURS pool lane \d+", name) and not (include_rsx and name == RSX_THREAD):
                self.k.CloseHandle(handle)
                del self.handles[tid]

    def wait_until(self, target):
        while True:
            if not self.alive():
                raise ValueError("Game stopped before the CPU window completed")
            remaining = (target-self.now())/1e9
            if remaining <= 0:
                return
            time.sleep(min(.5, max(.001, remaining-.02)))

    def close(self):
        for handle in self.handles.values():
            self.k.CloseHandle(handle)
        self.handles.clear()
        if self.process:
            self.k.CloseHandle(self.process); self.process = None


def write_new(path, value):
    with Path(path).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2); stream.write("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    bind = commands.add_parser("bind")
    bind.add_argument("--exe", type=Path, required=True); bind.add_argument("--map", type=Path, required=True)
    bind.add_argument("--output", type=Path, required=True)
    measure = commands.add_parser("measure")
    measure.add_argument("--pid", type=int, required=True); measure.add_argument("--exe", type=Path, required=True)
    measure.add_argument("--binding", type=Path, required=True); measure.add_argument("--output", type=Path, required=True)
    measure.add_argument("--start", type=float, required=True); measure.add_argument("--end", type=float, required=True)
    measure.add_argument("--include-rsx", action="store_true", help="Also measure the named FIFO/renderer thread")
    measure.add_argument("--all-threads", action="store_true", help="Retain all owned handles before the window; report persistent thread costs and uncaptured process work")
    args = parser.parse_args()
    if args.command == "bind":
        write_new(args.output, make_binding(args.exe, args.map)); return
    if not 0 < args.start < args.end <= 900:
        raise ValueError("Invalid renderer-relative CPU window")
    if args.output.exists():
        raise ValueError("CPU output already exists")
    binding = json.loads(args.binding.read_text())
    if sha256(args.exe) != binding["exe_sha256"]:
        raise ValueError("Executable differs from the CPU/map binding")
    target = NativeProcess(args.pid, args.exe)
    try:
        timeout = time.monotonic()+30
        while True:
            epoch = target.read_counter(binding, SYMBOLS[0]); now = target.now()
            if epoch:
                if epoch > now or now-epoch > 900_000_000_000:
                    raise ValueError("Renderer epoch is inconsistent with this bound executable")
                break
            if time.monotonic() > timeout or not target.alive():
                raise ValueError("Renderer epoch did not initialize")
            time.sleep(.05)
        if target.now() > epoch + args.start*1e9:
            raise ValueError("Attached after the requested CPU window began")
        target.wait_until(epoch + int((args.start-1)*1e9))
        if args.all_threads:
            target.discover()
        else:
            target.discover_pool(args.include_rsx)
        snapshots = []
        for seconds in (args.start, args.end):
            target.wait_until(epoch + int(seconds*1e9))
            snapshot = target.snapshot(binding, epoch)
            if abs(snapshot["elapsed_ns"]/1e9-seconds) > .05:
                raise ValueError(f"CPU snapshot missed its boundary by more than 50 ms: requested={seconds}, "
                                 f"actual={snapshot['elapsed_ns']/1e9:.6f}, query_ms={snapshot['query_span_ns']/1e6:.3f}")
            snapshots.append(snapshot)
        result = dict(status="complete", pid=args.pid, exe_sha256=binding["exe_sha256"],
                      binding_sha256=sha256(args.binding), window_seconds=[args.start, args.end],
                      method="OS CPU/cycle counters, pre-discovered named handles, no suspension; map-bound renderer epoch/serial",
                      includes_rsx=args.include_rsx,
                      includes_all_threads=args.all_threads,
                      snapshots=snapshots, **summarize(*snapshots, include_rsx=args.include_rsx,
                                                      all_threads=args.all_threads))
        write_new(args.output, result)
    finally:
        target.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(f"CPU measurement failed: {error}", file=sys.stderr)
        sys.exit(1)
