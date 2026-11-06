"""Synthetic release archives and a fake download: no network, no real upscaler."""
import hashlib
import io
from pathlib import Path
import tempfile
import threading
import unittest
import zipfile

from launcher import upscaler_install as up
from launcher import setup_pipeline as pipeline


def release(extra=(), drop=()):
    data = io.BytesIO()
    with zipfile.ZipFile(data, "w") as archive:
        for member in up.MEMBERS:
            if member not in drop:
                archive.writestr(member, f"content of {member}")
        for member in extra:
            archive.writestr(member, "not installed")
    payload = data.getvalue()
    return payload, hashlib.sha256(payload).hexdigest()


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class UpscalerInstallTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.log = []

    def tearDown(self):
        self.tmp.cleanup()

    def pin(self, payload, digest):
        return dict(sha256=digest, size=len(payload), windows=True)

    def test_local_archive_installs_only_the_fixed_members(self):
        payload, digest = release(extra=("input.jpg", "onepiece_demo.mp4", "../escape.txt"))
        archive = self.root / "release.zip"
        archive.write_bytes(payload)
        destination = up.install(self.root / "runtime", self.log.append, archive=archive, **self.pin(payload, digest))
        self.assertEqual(destination, self.root / "runtime/textures/upscaler")
        installed = sorted(p.relative_to(destination).as_posix() for p in destination.rglob("*") if p.is_file())
        self.assertEqual(installed, sorted(up.MEMBERS))
        self.assertTrue(up.installed(self.root / "runtime"))
        self.assertFalse((self.root / "escape.txt").exists())
        self.assertEqual([p.name for p in destination.parent.iterdir()], ["upscaler"])

    def test_wrong_archive_is_rejected_before_anything_is_written(self):
        payload, digest = release()
        archive = self.root / "release.zip"
        archive.write_bytes(payload + b"tampered")
        with self.assertRaisesRegex(ValueError, "not the pinned"):
            up.install(self.root / "runtime", self.log.append, archive=archive, **self.pin(payload, digest))
        self.assertFalse((self.root / "runtime").exists())

    def test_incomplete_archive_keeps_the_previous_install(self):
        good, good_digest = release()
        (self.root / "good.zip").write_bytes(good)
        up.install(self.root / "runtime", self.log.append, archive=self.root / "good.zip", **self.pin(good, good_digest))
        bad, bad_digest = release(drop=("models/realesrgan-x4plus.bin",))
        (self.root / "bad.zip").write_bytes(bad)
        with self.assertRaisesRegex(ValueError, "lacks: models/realesrgan-x4plus.bin"):
            up.install(self.root / "runtime", self.log.append, archive=self.root / "bad.zip", **self.pin(bad, bad_digest))
        destination = up.target(self.root / "runtime")
        self.assertTrue((destination / "models/realesrgan-x4plus.bin").is_file())
        self.assertEqual([p.name for p in destination.parent.iterdir()], ["upscaler"])

    def test_reinstall_replaces_the_folder_whole(self):
        payload, digest = release()
        (self.root / "r.zip").write_bytes(payload)
        destination = up.install(self.root / "runtime", self.log.append, archive=self.root / "r.zip", **self.pin(payload, digest))
        (destination / "stale.txt").write_text("old")
        up.install(self.root / "runtime", self.log.append, archive=self.root / "r.zip", **self.pin(payload, digest))
        self.assertFalse((destination / "stale.txt").exists())
        self.assertEqual([p.name for p in destination.parent.iterdir()], ["upscaler"])

    def test_download_is_verified_cached_and_reused(self):
        payload, digest = release()
        calls = []
        def opener(url, timeout):
            calls.append(url)
            return Response(payload)
        cache = self.root / "downloads"
        up.install(self.root / "a", self.log.append, cache=cache, opener=opener, **self.pin(payload, digest))
        up.install(self.root / "b", self.log.append, cache=cache, opener=opener, **self.pin(payload, digest))
        self.assertEqual(calls, [up.URL])
        self.assertTrue(up.installed(self.root / "b"))
        self.assertEqual([p.name for p in cache.iterdir()], [up.URL.rsplit("/", 1)[1]])

    def test_bad_or_oversized_download_leaves_no_file(self):
        payload, digest = release()
        cache = self.root / "downloads"
        for body in (payload[:-1] + b"x", payload + b"more"):
            with self.assertRaisesRegex(ValueError, "pinned"):
                up.install(self.root / "runtime", self.log.append, cache=cache,
                           opener=lambda url, timeout, body=body: Response(body), **self.pin(payload, digest))
            self.assertEqual(list(cache.iterdir()), [])
            self.assertFalse((self.root / "runtime/textures/upscaler").exists())

    def test_cancel_stops_without_an_install(self):
        payload, digest = release()
        cancel = threading.Event()
        cancel.set()
        with self.assertRaises(up.Cancelled):
            up.install(self.root / "runtime", self.log.append, cancel, cache=self.root / "downloads",
                       opener=lambda url, timeout: Response(payload), **self.pin(payload, digest))
        self.assertFalse(up.installed(self.root / "runtime"))

    def test_other_platforms_are_told_to_install_by_hand(self):
        with self.assertRaisesRegex(ValueError, "Windows and Linux"):
            up.install(self.root / "runtime", self.log.append, cache=self.root, windows=False)

    def test_preparation_choice_is_validated_and_defaults_off(self):
        self.assertFalse(pipeline.Inputs(game="g", firmware="f").upscaler)
        with self.assertRaisesRegex(ValueError, "upscaler choice"):
            pipeline.Inputs(game="g", firmware="f", upscaler="yes").validate()


if __name__ == "__main__":
    unittest.main()
