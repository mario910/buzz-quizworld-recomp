import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
REPORTER = ROOT / 'perf/geometry-rip-report.py'
ROLES = {40: 'buzz_job_abe1', 41: 'buzz_job_6eba', 43: 'buzz_job_19d0',
         45: 'disc_job_007d0980', 47: 'disc_job_007f4b80'}


class RipRoleTests(unittest.TestCase):
    def report(self, root, samples, *options):
        mapping = [' Preferred load address is 0000000140000000']
        for offset, prefix in enumerate(ROLES.values()):
            rva = 0x1000 + offset * 0x100
            mapping.append(f' 0001:{rva-0x1000:08x} {prefix}_spu_func_00001234 {0x140000000+rva:016x} f lift.obj')
        (root / 'test.map').write_text('\n'.join(mapping))
        (root / 'samples.txt').write_text(samples)
        result = subprocess.run([sys.executable, str(REPORTER), str(root / 'samples.txt'),
            '--map', str(root / 'test.map'), '--output', str(root / 'report.json'), *options],
            capture_output=True, text=True)
        path = root / 'report.json'
        return result, json.loads(path.read_text()) if path.exists() else None

    def test_each_role_excludes_other_images_and_uses_lane_denominator(self):
        for index, (image, prefix) in enumerate(ROLES.items()):
            with self.subTest(image=image), tempfile.TemporaryDirectory() as temp:
                rva = 0x1000 + index * 0x100
                other = 0x1000 + ((index + 1) % len(ROLES)) * 0x100
                samples = f'S 7 {rva:x}\n' * 3 + f'S 7 {other:x}\nS 7 -kernel32.dll\n'
                result, report = self.report(Path(temp), samples, '--image', str(image), '--lanes', '7')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(report[f'image{image}_samples'], 3)
                self.assertEqual(report['lane_samples'], 5)
                self.assertEqual(len(report['ranking']), 1)
                row = report['ranking'][0]
                self.assertEqual(row['symbol'], prefix + '_spu_func_00001234')
                self.assertEqual(row['lane_share_percent'], 60)
                self.assertEqual(row[f'image{image}_share_percent'], 100)
                self.assertFalse(report['automatic_lanes'])

    def test_default_retains_image41_schema(self):
        with tempfile.TemporaryDirectory() as temp:
            result, report = self.report(Path(temp), 'S 7 1100\n', '--lanes', '7')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(report['image41_samples'], 1)
            self.assertEqual(report['candidates'], [{'thread': '7', 'image41_samples': 1}])
            self.assertEqual(report['ranking'][0]['image41_share_percent'], 100)

    def test_automatic_lanes_follow_requested_role(self):
        with tempfile.TemporaryDirectory() as temp:
            result, report = self.report(Path(temp), 'S 7 1000\n' * 20 + 'S 9 1100\n' * 40,
                '--image', '40')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(report['lanes'], ['7'])
            self.assertEqual(report['image40_samples'], 20)

    def test_no_matching_target_rejects_automatic_report(self):
        with tempfile.TemporaryDirectory() as temp:
            result, report = self.report(Path(temp), 'S 7 1100\n' * 40, '--image', '40')
            self.assertNotEqual(result.returncode, 0)
            self.assertIsNone(report)

    def test_missing_explicit_lane_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            result, report = self.report(Path(temp), 'S 7 1000\n', '--image', '40', '--lanes', '8')
            self.assertNotEqual(result.returncode, 0)
            self.assertIsNone(report)


if __name__ == '__main__':
    unittest.main()
