import contextlib
import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools import regional_ppu, studio_adapter


class StudioAdapterTests(unittest.TestCase):
    def test_legacy_generated_sources_and_completion_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            # An earlier unrelated translation unit must not stop the scan.
            (root / 'ppu_recomp_000.cpp').write_bytes(b'/* cp1252 \x97 comment */\n')
            (root / 'ppu_recomp_001.cpp').write_text('/* replaced by completion */\n', encoding='utf-8')
            completion = root / 'completion' / 'ppu_recomp_001.cpp'
            completion.parent.mkdir()
            source = (
                '#include "ppu_recomp.h"\n'
                'void func_00000001(ppu_context* ctx) {\n'
                'loc_00000010:\n'
                '        ctx->gpr[0] = ppc_rlw64(ctx->gpr[8], 4, 0, 27, 0, 0);\n'
                '        /* mixed comments: \udc97 \u2014 */\n'
                f'        {studio_adapter.ANCHOR}\n'
                f'{studio_adapter.SWITCH} {{\n'
                '        case 0x00000020u: goto loc_00000020;\n'
                '        }\n'
                'loc_00000020:\n'
                + '        /* TODO: .word 0x00000000 */;\n' * 25
                + '}'
            )
            completion.write_bytes(source.encode('utf-8', errors='surrogateescape'))
            out = root / 'out'

            with contextlib.redirect_stderr(io.StringIO()):
                self.assertIsNone(studio_adapter.generate_studio(root, out, completion))
            self.assertFalse(out.exists())

            # Use a test-only fingerprint for this synthetic jump table.
            normalized = source.split('\n', 1)[1].replace('func_00000001', 'callee_0')
            normalized = normalized.replace('loc_00000010', 'loc_0')
            normalized = normalized.replace('loc_00000020', 'loc_1').replace('case 0x00000020', 'case 0x1')
            normalized = normalized.replace('/* TODO: .word 0x00000000 */;', 'table 1')
            fingerprint = hashlib.sha256(normalized.encode('utf-8', errors='surrogateescape')).hexdigest()
            with patch.object(studio_adapter, 'DISPATCHER', fingerprint):
                result = studio_adapter.generate_studio(root, out, completion)
            self.assertEqual(result, out / completion.name)
            patched = result.read_bytes()
            self.assertIn(b'#include "studio_effects.h"', patched)
            self.assertIn(b'buzz::skip_studio_effect', patched)
            self.assertIn(b'/* mixed comments: \x97 \xe2\x80\x94 */', patched)

            # Desktop builds use the regional generator and also reread the
            # generated translation when reusing a preparation workspace.
            completion.write_bytes(b'/* cp1252 \x97 header */\n#include "ppu_vmx.h"\n' + completion.read_bytes())
            with patch.object(studio_adapter, 'DISPATCHER', fingerprint), contextlib.redirect_stdout(io.StringIO()):
                for _ in range(2):
                    sources, matched = regional_ppu.generate_regional(root, out, completion)
                    self.assertTrue(matched)
                    self.assertEqual(sources, [out / completion.name])
                    patched = sources[0].read_bytes()
                    self.assertIn(b'buzz::ppu_scene_command', patched)
                    self.assertIn(b'/* cp1252 \x97 header */', patched)
