import importlib.util
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('producer',Path(__file__).resolve().parents[1]/'perf/producer-waits.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class ProducerWaitsTest(unittest.TestCase):
    def test_parallel_jobs_are_not_added(self):
        self.assertEqual(m.overlap([(0,4)],[(1,3),(2,5)]),3)
        self.assertEqual(m.overlap([(0,2),(2,4)],[(1,3)]),2)
        self.assertEqual(m.overlap([(0,1)],[(1,2)]),0)

    def test_clips_to_complete_fifo_frames(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'trace';p.write_text('''# ps3_ftrace dropped=0
8 0 0 0 1000 1000
100 0 2 4 0 3000
16 1 29 0 0 2000
16 2 29 0 1500 4000
8 0 0 0 5000 5000
''')
            result=m.analyze(p)
            self.assertEqual(result['idle_ms_per_frame'],2)
            self.assertEqual(result['activity']['image41_job']['overlap_idle_ms_per_frame'],2)
            self.assertEqual(result['activity']['image41_job']['percent_of_idle'],100)
            p.write_text(p.read_text().replace('dropped=0','dropped=1'))
            with self.assertRaises(ValueError):m.analyze(p)
