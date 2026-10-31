import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("cpu_report", ROOT/"perf/geometry-cpu-report.py")
reporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reporter)


class GeometryCPUTests(unittest.TestCase):
    def fixture(self, directory):
        events = []
        for index, mask in enumerate((1, 3, 5, 5, 3, 1)):
            tag = f"synthetic-{index}"
            start = dict(elapsed_ns=40_000_000_000, frames_before=1000, frames_after=1000,
                         process=dict(created_100ns=1, cycles=100, cpu_ns=100), threads=[])
            end = copy.deepcopy(start)
            end.update(elapsed_ns=98_000_000_000, frames_before=3000, frames_after=3000)
            cost = {1: 1000000, 3: 900000, 5: 800000}[mask]
            end["process"].update(cycles=100+cost*8, cpu_ns=100+cost*16)
            for i in range(1, 5):
                lane = dict(tid=i, created_100ns=i+1, name=f"SPURS pool lane {i}", cycles=100, cpu_ns=100)
                start["threads"].append(lane)
                end["threads"].append(dict(lane, cycles=100+cost, cpu_ns=100+cost*2))
            cpu = dict(status="complete", exe_sha256="a"*64, binding_sha256="b"*64, method="synthetic",
                       window_seconds=[40, 98], snapshots=[start, end])
            path = directory/f"{tag}.cpu.json"
            path.write_text(json.dumps(cpu))
            row = dict(tag=tag, scene="Menu", mask=mask, exe_sha256="a"*64, save_sha256="c"*64,
                       cpu_binding_sha256="b"*64, cpu_tool_sha256="d"*64, dump_every=0)
            events += [dict(row, state="start"), dict(row, state="done", cpu_sha256=reporter.work.sha256(path))]
        progress = directory/"progress.jsonl"
        self.save(progress, events)
        return progress, events

    def save(self, path, events):
        path.write_text("\n".join(json.dumps(e) for e in events))

    def test_reversed_savings_are_per_completed_frame(self):
        with tempfile.TemporaryDirectory() as where:
            path, _ = self.fixture(Path(where))
            result = reporter.report(path)
            for bit, change in (("0x2", -10), ("0x4", -20)):
                for owner in ("pool", "process"):
                    for values in result["kernels"][bit][owner].values():
                        self.assertAlmostEqual(values["change_percent"], change)
                        self.assertEqual([round(x) for x in values["pair_changes_percent"]], [change, change])

    def test_missing_changed_or_unbalanced_series_is_rejected(self):
        with tempfile.TemporaryDirectory() as where:
            path, events = self.fixture(Path(where))
            self.save(path, events[:-1])
            with self.assertRaises(ValueError): reporter.report(path)
            for field, value in (("mask", 9), ("cpu_binding_sha256", "d"*64), ("dump_every", 2500)):
                altered = copy.deepcopy(events)
                altered[2][field] = altered[3][field] = value
                self.save(path, altered)
                with self.assertRaises(ValueError): reporter.report(path)

    def test_cpu_file_tampering_or_wrong_window_is_rejected(self):
        with tempfile.TemporaryDirectory() as where:
            path, events = self.fixture(Path(where))
            cpu_path = path.parent/f"{events[0]['tag']}.cpu.json"
            cpu = json.loads(cpu_path.read_text())
            cpu["window_seconds"] = [41, 98]
            cpu_path.write_text(json.dumps(cpu))
            with self.assertRaisesRegex(ValueError, "completed-run hash"): reporter.report(path)
            events[1]["cpu_sha256"] = reporter.work.sha256(cpu_path)
            self.save(path, events)
            with self.assertRaisesRegex(ValueError, "window mismatch"): reporter.report(path)

    def test_cached_summary_cannot_hide_counter_reset(self):
        with tempfile.TemporaryDirectory() as where:
            path, events = self.fixture(Path(where))
            cpu_path = path.parent/f"{events[0]['tag']}.cpu.json"
            cpu = json.loads(cpu_path.read_text())
            cpu["snapshots"][1]["process"]["cycles"] = 0
            cpu["process"] = dict(cycles_per_frame=1)
            cpu_path.write_text(json.dumps(cpu))
            events[1]["cpu_sha256"] = reporter.work.sha256(cpu_path)
            self.save(path, events)
            with self.assertRaisesRegex(ValueError, "counter reset"): reporter.report(path)


if __name__ == "__main__": unittest.main()
