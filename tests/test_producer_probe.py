import hashlib
import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('producer_probe',ROOT/'tools/codegen/generate-producer-probe.py')
probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)


class ProducerProbeTests(unittest.TestCase):
    def test_pinned_source_roundtrip(self):
        for name,(expected,targets) in probe.SOURCES.items():
            original=(ROOT/'port/recompiled'/name).read_text()
            self.assertEqual(hashlib.sha256(original.encode()).hexdigest(),expected)
            actual=probe.instrument(original,targets)
            self.assertEqual(actual.count('BuzzProducerProbeScope producer_probe_scope'),len(targets))
            restored=actual.replace(probe.INCLUDE,'',1)
            for pc in targets:
                restored=restored.replace(f'\n        BuzzProducerProbeScope producer_probe_scope(ctx,0x{pc:08X});','',1)
            self.assertEqual(original,restored)

    def test_rejects_wrong_input(self):
        source='#include "ppu_recomp.h"\nvoid func_00000010(ppu_context* ctx) {\n}\n'
        with self.assertRaises(ValueError):probe.instrument(source,(0x20,))
        actual=probe.instrument(source,(0x10,))
        with self.assertRaises(ValueError):probe.instrument(actual,(0x10,))
        with self.assertRaises(ValueError):probe.instrument(source+source,(0x10,))


if __name__=='__main__':unittest.main()
