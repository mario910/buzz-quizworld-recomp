import copy
import importlib.util
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("work", Path(__file__).resolve().parents[1]/"perf/measure-process-work.py")
work = importlib.util.module_from_spec(spec)
spec.loader.exec_module(work)


class ProcessWorkTests(unittest.TestCase):
    def snapshots(self):
        start = dict(elapsed_ns=40_000_000_000, frames_before=1000, frames_after=1000,
                     process=dict(created_100ns=1, cycles=10_000, cpu_ns=20_000_000), threads=[])
        end = dict(elapsed_ns=98_000_000_000, frames_before=3000, frames_after=3000,
                   process=dict(created_100ns=1, cycles=8_010_000, cpu_ns=4_020_000_000), threads=[])
        for i in range(1, 5):
            start["threads"].append(dict(tid=i, created_100ns=100+i, name=f"SPURS pool lane {i}", cycles=100, cpu_ns=10))
            end["threads"].append(dict(tid=i, created_100ns=100+i, name=f"SPURS pool lane {i}", cycles=1_000_100, cpu_ns=500_000_010))
        return start, end

    def test_normalization_keeps_cycles_and_cpu_time_independent(self):
        result = work.summarize(*self.snapshots())
        self.assertEqual(result["frames"], 2000)
        self.assertEqual(result["elapsed_seconds"], 58)
        self.assertEqual(result["pool"]["cycles_per_frame"], 2000)
        self.assertEqual(result["pool"]["cpu_ms_per_frame"], 1)
        self.assertEqual(result["process"]["cycles_per_frame"], 4000)
        self.assertEqual(result["process"]["cpu_ms_per_frame"], 2)

    def test_missing_lane_and_tid_reuse_cannot_look_like_savings(self):
        start, end = self.snapshots()
        end["threads"].pop()
        with self.assertRaisesRegex(ValueError, "pool lanes"):
            work.summarize(start, end)
        start, end = self.snapshots()
        end["threads"][0]["created_100ns"] += 1
        with self.assertRaisesRegex(ValueError, "identities changed"):
            work.summarize(start, end)

    def test_exited_or_duplicated_pool_threads_are_rejected(self):
        start, end = self.snapshots()
        end["threads"][0]["exited_100ns"] = 99
        with self.assertRaisesRegex(ValueError, "lane exited"):
            work.summarize(start, end)
        start, end = self.snapshots()
        end["threads"].append(copy.deepcopy(end["threads"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            work.summarize(start, end)

    def test_reset_counters_or_wrong_process_are_rejected(self):
        start, end = self.snapshots()
        end["process"]["cycles"] = 1
        with self.assertRaisesRegex(ValueError, "counter reset"):
            work.summarize(start, end)
        start, end = self.snapshots()
        end["process"]["created_100ns"] += 1
        with self.assertRaisesRegex(ValueError, "identity mismatch"):
            work.summarize(start, end)

    def test_frame_boundary_uncertainty_is_bounded(self):
        start, end = self.snapshots()
        start["frames_after"] += 1
        end["frames_after"] += 1
        result = work.summarize(start, end)
        self.assertEqual((result["frames_lower"], result["frames_upper"]), (1999, 2001))
        end["frames_after"] += 20
        with self.assertRaisesRegex(ValueError, "uncertainty"):
            work.summarize(start, end)

    def test_zero_frames_or_reversed_time_are_rejected(self):
        start, end = self.snapshots()
        end["frames_before"] = end["frames_after"] = start["frames_before"]
        with self.assertRaisesRegex(ValueError, "denominator"):
            work.summarize(start, end)
        start, end = self.snapshots()
        end["elapsed_ns"] = start["elapsed_ns"]
        with self.assertRaisesRegex(ValueError, "advance"):
            work.summarize(start, end)

    def binding_fixture(self, directory):
        binary = bytearray(512); binary[:2] = b"MZ"; struct.pack_into("<I", binary, 60, 64)
        binary[64:68] = b"PE\0\0"; struct.pack_into("<H", binary, 88, 0x20b)
        struct.pack_into("<Q", binary, 112, 0x140000000); struct.pack_into("<I", binary, 144, 4096)
        executable = directory/"synthetic.exe"; executable.write_bytes(binary)
        link_map = directory/"synthetic.map"
        link_map.write_text("Preferred load address is 0000000140000000\n"
                            " 0003:00000000 g_render_perf_epoch 0000000140000200 main.obj\n"
                            " 0003:00000008 g_render_perf_serial 0000000140000208 main.obj\n")
        return executable, link_map

    def test_binding_requires_unique_bounded_counter_symbols(self):
        with tempfile.TemporaryDirectory() as where:
            executable, link_map = self.binding_fixture(Path(where))
            binding = work.make_binding(executable, link_map)
            self.assertEqual(binding["symbols"], dict(g_render_perf_epoch=512, g_render_perf_serial=520))
            link_map.write_text(link_map.read_text().replace("140000208", "140001008"))
            with self.assertRaisesRegex(ValueError, "Invalid counter RVA"):
                work.make_binding(executable, link_map)

    def test_binding_rejects_mismatched_or_aliased_maps(self):
        with tempfile.TemporaryDirectory() as where:
            executable, link_map = self.binding_fixture(Path(where))
            original = link_map.read_text()
            link_map.write_text(original.replace("140000208", "140000200"))
            with self.assertRaisesRegex(ValueError, "alias"):
                work.make_binding(executable, link_map)
            link_map.write_text(original.replace("address is 0000000140000000", "address is 0000000150000000"))
            with self.assertRaisesRegex(ValueError, "preferred address"):
                work.make_binding(executable, link_map)

    @unittest.skipUnless(sys.platform == "win32", "Windows API integration")
    def test_read_only_self_counter_apis_and_cleanup(self):
        # Windows venv python.exe can redirect to its base interpreter process.
        reader = work.NativeProcess(os.getpid(), Path(getattr(sys, "_base_executable", sys.executable)))
        try:
            first = reader.cpu(reader.process)
            self.assertTrue(reader.alive())
            reader.discover()
            self.assertTrue(reader.handles)
            for handle in reader.handles.values():
                self.assertIsInstance(reader.thread_name(handle), str)
                self.assertGreater(reader.cpu(handle, True)["created_100ns"], 0)
            second = reader.cpu(reader.process)
            self.assertGreaterEqual(second["cycles"], first["cycles"])
            self.assertGreaterEqual(second["cpu_ns"], first["cpu_ns"])
            binding = dict(image_size=4096, symbols={"g_render_perf_epoch": 0})
            self.assertEqual(reader.read_counter(binding, "g_render_perf_epoch") & 0xffff, 0x5a4d)
        finally:
            reader.close()
        self.assertFalse(reader.handles)
        self.assertIsNone(reader.process)


if __name__ == "__main__":
    unittest.main()
