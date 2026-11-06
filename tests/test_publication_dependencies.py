import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('dependencies',Path(__file__).resolve().parents[1]/'perf/publication-dependencies.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class DependencyTests(unittest.TestCase):
    def test_exact_addresses_and_job_owner(self):
        events=[(8,0,0,0,0,0),(8,0,0,0,10,10),(100,0,0,0,1,6),
                (110,0,0x100,0x200,1,1),(110,0,0x100,0x200,2,2),
                (112,7,0x104,0x300,2,2), # adjacent write is not a dependency
                (112,7,0x100,0x200,2.5,2.5), # unchanged park is not publication
                (112,7,0x100,0x300,3,3),(114,0,0x100,0x300,6,6),
                (16,7,39,0x900,2,4)]
        r=m.analyze(events)
        self.assertEqual(r['parks']['matches'],1)
        self.assertEqual(r['parks']['images'],{'39':1})
        self.assertEqual(r['parks']['idle_after_publication_ms_per_frame'],3)
        self.assertEqual(r['parks']['same_word_matches'],1)

    def test_jts_needs_full_changed_command_and_containing_job(self):
        e=[(8,0,0,0,0,0),(8,0,0,0,10,10),(18,2,0x100,0x200,1,5),
           (112,7,0x200,8,2,2),(113,7,0x204,0x12,2,2),
           (112,7,0x200,8,3,3),(113,7,0x204,0x13,3,3),
           (16,7,39,0,0,1)]
        r=m.analyze(e)
        self.assertEqual(r['chain_pc']['matches'],1)
        self.assertEqual(r['chain_pc']['images'],{'None':1})
        self.assertEqual(r['chain_details'][0]['published'],3)

    def test_unmatched_is_not_assumed_idle_savings(self):
        r=m.analyze([(8,0,0,0,0,0),(8,0,0,0,10,10),(110,0,1,2,1,1),
                     (114,0,1,3,5,5),(18,2,3,4,2,4)])
        self.assertEqual(r['parks']['unmatched'],1)
        self.assertEqual(r['parks']['idle_after_publication_ms_per_frame'],0)
        self.assertEqual(r['chain_pc']['unmatched_wait_spans'],1)

    def test_ring_address_reuse_is_not_a_dependency(self):
        e=[(8,0,0,0,0,0),(8,0,0,0,5,5),(8,0,0,0,10,10),
           (110,0,0x100,0x200,1,1),
           (112,7,0x100,0x300,6,6),(114,0,0x100,0x300,7,7)]
        r=m.analyze(e)
        self.assertEqual(r['parks']['matches'],0)
        self.assertEqual(r['parks']['unmatched'],1)
