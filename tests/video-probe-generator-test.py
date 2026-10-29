import importlib.util
from pathlib import Path
import unittest

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('video_probe_generator',root/'generate-video-probe.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


class Generator(unittest.TestCase):
    def test_real_source_preserved(self):
        for name,targets in module.SOURCES:
            source=(root/'port/recompiled'/name).read_text()
            result=module.instrument(source,targets)
            for pc in targets:
                marker=f'\n        BuzzVideoProbeScope video_probe_scope(ctx,0x{pc:08X});'
                self.assertEqual(result.count(marker),1)
                result=result.replace(marker,'')
            result=result.replace('\n#include "../../port/video_probe.h"','')
            self.assertEqual(result,source)

    def test_rejects_wrong_or_instrumented_source(self):
        with self.assertRaises(ValueError):module.instrument('')
        with self.assertRaises(ValueError):module.instrument('BuzzVideoProbeScope')


unittest.main()
