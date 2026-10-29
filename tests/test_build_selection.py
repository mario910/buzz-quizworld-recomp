"""Synthetic build selection fixtures; no executable is launched."""
import json
from pathlib import Path
import tempfile
import unittest

from launcher.active_build import matching_workspace, ready_workspace
from launcher.setup_pipeline import fingerprint


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def build(self, name, digest):
        session = self.root / "runtime/setup/sessions" / name
        work = session / "workspace"
        exe = work / "build-windows/BuzzQuizWorldRecomp.exe"
        exe.parent.mkdir(parents=True)
        exe.write_bytes(b"synthetic-not-executable-" + name.encode())
        identity = fingerprint(exe)
        (exe.parent / "game-build.json").write_text(json.dumps(dict(elf_sha256=digest, exe_sha256=identity["sha256"])))
        (session / "state.json").write_text(json.dumps(dict(built_exe=identity, profile=dict(elf_sha256=digest, title_id="BCES00647"))))
        return work

    def test_selects_matching_sibling_not_last_version_or_version_string(self):
        base = self.build("base", "a" * 64)
        update = self.build("update", "b" * 64)
        self.assertEqual(matching_workspace(base, "b" * 64, "BCES00647"), update)
        self.assertEqual(matching_workspace(update, "a" * 64, "BCES00647"), base)
        self.assertIsNone(matching_workspace(base, "c" * 64, "BCES00647"))

    def test_changed_executable_rejected(self):
        work = self.build("base", "a" * 64)
        (work / "build-windows/BuzzQuizWorldRecomp.exe").write_bytes(b"changed")
        self.assertIsNone(matching_workspace(self.root, "a" * 64, "BCES00647"))

    def test_ready_marker_is_scoped_and_hash_checked(self):
        work = self.build("base", "a" * 64)
        ready = self.root / "runtime/setup/last-ready.json"
        ready.write_text(json.dumps(dict(session=str(work.parent), workspace=str(work))))
        self.assertEqual(ready_workspace(self.root), work)
        ready.write_text(json.dumps(dict(session=str(self.root), workspace=str(work))))
        self.assertIsNone(ready_workspace(self.root))


if __name__ == "__main__":
    unittest.main()
