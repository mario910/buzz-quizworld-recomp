"""Synthetic ISO/SELF/process fixtures: no games, real emulator or UI input."""
from dataclasses import replace
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

import pycdlib
from launcher import setup_pipeline as pipeline
from launcher.game_profiles import PROFILES, identify


def sfo(title="BCES00647"):
    key, value = b"TITLE_ID\0", title.encode() + b"\0"
    return (b"\0PSF" + struct.pack("<IIII", 0x101, 36, 36 + len(key), 1)
            + struct.pack("<HHIII", 0, 0x204, len(value), len(value), 0) + key + value)


def make_iso(path, title="BCES00647", udf=False, joliet=False):
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
    for name, data in (("PARAM.SFO", sfo(title)), ("USRDIR/EBOOT.BIN", b"SCE\0synthetic")):
        buffer = io.BytesIO(data)
        buffers.append(buffer)
        image.add_fp(buffer, len(data), iso_path="/PS3_GAME/" + name + ";1",
                     **({"udf_path": "/PS3_GAME/" + name} if udf else {}),
                     **({"joliet_path": "/PS3_GAME/" + name + ";1"} if joliet else {}))
    image.write(str(path))
    image.close()


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.cancel = threading.Event()
        self.logs = []

    def tearDown(self):
        self.temp.cleanup()

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
            def open(self, path): pass
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
        helper = self.root / "helper.exe"
        helper.write_bytes(b"not executed")
        return pipeline.Inputs(str(game), str(flash), str(helper))

    def test_disc_import_only_retained_without_claiming_a_completed_build(self):
        inputs = self.inputs()
        with patch.object(pipeline, "identify", return_value=PROFILES[1]):
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
        with patch.object(pipeline, "identify", return_value=PROFILES[0]):
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

    def test_helper_is_headless_isolated_and_has_closed_stdin_runner(self):
        inputs = self.inputs()
        with patch.object(pipeline, "process") as run:
            pipeline.helper(inputs, ["--decrypt", "X.elf"], self.root / "private helper", self.logs.append, self.cancel)
            args, kwargs = run.call_args
            self.assertEqual(args[0][:2], [inputs.helper, "--headless"])
            self.assertTrue(kwargs["env"]["RPCS3_CONFIG_DIR"].endswith(("/", "\\")))
            self.assertEqual(Path(kwargs["env"]["RPCS3_CONFIG_DIR"]), (self.root / "private helper").resolve())

    def test_only_verified_firmware_teardown_failure_is_tolerated(self):
        inputs = self.inputs()
        home = self.root / "private-helper"
        (home / "log").mkdir(parents=True)
        log = home / "failed.log"
        log.write_text("manual_typemap process teardown")
        (home / "log/RPCS3.log").write_text("Successfully installed PS3 firmware version 4.93.")
        failure = pipeline.ProcessFailure(0xC0000409, log)
        with patch.object(pipeline, "helper", side_effect=failure), patch.object(pipeline, "firmware_validator") as validate:
            pipeline.install_firmware(inputs, home, self.logs.append, self.cancel)
            validate.assert_called_once_with(home / "dev_flash")
        self.assertTrue(any("WARNING" in item for item in self.logs))
        with patch.object(pipeline, "helper", side_effect=failure), patch.object(pipeline, "firmware_validator", side_effect=ValueError("bad firmware")):
            with self.assertRaisesRegex(ValueError, "bad firmware"):
                pipeline.install_firmware(inputs, home, self.logs.append, self.cancel)
        with patch.object(pipeline, "helper", side_effect=pipeline.ProcessFailure(1, log)):
            with self.assertRaises(pipeline.ProcessFailure):
                pipeline.install_firmware(inputs, home, self.logs.append, self.cancel)

    def test_hash_cancel_is_responsive_and_input_is_unchanged(self):
        source = self.root / "data"
        source.write_bytes(b"synthetic")
        self.cancel.set()
        with self.assertRaises(pipeline.Cancelled):
            pipeline.fingerprint(source, self.cancel)
        self.assertEqual(source.read_bytes(), b"synthetic")


if __name__ == "__main__":
    unittest.main()
