import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("ab", Path(__file__).resolve().parents[1]/"perf/geometry-ab-report.py")
ab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ab)


class GeometryABTests(unittest.TestCase):
    def fixture(self, directory):
        events = []
        for scene in ("Menu", "Round"):
            for i, mask in enumerate((0, 1, 1, 0)):
                tag = f"synthetic-{scene}-{i}"
                row = dict(tag=tag, scene=scene, mask=mask, exe_sha256="a"*64)
                events += [dict(state="start", **row), dict(state="done", **row)]
                with (directory/f"vk-scene-{tag}.csv").open("w", newline="") as f:
                    writer = csv.writer(f)
                    writer.writerow(("elapsed_ms", "interval_ms", "drain_ms"))
                    interval = 400 if mask else 500
                    for t in range(0, 250001, interval):
                        writer.writerow((t, interval, 20))
        path = directory/"progress.jsonl"
        self.save(path, events)
        return path, events

    def save(self, path, events):
        path.write_text("\n".join(json.dumps(row) for row in events))

    def test_reversed_pairs_have_consistent_direction(self):
        with tempfile.TemporaryDirectory() as where:
            directory = Path(where)
            path, _ = self.fixture(directory)
            report = ab.report(path, directory, {"Menu": (40, 98), "Round": (150, 220)})
        for scene in report["scenes"].values():
            self.assertAlmostEqual(scene["baseline_fps"], 2)
            self.assertAlmostEqual(scene["native_fps"], 2.5)
            self.assertEqual([round(p["change_percent"]) for p in scene["pairs"]], [25, 25])

    def test_missing_run_or_changed_executable_cannot_pass(self):
        with tempfile.TemporaryDirectory() as where:
            directory = Path(where)
            path, events = self.fixture(directory)
            self.save(path, events[:-1])
            with self.assertRaisesRegex(ValueError, "eight completed"):
                ab.report(path, directory, {})
            events[-1]["exe_sha256"] = "b"*64
            self.save(path, events)
            with self.assertRaisesRegex(ValueError, "different executables"):
                ab.report(path, directory, {})

    def test_wrong_order_or_truncated_window_is_rejected(self):
        with tempfile.TemporaryDirectory() as where:
            directory = Path(where)
            path, events = self.fixture(directory)
            events[4]["mask"] = events[5]["mask"] = 0
            events[6]["mask"] = events[7]["mask"] = 1
            self.save(path, events)
            with self.assertRaisesRegex(ValueError, "A-B-B-A"):
                ab.report(path, directory, {})
            with self.assertRaisesRegex(ValueError, "Incomplete timing window"):
                ab.summarize(directory/"vk-scene-synthetic-Menu-0.csv", 200, 300)


if __name__ == "__main__":
    unittest.main()
