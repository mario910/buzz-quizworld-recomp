import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('renderer_waits', Path(__file__).resolve().parents[1] / 'perf/renderer-waits.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class RendererWaitsTest(unittest.TestCase):
    def analyze(self, body, dropped=0):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.trace'
            path.write_text(f'# ps3_ftrace dropped={dropped}\n' + body, encoding='utf-8')
            return module.analyze(path)

    def test_previous_stop_and_clipping(self):
        result = self.analyze('''107 2 100 200 500 500
8 0 0 0 1000 1000
100 0 1 4 750 2000
107 4 200 200 5000 5000
100 0 2 4 6000 8000
108 0 0 0 7000 7000
8 0 0 0 11000 11000
107 1 300 400 12000 12000
''')
        self.assertEqual(result['frames'], 1)
        self.assertEqual(result['idle_after_fifo_stop']['parked_jump']['ms_per_frame'], 1)
        self.assertEqual(result['idle_after_fifo_stop']['caught_up']['ms_per_frame'], 2)
        self.assertEqual(result['idle_after_fifo_stop']['parked_jump']['kick_wakes'], 1)
        self.assertEqual(result['fifo_stop_counts']['parked_jump'], 0)
        self.assertEqual(result['fifo_stop_counts']['caught_up'], 1)
        self.assertEqual(result['fifo_stop_counts']['semaphore'], 0)
        self.assertEqual(result['producer_kick_count'], 1)

    def test_old_capture_has_unknown_stop(self):
        result = self.analyze('8 0 0 0 1000 1000\n100 0 0 4 2000 3000\n8 0 0 0 11000 11000\n')
        self.assertEqual(result['idle_after_fifo_stop']['unknown']['ms_per_frame'], 1)

    def test_published_work_excludes_parked_and_stale_head(self):
        result = self.analyze('''8 0 0 0 1000 1000
107 4 200 200 2000 2000
100 0 2 4 3000 7000
103 0 300 200 3001 3001
104 0 400 200 7001 7001
107 2 400 500 8000 8000
100 0 2 1 9000 10000
103 0 600 400 9001 9001
104 0 700 400 10001 10001
107 4 700 700 11000 11000
100 0 2 1 12000 13000
103 0 800 600 12001 12001
104 0 900 600 13001 13001
8 0 0 0 14000 14000
''')
        self.assertEqual(result['published_before_idle']['count'], 1)
        self.assertEqual(result['published_before_idle']['ms_per_frame'], 4)

    def test_dropped_capture_is_rejected(self):
        with self.assertRaises(ValueError):
            self.analyze('8 0 0 0 1000 1000\n8 0 0 0 11000 11000\n', dropped=1)


if __name__ == '__main__':
    unittest.main()
