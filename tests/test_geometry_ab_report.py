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

    def batch_fixture(self, directory):
        events = []
        for scene in ("Menu", "Round"):
            for i, mask in enumerate((1, 5, 33, 1025, 1025, 33, 5, 1)):
                tag = f"batch-{scene}-{i}"
                row = dict(tag=tag, scene=scene, mask=mask, exe_sha256="a"*64,
                           save_sha256="c"*64, dump_every=2500 if scene == "Menu" else 6500)
                events += [dict(state="start", **row), dict(state="done", **row)]
                with (directory/f"vk-scene-{tag}.csv").open("w", newline="") as f:
                    writer = csv.writer(f)
                    writer.writerow(("elapsed_ms", "interval_ms", "drain_ms"))
                    interval = {1: 500, 5: 400, 33: 250, 1025: 625}[mask]
                    for t in range(0, 250001, interval):
                        writer.writerow((t, interval, 20))
        path = directory/"batch.jsonl"
        self.save(path, events)
        return path, events

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

    def test_batch_preserves_isolated_reversals_and_shared_references(self):
        with tempfile.TemporaryDirectory() as where:
            directory = Path(where)
            path, _ = self.batch_fixture(directory)
            result = ab.batch_report(path, directory, {"Menu": (40, 98), "Round": (150, 220)})
        self.assertTrue(result["shared_reference"])
        self.assertEqual(result["baseline_mask"], 1)
        expected = {"0x4": 25, "0x20": 100, "0x400": -20}
        for kernel, change in expected.items():
            for scene in result["kernels"][kernel]["scenes"].values():
                self.assertEqual([round(p["change_percent"]) for p in scene["pairs"]], [change, change])
                self.assertEqual([r["mask"] for r in scene["runs"]],
                                 [1, 1 | int(kernel, 16), 1 | int(kernel, 16), 1])

    def test_batch_missing_or_asymmetric_runs_are_rejected(self):
        with tempfile.TemporaryDirectory() as where:
            directory = Path(where)
            path, events = self.batch_fixture(directory)
            self.save(path, events[:-1])
            with self.assertRaises(ValueError):
                ab.batch_report(path, directory, {})
            events[6]["mask"] = events[7]["mask"] = 5
            self.save(path, events)
            with self.assertRaisesRegex(ValueError, "independent bit"):
                ab.batch_report(path, directory, {})

    def test_batch_cannot_hide_a_changed_executable_in_another_arm(self):
        with tempfile.TemporaryDirectory() as where:
            directory = Path(where)
            path, events = self.batch_fixture(directory)
            for event in events:
                if event["mask"] == 33:
                    event["exe_sha256"] = "b"*64
            self.save(path, events)
            with self.assertRaisesRegex(ValueError, "different executables"):
                ab.batch_report(path, directory, {"Menu": (40, 98), "Round": (150, 220)})

    def test_batch_cannot_attribute_a_combination_to_one_kernel(self):
        with tempfile.TemporaryDirectory() as where:
            directory = Path(where)
            path, events = self.batch_fixture(directory)
            for event in events:
                if event["mask"] == 33:
                    event["mask"] = 37
            self.save(path, events)
            with self.assertRaisesRegex(ValueError, "independent bit"):
                ab.batch_report(path, directory, {})

    def test_batch_rejects_changed_save_or_asymmetric_readbacks(self):
        with tempfile.TemporaryDirectory() as where:
            directory = Path(where)
            path, events = self.batch_fixture(directory)
            events[2]["save_sha256"] = "d"*64
            self.save(path, events)
            with self.assertRaisesRegex(ValueError, "template saves differ"):
                ab.batch_report(path, directory, {})
            events[2]["save_sha256"] = "c"*64
            events[2]["dump_every"] = 0
            self.save(path, events)
            with self.assertRaisesRegex(ValueError, "dump settings differ"):
                ab.batch_report(path, directory, {})


if __name__ == "__main__":
    unittest.main()
