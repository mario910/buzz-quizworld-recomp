"""Public entry points stay usable after moving scripts out of the root."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


class SourceLayoutTests(unittest.TestCase):
    def test_player_entrypoints_and_examples(self):
        self.assertEqual(sorted(p.name for p in ROOT.glob('*.cmd')),
                         ['Launch-Buzz.cmd', 'Prepare-Buzz.cmd'])
        for pattern in ('*.py', '*.pyw', '*.ps1'):
            self.assertEqual(list(ROOT.glob(pattern)), [], pattern)
        for relative in ('launcher/start.pyw', 'scripts/Prepare-Buzz.ps1',
                         'scripts/Build-Buzz.ps1', 'scripts/run-windows.ps1',
                         'config/buzz-input.example.ini', 'config/buzz-paths.example.json'):
            self.assertTrue((ROOT / relative).is_file(), relative)
        from launcher.paths import ROOT as config_root
        self.assertEqual(config_root, ROOT)

    def test_cli_imports_do_not_depend_on_working_directory(self):
        with tempfile.TemporaryDirectory() as cwd:
            for relative in ('tools/verify-game.py',
                             'tools/codegen/build-disc100-reference.py',
                             'tools/inspect/compare-game-versions.py'):
                result = subprocess.run([sys.executable, str(ROOT / relative), '--help'],
                                        cwd=cwd, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('usage:', result.stdout.lower())

    def test_public_python_syntax(self):
        for directory in ('launcher', 'tools', 'perf', 'tests'):
            for path in (ROOT / directory).rglob('*.py'):
                compile(path.read_text(encoding='utf-8-sig'), str(path), 'exec')


if __name__ == '__main__':
    unittest.main()
