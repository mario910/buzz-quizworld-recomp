"""Synthetic tests only: never open the user's game, run it or access a remote."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


audit = module("audit_publication", ROOT / "tools/audit-publication.py")
verify = module("verify_game", ROOT / "tools/verify-game.py")


class PublicationTests(unittest.TestCase):
    def test_paths(self):
        for path in ("analysis/job.cpp", "port/recompiled/a.cpp", "port/spu/a.c", "runtime/save.dat", "extra/BOOT.ELF", "buzz-paths.json", "x/.env.secret"):
            self.assertIsNotNone(audit.path_issue(path, "port"), path)
        self.assertIsNone(audit.path_issue("runtime/spu/spu_context.h", "sdk"))
        self.assertIsNotNone(audit.path_issue("fw_spu/lifted/a.c", "sdk"))
        self.assertIsNone(audit.path_issue("config/buzz-paths.example.json", "port"))

    def test_all_ancestors_not_only_tip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*args):
                return subprocess.check_output(["git", "-C", directory, *args], stderr=subprocess.STDOUT)
            git("init", "-q")
            git("config", "user.name", "Synthetic Test")
            git("config", "user.email", "test@example.invalid")
            (root / "safe.c").write_text("/* synthetic source */\n")
            git("add", "safe.c")
            git("commit", "-qm", "safe")
            safe = git("rev-parse", "HEAD").decode().strip()
            (root / "bad.elf").write_text("/* synthetic source */\n")
            git("add", "bad.elf")
            git("commit", "-qm", "same blob under forbidden name")
            git("rm", "bad.elf")
            git("commit", "-qm", "remove")
            self.assertEqual(audit.audit(root, safe, "port")["issues"], [])
            report = audit.audit(root, "HEAD", "port")
            self.assertTrue(any(item["path"] == "bad.elf" for item in report["issues"]))
            tip = audit.audit(root, "HEAD", "port", "tip")
            self.assertEqual(tip["scope"], "tip")
            self.assertEqual(tip["issues"], [])  # deletion is NOT a clean history
            # Credentials must never be printed back in diagnostics.
            token = "gh" + "p_" + "S" * 36
            (root / "config.txt").write_text(token)
            git("add", "config.txt")
            git("commit", "-qm", "synthetic credential")
            report = audit.audit(root, "HEAD", "port")
            self.assertTrue(any("credential" in item["reason"] for item in report["issues"]))
            self.assertNotIn(token, str(report))
            tip = audit.audit(root, "HEAD", "port", "tip")
            self.assertTrue(any("credential" in item["reason"] for item in tip["issues"]))
            self.assertNotIn(token, str(tip))

    def test_elf_rejected_before_any_other_path_is_used(self):
        with tempfile.TemporaryDirectory() as directory:
            elf = Path(directory) / "EBOOT.elf"
            elf.write_bytes(b"synthetic fixture")
            paths = {"eboot_elf": str(elf)}
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                verify.verify(paths, elf_only=True)
            verify.verify(paths, elf_only=True, expected_hash=hashlib.sha256(elf.read_bytes()).hexdigest())
            self.assertEqual(sorted(p.name for p in Path(directory).iterdir()), ["EBOOT.elf"])


if __name__ == "__main__":
    unittest.main()
