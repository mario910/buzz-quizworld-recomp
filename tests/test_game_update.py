import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from launcher import game_update, update_sources


class UpdateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.dist, self.build = self.root / "dist", self.root / "build"
        self.dist.mkdir()
        self.build.mkdir()
        (self.dist / "runtime").mkdir()
        self.name = "BuzzQuizWorldRecomp.exe" if os.name == "nt" else "BuzzQuizWorldRecomp"
        self.exe = self.build / self.name
        self.elf = "d" * 64
        for loc, contents in [(self.dist, b"old"), (self.build, b"new")]:
            (loc / self.name).write_bytes(contents)
            (loc / "video.dll").write_bytes(contents + b"dll")
            (loc / "game-build.json").write_text(json.dumps(dict(standalone=True, renderer="vulkan",
                elf_sha256=self.elf, exe_sha256=hashlib.sha256(contents).hexdigest())))
        (self.build / "extra.dll").write_bytes(b"new dll")
        self.profile = self.dist / "runtime/profile"
        self.profile.write_bytes(b"custom")

    def test_install_executable_manifest_and_libraries_preserves_runtime(self):
        game_update.install_build(self.dist, self.exe, self.elf)
        game_update.manifest_for(self.dist / self.name, self.elf)
        self.assertEqual(self.profile.read_bytes(), b"custom")
        self.assertEqual((self.dist / "video.dll").read_bytes(), b"newdll")
        self.assertEqual((self.dist / self.name).read_bytes(), b"new")

    def test_wrong_output_leaves_installation_intact(self):
        self.exe.write_bytes(b"wrong")
        with self.assertRaises(ValueError):
            game_update.install_build(self.dist, self.exe, self.elf)
        self.assertEqual((self.dist / self.name).read_bytes(), b"old")

    def test_mid_install_error_or_interruption_rolls_back_all_files(self):
        original = os.replace
        for failure in (PermissionError, KeyboardInterrupt):
            def interrupt(source, target):
                if Path(target) == self.dist / self.name and Path(source).parent.name.startswith(".update-"):
                    raise failure()
                return original(source, target)
            with patch.object(game_update.os, "replace", interrupt), self.assertRaises(failure):
                game_update.install_build(self.dist, self.exe, self.elf)
            game_update.recover_installation(self.dist)
            game_update.manifest_for(self.dist / self.name, self.elf)
            self.assertEqual((self.dist / "video.dll").read_bytes(), b"olddll")
            self.assertFalse((self.dist / "extra.dll").exists())

    def test_repeat_update_uses_verified_release_revision(self):
        import io
        import zipfile
        archive = io.BytesIO()
        revision = "a" * 40
        with zipfile.ZipFile(archive, "w") as package:
            package.writestr("BuzzPrepare/build-files/release-sources.json", json.dumps(
                {"revision": revision, "files": ["tools/build-linux.py"]}))
            package.writestr("BuzzPrepare/build-files/tools/build-linux.py", b"source")
        data = archive.getvalue()
        release = {"tag_name": "v1.2.3", "assets": [{"name": update_sources.ASSET,
                   "digest": "sha256:" + hashlib.sha256(data).hexdigest(), "size": len(data)}]}
        def get(url):
            return io.BytesIO(json.dumps(release).encode() if "api.github.com" in url else data)
        with patch.object(update_sources, "release_url", get):
            self.assertEqual(update_sources.latest_sources(self.build, None, print), revision)
            self.assertEqual((self.build / "tools/build-linux.py").read_bytes(), b"source")
            self.assertEqual(update_sources.latest_sources(self.dist, revision, print), revision)
            self.assertFalse((self.dist / "tools").exists())
            release["assets"][0]["digest"] = "sha256:" + "0" * 64
            with self.assertRaises(ValueError):
                update_sources.latest_sources(self.dist, revision, print)

    def test_clean_checkout_builds_tracked_update_without_moving_branch(self):
        import subprocess
        upstream, local = self.root / "upstream", self.root / "local"
        def git(repo, *args):
            return subprocess.check_output(["git", "-C", str(repo), *args], text=True,
                                           stderr=subprocess.DEVNULL).strip()
        upstream.mkdir()
        git(upstream, "init", "-b", "main")
        for revision in ("before", "after"):
            (upstream / "version").write_text(revision)
            git(upstream, "add", "version")
            git(upstream, "-c", "user.name=Test", "-c", "user.email=test@invalid",
                "commit", "-m", revision)
            if revision == "before":
                git(self.root, "clone", str(upstream), str(local))
        def snapshot(src, dst):
            (dst / "selected").write_bytes((src / "version").read_bytes())
        with patch.object(update_sources, "copy_sources", snapshot):
            tip = update_sources.select(local, self.build, None, lambda line: None)
            self.assertEqual(tip, git(upstream, "rev-parse", "HEAD"))
            self.assertEqual((self.build / "selected").read_text(), "after")
            self.assertEqual((local / "version").read_text(), "before")
            (local / "version").write_text("my changes")
            self.assertIsNone(update_sources.select(local, self.build, tip, lambda line: None))
            self.assertEqual((self.build / "selected").read_text(), "my changes")
