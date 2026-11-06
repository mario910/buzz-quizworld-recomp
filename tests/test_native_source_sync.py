import importlib.util
import hashlib
import os
from pathlib import Path
import tempfile
import time
import unittest

spec=importlib.util.spec_from_file_location('sync_native',Path(__file__).resolve().parents[1]/'perf/sync-native-sources.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class NativeSourceSyncTest(unittest.TestCase):
    def test_generated_host_bindings_are_recorded_without_overwriting(self):
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'source';workspace=Path(temp)/'prepared'
            profile=workspace/'analysis/host-disc100/port'
            for path in (source/'port',source/'cmake',profile):path.mkdir(parents=True)
            for name in ('stubs','boot_trace','frontend_policy','video_queue','edge_decompress'):
                (source/'port'/f'{name}.cpp').write_text('EU102 template')
                (profile/f'{name}.cpp').write_text('EU100 bound input')
            cache=workspace/'CMakeCache.txt'
            cache.write_text(f'PORT_HOST_PROFILE_DIR:PATH={profile}\n')
            result=m.sync_native(source,workspace,cache)
            self.assertEqual(len(result['generated_host_sources']),5)
            self.assertEqual(result['generated_host_sources'][str((profile/'stubs.cpp').resolve())],
                             hashlib.sha256(b'EU100 bound input').hexdigest())
            self.assertEqual((profile/'stubs.cpp').read_text(),'EU100 bound input')
            (source/'port/stubs.cpp').write_text('new template')
            (profile/'video_queue.cpp').unlink()
            with self.assertRaises(FileNotFoundError):m.sync_native(source,workspace,cache)
            self.assertEqual((workspace/'port/stubs.cpp').read_text(),'EU102 template')

    def test_rollback_gets_a_fresh_timestamp(self):
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'source';workspace=Path(temp)/'prepared'
            for path in [source/'port',source/'cmake',workspace/'port']:
                path.mkdir(parents=True)
            header=source/'port/kernel.h';header.write_text('restored implementation')
            os.utime(header,(1_000_000_000,1_000_000_000))
            dest=workspace/'port/kernel.h';dest.write_text('experimental implementation')
            old_object_time=time.time_ns()-2_000_000_000
            os.utime(dest,ns=(old_object_time,old_object_time))
            result=m.sync_native(source,workspace)
            self.assertEqual(result['changed'],['port/kernel.h'])
            self.assertEqual(dest.read_bytes(),header.read_bytes())
            self.assertGreater(dest.stat().st_mtime_ns,old_object_time)
            modified=dest.stat().st_mtime_ns
            self.assertEqual(m.sync_native(source,workspace)['changed'],[])
            self.assertEqual(dest.stat().st_mtime_ns,modified)

    def test_same_workspace_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(ValueError):m.sync_native(temp,temp)


if __name__=='__main__':unittest.main()
