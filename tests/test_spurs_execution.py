import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('spurs_execution',Path(__file__).resolve().parents[1]/'perf/spurs-execution.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class ExecutionTraceTest(unittest.TestCase):
    def test_native_thread_identity_and_union_of_nested_waits(self):
        rows=[(117,7,39,300000,100,200),(118,7,3,0,110,130),
              (118,7,3,0,120,140),(118,8,3,0,100,200),(100,0,0,0,150,250)]
        out=m.summarize(rows)['39']
        self.assertEqual(out['blocked_channel_us'],30)
        self.assertEqual(out['rsx_idle_overlap_us'],50)
        self.assertEqual(out['cpu_units_per_wall_ns']['mean'],3)

    def test_unknown_cpu_counter_is_not_a_wrapped_measurement(self):
        out=m.summarize([(117,1,41,0xffffffff,10,20)])['41']
        self.assertEqual(out['invalid_cpu_counts'],1)
        self.assertEqual(out['cpu_units'],0)
        self.assertIsNone(out['cpu_units_per_wall_ns'])

    def test_capture_edges_and_record_loss(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'trace'
            p.write_text('# start_us=100 end_us=200 dropped=0\n117 7 27 100 90 150\n117 7 27 100 110 190\n')
            self.assertEqual(len(m.read(p)),1)
            p.write_text(p.read_text().replace('dropped=0','dropped=1'))
            with self.assertRaises(ValueError):m.read(p)

if __name__=='__main__':unittest.main()
