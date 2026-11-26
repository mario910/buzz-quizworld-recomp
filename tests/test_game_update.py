import hashlib
import json
import os
from pathlib import Path
import tempfile
import threading
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
            self.assertEqual(update_sources.latest_sources(self.build, None, print), (revision, "v1.2.3"))
            self.assertEqual((self.build / "tools/build-linux.py").read_bytes(), b"source")
            self.assertEqual(update_sources.latest_sources(self.dist, revision, print), (revision, "v1.2.3"))
            self.assertFalse((self.dist / "tools").exists())
            release["assets"][0]["digest"] = "sha256:" + "0" * 64
            with self.assertRaises(ValueError):
                update_sources.latest_sources(self.dist, revision, print)

    def test_automatic_update_of_legacy_install_is_offline_and_runs_once(self):
        old, new = "1" * 40, "2" * 40
        runtime = self.dist / "runtime"
        (runtime / "EBOOT.elf").write_bytes(b"elf")
        elf_hash = hashlib.sha256(b"elf").hexdigest()
        (runtime / "dev_flash").mkdir()
        disc = runtime / "disc/PS3_GAME"
        disc.mkdir(parents=True)
        (disc / "PARAM.SFO").touch()
        inventory = {"revision": new, "releases": {old: "v0.3.9", new: "v0.3.10"},
                               "files": ["source.txt"]}
        (self.root / "release-sources.json").write_text(json.dumps(inventory))
        (self.root / "source.txt").write_text("new bundled sources")
        for path in (self.dist, self.build):
            manifest = path / "game-build.json"
            data = json.loads(manifest.read_text())
            data["elf_sha256"] = elf_hash
            if path == self.dist:
                data["source_revision"] = old
            manifest.write_text(json.dumps(data))

        def build(project, workspace, *args):
            self.assertEqual((workspace / "source.txt").read_text(), "new bundled sources")
            return self.exe

        with patch.object(game_update.model, "title_id"), patch.object(game_update, "identify"), \
                patch.object(game_update, "build_workspace", side_effect=build) as compiler, \
                patch.object(update_sources, "release_url", side_effect=AssertionError):
            for _ in range(2):
                game_update.update_game(self.root, self.dist, 4, lambda line: None,
                                        threading.Event(), bundled_only=True)
            compiler.assert_called_once()
        manifest = game_update.manifest_for(self.dist / self.name, elf_hash)
        self.assertEqual((manifest["source_revision"], manifest["source_release"]), (new, "v0.3.10"))
        self.assertEqual(self.profile.read_bytes(), b"custom")

    def test_bundled_update_skips_unknown_builds_and_downgrades(self):
        revision = "a" * 40
        (self.root / "release-sources.json").write_text(json.dumps(
            {"revision": revision, "releases": {revision: "v0.3.9"}}))
        with patch.object(update_sources, "copy_sources") as copy:
            for release in (None, "v0.3.10", "v0.3.9"):
                installed = {"source_revision": "b" * 40, "source_release": release}
                update_sources.bundled_upgrade(self.root, self.build, installed)
            copy.assert_not_called()

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
            tip, release = update_sources.select(local, self.build, None, lambda line: None)
            self.assertIsNone(release)
            self.assertEqual(tip, git(upstream, "rev-parse", "HEAD"))
            self.assertEqual((self.build / "selected").read_text(), "after")
            self.assertEqual((local / "version").read_text(), "before")
            (local / "version").write_text("my changes")
            self.assertEqual(update_sources.select(local, self.build, tip, lambda line: None), (None, None))
            self.assertEqual((self.build / "selected").read_text(), "my changes")
