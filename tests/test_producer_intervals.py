import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('producer_scopes',Path(__file__).resolve().parents[1]/'tools/inspect/inspect-producer-scopes.py')
report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)


class ProducerIntervals(unittest.TestCase):
    def test_nesting_not_parent_subtraction(self):
        scopes=[(26,1,10,0,0,100),(26,1,20,0,10,30),(26,1,30,0,15,20),(26,1,20,0,50,70),
                (26,2,99,0,0,100)]
        actual,crossings=report.exclusive_scopes(scopes)
        self.assertFalse(crossings)
        self.assertEqual(actual[(1,10)],[(0,10),(30,50),(70,100)])
        self.assertEqual(actual[(1,20)],[(10,15),(20,30),(50,70)])
        self.assertEqual(actual[(1,30)],[(15,20)])
        self.assertEqual(actual[(2,99)],[(0,100)])
        self.assertEqual(sum(report.intervals.elapsed(v) for (tid,pc),v in actual.items() if tid==1),100)

    def test_crossing_is_rejected(self):
        _,crossings=report.exclusive_scopes([(26,1,10,0,0,30),(26,1,20,0,20,50)])
        self.assertEqual(crossings,1)


if __name__=='__main__':unittest.main()
