#!/usr/bin/env python3
"""Caller attribution must preserve clipping, nesting and old trace support."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "renderer_waits", Path(__file__).resolve().parents[1] / "perf/renderer-waits.py")
waits = importlib.util.module_from_spec(spec)
spec.loader.exec_module(waits)


class CallerSpans(unittest.TestCase):
    def test_clipped_nested_and_failed_calls(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace"
            path.write_text("""# ps3_ftrace spans=9 dropped=0
8 0 0 0 1000000 1000000
8 0 0 0 1030000 1030000
8 0 0 0 1060000 1060000
7 0 0 0 1000000 1060000
116 0 3 0 990000 1020000
115 0 5 1 1005000 1015000
102 0 1 0 1006000 1014000
116 0 3 ffffffff 1040000 1042000
115 0 6 ffffffff 1070000 1080000
""")
            source = Path(directory) / "backend.c"
            source.write_text("\n\nif(vk_batch_flush()) return;\n\n"
                              "buzz_vk_submit_at(__LINE__,reason);\n"
                              "vk_submit_and_wait();\n")
            report = waits.analyze(path, source)
            self.assertEqual(report["frames"], 2)
            calls = report["vulkan_calls"]
            self.assertEqual(len(calls), 3)  # Outside-window call excluded.
            self.assertEqual(calls["full_batch_flush/line_3/0"]["ms_per_frame"], 10)
            self.assertEqual(calls["full_batch_flush/line_3/4294967295"]["return_status"], 4294967295)
            self.assertEqual(calls["synchronous_submit/line_5/1"]["ms_per_frame"], 5)
            self.assertEqual(calls["synchronous_submit/line_5/1"]["source"],
                             "buzz_vk_submit_at(__LINE__,reason);")
            self.assertEqual(report["nested_fence_ms_per_frame"]["fifo_drain"], 4)
            self.assertEqual(report["spans"]["fifo_drain"]["ms_per_frame"], 30)
            source.write_text("unrelated source\n" * 6)
            self.assertTrue(all(call["source"] is None
                                for call in waits.analyze(path, source)["vulkan_calls"].values()))

    def test_old_trace_and_dropped_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace"
            rows = "8 0 0 0 1000000 1000000\n8 0 0 0 1030000 1030000\n"
            path.write_text("# dropped=0\n" + rows)
            report = waits.analyze(path)
            self.assertEqual(report["vulkan_calls"], {})
            self.assertIsNone(report["vulkan_source"])
            path.write_text("# dropped=1\n" + rows)
            with self.assertRaisesRegex(ValueError, "dropped"):
                waits.analyze(path)


if __name__ == "__main__":
    unittest.main()
