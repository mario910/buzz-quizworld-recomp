"""The diagnostic must reject disabled/reset/malformed counters, not infer zero from silence."""
import importlib.util
from pathlib import Path
import struct
import unittest

path=Path(__file__).resolve().parents[1]/'perf/observe-spu-interpreter.py'
spec=importlib.util.spec_from_file_location('interpreter_observer',path)
observer=importlib.util.module_from_spec(spec);spec.loader.exec_module(observer)


def values(interval=-1,total=0,count=0,image=41):
    return dict(s_is=struct.pack('<i',interval),s_tot=struct.pack('<Q',total),s_t0=bytes(8),
        s_img=struct.pack('<64i',image,*([0]*63)),s_page=struct.pack('<64I',0x9000,*([0]*63)),s_cnt=struct.pack('<64q',count,*([0]*63)))


class ObserverTests(unittest.TestCase):
    def test_never_entered_interpreter(self):self.assertTrue(observer.interpret(values(),60)['all_images_zero_proven'])
    def test_initialized_but_zero(self):self.assertTrue(observer.interpret(values(3600),60)['all_images_zero_proven'])
    def test_sub_reporting_threshold_is_observed(self):
        result=observer.interpret(values(3600,1,1),60)
        self.assertFalse(result['all_images_zero_proven']);self.assertEqual(result['buckets'],[dict(image=41,page=0x9000,observed=1)])
    def test_positive_total_with_unlisted_buckets_is_not_zero(self):self.assertFalse(observer.interpret(values(3600,1),60)['all_images_zero_proven'])
    def test_disabled_or_short_reset_interval(self):
        for interval in (0,1,10,3599):
            with self.subTest(interval=interval),self.assertRaises(ValueError):observer.interpret(values(interval),60)
    def test_reset_window(self):
        for seconds in (-1,3600,4000):
            with self.subTest(seconds=seconds),self.assertRaises(ValueError):observer.interpret(values(3600),seconds)
    def test_inconsistent_initial_or_zero_counters(self):
        for sample in (values(-1,1,1),values(-1,0,1),values(3600,0,1),values(3600,1,-1)):
            with self.assertRaises(ValueError):observer.interpret(sample,60)
    def test_partial_snapshot(self):
        sample=values();sample['s_cnt']=sample['s_cnt'][:-1]
        with self.assertRaises(ValueError):observer.interpret(sample,60)


if __name__=='__main__':unittest.main()
