"""Synthetic ISO/SELF/process fixtures: no games, real emulator or UI input."""
from dataclasses import replace
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import threading
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from urllib.error import URLError

import pycdlib
from Crypto.Cipher import AES
from launcher import disc_keys
from launcher import setup_pipeline as pipeline
from launcher.disc_image import PS3_MARKER, SECTOR_SIZE, _SectorReader, encrypted_regions
from launcher.game_profiles import PROFILES, identify


def sfo(title="BCES00647"):
    key, value = b"TITLE_ID\0", title.encode() + b"\0"
    return (b"\0PSF" + struct.pack("<IIII", 0x101, 36, 36 + len(key), 1)
            + struct.pack("<HHIII", 0, 0x204, len(value), len(value), 0) + key + value)


def make_iso(path, title="BCES00647", udf=False, joliet=False, *, eboot=b"SCE\0synthetic", icon=None):
    image = pycdlib.PyCdlib()
    options = dict(interchange_level=3)
    if udf:
        options["udf"] = "2.60"
    if joliet:
        options["joliet"] = 3
    image.new(**options)
    extra = lambda path: {**({"udf_path": path} if udf else {}), **({"joliet_path": path} if joliet else {})}
    image.add_directory(iso_path="/PS3_GAME", **extra("/PS3_GAME"))
    image.add_directory(iso_path="/PS3_GAME/USRDIR", **extra("/PS3_GAME/USRDIR"))
    buffers = []
    files = [("PARAM.SFO", sfo(title)), ("USRDIR/EBOOT.BIN", eboot)]
    if icon is not None:
        files.append(("ICON0.PNG", icon))
    for name, data in files:
        buffer = io.BytesIO(data)
        buffers.append(buffer)
        image.add_fp(buffer, len(data), iso_path="/PS3_GAME/" + name + ";1",
                     **({"udf_path": "/PS3_GAME/" + name} if udf else {}),
                     **({"joliet_path": "/PS3_GAME/" + name + ";1"} if joliet else {}))
    image.write(str(path))
    image.close()


def cipher_sectors(data, key, first, stop):
    for lba in range(first, stop):
        sector = slice(lba * SECTOR_SIZE, (lba + 1) * SECTOR_SIZE)
        data[sector] = AES.new(key, AES.MODE_CBC, lba.to_bytes(16, "big")).encrypt(data[sector])


def make_encrypted_iso(path, key, eboot, icon, **options):
    make_iso(path, eboot=eboot, icon=icon, **options)
    image = pycdlib.PyCdlib()
    image.open(str(path))
    first, stop = path.stat().st_size // SECTOR_SIZE, 0
    for name in ("PARAM.SFO", "USRDIR/EBOOT.BIN", "ICON0.PNG"):
        record = image.get_record(iso_path="/PS3_GAME/" + name + ";1")
        lba = record.extent_location()
        first = min(first, lba)
        stop = max(stop, lba + (record.get_data_length() + SECTOR_SIZE - 1) // SECTOR_SIZE)
    image.close()
    data = bytearray(path.read_bytes())
    # Keep a final plaintext run even when the fixture has file data at EOF.
    if stop == len(data) // SECTOR_SIZE:
        data.extend(bytes(SECTOR_SIZE))
    struct.pack_into(">I8xIII", data, 0, 2, first - 1, stop, len(data) // SECTOR_SIZE - 1)
    data[SECTOR_SIZE:SECTOR_SIZE + len(PS3_MARKER)] = PS3_MARKER
    plain = bytes(data)
    cipher_sectors(data, key, first, stop)
    path.write_bytes(data)
    return plain


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.cancel = threading.Event()
        self.logs = []

    def tearDown(self):
        self.temp.cleanup()

    def test_source_snapshot_from_release_without_git(self):
        workspace = self.root / "runtime/setup/workspace"
        listed = ("tools/prepare-game.py", "ps3recomp/LICENSE", "third_party/zlib/LICENSE")
        for name in listed:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(name, encoding="utf-8")
        (self.root / "buzz-paths.json").write_text("private paths", encoding="utf-8")
        manifest = self.root / "release-sources.json"
        manifest.write_text(json.dumps({"files": listed}), encoding="utf-8")
        with patch.object(pipeline.subprocess, "check_output", side_effect=AssertionError("Git was invoked")):
            pipeline.copy_sources(self.root, workspace)
        self.assertEqual({path.relative_to(workspace).as_posix()
                          for path in workspace.rglob("*") if path.is_file()}, set(listed))

        manifest.write_text(json.dumps({"files": ["../outside"]}), encoding="utf-8")
        with self.assertRaises(ValueError):
            pipeline.copy_sources(self.root, workspace)

    def test_publish_distribution_is_local_and_keeps_existing_copy(self):
        workspace = self.root / "workspace"
        exe = workspace / "build/BuzzQuizWorldRecomp.exe"
        game = self.root / "game"
        flash = self.root / "dev_flash"
        elf = self.root / "EBOOT.elf"
        files = {exe: b"game exe", exe.with_name("vcruntime140.dll"): b"dll",
                 game / "PS3_GAME/PARAM.SFO": sfo(), flash / "sys/external/test.sprx": b"firmware",
                 elf: b"elf", workspace / "runtime/dev_hdd0/game/test.edat": b"update"}
        for path, data in files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        paths = {"game_root": str(game), "dev_flash": str(flash), "eboot_elf": str(elf)}
        publish = lambda: pipeline.publish_distribution(self.root, workspace, paths, exe,
                                                         self.logs.append, self.cancel)
        self.cancel.set()
        with self.assertRaises(pipeline.Cancelled):
            publish()
        self.assertFalse((self.root / "dist").exists())
        self.assertEqual(list(self.root.glob(".dist-*")), [])

        self.cancel.clear()
        dist = publish()
        self.assertEqual({str(path.relative_to(dist).as_posix()) for path in dist.rglob("*") if path.is_file()}, {
            exe.name, "vcruntime140.dll", "runtime/EBOOT.elf", "runtime/disc/PS3_GAME/PARAM.SFO",
            "runtime/dev_flash/sys/external/test.sprx", "runtime/dev_hdd0/game/test.edat"})
        with self.assertRaisesRegex(ValueError, "existing distribution"):
            publish()
        self.assertEqual((dist / exe.name).read_bytes(), b"game exe")

    def test_update_content_staging_is_complete_and_does_not_copy_saves(self):
        source, workspace = self.root / "Patched", self.root / "workspace"
        source.mkdir()
        workspace.mkdir()
        for name in pipeline.UPDATE_102_FILES:
            (source / name).write_bytes(b"synthetic update")
        (source / "GLOBAL.EDAT.dec").write_bytes(b"stale plaintext cache")
        (source / "profile.dat").write_bytes(b"not an update asset")
        pipeline.stage_update_content(source, workspace, self.cancel, self.logs.append)
        target = workspace / "runtime/dev_hdd0/game/BCES00647/USRDIR/Patched"
        self.assertEqual(sorted(p.name for p in target.iterdir()), sorted(pipeline.UPDATE_102_FILES))
        pipeline.stage_update_content(source, workspace, self.cancel, self.logs.append)  # safe resume
        (target / "GLOBAL.EDAT").write_bytes(b"user modified")
        with self.assertRaisesRegex(ValueError, "changed"):
            pipeline.stage_update_content(source, workspace, self.cancel, self.logs.append)
        self.assertEqual((target / "GLOBAL.EDAT").read_bytes(), b"user modified")

    def test_missing_update_content_is_rejected_before_any_copy(self):
        source, workspace = self.root / "Patched", self.root / "workspace"
        source.mkdir()
        workspace.mkdir()
        (source / "GLOBAL.EDAT").write_bytes(b"only one asset")
        with self.assertRaisesRegex(ValueError, "complete Patched assets"):
            pipeline.stage_update_content(source, workspace, self.cancel, self.logs.append)
        self.assertEqual(list(workspace.iterdir()), [])

    def test_profiles_do_not_confuse_detected_with_buildable(self):
        self.assertTrue(identify("BCES00647", PROFILES[0].elf_sha256).buildable)
        self.assertTrue(identify("BCES00647", PROFILES[1].elf_sha256).buildable)
        self.assertTrue(PROFILES[1].compiler_options()["NativeSpuFloat"])
        for title in ("BCES00644", "BCES00645"):
            self.assertEqual(identify(title, PROFILES[1].elf_sha256).adapter_family, "quizworld-eu100")
            with self.assertRaises(ValueError):
                identify(title, PROFILES[0].elf_sha256)
        with self.assertRaises(ValueError):
            identify("OTHER00001", PROFILES[0].elf_sha256)

    def test_iso_and_udf_extract_only_game(self):
        for udf, joliet in ((False, False), (True, False), (False, True)):
            image = self.root / f"synthetic-{udf}-{joliet}.iso"
            destination = self.root / f"extract-{udf}-{joliet}"
            make_iso(image, udf=udf, joliet=joliet)
            before = pipeline.fingerprint(image)
            pipeline.extract_iso(image, destination, self.logs.append, self.cancel)
            self.assertEqual((destination / "PS3_GAME/PARAM.SFO").read_bytes(), sfo())
            self.assertEqual((destination / "PS3_GAME/USRDIR/EBOOT.BIN").read_bytes(), b"SCE\0synthetic")
            self.assertEqual(pipeline.fingerprint(image), before)

    def test_catalog_ps3_iso_import_and_offline_resume(self):
        image = self.root / "arbitrary filename.iso"
        key = bytes(range(16))
        elf = b"\x7fELF\x02\x02" + bytes(range(256)) * 30
        icon = b"\x89PNG\r\n\x1a\n" + bytes(range(255)) * 5
        make_encrypted_iso(image, key, elf, icon, udf=True, joliet=True)
        before = image.read_bytes()
        disc = disc_keys.DiscKey("BCES00647", hashlib.sha256(key).hexdigest())
        entries = {(len(before), hashlib.sha1(before).hexdigest()): disc}
        catalog = io.BytesIO(b"ANOTHER00\t" + b"1" * 32 + b"\nBCES00647\t" + key.hex().encode() + b"\n")
        inputs = replace(self.inputs(), game=str(image))
        with patch.object(disc_keys, "DISCS", entries), \
             patch.object(disc_keys, "urlopen", side_effect=[URLError("offline"), catalog]) as download, \
             patch.object(pipeline, "identify", return_value=PROFILES[1]), patch.object(pipeline, "firmware_validator"):
            with self.assertRaisesRegex(ValueError, "Could not reach the PS3 disc key catalog"):
                pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, build=False)
            session, = (self.root / "runtime/setup/sessions").iterdir()
            self.assertFalse((session / "game").exists())
            self.assertEqual(json.loads((session / "state.json").read_text())["done"], [])
            _, state = pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, build=False, resume=session)
            _, resumed = pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, build=False, resume=session)
        self.assertEqual(download.call_count, 2)
        self.assertEqual(download.call_args.args, (disc_keys.CATALOG_URL,))
        self.assertEqual(resumed["done"], state["done"])
        self.assertEqual(state["identities"]["game"], pipeline.fingerprint(image))
        game = Path(state["paths"]["game_root"])
        self.assertEqual((game / "PS3_GAME/PARAM.SFO").read_bytes(), sfo())
        self.assertEqual((game / "PS3_GAME/ICON0.PNG").read_bytes(), icon)
        self.assertEqual(Path(state["paths"]["eboot_elf"]).read_bytes(), elf)
        self.assertEqual(image.read_bytes(), before)
        self.assertNotIn(key.hex(), "\n".join(self.logs))

    def test_ps3_regions_alone_never_trigger_key_download(self):
        image = self.root / "disc.iso"
        elf = b"\x7fELF\x02\x02synthetic"
        key = bytes(range(16))
        plain = make_encrypted_iso(image, key, elf, b"\x89PNG\r\n\x1a\nsynthetic")
        encrypted = image.read_bytes()
        disc = disc_keys.DiscKey("BCES00647", hashlib.sha256(key).hexdigest())
        entries = {(len(encrypted), hashlib.sha1(encrypted).hexdigest()): disc}
        image.write_bytes(encrypted[:-1] + bytes([encrypted[-1] ^ 1]))
        inputs = replace(self.inputs(), game=str(image))
        with patch.object(disc_keys, "DISCS", entries), \
             patch.object(disc_keys, "urlopen", side_effect=AssertionError("unexpected download")), \
             patch.object(pipeline, "identify", return_value=PROFILES[1]), patch.object(pipeline, "firmware_validator"):
            with self.assertRaisesRegex(ValueError, "encrypted or damaged.*do not match"):
                pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, build=False)
            incomplete, = (self.root / "runtime/setup/sessions").iterdir()
            self.assertFalse((incomplete / "game").exists())
            image.write_bytes(plain)
            _, state = pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, build=False)
        self.assertEqual(Path(state["paths"]["eboot_elf"]).read_bytes(), elf)

    def test_catalog_key_and_decrypted_headers_are_checked_before_extraction(self):
        image, output = self.root / "disc.iso", self.root / "output"
        key, wrong = bytes(range(16)), bytes(range(16, 32))
        make_encrypted_iso(image, key, b"\x7fELF\x02\x02synthetic", b"\x89PNG\r\n\x1a\nsynthetic")
        for expected, message in ((key, "No verified disc key"), (wrong, "did not produce a readable EBOOT.BIN")):
            disc = disc_keys.DiscKey("BCES00647", hashlib.sha256(expected).hexdigest())
            catalog = io.BytesIO(b"BCES00647\t" + wrong.hex().encode() + b"\n")
            with patch.object(disc_keys, "urlopen", return_value=catalog):
                with self.assertRaisesRegex(ValueError, message):
                    pipeline.extract_iso(image, output, self.logs.append, self.cancel, disc=disc)
            self.assertFalse(output.exists())

    def test_ps3_sector_reader_crosses_plain_and_encrypted_boundaries(self):
        size = SECTOR_SIZE * 8
        key = bytes(range(16))
        data = bytearray(b"".join(bytes([i]) * SECTOR_SIZE for i in range(8)))
        struct.pack_into(">I8xIIIII", data, 0, 3, 1, 3, 4, 7, 7)
        regions = encrypted_regions(data[:SECTOR_SIZE], size)
        self.assertEqual(regions, ((2, 3), (5, 7)))
        self.assertEqual(encrypted_regions(data[:SECTOR_SIZE], size - SECTOR_SIZE), ())
        plain = bytes(data)
        for first, stop in regions:
            cipher_sectors(data, key, first, stop)
        with io.BufferedReader(_SectorReader(io.BytesIO(data), size, regions, key), SECTOR_SIZE) as stream:
            start = 2 * SECTOR_SIZE - 11
            stream.seek(start)
            self.assertEqual(stream.read(size), plain[start:])
            self.assertEqual(stream.read(16), b"")
            stream.seek(6 * SECTOR_SIZE + 3)
            self.assertEqual(stream.read(16), bytes([6]) * 16)

    def test_iso_rejects_other_title_before_creating_output(self):
        image, destination = self.root / "other.iso", self.root / "out"
        make_iso(image, "BCES00367")
        with self.assertRaises(ValueError):
            pipeline.extract_iso(image, destination, self.logs.append, self.cancel)
        self.assertFalse(destination.exists())

    def test_iso_cancel_and_safety_limits(self):
        image = self.root / "test.iso"
        make_iso(image)
        self.cancel.set()
        with self.assertRaises(pipeline.Cancelled):
            pipeline.extract_iso(image, self.root / "out", self.logs.append, self.cancel)
        self.cancel.clear()
        with patch.object(pipeline, "MAX_GAME_BYTES", 1):
            with self.assertRaisesRegex(ValueError, "safety limits"):
                pipeline.extract_iso(image, self.root / "out", self.logs.append, self.cancel)

    def test_multi_extent_lengths_cycles_and_limits(self):
        def record(size, continuation=None):
            return SimpleNamespace(get_data_length=lambda: size, is_symlink=lambda: False,
                                   data_continuation=continuation)
        tail = record(3)
        head = record(4, tail)
        self.assertEqual(pipeline.iso_file_size(head), 7)
        tail.data_continuation = head
        with self.assertRaisesRegex(ValueError, "Cyclic"):
            pipeline.iso_file_size(head)
        with self.assertRaisesRegex(ValueError, "oversized"):
            pipeline.iso_file_size(record(pipeline.MAX_GAME_BYTES, record(1)))
        with self.assertRaises(ValueError):
            pipeline.iso_file_size(record(-1))

    def test_multi_extent_extraction_and_overrun_are_checked(self):
        payloads = {"PARAM.SFO": sfo(), "USRDIR/EBOOT.BIN": b"SCE\0fixture", "USRDIR/PACK.PAK": b"partEND"}
        class Image:
            fallback_reason = ""
            has_ps3_regions = False
            def open(self, path, *, disc_key): pass
            def close(self): pass
            def has_udf(self): return False
            def has_joliet(self): return False
            def walk(self, **kwargs):
                yield "/PS3_GAME", ["USRDIR"], ["PARAM.SFO"]
                yield "/PS3_GAME/USRDIR", [], ["EBOOT.BIN", "PACK.PAK"]
            def get_record(self, **kwargs):
                name = kwargs["iso_path"].removeprefix("/PS3_GAME/")
                def record(size, continuation=None):
                    return SimpleNamespace(get_data_length=lambda: size, is_symlink=lambda: False,
                                           data_continuation=continuation)
                return record(4, record(3)) if name.endswith("PACK.PAK") else record(len(payloads[name]))
            def get_file_from_iso_fp(self, out, **kwargs):
                out.write(payloads[kwargs["iso_path"].removeprefix("/PS3_GAME/")])
        with patch("launcher.disc_image.DiscImage", Image):
            destination = self.root / "multi"
            pipeline.extract_iso(self.root / "fixture.iso", destination, self.logs.append, self.cancel)
            self.assertEqual((destination / "PS3_GAME/USRDIR/PACK.PAK").read_bytes(), b"partEND")
            payloads["USRDIR/PACK.PAK"] = b"too-many-bytes"
            with self.assertRaisesRegex(ValueError, "exceeds the planned"):
                pipeline.extract_iso(self.root / "fixture.iso", self.root / "overrun", self.logs.append, self.cancel)
    def test_windows_paths(self):
        for name in ("..", "x/y", "x\\y", "NUL", "COM1.dat", "name.", "C:foo", "bad\0name", "line\nname"):
            with self.assertRaises(ValueError):
                pipeline.safe_component(name)
        self.assertEqual(pipeline.safe_component("quiz ą.txt"), "quiz ą.txt")

    def inputs(self):
        game = self.root / "game"
        (game / "PS3_GAME/USRDIR").mkdir(parents=True)
        (game / "PS3_GAME/PARAM.SFO").write_bytes(sfo())
        (game / "PS3_GAME/USRDIR/EBOOT.BIN").write_bytes(b"\x7fELF\x02\x02synthetic")
        flash = self.root / "flash"
        (flash / "sys/external").mkdir(parents=True)
        (flash / "sys/external/flashATRAC.pic").write_bytes(b"synthetic")
        return pipeline.Inputs(str(game), str(flash))

    def test_disc_import_only_retained_without_claiming_a_completed_build(self):
        inputs = self.inputs()
        with patch.object(pipeline, "identify", return_value=PROFILES[1]), patch.object(pipeline, "firmware_validator"):
            pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, build=False)
        states = list((self.root / "runtime/setup/sessions").glob("*/state.json"))
        self.assertEqual(len(states), 1)
        state = json.loads(states[0].read_text())
        self.assertTrue(state["profile"]["buildable"])
        self.assertNotIn("built_exe", state)
        self.assertFalse((states[0].parent / "running.lock").exists())
        self.assertFalse((self.root / "runtime/setup/last-ready.json").exists())

    def test_import_resume_and_changed_input(self):
        inputs = self.inputs()
        with patch.object(pipeline, "identify", return_value=PROFILES[0]), patch.object(pipeline, "firmware_validator"):
            session, state = pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, build=False)
            same, again = pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, resume=session, build=False)
            self.assertEqual(same, session)
            self.assertEqual(again["done"], state["done"])
            Path(inputs.game, "PS3_GAME/USRDIR/EBOOT.BIN").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "Inputs changed"):
                pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, resume=session, build=False)

    def test_resume_cannot_escape_managed_sessions(self):
        inputs = self.inputs()
        with self.assertRaisesRegex(ValueError, "own setup sessions"):
            pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, resume=self.root / "unrelated")

    def test_import_never_launches_an_external_process(self):
        inputs = self.inputs()
        with patch.object(pipeline, "process", side_effect=AssertionError("unexpected external process")), \
             patch.object(pipeline.subprocess, "Popen", side_effect=AssertionError("unexpected subprocess")), \
             patch.object(pipeline, "identify", return_value=PROFILES[1]), patch.object(pipeline, "firmware_validator"):
            _, state = pipeline.prepare(inputs, self.root, self.logs.append, self.cancel, build=False)
        self.assertNotIn("helper", state["inputs"])
        self.assertEqual(state["identities"]["importer"], 1)

    def test_firmware_install_uses_in_process_importer(self):
        inputs = self.inputs()
        home = self.root / "private-import"
        with patch("launcher.ps3_import.extract_firmware") as extract:
            pipeline.install_firmware(inputs, home, self.logs.append, self.cancel)
            self.assertEqual(extract.call_args.args[:2], (inputs.firmware, home))
            self.cancel.set()
            with self.assertRaises(pipeline.Cancelled):
                extract.call_args.args[-1]()

    def test_hash_cancel_is_responsive_and_input_is_unchanged(self):
        source = self.root / "data"
        source.write_bytes(b"synthetic")
        self.cancel.set()
        with self.assertRaises(pipeline.Cancelled):
            pipeline.fingerprint(source, self.cancel)
        self.assertEqual(source.read_bytes(), b"synthetic")


if __name__ == "__main__":
    unittest.main()
