"""Optional cross-language migration checks; native tests also run via CTest."""
from dataclasses import asdict
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from launcher import model


@unittest.skipUnless(os.environ.get('BUZZ_NATIVE_LAUNCHER_ORACLE'), 'Build the native launcher and set BUZZ_NATIVE_LAUNCHER_ORACLE')
class NativeLauncherMigrationTests(unittest.TestCase):
    def oracle(self, data):
        return subprocess.run([os.environ['BUZZ_NATIVE_LAUNCHER_ORACLE'], '--settings-oracle'],
                              input=json.dumps(data, ensure_ascii=False).encode('utf-8'),
                              capture_output=True, timeout=10)

    def test_settings_and_game_ini_compatibility(self):
        for slots in (4, 8):
            settings = model.Settings(places=slots)
            settings.players[0].name = 'Żółw Adam'
            settings.players[0].keys = ['F08', '0013', '0x2A', 'RSHIFT', '+12']
            settings.pads[0].player = None
            settings.gamepads = True
            result = self.oracle(asdict(settings))
            self.assertEqual(result.returncode, 0, result.stderr)
            native = json.loads(result.stdout)
            self.assertEqual(native['settings'], asdict(settings))
            # Comments identify the writer; all values must be identical.
            values = lambda text: [s for s in text.splitlines() if s and not s.startswith('#')]
            self.assertEqual(values(native['input_ini']), values(settings.input_ini()))

    def test_invalid_config_is_not_silently_repaired(self):
        for mutate in (lambda d: d.update(version=True),
                       lambda d: d.update(pulse_ms=9),
                       lambda d: d['players'][0]['keys'].__setitem__(1, 'Space'),
                       lambda d: d['pads'][1].update(player=0)):
            data = asdict(model.Settings())
            mutate(data)
            result = self.oracle(data)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
