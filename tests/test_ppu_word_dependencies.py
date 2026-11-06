import importlib.util
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('ppu_deps',Path(__file__).resolve().parents[1]/'perf/ppu-word-dependencies.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class PpuDependencies(unittest.TestCase):
    def test_exact_address_zero_value_thread_and_containment(self):
        rows=[(16,7,39,0,20,80),(115,9,0x10000,0x288be0,10,90),
              (116,7,0x10004,0,30,30),(116,7,0x10000,1,40,40),
              (116,7,0x10000,0,70,70)]
        result=m.analyze(rows);self.assertEqual(result['images'],{'39':1})
        x=result['observations'][0]
        self.assertEqual((x['before_job_us'],x['inside_job_us'],x['publish_to_resume_us']),(10,50,20))
        rows[-1]=(116,8,0x10000,0,70,70)
        self.assertEqual(m.analyze(rows)['images'],{'None':1})

    def test_old_address_use_busy_writes_and_missing_writes_stay_unmatched(self):
        rows=[(115,9,0x10000,0,100,200),(116,7,0x10000,0,90,90),
              (116,7,0x10000,1,150,150),(116,7,0x10004,0,160,160)]
        result=m.analyze(rows)
        self.assertEqual((result['matched'],result['unmatched']),(0,1))

    def test_dropped_and_incomplete_capture_are_rejected_or_excluded(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'trace'
            p.write_text('# ps3_ftrace start_us=100 end_us=200 dropped=0\n115 2 10000 1 90 110\n115 2 10000 1 110 190\n')
            self.assertEqual(len(m.read_events(p)),1)
            p.write_text(p.read_text().replace('dropped=0','dropped=1'))
            with self.assertRaises(ValueError):m.read_events(p)


if __name__=='__main__':unittest.main()
